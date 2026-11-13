#include "FormatDocPrinter.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "FormatUnicode.h"

namespace ArgpiSuite::Format {
namespace {

enum class Mode : std::uint8_t { Flat, Break };

struct Indent {
    std::string value;
    int length = 0;
};

struct Command {
    Indent ind;
    Mode mode = Mode::Break;
    DocId doc = kNoDoc;
    std::size_t fillOffset = 0;
};

constexpr DocId kSyntheticLineBreak = -2;

Indent MakeIndent(const Indent& ind, const DocPrintOptions& options) {
    Indent next = ind;
    if (options.useTabs) {
        next.value.push_back('\t');
    } else {
        next.value.append(static_cast<std::size_t>(options.tabWidth), ' ');
    }
    next.length = ind.length + options.tabWidth;
    return next;
}

Indent MakeAlign(const Indent& ind, int width) {
    Indent next = ind;
    if (width > 0) {
        next.value.append(static_cast<std::size_t>(width), ' ');
        next.length = ind.length + width;
    }
    return next;
}

Indent MakeDedent(const Indent& ind, const DocPrintOptions& options) {
    Indent next = ind;
    int remove = options.tabWidth;
    if (options.useTabs && !next.value.empty() && next.value.back() == '\t') {
        next.value.pop_back();
    } else {
        while (remove > 0 && !next.value.empty() &&
               (next.value.back() == ' ' || next.value.back() == '\t')) {
            next.value.pop_back();
            --remove;
        }
    }
    next.length = ind.length > options.tabWidth ? ind.length - options.tabWidth : 0;
    return next;
}

int TrimTrailing(std::string& out) {
    std::size_t count = 0;
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
        ++count;
    }
    return static_cast<int>(count);
}

void AppendText(std::string& out, const std::string& text, int& pos) {
    out.append(text);
    const std::size_t lastBreak = text.find_last_of("\r\n");
    if (lastBreak == std::string::npos) {
        pos += DisplayWidth(text);
    } else {
        pos = DisplayWidth(std::string_view(text).substr(lastBreak + 1));
    }
}

bool ComputeBreaks(const DocArena& arena, DocId id, std::vector<std::uint8_t>& breaks,
                   std::vector<std::uint8_t>& state) {
    if (id < 0) {
        return false;
    }
    const auto index = static_cast<std::size_t>(id);
    if (index >= breaks.size()) {
        return false;
    }
    if (state[index] == 2) {
        return breaks[index] != 0;
    }
    if (state[index] == 1) {
        return false;  // cycle guard (docs are trees in practice)
    }
    state[index] = 1;
    const DocNode& node = arena.Get(id);
    bool childBreaks = false;
    switch (node.kind) {
        case DocKind::BreakParent:
        case DocKind::HardLine:
        case DocKind::LiteralLine:
            childBreaks = true;
            break;
        case DocKind::Group: {
            bool inner = node.shouldBreak;
            if (!node.expandedStates.empty()) {
                for (DocId part : node.expandedStates) {
                    inner = ComputeBreaks(arena, part, breaks, state) || inner;
                }
            } else {
                inner = ComputeBreaks(arena, node.contents, breaks, state) || inner;
            }
            breaks[index] = inner ? 1 : 0;
            state[index] = 2;
            return inner;
        }
        default:
            // Raw text with embedded newlines (multi-line comments / string
            // continuations) behaves like a hard line for layout purposes.
            if (node.kind == DocKind::Text &&
                (node.text.find('\n') != std::string::npos ||
                 node.text.find('\r') != std::string::npos)) {
                childBreaks = true;
                break;
            }
            if (node.contents != kNoDoc) {
                childBreaks = ComputeBreaks(arena, node.contents, breaks, state);
            }
            for (DocId part : node.parts) {
                childBreaks = ComputeBreaks(arena, part, breaks, state) || childBreaks;
            }
            if (node.breakContents != kNoDoc) {
                childBreaks = ComputeBreaks(arena, node.breakContents, breaks, state) || childBreaks;
            }
            if (node.flatContents != kNoDoc) {
                childBreaks = ComputeBreaks(arena, node.flatContents, breaks, state) || childBreaks;
            }
            break;
    }
    breaks[index] = childBreaks ? 1 : 0;
    state[index] = 2;
    return childBreaks;
}

bool Fits(const DocArena& arena, const std::vector<Command>& rest,
          const std::vector<Command>& seed, int width, bool hasLineSuffix,
          const std::vector<std::int8_t>& groupModes, const std::vector<std::uint8_t>& breaks) {
    if (width < 0) {
        return false;
    }
    std::size_t restIndex = rest.size();
    std::vector<Command> commands;
    commands.reserve(seed.size() + 8);
    for (auto it = seed.rbegin(); it != seed.rend(); ++it) {
        commands.push_back(*it);
    }
    std::string out;
    std::size_t steps = 0;

    while (width >= 0) {
        if (commands.empty()) {
            if (restIndex == 0) {
                return true;
            }
            --restIndex;
            commands.push_back(rest[restIndex]);
            continue;
        }
        if (++steps > 1000000U) {
            return false;
        }
        const Command cmd = commands.back();
        commands.pop_back();
        const DocNode& node = arena.Get(cmd.doc);
        switch (node.kind) {
            case DocKind::Text:
                if (hasLineSuffix && node.text.find('\n') == std::string::npos) {
                    return false;
                }
                width -= DisplayWidth(node.text);
                break;
            case DocKind::Concat:
                for (auto it = node.parts.rbegin(); it != node.parts.rend(); ++it) {
                    commands.push_back(Command{cmd.ind, cmd.mode, *it, 0});
                }
                break;
            case DocKind::Cursor:
                break;
            case DocKind::Indent:
            case DocKind::Align:
            case DocKind::Dedent:
                commands.push_back(Command{cmd.ind, cmd.mode, node.contents, 0});
                break;
            case DocKind::Trim:
                width += TrimTrailing(out);
                break;
            case DocKind::Group: {
                if (node.shouldBreak) {
                    return false;
                }
                const bool broken =
                    cmd.doc >= 0 && static_cast<std::size_t>(cmd.doc) < breaks.size() &&
                    breaks[static_cast<std::size_t>(cmd.doc)] != 0;
                const Mode groupMode = broken ? Mode::Break : cmd.mode;
                DocId contents = node.contents;
                if (groupMode == Mode::Break && !node.expandedStates.empty()) {
                    contents = node.expandedStates.back();
                }
                commands.push_back(Command{cmd.ind, groupMode, contents, 0});
                break;
            }
            case DocKind::Fill:
                for (std::size_t i = node.parts.size(); i > cmd.fillOffset; --i) {
                    commands.push_back(Command{cmd.ind, cmd.mode, node.parts[i - 1], 0});
                }
                break;
            case DocKind::IfBreak: {
                std::int8_t groupMode = -1;
                if (node.groupId >= 0 &&
                    static_cast<std::size_t>(node.groupId) < groupModes.size()) {
                    groupMode = groupModes[static_cast<std::size_t>(node.groupId)];
                }
                const Mode effective =
                    groupMode >= 0 ? (groupMode == 1 ? Mode::Break : Mode::Flat) : cmd.mode;
                const DocId selected =
                    effective == Mode::Break ? node.breakContents : node.flatContents;
                if (selected != kNoDoc) {
                    commands.push_back(Command{cmd.ind, cmd.mode, selected, 0});
                }
                break;
            }
            case DocKind::Line:
            case DocKind::SoftLine:
            case DocKind::HardLine:
            case DocKind::LiteralLine:
                if (cmd.mode == Mode::Break) {
                    return true;
                }
                if (node.kind == DocKind::Line) {
                    width -= 1;
                } else if (node.kind == DocKind::HardLine ||
                           node.kind == DocKind::LiteralLine) {
                    return true;
                }
                break;
            case DocKind::LineSuffix:
                hasLineSuffix = true;
                break;
            case DocKind::LineSuffixBoundary:
                if (hasLineSuffix) {
                    return false;
                }
                break;
            case DocKind::BreakParent:
                break;
        }
    }
    return false;
}

}  // namespace

DocPrintResult PrintDoc(const DocArena& arena, DocId root, const DocPrintOptions& options,
                        const std::atomic_bool* cancel) {
    DocPrintResult result;
    if (root == kNoDoc) {
        return result;
    }

    std::vector<std::uint8_t> breaks(arena.Size(), 0);
    std::vector<std::uint8_t> state(arena.Size(), 0);
    ComputeBreaks(arena, root, breaks, state);

    const DocPrintOptions opts = options.printWidth > 0 ? options : DocPrintOptions{};

    std::vector<Command> commands;
    commands.push_back(Command{Indent{}, Mode::Break, root, 0});
    std::vector<std::int8_t> groupModes(arena.Size(), -1);
    std::vector<DocId> lineSuffixes;
    std::string out;
    int pos = 0;
    bool shouldRemeasure = false;
    std::size_t steps = 0;
    const std::string newline = EolBytes(opts.endOfLine)[0] != '\0'
                                    ? std::string(EolBytes(opts.endOfLine))
                                    : std::string("\n");

    auto cancelRequested = [&]() {
        return cancel != nullptr && cancel->load(std::memory_order_relaxed);
    };

    auto processLine = [&](const Indent& ind, bool literal) {
        if (literal) {
            out.append(newline);
            pos = 0;
            return;
        }
        pos -= TrimTrailing(out);
        out.append(newline);
        out.append(ind.value);
        pos = ind.length;
    };

    while (!commands.empty()) {
        if ((++steps & 0x3FFFU) == 0 && cancelRequested()) {
            result.canceled = true;
            return result;
        }
        Command cmd = commands.back();
        commands.pop_back();

        if (cmd.doc == kSyntheticLineBreak) {
            processLine(cmd.ind, false);
            continue;
        }

        const DocNode& node = arena.Get(cmd.doc);
        switch (node.kind) {
            case DocKind::Text:
                AppendText(out, node.text, pos);
                break;
            case DocKind::Concat:
                for (auto it = node.parts.rbegin(); it != node.parts.rend(); ++it) {
                    commands.push_back(Command{cmd.ind, cmd.mode, *it, 0});
                }
                break;
            case DocKind::Cursor:
                result.cursorPosition = out.size();
                break;
            case DocKind::Indent:
                commands.push_back(Command{MakeIndent(cmd.ind, opts), cmd.mode, node.contents, 0});
                break;
            case DocKind::Align:
                commands.push_back(
                    Command{MakeAlign(cmd.ind, node.alignWidth), cmd.mode, node.contents, 0});
                break;
            case DocKind::Dedent:
                commands.push_back(Command{MakeDedent(cmd.ind, opts), cmd.mode, node.contents, 0});
                break;
            case DocKind::Group: {
                const bool broken =
                    static_cast<std::size_t>(cmd.doc) < breaks.size() &&
                    breaks[static_cast<std::size_t>(cmd.doc)] != 0;
                bool pushed = false;

                if (cmd.mode == Mode::Flat && !shouldRemeasure) {
                    commands.push_back(
                        Command{cmd.ind, broken ? Mode::Break : Mode::Flat, node.contents, 0});
                    pushed = true;
                }

                if (!pushed) {
                    shouldRemeasure = false;
                    const Command flat{cmd.ind, Mode::Flat, node.contents, 0};
                    const int remaining = opts.printWidth - pos;
                    if (!broken &&
                        Fits(arena, commands, std::vector<Command>{flat}, remaining,
                             !lineSuffixes.empty(), groupModes, breaks)) {
                        commands.push_back(flat);
                        pushed = true;
                    } else if (!node.expandedStates.empty()) {
                        if (broken) {
                            commands.push_back(Command{cmd.ind, Mode::Break,
                                                       node.expandedStates.back(), 0});
                        } else {
                            bool selected = false;
                            for (std::size_t i = 1; i < node.expandedStates.size(); ++i) {
                                const Command candidate{cmd.ind, Mode::Flat,
                                                        node.expandedStates[i], 0};
                                if (Fits(arena, commands, std::vector<Command>{candidate},
                                         remaining, !lineSuffixes.empty(), groupModes, breaks)) {
                                    commands.push_back(candidate);
                                    selected = true;
                                    break;
                                }
                            }
                            if (!selected) {
                                commands.push_back(Command{cmd.ind, Mode::Break,
                                                           node.expandedStates.back(), 0});
                            }
                        }
                        pushed = true;
                    } else {
                        commands.push_back(Command{cmd.ind, Mode::Break, node.contents, 0});
                        pushed = true;
                    }
                }
                (void)pushed;
                if (!commands.empty()) {
                    groupModes[static_cast<std::size_t>(cmd.doc)] =
                        commands.back().mode == Mode::Break ? 1 : 0;
                }
                break;
            }
            case DocKind::Fill: {
                if (node.parts.size() <= cmd.fillOffset) {
                    break;
                }
                const int remaining = opts.printWidth - pos;
                const std::size_t index = cmd.fillOffset;
                const DocId content = node.parts[index];
                const Command contentFlat{cmd.ind, Mode::Flat, content, 0};
                const Command contentBreak{cmd.ind, Mode::Break, content, 0};
                const bool contentFits =
                    Fits(arena, {}, std::vector<Command>{contentFlat}, remaining,
                         !lineSuffixes.empty(), groupModes, breaks);
                if (node.parts.size() == index + 1) {
                    commands.push_back(contentFits ? contentFlat : contentBreak);
                    break;
                }
                const DocId whitespace = node.parts[index + 1];
                const Command whitespaceFlat{cmd.ind, Mode::Flat, whitespace, 0};
                const Command whitespaceBreak{cmd.ind, Mode::Break, whitespace, 0};
                if (node.parts.size() == index + 2) {
                    if (contentFits) {
                        commands.push_back(whitespaceFlat);
                        commands.push_back(contentFlat);
                    } else {
                        commands.push_back(whitespaceBreak);
                        commands.push_back(contentBreak);
                    }
                    break;
                }
                const Command remainingFill{cmd.ind, cmd.mode, cmd.doc, index + 2};
                const DocId secondContent = node.parts[index + 2];
                const std::vector<Command> firstAndSecond{
                    Command{cmd.ind, Mode::Flat, content, 0},
                    Command{cmd.ind, Mode::Flat, whitespace, 0},
                    Command{cmd.ind, Mode::Flat, secondContent, 0}};
                const bool firstAndSecondFits =
                    Fits(arena, {}, firstAndSecond, remaining, !lineSuffixes.empty(), groupModes,
                         breaks);
                if (firstAndSecondFits) {
                    commands.push_back(remainingFill);
                    commands.push_back(whitespaceFlat);
                    commands.push_back(contentFlat);
                } else if (contentFits) {
                    commands.push_back(remainingFill);
                    commands.push_back(whitespaceBreak);
                    commands.push_back(contentFlat);
                } else {
                    commands.push_back(remainingFill);
                    commands.push_back(whitespaceBreak);
                    commands.push_back(contentBreak);
                }
                break;
            }
            case DocKind::IfBreak: {
                std::int8_t groupMode = -1;
                if (node.groupId >= 0 &&
                    static_cast<std::size_t>(node.groupId) < groupModes.size()) {
                    groupMode = groupModes[static_cast<std::size_t>(node.groupId)];
                }
                const Mode effective =
                    groupMode >= 0 ? (groupMode == 1 ? Mode::Break : Mode::Flat) : cmd.mode;
                const DocId selected =
                    effective == Mode::Break ? node.breakContents : node.flatContents;
                if (selected != kNoDoc) {
                    commands.push_back(Command{cmd.ind, cmd.mode, selected, 0});
                }
                break;
            }
            case DocKind::Line:
            case DocKind::SoftLine:
            case DocKind::HardLine:
            case DocKind::LiteralLine: {
                const bool hard = node.kind == DocKind::HardLine ||
                                  node.kind == DocKind::LiteralLine;
                if (cmd.mode == Mode::Flat && !hard) {
                    if (node.kind == DocKind::Line) {
                        out.push_back(' ');
                        ++pos;
                    }
                    break;
                }
                shouldRemeasure = true;
                if (!lineSuffixes.empty()) {
                    commands.push_back(cmd);
                    for (auto it = lineSuffixes.rbegin(); it != lineSuffixes.rend(); ++it) {
                        commands.push_back(Command{cmd.ind, cmd.mode, *it, 0});
                    }
                    lineSuffixes.clear();
                    break;
                }
                processLine(cmd.ind, node.kind == DocKind::LiteralLine);
                break;
            }
            case DocKind::LineSuffix:
                lineSuffixes.push_back(node.contents);
                break;
            case DocKind::LineSuffixBoundary:
                if (!lineSuffixes.empty()) {
                    commands.push_back(Command{cmd.ind, cmd.mode, kSyntheticLineBreak, 0});
                    for (auto it = lineSuffixes.rbegin(); it != lineSuffixes.rend(); ++it) {
                        commands.push_back(Command{cmd.ind, cmd.mode, *it, 0});
                    }
                    lineSuffixes.clear();
                }
                break;
            case DocKind::BreakParent:
                break;
            case DocKind::Trim:
                pos -= TrimTrailing(out);
                break;
        }
    }

    // Flush any remaining line suffixes at end of output.
    if (!lineSuffixes.empty()) {
        for (DocId suffix : lineSuffixes) {
            const DocNode& node = arena.Get(suffix);
            if (node.kind == DocKind::Text) {
                AppendText(out, node.text, pos);
            }
        }
    }

    result.text = std::move(out);
    return result;
}

}  // namespace ArgpiSuite::Format

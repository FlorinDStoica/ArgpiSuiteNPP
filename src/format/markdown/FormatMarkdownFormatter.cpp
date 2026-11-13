#include "FormatMarkdownFormatter.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "FormatLanguage.h"

namespace ArgpiSuite::Format {
namespace {

bool IsCanceled(const std::atomic_bool* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

std::string_view TrimRight(std::string_view sv) {
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}

std::string_view TrimBoth(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
        sv.remove_prefix(1);
    }
    return TrimRight(sv);
}

// Trims trailing whitespace while preserving an intentional 2-space Markdown hard break.
std::string TrimMarkdownTrailing(std::string_view line) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    std::size_t end = line.size();
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
        --end;
    }
    const std::size_t trailingSpaces = line.size() - end;
    std::string out(line.substr(0, end));
    if (end > 0 && trailingSpaces >= 2 && line[end] == ' ' && line[end + 1] == ' ') {
        out.append("  ");
    }
    return out;
}

std::string LowerAscii(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

Language LanguageFromFenceInfo(std::string_view info) {
    const std::string_view trimmed = TrimBoth(info);
    std::size_t end = 0;
    while (end < trimmed.size() && trimmed[end] != ' ' && trimmed[end] != '\t') {
        ++end;
    }
    const std::string tag = LowerAscii(trimmed.substr(0, end));
    if (tag == "json") return Language::Json;
    if (tag == "json5") return Language::Json5;
    if (tag == "jsonc") return Language::Jsonc;
    if (tag == "js" || tag == "javascript") return Language::JavaScript;
    if (tag == "ts" || tag == "typescript") return Language::TypeScript;
    if (tag == "jsx") return Language::Jsx;
    if (tag == "tsx") return Language::Tsx;
    if (tag == "css") return Language::Css;
    if (tag == "scss") return Language::Scss;
    if (tag == "less") return Language::Less;
    if (tag == "html") return Language::Html;
    if (tag == "xml") return Language::Xml;
    if (tag == "yaml" || tag == "yml") return Language::Yaml;
    if (tag == "c") return Language::C;
    if (tag == "cpp" || tag == "c++") return Language::Cpp;
    return Language::Unknown;
}

bool ParseFenceStart(std::string_view line, char& fenceChar, std::size_t& fenceLen,
                     std::string_view& info) {
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') {
        ++i;
    }
    if (i >= line.size() || (line[i] != '`' && line[i] != '~')) {
        return false;
    }
    const char ch = line[i];
    std::size_t count = 0;
    while (i + count < line.size() && line[i + count] == ch) {
        ++count;
    }
    if (count < 3) {
        return false;
    }
    const std::string_view rest = line.substr(i + count);
    if (ch == '`' && rest.find('`') != std::string_view::npos) {
        return false;
    }
    fenceChar = ch;
    fenceLen = count;
    info = TrimBoth(rest);
    return true;
}

bool IsFenceClose(std::string_view line, char fenceChar, std::size_t minLen) {
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') {
        ++i;
    }
    std::size_t count = 0;
    while (i + count < line.size() && line[i + count] == fenceChar) {
        ++count;
    }
    if (count < minLen) {
        return false;
    }
    return TrimBoth(line.substr(i + count)).empty();
}

bool TryFormatAtxHeading(std::string_view line, std::string& out) {
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') {
        ++i;
    }
    std::size_t hashes = 0;
    while (i + hashes < line.size() && line[i + hashes] == '#') {
        ++hashes;
    }
    if (hashes == 0 || hashes > 6) {
        return false;
    }
    if (i + hashes < line.size() && line[i + hashes] != ' ' && line[i + hashes] != '\t') {
        return false;
    }
    const std::string_view title = TrimBoth(line.substr(i + hashes));
    out.assign(hashes, '#');
    if (!title.empty()) {
        out.push_back(' ');
        out.append(title);
    }
    return true;
}

bool TryFormatBlockquote(std::string_view line, std::string& out) {
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') {
        ++i;
    }
    if (i >= line.size() || line[i] != '>') {
        return false;
    }
    out.clear();
    while (i < line.size()) {
        if (line[i] == '>') {
            out.push_back('>');
            ++i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                ++i;
            }
            if (i < line.size() && line[i] != '>') {
                out.push_back(' ');
                break;
            }
        } else {
            break;
        }
    }
    out.append(TrimMarkdownTrailing(line.substr(i)));
    return true;
}

bool TryFormatListItem(std::string_view line, std::string& out) {
    std::size_t indent = 0;
    while (indent < line.size() && line[indent] == ' ') {
        ++indent;
    }
    if (indent >= line.size()) {
        return false;
    }
    // Unordered: `- `, `* `, `+ `
    if ((line[indent] == '-' || line[indent] == '*' || line[indent] == '+') &&
        indent + 1 < line.size() && (line[indent + 1] == ' ' || line[indent + 1] == '\t')) {
        std::size_t textPos = indent + 1;
        while (textPos < line.size() && (line[textPos] == ' ' || line[textPos] == '\t')) {
            ++textPos;
        }
        out.assign(indent, ' ');
        out.push_back(line[indent]);
        out.push_back(' ');
        out.append(TrimMarkdownTrailing(line.substr(textPos)));
        return true;
    }
    // Ordered: `1. `, `2) `
    std::size_t digits = indent;
    while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits]))) {
        ++digits;
    }
    if (digits > indent && digits + 1 < line.size() &&
        (line[digits] == '.' || line[digits] == ')') &&
        (line[digits + 1] == ' ' || line[digits + 1] == '\t')) {
        std::size_t textPos = digits + 1;
        while (textPos < line.size() && (line[textPos] == ' ' || line[textPos] == '\t')) {
            ++textPos;
        }
        out.assign(line.substr(0, digits + 1));
        out.push_back(' ');
        out.append(TrimMarkdownTrailing(line.substr(textPos)));
        return true;
    }
    return false;
}

std::vector<std::string> SplitPipeRow(std::string_view line) {
    std::string_view s = TrimBoth(line);
    if (!s.empty() && s.front() == '|') {
        s.remove_prefix(1);
    }
    if (!s.empty() && s.back() == '|') {
        s.remove_suffix(1);
    }
    std::vector<std::string> cells;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || (s[i] == '|' && (i == 0 || s[i - 1] != '\\'))) {
            cells.push_back(std::string(TrimBoth(s.substr(start, i - start))));
            start = i + 1;
        }
    }
    return cells;
}

bool IsTableSeparatorRow(const std::vector<std::string>& cells) {
    if (cells.empty()) {
        return false;
    }
    for (const std::string& cell : cells) {
        if (cell.size() < 3) {
            return false;
        }
        std::size_t start = 0;
        std::size_t end = cell.size();
        if (cell[start] == ':') ++start;
        if (end > start && cell[end - 1] == ':') --end;
        if (end <= start) {
            return false;
        }
        for (std::size_t k = start; k < end; ++k) {
            if (cell[k] != '-') {
                return false;
            }
        }
    }
    return true;
}

bool FormatMarkdownInternal(std::string_view input, const FormatOptions& options,
                            const std::atomic_bool* cancel, std::string& out) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < input.size()) {
        std::size_t nl = input.find('\n', pos);
        if (nl == std::string_view::npos) {
            nl = input.size();
        }
        std::string_view line = input.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        pos = (nl < input.size()) ? nl + 1 : input.size();
    }

    out.clear();
    out.reserve(input.size() + 64);

    bool inFence = false;
    char fenceChar = '`';
    std::size_t fenceLen = 3;
    Language fenceLang = Language::Unknown;
    std::string fenceBuffer;
    int consecutiveBlanks = 0;

    for (std::size_t idx = 0; idx < lines.size(); ++idx) {
        if ((idx & 1023) == 0 && IsCanceled(cancel)) {
            return false;
        }

        const std::string_view rawLine = lines[idx];

        if (inFence) {
            if (IsFenceClose(rawLine, fenceChar, fenceLen)) {
                if (fenceLang != Language::Unknown && IsLanguageImplemented(fenceLang) &&
                    !TrimBoth(fenceBuffer).empty()) {
                    FormatOptions innerOpts = options;
                    innerOpts.endOfLine = Eol::Lf;
                    const FormatResult innerRes =
                        FormatText(fenceBuffer, fenceLang, innerOpts, cancel);
                    if (innerRes.canceled) {
                        return false;
                    }
                    if (innerRes.ok && !innerRes.output.empty()) {
                        out.append(innerRes.output);
                        if (out.back() != '\n') {
                            out.push_back('\n');
                        }
                    } else {
                        out.append(fenceBuffer);
                    }
                } else {
                    out.append(fenceBuffer);
                }
                fenceBuffer.clear();
                out.append(fenceLen, fenceChar);
                out.push_back('\n');
                inFence = false;
                consecutiveBlanks = 0;
            } else {
                fenceBuffer.append(rawLine);
                fenceBuffer.push_back('\n');
            }
            continue;
        }

        std::string_view info;
        if (ParseFenceStart(rawLine, fenceChar, fenceLen, info)) {
            inFence = true;
            fenceLang = LanguageFromFenceInfo(info);
            fenceBuffer.clear();
            out.append(fenceLen, fenceChar);
            if (!info.empty()) {
                out.append(info);
            }
            out.push_back('\n');
            consecutiveBlanks = 0;
            continue;
        }

        // GFM pipe table block detection (header + separator + 0..N rows).
        if (TrimBoth(rawLine).find('|') != std::string_view::npos && idx + 1 < lines.size()) {
            const std::vector<std::string> headerCells = SplitPipeRow(rawLine);
            const std::vector<std::string> sepCells = SplitPipeRow(lines[idx + 1]);
            if (headerCells.size() == sepCells.size() && IsTableSeparatorRow(sepCells)) {
                std::vector<std::vector<std::string>> rows;
                rows.push_back(headerCells);
                rows.push_back(sepCells);
                std::size_t rowEnd = idx + 2;
                while (rowEnd < lines.size()) {
                    const std::string_view candidate = TrimBoth(lines[rowEnd]);
                    if (candidate.empty() || candidate.find('|') == std::string_view::npos) {
                        break;
                    }
                    std::vector<std::string> cells = SplitPipeRow(candidate);
                    cells.resize(headerCells.size());
                    rows.push_back(std::move(cells));
                    ++rowEnd;
                }

                std::vector<std::size_t> colWidths(headerCells.size(), 3);
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    if (r == 1) continue;
                    for (std::size_t c = 0; c < headerCells.size(); ++c) {
                        colWidths[c] = std::max(colWidths[c], rows[r][c].size());
                    }
                }

                for (std::size_t r = 0; r < rows.size(); ++r) {
                    out.push_back('|');
                    for (std::size_t c = 0; c < headerCells.size(); ++c) {
                        out.push_back(' ');
                        if (r == 1) {
                            const std::string& sep = rows[r][c];
                            const bool leftColon = !sep.empty() && sep.front() == ':';
                            const bool rightColon = sep.size() >= 2 && sep.back() == ':';
                            std::size_t dashes = colWidths[c];
                            if (leftColon) {
                                out.push_back(':');
                                if (dashes > 0) --dashes;
                            }
                            if (rightColon && dashes > 0) {
                                --dashes;
                            }
                            out.append(std::max<std::size_t>(1, dashes), '-');
                            if (rightColon) {
                                out.push_back(':');
                            }
                        } else {
                            out.append(rows[r][c]);
                            if (rows[r][c].size() < colWidths[c]) {
                                out.append(colWidths[c] - rows[r][c].size(), ' ');
                            }
                        }
                        out.append(" |");
                    }
                    out.push_back('\n');
                }
                idx = rowEnd - 1;
                consecutiveBlanks = 0;
                continue;
            }
        }

        const std::string trimmedLine = TrimMarkdownTrailing(rawLine);
        if (trimmedLine.empty()) {
            if (!out.empty() && consecutiveBlanks == 0) {
                ++consecutiveBlanks;
                out.push_back('\n');
            }
            continue;
        }
        consecutiveBlanks = 0;

        std::string formattedLine;
        if (TryFormatAtxHeading(trimmedLine, formattedLine)) {
            if (!out.empty() && out.back() == '\n' &&
                (out.size() < 2 || out[out.size() - 2] != '\n')) {
                out.push_back('\n');
            }
            out.append(formattedLine);
            out.push_back('\n');
            continue;
        }
        if (TryFormatBlockquote(trimmedLine, formattedLine) ||
            TryFormatListItem(trimmedLine, formattedLine)) {
            out.append(formattedLine);
            out.push_back('\n');
            continue;
        }

        out.append(trimmedLine);
        out.push_back('\n');
    }

    if (inFence) {
        out.append(fenceBuffer);
    }

    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') {
        out.pop_back();
    }
    return true;
}

}  // namespace

FormatResult FormatMarkdownLanguage(std::string_view input, const FormatOptions& options,
                                    const std::atomic_bool* cancel) {
    FormatResult result;
    result.language = Language::Markdown;

    std::string formatted;
    if (!FormatMarkdownInternal(input, options, cancel, formatted)) {
        result.canceled = true;
        return result;
    }

    std::string second;
    if (FormatMarkdownInternal(formatted, options, cancel, second) && second != formatted) {
        result.idempotent = false;
    }

    result.output = std::move(formatted);
    result.ok = true;
    return result;
}

}  // namespace ArgpiSuite::Format


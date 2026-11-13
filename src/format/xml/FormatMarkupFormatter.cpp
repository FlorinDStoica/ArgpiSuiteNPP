#include "FormatMarkupFormatter.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "brace/FormatBraceFormatter.h"

namespace ArgpiSuite::Format {
namespace {

enum class MarkupKind {
    Declaration,  // <?xml ...?>, <!DOCTYPE ...>
    Comment,      // <!-- ... -->
    CData,        // <![CDATA[ ... ]]>
    PhpBlock,     // <?php ... ?> or <?= ... ?>
    OpenTag,      // <tag ...>
    CloseTag,     // </tag>
    SelfCloseTag, // <tag .../>
    RawBlock,     // inner text of <script>, <style>, <pre>, <textarea>
    Text,
};

struct MarkupNode {
    MarkupKind kind = MarkupKind::Text;
    std::string tagName;  // lowercased tag name for tag nodes
    std::string raw;
};

bool IsCanceled(const std::atomic_bool* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

std::string TrimView(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() &&
           (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' ||
            text[start] == '\n')) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
            text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

std::string MakeIndent(int depth, const FormatOptions& options) {
    if (depth <= 0) {
        return {};
    }
    if (options.useTabs) {
        return std::string(static_cast<std::size_t>(depth), '\t');
    }
    return std::string(static_cast<std::size_t>(depth * std::max(1, options.tabWidth)), ' ');
}

bool IsHtmlVoidTag(std::string_view lowerTag) {
    static constexpr std::string_view kVoidTags[] = {
        "area",  "base", "br",   "col",    "embed", "hr",    "img",
        "input", "link", "meta", "param",  "source", "track", "wbr",
    };
    for (std::string_view tag : kVoidTags) {
        if (lowerTag == tag) {
            return true;
        }
    }
    return false;
}

bool IsRawContentTag(std::string_view lowerTag) {
    return lowerTag == "script" || lowerTag == "style" || lowerTag == "pre" ||
           lowerTag == "textarea";
}

std::size_t FindTagEnd(std::string_view input, std::size_t start) {
    std::size_t i = start + 1;
    char quote = '\0';
    while (i < input.size()) {
        const char ch = input[i];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
        } else {
            if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '>') {
                return i + 1;
            }
        }
        ++i;
    }
    return input.size();
}

std::size_t FindPhpEnd(std::string_view input, std::size_t start) {
    std::size_t i = start + 2;
    char quote = '\0';
    bool escaped = false;
    while (i < input.size()) {
        const char ch = input[i];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            ++i;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            ++i;
            continue;
        }
        if (ch == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            i += 2;
            while (i < input.size() && input[i] != '\n' && input[i] != '\r') {
                if (input[i] == '?' && i + 1 < input.size() && input[i + 1] == '>') {
                    return i + 2;
                }
                ++i;
            }
            continue;
        }
        if (ch == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            i += 2;
            while (i + 1 < input.size() && !(input[i] == '*' && input[i + 1] == '/')) {
                ++i;
            }
            i = (i + 1 < input.size()) ? i + 2 : input.size();
            continue;
        }
        if (ch == '?' && i + 1 < input.size() && input[i + 1] == '>') {
            return i + 2;
        }
        ++i;
    }
    return input.size();
}

std::string ExtractTagName(std::string_view tagRaw) {
    std::size_t i = 1;
    if (i < tagRaw.size() && tagRaw[i] == '/') {
        ++i;
    }
    while (i < tagRaw.size() && std::isspace(static_cast<unsigned char>(tagRaw[i]))) {
        ++i;
    }
    const std::size_t start = i;
    while (i < tagRaw.size() && !std::isspace(static_cast<unsigned char>(tagRaw[i])) &&
           tagRaw[i] != '>' && tagRaw[i] != '/') {
        ++i;
    }
    return LowerAscii(tagRaw.substr(start, i - start));
}

bool EndsWithSelfClose(std::string_view tagRaw) {
    std::size_t i = tagRaw.size();
    if (i < 2 || tagRaw[i - 1] != '>') {
        return false;
    }
    --i;
    while (i > 0 && std::isspace(static_cast<unsigned char>(tagRaw[i - 1]))) {
        --i;
    }
    return i > 0 && tagRaw[i - 1] == '/';
}

std::vector<MarkupNode> TokenizeMarkup(std::string_view input, bool isHtml, bool allowPhp,
                                       const std::atomic_bool* cancel, bool& canceled) {
    std::vector<MarkupNode> nodes;
    canceled = false;
    std::size_t i = 0;
    int opsSinceCancel = 0;

    while (i < input.size()) {
        if ((++opsSinceCancel & 4095) == 0 && IsCanceled(cancel)) {
            canceled = true;
            return {};
        }

        if (input[i] == '<') {
            // Comment <!-- ... -->
            if (input.compare(i, 4, "<!--") == 0) {
                const std::size_t close = input.find("-->", i + 4);
                const std::size_t end = close == std::string_view::npos ? input.size() : close + 3;
                nodes.push_back(MarkupNode{MarkupKind::Comment, {},
                                           std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
            // CDATA <![CDATA[ ... ]]>
            if (input.compare(i, 9, "<![CDATA[") == 0) {
                const std::size_t close = input.find("]]>", i + 9);
                const std::size_t end = close == std::string_view::npos ? input.size() : close + 3;
                nodes.push_back(MarkupNode{MarkupKind::CData, {},
                                           std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
            // PHP block <?php ... ?> or <?= ... ?>
            if (allowPhp &&
                (input.compare(i, 5, "<?php") == 0 || input.compare(i, 3, "<?=") == 0 ||
                 (input.compare(i, 2, "<?") == 0 && input.compare(i, 5, "<?xml") != 0))) {
                const std::size_t end = FindPhpEnd(input, i);
                nodes.push_back(MarkupNode{MarkupKind::PhpBlock, {},
                                           std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
            // Processing instruction <?...?> or <!DOCTYPE ...>
            if (i + 1 < input.size() && (input[i + 1] == '?' || input[i + 1] == '!')) {
                const std::size_t end = FindTagEnd(input, i);
                nodes.push_back(MarkupNode{MarkupKind::Declaration, {},
                                           std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
            // Closing tag </tag>
            if (i + 1 < input.size() && input[i + 1] == '/') {
                const std::size_t end = FindTagEnd(input, i);
                const std::string_view raw = input.substr(i, end - i);
                nodes.push_back(MarkupNode{MarkupKind::CloseTag, ExtractTagName(raw),
                                           std::string(raw)});
                i = end;
                continue;
            }
            // Opening or self-closing tag
            if (i + 1 < input.size() &&
                (std::isalpha(static_cast<unsigned char>(input[i + 1])) ||
                 input[i + 1] == '_' || input[i + 1] == ':')) {
                const std::size_t end = FindTagEnd(input, i);
                const std::string_view raw = input.substr(i, end - i);
                const std::string tagName = ExtractTagName(raw);
                const bool selfClose =
                    EndsWithSelfClose(raw) || (isHtml && IsHtmlVoidTag(tagName));
                nodes.push_back(MarkupNode{
                    selfClose ? MarkupKind::SelfCloseTag : MarkupKind::OpenTag,
                    tagName,
                    std::string(raw),
                });
                i = end;

                if (!selfClose && isHtml && IsRawContentTag(tagName)) {
                    const std::string closeNeedle = "</" + tagName;
                    std::size_t search = i;
                    std::size_t closePos = std::string_view::npos;
                    while (search < input.size()) {
                        const std::size_t lt = input.find('<', search);
                        if (lt == std::string_view::npos) {
                            break;
                        }
                        if (lt + closeNeedle.size() < input.size() &&
                            LowerAscii(input.substr(lt, closeNeedle.size())) == closeNeedle) {
                            const char after = input[lt + closeNeedle.size()];
                            if (after == '>' ||
                                std::isspace(static_cast<unsigned char>(after))) {
                                closePos = lt;
                                break;
                            }
                        }
                        search = lt + 1;
                    }
                    const std::size_t rawEnd =
                        closePos == std::string_view::npos ? input.size() : closePos;
                    if (rawEnd > i) {
                        nodes.push_back(MarkupNode{MarkupKind::RawBlock, tagName,
                                                   std::string(input.substr(i, rawEnd - i))});
                    }
                    i = rawEnd;
                }
                continue;
            }
        }

        const std::size_t nextLt = input.find('<', i + 1);
        const std::size_t end = nextLt == std::string_view::npos ? input.size() : nextLt;
        nodes.push_back(MarkupNode{MarkupKind::Text, {}, std::string(input.substr(i, end - i))});
        i = end;
    }
    return nodes;
}

std::string FormatPhpBlock(std::string_view block, int baseDepth, const FormatOptions& options,
                           const std::atomic_bool* cancel, bool& canceled) {
    canceled = false;
    if (block.compare(0, 3, "<?=") == 0) {
        return std::string(block);
    }

    const bool hasOpenPhp = block.compare(0, 5, "<?php") == 0;
    const std::size_t openLen = hasOpenPhp ? 5 : 2;
    const bool hasClose = block.size() >= openLen + 2 &&
                          block.compare(block.size() - 2, 2, "?>") == 0;
    const std::string_view openTag = block.substr(0, openLen);
    const std::string_view body =
        block.substr(openLen, block.size() - openLen - (hasClose ? 2 : 0));

    const std::string trimmedBody = TrimView(body);
    if (trimmedBody.empty()) {
        return std::string(openTag) + (hasClose ? " ?>" : "");
    }

    const FormatResult formatted =
        FormatBraceLanguage(trimmedBody, Language::Php, options, cancel);
    if (formatted.canceled) {
        canceled = true;
        return {};
    }
    if (!formatted.ok) {
        return std::string(block);
    }

    std::string inner = formatted.output;
    while (!inner.empty() && inner.back() == '\n') {
        inner.pop_back();
    }

    // Keep single-line short PHP blocks inline if they have no newlines (or when minifying).
    if (options.minify) {
        return std::string(openTag) + " " + inner + (hasClose ? " ?>" : "");
    }
    if (hasClose && inner.find('\n') == std::string::npos && inner.size() <= 60) {
        return std::string(openTag) + " " + inner + " ?>";
    }

    const std::string baseIndent = MakeIndent(baseDepth, options);
    std::string out(openTag);
    out.push_back('\n');
    std::size_t pos = 0;
    while (pos <= inner.size()) {
        const std::size_t nl = inner.find('\n', pos);
        const std::string_view line =
            nl == std::string::npos ? std::string_view(inner).substr(pos)
                                    : std::string_view(inner).substr(pos, nl - pos);
        if (!line.empty()) {
            out += baseIndent;
            out.append(line);
        }
        out.push_back('\n');
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    if (hasClose) {
        out += baseIndent;
        out += "?>";
    }
    return out;
}

Language DetectEmbeddedBlockLanguage(std::string_view tagName, std::string_view openTagRaw) {
    const std::string lowerRaw = LowerAscii(openTagRaw);
    if (tagName == "style") {
        if (lowerRaw.find("lang=\"scss\"") != std::string::npos ||
            lowerRaw.find("lang='scss'") != std::string::npos) {
            return Language::Scss;
        }
        if (lowerRaw.find("lang=\"less\"") != std::string::npos ||
            lowerRaw.find("lang='less'") != std::string::npos) {
            return Language::Less;
        }
        return Language::Css;
    }
    if (lowerRaw.find("lang=\"tsx\"") != std::string::npos ||
        lowerRaw.find("lang='tsx'") != std::string::npos) {
        return Language::Tsx;
    }
    if (lowerRaw.find("lang=\"ts\"") != std::string::npos ||
        lowerRaw.find("lang='ts'") != std::string::npos) {
        return Language::TypeScript;
    }
    if (lowerRaw.find("lang=\"jsx\"") != std::string::npos ||
        lowerRaw.find("lang='jsx'") != std::string::npos) {
        return Language::Jsx;
    }
    return Language::JavaScript;
}

std::string FormatRawInner(const MarkupNode& node, std::string_view openTagRaw, int depth,
                           const FormatOptions& options, const std::atomic_bool* cancel,
                           bool& canceled) {
    canceled = false;
    if (node.tagName == "pre" || node.tagName == "textarea") {
        return node.raw;
    }
    const std::string trimmed = TrimView(node.raw);
    if (trimmed.empty()) {
        return {};
    }
    const Language lang = DetectEmbeddedBlockLanguage(node.tagName, openTagRaw);
    const FormatResult formatted = FormatBraceLanguage(trimmed, lang, options, cancel);
    if (formatted.canceled) {
        canceled = true;
        return {};
    }
    const std::string& body = formatted.ok ? formatted.output : trimmed;
    if (options.minify) {
        std::string compact = body;
        while (!compact.empty() && compact.back() == '\n') {
            compact.pop_back();
        }
        return compact;
    }
    std::string out;
    const std::string indent = MakeIndent(depth, options);
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t nl = body.find('\n', pos);
        const std::string_view line =
            nl == std::string_view::npos ? std::string_view(body).substr(pos)
                                    : std::string_view(body).substr(pos, nl - pos);
        if (!line.empty()) {
            out += indent;
            out.append(line);
        }
        out.push_back('\n');
        if (nl == std::string::npos) {
            break;
        }
        pos = nl + 1;
    }
    return out;
}

std::string RenderMarkup(const std::vector<MarkupNode>& nodes, const FormatOptions& options,
                         const std::atomic_bool* cancel, bool& canceled) {
    canceled = false;
    std::string out;
    int depth = 0;

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if ((i & 4095) == 0 && IsCanceled(cancel)) {
            canceled = true;
            return {};
        }

        const MarkupNode& node = nodes[i];
        if (options.minify) {
            if (node.kind == MarkupKind::Comment) {
                continue;
            }
            if (node.kind == MarkupKind::Text) {
                out += TrimView(node.raw);
                continue;
            }
            if (node.kind == MarkupKind::RawBlock) {
                const std::string_view openRaw =
                    (i > 0 && nodes[i - 1].kind == MarkupKind::OpenTag) ? nodes[i - 1].raw
                                                                        : std::string_view{};
                const std::string inner =
                    FormatRawInner(node, openRaw, 0, options, cancel, canceled);
                if (canceled) {
                    return {};
                }
                out += inner;
                continue;
            }
            if (node.kind == MarkupKind::PhpBlock) {
                const std::string php = FormatPhpBlock(node.raw, 0, options, cancel, canceled);
                if (canceled) {
                    return {};
                }
                out += php;
                continue;
            }
            out += node.raw;
            continue;
        }
        if (node.kind == MarkupKind::Text) {
            const std::string trimmed = TrimView(node.raw);
            if (trimmed.empty()) {
                continue;
            }
            out += MakeIndent(depth, options);
            out += trimmed;
            out.push_back('\n');
            continue;
        }

        if (node.kind == MarkupKind::RawBlock) {
            const std::string_view openRaw =
                (i > 0 && nodes[i - 1].kind == MarkupKind::OpenTag) ? nodes[i - 1].raw
                                                                    : std::string_view{};
            const std::string inner =
                FormatRawInner(node, openRaw, depth, options, cancel, canceled);
            if (canceled) {
                return {};
            }
            out += inner;
            continue;
        }

        if (node.kind == MarkupKind::PhpBlock) {
            const std::string php = FormatPhpBlock(node.raw, depth, options, cancel, canceled);
            if (canceled) {
                return {};
            }
            out += MakeIndent(depth, options);
            out += php;
            out.push_back('\n');
            continue;
        }

        if (node.kind == MarkupKind::CloseTag) {
            depth = std::max(0, depth - 1);
            out += MakeIndent(depth, options);
            out += node.raw;
            out.push_back('\n');
            continue;
        }

        if (node.kind == MarkupKind::OpenTag) {
            // Inline `<tag>short text</tag>` on one line when possible.
            if (i + 2 < nodes.size() && nodes[i + 1].kind == MarkupKind::Text &&
                nodes[i + 2].kind == MarkupKind::CloseTag &&
                nodes[i + 2].tagName == node.tagName) {
                const std::string text = TrimView(nodes[i + 1].raw);
                if (text.find('\n') == std::string::npos &&
                    node.raw.size() + text.size() + nodes[i + 2].raw.size() <=
                        static_cast<std::size_t>(std::max(40, options.printWidth))) {
                    out += MakeIndent(depth, options);
                    out += node.raw;
                    out += text;
                    out += nodes[i + 2].raw;
                    out.push_back('\n');
                    i += 2;
                    continue;
                }
            }
            // Empty `<tag></tag>` on one line.
            if (i + 1 < nodes.size() && nodes[i + 1].kind == MarkupKind::CloseTag &&
                nodes[i + 1].tagName == node.tagName) {
                out += MakeIndent(depth, options);
                out += node.raw;
                out += nodes[i + 1].raw;
                out.push_back('\n');
                ++i;
                continue;
            }
            out += MakeIndent(depth, options);
            out += node.raw;
            out.push_back('\n');
            ++depth;
            continue;
        }

        // Declaration, Comment, CData, SelfCloseTag
        out += MakeIndent(depth, options);
        out += node.raw;
        out.push_back('\n');
    }

    if (options.minify && !out.empty() && out.back() != '\n') {
        out.push_back('\n');
    }
    return out;
}

bool MarkupTokensEquivalent(const std::vector<MarkupNode>& a, const std::vector<MarkupNode>& b,
                            bool ignoreComments) {
    std::size_t ia = 0;
    std::size_t ib = 0;
    auto skipEmpty = [ignoreComments](const std::vector<MarkupNode>& v, std::size_t& idx) {
        while (idx < v.size()) {
            if (ignoreComments && v[idx].kind == MarkupKind::Comment) {
                ++idx;
                continue;
            }
            if (v[idx].kind == MarkupKind::Text && TrimView(v[idx].raw).empty()) {
                ++idx;
                continue;
            }
            if (v[idx].kind == MarkupKind::RawBlock && TrimView(v[idx].raw).empty()) {
                ++idx;
                continue;
            }
            break;
        }
    };

    while (true) {
        skipEmpty(a, ia);
        skipEmpty(b, ib);
        if (ia == a.size() && ib == b.size()) {
            return true;
        }
        if (ia == a.size() || ib == b.size()) {
            return false;
        }
        if (a[ia].kind != b[ib].kind || a[ia].tagName != b[ib].tagName) {
            return false;
        }
        if (a[ia].kind == MarkupKind::Text) {
            if (TrimView(a[ia].raw) != TrimView(b[ib].raw)) {
                return false;
            }
        } else if (a[ia].kind != MarkupKind::RawBlock && a[ia].kind != MarkupKind::PhpBlock) {
            if (a[ia].raw != b[ib].raw) {
                return false;
            }
        }
        ++ia;
        ++ib;
    }
}

}  // namespace

FormatResult FormatMarkupLanguage(std::string_view input, Language language,
                                  const FormatOptions& options,
                                  const std::atomic_bool* cancel) {
    FormatResult result;
    result.language = language;

    const bool isHtml =
        (language == Language::Html || language == Language::Php || language == Language::Vue);
    const bool allowPhp = (language == Language::Php);

    bool canceled = false;
    const std::vector<MarkupNode> nodes =
        TokenizeMarkup(input, isHtml, allowPhp, cancel, canceled);
    if (canceled) {
        result.canceled = true;
        return result;
    }

    // Pure PHP file optimization: single PhpBlock with no surrounding HTML.
    if (language == Language::Php) {
        std::size_t nonEmptyCount = 0;
        const MarkupNode* singlePhp = nullptr;
        for (const MarkupNode& node : nodes) {
            if (node.kind == MarkupKind::Text && TrimView(node.raw).empty()) {
                continue;
            }
            ++nonEmptyCount;
            if (node.kind == MarkupKind::PhpBlock) {
                singlePhp = &node;
            }
        }
        if (nonEmptyCount == 1 && singlePhp != nullptr) {
            std::string formatted =
                FormatPhpBlock(singlePhp->raw, 0, options, cancel, canceled);
            if (canceled) {
                result.canceled = true;
                return result;
            }
            if (!formatted.empty() && formatted.back() != '\n') {
                formatted.push_back('\n');
            }
            result.output = std::move(formatted);
            result.ok = true;
            return result;
        }
    }

    std::string rendered = RenderMarkup(nodes, options, cancel, canceled);
    if (canceled) {
        result.canceled = true;
        return result;
    }

    const std::vector<MarkupNode> reparsed =
        TokenizeMarkup(rendered, isHtml, allowPhp, cancel, canceled);
    if (canceled) {
        result.canceled = true;
        return result;
    }
    if (!MarkupTokensEquivalent(nodes, reparsed, options.minify)) {
        result.error.message = "formatter produced non-equivalent markup output";
        return result;
    }

    const std::string second = RenderMarkup(reparsed, options, cancel, canceled);
    if (!canceled && second != rendered) {
        result.idempotent = false;
    }

    result.output = std::move(rendered);
    result.ok = true;
    return result;
}

}  // namespace ArgpiSuite::Format


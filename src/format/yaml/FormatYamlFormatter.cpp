#include "FormatYamlFormatter.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

bool IsBlockScalarHeader(std::string_view trimmedContent) {
    // Strip trailing inline comment outside quotes.
    char quote = '\0';
    bool escaped = false;
    std::size_t commentPos = std::string_view::npos;
    for (std::size_t i = 0; i < trimmedContent.size(); ++i) {
        const char ch = trimmedContent[i];
        if (quote != '\0') {
            if (quote == '"' && !escaped && ch == '\\') {
                escaped = true;
                continue;
            }
            if (!escaped && ch == quote) {
                quote = '\0';
            }
            escaped = false;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '#' && (i == 0 || trimmedContent[i - 1] == ' ')) {
            commentPos = i;
            break;
        }
    }
    std::string_view code =
        commentPos == std::string_view::npos ? trimmedContent : trimmedContent.substr(0, commentPos);
    code = TrimRight(code);
    if (code.empty()) {
        return false;
    }
    // Ends with `|` or `>` optionally followed by chomp (`+`/`-`) and/or indent digit.
    std::size_t pos = code.size();
    while (pos > 0 && (code[pos - 1] == '+' || code[pos - 1] == '-' ||
                       (code[pos - 1] >= '1' && code[pos - 1] <= '9'))) {
        --pos;
    }
    if (pos == 0) {
        return false;
    }
    const char indicator = code[pos - 1];
    if (indicator != '|' && indicator != '>') {
        return false;
    }
    if (pos == 1) {
        return true;
    }
    const char before = code[pos - 2];
    return before == ' ' || before == '\t' || before == ':';
}

// Normalizes `key:value` -> `key: value` and `-  item` -> `- item` outside quotes/comments.
std::string NormalizeYamlLineContent(std::string_view line) {
    std::string out;
    out.reserve(line.size() + 4);

    std::size_t i = 0;
    // Sequence dash prefix `-   foo` -> `- foo`
    if (line.size() >= 2 && line[0] == '-' && (line[1] == ' ' || line[1] == '\t')) {
        out += "- ";
        i = 1;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
    }

    char quote = '\0';
    bool escaped = false;
    while (i < line.size()) {
        const char ch = line[i];
        if (quote != '\0') {
            out.push_back(ch);
            if (quote == '"' && !escaped && ch == '\\') {
                escaped = true;
                ++i;
                continue;
            }
            if (!escaped && ch == quote) {
                quote = '\0';
            }
            escaped = false;
            ++i;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            out.push_back(ch);
            ++i;
            continue;
        }
        if (ch == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
            out.append(line.substr(i));
            break;
        }
        if (ch == ':') {
            // Do not touch `://` (URLs) or `::`.
            const bool isUrlOrScope =
                (i + 1 < line.size() && (line[i + 1] == '/' || line[i + 1] == ':'));
            out.push_back(':');
            ++i;
            if (!isUrlOrScope && i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                out.push_back(' ');
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
                    ++i;
                }
            }
            continue;
        }
        out.push_back(ch);
        ++i;
    }

    return std::string(TrimRight(out));
}

bool FormatYamlInternal(std::string_view input, const FormatOptions& options,
                        const std::atomic_bool* cancel, std::string& out,
                        FormatError& error) {
    out.clear();
    out.reserve(input.size() + 32);

    const int tabWidth = std::max(1, options.tabWidth);
    std::vector<int> indentStack;
    indentStack.push_back(0);

    bool inBlockScalar = false;
    int blockScalarParentIndent = 0;
    int blockScalarTargetParentIndent = 0;
    int blockScalarContentBaseIndent = -1;

    int consecutiveBlanks = 0;
    std::size_t pos = 0;
    int lineNum = 0;

    while (pos < input.size()) {
        if ((lineNum & 1023) == 0 && IsCanceled(cancel)) {
            return false;
        }
        ++lineNum;

        std::size_t nl = input.find('\n', pos);
        if (nl == std::string_view::npos) {
            nl = input.size();
        }
        std::string_view rawLine = input.substr(pos, nl - pos);
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.remove_suffix(1);
        }
        pos = (nl < input.size()) ? nl + 1 : input.size();

        const std::string_view trimmedRight = TrimRight(rawLine);
        std::size_t leading = 0;
        while (leading < trimmedRight.size() && trimmedRight[leading] == ' ') {
            ++leading;
        }

        if (leading < trimmedRight.size() && trimmedRight[leading] == '\t') {
            error.line = lineNum;
            error.column = static_cast<int>(leading + 1);
            error.message = "YAML does not allow tabs for indentation";
            return false;
        }

        const std::string_view content = trimmedRight.substr(leading);
        if (content.empty()) {
            if (inBlockScalar) {
                out.push_back('\n');
            } else if (!out.empty() && consecutiveBlanks == 0) {
                ++consecutiveBlanks;
                out.push_back('\n');
            }
            continue;
        }
        consecutiveBlanks = 0;

        const int origIndent = static_cast<int>(leading);

        if (inBlockScalar) {
            if (origIndent > blockScalarParentIndent) {
                if (blockScalarContentBaseIndent < 0) {
                    blockScalarContentBaseIndent = origIndent;
                }
                const int extra = std::max(0, origIndent - blockScalarContentBaseIndent);
                out.append(static_cast<std::size_t>(blockScalarTargetParentIndent + tabWidth + extra),
                           ' ');
                out.append(content);
                out.push_back('\n');
                continue;
            }
            inBlockScalar = false;
            blockScalarContentBaseIndent = -1;
        }

        if (content == "---" || content == "...") {
            indentStack.clear();
            indentStack.push_back(0);
            out.append(content);
            out.push_back('\n');
            continue;
        }

        // Update indentation stack to map original indent -> normalized level * tabWidth.
        while (indentStack.size() > 1 && origIndent < indentStack.back()) {
            indentStack.pop_back();
        }
        if (origIndent > indentStack.back()) {
            indentStack.push_back(origIndent);
        }

        const int level = static_cast<int>(indentStack.size() - 1);
        const int targetIndent = level * tabWidth;

        out.append(static_cast<std::size_t>(targetIndent), ' ');
        const std::string normalized = NormalizeYamlLineContent(content);
        out.append(normalized);
        out.push_back('\n');

        if (IsBlockScalarHeader(normalized)) {
            inBlockScalar = true;
            blockScalarParentIndent = origIndent;
            blockScalarTargetParentIndent = targetIndent;
            blockScalarContentBaseIndent = -1;
        }
    }

    while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') {
        out.pop_back();
    }
    return true;
}

}  // namespace

FormatResult FormatYamlLanguage(std::string_view input, const FormatOptions& options,
                                const std::atomic_bool* cancel) {
    FormatResult result;
    result.language = Language::Yaml;

    std::string formatted;
    FormatError error;
    if (!FormatYamlInternal(input, options, cancel, formatted, error)) {
        if (IsCanceled(cancel)) {
            result.canceled = true;
        } else {
            result.error = std::move(error);
        }
        return result;
    }

    std::string second;
    FormatError secondErr;
    if (FormatYamlInternal(formatted, options, cancel, second, secondErr) &&
        second != formatted) {
        result.idempotent = false;
    }

    result.output = std::move(formatted);
    result.ok = true;
    return result;
}

}  // namespace ArgpiSuite::Format


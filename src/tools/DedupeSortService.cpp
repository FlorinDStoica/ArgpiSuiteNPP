#include "DedupeSortService.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "format/Format.h"
#include "format/FormatLanguage.h"
#include "format/FormatOptions.h"
#include "format/json/FormatJsonParser.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

std::string TrimAscii(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && (sv[s] == ' ' || sv[s] == '\t')) {
        ++s;
    }
    size_t e = sv.size();
    while (e > s && (sv[e - 1] == ' ' || sv[e - 1] == '\t')) {
        --e;
    }
    return std::string(sv.substr(s, e - s));
}

std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::vector<std::string> SplitLines(std::string_view text, bool& hadTrailingNewline) {
    hadTrailingNewline = false;
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t lf = text.find('\n', pos);
        if (lf == std::string_view::npos) {
            std::string line(text.substr(pos));
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            break;
        }
        size_t lineEnd = lf;
        if (lineEnd > pos && text[lineEnd - 1] == '\r') {
            --lineEnd;
        }
        lines.emplace_back(text.substr(pos, lineEnd - pos));
        pos = lf + 1;
        if (pos == text.size()) {
            hadTrailingNewline = true;
        }
    }
    return lines;
}

std::string JoinLines(const std::vector<std::string>& lines, const std::string& eol,
                      bool trailingNewline) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += eol;
        out += lines[i];
    }
    if (trailingNewline && !lines.empty()) {
        out += eol;
    }
    return out;
}

int CompareStrings(std::string_view a, std::string_view b, bool caseSensitive) {
    if (caseSensitive) {
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
        const unsigned char cb = static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (a.size() < b.size()) return -1;
    if (a.size() > b.size()) return 1;
    return 0;
}

int CompareNatural(std::string_view a, std::string_view b, bool caseSensitive) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[i])) &&
            std::isdigit(static_cast<unsigned char>(b[j]))) {
            size_t iEnd = i;
            while (iEnd < a.size() && std::isdigit(static_cast<unsigned char>(a[iEnd]))) ++iEnd;
            size_t jEnd = j;
            while (jEnd < b.size() && std::isdigit(static_cast<unsigned char>(b[jEnd]))) ++jEnd;

            // Skip leading zeros for numeric comparison
            size_t iSig = i;
            while (iSig < iEnd && a[iSig] == '0') ++iSig;
            size_t jSig = j;
            while (jSig < jEnd && b[jSig] == '0') ++jSig;

            const size_t lenA = iEnd - iSig;
            const size_t lenB = jEnd - jSig;
            if (lenA != lenB) return (lenA < lenB) ? -1 : 1;
            for (size_t k = 0; k < lenA; ++k) {
                if (a[iSig + k] != b[jSig + k]) {
                    return (a[iSig + k] < b[jSig + k]) ? -1 : 1;
                }
            }
            if ((iEnd - i) != (jEnd - j)) {
                return ((iEnd - i) < (jEnd - j)) ? -1 : 1;
            }
            i = iEnd;
            j = jEnd;
        } else {
            unsigned char ca = static_cast<unsigned char>(a[i]);
            unsigned char cb = static_cast<unsigned char>(b[j]);
            if (!caseSensitive) {
                ca = static_cast<unsigned char>(std::tolower(ca));
                cb = static_cast<unsigned char>(std::tolower(cb));
            }
            if (ca != cb) return (ca < cb) ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

std::string EscapeJsonString(std::string_view s) {
    std::string out = "\"";
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

void SerializeSortedJson(const Format::JsonDocument& doc, int nodeId,
                         const DedupeSortOptions& opts, std::string& out) {
    const Format::JsonNode& n = doc.Node(nodeId);
    switch (n.kind) {
        case Format::JsonKind::Null:
            out += "null";
            break;
        case Format::JsonKind::Bool:
            out += (n.boolean ? "true" : "false");
            break;
        case Format::JsonKind::Number:
            out += n.raw;
            break;
        case Format::JsonKind::String:
            out += EscapeJsonString(n.value);
            break;
        case Format::JsonKind::Array:
            out.push_back('[');
            for (size_t i = 0; i < n.elements.size(); ++i) {
                if (i > 0) out.push_back(',');
                SerializeSortedJson(doc, n.elements[i], opts, out);
            }
            out.push_back(']');
            break;
        case Format::JsonKind::Object: {
            std::vector<Format::JsonMember> sorted = n.members;
            std::sort(sorted.begin(), sorted.end(),
                      [&](const Format::JsonMember& a, const Format::JsonMember& b) {
                          const int cmp = CompareStrings(a.key, b.key, opts.caseSensitive);
                          return opts.descending ? (cmp > 0) : (cmp < 0);
                      });
            out.push_back('{');
            for (size_t i = 0; i < sorted.size(); ++i) {
                if (i > 0) out.push_back(',');
                out += EscapeJsonString(sorted[i].key);
                out.push_back(':');
                SerializeSortedJson(doc, sorted[i].value, opts, out);
            }
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

DedupeSortService& DedupeSortService::Instance() {
    static DedupeSortService instance;
    return instance;
}

DedupeSortResult DedupeSortService::Process(std::string_view text,
                                            const DedupeSortOptions& options) {
    DedupeSortResult res;
    const std::string eol = options.eol.empty() ? "\r\n" : options.eol;

    if (options.operation == DedupeSortOperation::SortJsonKeys) {
        const Format::JsonParseResult parsed = Format::ParseJson(text, Format::Language::Json5);
        if (!parsed.ok || parsed.document.root == Format::kNoJsonNode) {
            res.message = L"Invalid JSON: " + Plugin::ToWide(parsed.error.message);
            return res;
        }
        std::string compact;
        SerializeSortedJson(parsed.document, parsed.document.root, options, compact);

        Format::FormatOptions fmtOpts;
        fmtOpts.tabWidth = 2;
        fmtOpts.endOfLine = (eol == "\n") ? Format::Eol::Lf : Format::Eol::Crlf;
        const Format::FormatResult formatted =
            Format::FormatText(compact, Format::Language::Json, fmtOpts);
        res.ok = true;
        res.output = formatted.ok ? formatted.output : compact;
        return res;
    }

    bool hadTrailingNewline = false;
    std::vector<std::string> lines = SplitLines(text, hadTrailingNewline);
    res.inputLines = static_cast<int>(lines.size());

    if (options.trimWhitespace) {
        for (auto& line : lines) {
            line = TrimAscii(line);
        }
    }

    if (options.ignoreEmptyLines || options.operation == DedupeSortOperation::RemoveEmptyLines) {
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [](const std::string& s) { return TrimAscii(s).empty(); }),
                    lines.end());
    }

    if (options.operation == DedupeSortOperation::DeduplicatePreserveOrder) {
        std::unordered_set<std::string> seen;
        seen.reserve(lines.size());
        std::vector<std::string> unique;
        unique.reserve(lines.size());
        for (auto& line : lines) {
            std::string key = options.caseSensitive ? line : ToLowerAscii(line);
            if (seen.insert(std::move(key)).second) {
                unique.push_back(std::move(line));
            }
        }
        lines = std::move(unique);
    } else if (options.operation == DedupeSortOperation::SortAlphabetical) {
        std::sort(lines.begin(), lines.end(), [&](const std::string& a, const std::string& b) {
            const int cmp = CompareStrings(a, b, options.caseSensitive);
            return options.descending ? (cmp > 0) : (cmp < 0);
        });
    } else if (options.operation == DedupeSortOperation::SortByLength) {
        std::sort(lines.begin(), lines.end(), [&](const std::string& a, const std::string& b) {
            if (a.size() != b.size()) {
                return options.descending ? (a.size() > b.size()) : (a.size() < b.size());
            }
            const int cmp = CompareStrings(a, b, options.caseSensitive);
            return options.descending ? (cmp > 0) : (cmp < 0);
        });
    } else if (options.operation == DedupeSortOperation::SortNumericNatural) {
        std::sort(lines.begin(), lines.end(), [&](const std::string& a, const std::string& b) {
            const int cmp = CompareNatural(a, b, options.caseSensitive);
            return options.descending ? (cmp > 0) : (cmp < 0);
        });
    } else if (options.operation == DedupeSortOperation::ReverseLines) {
        std::reverse(lines.begin(), lines.end());
    }

    res.ok = true;
    res.outputLines = static_cast<int>(lines.size());
    res.removedCount = std::max(0, res.inputLines - res.outputLines);
    res.output = JoinLines(lines, eol, hadTrailingNewline);
    return res;
}

}  // namespace ArgpiSuite::Tools

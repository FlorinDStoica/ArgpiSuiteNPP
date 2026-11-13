#include "FormatJsonPrinter.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "../FormatDoc.h"
#include "../FormatDocPrinter.h"
#include "../FormatUnicode.h"

namespace ArgpiSuite::Format {
namespace {

std::string QuoteString(std::string_view value) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char raw : value) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
            case '"':
                out.append("\\\"");
                break;
            case '\\':
                out.append("\\\\");
                break;
            case '\b':
                out.append("\\b");
                break;
            case '\f':
                out.append("\\f");
                break;
            case '\n':
                out.append("\\n");
                break;
            case '\r':
                out.append("\\r");
                break;
            case '\t':
                out.append("\\t");
                break;
            default:
                if (ch < 0x20) {
                    out.append("\\u00");
                    out.push_back(kHex[(ch >> 4) & 0xF]);
                    out.push_back(kHex[ch & 0xF]);
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

/// Prettier keeps JSON string escapes verbatim. For JSON5 single-quoted input
/// the quote style is normalized to double quotes without touching escapes.
std::string NormalizeStringRaw(std::string_view raw) {
    if (raw.size() < 2) {
        return QuoteString(raw);
    }
    if (raw.front() == '"') {
        return std::string(raw);
    }
    std::string out;
    out.reserve(raw.size() + 2);
    out.push_back('"');
    const std::size_t end = raw.size() - 1;  // exclude closing quote
    for (std::size_t i = 1; i < end; ++i) {
        const char ch = raw[i];
        if (ch == '\\' && i + 1 < end) {
            const char esc = raw[++i];
            if (esc == '\'') {
                out.push_back('\'');
            } else if (esc == '"') {
                out.append("\\\"");
            } else {
                out.push_back('\\');
                out.push_back(esc);
            }
        } else if (ch == '"') {
            out.append("\\\"");
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

std::string NormalizeJson5Number(std::string_view raw) {
    if (raw.empty()) {
        return {};
    }
    std::string out(raw);

    std::size_t exponent = std::string::npos;
    for (std::size_t i = 1; i < out.size(); ++i) {
        if (out[i] == 'e' || out[i] == 'E') {
            exponent = i;
            break;
        }
    }
    std::string mantissa = out.substr(0, exponent);
    std::string exponentPart = exponent == std::string::npos ? std::string{} : out.substr(exponent);

    std::size_t signLength = 0;
    if (!mantissa.empty() && (mantissa.front() == '+' || mantissa.front() == '-')) {
        signLength = 1;
    }

    if (mantissa.size() > signLength + 1 && mantissa[signLength] == '0' &&
        (mantissa[signLength + 1] == 'x' || mantissa[signLength + 1] == 'X')) {
        for (std::size_t i = signLength + 2; i < mantissa.size(); ++i) {
            mantissa[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(mantissa[i])));
        }
        mantissa[signLength + 1] = 'x';
    } else {
        if (mantissa.size() > signLength && mantissa[signLength] == '.') {
            mantissa.insert(signLength, "0");
        }
        const std::size_t dot = mantissa.find('.');
        if (dot != std::string::npos) {
            while (mantissa.size() > dot + 2 && mantissa.back() == '0') {
                mantissa.pop_back();
            }
        }
        if (!exponentPart.empty()) {
            exponentPart[0] = 'e';
            if (exponentPart.size() > 1 && exponentPart[1] == '+') {
                exponentPart.erase(1, 1);
            }
        }
    }
    return mantissa + exponentPart;
}

bool IsIdentifierKey(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < key.size(); ++i) {
        const auto ch = static_cast<unsigned char>(key[i]);
        const bool start = (ch == '_' || ch == '$' || (ch >= 'a' && ch <= 'z') ||
                            (ch >= 'A' && ch <= 'Z') || ch >= 0x80);
        const bool part = start || (ch >= '0' && ch <= '9');
        if (i == 0 ? !start : !part) {
            return false;
        }
    }
    return true;
}

class JsonDocBuilder {
public:
    JsonDocBuilder(const JsonDocument& document, std::string_view source,
                   const FormatOptions& options, const std::atomic_bool* cancel)
        : document_(document),
          source_(source),
          options_(options),
          cancel_(cancel),
          printed_(document.comments.size(), false) {}

    DocId Build() {
        const JsonNode& root = document_.Node(document_.root);
        std::vector<DocId> parts;
        for (int commentId : root.leadingComments) {
            parts.push_back(Comment(commentId));
            parts.push_back(arena_.HardLine());
        }
        parts.push_back(PrintNode(document_.root, false));
        for (int commentId : root.trailingComments) {
            if (IsLineComment(commentId)) {
                parts.push_back(arena_.LineSuffix(
                    arena_.Concat({arena_.Text(" "), Comment(commentId)})));
            } else {
                parts.push_back(arena_.Concat({arena_.Text(" "), Comment(commentId)}));
            }
        }
        parts.push_back(arena_.HardLine());
        return arena_.Concat(std::move(parts));
    }

    DocArena& Arena() { return arena_; }

    [[nodiscard]] int UnprintedComments() const {
        int count = 0;
        for (bool printed : printed_) {
            if (!printed) {
                ++count;
            }
        }
        return count;
    }

private:
    [[nodiscard]] bool IsJson5() const { return document_.dialect == Language::Json5; }

    const JsonComment& GetComment(int id) const {
        return document_.comments[static_cast<std::size_t>(id)];
    }

    bool IsLineComment(int id) const { return GetComment(id).kind == JsonComment::Kind::Line; }

    DocId Comment(int id) {
        const JsonComment& comment = document_.comments[static_cast<std::size_t>(id)];
        printed_[static_cast<std::size_t>(id)] = true;
        return arena_.Text(comment.text);
    }

    /// Line comments that end a child line force the enclosing group to break so
    /// the lineSuffix is flushed inside the container. Comments attached to this
    /// node itself are printed outside its own group and do not force a break.
    bool ChildHasForcingComment(int id) const {
        const JsonNode& node = document_.Node(id);
        auto valueForces = [&](int valueId) {
            const JsonNode& value = document_.Node(valueId);
            for (int commentId : value.trailingComments) {
                if (IsLineComment(commentId)) {
                    return true;
                }
            }
            return ChildHasForcingComment(valueId);
        };
        for (const JsonMember& member : node.members) {
            if (valueForces(member.value)) {
                return true;
            }
        }
        for (int element : node.elements) {
            if (valueForces(element)) {
                return true;
            }
        }
        return false;
    }

    /// Mirrors Prettier's `shouldBreak` heuristic for arrays: an array breaks
    /// when it holds at least two elements that are all containers of the same
    /// kind (all objects or all arrays) and each of those containers holds more
    /// than one item. That turns `[[1, 2], [3, 4]]` into one element per line
    /// while `[[1], [2]]`, `[{ "a": 1 }, { "b": 2 }]` and mixed arrays stay
    /// flat.
    bool ContainerArrayForcesBreak(int id) const {
        const JsonNode& node = document_.Node(id);
        if (node.elements.size() < 2) {
            return false;
        }
        const JsonKind expected = document_.Node(node.elements.front()).kind;
        if (expected != JsonKind::Array && expected != JsonKind::Object) {
            return false;
        }
        for (int element : node.elements) {
            const JsonNode& child = document_.Node(element);
            if (child.kind != expected) {
                return false;
            }
            const std::size_t items =
                expected == JsonKind::Array ? child.elements.size() : child.members.size();
            if (items < 2) {
                return false;
            }
        }
        return true;
    }

    bool HasMultilineIgnored(int id) const {
        const JsonNode& node = document_.Node(id);
        if (node.ignored && source_.substr(node.start, node.end - node.start).find('\n') !=
                                std::string_view::npos) {
            return true;
        }
        for (const JsonMember& member : node.members) {
            if (HasMultilineIgnored(member.value)) {
                return true;
            }
        }
        for (int element : node.elements) {
            if (HasMultilineIgnored(element)) {
                return true;
            }
        }
        return false;
    }

    DocId LeadingComments(int id) {
        const JsonNode& node = document_.Node(id);
        if (node.leadingComments.empty()) {
            return kNoDoc;
        }
        std::vector<DocId> parts;
        for (int commentId : node.leadingComments) {
            parts.push_back(Comment(commentId));
            parts.push_back(arena_.HardLine());
        }
        return arena_.Concat(std::move(parts));
    }

    DocId TrailingComments(int id) {
        const JsonNode& node = document_.Node(id);
        if (node.trailingComments.empty()) {
            return kNoDoc;
        }
        std::vector<DocId> parts;
        for (int commentId : node.trailingComments) {
            if (IsLineComment(commentId)) {
                parts.push_back(arena_.LineSuffix(
                    arena_.Concat({arena_.Text(" "), Comment(commentId)})));
            } else {
                parts.push_back(arena_.Concat({arena_.Text(" "), Comment(commentId)}));
            }
        }
        return arena_.Concat(std::move(parts));
    }

    DocId KeyDoc(const JsonMember& member) {
        if (IsJson5() && IsIdentifierKey(member.key)) {
            return arena_.Text(member.key);
        }
        if (member.keyRaw.size() >= 2 && (member.keyRaw.front() == '"' ||
                                          member.keyRaw.front() == '\'')) {
            return arena_.Text(NormalizeStringRaw(member.keyRaw));
        }
        return arena_.Text(QuoteString(member.key));
    }

    DocId ValueDoc(const JsonNode& node) {
        switch (node.kind) {
            case JsonKind::Null:
                return arena_.Text("null");
            case JsonKind::Bool:
                return arena_.Text(node.boolean ? "true" : "false");
            case JsonKind::Number:
                return arena_.Text(IsJson5() ? NormalizeJson5Number(node.raw) : node.raw);
            case JsonKind::String:
                return arena_.Text(node.raw.empty() ? QuoteString(node.value)
                                                    : NormalizeStringRaw(node.raw));
            case JsonKind::Array:
            case JsonKind::Object:
                return kNoDoc;
        }
        return arena_.Text("null");
    }

    DocId PrintNode(int id, bool skipLeading) {
        if (id == kNoJsonNode) {
            return arena_.Text("null");
        }
        const JsonNode& node = document_.Node(id);
        if (node.ignored) {
            return arena_.Text(source_.substr(node.start, node.end - node.start));
        }

        std::vector<DocId> parts;
        if (!skipLeading) {
            const DocId leading = LeadingComments(id);
            if (leading != kNoDoc) {
                parts.push_back(leading);
            }
        }

        switch (node.kind) {
            case JsonKind::Array:
                parts.push_back(PrintArray(id));
                break;
            case JsonKind::Object:
                parts.push_back(PrintObject(id));
                break;
            default: {
                const DocId value = ValueDoc(node);
                if (value != kNoDoc) {
                    parts.push_back(value);
                }
                break;
            }
        }

        const DocId trailing = TrailingComments(id);
        if (trailing != kNoDoc) {
            parts.push_back(trailing);
        }
        return arena_.Concat(std::move(parts));
    }

    DocId TrailingCommaDoc(DocId groupId) {
        if (!IsJson5() || options_.trailingComma == TrailingComma::None) {
            return kNoDoc;
        }
        return arena_.IfBreak(arena_.Text(","), kNoDoc, groupId);
    }

    DocId PrintArray(int id) {
        const JsonNode& node = document_.Node(id);
        if (node.elements.empty()) {
            if (node.danglingComments.empty()) {
                return arena_.Text("[]");
            }
            std::vector<DocId> comments;
            for (int commentId : node.danglingComments) {
                comments.push_back(Comment(commentId));
            }
            std::vector<DocId> body;
            body.push_back(arena_.Text("["));
            body.push_back(
                arena_.Indent(arena_.Concat({arena_.HardLine(), JoinWithHardLine(comments)})));
            body.push_back(arena_.HardLine());
            body.push_back(arena_.Text("]"));
            return arena_.Group(arena_.Concat(std::move(body)), true);
        }

        const DocId groupId = arena_.Reserve();
        const DocId trailingComma = TrailingCommaDoc(groupId);
        bool hasBlankLines = false;
        for (int element : node.elements) {
            if (document_.Node(element).blankLineBefore) {
                hasBlankLines = true;
                break;
            }
        }
        const bool concise =
            node.elements.size() > 1 && !hasBlankLines && !ChildHasForcingComment(id) &&
            std::all_of(node.elements.begin(), node.elements.end(), [&](int element) {
                return document_.Node(element).kind == JsonKind::Number;
            });

        std::vector<DocId> body;
        body.push_back(arena_.Text("["));
        if (concise) {
            std::vector<DocId> fillParts;
            for (std::size_t i = 0; i < node.elements.size(); ++i) {
                DocId content = PrintNode(node.elements[i], false);
                if (i + 1 == node.elements.size() && trailingComma != kNoDoc) {
                    content = arena_.Concat({content, trailingComma});
                }
                fillParts.push_back(content);
                if (i + 1 != node.elements.size()) {
                    fillParts.push_back(
                        arena_.Concat({arena_.Text(","), arena_.Line()}));
                }
            }
            body.push_back(arena_.Indent(
                arena_.Concat({arena_.SoftLine(), arena_.Fill(std::move(fillParts))})));
        } else {
            std::vector<DocId> items;
            for (std::size_t i = 0; i < node.elements.size(); ++i) {
                const JsonNode& value = document_.Node(node.elements[i]);
                std::vector<DocId> item;
                if (value.blankLineBefore) {
                    item.push_back(arena_.HardLine());
                }
                DocId content = PrintNode(node.elements[i], false);
                if (i + 1 == node.elements.size() && trailingComma != kNoDoc) {
                    content = arena_.Concat({content, trailingComma});
                }
                item.push_back(content);
                items.push_back(arena_.Concat(std::move(item)));
            }
            body.push_back(arena_.Indent(arena_.Concat(
                {arena_.SoftLine(),
                 JoinWithSeparator(items,
                                   arena_.Concat({arena_.Text(","), arena_.Line()}))})));
        }
        body.push_back(arena_.SoftLine());
        body.push_back(arena_.Text("]"));

        DocNode group;
        group.kind = DocKind::Group;
        group.contents = arena_.Concat(std::move(body));
        // Arrays collapse regardless of the source layout (Prettier does not
        // preserve a line break after "["), but comments and blank lines keep
        // them expanded.
        group.shouldBreak =
            hasBlankLines || ChildHasForcingComment(id) || HasMultilineIgnored(id) ||
            ContainerArrayForcesBreak(id);
        arena_.Set(groupId, std::move(group));
        return groupId;
    }

    DocId PrintObject(int id) {
        const JsonNode& node = document_.Node(id);
        if (node.members.empty() && node.danglingComments.empty()) {
            return arena_.Text("{}");
        }
        if (node.members.empty()) {
            std::vector<DocId> comments;
            for (int commentId : node.danglingComments) {
                comments.push_back(Comment(commentId));
            }
            std::vector<DocId> body;
            body.push_back(arena_.Text("{"));
            body.push_back(
                arena_.Indent(arena_.Concat({arena_.HardLine(), JoinWithHardLine(comments)})));
            body.push_back(arena_.HardLine());
            body.push_back(arena_.Text("}"));
            return arena_.Group(arena_.Concat(std::move(body)), true);
        }

        const DocId groupId = arena_.Reserve();
        const DocId trailingComma = TrailingCommaDoc(groupId);

        std::vector<DocId> items;
        for (std::size_t i = 0; i < node.members.size(); ++i) {
            const JsonMember& member = node.members[i];
            const JsonNode& value = document_.Node(member.value);
            std::vector<DocId> item;
            const DocId leading = LeadingComments(member.value);
            if (leading != kNoDoc) {
                item.push_back(leading);
            }
            if (value.blankLineBefore) {
                item.push_back(arena_.HardLine());
            }
            item.push_back(KeyDoc(member));
            item.push_back(arena_.Text(": "));
            DocId content = PrintNode(member.value, true);
            if (i + 1 == node.members.size() && trailingComma != kNoDoc) {
                content = arena_.Concat({content, trailingComma});
            }
            item.push_back(content);
            items.push_back(arena_.Concat(std::move(item)));
        }

        const DocId spacing = options_.bracketSpacing ? arena_.Line() : arena_.SoftLine();
        std::vector<DocId> body;
        body.push_back(arena_.Text("{"));
        body.push_back(arena_.Indent(arena_.Concat(
            {spacing, JoinWithSeparator(items,
                                        arena_.Concat({arena_.Text(","), arena_.Line()}))})));
        body.push_back(spacing);
        body.push_back(arena_.Text("}"));

        DocNode group;
        group.kind = DocKind::Group;
        group.contents = arena_.Concat(std::move(body));
        group.shouldBreak =
            node.multiline || ChildHasForcingComment(id) || HasMultilineIgnored(id);
        arena_.Set(groupId, std::move(group));
        return groupId;
    }

    DocId JoinWithHardLine(const std::vector<DocId>& parts) {
        if (parts.empty()) {
            return arena_.Text({});
        }
        std::vector<DocId> out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back(arena_.HardLine());
            }
            out.push_back(parts[i]);
        }
        return arena_.Concat(std::move(out));
    }

    DocId JoinWithSeparator(const std::vector<DocId>& parts, DocId separator) {
        if (parts.empty()) {
            return arena_.Text({});
        }
        std::vector<DocId> out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                out.push_back(separator);
            }
            out.push_back(parts[i]);
        }
        return arena_.Concat(std::move(out));
    }

    const JsonDocument& document_;
    std::string_view source_;
    const FormatOptions& options_;
    const std::atomic_bool* cancel_;
    std::vector<bool> printed_;
    DocArena arena_;
};

void PrintMinifiedJsonNode(const JsonDocument& document, int nodeId, std::string& out,
                           const std::atomic_bool* cancel, bool& canceled) {
    if (canceled || (cancel != nullptr && cancel->load(std::memory_order_relaxed))) {
        canceled = true;
        return;
    }
    const JsonNode& node = document.Node(nodeId);
    switch (node.kind) {
        case JsonKind::Null:
            out.append("null");
            break;
        case JsonKind::Bool:
            out.append(node.boolean ? "true" : "false");
            break;
        case JsonKind::Number:
            out.append(node.raw);
            break;
        case JsonKind::String:
            out.append(QuoteString(node.value));
            break;
        case JsonKind::Array:
            out.push_back('[');
            for (std::size_t i = 0; i < node.elements.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                PrintMinifiedJsonNode(document, node.elements[i], out, cancel, canceled);
                if (canceled) {
                    return;
                }
            }
            out.push_back(']');
            break;
        case JsonKind::Object:
            out.push_back('{');
            for (std::size_t i = 0; i < node.members.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                out.append(QuoteString(node.members[i].key));
                out.push_back(':');
                PrintMinifiedJsonNode(document, node.members[i].value, out, cancel, canceled);
                if (canceled) {
                    return;
                }
            }
            out.push_back('}');
            break;
    }
}

}  // namespace

JsonPrintResult PrintJson(const JsonDocument& document, std::string_view source,
                          const FormatOptions& options, const std::atomic_bool* cancel) {
    JsonPrintResult result;
    if (document.root == kNoJsonNode) {
        result.error.message = "empty document";
        return result;
    }
    if (options.minify) {
        bool canceled = false;
        std::string out;
        out.reserve(source.size());
        PrintMinifiedJsonNode(document, document.root, out, cancel, canceled);
        if (canceled) {
            result.canceled = true;
            return result;
        }
        if (!out.empty()) {
            out.push_back('\n');
        }
        result.text = std::move(out);
        result.ok = true;
        return result;
    }
    JsonDocBuilder builder(document, source, options, cancel);
    const DocId root = builder.Build();

    DocPrintOptions printOptions;
    printOptions.printWidth = options.printWidth > 0 ? options.printWidth : 80;
    printOptions.tabWidth = options.tabWidth > 0 ? options.tabWidth : 2;
    printOptions.useTabs = options.useTabs;
    printOptions.endOfLine = Eol::Lf;

    DocPrintResult printed = PrintDoc(builder.Arena(), root, printOptions, cancel);
    if (printed.canceled) {
        result.canceled = true;
        return result;
    }
    result.text = std::move(printed.text);
    result.unprintedComments = builder.UnprintedComments();
    if (result.unprintedComments != 0) {
        result.error.message = "formatter lost a comment";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace ArgpiSuite::Format

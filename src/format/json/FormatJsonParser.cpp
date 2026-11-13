#include "../json/FormatJsonParser.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "../FormatUnicode.h"

namespace ArgpiSuite::Format {
namespace {

struct ParseAbort {};

constexpr int kMaxDepth = 512;

bool IsWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool IsDigit(char ch) { return ch >= '0' && ch <= '9'; }

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool IsIdentifierStart(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return ch == '_' || ch == '$' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           uch >= 0x80;
}

bool IsIdentifierPart(char ch) {
    return IsIdentifierStart(ch) || IsDigit(ch);
}

bool HasNewline(std::string_view text, std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end && i < text.size(); ++i) {
        if (text[i] == '\n' || text[i] == '\r') {
            return true;
        }
    }
    return false;
}

bool HasBlankLine(std::string_view text, std::size_t begin, std::size_t end) {
    int newlines = 0;
    for (std::size_t i = begin; i < end && i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < end && text[i + 1] == '\n') {
                ++i;
            }
            ++newlines;
        } else if (text[i] == '\n') {
            ++newlines;
        }
        if (newlines >= 2) {
            return true;
        }
    }
    return false;
}

class JsonParser {
public:
    JsonParser(std::string_view text, Language dialect, JsonDocument& document)
        : text_(text), dialect_(dialect), document_(document) {
        allowJson5_ = dialect == Language::Json5;
    }

    bool Parse(FormatError& error) {
        try {
            SkipTrivia();
            document_.root = ParseValue(0);
            SkipTrivia();
            if (pos_ < text_.size()) {
                Fail("unexpected token after the top-level value", pos_);
            }
            MarkBlankLines(document_.root);
            return true;
        } catch (const ParseAbort&) {
            error = error_;
            return false;
        }
    }

private:
    [[noreturn]] void Fail(const char* message, std::size_t offset) {
        error_.message = message;
        error_.offset = offset < text_.size() ? offset : text_.size();
        Utf8LineColumn(text_, error_.offset, error_.line, error_.column);
        throw ParseAbort{};
    }

    char Peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    bool StartsWith(std::string_view token) const {
        return text_.compare(pos_, token.size(), token) == 0;
    }

    int AddNode(JsonKind kind, std::size_t start) {
        JsonNode node;
        node.kind = kind;
        node.start = start;
        document_.nodes.push_back(std::move(node));
        return static_cast<int>(document_.nodes.size() - 1);
    }

    void RecordComment(JsonComment::Kind kind, std::size_t start, std::size_t end) {
        JsonComment comment;
        comment.kind = kind;
        comment.start = start;
        comment.end = end;
        comment.text.assign(text_.substr(start, end - start));
        while (!comment.text.empty() &&
               (comment.text.back() == ' ' || comment.text.back() == '\t' ||
                comment.text.back() == '\r' || comment.text.back() == '\n')) {
            comment.text.pop_back();
        }
        // Own-line: only whitespace between the previous line start and the comment.
        std::size_t lineStart = start;
        while (lineStart > 0 && text_[lineStart - 1] != '\n' && text_[lineStart - 1] != '\r') {
            --lineStart;
        }
        bool onlyWhitespace = true;
        for (std::size_t i = lineStart; i < start; ++i) {
            if (!IsWhitespace(text_[i])) {
                onlyWhitespace = false;
                break;
            }
        }
        comment.ownLine = onlyWhitespace;
        document_.comments.push_back(std::move(comment));
    }

    void SkipTrivia() {
        for (;;) {
            while (pos_ < text_.size() && IsWhitespace(text_[pos_])) {
                ++pos_;
            }
            if (pos_ + 1 < text_.size() && text_[pos_] == '/' && text_[pos_ + 1] == '/') {
                const std::size_t start = pos_;
                pos_ += 2;
                while (pos_ < text_.size() && text_[pos_] != '\n' && text_[pos_] != '\r') {
                    ++pos_;
                }
                RecordComment(JsonComment::Kind::Line, start, pos_);
                continue;
            }
            if (pos_ + 1 < text_.size() && text_[pos_] == '/' && text_[pos_ + 1] == '*') {
                const std::size_t start = pos_;
                pos_ += 2;
                while (pos_ + 1 < text_.size() &&
                       !(text_[pos_] == '*' && text_[pos_ + 1] == '/')) {
                    ++pos_;
                }
                if (pos_ + 1 >= text_.size()) {
                    Fail("unterminated block comment", start);
                }
                pos_ += 2;
                RecordComment(JsonComment::Kind::Block, start, pos_);
                continue;
            }
            break;
        }
    }

    int ParseValue(int depth) {
        if (depth > kMaxDepth) {
            Fail("nesting is too deep", pos_);
        }
        SkipTrivia();
        if (pos_ >= text_.size()) {
            Fail("unexpected end of input", pos_);
        }
        const std::size_t start = pos_;
        const char ch = text_[pos_];
        switch (ch) {
            case '{':
                return ParseObject(depth);
            case '[':
                return ParseArray(depth);
            case '"':
                return ParseStringValue();
            case '\'':
                if (!allowJson5_) {
                    Fail("single-quoted strings are not valid JSON", pos_);
                }
                return ParseStringValue();
            case 't':
                if (StartsWith("true")) {
                    pos_ += 4;
                    const int id = AddNode(JsonKind::Bool, start);
                    document_.nodes[static_cast<std::size_t>(id)].boolean = true;
                    document_.nodes[static_cast<std::size_t>(id)].end = pos_;
                    return id;
                }
                break;
            case 'f':
                if (StartsWith("false")) {
                    pos_ += 5;
                    const int id = AddNode(JsonKind::Bool, start);
                    document_.nodes[static_cast<std::size_t>(id)].end = pos_;
                    return id;
                }
                break;
            case 'n':
                if (StartsWith("null")) {
                    pos_ += 4;
                    const int id = AddNode(JsonKind::Null, start);
                    document_.nodes[static_cast<std::size_t>(id)].end = pos_;
                    return id;
                }
                break;
            default:
                break;
        }

        if (ch == '-' || IsDigit(ch) || (allowJson5_ && (ch == '+' || ch == '.' || ch == 'I' ||
                                                         ch == 'N'))) {
            return ParseNumber();
        }
        Fail("expected a JSON value", pos_);
    }

    int ParseStringValue() {
        const std::size_t start = pos_;
        const std::string value = ParseString();
        const int id = AddNode(JsonKind::String, start);
        JsonNode& node = document_.nodes[static_cast<std::size_t>(id)];
        node.value = value;
        node.raw.assign(text_.substr(start, pos_ - start));
        node.end = pos_;
        return id;
    }

    std::string ParseString() {
        const char quote = text_[pos_];
        if (quote != '"' && !(allowJson5_ && quote == '\'')) {
            Fail("expected a string", pos_);
        }
        ++pos_;
        std::string out;
        for (;;) {
            if (pos_ >= text_.size()) {
                Fail("unterminated string", pos_);
            }
            const char ch = text_[pos_];
            if (ch == quote) {
                ++pos_;
                break;
            }
            if (ch == '\\') {
                ++pos_;
                ParseEscape(out);
                continue;
            }
            if (ch == '\n' || ch == '\r') {
                Fail("unterminated string", pos_);
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                Fail("unescaped control character in string", pos_);
            }
            out.push_back(ch);
            ++pos_;
        }
        return out;
    }

    void ParseEscape(std::string& out) {
        if (pos_ >= text_.size()) {
            Fail("unterminated escape sequence", pos_);
        }
        const char esc = text_[pos_++];
        switch (esc) {
            case '"':
            case '\'':
            case '\\':
            case '/':
                out.push_back(esc);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'v':
                if (!allowJson5_) {
                    Fail("invalid escape sequence", pos_ - 2);
                }
                out.push_back('\v');
                break;
            case '0':
                if (!allowJson5_) {
                    Fail("invalid escape sequence", pos_ - 2);
                }
                out.push_back('\0');
                break;
            case 'x': {
                if (!allowJson5_) {
                    Fail("invalid escape sequence", pos_ - 2);
                }
                if (pos_ + 2 > text_.size()) {
                    Fail("invalid \\x escape", pos_ - 2);
                }
                const int hi = HexValue(text_[pos_]);
                const int lo = HexValue(text_[pos_ + 1]);
                if (hi < 0 || lo < 0) {
                    Fail("invalid \\x escape", pos_ - 2);
                }
                pos_ += 2;
                Utf8Append(out, static_cast<char32_t>(hi * 16 + lo));
                break;
            }
            case 'u': {
                char32_t codePoint = ReadHex4();
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
                    if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        const char32_t low = ReadHex4();
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            codePoint = 0xFFFD;
                        }
                    } else {
                        codePoint = 0xFFFD;
                    }
                } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
                    codePoint = 0xFFFD;
                }
                Utf8Append(out, codePoint);
                break;
            }
            case '\r':
                if (!allowJson5_) {
                    Fail("invalid escape sequence", pos_ - 2);
                }
                if (pos_ < text_.size() && text_[pos_] == '\n') {
                    ++pos_;
                }
                break;
            case '\n':
                if (!allowJson5_) {
                    Fail("invalid escape sequence", pos_ - 2);
                }
                break;
            default:
                if (!allowJson5_) {
                    Fail("invalid escape sequence", pos_ - 2);
                }
                out.push_back(esc);
                break;
        }
    }

    char32_t ReadHex4() {
        if (pos_ + 4 > text_.size()) {
            Fail("invalid \\u escape", pos_);
        }
        char32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = HexValue(text_[pos_ + static_cast<std::size_t>(i)]);
            if (digit < 0) {
                Fail("invalid \\u escape", pos_);
            }
            value = (value << 4) | static_cast<char32_t>(digit);
        }
        pos_ += 4;
        return value;
    }

    int ParseNumber() {
        const std::size_t start = pos_;
        if (allowJson5_) {
            if (Peek() == '+' || Peek() == '-') {
                ++pos_;
            }
            if (StartsWith("Infinity")) {
                pos_ += 8;
            } else if (StartsWith("NaN")) {
                pos_ += 3;
            } else if (pos_ + 1 < text_.size() && text_[pos_] == '0' &&
                       (text_[pos_ + 1] == 'x' || text_[pos_ + 1] == 'X')) {
                pos_ += 2;
                const std::size_t digitsStart = pos_;
                while (pos_ < text_.size() && HexValue(text_[pos_]) >= 0) {
                    ++pos_;
                }
                if (pos_ == digitsStart) {
                    Fail("invalid hexadecimal number", start);
                }
            } else {
                bool anyDigits = false;
                while (pos_ < text_.size() && IsDigit(text_[pos_])) {
                    ++pos_;
                    anyDigits = true;
                }
                if (pos_ < text_.size() && text_[pos_] == '.') {
                    ++pos_;
                    while (pos_ < text_.size() && IsDigit(text_[pos_])) {
                        ++pos_;
                        anyDigits = true;
                    }
                }
                if (!anyDigits) {
                    Fail("invalid number", start);
                }
                if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                    ++pos_;
                    if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                        ++pos_;
                    }
                    const std::size_t expStart = pos_;
                    while (pos_ < text_.size() && IsDigit(text_[pos_])) {
                        ++pos_;
                    }
                    if (pos_ == expStart) {
                        Fail("invalid exponent", start);
                    }
                }
            }
        } else {
            if (Peek() == '-') {
                ++pos_;
            }
            if (pos_ >= text_.size() || !IsDigit(text_[pos_])) {
                Fail("invalid number", start);
            }
            if (text_[pos_] == '0') {
                ++pos_;
                if (pos_ < text_.size() && IsDigit(text_[pos_])) {
                    Fail("numbers may not have leading zeroes", start);
                }
            } else {
                while (pos_ < text_.size() && IsDigit(text_[pos_])) {
                    ++pos_;
                }
            }
            if (pos_ < text_.size() && text_[pos_] == '.') {
                ++pos_;
                if (pos_ >= text_.size() || !IsDigit(text_[pos_])) {
                    Fail("invalid fraction", start);
                }
                while (pos_ < text_.size() && IsDigit(text_[pos_])) {
                    ++pos_;
                }
            }
            if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                ++pos_;
                if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                    ++pos_;
                }
                const std::size_t expStart = pos_;
                while (pos_ < text_.size() && IsDigit(text_[pos_])) {
                    ++pos_;
                }
                if (pos_ == expStart) {
                    Fail("invalid exponent", start);
                }
            }
        }
        const int id = AddNode(JsonKind::Number, start);
        JsonNode& node = document_.nodes[static_cast<std::size_t>(id)];
        node.raw.assign(text_.substr(start, pos_ - start));
        node.end = pos_;
        return id;
    }

    bool SkipTriviaCheckNewline() {
        const std::size_t begin = pos_;
        SkipTrivia();
        return HasNewline(text_, begin, pos_);
    }

    int ParseObject(int depth) {
        const std::size_t start = pos_;
        ++pos_;
        const int id = AddNode(JsonKind::Object, start);
        bool& multiline = document_.nodes[static_cast<std::size_t>(id)].multiline;
        multiline = SkipTriviaCheckNewline();
        SkipTrivia();
        if (Peek() == '}') {
            ++pos_;
            document_.nodes[static_cast<std::size_t>(id)].end = pos_;
            return id;
        }
        std::size_t previousEnd = pos_;
        for (;;) {
            SkipTrivia();
            if (Peek() == '}') {
                document_.nodes[static_cast<std::size_t>(id)].hasTrailingComma = true;
                break;
            }
            if (pos_ >= text_.size()) {
                Fail("unterminated object", start);
            }
            const std::size_t keyStart = pos_;
            std::string key;
            if (Peek() == '"' || (allowJson5_ && Peek() == '\'')) {
                key = ParseString();
            } else if (allowJson5_ && IsIdentifierStart(Peek())) {
                const std::size_t identStart = pos_;
                ++pos_;
                while (pos_ < text_.size() && IsIdentifierPart(text_[pos_])) {
                    ++pos_;
                }
                key.assign(text_.substr(identStart, pos_ - identStart));
            } else {
                Fail("expected a property name", pos_);
            }
            const std::size_t keyEnd = pos_;
            const std::string keyRaw(text_.substr(keyStart, keyEnd - keyStart));
            SkipTrivia();
            if (Peek() != ':') {
                Fail("expected ':' after property name", pos_);
            }
            ++pos_;
            SkipTrivia();
            const int value = ParseValue(depth + 1);
            document_.nodes[static_cast<std::size_t>(value)].parent = id;
            document_.nodes[static_cast<std::size_t>(value)].blankLineBefore =
                HasBlankLine(text_, previousEnd, keyStart);
            JsonMember member;
            member.key = std::move(key);
            member.keyRaw = keyRaw;
            member.keyStart = keyStart;
            member.keyEnd = keyEnd;
            member.value = value;
            document_.nodes[static_cast<std::size_t>(id)].members.push_back(std::move(member));
            document_.nodes[static_cast<std::size_t>(id)].hasTrailingComma = false;
            previousEnd = document_.nodes[static_cast<std::size_t>(value)].end;
            SkipTrivia();
            if (Peek() == ',') {
                ++pos_;
                SkipTrivia();
                if (Peek() == '}') {
                    document_.nodes[static_cast<std::size_t>(id)].hasTrailingComma = true;
                    break;
                }
                continue;
            }
            if (Peek() == '}') {
                break;
            }
            Fail("expected ',' or '}'", pos_);
        }
        if (Peek() != '}') {
            Fail("unterminated object", start);
        }
        ++pos_;
        document_.nodes[static_cast<std::size_t>(id)].end = pos_;
        return id;
    }

    int ParseArray(int depth) {
        const std::size_t start = pos_;
        ++pos_;
        const int id = AddNode(JsonKind::Array, start);
        bool& multiline = document_.nodes[static_cast<std::size_t>(id)].multiline;
        multiline = SkipTriviaCheckNewline();
        SkipTrivia();
        if (Peek() == ']') {
            ++pos_;
            document_.nodes[static_cast<std::size_t>(id)].end = pos_;
            return id;
        }
        std::size_t previousEnd = pos_;
        for (;;) {
            SkipTrivia();
            if (Peek() == ']') {
                document_.nodes[static_cast<std::size_t>(id)].hasTrailingComma = true;
                break;
            }
            const std::size_t elementStart = pos_;
            const int value = ParseValue(depth + 1);
            document_.nodes[static_cast<std::size_t>(value)].parent = id;
            document_.nodes[static_cast<std::size_t>(value)].blankLineBefore =
                HasBlankLine(text_, previousEnd, elementStart);
            document_.nodes[static_cast<std::size_t>(id)].elements.push_back(value);
            document_.nodes[static_cast<std::size_t>(id)].hasTrailingComma = false;
            previousEnd = document_.nodes[static_cast<std::size_t>(value)].end;
            SkipTrivia();
            if (Peek() == ',') {
                ++pos_;
                SkipTrivia();
                if (Peek() == ']') {
                    document_.nodes[static_cast<std::size_t>(id)].hasTrailingComma = true;
                    break;
                }
                continue;
            }
            if (Peek() == ']') {
                break;
            }
            Fail("expected ',' or ']'", pos_);
        }
        if (Peek() != ']') {
            Fail("unterminated array", start);
        }
        ++pos_;
        document_.nodes[static_cast<std::size_t>(id)].end = pos_;
        return id;
    }

    void MarkBlankLines(int id) {
        for (const JsonMember& member : document_.nodes[static_cast<std::size_t>(id)].members) {
            MarkBlankLines(member.value);
        }
        for (int element : document_.nodes[static_cast<std::size_t>(id)].elements) {
            MarkBlankLines(element);
        }
    }

    std::string_view text_;
    Language dialect_;
    JsonDocument& document_;
    std::size_t pos_ = 0;
    bool allowJson5_ = false;
    FormatError error_;
};

std::string Trimmed(std::string_view text) {
    while (!text.empty() && IsWhitespace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsWhitespace(text.back())) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

}  // namespace

JsonParseResult ParseJson(std::string_view text, Language dialect) {
    JsonParseResult result;
    result.document.dialect = dialect;
    JsonParser parser(text, dialect, result.document);
    result.ok = parser.Parse(result.error);
    if (!result.ok) {
        result.document = JsonDocument{};
    }
    return result;
}

void AttachJsonComments(JsonDocument& document, std::string_view source) {
    if (document.root == kNoJsonNode) {
        return;
    }
    auto nodeAt = [&](int id) -> JsonNode& {
        return document.nodes[static_cast<std::size_t>(id)];
    };

    for (std::size_t commentIndex = 0; commentIndex < document.comments.size(); ++commentIndex) {
        const JsonComment& comment = document.comments[commentIndex];
        const int commentId = static_cast<int>(commentIndex);

        if (comment.end <= document.nodes[static_cast<std::size_t>(document.root)].start) {
            nodeAt(document.root).leadingComments.push_back(commentId);
            continue;
        }
        if (comment.start >= document.nodes[static_cast<std::size_t>(document.root)].end) {
            nodeAt(document.root).trailingComments.push_back(commentId);
            continue;
        }

        int id = document.root;
        bool insideMember = false;
        for (;;) {
            JsonNode& node = nodeAt(id);
            if (node.kind == JsonKind::Object) {
                int descend = kNoJsonNode;
                for (const JsonMember& member : node.members) {
                    const JsonNode& value = document.nodes[static_cast<std::size_t>(member.value)];
                    if (member.keyStart <= comment.start && comment.end <= value.end) {
                        if (value.start <= comment.start && comment.end <= value.end) {
                            descend = member.value;
                        } else {
                            insideMember = true;
                            nodeAt(member.value).leadingComments.push_back(commentId);
                        }
                        break;
                    }
                }
                if (insideMember) {
                    break;
                }
                if (descend != kNoJsonNode) {
                    id = descend;
                    continue;
                }
                break;
            }
            if (node.kind == JsonKind::Array) {
                int descend = kNoJsonNode;
                for (int element : node.elements) {
                    const JsonNode& value = document.nodes[static_cast<std::size_t>(element)];
                    if (value.start <= comment.start && comment.end <= value.end) {
                        descend = element;
                        break;
                    }
                }
                if (descend != kNoJsonNode) {
                    id = descend;
                    continue;
                }
                break;
            }
            break;
        }
        if (insideMember) {
            continue;
        }

        JsonNode& container = nodeAt(id);
        if (container.kind != JsonKind::Object && container.kind != JsonKind::Array) {
            if (comment.start >= container.end) {
                container.trailingComments.push_back(commentId);
            } else {
                container.leadingComments.push_back(commentId);
            }
            continue;
        }

        // Build ordered child ranges.
        struct Child {
            int node = kNoJsonNode;
            std::size_t start = 0;
            std::size_t end = 0;
        };
        std::vector<Child> children;
        if (container.kind == JsonKind::Object) {
            for (const JsonMember& member : container.members) {
                const JsonNode& value = document.nodes[static_cast<std::size_t>(member.value)];
                children.push_back(Child{member.value, member.keyStart, value.end});
            }
        } else {
            for (int element : container.elements) {
                const JsonNode& value = document.nodes[static_cast<std::size_t>(element)];
                children.push_back(Child{element, value.start, value.end});
            }
        }

        if (children.empty()) {
            container.danglingComments.push_back(commentId);
            continue;
        }

        int preceding = kNoJsonNode;
        int following = kNoJsonNode;
        for (const Child& child : children) {
            if (child.end <= comment.start) {
                preceding = child.node;
            } else if (child.start > comment.end && following == kNoJsonNode) {
                following = child.node;
            }
        }

        if (comment.ownLine) {
            if (following != kNoJsonNode) {
                nodeAt(following).leadingComments.push_back(commentId);
            } else {
                nodeAt(preceding).trailingComments.push_back(commentId);
            }
            continue;
        }

        bool sameLine = false;
        if (preceding != kNoJsonNode) {
            const JsonNode& previous = document.nodes[static_cast<std::size_t>(preceding)];
            sameLine = !HasNewline(source, previous.end, comment.start);
        }
        if (sameLine) {
            nodeAt(preceding).trailingComments.push_back(commentId);
        } else if (following != kNoJsonNode) {
            nodeAt(following).leadingComments.push_back(commentId);
        } else {
            nodeAt(preceding).trailingComments.push_back(commentId);
        }
    }

    // Blank lines must be measured from the last leading comment, not from the
    // previous sibling (a comment on its own line is not a blank line).
    for (JsonNode& container : document.nodes) {
        for (JsonMember& member : container.members) {
            JsonNode& value = document.nodes[static_cast<std::size_t>(member.value)];
            if (value.leadingComments.empty()) {
                continue;
            }
            const JsonComment& last =
                document.comments[static_cast<std::size_t>(value.leadingComments.back())];
            value.blankLineBefore = HasBlankLine(source, last.end, member.keyStart);
        }
        for (int element : container.elements) {
            JsonNode& value = document.nodes[static_cast<std::size_t>(element)];
            if (value.leadingComments.empty()) {
                continue;
            }
            const JsonComment& last =
                document.comments[static_cast<std::size_t>(value.leadingComments.back())];
            value.blankLineBefore = HasBlankLine(source, last.end, value.start);
        }
    }

    // Evaluate prettier-ignore pragmas on leading comments.
    for (JsonNode& node : document.nodes) {
        for (int commentId : node.leadingComments) {
            const std::string text =
                Trimmed(document.comments[static_cast<std::size_t>(commentId)].text);
            if (text == "// prettier-ignore" || text == "/* prettier-ignore */") {
                node.ignored = true;
            }
        }
    }
}

const JsonNode* JsonMemberValue(const JsonDocument& document, const JsonNode& object,
                                std::string_view key) {
    if (object.kind != JsonKind::Object) {
        return nullptr;
    }
    for (const JsonMember& member : object.members) {
        if (member.key == key) {
            return &document.Node(member.value);
        }
    }
    return nullptr;
}

std::string JsonScalarText(const JsonNode& node) {
    switch (node.kind) {
        case JsonKind::String:
            return node.value;
        case JsonKind::Number:
            return node.raw;
        case JsonKind::Bool:
            return node.boolean ? "true" : "false";
        case JsonKind::Null:
            return "null";
        default:
            return {};
    }
}

bool JsonScalarBool(const JsonNode& node, bool& out) {
    if (node.kind == JsonKind::Bool) {
        out = node.boolean;
        return true;
    }
    if (node.kind == JsonKind::Number) {
        out = node.raw != "0";
        return true;
    }
    return false;
}

}  // namespace ArgpiSuite::Format

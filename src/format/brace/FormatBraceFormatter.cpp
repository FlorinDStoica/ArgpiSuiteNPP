#include "FormatBraceFormatter.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Format {
namespace {

enum class TokenKind {
    Word,
    String,
    LineComment,
    BlockComment,
    Preprocessor,
    OpenBrace,
    CloseBrace,
    OpenParen,
    CloseParen,
    OpenBracket,
    CloseBracket,
    Semicolon,
    Comma,
    Colon,
    Question,
    Operator,
    Newline,
};

struct Token {
    TokenKind kind = TokenKind::Word;
    std::string text;
};

bool IsCanceled(const std::atomic_bool* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

bool IsCssFamily(Language language) {
    return language == Language::Css || language == Language::Scss || language == Language::Less;
}

bool IsJsFamily(Language language) {
    return language == Language::JavaScript || language == Language::TypeScript ||
           language == Language::Jsx || language == Language::Tsx;
}

bool IsTypeScriptFamily(Language language) {
    return language == Language::TypeScript || language == Language::Tsx;
}

bool IsJsxFamily(Language language) {
    return language == Language::Jsx || language == Language::Tsx;
}

bool IsCStylePreprocessorLanguage(Language language) {
    return language == Language::C || language == Language::Cpp;
}

bool SupportsHashComment(Language language) {
    return language == Language::Php || language == Language::Gradle;
}

bool IsIdentStart(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return ch == '_' || ch == '$' || ch == '@' || (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') || uch >= 0x80;
}

bool IsIdentPart(char ch, Language language) {
    if (IsIdentStart(ch) || (ch >= '0' && ch <= '9')) {
        return true;
    }
    if (IsCssFamily(language) && (ch == '-' || ch == '%')) {
        return true;
    }
    return false;
}

bool IsRegexAllowedAfter(const std::vector<Token>& tokens) {
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (it->kind == TokenKind::Newline || it->kind == TokenKind::LineComment ||
            it->kind == TokenKind::BlockComment) {
            continue;
        }
        switch (it->kind) {
            case TokenKind::OpenParen:
            case TokenKind::OpenBracket:
            case TokenKind::OpenBrace:
            case TokenKind::Semicolon:
            case TokenKind::Comma:
            case TokenKind::Colon:
            case TokenKind::Question:
            case TokenKind::Operator:
                return true;
            case TokenKind::Word:
                return it->text == "return" || it->text == "throw" || it->text == "case" ||
                       it->text == "typeof" || it->text == "void" || it->text == "delete" ||
                       it->text == "await" || it->text == "yield" || it->text == "in" ||
                       it->text == "of" || it->text == "else";
            default:
                return false;
        }
    }
    return true;
}

std::size_t ScanQuoted(std::string_view input, std::size_t start, char quote, bool multiline) {
    std::size_t i = start + 1;
    while (i < input.size()) {
        const char ch = input[i];
        if (ch == '\\') {
            i += (i + 1 < input.size()) ? 2 : 1;
            continue;
        }
        if (!multiline && (ch == '\n' || ch == '\r')) {
            return i;
        }
        if (ch == quote) {
            return i + 1;
        }
        ++i;
    }
    return input.size();
}

std::size_t ScanTripleQuoted(std::string_view input, std::size_t start, char quote) {
    std::size_t i = start + 3;
    while (i + 2 < input.size()) {
        if (input[i] == '\\') {
            i += 2;
            continue;
        }
        if (input[i] == quote && input[i + 1] == quote && input[i + 2] == quote) {
            return i + 3;
        }
        ++i;
    }
    return input.size();
}

std::size_t ScanCppRawString(std::string_view input, std::size_t start) {
    // Expects input[start] == 'R' and input[start + 1] == '"'
    std::size_t delimStart = start + 2;
    std::size_t openParen = delimStart;
    while (openParen < input.size() && input[openParen] != '(' && input[openParen] != '\n' &&
           input[openParen] != '\r') {
        ++openParen;
    }
    if (openParen >= input.size() || input[openParen] != '(') {
        return start;
    }
    const std::string_view delim = input.substr(delimStart, openParen - delimStart);
    std::size_t i = openParen + 1;
    while (i < input.size()) {
        if (input[i] == ')' && i + 1 + delim.size() < input.size() &&
            input.compare(i + 1, delim.size(), delim) == 0 &&
            input[i + 1 + delim.size()] == '"') {
            return i + 2 + delim.size();
        }
        ++i;
    }
    return input.size();
}

std::size_t ScanJsRegex(std::string_view input, std::size_t start) {
    std::size_t i = start + 1;
    bool inClass = false;
    while (i < input.size()) {
        const char ch = input[i];
        if (ch == '\n' || ch == '\r') {
            return start;
        }
        if (ch == '\\') {
            i += (i + 1 < input.size()) ? 2 : 1;
            continue;
        }
        if (ch == '[') {
            inClass = true;
        } else if (ch == ']') {
            inClass = false;
        } else if (ch == '/' && !inClass) {
            ++i;
            while (i < input.size() && std::isalpha(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            return i;
        }
        ++i;
    }
    return start;
}

// Scans a balanced JSX element or fragment (`<Tag ...>...</Tag>`, `<Tag />`, `<>...</>`).
// Returns `start` if the `<` does not begin a balanced JSX element (e.g. a comparison or TS generic).
std::size_t ScanJsxElement(std::string_view input, std::size_t start) {
    if (start >= input.size() || input[start] != '<') {
        return start;
    }
    if (start + 1 >= input.size()) {
        return start;
    }
    const char next = input[start + 1];
    if (next != '>' && !std::isalpha(static_cast<unsigned char>(next)) && next != '_' &&
        next != '$') {
        return start;
    }

    int depth = 0;
    std::size_t i = start;
    while (i < input.size()) {
        if (input[i] == '<') {
            if (i + 1 < input.size() && input[i + 1] == '/') {
                // Closing tag `</...>`
                std::size_t j = i + 2;
                while (j < input.size() && input[j] != '>') {
                    if (input[j] == '\n' || input[j] == '\r') {
                        return start;
                    }
                    ++j;
                }
                if (j >= input.size()) {
                    return start;
                }
                --depth;
                i = j + 1;
                if (depth <= 0) {
                    return i;
                }
                continue;
            }
            if (i + 1 < input.size() && input[i + 1] == '>') {
                // Fragment open `<>`
                ++depth;
                i += 2;
                continue;
            }
            // Opening or self-closing tag `<Tag ...>`
            std::size_t j = i + 1;
            if (j >= input.size() ||
                (!std::isalpha(static_cast<unsigned char>(input[j])) && input[j] != '_' &&
                 input[j] != '$')) {
                // Plain `<` inside JSX text? Not a tag.
                ++i;
                continue;
            }
            bool selfClosing = false;
            int braceNest = 0;
            while (j < input.size()) {
                const char c = input[j];
                if (c == '"' || c == '\'' || c == '`') {
                    j = ScanQuoted(input, j, c, c == '`');
                    continue;
                }
                if (c == '{') {
                    ++braceNest;
                    ++j;
                    continue;
                }
                if (c == '}' && braceNest > 0) {
                    --braceNest;
                    ++j;
                    continue;
                }
                if (braceNest == 0) {
                    if (c == '/' && j + 1 < input.size() && input[j + 1] == '>') {
                        selfClosing = true;
                        j += 2;
                        break;
                    }
                    if (c == '>') {
                        ++j;
                        break;
                    }
                }
                ++j;
            }
            if (j > input.size() || (j == input.size() && input.back() != '>')) {
                return start;
            }
            if (!selfClosing) {
                ++depth;
            } else if (depth == 0) {
                return j;
            }
            i = j;
            continue;
        }
        if (input[i] == '{' && depth > 0) {
            // JSX expression `{ ... }`
            int braceNest = 1;
            ++i;
            while (i < input.size() && braceNest > 0) {
                const char c = input[i];
                if (c == '"' || c == '\'' || c == '`') {
                    i = ScanQuoted(input, i, c, c == '`');
                    continue;
                }
                if (c == '{') {
                    ++braceNest;
                } else if (c == '}') {
                    --braceNest;
                }
                ++i;
            }
            continue;
        }
        ++i;
    }
    return start;
}

std::vector<Token> Tokenize(std::string_view input, Language language,
                            const std::atomic_bool* cancel, bool& canceled) {
    std::vector<Token> tokens;
    tokens.reserve(input.size() / 4 + 16);
    canceled = false;

    std::size_t i = 0;
    bool atLineStart = true;
    int opsSinceCancel = 0;

    while (i < input.size()) {
        if ((++opsSinceCancel & 4095) == 0 && IsCanceled(cancel)) {
            canceled = true;
            return {};
        }

        const char ch = input[i];
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') {
            ++i;
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < input.size() && input[i + 1] == '\n') {
                ++i;
            }
            tokens.push_back(Token{TokenKind::Newline, "\n"});
            atLineStart = true;
            ++i;
            continue;
        }

        // C/C++ preprocessor directive at start of line.
        if (atLineStart && IsCStylePreprocessorLanguage(language) && ch == '#') {
            std::size_t end = i;
            while (end < input.size()) {
                if (input[end] == '\r' || input[end] == '\n') {
                    // Check for trailing backslash line continuation.
                    std::size_t back = end;
                    while (back > i && (input[back - 1] == ' ' || input[back - 1] == '\t')) {
                        --back;
                    }
                    if (back > i && input[back - 1] == '\\') {
                        if (input[end] == '\r' && end + 1 < input.size() &&
                            input[end + 1] == '\n') {
                            end += 2;
                        } else {
                            ++end;
                        }
                        continue;
                    }
                    break;
                }
                ++end;
            }
            tokens.push_back(Token{TokenKind::Preprocessor, std::string(input.substr(i, end - i))});
            i = end;
            atLineStart = false;
            continue;
        }

        atLineStart = false;

        // Line comments: // or # (PHP/Gradle)
        if (ch == '/' && i + 1 < input.size() && input[i + 1] == '/') {
            std::size_t end = i + 2;
            while (end < input.size() && input[end] != '\n' && input[end] != '\r') {
                ++end;
            }
            tokens.push_back(Token{TokenKind::LineComment, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }
        if (SupportsHashComment(language) && ch == '#') {
            std::size_t end = i + 1;
            while (end < input.size() && input[end] != '\n' && input[end] != '\r') {
                ++end;
            }
            tokens.push_back(Token{TokenKind::LineComment, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }

        // Block comments: /* ... */
        if (ch == '/' && i + 1 < input.size() && input[i + 1] == '*') {
            std::size_t end = i + 2;
            while (end + 1 < input.size() && !(input[end] == '*' && input[end + 1] == '/')) {
                ++end;
            }
            end = (end + 1 < input.size()) ? end + 2 : input.size();
            tokens.push_back(Token{TokenKind::BlockComment, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }

        // JS/TS regex literal
        if (IsJsFamily(language) && ch == '/' && IsRegexAllowedAfter(tokens)) {
            const std::size_t end = ScanJsRegex(input, i);
            if (end > i) {
                tokens.push_back(Token{TokenKind::String, std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
        }

        // JSX/TSX element or fragment
        if (IsJsxFamily(language) && ch == '<' && IsRegexAllowedAfter(tokens)) {
            const std::size_t end = ScanJsxElement(input, i);
            if (end > i) {
                tokens.push_back(Token{TokenKind::String, std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
        }

        // C++ raw string R"delim(...)delim"
        if (language == Language::Cpp && ch == 'R' && i + 1 < input.size() && input[i + 1] == '"') {
            const std::size_t end = ScanCppRawString(input, i);
            if (end > i) {
                tokens.push_back(Token{TokenKind::String, std::string(input.substr(i, end - i))});
                i = end;
                continue;
            }
        }

        // Triple-quoted strings (Kotlin, Java, Gradle/Groovy)
        if ((language == Language::Kotlin || language == Language::Java ||
             language == Language::Gradle) &&
            (ch == '"' || ch == '\'') && i + 2 < input.size() && input[i + 1] == ch &&
            input[i + 2] == ch) {
            const std::size_t end = ScanTripleQuoted(input, i, ch);
            tokens.push_back(Token{TokenKind::String, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }

        // Standard quoted strings / char literals / backtick template literals
        if (ch == '"' || ch == '\'' ||
            (ch == '`' && (IsJsFamily(language) || language == Language::Kotlin))) {
            const bool multiline = (ch == '`') || (language == Language::Php) ||
                                   (language == Language::Gradle);
            const std::size_t end = ScanQuoted(input, i, ch, multiline);
            tokens.push_back(Token{TokenKind::String, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }

        // CSS/SCSS hex color or #{} interpolation
        if (IsCssFamily(language) && ch == '#') {
            if (i + 1 < input.size() && input[i + 1] == '{') {
                tokens.push_back(Token{TokenKind::Operator, "#{"});
                i += 2;
                continue;
            }
            std::size_t end = i + 1;
            while (end < input.size() &&
                   (std::isxdigit(static_cast<unsigned char>(input[end])) || input[end] == '-' ||
                    input[end] == '_')) {
                ++end;
            }
            tokens.push_back(Token{TokenKind::Word, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }

        // Punctuation
        switch (ch) {
            case '{':
                tokens.push_back(Token{TokenKind::OpenBrace, "{"});
                ++i;
                continue;
            case '}':
                tokens.push_back(Token{TokenKind::CloseBrace, "}"});
                ++i;
                continue;
            case '(':
                tokens.push_back(Token{TokenKind::OpenParen, "("});
                ++i;
                continue;
            case ')':
                tokens.push_back(Token{TokenKind::CloseParen, ")"});
                ++i;
                continue;
            case '[':
                tokens.push_back(Token{TokenKind::OpenBracket, "["});
                ++i;
                continue;
            case ']':
                tokens.push_back(Token{TokenKind::CloseBracket, "]"});
                ++i;
                continue;
            case ';':
                tokens.push_back(Token{TokenKind::Semicolon, ";"});
                ++i;
                continue;
            case ',':
                tokens.push_back(Token{TokenKind::Comma, ","});
                ++i;
                continue;
            case ':':
                if (i + 1 < input.size() && input[i + 1] == ':') {
                    tokens.push_back(Token{TokenKind::Operator, "::"});
                    i += 2;
                } else {
                    tokens.push_back(Token{TokenKind::Colon, ":"});
                    ++i;
                }
                continue;
            case '?':
                if (i + 1 < input.size() && (input[i + 1] == '?' || input[i + 1] == '.' ||
                                             input[i + 1] == ':')) {
                    tokens.push_back(Token{TokenKind::Operator, std::string(input.substr(i, 2))});
                    i += 2;
                } else {
                    tokens.push_back(Token{TokenKind::Question, "?"});
                    ++i;
                }
                continue;
            default:
                break;
        }

        // Identifiers and numbers
        if (IsIdentStart(ch) || (ch >= '0' && ch <= '9')) {
            std::size_t end = i + 1;
            while (end < input.size()) {
                const char cur = input[end];
                if (IsIdentPart(cur, language)) {
                    ++end;
                } else if (cur == '.' && end + 1 < input.size() &&
                           std::isdigit(static_cast<unsigned char>(input[i])) &&
                           std::isdigit(static_cast<unsigned char>(input[end + 1]))) {
                    ++end;
                } else if ((cur == '+' || cur == '-') && end > i + 1 &&
                           (input[end - 1] == 'e' || input[end - 1] == 'E') &&
                           std::isdigit(static_cast<unsigned char>(input[i]))) {
                    ++end;
                } else {
                    break;
                }
            }
            tokens.push_back(Token{TokenKind::Word, std::string(input.substr(i, end - i))});
            i = end;
            continue;
        }

        // Multi-character operators
        static constexpr std::string_view kThreeCharOps[] = {
            "===", "!==", "<<=", ">>=", ">>>", "...", "<=>", "->*", "??=",
        };
        bool matchedOp = false;
        if (i + 3 <= input.size()) {
            const std::string_view sub = input.substr(i, 3);
            for (std::string_view op : kThreeCharOps) {
                if (sub == op) {
                    tokens.push_back(Token{TokenKind::Operator, std::string(sub)});
                    i += 3;
                    matchedOp = true;
                    break;
                }
            }
        }
        if (matchedOp) {
            continue;
        }

        static constexpr std::string_view kTwoCharOps[] = {
            "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=",
            "/=", "%=", "&=", "|=", "^=", "<<", ">>", "->", "=>", "..", ".*",
            "**", "!!", "?.", "??",
        };
        if (i + 2 <= input.size()) {
            const std::string_view sub = input.substr(i, 2);
            for (std::string_view op : kTwoCharOps) {
                if (sub == op) {
                    tokens.push_back(Token{TokenKind::Operator, std::string(sub)});
                    i += 2;
                    matchedOp = true;
                    break;
                }
            }
        }
        if (matchedOp) {
            continue;
        }

        tokens.push_back(Token{TokenKind::Operator, std::string(1, ch)});
        ++i;
    }

    return tokens;
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

bool IsControlKeyword(std::string_view word) {
    return word == "if" || word == "for" || word == "while" || word == "switch" ||
           word == "catch" || word == "when" || word == "synchronized" || word == "elseif" ||
           word == "foreach";
}

bool IsContinuationKeywordAfterBrace(std::string_view word) {
    return word == "else" || word == "catch" || word == "finally" || word == "while";
}

bool IsAccessSpecifier(std::string_view word, Language language) {
    if (language != Language::Cpp) {
        return false;
    }
    return word == "public" || word == "private" || word == "protected" || word == "signals" ||
           word == "slots";
}

bool IsUnaryPrefixAllowed(const Token* prev) {
    if (prev == nullptr) {
        return true;
    }
    switch (prev->kind) {
        case TokenKind::OpenParen:
        case TokenKind::OpenBracket:
        case TokenKind::OpenBrace:
        case TokenKind::Semicolon:
        case TokenKind::Comma:
        case TokenKind::Colon:
        case TokenKind::Question:
        case TokenKind::Operator:
            return prev->text != "++" && prev->text != "--";
        case TokenKind::Word:
            return prev->text == "return" || prev->text == "throw" || prev->text == "case" ||
                   prev->text == "else" || prev->text == "delete" || prev->text == "new" ||
                   prev->text == "typeof" || prev->text == "void" || prev->text == "await" ||
                   prev->text == "yield";
        default:
            return false;
    }
}

bool IsBinarySpacedOp(std::string_view op) {
    if (op == "." || op == "::" || op == "->" || op == "?." || op == ".*" || op == "->*" ||
        op == "++" || op == "--" || op == "!" || op == "~" || op == "!!" || op == ".." ||
        op == "..." || op == "#{") {
        return false;
    }
    return true;
}

bool TokensEquivalent(const std::vector<Token>& a, const std::vector<Token>& b,
                      bool ignoreComments) {
    std::size_t ia = 0;
    std::size_t ib = 0;
    auto shouldSkip = [ignoreComments](const Token& t) {
        if (t.kind == TokenKind::Newline) {
            return true;
        }
        return ignoreComments &&
               (t.kind == TokenKind::LineComment || t.kind == TokenKind::BlockComment);
    };
    while (true) {
        while (ia < a.size() && shouldSkip(a[ia])) {
            ++ia;
        }
        while (ib < b.size() && shouldSkip(b[ib])) {
            ++ib;
        }
        if (ia == a.size() && ib == b.size()) {
            return true;
        }
        if (ia == a.size() || ib == b.size()) {
            return false;
        }
        if (a[ia].kind != b[ib].kind || a[ia].text != b[ib].text) {
            return false;
        }
        ++ia;
        ++ib;
    }
}

bool NeedsMinifySpace(const Token& prev, const Token& cur) {
    if (prev.kind == TokenKind::Word && cur.kind == TokenKind::Word) {
        return true;
    }
    if ((prev.kind == TokenKind::Word || prev.kind == TokenKind::String) &&
        (cur.kind == TokenKind::Word || cur.kind == TokenKind::String)) {
        return prev.kind == TokenKind::Word && cur.kind == TokenKind::Word;
    }
    if (prev.kind == TokenKind::Operator && cur.kind == TokenKind::Operator) {
        const char a = prev.text.back();
        const char b = cur.text.front();
        if ((a == '+' && b == '+') || (a == '-' && b == '-') || (a == '/' && b == '*') ||
            (a == '/' && b == '/') || (a == '<' && b == '<') || (a == '>' && b == '>') ||
            (a == '&' && b == '&') || (a == '|' && b == '|') || (a == '=' && b == '=') ||
            (a == '!' && b == '=') || (a == ':' && b == ':')) {
            return true;
        }
    }
    if (prev.kind == TokenKind::Word && cur.kind == TokenKind::Operator &&
        (cur.text == "." || cur.text == "::" || cur.text == "->")) {
        return false;
    }
    return false;
}

std::string MinifyTokens(const std::vector<Token>& tokens, const std::atomic_bool* cancel,
                         bool& canceled) {
    std::string out;
    out.reserve(tokens.size() * 2);
    canceled = false;
    const Token* prev = nullptr;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if ((i & 4095) == 0 && IsCanceled(cancel)) {
            canceled = true;
            return {};
        }
        const Token& tok = tokens[i];
        if (tok.kind == TokenKind::Newline || tok.kind == TokenKind::LineComment ||
            tok.kind == TokenKind::BlockComment) {
            continue;
        }
        if (tok.kind == TokenKind::Preprocessor) {
            if (!out.empty() && out.back() != '\n') {
                out.push_back('\n');
            }
            out += tok.text;
            out.push_back('\n');
            prev = nullptr;
            continue;
        }
        if (prev != nullptr && NeedsMinifySpace(*prev, tok)) {
            out.push_back(' ');
        }
        out += tok.text;
        prev = &tok;
    }
    if (!out.empty() && out.back() != '\n') {
        out.push_back('\n');
    }
    return out;
}

std::string FormatTokens(const std::vector<Token>& tokens, Language language,
                         const FormatOptions& options, const std::atomic_bool* cancel,
                         bool& canceled) {
    std::string out;
    out.reserve(tokens.size() * 4);
    canceled = false;

    int braceDepth = 0;
    int parenDepth = 0;
    bool atLineStart = true;
    int consecutiveNewlines = 0;
    int pendingBlankLines = 0;
    const Token* prevSig = nullptr;

    auto emitNewline = [&](int extraBlankLines) {
        // Trim trailing spaces on the current line.
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
            out.pop_back();
        }
        if (!out.empty() && out.back() != '\n') {
            out.push_back('\n');
        }
        if (extraBlankLines > 0 && !out.empty()) {
            out.push_back('\n');
        }
        atLineStart = true;
    };

    auto ensureIndent = [&](int depth) {
        if (atLineStart) {
            out += MakeIndent(depth, options);
            atLineStart = false;
        }
    };

    // Precompute non-newline next token index for lookahead.
    std::vector<std::size_t> nextSigIdx(tokens.size(), tokens.size());
    std::size_t nextIdx = tokens.size();
    for (std::size_t k = tokens.size(); k > 0; --k) {
        nextSigIdx[k - 1] = nextIdx;
        if (tokens[k - 1].kind != TokenKind::Newline) {
            nextIdx = k - 1;
        }
    }

    // For TypeScript/TSX, mark `<` and `>` that form generic type parameter/argument lists.
    std::vector<char> isGenericAngle(tokens.size(), 0);
    if (IsTypeScriptFamily(language)) {
        for (std::size_t idx = 0; idx < tokens.size(); ++idx) {
            if (tokens[idx].kind != TokenKind::Operator || tokens[idx].text != "<" ||
                isGenericAngle[idx]) {
                continue;
            }
            int depth = 1;
            bool valid = true;
            std::size_t closeIdx = tokens.size();
            for (std::size_t k = idx + 1; k < tokens.size(); ++k) {
                const Token& tk = tokens[k];
                if (tk.kind == TokenKind::Semicolon || tk.kind == TokenKind::OpenBrace ||
                    tk.kind == TokenKind::CloseBrace || tk.kind == TokenKind::Newline) {
                    valid = false;
                    break;
                }
                if (tk.kind == TokenKind::Operator) {
                    if (tk.text == "==" || tk.text == "!=" || tk.text == "===" ||
                        tk.text == "!==" || tk.text == "&&" || tk.text == "||" ||
                        tk.text == "<=" || tk.text == ">=") {
                        valid = false;
                        break;
                    }
                    if (tk.text == "<") {
                        ++depth;
                    } else if (tk.text == ">") {
                        --depth;
                        if (depth == 0) {
                            closeIdx = k;
                            break;
                        }
                    }
                }
            }
            if (valid && closeIdx < tokens.size()) {
                const std::size_t afterIdx = nextSigIdx[closeIdx];
                const Token* after = afterIdx < tokens.size() ? &tokens[afterIdx] : nullptr;
                const bool followedByTypeContext =
                    after == nullptr || after->kind == TokenKind::OpenParen ||
                    after->kind == TokenKind::CloseParen || after->kind == TokenKind::Comma ||
                    after->kind == TokenKind::Semicolon || after->kind == TokenKind::Colon ||
                    after->kind == TokenKind::OpenBrace || after->kind == TokenKind::OpenBracket ||
                    (after->kind == TokenKind::Operator &&
                     (after->text == "=" || after->text == "|" || after->text == "&" ||
                      after->text == ">" || after->text == "." || after->text == "=>")) ||
                    (after->kind == TokenKind::Word &&
                     (after->text == "extends" || after->text == "implements"));
                if (followedByTypeContext) {
                    isGenericAngle[idx] = 1;
                    isGenericAngle[closeIdx] = 1;
                }
            }
        }
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if ((i & 4095) == 0 && IsCanceled(cancel)) {
            canceled = true;
            return {};
        }

        const Token& tok = tokens[i];
        if (tok.kind == TokenKind::Newline) {
            ++consecutiveNewlines;
            if (consecutiveNewlines >= 2 && atLineStart && !out.empty()) {
                pendingBlankLines = 1;
            }
            continue;
        }
        consecutiveNewlines = 0;

        const Token* nextSig =
            nextSigIdx[i] < tokens.size() ? &tokens[nextSigIdx[i]] : nullptr;

        // Preprocessor directives always live on their own line at column 0.
        if (tok.kind == TokenKind::Preprocessor) {
            if (!out.empty()) {
                emitNewline(pendingBlankLines);
            }
            out += tok.text;
            pendingBlankLines = 0;
            emitNewline(0);
            prevSig = &tok;
            continue;
        }

        // Closing brace
        if (tok.kind == TokenKind::CloseBrace) {
            braceDepth = std::max(0, braceDepth - 1);
            if (prevSig != nullptr && prevSig->kind == TokenKind::OpenBrace &&
                out.back() == '{') {
                // Empty block `{}` on one line.
                out.push_back('}');
                atLineStart = false;
            } else {
                if (!atLineStart) {
                    emitNewline(0);
                } else if (pendingBlankLines > 0 && !out.empty() &&
                           out.size() >= 2 && out[out.size() - 2] == '\n') {
                    // Do not leave a trailing blank line right before `}`.
                    out.pop_back();
                }
                ensureIndent(braceDepth);
                out.push_back('}');
            }
            pendingBlankLines = 0;
            if (nextSig != nullptr &&
                nextSig->kind == TokenKind::Word &&
                IsContinuationKeywordAfterBrace(nextSig->text)) {
                // Keep `} else {` on the same line.
            } else if (nextSig != nullptr &&
                       (nextSig->kind == TokenKind::Semicolon ||
                        nextSig->kind == TokenKind::Comma ||
                        nextSig->kind == TokenKind::CloseParen)) {
                // Keep `};` or `},` or `})` together.
            } else if (nextSig != nullptr && nextSig->kind != TokenKind::LineComment &&
                       nextSig->kind != TokenKind::BlockComment) {
                emitNewline(0);
            }
            prevSig = &tok;
            continue;
        }

        // Open brace
        if (tok.kind == TokenKind::OpenBrace) {
            if (atLineStart) {
                ensureIndent(braceDepth);
            } else if (prevSig != nullptr && prevSig->kind != TokenKind::OpenParen &&
                       prevSig->kind != TokenKind::OpenBracket &&
                       prevSig->text != "#{") {
                out.push_back(' ');
            }
            out.push_back('{');
            ++braceDepth;
            pendingBlankLines = 0;
            if (nextSig != nullptr && nextSig->kind != TokenKind::CloseBrace &&
                nextSig->kind != TokenKind::LineComment &&
                nextSig->kind != TokenKind::BlockComment) {
                emitNewline(0);
            }
            prevSig = &tok;
            continue;
        }

        // Line / block comments
        if (tok.kind == TokenKind::LineComment) {
            if (atLineStart) {
                if (pendingBlankLines > 0 && !out.empty() && out.back() == '\n' &&
                    (out.size() < 2 || out[out.size() - 2] != '\n')) {
                    out.push_back('\n');
                }
                ensureIndent(braceDepth + (parenDepth > 0 ? 1 : 0));
            } else {
                out.push_back(' ');
            }
            out += tok.text;
            pendingBlankLines = 0;
            emitNewline(0);
            prevSig = &tok;
            continue;
        }

        if (tok.kind == TokenKind::BlockComment) {
            if (atLineStart) {
                if (pendingBlankLines > 0 && !out.empty() && out.back() == '\n' &&
                    (out.size() < 2 || out[out.size() - 2] != '\n')) {
                    out.push_back('\n');
                }
                ensureIndent(braceDepth + (parenDepth > 0 ? 1 : 0));
            } else {
                out.push_back(' ');
            }
            out += tok.text;
            pendingBlankLines = 0;
            if (tok.text.find('\n') != std::string::npos ||
                (nextSig != nullptr && i + 1 < tokens.size() &&
                 tokens[i + 1].kind == TokenKind::Newline)) {
                emitNewline(0);
            }
            prevSig = &tok;
            continue;
        }

        // If we are at line start, apply pending blank line (at most 1) and indent.
        if (atLineStart) {
            if (pendingBlankLines > 0 && !out.empty() && out.back() == '\n' &&
                (out.size() < 2 || out[out.size() - 2] != '\n')) {
                out.push_back('\n');
            }
            int lineIndent = braceDepth + (parenDepth > 0 ? 1 : 0);
            if (braceDepth > 0 && tok.kind == TokenKind::Word &&
                ((IsAccessSpecifier(tok.text, language) && nextSig != nullptr &&
                  nextSig->kind == TokenKind::Colon) ||
                 tok.text == "case" || tok.text == "default")) {
                lineIndent = std::max(0, braceDepth - 1);
            }
            ensureIndent(lineIndent);
            pendingBlankLines = 0;
        } else if (prevSig != nullptr) {
            // Decide whether a space is needed between `prevSig` and `tok`.
            bool needSpace = false;
            if (tok.kind == TokenKind::Comma || tok.kind == TokenKind::Semicolon ||
                tok.kind == TokenKind::CloseParen || tok.kind == TokenKind::CloseBracket) {
                needSpace = false;
            } else if (tok.kind == TokenKind::Colon) {
                needSpace = (prevSig->kind == TokenKind::Question);
                // In Kotlin/JS/TS/Java/C++ ternary `a ? b : c`, space before `:`
                // is kept simple; CSS/SCSS `color: red` has no space before `:`.
            } else if (prevSig->kind == TokenKind::OpenParen ||
                       prevSig->kind == TokenKind::OpenBracket ||
                       prevSig->kind == TokenKind::OpenBrace) {
                needSpace = false;
            } else if (prevSig->kind == TokenKind::Comma ||
                       prevSig->kind == TokenKind::Semicolon) {
                needSpace = true;
            } else if (prevSig->kind == TokenKind::Colon) {
                needSpace = !IsCssFamily(language) ||
                            (nextSig != nullptr && tok.kind != TokenKind::Word) ||
                            (tok.kind == TokenKind::Word && prevSig->kind == TokenKind::Colon);
                // Avoid space in CSS pseudo-selectors like `a:hover` where `:` follows a word
                // and is not inside a `{ ... }` declaration block, or when `:` is followed by
                // `hover`/`focus`/`before`? Wait: in CSS/SCSS, `color: red;` is inside `{}` with `;`!
                // Even simpler: in CSS/SCSS, if `braceDepth > 0` and we aren't `&:...`, space after `:`.
                if (IsCssFamily(language)) {
                    const bool isScssVarDecl =
                        braceDepth == 0 && i >= 2 && !tokens[i - 2].text.empty() &&
                        tokens[i - 2].text[0] == '$';
                    const bool isBlockProp =
                        braceDepth > 0 && i >= 2 && tokens[i - 2].text != "&" &&
                        tokens[i - 2].text != ":";
                    needSpace = isScssVarDecl || isBlockProp;
                }
            } else if (tok.kind == TokenKind::OpenParen) {
                const bool prevIsGenericClose = [&]() -> bool {
                    if (prevSig->kind != TokenKind::Operator || prevSig->text != ">") {
                        return false;
                    }
                    for (std::size_t back = i; back > 0; --back) {
                        if (tokens[back - 1].kind != TokenKind::Newline) {
                            return isGenericAngle[back - 1] != 0;
                        }
                    }
                    return false;
                }();
                needSpace = !prevIsGenericClose &&
                            ((prevSig->kind == TokenKind::Word && IsControlKeyword(prevSig->text)) ||
                             prevSig->kind == TokenKind::Operator ||
                             prevSig->kind == TokenKind::Comma ||
                             prevSig->kind == TokenKind::Colon ||
                             prevSig->kind == TokenKind::Question);
            } else if (tok.kind == TokenKind::OpenBracket) {
                needSpace = prevSig->kind == TokenKind::Operator ||
                            prevSig->kind == TokenKind::Comma ||
                            prevSig->kind == TokenKind::Colon ||
                            prevSig->kind == TokenKind::Word && IsControlKeyword(prevSig->text);
            } else if (tok.kind == TokenKind::Operator) {
                if (isGenericAngle[i]) {
                    if (tok.text == "<") {
                        needSpace = (prevSig->kind == TokenKind::Colon ||
                                     prevSig->kind == TokenKind::Comma ||
                                     (prevSig->kind == TokenKind::Operator &&
                                      (prevSig->text == "=" || prevSig->text == "=>")));
                    } else {
                        needSpace = false;
                    }
                } else if (tok.text == "++" || tok.text == "--") {
                    needSpace = false;
                } else if (IsBinarySpacedOp(tok.text)) {
                    // Check for unary `+`, `-`, `*`, `&`, `~`, `!`
                    if ((tok.text == "+" || tok.text == "-" || tok.text == "*" ||
                         tok.text == "&" || tok.text == "!" || tok.text == "~") &&
                        i > 0) {
                        // Look back to token before `tok` (excluding newlines).
                        const Token* beforeTok = prevSig;
                        needSpace = !IsUnaryPrefixAllowed(beforeTok);
                        // Wait: if `beforeTok` is `,`, `;`, `=`, `return`, we STILL want a space
                        // before `-1` or `!flag`, e.g. `return -1;` or `foo(a, -1)`!
                        if (beforeTok != nullptr &&
                            (beforeTok->kind == TokenKind::Comma ||
                             beforeTok->kind == TokenKind::Semicolon ||
                             beforeTok->kind == TokenKind::Colon ||
                             beforeTok->kind == TokenKind::Question ||
                             beforeTok->kind == TokenKind::Operator ||
                             (beforeTok->kind == TokenKind::Word &&
                              (beforeTok->text == "return" || beforeTok->text == "throw" ||
                               beforeTok->text == "case" || beforeTok->text == "else")))) {
                            needSpace = true;
                        }
                    } else {
                        needSpace = true;
                    }
                }
            } else if (prevSig->kind == TokenKind::Operator) {
                const bool prevIsGeneric = [&]() -> bool {
                    for (std::size_t back = i; back > 0; --back) {
                        if (tokens[back - 1].kind != TokenKind::Newline) {
                            return isGenericAngle[back - 1] != 0;
                        }
                    }
                    return false;
                }();
                if (prevIsGeneric) {
                    if (prevSig->text == "<") {
                        needSpace = false;
                    } else {
                        // Closing `>` of a generic: space before `Word`, `{`, `=`, `|`, `&`, `=>`,
                        // but not before `(`, `)`, `,`, `;`, `[`, `.`, `>`.
                        needSpace = (tok.kind == TokenKind::Word ||
                                     tok.kind == TokenKind::OpenBrace ||
                                     (tok.kind == TokenKind::Operator &&
                                      (tok.text == "=" || tok.text == "|" || tok.text == "&" ||
                                       tok.text == "=>")));
                    }
                } else if (prevSig->text == "." || prevSig->text == "::" || prevSig->text == "->" ||
                    prevSig->text == "?." || prevSig->text == ".*" || prevSig->text == "->*" ||
                    prevSig->text == "!" || prevSig->text == "~" || prevSig->text == "#{") {
                    needSpace = false;
                } else if (prevSig->text == "++" || prevSig->text == "--") {
                    needSpace = (tok.kind == TokenKind::Word);
                } else if (prevSig->text == "+" || prevSig->text == "-" || prevSig->text == "*" ||
                           prevSig->text == "&") {
                    // Check the token before `prevSig` to see if `prevSig` was unary.
                    const Token* beforePrev = nullptr;
                    for (std::size_t back = i; back > 0; --back) {
                        if (tokens[back - 1].kind != TokenKind::Newline &&
                            &tokens[back - 1] != prevSig) {
                            beforePrev = &tokens[back - 1];
                            break;
                        }
                    }
                    needSpace = !IsUnaryPrefixAllowed(beforePrev);
                } else {
                    needSpace = true;
                }
            } else {
                needSpace = true;
            }

            if (needSpace) {
                out.push_back(' ');
            }
        }

        if (tok.kind == TokenKind::OpenParen || tok.kind == TokenKind::OpenBracket) {
            ++parenDepth;
        } else if (tok.kind == TokenKind::CloseParen || tok.kind == TokenKind::CloseBracket) {
            parenDepth = std::max(0, parenDepth - 1);
        }

        out += tok.text;

        // Line breaks after `;` (outside `for (...)` parens) or `case ...:` / `public:`.
        if (tok.kind == TokenKind::Semicolon && parenDepth == 0) {
            if (nextSig != nullptr && nextSig->kind != TokenKind::CloseBrace &&
                nextSig->kind != TokenKind::LineComment &&
                nextSig->kind != TokenKind::BlockComment) {
                emitNewline(0);
            }
        } else if (tok.kind == TokenKind::Colon && parenDepth == 0 && braceDepth > 0 &&
                   prevSig != nullptr && prevSig->kind == TokenKind::Word &&
                   IsAccessSpecifier(prevSig->text, language)) {
            if (nextSig != nullptr && nextSig->kind != TokenKind::CloseBrace &&
                nextSig->kind != TokenKind::LineComment) {
                emitNewline(0);
            }
        }

        prevSig = &tok;
    }

    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\n')) {
        out.pop_back();
    }
    if (!out.empty()) {
        out.push_back('\n');
    }
    return out;
}

}  // namespace

FormatResult FormatBraceLanguage(std::string_view input, Language language,
                                 const FormatOptions& options,
                                 const std::atomic_bool* cancel) {
    FormatResult result;
    result.language = language;

    bool canceled = false;
    const std::vector<Token> inputTokens = Tokenize(input, language, cancel, canceled);
    if (canceled) {
        result.canceled = true;
        return result;
    }

    std::string formatted =
        options.minify ? MinifyTokens(inputTokens, cancel, canceled)
                       : FormatTokens(inputTokens, language, options, cancel, canceled);
    if (canceled) {
        result.canceled = true;
        return result;
    }

    // Safety net: verify lossless token equivalence.
    const std::vector<Token> outputTokens = Tokenize(formatted, language, cancel, canceled);
    if (canceled) {
        result.canceled = true;
        return result;
    }
    if (!TokensEquivalent(inputTokens, outputTokens, options.minify)) {
        result.error.message = "formatter produced non-equivalent output";
        return result;
    }

    // Idempotency check.
    const std::string second =
        options.minify ? MinifyTokens(outputTokens, cancel, canceled)
                       : FormatTokens(outputTokens, language, options, cancel, canceled);
    if (!canceled && second != formatted) {
        result.idempotent = false;
    }

    result.output = std::move(formatted);
    result.ok = true;
    return result;
}

}  // namespace ArgpiSuite::Format

#include "FormatLanguage.h"

#include <algorithm>
#include <cctype>

namespace ArgpiSuite::Format {
namespace {

bool IsJsonWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

bool IsAsciiDigit(char ch) { return ch >= '0' && ch <= '9'; }

bool IsIdentifierStartChar(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return ch == '_' || ch == '$' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           uch >= 0x80;
}

bool IsIdentifierPartChar(char ch) { return IsIdentifierStartChar(ch) || IsAsciiDigit(ch); }

std::string LowerAscii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string_view BaseName(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool IsJsonLikeName(std::string_view lowerName) {
    static constexpr std::string_view kNames[] = {
        ".babelrc",       ".eslintrc",      ".prettierrc",   ".stylelintrc",
        ".swcrc",         ".jshintrc",      ".hintrc",       "tsconfig.json",
        "jsconfig.json",  "package.json",   "composer.json", "package-lock.json",
        "npm-shrinkwrap.json", "settings.json", ".vscode/settings.json",
    };
    for (std::string_view name : kNames) {
        if (EndsWith(lowerName, name)) {
            return true;
        }
    }
    return false;
}

}  // namespace

const wchar_t* LanguageDisplayName(Language language) {
    switch (language) {
        case Language::Json:
            return L"JSON";
        case Language::Json5:
            return L"JSON5";
        case Language::Jsonc:
            return L"JSON with comments";
        case Language::Css:
            return L"CSS";
        case Language::Scss:
            return L"SCSS";
        case Language::Less:
            return L"Less";
        case Language::Yaml:
            return L"YAML";
        case Language::Markdown:
            return L"Markdown";
        case Language::JavaScript:
            return L"JavaScript";
        case Language::TypeScript:
            return L"TypeScript";
        case Language::Jsx:
            return L"JavaScript (JSX)";
        case Language::Tsx:
            return L"TypeScript (TSX)";
        case Language::Html:
            return L"HTML";
        case Language::Vue:
            return L"Vue";
        case Language::Xml:
            return L"XML";
        case Language::Php:
            return L"PHP";
        case Language::Java:
            return L"Java";
        case Language::Kotlin:
            return L"Kotlin";
        case Language::Gradle:
            return L"Gradle";
        case Language::C:
            return L"C";
        case Language::Cpp:
            return L"C++";
        case Language::Unknown:
        default:
            return L"Unknown";
    }
}

bool IsLanguageImplemented(Language language) {
    switch (language) {
        case Language::Json:
        case Language::Json5:
        case Language::Jsonc:
        case Language::Css:
        case Language::Scss:
        case Language::Less:
        case Language::Yaml:
        case Language::Markdown:
        case Language::JavaScript:
        case Language::TypeScript:
        case Language::Jsx:
        case Language::Tsx:
        case Language::Html:
        case Language::Vue:
        case Language::Xml:
        case Language::Php:
        case Language::Java:
        case Language::Kotlin:
        case Language::Gradle:
        case Language::C:
        case Language::Cpp:
            return true;
        default:
            return false;
    }
}

Language DetectJsonDialect(std::string_view path) {
    const std::string lower = LowerAscii(BaseName(path));
    if (EndsWith(lower, ".json5") || lower == ".babelrc") {
        return Language::Json5;
    }
    if (EndsWith(lower, ".jsonc") || lower == "tsconfig.json" || lower == "jsconfig.json" ||
        lower == ".eslintrc" || lower == "settings.json") {
        return Language::Jsonc;
    }
    return Language::Json;
}

Language DetectLanguageFromPath(std::string_view path) {
    if (path.empty()) {
        return Language::Unknown;
    }
    const std::string lowerPath = LowerAscii(path);
    const std::string lowerName = LowerAscii(BaseName(path));

    if (EndsWith(lowerName, ".json5") || lowerName == ".babelrc") {
        return Language::Json5;
    }
    if (EndsWith(lowerName, ".jsonc")) {
        return Language::Jsonc;
    }
    if (EndsWith(lowerName, ".json") || IsJsonLikeName(lowerName)) {
        return Language::Json;
    }
    if (EndsWith(lowerName, ".css")) {
        return Language::Css;
    }
    if (EndsWith(lowerName, ".scss")) {
        return Language::Scss;
    }
    if (EndsWith(lowerName, ".less")) {
        return Language::Less;
    }
    if (EndsWith(lowerName, ".yaml") || EndsWith(lowerName, ".yml")) {
        return Language::Yaml;
    }
    if (EndsWith(lowerName, ".md") || EndsWith(lowerName, ".markdown") ||
        EndsWith(lowerName, ".mdx")) {
        return Language::Markdown;
    }
    if (EndsWith(lowerName, ".tsx")) {
        return Language::Tsx;
    }
    if (EndsWith(lowerName, ".ts") || EndsWith(lowerName, ".mts") ||
        EndsWith(lowerName, ".cts")) {
        return Language::TypeScript;
    }
    if (EndsWith(lowerName, ".jsx")) {
        return Language::Jsx;
    }
    if (EndsWith(lowerName, ".js") || EndsWith(lowerName, ".mjs") ||
        EndsWith(lowerName, ".cjs")) {
        return Language::JavaScript;
    }
    if (EndsWith(lowerName, ".html") || EndsWith(lowerName, ".htm") ||
        EndsWith(lowerName, ".xhtml")) {
        return Language::Html;
    }
    if (EndsWith(lowerName, ".vue")) {
        return Language::Vue;
    }
    if (EndsWith(lowerName, ".gradle")) {
        return Language::Gradle;
    }
    if (EndsWith(lowerName, ".kt") || EndsWith(lowerName, ".kts")) {
        return Language::Kotlin;
    }
    if (EndsWith(lowerName, ".java")) {
        return Language::Java;
    }
    if (EndsWith(lowerName, ".php") || EndsWith(lowerName, ".phtml")) {
        return Language::Php;
    }
    if (EndsWith(lowerName, ".xml") || EndsWith(lowerName, ".xsd") ||
        EndsWith(lowerName, ".xsl") || EndsWith(lowerName, ".xslt") ||
        EndsWith(lowerName, ".svg") || EndsWith(lowerName, ".plist") ||
        EndsWith(lowerName, ".csproj") || EndsWith(lowerName, ".vcxproj") ||
        EndsWith(lowerName, ".props") || EndsWith(lowerName, ".targets") ||
        EndsWith(lowerName, ".config") || EndsWith(lowerName, ".resx") ||
        EndsWith(lowerName, ".manifest")) {
        return Language::Xml;
    }
    if (EndsWith(lowerName, ".c")) {
        return Language::C;
    }
    if (EndsWith(lowerName, ".h") || EndsWith(lowerName, ".hpp") ||
        EndsWith(lowerName, ".hh") || EndsWith(lowerName, ".hxx") ||
        EndsWith(lowerName, ".cpp") || EndsWith(lowerName, ".cc") ||
        EndsWith(lowerName, ".cxx")) {
        return Language::Cpp;
    }
    (void)lowerPath;
    return Language::Unknown;
}

Language DetectLanguageFromContent(std::string_view text) {
    // Ignore a UTF-8 BOM and leading whitespace; only a top-level object or
    // array is treated as JSON. Scalar documents are too easy to confuse with
    // ordinary text (a single quoted word, a number, `true`, ...).
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    std::size_t start = 0;
    while (start < text.size() && IsJsonWhitespace(text[start])) {
        ++start;
    }
    if (start >= text.size() || (text[start] != '{' && text[start] != '[')) {
        return Language::Unknown;
    }

    bool json5 = false;
    bool inString = false;
    char quote = '\0';
    bool escaped = false;
    bool inLineComment = false;
    bool inBlockComment = false;

    for (std::size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];

        if (inLineComment) {
            if (ch == '\n' || ch == '\r') {
                inLineComment = false;
            }
            continue;
        }
        if (inBlockComment) {
            if (ch == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
                // JSON5-only escape sequences (strict JSON allows only
                // \" \\ \/ \b \f \n \r \t \uXXXX).
                if (ch == 'x' || ch == 'v' || ch == '0' || ch == '\n' || ch == '\r') {
                    json5 = true;
                }
                continue;
            }
            if (ch == '\\') {
                escaped = true;
                continue;
            }
            if (ch == quote) {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            quote = ch;
            continue;
        }
        if (ch == '\'') {
            // Single-quoted strings are valid in JSON5 only.
            json5 = true;
            inString = true;
            quote = ch;
            continue;
        }
        if (ch == '/' && i + 1 < text.size()) {
            if (text[i + 1] == '/') {
                inLineComment = true;
                ++i;
                continue;
            }
            if (text[i + 1] == '*') {
                inBlockComment = true;
                ++i;
                continue;
            }
        }
        if (ch == '+') {
            // A plus sign is only legal in strict JSON as part of an exponent.
            if (i == 0 || (text[i - 1] != 'e' && text[i - 1] != 'E')) {
                json5 = true;
            }
            continue;
        }
        if (ch == '.' && (i == 0 || !IsAsciiDigit(text[i - 1])) && i + 1 < text.size() &&
            IsAsciiDigit(text[i + 1])) {
            // A leading decimal point (`0.5` stays strict JSON).
            json5 = true;
            continue;
        }
        if (ch == '0' && i + 1 < text.size() &&
            (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            json5 = true;
            continue;
        }
        if (IsIdentifierStartChar(ch)) {
            std::size_t end = i + 1;
            while (end < text.size() && IsIdentifierPartChar(text[end])) {
                ++end;
            }
            const std::string_view word = text.substr(i, end - i);
            if (word == "Infinity" || word == "NaN") {
                json5 = true;
            }
            // An unquoted object key (identifier followed by `:`) is JSON5.
            std::size_t next = end;
            while (next < text.size() && IsJsonWhitespace(text[next])) {
                ++next;
            }
            if (next < text.size() && text[next] == ':') {
                json5 = true;
            }
            i = end - 1;
            continue;
        }
    }

    return json5 ? Language::Json5 : Language::Jsonc;
}


}  // namespace ArgpiSuite::Format

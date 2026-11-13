// ArgpiSuite native formatter — language registry.
#pragma once

#include <string>
#include <string_view>

namespace ArgpiSuite::Format {

enum class Language {
    Unknown,
    Json,
    Json5,
    Jsonc,
    Css,
    Scss,
    Less,
    Yaml,
    Markdown,
    JavaScript,
    TypeScript,
    Jsx,
    Tsx,
    Html,
    Vue,
    Xml,
    Php,
    Java,
    Kotlin,
    Gradle,
    C,
    Cpp,
};

const wchar_t* LanguageDisplayName(Language language);

/// True for languages with a complete native printer in this build.
bool IsLanguageImplemented(Language language);

/// Extension / well-known filename based detection. Case-insensitive.
Language DetectLanguageFromPath(std::string_view path);

/// Content sniffing used as a fallback when Notepad++ reports an unknown or
/// unsupported language. Returns `Jsonc` for a JSON-looking object/array
/// (strict JSON is a subset) and `Json5` when JSON5-only syntax is present.
/// Returns `Unknown` for anything else, including HTML and source code.
Language DetectLanguageFromContent(std::string_view text);

/// Dialect selection for the JSON family (special files such as .babelrc).
Language DetectJsonDialect(std::string_view path);

}  // namespace ArgpiSuite::Format

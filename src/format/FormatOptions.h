// ArgpiSuite native formatter — Prettier-compatible formatting options.
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <string>
#include <string_view>

#include "FormatLineEnding.h"

namespace ArgpiSuite::Format {

enum class QuoteProps { AsNeeded, Consistent, Preserve };
enum class TrailingComma { All, Es5, None };
enum class ArrowParens { Always, Avoid };
enum class ProseWrap { Preserve, Always, Never };
enum class HtmlWhitespaceSensitivity { Css, Strict, Ignore };
enum class EmbeddedLanguageFormatting { Auto, Off };
enum class ObjectWrap { Preserve, Collapse };

/// Union of Prettier 3.x options (only the ones ArgpiSuite honours are used).
struct FormatOptions {
    int printWidth = 80;
    int tabWidth = 2;
    bool useTabs = false;

    bool semi = true;
    bool singleQuote = false;
    bool jsxSingleQuote = false;
    QuoteProps quoteProps = QuoteProps::AsNeeded;
    TrailingComma trailingComma = TrailingComma::All;
    bool bracketSpacing = true;
    bool bracketSameLine = false;
    ArrowParens arrowParens = ArrowParens::Always;

    ProseWrap proseWrap = ProseWrap::Preserve;
    HtmlWhitespaceSensitivity htmlWhitespaceSensitivity = HtmlWhitespaceSensitivity::Css;
    Eol endOfLine = Eol::None;  // None == "auto" (keep the document's EOL)
    EmbeddedLanguageFormatting embeddedLanguageFormatting = EmbeddedLanguageFormatting::Auto;
    bool singleAttributePerLine = false;
    ObjectWrap objectWrap = ObjectWrap::Preserve;
    bool experimentalTernaries = false;

    // Plugin-only knobs (not part of Prettier's option set).
    bool minify = false;
    int timeoutMs = 5000;
    std::string filepath;  // used for config resolution / special files

    [[nodiscard]] bool operator==(const FormatOptions& other) const noexcept;
    [[nodiscard]] bool operator!=(const FormatOptions& other) const noexcept {
        return !(*this == other);
    }
};

const char* ToString(QuoteProps value) noexcept;
const char* ToString(TrailingComma value) noexcept;
const char* ToString(ArrowParens value) noexcept;
const char* ToString(ProseWrap value) noexcept;
const char* ToString(HtmlWhitespaceSensitivity value) noexcept;
const char* ToString(EmbeddedLanguageFormatting value) noexcept;
const char* ToString(ObjectWrap value) noexcept;
const char* ToString(Eol value) noexcept;

bool ParseQuoteProps(std::string_view text, QuoteProps& out) noexcept;
bool ParseTrailingComma(std::string_view text, TrailingComma& out) noexcept;
bool ParseArrowParens(std::string_view text, ArrowParens& out) noexcept;
bool ParseProseWrap(std::string_view text, ProseWrap& out) noexcept;
bool ParseHtmlWhitespaceSensitivity(std::string_view text, HtmlWhitespaceSensitivity& out) noexcept;
bool ParseEmbeddedLanguageFormatting(std::string_view text, EmbeddedLanguageFormatting& out) noexcept;
bool ParseObjectWrap(std::string_view text, ObjectWrap& out) noexcept;
bool ParseEol(std::string_view text, Eol& out) noexcept;
bool ParseBoolOption(std::string_view text, bool& out) noexcept;

/// Serializes options as `key=value` lines, for "Show Resolved Config".
std::string DescribeOptions(const FormatOptions& options);

}  // namespace ArgpiSuite::Format

#include "FormatOptions.h"

#include <sstream>
#include <string>

namespace ArgpiSuite::Format {
namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = static_cast<char>(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = static_cast<char>(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool ParseBool(std::string_view text, bool& out) {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "true") || text == "1" || EqualsIgnoreCase(text, "yes")) {
        out = true;
        return true;
    }
    if (EqualsIgnoreCase(text, "false") || text == "0" || EqualsIgnoreCase(text, "no")) {
        out = false;
        return true;
    }
    return false;
}

}  // namespace

bool FormatOptions::operator==(const FormatOptions& other) const noexcept {
    return printWidth == other.printWidth && tabWidth == other.tabWidth &&
           useTabs == other.useTabs && semi == other.semi && singleQuote == other.singleQuote &&
           jsxSingleQuote == other.jsxSingleQuote && quoteProps == other.quoteProps &&
           trailingComma == other.trailingComma && bracketSpacing == other.bracketSpacing &&
           bracketSameLine == other.bracketSameLine && arrowParens == other.arrowParens &&
           proseWrap == other.proseWrap &&
           htmlWhitespaceSensitivity == other.htmlWhitespaceSensitivity &&
           endOfLine == other.endOfLine &&
           embeddedLanguageFormatting == other.embeddedLanguageFormatting &&
           singleAttributePerLine == other.singleAttributePerLine &&
           objectWrap == other.objectWrap && experimentalTernaries == other.experimentalTernaries &&
           minify == other.minify;
}

const char* ToString(QuoteProps value) noexcept {
    switch (value) {
        case QuoteProps::Consistent:
            return "consistent";
        case QuoteProps::Preserve:
            return "preserve";
        case QuoteProps::AsNeeded:
        default:
            return "as-needed";
    }
}

const char* ToString(TrailingComma value) noexcept {
    switch (value) {
        case TrailingComma::Es5:
            return "es5";
        case TrailingComma::None:
            return "none";
        case TrailingComma::All:
        default:
            return "all";
    }
}

const char* ToString(ArrowParens value) noexcept {
    return value == ArrowParens::Avoid ? "avoid" : "always";
}

const char* ToString(ProseWrap value) noexcept {
    switch (value) {
        case ProseWrap::Always:
            return "always";
        case ProseWrap::Never:
            return "never";
        case ProseWrap::Preserve:
        default:
            return "preserve";
    }
}

const char* ToString(HtmlWhitespaceSensitivity value) noexcept {
    switch (value) {
        case HtmlWhitespaceSensitivity::Strict:
            return "strict";
        case HtmlWhitespaceSensitivity::Ignore:
            return "ignore";
        case HtmlWhitespaceSensitivity::Css:
        default:
            return "css";
    }
}

const char* ToString(EmbeddedLanguageFormatting value) noexcept {
    return value == EmbeddedLanguageFormatting::Off ? "off" : "auto";
}

const char* ToString(ObjectWrap value) noexcept {
    return value == ObjectWrap::Collapse ? "collapse" : "preserve";
}

const char* ToString(Eol value) noexcept { return EolName(value); }

bool ParseQuoteProps(std::string_view text, QuoteProps& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "as-needed")) {
        out = QuoteProps::AsNeeded;
    } else if (EqualsIgnoreCase(text, "consistent")) {
        out = QuoteProps::Consistent;
    } else if (EqualsIgnoreCase(text, "preserve")) {
        out = QuoteProps::Preserve;
    } else {
        return false;
    }
    return true;
}

bool ParseTrailingComma(std::string_view text, TrailingComma& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "all")) {
        out = TrailingComma::All;
    } else if (EqualsIgnoreCase(text, "es5")) {
        out = TrailingComma::Es5;
    } else if (EqualsIgnoreCase(text, "none")) {
        out = TrailingComma::None;
    } else {
        return false;
    }
    return true;
}

bool ParseArrowParens(std::string_view text, ArrowParens& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "always")) {
        out = ArrowParens::Always;
    } else if (EqualsIgnoreCase(text, "avoid")) {
        out = ArrowParens::Avoid;
    } else {
        return false;
    }
    return true;
}

bool ParseProseWrap(std::string_view text, ProseWrap& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "preserve")) {
        out = ProseWrap::Preserve;
    } else if (EqualsIgnoreCase(text, "always")) {
        out = ProseWrap::Always;
    } else if (EqualsIgnoreCase(text, "never")) {
        out = ProseWrap::Never;
    } else {
        return false;
    }
    return true;
}

bool ParseHtmlWhitespaceSensitivity(std::string_view text,
                                    HtmlWhitespaceSensitivity& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "css")) {
        out = HtmlWhitespaceSensitivity::Css;
    } else if (EqualsIgnoreCase(text, "strict")) {
        out = HtmlWhitespaceSensitivity::Strict;
    } else if (EqualsIgnoreCase(text, "ignore")) {
        out = HtmlWhitespaceSensitivity::Ignore;
    } else {
        return false;
    }
    return true;
}

bool ParseEmbeddedLanguageFormatting(std::string_view text,
                                     EmbeddedLanguageFormatting& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "auto")) {
        out = EmbeddedLanguageFormatting::Auto;
    } else if (EqualsIgnoreCase(text, "off")) {
        out = EmbeddedLanguageFormatting::Off;
    } else {
        return false;
    }
    return true;
}

bool ParseObjectWrap(std::string_view text, ObjectWrap& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "preserve")) {
        out = ObjectWrap::Preserve;
    } else if (EqualsIgnoreCase(text, "collapse")) {
        out = ObjectWrap::Collapse;
    } else {
        return false;
    }
    return true;
}

bool ParseEol(std::string_view text, Eol& out) noexcept {
    text = Trim(text);
    if (EqualsIgnoreCase(text, "lf")) {
        out = Eol::Lf;
    } else if (EqualsIgnoreCase(text, "crlf")) {
        out = Eol::Crlf;
    } else if (EqualsIgnoreCase(text, "cr")) {
        out = Eol::Cr;
    } else if (EqualsIgnoreCase(text, "auto")) {
        out = Eol::None;
    } else {
        return false;
    }
    return true;
}

// Helper used by the config resolver and the INI reader.
bool ParseBoolOption(std::string_view text, bool& out) noexcept { return ParseBool(text, out); }

std::string DescribeOptions(const FormatOptions& o) {
    std::ostringstream out;
    out << "printWidth = " << o.printWidth << "\n"
        << "tabWidth = " << o.tabWidth << "\n"
        << "useTabs = " << (o.useTabs ? "true" : "false") << "\n"
        << "semi = " << (o.semi ? "true" : "false") << "\n"
        << "singleQuote = " << (o.singleQuote ? "true" : "false") << "\n"
        << "quoteProps = " << ToString(o.quoteProps) << "\n"
        << "trailingComma = " << ToString(o.trailingComma) << "\n"
        << "bracketSpacing = " << (o.bracketSpacing ? "true" : "false") << "\n"
        << "bracketSameLine = " << (o.bracketSameLine ? "true" : "false") << "\n"
        << "arrowParens = " << ToString(o.arrowParens) << "\n"
        << "proseWrap = " << ToString(o.proseWrap) << "\n"
        << "htmlWhitespaceSensitivity = " << ToString(o.htmlWhitespaceSensitivity) << "\n"
        << "endOfLine = " << ToString(o.endOfLine) << "\n"
        << "embeddedLanguageFormatting = " << ToString(o.embeddedLanguageFormatting) << "\n"
        << "singleAttributePerLine = " << (o.singleAttributePerLine ? "true" : "false") << "\n"
        << "objectWrap = " << ToString(o.objectWrap) << "\n"
        << "experimentalTernaries = " << (o.experimentalTernaries ? "true" : "false") << "\n"
        << "minify = " << (o.minify ? "true" : "false") << "\n"
        << "timeoutMs = " << o.timeoutMs;
    return out.str();
}

}  // namespace ArgpiSuite::Format

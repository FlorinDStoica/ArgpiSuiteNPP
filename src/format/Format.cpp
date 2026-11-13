#include "Format.h"

#include <string>

#include "FormatLineEnding.h"
#include "FormatUnicode.h"
#include "brace/FormatBraceFormatter.h"
#include "json/FormatJsonEquivalence.h"
#include "json/FormatJsonParser.h"
#include "json/FormatJsonPrinter.h"
#include "markdown/FormatMarkdownFormatter.h"
#include "xml/FormatMarkupFormatter.h"
#include "yaml/FormatYamlFormatter.h"

namespace ArgpiSuite::Format {
namespace {

bool IsCanceled(const std::atomic_bool* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

bool IsAllWhitespace(std::string_view text) {
    for (char ch : text) {
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '\f' && ch != '\v') {
            return false;
        }
    }
    return true;
}

FormatResult FormatJsonFamily(std::string_view input, Language language,
                              const FormatOptions& options, const std::atomic_bool* cancel) {
    FormatResult result;
    result.language = language;

    JsonParseResult parsed = ParseJson(input, language);
    if (!parsed.ok) {
        result.error = parsed.error;
        return result;
    }
    AttachJsonComments(parsed.document, input);

    JsonPrintResult printed = PrintJson(parsed.document, input, options, cancel);
    if (printed.canceled) {
        result.canceled = true;
        return result;
    }
    if (!printed.ok) {
        result.error = printed.error;
        return result;
    }

    // Safety net: the output must parse and describe the same value.
    JsonParseResult reparsed = ParseJson(printed.text, language);
    if (!reparsed.ok) {
        result.error.message = "formatter produced invalid JSON: " + reparsed.error.message;
        return result;
    }
    if (!JsonEquivalent(parsed.document, reparsed.document)) {
        result.error.message = "formatter produced non-equivalent output";
        return result;
    }

    // Idempotency check. A non-idempotent result is still applied when it is
    // equivalent, but the caller logs the warning.
    AttachJsonComments(reparsed.document, printed.text);
    JsonPrintResult second = PrintJson(reparsed.document, printed.text, options, cancel);
    if (!second.canceled && second.ok && second.text != printed.text) {
        result.idempotent = false;
    }

    result.output = std::move(printed.text);
    result.ok = true;
    return result;
}

}  // namespace

FormatResult FormatText(std::string_view input, Language language, const FormatOptions& options,
                        const std::atomic_bool* cancel) {
    FormatResult result;
    result.language = language;

    if (IsCanceled(cancel)) {
        result.canceled = true;
        return result;
    }

    bool hasBom = false;
    if (input.size() >= 3 && static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF) {
        hasBom = true;
        input.remove_prefix(3);
    }

    if (!IsLanguageImplemented(language)) {
        result.unsupported = true;
        result.error.message = "language not supported yet";
        return result;
    }

    if (IsAllWhitespace(input)) {
        result.ok = true;
        result.idempotent = true;
        return result;
    }

    const Eol detected = DetectEol(input);
    Eol target = options.endOfLine;
    if (target == Eol::None || target == Eol::Mixed) {
        target = (detected == Eol::None || detected == Eol::Mixed) ? Eol::Lf : detected;
    }

    switch (language) {
        case Language::Json:
        case Language::Json5:
        case Language::Jsonc:
            result = FormatJsonFamily(input, language, options, cancel);
            break;
        case Language::Css:
        case Language::Scss:
        case Language::Less:
        case Language::JavaScript:
        case Language::TypeScript:
        case Language::Jsx:
        case Language::Tsx:
        case Language::Java:
        case Language::Kotlin:
        case Language::Gradle:
        case Language::C:
        case Language::Cpp:
            result = FormatBraceLanguage(input, language, options, cancel);
            break;
        case Language::Html:
        case Language::Vue:
        case Language::Xml:
        case Language::Php:
            result = FormatMarkupLanguage(input, language, options, cancel);
            break;
        case Language::Yaml:
            result = FormatYamlLanguage(input, options, cancel);
            break;
        case Language::Markdown:
            result = FormatMarkdownLanguage(input, options, cancel);
            break;
        default:
            result.unsupported = true;
            result.error.message = "language not supported yet";
            return result;
    }

    if (!result.ok) {
        return result;
    }

    result.output = NormalizeEol(result.output, target);
    if (hasBom) {
        result.output.insert(0, "\xEF\xBB\xBF");
    }
    return result;
}

}  // namespace ArgpiSuite::Format

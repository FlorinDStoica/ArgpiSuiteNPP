// ArgpiSuite native formatter — public entry point.
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "FormatError.h"
#include "FormatLanguage.h"
#include "FormatOptions.h"

namespace ArgpiSuite::Format {

struct FormatResult {
    bool ok = false;
    bool unsupported = false;
    bool canceled = false;
    bool idempotent = true;
    Language language = Language::Unknown;
    std::string output;
    FormatError error;
};

/// Formats `input`. Never throws for syntax errors; cancellation is reported
/// through `canceled`. The returned text uses the requested end-of-line style
/// and always ends with exactly one newline for non-empty documents.
FormatResult FormatText(std::string_view input, Language language, const FormatOptions& options,
                        const std::atomic_bool* cancel = nullptr);

}  // namespace ArgpiSuite::Format

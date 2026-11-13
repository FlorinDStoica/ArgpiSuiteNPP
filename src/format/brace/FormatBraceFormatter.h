// ArgpiSuite native formatter — token-aware curly-brace formatter.
// Supports C, C++, Java, Kotlin (.kt/.kts), Gradle (.gradle), JavaScript,
// CSS/SCSS/Less, and PHP code blocks. Zero Win32 / Notepad++ dependencies.
#pragma once

#include <atomic>
#include <string_view>

#include "Format.h"
#include "FormatLanguage.h"
#include "FormatOptions.h"

namespace ArgpiSuite::Format {

FormatResult FormatBraceLanguage(std::string_view input, Language language,
                                 const FormatOptions& options,
                                 const std::atomic_bool* cancel = nullptr);

}  // namespace ArgpiSuite::Format


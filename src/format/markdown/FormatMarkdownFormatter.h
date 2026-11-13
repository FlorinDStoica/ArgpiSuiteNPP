// ArgpiSuite native formatter — Markdown formatter.
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <atomic>
#include <string_view>

#include "Format.h"
#include "FormatOptions.h"

namespace ArgpiSuite::Format {

FormatResult FormatMarkdownLanguage(std::string_view input, const FormatOptions& options,
                                    const std::atomic_bool* cancel = nullptr);

}  // namespace ArgpiSuite::Format


// ArgpiSuite native formatter — lossless XML, HTML, and PHP formatter.
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <atomic>
#include <string_view>

#include "Format.h"
#include "FormatLanguage.h"
#include "FormatOptions.h"

namespace ArgpiSuite::Format {

FormatResult FormatMarkupLanguage(std::string_view input, Language language,
                                  const FormatOptions& options,
                                  const std::atomic_bool* cancel = nullptr);

}  // namespace ArgpiSuite::Format


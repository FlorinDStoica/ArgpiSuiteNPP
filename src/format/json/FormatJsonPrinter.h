// ArgpiSuite native formatter — JSON family printer.
#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include "FormatError.h"
#include "FormatOptions.h"
#include "FormatJsonParser.h"

namespace ArgpiSuite::Format {

struct JsonPrintResult {
    bool ok = false;
    bool canceled = false;
    std::string text;
    FormatError error;
    int unprintedComments = 0;
};

JsonPrintResult PrintJson(const JsonDocument& document, std::string_view source,
                          const FormatOptions& options, const std::atomic_bool* cancel = nullptr);

}  // namespace ArgpiSuite::Format

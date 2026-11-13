// ArgpiSuite native formatter — error reporting.
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <cstddef>
#include <string>

namespace ArgpiSuite::Format {

/// A syntax/format failure. Offsets are UTF-8 byte offsets into the input.
struct FormatError {
    std::string message;
    std::size_t offset = 0;
    int line = 1;    // 1-based
    int column = 1;  // 1-based, counted in code points

    [[nodiscard]] bool IsValid() const noexcept { return !message.empty(); }
};

}  // namespace ArgpiSuite::Format

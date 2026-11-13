// ArgpiSuite native formatter — Prettier's printDocToString (Wadler style).
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>

#include "FormatDoc.h"
#include "FormatLineEnding.h"

namespace ArgpiSuite::Format {

struct DocPrintOptions {
    int printWidth = 80;
    int tabWidth = 2;
    bool useTabs = false;
    Eol endOfLine = Eol::Lf;
};

struct DocPrintResult {
    std::string text;
    bool canceled = false;
    std::size_t cursorPosition = std::string::npos;
};

/// Renders a Doc tree. `cancel` may be null; when set it is polled regularly.
DocPrintResult PrintDoc(const DocArena& arena, DocId root, const DocPrintOptions& options,
                        const std::atomic_bool* cancel = nullptr);

}  // namespace ArgpiSuite::Format

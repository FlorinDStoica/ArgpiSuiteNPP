// ArgpiSuite CSV / TSV Column Alignment & Filtering Dialog.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

#include "plugin/Settings.h"

namespace ArgpiSuite::Tools {

struct CsvDialogInput {
    bool hasSelection = false;
    std::string selectionText;
    std::string documentText;
    std::string documentEol = "\r\n";
};

using CsvApplyCallback = std::function<bool(const std::string& utf8Text, bool applyToSelection)>;

/// Shows the modal CSV / TSV Column Alignment & Filtering dialog.
void ShowCsvToolDialog(HWND parent, Plugin::Settings& settings, const CsvDialogInput& input,
                       CsvApplyCallback onApply);

}  // namespace ArgpiSuite::Tools


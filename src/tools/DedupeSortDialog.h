// ArgpiSuite Smart Deduplication & Sorting Dialog.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

#include "plugin/Settings.h"

namespace ArgpiSuite::Tools {

struct DedupeSortDialogInput {
    bool hasSelection = false;
    std::string selectionText;
    std::string documentText;
    std::string documentEol = "\r\n";
};

using DedupeApplyCallback = std::function<bool(const std::string& utf8Text, bool applyToSelection)>;

/// Shows the modal Smart Deduplication & Sorting dialog.
void ShowDedupeSortDialog(HWND parent, Plugin::Settings& settings,
                          const DedupeSortDialogInput& input, DedupeApplyCallback onApply);

}  // namespace ArgpiSuite::Tools


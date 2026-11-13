// ArgpiSuite JSON / XML / SQL Path Querying Dialog.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

#include "plugin/Settings.h"

namespace ArgpiSuite::Tools {

struct PathQueryDialogInput {
    bool hasSelection = false;
    std::string selectionText;
    std::string documentText;
};

using PathApplyCallback = std::function<bool(const std::string& utf8Text, bool applyToSelection)>;

/// Shows the modal JSON / XML / SQL Path Querying dialog.
void ShowPathQueryDialog(HWND parent, Plugin::Settings& settings, const PathQueryDialogInput& input,
                         PathApplyCallback onApply);

}  // namespace ArgpiSuite::Tools


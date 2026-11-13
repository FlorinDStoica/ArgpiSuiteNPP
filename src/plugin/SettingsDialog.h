// ArgpiSuite plugin — Format settings dialog built from an in-memory template.
// No .rc resource IDs are used.
#pragma once

#include <windows.h>

#include "Settings.h"

namespace ArgpiSuite::Plugin {

/// Shows the modal Format settings dialog. Returns true when the user pressed
/// OK; `settings` already contains the edited values in that case.
bool ShowFormatSettings(HWND owner, Settings& settings);

}  // namespace ArgpiSuite::Plugin

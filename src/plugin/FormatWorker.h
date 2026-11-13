// ArgpiSuite plugin — folder wide batch formatting (worker thread + progress).
#pragma once

#include <windows.h>

#include "Settings.h"

namespace ArgpiSuite::Plugin {

/// Lets the user pick a folder and formats every matching file recursively.
/// The worker thread only touches the file system; all UI updates are polled by
/// a timer on the UI thread, so Scintilla and menus are never touched from it.
void FormatFolderInteractive(HWND owner, const Settings& settings);

}  // namespace ArgpiSuite::Plugin

#pragma once

#include <windows.h>
#include "plugin/Settings.h"

namespace ArgpiSuite::Crypto {

/// Shows modal Encoding / Decoding Settings dialog.
void ShowEncodingSettingsDialog(HWND parent, Plugin::Settings& settings);

}  // namespace ArgpiSuite::Crypto


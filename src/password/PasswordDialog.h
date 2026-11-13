// ArgpiSuite Password & Hash Generator Dialog.
// Implements the complete GUI for password generation, hashing, presets,
// verification, clipboard management, and editor insertion.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "plugin/Settings.h"

namespace ArgpiSuite::Password {

/// Shows the modeless/modal Password & Hash Generator dialog.
/// If `owner` is provided, centers relative to the Notepad++ window.
void ShowPasswordDialog(HWND owner, Plugin::Settings& settings);

/// Shows the Passphrase dialog for Diceware-style passphrase generation.
void ShowPassphraseDialog(HWND owner, Plugin::Settings& settings);

}  // namespace ArgpiSuite::Password

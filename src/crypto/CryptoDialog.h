#pragma once

#include <windows.h>
#include <string>

#include "CryptoService.h"
#include "plugin/Settings.h"

namespace ArgpiSuite::Crypto {

struct EncryptDialogResult {
    bool ok = false;
    std::wstring passphrase;
    CryptoOptions options;
};

struct DecryptDialogResult {
    bool ok = false;
    std::wstring passphrase;
};

/// Shows modal Encrypt dialog prompting for passphrase and cipher settings.
EncryptDialogResult ShowEncryptDialog(HWND parent, const std::wstring& targetDesc,
                                      const Plugin::Settings& settings,
                                      bool defaultArmor = true);

/// Shows modal Decrypt dialog prompting for passphrase.
DecryptDialogResult ShowDecryptDialog(HWND parent, const std::wstring& targetDesc,
                                      const std::wstring& containerInfo = L"");

/// Shows modal Crypto Settings dialog.
void ShowCryptoSettingsDialog(HWND parent, Plugin::Settings& settings);

}  // namespace ArgpiSuite::Crypto


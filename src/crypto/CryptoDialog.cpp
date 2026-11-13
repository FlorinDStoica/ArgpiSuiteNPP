#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CryptoDialog.h"

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "plugin/PluginHost.h"
#include "plugin/TextUtil.h"
#include "PluginDefinition.h"

namespace ArgpiSuite::Crypto {
namespace {

constexpr WORD kIdTargetLabel    = 40001;
constexpr WORD kIdPassEdit       = 40002;
constexpr WORD kIdPassShowBtn    = 40003;
constexpr WORD kIdConfirmEdit    = 40004;
constexpr WORD kIdCipherCombo    = 40005;
constexpr WORD kIdKdfCombo       = 40006;
constexpr WORD kIdArmorCheck     = 40007;
constexpr WORD kIdInfoLabel      = 40008;
constexpr WORD kIdItersEdit      = 40009;

// --- In-Memory Dialog Template Builder ---

void AlignDword(std::vector<unsigned char>& data) {
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
}

void WriteWord(std::vector<unsigned char>& data, WORD value) {
    data.push_back(static_cast<unsigned char>(value & 0xFF));
    data.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void WriteDword(std::vector<unsigned char>& data, DWORD value) {
    for (int i = 0; i < 4; ++i) {
        data.push_back(static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
    }
}

void WriteShort(std::vector<unsigned char>& data, short value) {
    WriteWord(data, static_cast<WORD>(value));
}

void WriteWide(std::vector<unsigned char>& data, const wchar_t* text) {
    if (text == nullptr) {
        WriteWord(data, 0);
        return;
    }
    for (const wchar_t* ch = text; *ch != L'\0'; ++ch) {
        WriteWord(data, static_cast<WORD>(*ch));
    }
    WriteWord(data, 0);
}

class TemplateBuilder {
public:
    TemplateBuilder(short widthDlu, short heightDlu, const wchar_t* title) {
        WriteDword(data_, DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        WriteDword(data_, 0);  // extended style
        WriteWord(data_, 0);   // item count (patched by Build())
        WriteShort(data_, 0);  // x
        WriteShort(data_, 0);  // y
        WriteShort(data_, widthDlu);
        WriteShort(data_, heightDlu);
        WriteWord(data_, 0);  // no menu
        WriteWord(data_, 0);  // default window class
        WriteWide(data_, title);
        WriteWord(data_, 9);  // 9pt Segoe UI
        WriteWide(data_, L"Segoe UI");
    }

    void AddStatic(short x, short y, short cx, short cy, const wchar_t* text, WORD id = 0xFFFF) {
        AddItem(WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, cx, cy, 0x0082, id, text);
    }

    void AddEdit(short x, short y, short cx, short cy, WORD id, bool password = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
        if (password) {
            style |= ES_PASSWORD;
        }
        AddItem(style, x, y, cx, cy, 0x0081, id, L"");
    }

    void AddCheckbox(short x, short y, short cx, short cy, WORD id, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, x, y, cx, cy, 0x0080, id, text);
    }

    void AddButton(short x, short y, short cx, short cy, WORD id, const wchar_t* text, bool defBtn = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
        if (defBtn) {
            style |= BS_DEFPUSHBUTTON;
        }
        AddItem(style, x, y, cx, cy, 0x0080, id, text);
    }

    void AddCombobox(short x, short y, short cx, short cy, WORD id) {
        AddItem(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                x, y, cx, cy, 0x0085, id, L"");
    }

    std::vector<unsigned char> Build() {
        const WORD count = static_cast<WORD>(items_);
        data_[8] = static_cast<unsigned char>(count & 0xFF);
        data_[9] = static_cast<unsigned char>((count >> 8) & 0xFF);
        return data_;
    }

private:
    void AddItem(DWORD style, short x, short y, short cx, short cy, WORD atom, WORD id,
                 const wchar_t* text) {
        AlignDword(data_);
        WriteDword(data_, style);
        WriteDword(data_, 0);  // extended style
        WriteShort(data_, x);
        WriteShort(data_, y);
        WriteShort(data_, cx);
        WriteShort(data_, cy);
        WriteWord(data_, id);
        WriteWord(data_, 0xFFFF);
        WriteWord(data_, atom);
        WriteWide(data_, text);
        WriteWord(data_, 0);  // no creation data
        ++items_;
    }

    std::vector<unsigned char> data_;
    int items_ = 0;
};

// --- Encrypt Dialog ---

struct EncryptDialogContext {
    std::wstring targetDesc;
    const Plugin::Settings* settings = nullptr;
    bool defaultArmor = true;
    bool passVisible = false;
    EncryptDialogResult result;
};

INT_PTR CALLBACK EncryptDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EncryptDialogContext*>(::GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<EncryptDialogContext*>(lParam);
            ::SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

            // Set target label
            ::SetDlgItemTextW(hwnd, kIdTargetLabel, ctx->targetDesc.c_str());

            // Populate Cipher combo
            HWND hCipher = ::GetDlgItem(hwnd, kIdCipherCombo);
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-256-GCM (Authenticated, Recommended)"));
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-256-CBC (HMAC-SHA256 Authenticated)"));
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-128-GCM (Authenticated)"));
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-128-CBC (HMAC Authenticated)"));

            int cipherIdx = 0;
            if (Plugin::EqualsIgnoreCase(ctx->settings->crypto.cipher, L"aes-256-cbc")) cipherIdx = 1;
            else if (Plugin::EqualsIgnoreCase(ctx->settings->crypto.cipher, L"aes-128-gcm")) cipherIdx = 2;
            else if (Plugin::EqualsIgnoreCase(ctx->settings->crypto.cipher, L"aes-128-cbc")) cipherIdx = 3;
            ::SendMessageW(hCipher, CB_SETCURSEL, cipherIdx, 0);

            // Populate KDF combo
            HWND hKdf = ::GetDlgItem(hwnd, kIdKdfCombo);
            ::SendMessageW(hKdf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PBKDF2-SHA256 (600,000 iterations)"));
            ::SendMessageW(hKdf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Argon2id (RFC 9106, 19 MiB memory)"));
            const int kdfIdx = Plugin::EqualsIgnoreCase(ctx->settings->crypto.kdf, L"argon2id") ? 1 : 0;
            ::SendMessageW(hKdf, CB_SETCURSEL, kdfIdx, 0);

            // Set Armor checkbox
            ::CheckDlgButton(hwnd, kIdArmorCheck, ctx->defaultArmor ? BST_CHECKED : BST_UNCHECKED);

            // Focus passphrase
            ::SetFocus(::GetDlgItem(hwnd, kIdPassEdit));
            return FALSE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if (id == kIdPassShowBtn && code == BN_CLICKED) {
                ctx->passVisible = !ctx->passVisible;
                HWND hPass = ::GetDlgItem(hwnd, kIdPassEdit);
                HWND hConfirm = ::GetDlgItem(hwnd, kIdConfirmEdit);
                const wchar_t maskCh = ctx->passVisible ? 0 : 0x25CF; // black circle
                ::SendMessageW(hPass, EM_SETPASSWORDCHAR, maskCh, 0);
                ::SendMessageW(hConfirm, EM_SETPASSWORDCHAR, maskCh, 0);
                ::InvalidateRect(hPass, nullptr, TRUE);
                ::InvalidateRect(hConfirm, nullptr, TRUE);
                ::SetDlgItemTextW(hwnd, kIdPassShowBtn, ctx->passVisible ? L"Hide" : L"Show");
                return TRUE;
            }

            if (id == IDOK) {
                wchar_t passBuf[512] = {};
                wchar_t confirmBuf[512] = {};
                ::GetDlgItemTextW(hwnd, kIdPassEdit, passBuf, 512);
                ::GetDlgItemTextW(hwnd, kIdConfirmEdit, confirmBuf, 512);

                if (passBuf[0] == L'\0') {
                    ::MessageBoxW(hwnd, L"Please enter a passphrase or password.", L"Passphrase Required",
                                  MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(hwnd, kIdPassEdit));
                    return TRUE;
                }

                if (std::wcscmp(passBuf, confirmBuf) != 0) {
                    ::MessageBoxW(hwnd, L"Passphrases do not match. Please re-enter.",
                                  L"Confirmation Mismatch", MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(hwnd, kIdConfirmEdit));
                    return TRUE;
                }

                ctx->result.ok = true;
                ctx->result.passphrase = passBuf;

                const int cSel = static_cast<int>(::SendMessageW(::GetDlgItem(hwnd, kIdCipherCombo), CB_GETCURSEL, 0, 0));
                if (cSel == 1) ctx->result.options.cipher = L"aes-256-cbc";
                else if (cSel == 2) ctx->result.options.cipher = L"aes-128-gcm";
                else if (cSel == 3) ctx->result.options.cipher = L"aes-128-cbc";
                else ctx->result.options.cipher = L"aes-256-gcm";

                const int kSel = static_cast<int>(::SendMessageW(::GetDlgItem(hwnd, kIdKdfCombo), CB_GETCURSEL, 0, 0));
                ctx->result.options.kdf = (kSel == 1) ? L"argon2id" : L"pbkdf2-sha256";
                ctx->result.options.iterations = ctx->settings->crypto.iterations;
                ctx->result.options.armorBase64 = (::IsDlgButtonChecked(hwnd, kIdArmorCheck) == BST_CHECKED);

                ::EndDialog(hwnd, IDOK);
                return TRUE;
            }

            if (id == IDCANCEL) {
                ctx->result.ok = false;
                ::EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// --- Decrypt Dialog ---

struct DecryptDialogContext {
    std::wstring targetDesc;
    std::wstring containerInfo;
    bool passVisible = false;
    DecryptDialogResult result;
};

INT_PTR CALLBACK DecryptDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<DecryptDialogContext*>(::GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<DecryptDialogContext*>(lParam);
            ::SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

            ::SetDlgItemTextW(hwnd, kIdTargetLabel, ctx->targetDesc.c_str());
            if (!ctx->containerInfo.empty()) {
                ::SetDlgItemTextW(hwnd, kIdInfoLabel, ctx->containerInfo.c_str());
            }

            ::SetFocus(::GetDlgItem(hwnd, kIdPassEdit));
            return FALSE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if (id == kIdPassShowBtn && code == BN_CLICKED) {
                ctx->passVisible = !ctx->passVisible;
                HWND hPass = ::GetDlgItem(hwnd, kIdPassEdit);
                const wchar_t maskCh = ctx->passVisible ? 0 : 0x25CF;
                ::SendMessageW(hPass, EM_SETPASSWORDCHAR, maskCh, 0);
                ::InvalidateRect(hPass, nullptr, TRUE);
                ::SetDlgItemTextW(hwnd, kIdPassShowBtn, ctx->passVisible ? L"Hide" : L"Show");
                return TRUE;
            }

            if (id == IDOK) {
                wchar_t passBuf[512] = {};
                ::GetDlgItemTextW(hwnd, kIdPassEdit, passBuf, 512);
                if (passBuf[0] == L'\0') {
                    ::MessageBoxW(hwnd, L"Please enter the passphrase to decrypt.",
                                  L"Passphrase Required", MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(hwnd, kIdPassEdit));
                    return TRUE;
                }

                ctx->result.ok = true;
                ctx->result.passphrase = passBuf;
                ::EndDialog(hwnd, IDOK);
                return TRUE;
            }

            if (id == IDCANCEL) {
                ctx->result.ok = false;
                ::EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

// --- Crypto Settings Dialog ---

struct CryptoSettingsDialogContext {
    Plugin::Settings* settings = nullptr;
};

INT_PTR CALLBACK CryptoSettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<CryptoSettingsDialogContext*>(::GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<CryptoSettingsDialogContext*>(lParam);
            ::SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

            HWND hCipher = ::GetDlgItem(hwnd, kIdCipherCombo);
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-256-GCM (Authenticated, Recommended)"));
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-256-CBC (HMAC-SHA256 Authenticated)"));
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-128-GCM (Authenticated)"));
            ::SendMessageW(hCipher, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AES-128-CBC (HMAC Authenticated)"));

            int cSel = 0;
            if (Plugin::EqualsIgnoreCase(ctx->settings->crypto.cipher, L"aes-256-cbc")) cSel = 1;
            else if (Plugin::EqualsIgnoreCase(ctx->settings->crypto.cipher, L"aes-128-gcm")) cSel = 2;
            else if (Plugin::EqualsIgnoreCase(ctx->settings->crypto.cipher, L"aes-128-cbc")) cSel = 3;
            ::SendMessageW(hCipher, CB_SETCURSEL, cSel, 0);

            HWND hKdf = ::GetDlgItem(hwnd, kIdKdfCombo);
            ::SendMessageW(hKdf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PBKDF2-SHA256"));
            ::SendMessageW(hKdf, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Argon2id (RFC 9106)"));
            ::SendMessageW(hKdf, CB_SETCURSEL, Plugin::EqualsIgnoreCase(ctx->settings->crypto.kdf, L"argon2id") ? 1 : 0, 0);

            ::SetDlgItemInt(hwnd, kIdItersEdit, static_cast<UINT>(ctx->settings->crypto.iterations), FALSE);
            ::CheckDlgButton(hwnd, kIdArmorCheck, ctx->settings->crypto.armorBase64 ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            if (id == IDOK) {
                const int cSel = static_cast<int>(::SendMessageW(::GetDlgItem(hwnd, kIdCipherCombo), CB_GETCURSEL, 0, 0));
                if (cSel == 1) ctx->settings->crypto.cipher = L"aes-256-cbc";
                else if (cSel == 2) ctx->settings->crypto.cipher = L"aes-128-gcm";
                else if (cSel == 3) ctx->settings->crypto.cipher = L"aes-128-cbc";
                else ctx->settings->crypto.cipher = L"aes-256-gcm";

                const int kSel = static_cast<int>(::SendMessageW(::GetDlgItem(hwnd, kIdKdfCombo), CB_GETCURSEL, 0, 0));
                ctx->settings->crypto.kdf = (kSel == 1) ? L"argon2id" : L"pbkdf2-sha256";

                BOOL translated = FALSE;
                const UINT iters = ::GetDlgItemInt(hwnd, kIdItersEdit, &translated, FALSE);
                if (translated && iters >= 1000) {
                    ctx->settings->crypto.iterations = static_cast<int>(iters);
                }
                ctx->settings->crypto.armorBase64 = (::IsDlgButtonChecked(hwnd, kIdArmorCheck) == BST_CHECKED);
                ctx->settings->Save();

                ::EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                ::EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

}  // namespace

EncryptDialogResult ShowEncryptDialog(HWND parent, const std::wstring& targetDesc,
                                      const Plugin::Settings& settings,
                                      bool defaultArmor) {
    TemplateBuilder builder(260, 205, L"Encrypt (AES-256)");
    builder.AddStatic(14, 10, 232, 18, L"Target: Selected Text", kIdTargetLabel);

    builder.AddStatic(14, 32, 80, 10, L"Passphrase / Password:");
    builder.AddEdit(14, 44, 190, 14, kIdPassEdit, true);
    builder.AddButton(208, 44, 38, 14, kIdPassShowBtn, L"Show");

    builder.AddStatic(14, 62, 100, 10, L"Confirm Passphrase:");
    builder.AddEdit(14, 74, 190, 14, kIdConfirmEdit, true);

    builder.AddStatic(14, 94, 80, 10, L"Encryption Algorithm:");
    builder.AddCombobox(14, 106, 232, 14, kIdCipherCombo);

    builder.AddStatic(14, 126, 80, 10, L"Key Derivation (KDF):");
    builder.AddCombobox(14, 138, 232, 14, kIdKdfCombo);

    builder.AddCheckbox(14, 158, 232, 12, kIdArmorCheck, L"Armor output as Base64 text banner");

    builder.AddButton(130, 180, 56, 15, IDOK, L"Encrypt", true);
    builder.AddButton(190, 180, 56, 15, IDCANCEL, L"Cancel");

    const std::vector<unsigned char> dlgData = builder.Build();
    EncryptDialogContext ctx;
    ctx.targetDesc = targetDesc;
    ctx.settings = &settings;
    ctx.defaultArmor = defaultArmor;

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, EncryptDlgProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

DecryptDialogResult ShowDecryptDialog(HWND parent, const std::wstring& targetDesc,
                                      const std::wstring& containerInfo) {
    TemplateBuilder builder(240, 125, L"Decrypt");
    builder.AddStatic(14, 10, 212, 18, L"Target: Selected Text", kIdTargetLabel);

    if (!containerInfo.empty()) {
        builder.AddStatic(14, 28, 212, 10, containerInfo.c_str(), kIdInfoLabel);
    }

    builder.AddStatic(14, 44, 80, 10, L"Passphrase / Password:");
    builder.AddEdit(14, 56, 170, 14, kIdPassEdit, true);
    builder.AddButton(188, 56, 38, 14, kIdPassShowBtn, L"Show");

    builder.AddButton(110, 95, 56, 15, IDOK, L"Decrypt", true);
    builder.AddButton(170, 95, 56, 15, IDCANCEL, L"Cancel");

    const std::vector<unsigned char> dlgData = builder.Build();
    DecryptDialogContext ctx;
    ctx.targetDesc = targetDesc;
    ctx.containerInfo = containerInfo;

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, DecryptDlgProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.result;
}

void ShowCryptoSettingsDialog(HWND parent, Plugin::Settings& settings) {
    TemplateBuilder builder(250, 160, L"Encryption Settings");
    builder.AddStatic(14, 12, 120, 10, L"Default Cipher:");
    builder.AddCombobox(14, 24, 222, 14, kIdCipherCombo);

    builder.AddStatic(14, 44, 120, 10, L"Default Key Derivation:");
    builder.AddCombobox(14, 56, 222, 14, kIdKdfCombo);

    builder.AddStatic(14, 76, 120, 10, L"PBKDF2 Iterations:");
    builder.AddEdit(14, 88, 100, 14, kIdItersEdit);

    builder.AddCheckbox(14, 110, 222, 12, kIdArmorCheck, L"Armor text encryption as Base64 by default");

    builder.AddButton(120, 134, 56, 15, IDOK, L"Save", true);
    builder.AddButton(180, 134, 56, 15, IDCANCEL, L"Cancel");

    const std::vector<unsigned char> dlgData = builder.Build();
    CryptoSettingsDialogContext ctx;
    ctx.settings = &settings;

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, CryptoSettingsDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Crypto


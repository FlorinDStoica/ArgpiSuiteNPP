#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "EncodingDialog.h"

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "EncodingService.h"
#include "plugin/PluginHost.h"
#include "plugin/TextUtil.h"
#include "PluginDefinition.h"

namespace ArgpiSuite::Crypto {
namespace {

constexpr WORD kIdAlphabetEdit       = 41001;
constexpr WORD kIdAlphabetPresetCombo= 41002;
constexpr WORD kIdAlphabetStatus     = 41003;

constexpr WORD kIdBase16UpperRadio   = 41010;
constexpr WORD kIdBase16LowerRadio   = 41011;
constexpr WORD kIdBase16DelimCombo   = 41012;

constexpr WORD kIdBase64StdRadio     = 41020;
constexpr WORD kIdBase64UrlRadio     = 41021;
constexpr WORD kIdBase64PadCheck     = 41022;

constexpr WORD kIdBase85Combo        = 41030;
constexpr WORD kIdResetBtn           = 41040;

// Presets
struct AlphabetPreset {
    const wchar_t* name;
    const wchar_t* alphabet;
};

const AlphabetPreset kPresets[] = {
    {L"Custom...", L""},
    {L"Base58 (Bitcoin)", L"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"},
    {L"Base62 (Alphanumeric)", L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"},
    {L"Base36 (Alphanumeric lowercase)", L"0123456789abcdefghijklmnopqrstuvwxyz"},
    {L"Base32 (RFC 4648)", L"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"},
    {L"Crockford Base32", L"0123456789ABCDEFGHJKMNPQRSTVWXYZ"},
    {L"Base16 Hex (Lower)", L"0123456789abcdef"},
    {L"Base2 Binary", L"01"},
};

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

    void AddGroupbox(short x, short y, short cx, short cy, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, cx, cy, 0x0080, 0xFFFF, text);
    }

    void AddEdit(short x, short y, short cx, short cy, WORD id, bool multiline = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP;
        if (multiline) {
            style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
        } else {
            style |= ES_AUTOHSCROLL;
        }
        AddItem(style, x, y, cx, cy, 0x0081, id, L"");
    }

    void AddRadio(short x, short y, short cx, short cy, WORD id, const wchar_t* text, bool groupStart = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP;
        if (groupStart) {
            style |= WS_GROUP;
        }
        AddItem(style, x, y, cx, cy, 0x0080, id, text);
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

struct EncodingDialogContext {
    Plugin::Settings* settings = nullptr;
};

void UpdateAlphabetStatus(HWND hwnd) {
    wchar_t buf[1024] = {};
    ::GetDlgItemTextW(hwnd, kIdAlphabetEdit, buf, 1024);
    const std::wstring alpha = buf;
    std::wstring err;
    if (alpha.empty()) {
        ::SetDlgItemTextW(hwnd, kIdAlphabetStatus, L"Alphabet is empty (must have >= 2 unique characters).");
    } else if (!EncodingService::ValidateAlphabet(alpha, err)) {
        ::SetDlgItemTextW(hwnd, kIdAlphabetStatus, err.c_str());
    } else {
        const std::wstring msg = L"Valid: " + std::to_wstring(alpha.size()) + L" unique characters (Base" +
                                 std::to_wstring(alpha.size()) + L")";
        ::SetDlgItemTextW(hwnd, kIdAlphabetStatus, msg.c_str());
    }
}

INT_PTR CALLBACK EncodingDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EncodingDialogContext*>(::GetWindowLongPtrW(hwnd, DWLP_USER));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<EncodingDialogContext*>(lParam);
            ::SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

            // Custom Alphabet Edit
            ::SetDlgItemTextW(hwnd, kIdAlphabetEdit, ctx->settings->encoding.customAlphabet.c_str());

            // Alphabet Presets Combo
            HWND hPreset = ::GetDlgItem(hwnd, kIdAlphabetPresetCombo);
            int selectedPreset = 0;
            for (size_t i = 0; i < sizeof(kPresets) / sizeof(kPresets[0]); ++i) {
                ::SendMessageW(hPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kPresets[i].name));
                if (i > 0 && ctx->settings->encoding.customAlphabet == kPresets[i].alphabet) {
                    selectedPreset = static_cast<int>(i);
                }
            }
            ::SendMessageW(hPreset, CB_SETCURSEL, selectedPreset, 0);
            UpdateAlphabetStatus(hwnd);

            // Base16 Options
            if (ctx->settings->encoding.base16Uppercase) {
                ::CheckRadioButton(hwnd, kIdBase16UpperRadio, kIdBase16LowerRadio, kIdBase16UpperRadio);
            } else {
                ::CheckRadioButton(hwnd, kIdBase16UpperRadio, kIdBase16LowerRadio, kIdBase16LowerRadio);
            }

            HWND hDelim = ::GetDlgItem(hwnd, kIdBase16DelimCombo);
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None (contiguous)"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Space (\" \")"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Colon (\":\")"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dash (\"-\")"));

            int delimIdx = 0;
            if (ctx->settings->encoding.base16Delimiter == L" ") delimIdx = 1;
            else if (ctx->settings->encoding.base16Delimiter == L":") delimIdx = 2;
            else if (ctx->settings->encoding.base16Delimiter == L"-") delimIdx = 3;
            ::SendMessageW(hDelim, CB_SETCURSEL, delimIdx, 0);

            // Base64 Options
            if (ctx->settings->encoding.base64UrlSafe) {
                ::CheckRadioButton(hwnd, kIdBase64StdRadio, kIdBase64UrlRadio, kIdBase64UrlRadio);
            } else {
                ::CheckRadioButton(hwnd, kIdBase64StdRadio, kIdBase64UrlRadio, kIdBase64StdRadio);
            }
            ::CheckDlgButton(hwnd, kIdBase64PadCheck, ctx->settings->encoding.base64Padding ? BST_CHECKED : BST_UNCHECKED);

            // Base85 Options
            HWND hB85 = ::GetDlgItem(hwnd, kIdBase85Combo);
            ::SendMessageW(hB85, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ascii85 / Adobe (<~...~>)"));
            ::SendMessageW(hB85, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Z85 / ZeroMQ (String-safe)"));
            ::SendMessageW(hB85, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"RFC 1924 (IPv6 Base85)"));

            int b85Idx = 0;
            if (Plugin::EqualsIgnoreCase(ctx->settings->encoding.base85Variant, L"z85")) b85Idx = 1;
            else if (Plugin::EqualsIgnoreCase(ctx->settings->encoding.base85Variant, L"rfc1924")) b85Idx = 2;
            ::SendMessageW(hB85, CB_SETCURSEL, b85Idx, 0);

            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if (id == kIdAlphabetEdit && code == EN_CHANGE) {
                UpdateAlphabetStatus(hwnd);
                return TRUE;
            }

            if (id == kIdAlphabetPresetCombo && code == CBN_SELCHANGE) {
                const int sel = static_cast<int>(::SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
                if (sel > 0 && sel < static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]))) {
                    ::SetDlgItemTextW(hwnd, kIdAlphabetEdit, kPresets[sel].alphabet);
                    UpdateAlphabetStatus(hwnd);
                }
                return TRUE;
            }

            if (id == kIdResetBtn && code == BN_CLICKED) {
                ::SetDlgItemTextW(hwnd, kIdAlphabetEdit, L"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz");
                ::SendMessageW(::GetDlgItem(hwnd, kIdAlphabetPresetCombo), CB_SETCURSEL, 1, 0);
                UpdateAlphabetStatus(hwnd);

                ::CheckRadioButton(hwnd, kIdBase16UpperRadio, kIdBase16LowerRadio, kIdBase16UpperRadio);
                ::SendMessageW(::GetDlgItem(hwnd, kIdBase16DelimCombo), CB_SETCURSEL, 0, 0);
                ::CheckRadioButton(hwnd, kIdBase64StdRadio, kIdBase64UrlRadio, kIdBase64StdRadio);
                ::CheckDlgButton(hwnd, kIdBase64PadCheck, BST_CHECKED);
                ::SendMessageW(::GetDlgItem(hwnd, kIdBase85Combo), CB_SETCURSEL, 0, 0);
                return TRUE;
            }

            if (id == IDOK) {
                wchar_t alphaBuf[1024] = {};
                ::GetDlgItemTextW(hwnd, kIdAlphabetEdit, alphaBuf, 1024);
                std::wstring err;
                if (!EncodingService::ValidateAlphabet(alphaBuf, err)) {
                    ::MessageBoxW(hwnd, err.c_str(), L"Invalid Custom Alphabet", MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(hwnd, kIdAlphabetEdit));
                    return TRUE;
                }
                ctx->settings->encoding.customAlphabet = alphaBuf;

                ctx->settings->encoding.base16Uppercase =
                    (::IsDlgButtonChecked(hwnd, kIdBase16UpperRadio) == BST_CHECKED);

                const int delimSel = static_cast<int>(
                    ::SendMessageW(::GetDlgItem(hwnd, kIdBase16DelimCombo), CB_GETCURSEL, 0, 0));
                if (delimSel == 1) ctx->settings->encoding.base16Delimiter = L" ";
                else if (delimSel == 2) ctx->settings->encoding.base16Delimiter = L":";
                else if (delimSel == 3) ctx->settings->encoding.base16Delimiter = L"-";
                else ctx->settings->encoding.base16Delimiter = L"";

                ctx->settings->encoding.base64UrlSafe =
                    (::IsDlgButtonChecked(hwnd, kIdBase64UrlRadio) == BST_CHECKED);
                ctx->settings->encoding.base64Padding =
                    (::IsDlgButtonChecked(hwnd, kIdBase64PadCheck) == BST_CHECKED);

                const int b85Sel = static_cast<int>(
                    ::SendMessageW(::GetDlgItem(hwnd, kIdBase85Combo), CB_GETCURSEL, 0, 0));
                if (b85Sel == 1) ctx->settings->encoding.base85Variant = L"z85";
                else if (b85Sel == 2) ctx->settings->encoding.base85Variant = L"rfc1924";
                else ctx->settings->encoding.base85Variant = L"ascii85";

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

void ShowEncodingSettingsDialog(HWND parent, Plugin::Settings& settings) {
    TemplateBuilder builder(290, 275, L"Encoding / Decoding Settings");

    // Group 1: Custom Alphabet
    builder.AddGroupbox(10, 8, 270, 95, L"Custom Alphabet (for Custom Base Encoding)");
    builder.AddStatic(18, 22, 50, 10, L"Preset:");
    builder.AddCombobox(72, 20, 200, 14, kIdAlphabetPresetCombo);

    builder.AddStatic(18, 38, 80, 10, L"Alphabet Characters:");
    builder.AddEdit(18, 50, 254, 30, kIdAlphabetEdit, true);
    builder.AddStatic(18, 84, 254, 12, L"Valid: 58 characters", kIdAlphabetStatus);

    // Group 2: Base16 (Hex)
    builder.AddGroupbox(10, 110, 270, 48, L"Base16 (Hex) Options");
    builder.AddRadio(18, 124, 60, 12, kIdBase16UpperRadio, L"Uppercase", true);
    builder.AddRadio(82, 124, 60, 12, kIdBase16LowerRadio, L"Lowercase");
    builder.AddStatic(150, 124, 40, 10, L"Delimiter:");
    builder.AddCombobox(192, 122, 80, 14, kIdBase16DelimCombo);

    // Group 3: Base64
    builder.AddGroupbox(10, 164, 270, 42, L"Base64 Options");
    builder.AddRadio(18, 178, 65, 12, kIdBase64StdRadio, L"Standard (+/)", true);
    builder.AddRadio(88, 178, 65, 12, kIdBase64UrlRadio, L"URL-Safe (-_)");
    builder.AddCheckbox(165, 178, 105, 12, kIdBase64PadCheck, L"Include '=' padding");

    // Group 4: Base85
    builder.AddGroupbox(10, 212, 270, 36, L"Base85 Options");
    builder.AddStatic(18, 226, 60, 10, L"Default Variant:");
    builder.AddCombobox(82, 224, 190, 14, kIdBase85Combo);

    // Actions
    builder.AddButton(10, 254, 75, 15, kIdResetBtn, L"Reset Defaults");
    builder.AddButton(160, 254, 56, 15, IDOK, L"Save", true);
    builder.AddButton(222, 254, 56, 15, IDCANCEL, L"Cancel");

    const std::vector<unsigned char> dlgData = builder.Build();
    EncodingDialogContext ctx;
    ctx.settings = &settings;

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, EncodingDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Crypto


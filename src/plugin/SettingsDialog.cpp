#include "SettingsDialog.h"

#include <windows.h>
#include <commctrl.h>

#include <iterator>
#include <string>
#include <vector>

#include "PluginDefinition.h"
#include "TextUtil.h"

namespace ArgpiSuite::Plugin {
namespace {

constexpr WORD kIdPrintWidth = 30100;
constexpr WORD kIdTabWidth = 30101;
constexpr WORD kIdUseTabs = 30102;
constexpr WORD kIdFormatOnSave = 30103;
constexpr WORD kIdRequireConfig = 30104;
constexpr WORD kIdTimeout = 30105;
constexpr WORD kIdExtensions = 30106;
constexpr WORD kIdIgnoreFile = 30107;
constexpr WORD kIdLogLevel = 30108;
constexpr WORD kIdMinify = 30109;

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

/// Standard control classes are encoded as 0xFFFF followed by the atom.
void WriteControlClass(std::vector<unsigned char>& data, WORD atom) {
    WriteWord(data, 0xFFFF);
    WriteWord(data, atom);
}

class TemplateBuilder {
public:
    TemplateBuilder() {
        WriteDword(data_, DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        WriteDword(data_, 0);  // extended style
        WriteWord(data_, 0);   // item count, patched by Build()
        WriteShort(data_, 0);  // x
        WriteShort(data_, 0);  // y
        WriteShort(data_, 300);
        WriteShort(data_, 226);
        WriteWord(data_, 0);  // no menu
        WriteWord(data_, 0);  // default window class
        WriteWide(data_, L"ArgpiSuite — Format Settings");
        // DS_SETFONT payload: point size and typeface.
        WriteWord(data_, 9);
        WriteWide(data_, L"Segoe UI");
    }

    void AddStatic(short x, short y, short cx, short cy, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, cx, cy, 0x0082, 0xFFFF, text, -1);
    }

    void AddEdit(short x, short y, short cx, short cy, WORD id) {
        AddItem(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, x, y, cx, cy,
                0x0081, id, L"", -1);
    }

    void AddCheckbox(short x, short y, short cx, short cy, WORD id, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, x, y, cx, cy, 0x0080, id,
                text, -1);
    }

    void AddButton(short x, short y, short cx, short cy, WORD id, const wchar_t* text,
                   bool defaultButton) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
        if (defaultButton) {
            style |= BS_DEFPUSHBUTTON;
        }
        AddItem(style, x, y, cx, cy, 0x0080, id, text, -1);
    }

    std::vector<unsigned char> Build() {
        const WORD count = static_cast<WORD>(items_);
        data_[8] = static_cast<unsigned char>(count & 0xFF);
        data_[9] = static_cast<unsigned char>((count >> 8) & 0xFF);
        return data_;
    }

private:
    void AddItem(DWORD style, short x, short y, short cx, short cy, WORD atom, WORD id,
                 const wchar_t* text, short extra) {
        AlignDword(data_);
        WriteDword(data_, style);
        WriteDword(data_, 0);  // extended style
        WriteShort(data_, x);
        WriteShort(data_, y);
        WriteShort(data_, cx);
        WriteShort(data_, cy);
        WriteWord(data_, id);
        WriteControlClass(data_, atom);
        WriteWide(data_, text);
        WriteWord(data_, static_cast<WORD>(extra < 0 ? 0 : extra));  // creation data size
        ++items_;
    }

    std::vector<unsigned char> data_;
    int items_ = 0;
};

struct DialogState {
    Settings* settings = nullptr;
    Settings working;
};

void SetText(HWND dialog, WORD id, const std::wstring& text) {
    ::SetDlgItemTextW(dialog, id, text.c_str());
}

std::wstring GetText(HWND dialog, WORD id) {
    wchar_t buffer[2048] = {};
    ::GetDlgItemTextW(dialog, id, buffer, static_cast<int>(std::size(buffer)));
    return Trim(buffer);
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    DialogState* state = reinterpret_cast<DialogState*>(
        ::GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<DialogState*>(lParam);
            ::SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            if (state == nullptr) {
                return FALSE;
            }
            const Settings& settings = state->working;
            SetText(dialog, kIdPrintWidth, std::to_wstring(settings.format.printWidth));
            SetText(dialog, kIdTabWidth, std::to_wstring(settings.format.tabWidth));
            SetText(dialog, kIdTimeout, std::to_wstring(settings.format.timeoutMs));
            SetText(dialog, kIdExtensions, settings.extensionFilter);
            SetText(dialog, kIdIgnoreFile, settings.ignoreFile);
            SetText(dialog, kIdLogLevel, settings.logLevel);
            ::CheckDlgButton(dialog, kIdUseTabs,
                             settings.format.useTabs ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dialog, kIdFormatOnSave,
                             settings.formatOnSave ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dialog, kIdRequireConfig,
                             settings.requireConfig ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(dialog, kIdMinify,
                             settings.format.minify ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            if (id == IDOK) {
                if (state == nullptr) {
                    ::EndDialog(dialog, IDCANCEL);
                    return TRUE;
                }
                const int printWidth = ::GetDlgItemInt(dialog, kIdPrintWidth, nullptr, FALSE);
                const int tabWidth = ::GetDlgItemInt(dialog, kIdTabWidth, nullptr, FALSE);
                const int timeout = ::GetDlgItemInt(dialog, kIdTimeout, nullptr, FALSE);
                if (printWidth < 20 || printWidth > 500) {
                    ::MessageBoxW(dialog, L"Print width must be between 20 and 500.",
                                  L"ArgpiSuite", MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(dialog, kIdPrintWidth));
                    return TRUE;
                }
                if (tabWidth < 1 || tabWidth > 16) {
                    ::MessageBoxW(dialog, L"Tab width must be between 1 and 16.", L"ArgpiSuite",
                                  MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(dialog, kIdTabWidth));
                    return TRUE;
                }
                if (timeout < 100 || timeout > 600000) {
                    ::MessageBoxW(dialog, L"Timeout must be between 100 and 600000 ms.",
                                  L"ArgpiSuite", MB_OK | MB_ICONWARNING);
                    ::SetFocus(::GetDlgItem(dialog, kIdTimeout));
                    return TRUE;
                }
                state->working.format.printWidth = printWidth;
                state->working.format.tabWidth = tabWidth;
                state->working.format.timeoutMs = timeout;
                state->working.format.useTabs =
                    ::IsDlgButtonChecked(dialog, kIdUseTabs) == BST_CHECKED;
                state->working.formatOnSave =
                    ::IsDlgButtonChecked(dialog, kIdFormatOnSave) == BST_CHECKED;
                state->working.requireConfig =
                    ::IsDlgButtonChecked(dialog, kIdRequireConfig) == BST_CHECKED;
                state->working.format.minify =
                    ::IsDlgButtonChecked(dialog, kIdMinify) == BST_CHECKED;
                state->working.extensionFilter = GetText(dialog, kIdExtensions);
                state->working.ignoreFile = GetText(dialog, kIdIgnoreFile);
                state->working.logLevel = GetText(dialog, kIdLogLevel);
                if (state->working.logLevel.empty()) {
                    state->working.logLevel = L"info";
                }
                *state->settings = state->working;
                ::EndDialog(dialog, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                ::EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            ::EndDialog(dialog, IDCANCEL);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace

bool ShowFormatSettings(HWND owner, Settings& settings) {
    TemplateBuilder builder;
    const short left = 10;
    short y = 10;

    builder.AddStatic(left, y + 2, 80, 10, L"Print width:");
    builder.AddEdit(95, y, 60, 12, kIdPrintWidth);
    y += 18;
    builder.AddStatic(left, y + 2, 80, 10, L"Tab width:");
    builder.AddEdit(95, y, 60, 12, kIdTabWidth);
    y += 18;
    builder.AddCheckbox(left, y, 170, 10, kIdUseTabs, L"Use tabs");
    y += 14;
    builder.AddCheckbox(left, y, 170, 10, kIdFormatOnSave, L"Format on save");
    y += 14;
    builder.AddCheckbox(left, y, 170, 10, kIdRequireConfig,
                        L"Only format files with a config");
    y += 14;
    builder.AddCheckbox(left, y, 170, 10, kIdMinify, L"Minify output");
    y += 18;
    builder.AddStatic(left, y + 2, 80, 10, L"Timeout (ms):");
    builder.AddEdit(95, y, 60, 12, kIdTimeout);
    y += 18;
    builder.AddStatic(left, y + 2, 80, 10, L"Extensions:");
    builder.AddEdit(95, y, 190, 12, kIdExtensions);
    y += 18;
    builder.AddStatic(left, y + 2, 80, 10, L"Ignore file:");
    builder.AddEdit(95, y, 190, 12, kIdIgnoreFile);
    y += 18;
    builder.AddStatic(left, y + 2, 80, 10, L"Log level:");
    builder.AddEdit(95, y, 80, 12, kIdLogLevel);
    y += 22;
    builder.AddStatic(left, y, 280, 24,
                      L"Crypto and Password sections are reserved for future versions and are "
                      L"edited in ArgpiSuite.ini.");
    y += 32;
    builder.AddButton(160, y, 60, 14, IDOK, L"OK", true);
    builder.AddButton(228, y, 60, 14, IDCANCEL, L"Cancel", false);

    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    ::InitCommonControlsEx(&icex);

    DialogState state;
    state.settings = &settings;
    state.working = settings;

    std::vector<unsigned char> data = builder.Build();
    const INT_PTR result = ::DialogBoxIndirectParamW(
        static_cast<HINSTANCE>(PluginModule()), reinterpret_cast<LPCDLGTEMPLATEW>(data.data()), owner,
        &DialogProc, reinterpret_cast<LPARAM>(&state));
    return result == IDOK;
}

}  // namespace ArgpiSuite::Plugin

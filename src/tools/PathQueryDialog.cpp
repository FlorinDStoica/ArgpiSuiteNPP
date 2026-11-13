#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PathQueryDialog.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "PathQueryService.h"
#include "PluginDefinition.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

constexpr WORD kIdSourceSelRadio   = 45001;
constexpr WORD kIdSourceDocRadio   = 45002;
constexpr WORD kIdModeCombo        = 45003;
constexpr WORD kIdQueryEdit        = 45004;
constexpr WORD kIdHintLabel        = 45005;
constexpr WORD kIdResultEdit       = 45006;
constexpr WORD kIdStatusLabel      = 45007;
constexpr WORD kIdCopyBtn          = 45010;
constexpr WORD kIdApplyBtn         = 45011;

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
        WriteDword(data_, 0);
        WriteWord(data_, 0);
        WriteShort(data_, 0);
        WriteShort(data_, 0);
        WriteShort(data_, widthDlu);
        WriteShort(data_, heightDlu);
        WriteWord(data_, 0);
        WriteWord(data_, 0);
        WriteWide(data_, title);
        WriteWord(data_, 9);
        WriteWide(data_, L"Segoe UI");
    }

    void AddStatic(short x, short y, short cx, short cy, const wchar_t* text, WORD id = 0xFFFF) {
        AddItem(WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, cx, cy, 0x0082, id, text);
    }

    void AddGroupbox(short x, short y, short cx, short cy, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, cx, cy, 0x0080, 0xFFFF, text);
    }

    void AddEdit(short x, short y, short cx, short cy, WORD id, DWORD extraStyle = 0) {
        AddItem(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | extraStyle,
                x, y, cx, cy, 0x0081, id, L"");
    }

    void AddRadio(short x, short y, short cx, short cy, WORD id, const wchar_t* text,
                  bool groupStart = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP;
        if (groupStart) {
            style |= WS_GROUP;
        }
        AddItem(style, x, y, cx, cy, 0x0080, id, text);
    }

    void AddButton(short x, short y, short cx, short cy, WORD id, const wchar_t* text,
                   bool defBtn = false) {
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
        WriteDword(data_, 0);
        WriteShort(data_, x);
        WriteShort(data_, y);
        WriteShort(data_, cx);
        WriteShort(data_, cy);
        WriteWord(data_, id);
        WriteWord(data_, 0xFFFF);
        WriteWord(data_, atom);
        WriteWide(data_, text);
        WriteWord(data_, 0);
        ++items_;
    }

    std::vector<unsigned char> data_;
    int items_ = 0;
};

struct PathDialogContext {
    Plugin::Settings* settings = nullptr;
    const PathQueryDialogInput* input = nullptr;
    PathApplyCallback onApply;
    std::string lastOutput;
};

std::wstring GetEditText(HWND hwnd, WORD id) {
    HWND ctrl = ::GetDlgItem(hwnd, id);
    const int len = ::GetWindowTextLengthW(ctrl);
    if (len <= 0) {
        return {};
    }
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    ::GetWindowTextW(ctrl, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(len));
    return buf;
}

bool CopyUtf8ToClipboard(HWND owner, const std::string& utf8) {
    const std::wstring wide = Plugin::ToWide(utf8);
    if (!::OpenClipboard(owner)) {
        return false;
    }
    ::EmptyClipboard();
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != nullptr) {
        void* ptr = ::GlobalLock(hMem);
        if (ptr != nullptr) {
            std::memcpy(ptr, wide.c_str(), bytes);
            ::GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    ::CloseClipboard();
    return true;
}

QueryMode SelectedMode(HWND hwnd) {
    const int sel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdModeCombo), CB_GETCURSEL, 0, 0));
    if (sel == 1) return QueryMode::JsonPath;
    if (sel == 2) return QueryMode::XPath;
    if (sel == 3) return QueryMode::SqlInsert;
    return QueryMode::Auto;
}

void UpdateHintText(HWND hwnd, QueryMode effectiveMode) {
    if (effectiveMode == QueryMode::XPath) {
        ::SetDlgItemTextW(hwnd, kIdHintLabel,
                          L"XPath: //item[@active='true']/name/text()   or   /root/users[1]/@id");
    } else if (effectiveMode == QueryMode::SqlInsert) {
        ::SetDlgItemTextW(hwnd, kIdHintLabel,
                          L"SQL: SELECT id, email WHERE status = 'active'   or   *");
    } else {
        ::SetDlgItemTextW(hwnd, kIdHintLabel,
                          L"JSONPath: $.users[?(@.age >= 18)].email   or   $..id   or   $.items[0:3]");
    }
}

void EvaluateQuery(HWND hwnd, PathDialogContext* ctx) {
    if (ctx == nullptr || ctx->input == nullptr) {
        return;
    }
    const bool useSel = (::IsDlgButtonChecked(hwnd, kIdSourceSelRadio) == BST_CHECKED);
    const std::string& src = useSel ? ctx->input->selectionText : ctx->input->documentText;
    const std::string query = Plugin::ToUtf8(GetEditText(hwnd, kIdQueryEdit));
    const QueryMode mode = SelectedMode(hwnd);

    const PathQueryResult res = PathQueryService::Instance().Execute(src, query, mode);
    UpdateHintText(hwnd, res.detectedMode);

    if (!res.ok) {
        ctx->lastOutput.clear();
        ::SetDlgItemTextW(hwnd, kIdResultEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdStatusLabel, res.message.c_str());
        return;
    }

    ctx->lastOutput = res.output;
    ::SetDlgItemTextW(hwnd, kIdResultEdit, Plugin::ToWide(res.output).c_str());
    ::SetDlgItemTextW(hwnd, kIdStatusLabel,
                      (std::to_wstring(res.matchCount) + L" match(es)").c_str());
}

INT_PTR CALLBACK PathDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PathDialogContext* ctx =
        reinterpret_cast<PathDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<PathDialogContext*>(lParam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            HWND parent = ::GetParent(hwnd);
            if (parent != nullptr) {
                RECT pr{}, dr{};
                ::GetWindowRect(parent, &pr);
                ::GetWindowRect(hwnd, &dr);
                const int x = pr.left + ((pr.right - pr.left) - (dr.right - dr.left)) / 2;
                const int y = pr.top + ((pr.bottom - pr.top) - (dr.bottom - dr.top)) / 2;
                ::SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }

            if (!ctx->input->hasSelection) {
                ::EnableWindow(::GetDlgItem(hwnd, kIdSourceSelRadio), FALSE);
                ::CheckRadioButton(hwnd, kIdSourceSelRadio, kIdSourceDocRadio, kIdSourceDocRadio);
            } else {
                ::CheckRadioButton(hwnd, kIdSourceSelRadio, kIdSourceDocRadio, kIdSourceSelRadio);
            }

            HWND hMode = ::GetDlgItem(hwnd, kIdModeCombo);
            ::SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto-Detect"));
            ::SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JSONPath / JMESPath"));
            ::SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"XPath (XML)"));
            ::SendMessageW(hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"SQL INSERT Dump"));
            ::SendMessageW(hMode, CB_SETCURSEL, 0, 0);

            EvaluateQuery(hwnd, ctx);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if ((id == kIdSourceSelRadio || id == kIdSourceDocRadio) && code == BN_CLICKED) {
                EvaluateQuery(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdModeCombo && code == CBN_SELCHANGE) {
                EvaluateQuery(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdQueryEdit && code == EN_CHANGE) {
                EvaluateQuery(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdCopyBtn && code == BN_CLICKED) {
                if (!ctx->lastOutput.empty()) {
                    CopyUtf8ToClipboard(hwnd, ctx->lastOutput);
                }
                return TRUE;
            }

            if ((id == kIdApplyBtn || id == IDOK) && code == BN_CLICKED) {
                if (ctx->lastOutput.empty()) {
                    return TRUE;
                }
                const bool useSel =
                    (::IsDlgButtonChecked(hwnd, kIdSourceSelRadio) == BST_CHECKED);
                if (ctx->onApply && ctx->onApply(ctx->lastOutput, useSel)) {
                    ::EndDialog(hwnd, IDOK);
                } else {
                    ::MessageBoxW(hwnd, L"Could not update the active editor (is it read-only?).",
                                  L"Path Querying", MB_OK | MB_ICONWARNING);
                }
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

void ShowPathQueryDialog(HWND parent, Plugin::Settings& settings, const PathQueryDialogInput& input,
                         PathApplyCallback onApply) {
    TemplateBuilder builder(340, 285, L"JSON / XML / SQL Path Querying");

    // Source & Mode
    builder.AddGroupbox(10, 8, 320, 34, L"Source && Query Language");
    builder.AddRadio(18, 22, 66, 12, kIdSourceSelRadio, L"Selection", true);
    builder.AddRadio(88, 22, 76, 12, kIdSourceDocRadio, L"Active Document");
    builder.AddStatic(174, 23, 24, 10, L"Mode:");
    builder.AddCombobox(200, 20, 122, 14, kIdModeCombo);

    // Query Input
    builder.AddGroupbox(10, 48, 320, 44, L"Query Expression");
    builder.AddEdit(18, 60, 304, 13, kIdQueryEdit, ES_AUTOHSCROLL);
    builder.AddStatic(18, 77, 304, 10, L"", kIdHintLabel);

    // Results
    builder.AddGroupbox(10, 98, 320, 158, L"Query Results");
    builder.AddEdit(18, 110, 304, 128, kIdResultEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
    builder.AddStatic(18, 242, 304, 10, L"", kIdStatusLabel);

    // Bottom Buttons
    builder.AddButton(120, 263, 66, 15, kIdCopyBtn, L"Copy Result", true);
    builder.AddButton(190, 263, 86, 15, kIdApplyBtn, L"Replace Selection/Doc");
    builder.AddButton(280, 263, 50, 15, IDCANCEL, L"Close");

    const std::vector<unsigned char> dlgData = builder.Build();
    PathDialogContext ctx;
    ctx.settings = &settings;
    ctx.input = &input;
    ctx.onApply = std::move(onApply);

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, PathDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Tools


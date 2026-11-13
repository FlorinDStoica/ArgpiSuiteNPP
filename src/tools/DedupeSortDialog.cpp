#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "DedupeSortDialog.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "DedupeSortService.h"
#include "PluginDefinition.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

constexpr WORD kIdSourceSelRadio   = 47001;
constexpr WORD kIdSourceDocRadio   = 47002;
constexpr WORD kIdOpCombo          = 47003;
constexpr WORD kIdDescendingCheck  = 47004;
constexpr WORD kIdCaseSensCheck    = 47005;
constexpr WORD kIdTrimCheck        = 47006;
constexpr WORD kIdIgnoreEmptyCheck = 47007;
constexpr WORD kIdPreviewEdit      = 47010;
constexpr WORD kIdStatusLabel      = 47011;
constexpr WORD kIdCopyBtn          = 47020;
constexpr WORD kIdApplyBtn         = 47021;

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

    void AddCheckbox(short x, short y, short cx, short cy, WORD id, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                x, y, cx, cy, 0x0080, id, text);
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

struct DedupeDialogContext {
    Plugin::Settings* settings = nullptr;
    const DedupeSortDialogInput* input = nullptr;
    DedupeApplyCallback onApply;
    std::string lastOutput;
};

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

DedupeSortOptions ReadOptions(HWND hwnd, const std::string& eol) {
    DedupeSortOptions opts;
    opts.eol = eol;
    const int opSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdOpCombo), CB_GETCURSEL, 0, 0));
    opts.operation = static_cast<DedupeSortOperation>(opSel >= 0 ? opSel : 0);
    opts.descending = (::IsDlgButtonChecked(hwnd, kIdDescendingCheck) == BST_CHECKED);
    opts.caseSensitive = (::IsDlgButtonChecked(hwnd, kIdCaseSensCheck) == BST_CHECKED);
    opts.trimWhitespace = (::IsDlgButtonChecked(hwnd, kIdTrimCheck) == BST_CHECKED);
    opts.ignoreEmptyLines = (::IsDlgButtonChecked(hwnd, kIdIgnoreEmptyCheck) == BST_CHECKED);
    return opts;
}

void RecomputeDedupeSort(HWND hwnd, DedupeDialogContext* ctx) {
    if (ctx == nullptr || ctx->input == nullptr) {
        return;
    }
    const bool useSel = (::IsDlgButtonChecked(hwnd, kIdSourceSelRadio) == BST_CHECKED);
    const std::string& src = useSel ? ctx->input->selectionText : ctx->input->documentText;
    const DedupeSortOptions opts = ReadOptions(hwnd, ctx->input->documentEol);

    const DedupeSortResult res = DedupeSortService::Instance().Process(src, opts);
    if (!res.ok) {
        ctx->lastOutput.clear();
        ::SetDlgItemTextW(hwnd, kIdPreviewEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdStatusLabel, res.message.c_str());
        return;
    }

    ctx->lastOutput = res.output;
    const std::string previewUtf8 =
        (res.output.size() > 16384) ? res.output.substr(0, 16384) : res.output;
    ::SetDlgItemTextW(hwnd, kIdPreviewEdit, Plugin::ToWide(previewUtf8).c_str());

    if (opts.operation == DedupeSortOperation::SortJsonKeys) {
        ::SetDlgItemTextW(hwnd, kIdStatusLabel, L"JSON keys sorted.");
    } else {
        const std::wstring status = std::to_wstring(res.inputLines) + L" line(s) -> " +
                                    std::to_wstring(res.outputLines) + L" line(s) (" +
                                    std::to_wstring(res.removedCount) + L" removed)";
        ::SetDlgItemTextW(hwnd, kIdStatusLabel, status.c_str());
    }
}

INT_PTR CALLBACK DedupeDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DedupeDialogContext* ctx =
        reinterpret_cast<DedupeDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<DedupeDialogContext*>(lParam);
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

            HWND hOp = ::GetDlgItem(hwnd, kIdOpCombo);
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Remove Duplicate Lines (Preserve Order)"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Sort Lines Alphabetically"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Sort Lines by Length"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Sort Lines Numerically (Natural)"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Sort JSON Keys Alphabetically"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Remove Empty Lines"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Reverse Line Order"));
            ::SendMessageW(hOp, CB_SETCURSEL, 0, 0);

            ::CheckDlgButton(hwnd, kIdCaseSensCheck, BST_CHECKED);

            RecomputeDedupeSort(hwnd, ctx);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if ((id == kIdSourceSelRadio || id == kIdSourceDocRadio ||
                 id == kIdDescendingCheck || id == kIdCaseSensCheck ||
                 id == kIdTrimCheck || id == kIdIgnoreEmptyCheck) &&
                code == BN_CLICKED) {
                RecomputeDedupeSort(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdOpCombo && code == CBN_SELCHANGE) {
                RecomputeDedupeSort(hwnd, ctx);
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
                                  L"Smart Deduplication & Sorting", MB_OK | MB_ICONWARNING);
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

void ShowDedupeSortDialog(HWND parent, Plugin::Settings& settings,
                          const DedupeSortDialogInput& input, DedupeApplyCallback onApply) {
    TemplateBuilder builder(330, 285, L"Smart Deduplication && Sorting");

    // Source & Operation
    builder.AddGroupbox(10, 8, 310, 42, L"Source && Operation");
    builder.AddRadio(18, 22, 52, 12, kIdSourceSelRadio, L"Selection", true);
    builder.AddRadio(74, 22, 72, 12, kIdSourceDocRadio, L"Active Document");
    builder.AddCombobox(152, 20, 160, 14, kIdOpCombo);

    // Options
    builder.AddGroupbox(10, 56, 310, 32, L"Options");
    builder.AddCheckbox(18, 69, 64, 12, kIdDescendingCheck, L"Descending");
    builder.AddCheckbox(88, 69, 72, 12, kIdCaseSensCheck, L"Case-sensitive");
    builder.AddCheckbox(166, 69, 74, 12, kIdTrimCheck, L"Trim whitespace");
    builder.AddCheckbox(244, 69, 72, 12, kIdIgnoreEmptyCheck, L"Ignore empty");

    // Preview
    builder.AddGroupbox(10, 94, 310, 162, L"Preview");
    builder.AddEdit(18, 106, 294, 132, kIdPreviewEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
    builder.AddStatic(18, 242, 294, 10, L"", kIdStatusLabel);

    // Bottom Buttons
    builder.AddButton(120, 263, 58, 15, kIdCopyBtn, L"Copy");
    builder.AddButton(182, 263, 82, 15, kIdApplyBtn, L"Apply to Editor", true);
    builder.AddButton(268, 263, 52, 15, IDCANCEL, L"Close");

    const std::vector<unsigned char> dlgData = builder.Build();
    DedupeDialogContext ctx;
    ctx.settings = &settings;
    ctx.input = &input;
    ctx.onApply = std::move(onApply);

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, DedupeDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Tools


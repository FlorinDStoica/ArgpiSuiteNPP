#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CsvToolDialog.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "CsvToolService.h"
#include "PluginDefinition.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

constexpr WORD kIdSourceSelRadio   = 46001;
constexpr WORD kIdSourceDocRadio   = 46002;
constexpr WORD kIdDelimCombo       = 46003;
constexpr WORD kIdHeaderCheck      = 46004;
constexpr WORD kIdOpCombo          = 46005;
constexpr WORD kIdColSpecEdit      = 46006;
constexpr WORD kIdFilterValEdit    = 46007;
constexpr WORD kIdInvertCheck      = 46008;
constexpr WORD kIdCaseCheck        = 46009;
constexpr WORD kIdPreviewEdit      = 46010;
constexpr WORD kIdStatusLabel      = 46011;
constexpr WORD kIdCopyBtn          = 46020;
constexpr WORD kIdApplyBtn         = 46021;

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

struct CsvDialogContext {
    Plugin::Settings* settings = nullptr;
    const CsvDialogInput* input = nullptr;
    CsvApplyCallback onApply;
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

CsvOptions ReadOptions(HWND hwnd, const std::string& eol) {
    CsvOptions opts;
    opts.eol = eol;
    opts.hasHeader = (::IsDlgButtonChecked(hwnd, kIdHeaderCheck) == BST_CHECKED);

    const int dSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdDelimCombo), CB_GETCURSEL, 0, 0));
    if (dSel == 1) opts.delimiter = ',';
    else if (dSel == 2) opts.delimiter = '\t';
    else if (dSel == 3) opts.delimiter = ';';
    else if (dSel == 4) opts.delimiter = '|';
    else opts.delimiter = '\0';

    const int opSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdOpCombo), CB_GETCURSEL, 0, 0));
    opts.operation = static_cast<CsvOperation>(opSel >= 0 ? opSel : 0);

    opts.columnSpec = Plugin::ToUtf8(GetEditText(hwnd, kIdColSpecEdit));
    opts.filterValue = Plugin::ToUtf8(GetEditText(hwnd, kIdFilterValEdit));
    opts.filterInvert = (::IsDlgButtonChecked(hwnd, kIdInvertCheck) == BST_CHECKED);
    opts.filterCaseSensitive = (::IsDlgButtonChecked(hwnd, kIdCaseCheck) == BST_CHECKED);
    return opts;
}

void UpdateEnableStates(HWND hwnd) {
    const int opSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdOpCombo), CB_GETCURSEL, 0, 0));
    const CsvOperation op = static_cast<CsvOperation>(opSel >= 0 ? opSel : 0);
    const bool needsCols = (op == CsvOperation::ExtractColumns ||
                            op == CsvOperation::DeleteColumns ||
                            op == CsvOperation::FilterRows);
    const bool isFilter = (op == CsvOperation::FilterRows);
    ::EnableWindow(::GetDlgItem(hwnd, kIdColSpecEdit), needsCols ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(hwnd, kIdFilterValEdit), isFilter ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(hwnd, kIdInvertCheck), isFilter ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(hwnd, kIdCaseCheck), isFilter ? TRUE : FALSE);
}

void RecomputeCsv(HWND hwnd, CsvDialogContext* ctx) {
    if (ctx == nullptr || ctx->input == nullptr) {
        return;
    }
    const bool useSel = (::IsDlgButtonChecked(hwnd, kIdSourceSelRadio) == BST_CHECKED);
    const std::string& src = useSel ? ctx->input->selectionText : ctx->input->documentText;
    const CsvOptions opts = ReadOptions(hwnd, ctx->input->documentEol);

    const CsvResult res = CsvToolService::Instance().Process(src, opts);
    if (!res.ok) {
        ctx->lastOutput.clear();
        ::SetDlgItemTextW(hwnd, kIdPreviewEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdStatusLabel, res.message.c_str());
        return;
    }

    ctx->lastOutput = res.output;
    // Preview first 16 KB so huge files remain snappy in the Edit control.
    const std::string previewUtf8 =
        (res.output.size() > 16384) ? res.output.substr(0, 16384) : res.output;
    ::SetDlgItemTextW(hwnd, kIdPreviewEdit, Plugin::ToWide(previewUtf8).c_str());

    std::wstring delimName = L",";
    if (res.detectedDelimiter == '\t') delimName = L"Tab";
    else if (res.detectedDelimiter == ';') delimName = L";";
    else if (res.detectedDelimiter == '|') delimName = L"|";
    const std::wstring status = L"Delimiter: " + delimName + L" | " +
                                std::to_wstring(res.rowCount) + L" row(s), " +
                                std::to_wstring(res.columnCount) + L" column(s)";
    ::SetDlgItemTextW(hwnd, kIdStatusLabel, status.c_str());
}

INT_PTR CALLBACK CsvDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CsvDialogContext* ctx =
        reinterpret_cast<CsvDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<CsvDialogContext*>(lParam);
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

            HWND hDelim = ::GetDlgItem(hwnd, kIdDelimCombo);
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto-Detect"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Comma (,)"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tab (\\t)"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Semicolon (;)"));
            ::SendMessageW(hDelim, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pipe (|)"));
            ::SendMessageW(hDelim, CB_SETCURSEL, 0, 0);

            ::CheckDlgButton(hwnd, kIdHeaderCheck, BST_CHECKED);

            HWND hOp = ::GetDlgItem(hwnd, kIdOpCombo);
            ::SendMessageW(hOp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Align Columns Visually"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Compact / Unalign"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Extract Columns"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Delete Columns"));
            ::SendMessageW(hOp, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Filter Rows"));
            ::SendMessageW(hOp, CB_SETCURSEL, 0, 0);

            UpdateEnableStates(hwnd);
            RecomputeCsv(hwnd, ctx);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if ((id == kIdSourceSelRadio || id == kIdSourceDocRadio || id == kIdHeaderCheck ||
                 id == kIdInvertCheck || id == kIdCaseCheck) &&
                code == BN_CLICKED) {
                RecomputeCsv(hwnd, ctx);
                return TRUE;
            }

            if ((id == kIdDelimCombo || id == kIdOpCombo) && code == CBN_SELCHANGE) {
                UpdateEnableStates(hwnd);
                RecomputeCsv(hwnd, ctx);
                return TRUE;
            }

            if ((id == kIdColSpecEdit || id == kIdFilterValEdit) && code == EN_CHANGE) {
                RecomputeCsv(hwnd, ctx);
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
                                  L"CSV / TSV Tool", MB_OK | MB_ICONWARNING);
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

void ShowCsvToolDialog(HWND parent, Plugin::Settings& settings, const CsvDialogInput& input,
                       CsvApplyCallback onApply) {
    TemplateBuilder builder(350, 295, L"CSV / TSV Column Alignment && Filtering");

    // Source & Delimiter
    builder.AddGroupbox(10, 8, 330, 34, L"Source && Delimiter");
    builder.AddRadio(18, 22, 54, 12, kIdSourceSelRadio, L"Selection", true);
    builder.AddRadio(74, 22, 72, 12, kIdSourceDocRadio, L"Active Document");
    builder.AddStatic(152, 23, 34, 10, L"Delimiter:");
    builder.AddCombobox(188, 20, 74, 14, kIdDelimCombo);
    builder.AddCheckbox(270, 22, 64, 12, kIdHeaderCheck, L"Has Header");

    // Operation & Column/Filter Spec
    builder.AddGroupbox(10, 48, 330, 56, L"Operation && Column Options");
    builder.AddStatic(18, 63, 38, 10, L"Operation:");
    builder.AddCombobox(58, 61, 114, 14, kIdOpCombo);
    builder.AddStatic(180, 63, 56, 10, L"Columns (1,3 / name):");
    builder.AddEdit(242, 61, 90, 13, kIdColSpecEdit, ES_AUTOHSCROLL);

    builder.AddStatic(18, 83, 38, 10, L"Filter Text:");
    builder.AddEdit(58, 81, 114, 13, kIdFilterValEdit, ES_AUTOHSCROLL);
    builder.AddCheckbox(180, 82, 72, 12, kIdInvertCheck, L"Exclude matches");
    builder.AddCheckbox(258, 82, 74, 12, kIdCaseCheck, L"Case-sensitive");

    // Preview
    builder.AddGroupbox(10, 110, 330, 156, L"Preview");
    builder.AddEdit(18, 122, 314, 126, kIdPreviewEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);
    builder.AddStatic(18, 252, 314, 10, L"", kIdStatusLabel);

    // Bottom Buttons
    builder.AddButton(140, 273, 58, 15, kIdCopyBtn, L"Copy");
    builder.AddButton(202, 273, 82, 15, kIdApplyBtn, L"Apply to Editor", true);
    builder.AddButton(288, 273, 52, 15, IDCANCEL, L"Close");

    const std::vector<unsigned char> dlgData = builder.Build();
    CsvDialogContext ctx;
    ctx.settings = &settings;
    ctx.input = &input;
    ctx.onApply = std::move(onApply);

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, CsvDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Tools


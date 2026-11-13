#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "IdGeneratorDialog.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "IdGeneratorService.h"
#include "PluginDefinition.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Generator {
namespace {

constexpr WORD kIdTypeUuidV4Radio   = 42001;
constexpr WORD kIdTypeUuidV7Radio   = 42002;
constexpr WORD kIdTypeGuidRadio     = 42003;
constexpr WORD kIdTypeUlidRadio     = 42004;
constexpr WORD kIdCountEdit         = 42005;
constexpr WORD kIdDefaultUuidCombo  = 42006;

constexpr WORD kIdUppercaseCheck    = 42010;
constexpr WORD kIdHyphensCheck      = 42011;
constexpr WORD kIdBracesCheck       = 42012;
constexpr WORD kIdQuotesCombo       = 42013;
constexpr WORD kIdSeparatorCombo    = 42014;

constexpr WORD kIdPreviewEdit       = 42020;
constexpr WORD kIdRegenerateBtn     = 42030;
constexpr WORD kIdSaveDefaultsBtn   = 42031;
constexpr WORD kIdCopyBtn           = 42032;
constexpr WORD kIdInsertBtn         = 42033;

constexpr int kMaxPreviewItems = 50;

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

struct IdDialogContext {
    Plugin::Settings* settings = nullptr;
    std::string documentEol = "\r\n";
    InsertCallback onInsert;
};

IdType SelectedIdType(HWND hwnd) {
    if (::IsDlgButtonChecked(hwnd, kIdTypeUuidV7Radio) == BST_CHECKED) {
        return IdType::UuidV7;
    }
    if (::IsDlgButtonChecked(hwnd, kIdTypeGuidRadio) == BST_CHECKED) {
        return IdType::Guid;
    }
    if (::IsDlgButtonChecked(hwnd, kIdTypeUlidRadio) == BST_CHECKED) {
        return IdType::Ulid;
    }
    return IdType::UuidV4;
}

int ReadCount(HWND hwnd) {
    BOOL translated = FALSE;
    const UINT val = ::GetDlgItemInt(hwnd, kIdCountEdit, &translated, FALSE);
    if (!translated || val == 0) {
        return 1;
    }
    return std::clamp(static_cast<int>(val), 1, 100000);
}

BulkGenerateOptions ReadOptionsFromDialog(HWND hwnd, const std::string& eol) {
    BulkGenerateOptions opts;
    opts.type = SelectedIdType(hwnd);
    opts.count = ReadCount(hwnd);
    opts.eol = eol;

    opts.format.uppercase = (::IsDlgButtonChecked(hwnd, kIdUppercaseCheck) == BST_CHECKED);
    opts.format.hyphens = (::IsDlgButtonChecked(hwnd, kIdHyphensCheck) == BST_CHECKED);
    opts.format.braces = (::IsDlgButtonChecked(hwnd, kIdBracesCheck) == BST_CHECKED);

    const int qSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdQuotesCombo), CB_GETCURSEL, 0, 0));
    if (qSel == 1) {
        opts.format.quotes = QuoteStyle::Single;
    } else if (qSel == 2) {
        opts.format.quotes = QuoteStyle::Double;
    } else {
        opts.format.quotes = QuoteStyle::None;
    }

    const int sepSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdSeparatorCombo), CB_GETCURSEL, 0, 0));
    if (sepSel == 1) {
        opts.separator = BulkSeparator::CommaSpace;
    } else if (sepSel == 2) {
        opts.separator = BulkSeparator::CommaNewline;
    } else if (sepSel == 3) {
        opts.separator = BulkSeparator::Space;
    } else {
        opts.separator = BulkSeparator::Newline;
    }

    return opts;
}

void SaveOptionsToSettings(HWND hwnd, Plugin::Settings& settings) {
    const BulkGenerateOptions opts = ReadOptionsFromDialog(hwnd, "\r\n");
    settings.generator.bulkType = IdTypeName(opts.type);
    settings.generator.bulkCount = opts.count;
    settings.generator.bulkSeparator = BulkSeparatorName(opts.separator);
    settings.generator.quotes = QuoteStyleName(opts.format.quotes);

    if (opts.type == IdType::Guid) {
        settings.generator.guidUppercase = opts.format.uppercase;
        settings.generator.guidBraces = opts.format.braces;
    } else if (opts.type == IdType::Ulid) {
        settings.generator.ulidLowercase = !opts.format.uppercase;
    } else {
        settings.generator.uuidUppercase = opts.format.uppercase;
        settings.generator.uuidHyphens = opts.format.hyphens;
        settings.generator.uuidBraces = opts.format.braces;
    }

    const int defUuidSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdDefaultUuidCombo), CB_GETCURSEL, 0, 0));
    settings.generator.defaultUuidVersion = (defUuidSel == 1) ? L"v7" : L"v4";
    settings.Save();
}

void ApplyTypeFormattingDefaults(HWND hwnd, IdType type, const Plugin::Settings& settings) {
    const bool isUlid = (type == IdType::Ulid);
    ::EnableWindow(::GetDlgItem(hwnd, kIdHyphensCheck), isUlid ? FALSE : TRUE);
    ::EnableWindow(::GetDlgItem(hwnd, kIdBracesCheck), isUlid ? FALSE : TRUE);

    if (type == IdType::Guid) {
        ::CheckDlgButton(hwnd, kIdUppercaseCheck,
                         settings.generator.guidUppercase ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(hwnd, kIdHyphensCheck, BST_CHECKED);
        ::CheckDlgButton(hwnd, kIdBracesCheck,
                         settings.generator.guidBraces ? BST_CHECKED : BST_UNCHECKED);
    } else if (type == IdType::Ulid) {
        ::CheckDlgButton(hwnd, kIdUppercaseCheck,
                         settings.generator.ulidLowercase ? BST_UNCHECKED : BST_CHECKED);
    } else {
        ::CheckDlgButton(hwnd, kIdUppercaseCheck,
                         settings.generator.uuidUppercase ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(hwnd, kIdHyphensCheck,
                         settings.generator.uuidHyphens ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(hwnd, kIdBracesCheck,
                         settings.generator.uuidBraces ? BST_CHECKED : BST_UNCHECKED);
    }
}

void UpdatePreview(HWND hwnd) {
    BulkGenerateOptions opts = ReadOptionsFromDialog(hwnd, "\r\n");
    const int totalCount = opts.count;
    opts.count = std::min(totalCount, kMaxPreviewItems);

    const IdGenerateResult res = IdGeneratorService::Instance().GenerateBulk(opts);
    if (!res.ok) {
        ::SetDlgItemTextW(hwnd, kIdPreviewEdit, res.message.c_str());
        return;
    }

    std::wstring preview = Plugin::ToWide(res.text);
    if (totalCount > kMaxPreviewItems) {
        preview += L"\r\n... (" + std::to_wstring(totalCount) + L" total will be generated)";
    }
    ::SetDlgItemTextW(hwnd, kIdPreviewEdit, preview.c_str());
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

INT_PTR CALLBACK IdGeneratorDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    IdDialogContext* ctx =
        reinterpret_cast<IdDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<IdDialogContext*>(lParam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

            // Center relative to parent
            HWND parent = ::GetParent(hwnd);
            if (parent != nullptr) {
                RECT pr{}, dr{};
                ::GetWindowRect(parent, &pr);
                ::GetWindowRect(hwnd, &dr);
                const int x = pr.left + ((pr.right - pr.left) - (dr.right - dr.left)) / 2;
                const int y = pr.top + ((pr.bottom - pr.top) - (dr.bottom - dr.top)) / 2;
                ::SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }

            const IdType initialType = ParseIdType(ctx->settings->generator.bulkType);
            WORD typeRadio = kIdTypeUuidV4Radio;
            if (initialType == IdType::UuidV7) typeRadio = kIdTypeUuidV7Radio;
            else if (initialType == IdType::Guid) typeRadio = kIdTypeGuidRadio;
            else if (initialType == IdType::Ulid) typeRadio = kIdTypeUlidRadio;
            ::CheckRadioButton(hwnd, kIdTypeUuidV4Radio, kIdTypeUlidRadio, typeRadio);

            const int initialCount = std::clamp(ctx->settings->generator.bulkCount, 1, 100000);
            ::SetDlgItemInt(hwnd, kIdCountEdit, static_cast<UINT>(initialCount), FALSE);

            HWND hDefUuid = ::GetDlgItem(hwnd, kIdDefaultUuidCombo);
            ::SendMessageW(hDefUuid, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"UUID v4 (Random)"));
            ::SendMessageW(hDefUuid, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"UUID v7 (Timestamp)"));
            ::SendMessageW(hDefUuid, CB_SETCURSEL,
                           Plugin::EqualsIgnoreCase(ctx->settings->generator.defaultUuidVersion, L"v7")
                               ? 1
                               : 0,
                           0);

            HWND hQuotes = ::GetDlgItem(hwnd, kIdQuotesCombo);
            ::SendMessageW(hQuotes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"None"));
            ::SendMessageW(hQuotes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Single ('...')"));
            ::SendMessageW(hQuotes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Double (\"...\")"));
            const QuoteStyle q = ParseQuoteStyle(ctx->settings->generator.quotes);
            ::SendMessageW(hQuotes, CB_SETCURSEL, static_cast<WPARAM>(q), 0);

            HWND hSep = ::GetDlgItem(hwnd, kIdSeparatorCombo);
            ::SendMessageW(hSep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Newline"));
            ::SendMessageW(hSep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Comma + Space (, )"));
            ::SendMessageW(hSep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Comma + Newline"));
            ::SendMessageW(hSep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Space"));
            const BulkSeparator sep = ParseBulkSeparator(ctx->settings->generator.bulkSeparator);
            ::SendMessageW(hSep, CB_SETCURSEL, static_cast<WPARAM>(sep), 0);

            ApplyTypeFormattingDefaults(hwnd, initialType, *ctx->settings);
            UpdatePreview(hwnd);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if ((id >= kIdTypeUuidV4Radio && id <= kIdTypeUlidRadio) && code == BN_CLICKED) {
                ApplyTypeFormattingDefaults(hwnd, SelectedIdType(hwnd), *ctx->settings);
                UpdatePreview(hwnd);
                return TRUE;
            }

            if ((id == kIdUppercaseCheck || id == kIdHyphensCheck || id == kIdBracesCheck) &&
                code == BN_CLICKED) {
                UpdatePreview(hwnd);
                return TRUE;
            }

            if ((id == kIdQuotesCombo || id == kIdSeparatorCombo) && code == CBN_SELCHANGE) {
                UpdatePreview(hwnd);
                return TRUE;
            }

            if (id == kIdCountEdit && code == EN_CHANGE) {
                UpdatePreview(hwnd);
                return TRUE;
            }

            if (id == kIdRegenerateBtn && code == BN_CLICKED) {
                UpdatePreview(hwnd);
                return TRUE;
            }

            if (id == kIdSaveDefaultsBtn && code == BN_CLICKED) {
                SaveOptionsToSettings(hwnd, *ctx->settings);
                ::MessageBoxW(hwnd, L"Generator defaults saved to ArgpiSuite.ini.",
                              L"ArgpiSuite", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }

            if (id == kIdCopyBtn && code == BN_CLICKED) {
                const BulkGenerateOptions opts = ReadOptionsFromDialog(hwnd, ctx->documentEol);
                const IdGenerateResult res = IdGeneratorService::Instance().GenerateBulk(opts);
                if (!res.ok) {
                    ::MessageBoxW(hwnd, res.message.c_str(), L"ID Generator", MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                if (CopyUtf8ToClipboard(hwnd, res.text)) {
                    SaveOptionsToSettings(hwnd, *ctx->settings);
                    ::EndDialog(hwnd, kIdCopyBtn);
                }
                return TRUE;
            }

            if ((id == kIdInsertBtn || id == IDOK) && code == BN_CLICKED) {
                const BulkGenerateOptions opts = ReadOptionsFromDialog(hwnd, ctx->documentEol);
                const IdGenerateResult res = IdGeneratorService::Instance().GenerateBulk(opts);
                if (!res.ok) {
                    ::MessageBoxW(hwnd, res.message.c_str(), L"ID Generator", MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                SaveOptionsToSettings(hwnd, *ctx->settings);
                if (ctx->onInsert && ctx->onInsert(res.text, res.generatedCount)) {
                    ::EndDialog(hwnd, IDOK);
                } else {
                    ::MessageBoxW(hwnd, L"Could not insert into the active editor (is it read-only?).",
                                  L"ID Generator", MB_OK | MB_ICONWARNING);
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

void ShowIdGeneratorDialog(HWND parent, Plugin::Settings& settings,
                           const std::string& documentEol, InsertCallback onInsert) {
    TemplateBuilder builder(304, 286, L"UUID / GUID & ULID Generator");

    // Group 1: Type & Quantity
    builder.AddGroupbox(10, 8, 284, 62, L"Identifier Type && Quantity");
    builder.AddRadio(18, 22, 68, 12, kIdTypeUuidV4Radio, L"UUID v4 (Random)", true);
    builder.AddRadio(92, 22, 76, 12, kIdTypeUuidV7Radio, L"UUID v7 (Time)");
    builder.AddRadio(174, 22, 42, 12, kIdTypeGuidRadio, L"GUID");
    builder.AddRadio(222, 22, 42, 12, kIdTypeUlidRadio, L"ULID");

    builder.AddStatic(18, 43, 52, 10, L"Count (1-100k):");
    builder.AddEdit(72, 41, 48, 13, kIdCountEdit, ES_NUMBER | ES_AUTOHSCROLL);
    builder.AddStatic(132, 43, 68, 10, L"Default UUID Cmd:");
    builder.AddCombobox(202, 40, 84, 14, kIdDefaultUuidCombo);

    // Group 2: Formatting & Separator
    builder.AddGroupbox(10, 76, 284, 60, L"Formatting && Separator");
    builder.AddCheckbox(18, 90, 56, 12, kIdUppercaseCheck, L"Uppercase");
    builder.AddCheckbox(82, 90, 76, 12, kIdHyphensCheck, L"Include hyphens");
    builder.AddCheckbox(166, 90, 80, 12, kIdBracesCheck, L"Wrap in braces { }");

    builder.AddStatic(18, 111, 28, 10, L"Quotes:");
    builder.AddCombobox(48, 109, 76, 14, kIdQuotesCombo);
    builder.AddStatic(134, 111, 40, 10, L"Separator:");
    builder.AddCombobox(176, 109, 110, 14, kIdSeparatorCombo);

    // Group 3: Live Preview
    builder.AddGroupbox(10, 142, 284, 114, L"Live Preview");
    builder.AddEdit(18, 155, 268, 93, kIdPreviewEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);

    // Bottom Actions
    builder.AddButton(10, 263, 54, 15, kIdRegenerateBtn, L"Regenerate");
    builder.AddButton(68, 263, 56, 15, kIdSaveDefaultsBtn, L"Save Defaults");
    builder.AddButton(128, 263, 60, 15, kIdCopyBtn, L"Copy");
    builder.AddButton(192, 263, 60, 15, kIdInsertBtn, L"Insert at Cursor", true);
    builder.AddButton(256, 263, 38, 15, IDCANCEL, L"Close");

    const std::vector<unsigned char> dlgData = builder.Build();
    IdDialogContext ctx;
    ctx.settings = &settings;
    ctx.documentEol = documentEol.empty() ? "\r\n" : documentEol;
    ctx.onInsert = std::move(onInsert);

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, IdGeneratorDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Generator


#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "HashDialog.h"

#include <windows.h>
#include <commdlg.h>

#include <cstring>
#include <string>
#include <vector>

#include "PluginDefinition.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Hash {
namespace {

constexpr WORD kIdSourceSelRadio     = 43001;
constexpr WORD kIdSourceDocRadio     = 43002;
constexpr WORD kIdSourceFileRadio    = 43003;
constexpr WORD kIdFilePathEdit       = 43004;
constexpr WORD kIdBrowseBtn          = 43005;

constexpr WORD kIdAlgorithmCombo     = 43010;
constexpr WORD kIdHmacKeyEdit        = 43011;
constexpr WORD kIdHmacHexCheck       = 43012;
constexpr WORD kIdUppercaseCheck     = 43013;
constexpr WORD kIdQuickActionCombo   = 43014;

constexpr WORD kIdHexOutputEdit      = 43020;
constexpr WORD kIdB64OutputEdit      = 43021;
constexpr WORD kIdStatusLabel        = 43022;
constexpr WORD kIdVerifyEdit         = 43023;
constexpr WORD kIdVerifyStatusLabel  = 43024;

constexpr WORD kIdSaveDefaultsBtn    = 43030;
constexpr WORD kIdCopyBtn            = 43031;
constexpr WORD kIdInsertBtn          = 43032;

constexpr HashAlgorithm kAlgorithms[] = {
    HashAlgorithm::Md5,
    HashAlgorithm::Sha1,
    HashAlgorithm::Sha256,
    HashAlgorithm::DoubleSha256,
    HashAlgorithm::Sha384,
    HashAlgorithm::Sha512,
    HashAlgorithm::Blake2b256,
    HashAlgorithm::Blake2b512,
    HashAlgorithm::Crc32,
    HashAlgorithm::HmacMd5,
    HashAlgorithm::HmacSha1,
    HashAlgorithm::HmacSha256,
    HashAlgorithm::HmacSha384,
    HashAlgorithm::HmacSha512,
};
constexpr int kAlgorithmCount = static_cast<int>(sizeof(kAlgorithms) / sizeof(kAlgorithms[0]));

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

struct HashDialogContext {
    Plugin::Settings* settings = nullptr;
    const HashDialogInput* input = nullptr;
    HashInsertCallback onInsert;
    std::string lastHex;
};

HashAlgorithm SelectedAlgorithm(HWND hwnd) {
    const int sel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdAlgorithmCombo), CB_GETCURSEL, 0, 0));
    if (sel >= 0 && sel < kAlgorithmCount) {
        return kAlgorithms[sel];
    }
    return HashAlgorithm::Sha256;
}

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

void UpdateVerifyStatus(HWND hwnd, const std::string& computedHex) {
    const std::string expected = Plugin::ToUtf8(GetEditText(hwnd, kIdVerifyEdit));
    if (Plugin::Trim(Plugin::ToWide(expected)).empty()) {
        ::SetDlgItemTextW(hwnd, kIdVerifyStatusLabel, L"");
        return;
    }
    if (computedHex.empty()) {
        ::SetDlgItemTextW(hwnd, kIdVerifyStatusLabel, L"No digest");
        return;
    }
    const bool match = HashService::VerifyChecksum(computedHex, expected);
    ::SetDlgItemTextW(hwnd, kIdVerifyStatusLabel, match ? L"MATCH" : L"MISMATCH");
}

void UpdateHmacControls(HWND hwnd) {
    const bool isHmac = HashService::IsHmac(SelectedAlgorithm(hwnd));
    ::EnableWindow(::GetDlgItem(hwnd, kIdHmacKeyEdit), isHmac ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(hwnd, kIdHmacHexCheck), isHmac ? TRUE : FALSE);
}

void UpdateFileControls(HWND hwnd) {
    const bool isFile = (::IsDlgButtonChecked(hwnd, kIdSourceFileRadio) == BST_CHECKED);
    ::EnableWindow(::GetDlgItem(hwnd, kIdFilePathEdit), isFile ? TRUE : FALSE);
    ::EnableWindow(::GetDlgItem(hwnd, kIdBrowseBtn), isFile ? TRUE : FALSE);
}

void RecomputeHash(HWND hwnd, HashDialogContext* ctx) {
    if (ctx == nullptr || ctx->input == nullptr) {
        return;
    }

    HashOptions opts;
    opts.algorithm = SelectedAlgorithm(hwnd);
    opts.uppercase = (::IsDlgButtonChecked(hwnd, kIdUppercaseCheck) == BST_CHECKED);
    opts.hmacKeyIsHex = (::IsDlgButtonChecked(hwnd, kIdHmacHexCheck) == BST_CHECKED);
    if (HashService::IsHmac(opts.algorithm)) {
        opts.hmacKeyUtf8 = Plugin::ToUtf8(GetEditText(hwnd, kIdHmacKeyEdit));
    }

    HashResult res;
    if (::IsDlgButtonChecked(hwnd, kIdSourceFileRadio) == BST_CHECKED) {
        const std::wstring path = Plugin::Trim(GetEditText(hwnd, kIdFilePathEdit));
        if (path.empty()) {
            ctx->lastHex.clear();
            ::SetDlgItemTextW(hwnd, kIdHexOutputEdit, L"");
            ::SetDlgItemTextW(hwnd, kIdB64OutputEdit, L"");
            ::SetDlgItemTextW(hwnd, kIdStatusLabel, L"Select a file on disk to hash.");
            UpdateVerifyStatus(hwnd, "");
            return;
        }
        res = HashService::Instance().ComputeFile(path, opts);
    } else if (::IsDlgButtonChecked(hwnd, kIdSourceSelRadio) == BST_CHECKED) {
        res = HashService::Instance().Compute(ctx->input->selectionBytes, opts);
    } else {
        res = HashService::Instance().Compute(ctx->input->documentBytes, opts);
    }

    if (!res.ok) {
        ctx->lastHex.clear();
        ::SetDlgItemTextW(hwnd, kIdHexOutputEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdB64OutputEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdStatusLabel, res.message.c_str());
        UpdateVerifyStatus(hwnd, "");
        return;
    }

    ctx->lastHex = res.hex;
    ::SetDlgItemTextW(hwnd, kIdHexOutputEdit, Plugin::ToWide(res.hex).c_str());
    ::SetDlgItemTextW(hwnd, kIdB64OutputEdit, Plugin::ToWide(res.base64).c_str());
    const std::wstring status = AlgorithmDisplayName(opts.algorithm) + L" | " +
                                std::to_wstring(res.bitLength) + L"-bit | " +
                                std::to_wstring(res.inputBytes) + L" byte(s)";
    ::SetDlgItemTextW(hwnd, kIdStatusLabel, status.c_str());
    UpdateVerifyStatus(hwnd, res.hex);
}

void SaveHashSettings(HWND hwnd, Plugin::Settings& settings) {
    settings.hashing.uppercase = (::IsDlgButtonChecked(hwnd, kIdUppercaseCheck) == BST_CHECKED);
    settings.hashing.hmacKeyIsHex = (::IsDlgButtonChecked(hwnd, kIdHmacHexCheck) == BST_CHECKED);
    settings.hashing.lastAlgorithm = AlgorithmId(SelectedAlgorithm(hwnd));

    const int qaSel = static_cast<int>(
        ::SendMessageW(::GetDlgItem(hwnd, kIdQuickActionCombo), CB_GETCURSEL, 0, 0));
    settings.hashing.quickAction = (qaSel == 1) ? L"insert" : L"copy";
    settings.Save();
}

bool CopyTextToClipboard(HWND owner, const std::string& utf8) {
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

INT_PTR CALLBACK HashDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    HashDialogContext* ctx =
        reinterpret_cast<HashDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<HashDialogContext*>(lParam);
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
                ::CheckRadioButton(hwnd, kIdSourceSelRadio, kIdSourceFileRadio, kIdSourceDocRadio);
            } else {
                ::CheckRadioButton(hwnd, kIdSourceSelRadio, kIdSourceFileRadio, kIdSourceSelRadio);
            }

            if (!ctx->input->activeFilePath.empty()) {
                ::SetDlgItemTextW(hwnd, kIdFilePathEdit, ctx->input->activeFilePath.c_str());
            }

            HWND hAlg = ::GetDlgItem(hwnd, kIdAlgorithmCombo);
            const HashAlgorithm targetAlg =
                ctx->input->useOverrideAlgorithm
                    ? ctx->input->overrideAlgorithm
                    : ParseAlgorithmId(ctx->settings->hashing.lastAlgorithm);
            int targetIdx = 2;  // SHA-256
            for (int i = 0; i < kAlgorithmCount; ++i) {
                ::SendMessageW(hAlg, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(AlgorithmDisplayName(kAlgorithms[i]).c_str()));
                if (kAlgorithms[i] == targetAlg) {
                    targetIdx = i;
                }
            }
            ::SendMessageW(hAlg, CB_SETCURSEL, targetIdx, 0);

            ::CheckDlgButton(hwnd, kIdUppercaseCheck,
                             ctx->settings->hashing.uppercase ? BST_CHECKED : BST_UNCHECKED);
            ::CheckDlgButton(hwnd, kIdHmacHexCheck,
                             ctx->settings->hashing.hmacKeyIsHex ? BST_CHECKED : BST_UNCHECKED);

            HWND hQuick = ::GetDlgItem(hwnd, kIdQuickActionCombo);
            ::SendMessageW(hQuick, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Copy to Clipboard"));
            ::SendMessageW(hQuick, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(L"Insert at Cursor"));
            ::SendMessageW(hQuick, CB_SETCURSEL,
                           Plugin::EqualsIgnoreCase(ctx->settings->hashing.quickAction, L"insert")
                               ? 1
                               : 0,
                           0);

            UpdateFileControls(hwnd);
            UpdateHmacControls(hwnd);
            RecomputeHash(hwnd, ctx);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if ((id >= kIdSourceSelRadio && id <= kIdSourceFileRadio) && code == BN_CLICKED) {
                UpdateFileControls(hwnd);
                RecomputeHash(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdBrowseBtn && code == BN_CLICKED) {
                wchar_t fileBuf[32768] = {};
                const std::wstring current = GetEditText(hwnd, kIdFilePathEdit);
                if (!current.empty() && current.size() < 32768) {
                    std::memcpy(fileBuf, current.c_str(), (current.size() + 1) * sizeof(wchar_t));
                }
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = L"All Files (*.*)\0*.*\0";
                ofn.lpstrFile = fileBuf;
                ofn.nMaxFile = 32768;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
                if (::GetOpenFileNameW(&ofn)) {
                    ::SetDlgItemTextW(hwnd, kIdFilePathEdit, fileBuf);
                    ::CheckRadioButton(hwnd, kIdSourceSelRadio, kIdSourceFileRadio, kIdSourceFileRadio);
                    UpdateFileControls(hwnd);
                    RecomputeHash(hwnd, ctx);
                }
                return TRUE;
            }

            if (id == kIdAlgorithmCombo && code == CBN_SELCHANGE) {
                UpdateHmacControls(hwnd);
                RecomputeHash(hwnd, ctx);
                return TRUE;
            }

            if ((id == kIdUppercaseCheck || id == kIdHmacHexCheck) && code == BN_CLICKED) {
                RecomputeHash(hwnd, ctx);
                return TRUE;
            }

            if ((id == kIdHmacKeyEdit || id == kIdFilePathEdit) && code == EN_CHANGE) {
                RecomputeHash(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdVerifyEdit && code == EN_CHANGE) {
                UpdateVerifyStatus(hwnd, ctx != nullptr ? ctx->lastHex : "");
                return TRUE;
            }

            if (id == kIdSaveDefaultsBtn && code == BN_CLICKED) {
                SaveHashSettings(hwnd, *ctx->settings);
                ::MessageBoxW(hwnd, L"Hashing defaults saved to ArgpiSuite.ini.",
                              L"ArgpiSuite", MB_OK | MB_ICONINFORMATION);
                return TRUE;
            }

            if (id == kIdCopyBtn && code == BN_CLICKED) {
                if (!ctx->lastHex.empty() && CopyTextToClipboard(hwnd, ctx->lastHex)) {
                    SaveHashSettings(hwnd, *ctx->settings);
                    ::EndDialog(hwnd, kIdCopyBtn);
                }
                return TRUE;
            }

            if ((id == kIdInsertBtn || id == IDOK) && code == BN_CLICKED) {
                if (ctx->lastHex.empty()) {
                    return TRUE;
                }
                SaveHashSettings(hwnd, *ctx->settings);
                if (ctx->onInsert && ctx->onInsert(ctx->lastHex)) {
                    ::EndDialog(hwnd, IDOK);
                } else {
                    ::MessageBoxW(hwnd, L"Could not insert into the active editor (is it read-only?).",
                                  L"Hash & Checksum Tool", MB_OK | MB_ICONWARNING);
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

void ShowHashDialog(HWND parent, Plugin::Settings& settings, const HashDialogInput& input,
                    HashInsertCallback onInsert) {
    TemplateBuilder builder(320, 290, L"Hash & Checksum Tool");

    // Group 1: Source
    builder.AddGroupbox(10, 8, 300, 56, L"Input Source");
    builder.AddRadio(18, 22, 85, 12, kIdSourceSelRadio, L"Selected Text", true);
    builder.AddRadio(112, 22, 95, 12, kIdSourceDocRadio, L"Active Document");
    builder.AddRadio(214, 22, 80, 12, kIdSourceFileRadio, L"File on Disk");
    builder.AddEdit(18, 40, 230, 13, kIdFilePathEdit, ES_AUTOHSCROLL);
    builder.AddButton(254, 39, 48, 15, kIdBrowseBtn, L"Browse…");

    // Group 2: Algorithm & Options
    builder.AddGroupbox(10, 70, 300, 68, L"Algorithm && Options");
    builder.AddStatic(18, 85, 42, 10, L"Algorithm:");
    builder.AddCombobox(62, 83, 122, 14, kIdAlgorithmCombo);
    builder.AddCheckbox(194, 85, 105, 12, kIdUppercaseCheck, L"Uppercase hex");

    builder.AddStatic(18, 104, 42, 10, L"HMAC Key:");
    builder.AddEdit(62, 102, 174, 13, kIdHmacKeyEdit, ES_AUTOHSCROLL);
    builder.AddCheckbox(242, 103, 60, 12, kIdHmacHexCheck, L"Key is Hex");

    builder.AddStatic(18, 122, 76, 10, L"Quick Menu Action:");
    builder.AddCombobox(96, 120, 110, 14, kIdQuickActionCombo);

    // Group 3: Digest Output
    builder.AddGroupbox(10, 144, 300, 72, L"Computed Digest");
    builder.AddStatic(18, 158, 28, 10, L"Hex:");
    builder.AddEdit(48, 156, 254, 22, kIdHexOutputEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);
    builder.AddStatic(18, 184, 28, 10, L"Base64:");
    builder.AddEdit(48, 182, 254, 13, kIdB64OutputEdit, ES_READONLY | ES_AUTOHSCROLL);
    builder.AddStatic(18, 201, 284, 10, L"", kIdStatusLabel);

    // Group 4: Verify Checksum
    builder.AddGroupbox(10, 222, 300, 34, L"Verify Expected Checksum");
    builder.AddEdit(18, 236, 222, 13, kIdVerifyEdit, ES_AUTOHSCROLL);
    builder.AddStatic(246, 238, 56, 10, L"", kIdVerifyStatusLabel);

    // Bottom Actions
    builder.AddButton(10, 265, 62, 15, kIdSaveDefaultsBtn, L"Save Defaults");
    builder.AddButton(128, 265, 58, 15, kIdCopyBtn, L"Copy Hex", true);
    builder.AddButton(190, 265, 68, 15, kIdInsertBtn, L"Insert at Cursor");
    builder.AddButton(262, 265, 48, 15, IDCANCEL, L"Close");

    const std::vector<unsigned char> dlgData = builder.Build();
    HashDialogContext ctx;
    ctx.settings = &settings;
    ctx.input = &input;
    ctx.onInsert = std::move(onInsert);

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, HashDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Hash


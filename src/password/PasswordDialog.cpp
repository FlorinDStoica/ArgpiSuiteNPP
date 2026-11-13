#include "PasswordDialog.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "CryptoHash.h"
#include "PasswordService.h"
#include "PluginDefinition.h"
#include "Scintilla.h"
#include "plugin/PluginHost.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Password {
namespace {

constexpr WORD kIdManualPassword      = 30300;
constexpr WORD kIdRevealPassword      = 30301;
constexpr WORD kIdGenerateRandom      = 30302;
constexpr WORD kIdCopyPassword        = 30303;
constexpr WORD kIdInsertPassword      = 30304;

constexpr WORD kIdComplexityBasic     = 30310;
constexpr WORD kIdComplexityBalanced  = 30311;
constexpr WORD kIdComplexityStrong    = 30312;
constexpr WORD kIdComplexityElite     = 30313;

constexpr WORD kIdLengthNumber        = 30321;

constexpr WORD kIdIncludeLower        = 30322;
constexpr WORD kIdIncludeUpper        = 30323;
constexpr WORD kIdIncludeDigits       = 30324;
constexpr WORD kIdIncludeSymbols      = 30325;
constexpr WORD kIdExcludeAmbiguous    = 30326;
constexpr WORD kIdExcludeChars        = 30327;

constexpr WORD kIdPresetCustom        = 30330;
constexpr WORD kIdPresetJoomla        = 30331;
constexpr WORD kIdPresetWordpress     = 30332;
constexpr WORD kIdPresetDrupal        = 30333;
constexpr WORD kIdPresetMagento       = 30334;

constexpr WORD kIdStrengthLabel       = 30340;

constexpr WORD kIdPassphraseWords     = 30342;
constexpr WORD kIdPassphraseSep       = 30343;
constexpr WORD kIdGeneratePassphrase  = 30344;
constexpr WORD kIdPassphraseOutput    = 30345;
constexpr WORD kIdInsertPassphrase    = 30346;
constexpr WORD kIdCopyPassphrase      = 30347;

constexpr WORD kIdAlgorithmCombo      = 30350;
constexpr WORD kIdAlgorithmDesc       = 30351;
constexpr WORD kIdSecretKey           = 30352;
constexpr WORD kIdRevealSecret        = 30353;
constexpr WORD kIdRememberSecret      = 30354;

constexpr WORD kIdParam1Label         = 30360;
constexpr WORD kIdParam1Edit          = 30361;
constexpr WORD kIdParam2Label         = 30362;
constexpr WORD kIdParam2Edit          = 30363;

constexpr WORD kIdGenerateHash        = 30370;
constexpr WORD kIdCopyHash            = 30371;
constexpr WORD kIdInsertHash          = 30372;
constexpr WORD kIdHashOutput          = 30373;
constexpr WORD kIdHashInfo            = 30374;

constexpr WORD kIdVerifyHash          = 30381;
constexpr WORD kIdVerifyPassword      = 30382;
constexpr WORD kIdRevealVerify        = 30383;
constexpr WORD kIdVerifyBtn           = 30384;
constexpr WORD kIdVerifyResult        = 30385;

constexpr WORD kIdStatusBox           = 30390;
constexpr WORD kIdClearAll            = 30391;
constexpr WORD kIdResetSettings       = 30392;

constexpr UINT_PTR kTimerClipboardClear = 40101;

// --- Dialog Template Builder ---------------------------------------------

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

void WriteControlClass(std::vector<unsigned char>& data, WORD atom) {
    WriteWord(data, 0xFFFF);
    WriteWord(data, atom);
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
        WriteWord(data_, 9);  // font point size
        WriteWide(data_, L"Segoe UI");
    }

    void AddStatic(short x, short y, short cx, short cy, const wchar_t* text, WORD id = 0xFFFF) {
        AddItem(WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, cx, cy, 0x0082, id, text);
    }

    void AddGroupbox(short x, short y, short cx, short cy, const wchar_t* text, WORD id = 0xFFFF) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, cx, cy, 0x0080, id, text);
    }

    void AddEdit(short x, short y, short cx, short cy, WORD id, bool password = false, bool multiline = false, bool readOnly = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP;
        if (password) {
            style |= ES_PASSWORD | ES_AUTOHSCROLL;
        } else if (multiline) {
            style |= ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL;
        } else {
            style |= ES_AUTOHSCROLL;
        }
        if (readOnly) {
            style |= ES_READONLY;
        }
        AddItem(style, x, y, cx, cy, 0x0081, id, L"");
    }

    void AddCheckbox(short x, short y, short cx, short cy, WORD id, const wchar_t* text) {
        AddItem(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, x, y, cx, cy, 0x0080, id, text);
    }

    void AddRadio(short x, short y, short cx, short cy, WORD id, const wchar_t* text, bool groupStart = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP;
        if (groupStart) {
            style |= WS_GROUP;
        }
        AddItem(style, x, y, cx, cy, 0x0080, id, text);
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
        WriteControlClass(data_, atom);
        WriteWide(data_, text);
        WriteWord(data_, 0);  // creation data size
        ++items_;
    }

    std::vector<unsigned char> data_;
    int items_ = 0;
};

// --- Dialog State & Logic ------------------------------------------------

struct PasswordDialogState {
    Plugin::Settings* settings = nullptr;
    std::wstring lastCopiedPassword;
    bool revealPassword = false;
    bool revealSecret = false;
    bool revealVerify = false;
};

void SetText(HWND dialog, WORD id, const std::wstring& text) {
    ::SetDlgItemTextW(dialog, id, text.c_str());
}

std::wstring GetText(HWND dialog, WORD id) {
    wchar_t buffer[4096] = {};
    ::GetDlgItemTextW(dialog, id, buffer, static_cast<int>(std::size(buffer)));
    return Plugin::Trim(buffer);
}

void ToggleReveal(HWND dialog, WORD editId, WORD btnId, bool& isRevealed) {
    isRevealed = !isRevealed;
    HWND hEdit = ::GetDlgItem(dialog, editId);
    ::SendMessageW(hEdit, EM_SETPASSWORDCHAR, isRevealed ? 0 : static_cast<WPARAM>(L'*'), 0);
    ::InvalidateRect(hEdit, nullptr, TRUE);
    ::SetDlgItemTextW(dialog, btnId, isRevealed ? L"Hide" : L"Show");
}

void CopyTextToClipboard(HWND owner, const std::wstring& text, bool scheduleClear,
                         PasswordDialogState* state) {
    if (text.empty() || owner == nullptr) {
        return;
    }
    if (!::OpenClipboard(owner)) {
        return;
    }
    ::EmptyClipboard();

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != nullptr) {
        void* ptr = ::GlobalLock(hMem);
        if (ptr != nullptr) {
            std::memcpy(ptr, text.c_str(), bytes);
            ::GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    ::CloseClipboard();

    if (scheduleClear && state != nullptr) {
        state->lastCopiedPassword = text;
        ::SetTimer(owner, kTimerClipboardClear, 30000, nullptr);
    }
}

void InsertIntoCurrentScintilla(const std::wstring& text) {
    Plugin::PluginHost& host = Plugin::PluginHost::Instance();
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr || text.empty()) {
        return;
    }
    const std::string utf8 = Plugin::ToUtf8(text);
    ::SendMessageW(scintilla, SCI_BEGINUNDOACTION, 0, 0);
    ::SendMessageW(scintilla, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(utf8.c_str()));
    ::SendMessageW(scintilla, SCI_ENDUNDOACTION, 0, 0);
}

void UpdateStrengthDisplay(HWND dialog) {
    const std::wstring pwd = GetText(dialog, kIdManualPassword);
    if (pwd.empty()) {
        SetText(dialog, kIdStrengthLabel, L"— · 0 bits");
        return;
    }
    const int pool = EstimatePoolSize(pwd);
    const double entropy = EstimateEntropy(pwd, pool);
    const std::wstring label = StrengthRating(entropy);

    std::wostringstream ss;
    ss << label << L" · " << std::fixed << std::setprecision(0) << entropy << L" bits (estimated)";
    SetText(dialog, kIdStrengthLabel, ss.str());
}

void UpdateParameterControls(HWND dialog, Algorithm alg, const Plugin::PasswordSettings& cfg) {
    HWND hP1Label = ::GetDlgItem(dialog, kIdParam1Label);
    HWND hP1Edit  = ::GetDlgItem(dialog, kIdParam1Edit);
    HWND hP2Label = ::GetDlgItem(dialog, kIdParam2Label);
    HWND hP2Edit  = ::GetDlgItem(dialog, kIdParam2Edit);

    if (alg == Algorithm::JoomlaBcrypt || alg == Algorithm::CustomBcrypt) {
        ::ShowWindow(hP1Label, SW_SHOW);
        ::ShowWindow(hP1Edit, SW_SHOW);
        ::ShowWindow(hP2Label, SW_HIDE);
        ::ShowWindow(hP2Edit, SW_HIDE);
        ::SetWindowTextW(hP1Label, L"Cost (rounds 4-31):");
        SetText(dialog, kIdParam1Edit, std::to_wstring(cfg.bcryptCost));
    } else if (alg == Algorithm::Argon2id) {
        ::ShowWindow(hP1Label, SW_SHOW);
        ::ShowWindow(hP1Edit, SW_SHOW);
        ::ShowWindow(hP2Label, SW_SHOW);
        ::ShowWindow(hP2Edit, SW_SHOW);
        ::SetWindowTextW(hP1Label, L"Time t (1-10) / Mem KiB:");
        ::SetWindowTextW(hP2Label, L"Parallelism p / Hash Len:");
        SetText(dialog, kIdParam1Edit, std::to_wstring(cfg.argon2t) + L" / " + std::to_wstring(cfg.argon2m));
        SetText(dialog, kIdParam2Edit, std::to_wstring(cfg.argon2p) + L" / " + std::to_wstring(cfg.argon2HashLength));
    } else if (alg == Algorithm::Pbkdf2Secret) {
        ::ShowWindow(hP1Label, SW_SHOW);
        ::ShowWindow(hP1Edit, SW_SHOW);
        ::ShowWindow(hP2Label, SW_SHOW);
        ::ShowWindow(hP2Edit, SW_SHOW);
        ::SetWindowTextW(hP1Label, L"Iterations (10k-5M):");
        ::SetWindowTextW(hP2Label, L"Key Length (bytes):");
        SetText(dialog, kIdParam1Edit, std::to_wstring(cfg.pbkdf2Iterations));
        SetText(dialog, kIdParam2Edit, std::to_wstring(cfg.pbkdf2KeyLength));
    } else {
        ::ShowWindow(hP1Label, SW_HIDE);
        ::ShowWindow(hP1Edit, SW_HIDE);
        ::ShowWindow(hP2Label, SW_HIDE);
        ::ShowWindow(hP2Edit, SW_HIDE);
    }
}

void SyncUiFromSettings(HWND dialog, const Plugin::PasswordSettings& cfg) {
    SetText(dialog, kIdLengthNumber, std::to_wstring(cfg.length));
    ::CheckDlgButton(dialog, kIdIncludeLower, cfg.useLower ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdIncludeUpper, cfg.useUpper ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdIncludeDigits, cfg.useDigits ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdIncludeSymbols, cfg.useSymbols ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdExcludeAmbiguous, cfg.avoidAmbiguous ? BST_CHECKED : BST_UNCHECKED);
    SetText(dialog, kIdExcludeChars, cfg.excludeChars);

    const Complexity comp = ParseComplexity(cfg.complexity);
    ::CheckRadioButton(dialog, kIdComplexityBasic, kIdComplexityElite,
                       kIdComplexityBasic + static_cast<int>(comp));

    HWND hCombo = ::GetDlgItem(dialog, kIdAlgorithmCombo);
    const Algorithm alg = ParseAlgorithm(cfg.algorithm);
    ::SendMessageW(hCombo, CB_SETCURSEL, static_cast<WPARAM>(alg), 0);
    SetText(dialog, kIdAlgorithmDesc, AlgorithmDescription(alg));

    SetText(dialog, kIdSecretKey, cfg.rememberSecret ? cfg.secretKey : L"");
    ::CheckDlgButton(dialog, kIdRememberSecret, cfg.rememberSecret ? BST_CHECKED : BST_UNCHECKED);

    UpdateParameterControls(dialog, alg, cfg);

    SetText(dialog, kIdPassphraseWords, std::to_wstring(cfg.words));
    SetText(dialog, kIdPassphraseSep, cfg.separator.empty() ? L"-" : cfg.separator);
}

void ApplyComplexityProfileToUi(HWND dialog, Complexity comp) {
    const PasswordPolicy pol = PolicyForComplexity(comp);
    SetText(dialog, kIdLengthNumber, std::to_wstring(pol.length));
    ::CheckDlgButton(dialog, kIdIncludeLower, pol.useLower ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdIncludeUpper, pol.useUpper ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdIncludeDigits, pol.useDigits ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdIncludeSymbols, pol.useSymbols ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(dialog, kIdExcludeAmbiguous, pol.avoidAmbiguous ? BST_CHECKED : BST_UNCHECKED);
}

void ApplyPresetToUi(HWND dialog, Preset pr) {
    Algorithm alg = Algorithm::JoomlaBcrypt;
    std::wstring secretKey;
    ApplyPreset(pr, alg, secretKey);

    HWND hCombo = ::GetDlgItem(dialog, kIdAlgorithmCombo);
    ::SendMessageW(hCombo, CB_SETCURSEL, static_cast<WPARAM>(alg), 0);
    SetText(dialog, kIdAlgorithmDesc, AlgorithmDescription(alg));
    SetText(dialog, kIdSecretKey, secretKey);

    Plugin::Settings* s = reinterpret_cast<PasswordDialogState*>(
        ::GetWindowLongPtrW(dialog, GWLP_USERDATA))->settings;
    if (s != nullptr) {
        UpdateParameterControls(dialog, alg, s->password);
    }
}

PasswordPolicy PolicyFromUi(HWND dialog) {
    PasswordPolicy p;
    p.length = ::GetDlgItemInt(dialog, kIdLengthNumber, nullptr, FALSE);
    p.length = std::min(4096, std::max(4, p.length));
    p.useLower = ::IsDlgButtonChecked(dialog, kIdIncludeLower) == BST_CHECKED;
    p.useUpper = ::IsDlgButtonChecked(dialog, kIdIncludeUpper) == BST_CHECKED;
    p.useDigits = ::IsDlgButtonChecked(dialog, kIdIncludeDigits) == BST_CHECKED;
    p.useSymbols = ::IsDlgButtonChecked(dialog, kIdIncludeSymbols) == BST_CHECKED;
    p.avoidAmbiguous = ::IsDlgButtonChecked(dialog, kIdExcludeAmbiguous) == BST_CHECKED;
    p.excludeChars = GetText(dialog, kIdExcludeChars);
    return p;
}

HashParameters HashParamsFromUi(HWND dialog, const Plugin::PasswordSettings& cfg) {
    HashParameters params;
    HWND hCombo = ::GetDlgItem(dialog, kIdAlgorithmCombo);
    const int sel = static_cast<int>(::SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
    params.algorithm = static_cast<Algorithm>(sel >= 0 ? sel : 0);
    params.secretKey = GetText(dialog, kIdSecretKey);

    params.bcryptCost = cfg.bcryptCost;
    params.argon2.t = cfg.argon2t;
    params.argon2.m = cfg.argon2m;
    params.argon2.p = cfg.argon2p;
    params.argon2.hashLength = cfg.argon2HashLength;
    params.pbkdf2Iterations = cfg.pbkdf2Iterations;
    params.pbkdf2KeyLength = cfg.pbkdf2KeyLength;

    if (params.algorithm == Algorithm::JoomlaBcrypt || params.algorithm == Algorithm::CustomBcrypt) {
        const int c = ::GetDlgItemInt(dialog, kIdParam1Edit, nullptr, FALSE);
        if (c >= 4 && c <= 31) {
            params.bcryptCost = c;
        }
    }
    return params;
}

void GeneratePasswordInDialog(HWND dialog) {
    const PasswordPolicy policy = PolicyFromUi(dialog);
    const PasswordResult res = PasswordService::Instance().Generate(policy);
    if (res.ok) {
        SetText(dialog, kIdManualPassword, res.value);
        UpdateStrengthDisplay(dialog);
        SetText(dialog, kIdStatusBox, L"Password generated locally.");
    } else {
        SetText(dialog, kIdStatusBox, res.message);
    }
}

void GenerateHashInDialog(HWND dialog, PasswordDialogState* state) {
    const std::wstring pwd = GetText(dialog, kIdManualPassword);
    if (pwd.empty()) {
        SetText(dialog, kIdStatusBox, L"Please enter a password first.");
        return;
    }

    const HashParameters params = HashParamsFromUi(dialog, state->settings->password);
    const HashResult res = PasswordService::Instance().ComputeHash(pwd, params);
    if (res.ok) {
        SetText(dialog, kIdHashOutput, res.hash);
        std::wostringstream ss;
        ss << res.algorithmInfo << L" · " << res.hashLength << L" chars";
        SetText(dialog, kIdHashInfo, ss.str());

        if (!res.warning.empty()) {
            SetText(dialog, kIdStatusBox, res.warning);
        } else {
            SetText(dialog, kIdStatusBox, L"Hash generated locally in your browser/suite.");
        }
    } else {
        SetText(dialog, kIdHashOutput, L"");
        SetText(dialog, kIdHashInfo, L"—");
        SetText(dialog, kIdStatusBox, res.errorMessage);
    }
}

void VerifyInDialog(HWND dialog) {
    const std::wstring hash = GetText(dialog, kIdVerifyHash);
    const std::wstring pwd = GetText(dialog, kIdVerifyPassword);
    const VerifyResult res = PasswordService::Instance().Verify(pwd, hash);
    SetText(dialog, kIdVerifyResult, res.message);
}

INT_PTR CALLBACK PasswordDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    PasswordDialogState* state = reinterpret_cast<PasswordDialogState*>(
        ::GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<PasswordDialogState*>(lParam);
            ::SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            if (state == nullptr || state->settings == nullptr) {
                return FALSE;
            }

            // Populate Algorithm Combo
            HWND hCombo = ::GetDlgItem(dialog, kIdAlgorithmCombo);
            for (int i = 0; i <= static_cast<int>(Algorithm::Pbkdf2Secret); ++i) {
                ::SendMessageW(hCombo, CB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(AlgorithmDisplayName(static_cast<Algorithm>(i)).c_str()));
            }

            SyncUiFromSettings(dialog, state->settings->password);

            // Pre-fill password from active selection in Scintilla if available, or generate one
            Plugin::PluginHost& host = Plugin::PluginHost::Instance();
            HWND scintilla = host.CurrentScintilla();
            bool hasSelection = false;
            if (scintilla != nullptr) {
                const auto selLen = ::SendMessageW(scintilla, SCI_GETSELTEXT, 0, 0);
                if (selLen > 1 && selLen <= 1024) {
                    std::vector<char> buf(static_cast<size_t>(selLen) + 1);
                    ::SendMessageW(scintilla, SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(buf.data()));
                    const std::wstring selText = Plugin::ToWide(buf.data());
                    if (!selText.empty()) {
                        SetText(dialog, kIdManualPassword, selText);
                        hasSelection = true;
                    }
                }
            }

            if (!hasSelection) {
                GeneratePasswordInDialog(dialog);
            } else {
                UpdateStrengthDisplay(dialog);
            }

            return TRUE;
        }

        case WM_TIMER: {
            if (wParam == kTimerClipboardClear && state != nullptr) {
                ::KillTimer(dialog, kTimerClipboardClear);
                if (!state->lastCopiedPassword.empty() && ::OpenClipboard(dialog)) {
                    HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
                    if (hData != nullptr) {
                        const wchar_t* pText = static_cast<const wchar_t*>(::GlobalLock(hData));
                        if (pText != nullptr && state->lastCopiedPassword == pText) {
                            ::EmptyClipboard();
                        }
                        ::GlobalUnlock(hData);
                    }
                    ::CloseClipboard();
                    state->lastCopiedPassword.clear();
                }
            }
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if (id == kIdManualPassword && code == EN_CHANGE) {
                UpdateStrengthDisplay(dialog);
                return TRUE;
            }

            if (id == kIdRevealPassword) {
                ToggleReveal(dialog, kIdManualPassword, kIdRevealPassword, state->revealPassword);
                return TRUE;
            }
            if (id == kIdRevealSecret) {
                ToggleReveal(dialog, kIdSecretKey, kIdRevealSecret, state->revealSecret);
                return TRUE;
            }
            if (id == kIdRevealVerify) {
                ToggleReveal(dialog, kIdVerifyPassword, kIdRevealVerify, state->revealVerify);
                return TRUE;
            }

            if (id == kIdGenerateRandom) {
                GeneratePasswordInDialog(dialog);
                return TRUE;
            }

            if (id == kIdCopyPassword) {
                const std::wstring pwd = GetText(dialog, kIdManualPassword);
                CopyTextToClipboard(dialog, pwd, state->settings->password.autoClearClipboard, state);
                SetText(dialog, kIdStatusBox, L"Password copied. Clipboard clears in 30 seconds.");
                return TRUE;
            }

            if (id == kIdInsertPassword) {
                const std::wstring pwd = GetText(dialog, kIdManualPassword);
                InsertIntoCurrentScintilla(pwd);
                SetText(dialog, kIdStatusBox, L"Password inserted at caret.");
                return TRUE;
            }

            if (id >= kIdComplexityBasic && id <= kIdComplexityElite) {
                const Complexity comp = static_cast<Complexity>(id - kIdComplexityBasic);
                ApplyComplexityProfileToUi(dialog, comp);
                GeneratePasswordInDialog(dialog);
                return TRUE;
            }

            if (id >= kIdPresetCustom && id <= kIdPresetMagento) {
                const Preset pr = static_cast<Preset>(id - kIdPresetCustom);
                ApplyPresetToUi(dialog, pr);
                return TRUE;
            }

            if (id == kIdAlgorithmCombo && code == CBN_SELCHANGE) {
                HWND hCombo = ::GetDlgItem(dialog, kIdAlgorithmCombo);
                const int sel = static_cast<int>(::SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
                const Algorithm alg = static_cast<Algorithm>(sel >= 0 ? sel : 0);
                SetText(dialog, kIdAlgorithmDesc, AlgorithmDescription(alg));
                UpdateParameterControls(dialog, alg, state->settings->password);
                return TRUE;
            }

            if (id == kIdGenerateHash) {
                GenerateHashInDialog(dialog, state);
                return TRUE;
            }

            if (id == kIdCopyHash) {
                const std::wstring hash = GetText(dialog, kIdHashOutput);
                CopyTextToClipboard(dialog, hash, false, state);
                SetText(dialog, kIdStatusBox, L"Hash copied to clipboard.");
                return TRUE;
            }

            if (id == kIdInsertHash) {
                const std::wstring hash = GetText(dialog, kIdHashOutput);
                InsertIntoCurrentScintilla(hash);
                SetText(dialog, kIdStatusBox, L"Hash inserted at caret.");
                return TRUE;
            }

            if (id == kIdVerifyBtn) {
                VerifyInDialog(dialog);
                return TRUE;
            }

            if (id == kIdGeneratePassphrase) {
                const int words = ::GetDlgItemInt(dialog, kIdPassphraseWords, nullptr, FALSE);
                const std::wstring sep = GetText(dialog, kIdPassphraseSep);
                const wchar_t sepCh = sep.empty() ? L'-' : sep[0];
                const PasswordResult res = PasswordService::Instance().GeneratePassphrase(words, sepCh);
                if (res.ok) {
                    SetText(dialog, kIdPassphraseOutput, res.value);
                    SetText(dialog, kIdStatusBox, L"Passphrase generated locally.");
                }
                return TRUE;
            }

            if (id == kIdCopyPassphrase) {
                const std::wstring pp = GetText(dialog, kIdPassphraseOutput);
                CopyTextToClipboard(dialog, pp, state->settings->password.autoClearClipboard, state);
                SetText(dialog, kIdStatusBox, L"Passphrase copied to clipboard.");
                return TRUE;
            }

            if (id == kIdInsertPassphrase) {
                const std::wstring pp = GetText(dialog, kIdPassphraseOutput);
                InsertIntoCurrentScintilla(pp);
                SetText(dialog, kIdStatusBox, L"Passphrase inserted at caret.");
                return TRUE;
            }

            if (id == kIdClearAll) {
                SetText(dialog, kIdManualPassword, L"");
                SetText(dialog, kIdSecretKey, L"");
                SetText(dialog, kIdHashOutput, L"");
                SetText(dialog, kIdHashInfo, L"—");
                SetText(dialog, kIdVerifyHash, L"");
                SetText(dialog, kIdVerifyPassword, L"");
                SetText(dialog, kIdVerifyResult, L"");
                SetText(dialog, kIdPassphraseOutput, L"");
                UpdateStrengthDisplay(dialog);
                SetText(dialog, kIdStatusBox, L"Cleared password, secret key, hash, and verification fields.");
                return TRUE;
            }

            if (id == kIdResetSettings) {
                Plugin::PasswordSettings defs;
                SyncUiFromSettings(dialog, defs);
                GeneratePasswordInDialog(dialog);
                SetText(dialog, kIdStatusBox, L"Settings reset to defaults.");
                return TRUE;
            }

            if (id == IDCANCEL) {
                // Save settings to INI before exiting
                if (state != nullptr && state->settings != nullptr) {
                    Plugin::PasswordSettings& cfg = state->settings->password;
                    cfg.length = ::GetDlgItemInt(dialog, kIdLengthNumber, nullptr, FALSE);
                    cfg.useLower = ::IsDlgButtonChecked(dialog, kIdIncludeLower) == BST_CHECKED;
                    cfg.useUpper = ::IsDlgButtonChecked(dialog, kIdIncludeUpper) == BST_CHECKED;
                    cfg.useDigits = ::IsDlgButtonChecked(dialog, kIdIncludeDigits) == BST_CHECKED;
                    cfg.useSymbols = ::IsDlgButtonChecked(dialog, kIdIncludeSymbols) == BST_CHECKED;
                    cfg.avoidAmbiguous = ::IsDlgButtonChecked(dialog, kIdExcludeAmbiguous) == BST_CHECKED;
                    cfg.excludeChars = GetText(dialog, kIdExcludeChars);

                    HWND hCombo = ::GetDlgItem(dialog, kIdAlgorithmCombo);
                    const int sel = static_cast<int>(::SendMessageW(hCombo, CB_GETCURSEL, 0, 0));
                    const Algorithm alg = static_cast<Algorithm>(sel >= 0 ? sel : 0);
                    cfg.algorithm = AlgorithmName(alg);

                    cfg.rememberSecret = ::IsDlgButtonChecked(dialog, kIdRememberSecret) == BST_CHECKED;
                    if (cfg.rememberSecret) {
                        cfg.secretKey = GetText(dialog, kIdSecretKey);
                    } else {
                        cfg.secretKey.clear();
                    }

                    const int words = ::GetDlgItemInt(dialog, kIdPassphraseWords, nullptr, FALSE);
                    if (words >= 2) cfg.words = words;
                    const std::wstring sep = GetText(dialog, kIdPassphraseSep);
                    if (!sep.empty()) cfg.separator = sep;

                    state->settings->Save();
                }
                ::EndDialog(dialog, IDCANCEL);
                return TRUE;
            }

            break;
        }

        case WM_CLOSE:
            ::SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            return TRUE;

        default:
            break;
    }

    return FALSE;
}

}  // namespace

void ShowPasswordDialog(HWND owner, Plugin::Settings& settings) {
    INITCOMMONCONTROLSEX icex{};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&icex);

    TemplateBuilder builder(490, 350, L"ArgpiSuite — Password & Hash Generator");

    // Left Column: Password Generator
    builder.AddGroupbox(8, 6, 234, 316, L"Password Generator");
    builder.AddStatic(14, 18, 60, 10, L"Password:");
    builder.AddEdit(14, 28, 170, 13, kIdManualPassword, false);
    builder.AddButton(188, 27, 48, 15, kIdRevealPassword, L"Show");

    builder.AddButton(14, 46, 74, 14, kIdGenerateRandom, L"Generate Random");
    builder.AddButton(91, 46, 68, 14, kIdCopyPassword, L"Copy Password");
    builder.AddButton(162, 46, 74, 14, kIdInsertPassword, L"Insert at Caret");

    builder.AddStatic(14, 64, 60, 10, L"Complexity:");
    builder.AddRadio(14, 74, 52, 11, kIdComplexityBasic, L"Basic (12)", true);
    builder.AddRadio(68, 74, 58, 11, kIdComplexityBalanced, L"Balanced (16)");
    builder.AddRadio(128, 74, 54, 11, kIdComplexityStrong, L"Strong (20)");
    builder.AddRadio(184, 74, 52, 11, kIdComplexityElite, L"Elite (32)");

    builder.AddStatic(14, 89, 30, 10, L"Length:");
    builder.AddEdit(46, 87, 34, 12, kIdLengthNumber);
    builder.AddStatic(84, 89, 150, 10, L"(Min 4, up to 4096 chars)");

    builder.AddStatic(14, 103, 80, 10, L"Character Sets:");
    builder.AddCheckbox(14, 114, 106, 11, kIdIncludeLower, L"Lowercase (a-z)");
    builder.AddCheckbox(124, 114, 106, 11, kIdIncludeUpper, L"Uppercase (A-Z)");
    builder.AddCheckbox(14, 126, 106, 11, kIdIncludeDigits, L"Numbers (0-9)");
    builder.AddCheckbox(124, 126, 106, 11, kIdIncludeSymbols, L"Symbols (!@#$...)");
    builder.AddCheckbox(14, 138, 218, 11, kIdExcludeAmbiguous, L"Exclude ambiguous (0 O 1 l I | ' \" `)");

    builder.AddStatic(14, 152, 120, 10, L"Exclude specific characters:");
    builder.AddEdit(14, 162, 222, 12, kIdExcludeChars);

    builder.AddStatic(14, 178, 80, 10, L"Quick Presets:");
    builder.AddButton(14, 188, 42, 13, kIdPresetCustom, L"Custom");
    builder.AddButton(58, 188, 42, 13, kIdPresetJoomla, L"Joomla");
    builder.AddButton(102, 188, 46, 13, kIdPresetWordpress, L"WordPress");
    builder.AddButton(150, 188, 40, 13, kIdPresetDrupal, L"Drupal");
    builder.AddButton(192, 188, 44, 13, kIdPresetMagento, L"Magento");

    builder.AddStatic(14, 206, 38, 10, L"Strength:");
    builder.AddStatic(54, 206, 180, 10, L"—", kIdStrengthLabel);

    builder.AddGroupbox(14, 220, 222, 54, L"Passphrase Generator (Diceware)");
    builder.AddStatic(20, 233, 26, 10, L"Words:");
    builder.AddEdit(48, 231, 22, 12, kIdPassphraseWords);
    builder.AddStatic(74, 233, 18, 10, L"Sep:");
    builder.AddEdit(94, 231, 16, 12, kIdPassphraseSep);
    builder.AddButton(114, 230, 84, 14, kIdGeneratePassphrase, L"Generate Passphrase");

    builder.AddEdit(20, 248, 140, 12, kIdPassphraseOutput);
    builder.AddButton(164, 247, 34, 14, kIdInsertPassphrase, L"Insert");
    builder.AddButton(200, 247, 32, 14, kIdCopyPassphrase, L"Copy");

    builder.AddButton(14, 282, 56, 14, kIdClearAll, L"Clear All");
    builder.AddButton(74, 282, 70, 14, kIdResetSettings, L"Reset Settings");

    // Right Column: Hash Generator & Verifier
    builder.AddGroupbox(248, 6, 234, 316, L"Hash Generator & Verifier");
    builder.AddStatic(254, 18, 50, 10, L"Algorithm:");
    builder.AddCombobox(254, 28, 166, 140, kIdAlgorithmCombo);
    builder.AddStatic(254, 44, 222, 22, L"", kIdAlgorithmDesc);

    builder.AddStatic(254, 68, 140, 10, L"Secret Key (for secret-key modes):");
    builder.AddEdit(254, 78, 172, 13, kIdSecretKey, true);
    builder.AddButton(430, 77, 46, 15, kIdRevealSecret, L"Show");
    builder.AddCheckbox(254, 94, 222, 11, kIdRememberSecret, L"Remember secret key on this device");

    builder.AddStatic(254, 108, 100, 10, L"", kIdParam1Label);
    builder.AddEdit(358, 106, 118, 12, kIdParam1Edit);
    builder.AddStatic(254, 122, 100, 10, L"", kIdParam2Label);
    builder.AddEdit(358, 120, 118, 12, kIdParam2Edit);

    builder.AddButton(254, 137, 68, 14, kIdGenerateHash, L"Generate Hash", true);
    builder.AddButton(326, 137, 56, 14, kIdCopyHash, L"Copy Hash");
    builder.AddButton(386, 137, 72, 14, kIdInsertHash, L"Insert at Caret");

    builder.AddStatic(254, 154, 55, 10, L"Hash Output:");
    builder.AddStatic(310, 154, 166, 10, L"—", kIdHashInfo);
    builder.AddEdit(254, 165, 222, 34, kIdHashOutput, false, true, true);

    builder.AddGroupbox(254, 204, 222, 92, L"Verify a password against a hash");
    builder.AddStatic(260, 215, 150, 10, L"Hash ($2y$... or $argon2id$...):");
    builder.AddEdit(260, 225, 210, 12, kIdVerifyHash);
    builder.AddStatic(260, 240, 90, 10, L"Password to verify:");
    builder.AddEdit(260, 250, 162, 12, kIdVerifyPassword, true);
    builder.AddButton(426, 249, 44, 14, kIdRevealVerify, L"Show");
    builder.AddButton(260, 266, 44, 13, kIdVerifyBtn, L"Verify");
    builder.AddStatic(308, 267, 162, 22, L"", kIdVerifyResult);

    builder.AddStatic(254, 301, 222, 18, L"", kIdStatusBox);

    // Bottom Bar
    builder.AddStatic(10, 329, 390, 10, L"Settings are remembered in ArgpiSuite.ini. Passwords and hashes are never saved.");
    builder.AddButton(432, 326, 50, 14, IDCANCEL, L"Close");

    PasswordDialogState state;
    state.settings = &settings;

    std::vector<unsigned char> data = builder.Build();
    ::DialogBoxIndirectParamW(
        static_cast<HINSTANCE>(PluginModule()), reinterpret_cast<LPCDLGTEMPLATEW>(data.data()),
        owner, &PasswordDialogProc, reinterpret_cast<LPARAM>(&state));
}

void ShowPassphraseDialog(HWND owner, Plugin::Settings& settings) {
    ShowPasswordDialog(owner, settings);
}

}  // namespace ArgpiSuite::Password

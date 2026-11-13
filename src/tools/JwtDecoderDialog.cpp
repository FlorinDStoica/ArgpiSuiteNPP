#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "JwtDecoderDialog.h"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "JwtDecoderService.h"
#include "PluginDefinition.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

constexpr WORD kIdTokenEdit        = 44001;
constexpr WORD kIdHeaderEdit       = 44002;
constexpr WORD kIdPayloadEdit      = 44003;
constexpr WORD kIdClaimsEdit       = 44004;
constexpr WORD kIdSecretEdit       = 44005;
constexpr WORD kIdVerifyStatus     = 44006;

constexpr WORD kIdCopyHeaderBtn    = 44010;
constexpr WORD kIdCopyPayloadBtn   = 44011;
constexpr WORD kIdInsertBtn        = 44012;

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

    void AddButton(short x, short y, short cx, short cy, WORD id, const wchar_t* text,
                   bool defBtn = false) {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
        if (defBtn) {
            style |= BS_DEFPUSHBUTTON;
        }
        AddItem(style, x, y, cx, cy, 0x0080, id, text);
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

struct JwtDialogContext {
    std::string initialToken;
    ToolInsertCallback onInsert;
    JwtDecodeResult lastDecoded;
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

void UpdateVerification(HWND hwnd, JwtDialogContext* ctx) {
    if (ctx == nullptr || !ctx->lastDecoded.ok) {
        ::SetDlgItemTextW(hwnd, kIdVerifyStatus, L"");
        return;
    }
    const std::string secret = Plugin::ToUtf8(GetEditText(hwnd, kIdSecretEdit));
    if (secret.empty()) {
        ::SetDlgItemTextW(hwnd, kIdVerifyStatus, L"Enter secret to verify HS256/384/512");
        return;
    }
    const JwtVerifyResult vr = JwtDecoderService::Instance().VerifyHmac(ctx->lastDecoded, secret);
    ::SetDlgItemTextW(hwnd, kIdVerifyStatus, vr.message.c_str());
}

void DecodeAndRefresh(HWND hwnd, JwtDialogContext* ctx) {
    if (ctx == nullptr) {
        return;
    }
    const std::string token = Plugin::ToUtf8(GetEditText(hwnd, kIdTokenEdit));
    ctx->lastDecoded = JwtDecoderService::Instance().Decode(token);
    if (!ctx->lastDecoded.ok) {
        ::SetDlgItemTextW(hwnd, kIdHeaderEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdPayloadEdit, L"");
        ::SetDlgItemTextW(hwnd, kIdClaimsEdit, ctx->lastDecoded.message.c_str());
        ::SetDlgItemTextW(hwnd, kIdVerifyStatus, L"");
        return;
    }
    ::SetDlgItemTextW(hwnd, kIdHeaderEdit, Plugin::ToWide(ctx->lastDecoded.headerJson).c_str());
    ::SetDlgItemTextW(hwnd, kIdPayloadEdit, Plugin::ToWide(ctx->lastDecoded.payloadJson).c_str());
    ::SetDlgItemTextW(hwnd, kIdClaimsEdit, ctx->lastDecoded.claimsSummary.c_str());
    UpdateVerification(hwnd, ctx);
}

INT_PTR CALLBACK JwtDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JwtDialogContext* ctx =
        reinterpret_cast<JwtDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<JwtDialogContext*>(lParam);
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

            if (!ctx->initialToken.empty()) {
                ::SetDlgItemTextW(hwnd, kIdTokenEdit, Plugin::ToWide(ctx->initialToken).c_str());
            }
            DecodeAndRefresh(hwnd, ctx);
            return TRUE;
        }

        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            const WORD code = HIWORD(wParam);

            if (id == kIdTokenEdit && code == EN_CHANGE) {
                DecodeAndRefresh(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdSecretEdit && code == EN_CHANGE) {
                UpdateVerification(hwnd, ctx);
                return TRUE;
            }

            if (id == kIdCopyHeaderBtn && code == BN_CLICKED) {
                if (ctx->lastDecoded.ok) {
                    CopyUtf8ToClipboard(hwnd, ctx->lastDecoded.headerJson);
                }
                return TRUE;
            }

            if (id == kIdCopyPayloadBtn && code == BN_CLICKED) {
                if (ctx->lastDecoded.ok) {
                    CopyUtf8ToClipboard(hwnd, ctx->lastDecoded.payloadJson);
                }
                return TRUE;
            }

            if (id == kIdInsertBtn && code == BN_CLICKED) {
                if (!ctx->lastDecoded.ok) {
                    return TRUE;
                }
                const std::string combined = "{\r\n  \"header\": " + ctx->lastDecoded.headerJson +
                                             ",\r\n  \"payload\": " + ctx->lastDecoded.payloadJson +
                                             "\r\n}";
                if (ctx->onInsert && ctx->onInsert(combined)) {
                    ::EndDialog(hwnd, IDOK);
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

void ShowJwtDecoderDialog(HWND parent, const std::string& initialTokenUtf8,
                          ToolInsertCallback onInsert) {
    TemplateBuilder builder(360, 310, L"JWT (JSON Web Token) Decoder");

    // Token Input
    builder.AddGroupbox(10, 8, 340, 48, L"Encoded JWT (eyJ...)");
    builder.AddEdit(18, 20, 324, 30, kIdTokenEdit,
                    ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);

    // Header & Payload side by side
    builder.AddGroupbox(10, 62, 130, 128, L"Header (JSON)");
    builder.AddEdit(16, 74, 118, 110, kIdHeaderEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);

    builder.AddGroupbox(146, 62, 204, 128, L"Payload (JSON)");
    builder.AddEdit(152, 74, 192, 110, kIdPayloadEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL);

    // Claims Summary
    builder.AddGroupbox(10, 196, 340, 52, L"Claims && Expiration Summary");
    builder.AddEdit(18, 208, 324, 34, kIdClaimsEdit,
                    ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);

    // HMAC Verification
    builder.AddGroupbox(10, 254, 340, 30, L"Verify Signature (HS256 / HS384 / HS512)");
    builder.AddStatic(18, 267, 26, 10, L"Secret:");
    builder.AddEdit(46, 265, 160, 13, kIdSecretEdit, ES_AUTOHSCROLL);
    builder.AddStatic(212, 267, 132, 10, L"", kIdVerifyStatus);

    // Bottom Buttons
    builder.AddButton(10, 290, 64, 15, kIdCopyHeaderBtn, L"Copy Header");
    builder.AddButton(78, 290, 68, 15, kIdCopyPayloadBtn, L"Copy Payload", true);
    builder.AddButton(150, 290, 92, 15, kIdInsertBtn, L"Insert JSON at Cursor");
    builder.AddButton(302, 290, 48, 15, IDCANCEL, L"Close");

    const std::vector<unsigned char> dlgData = builder.Build();
    JwtDialogContext ctx;
    ctx.initialToken = initialTokenUtf8;
    ctx.onInsert = std::move(onInsert);

    ::DialogBoxIndirectParamW(static_cast<HINSTANCE>(PluginModule()),
                             reinterpret_cast<const DLGTEMPLATE*>(dlgData.data()),
                             parent, JwtDlgProc, reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace ArgpiSuite::Tools


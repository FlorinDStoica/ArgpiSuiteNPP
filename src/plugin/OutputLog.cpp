#include "OutputLog.h"

#include <windows.h>

#include <string>

namespace ArgpiSuite::Plugin {
namespace {

constexpr wchar_t kLogClass[] = L"ArgpiSuiteOutputLog";
constexpr int kEditId = 30200;
constexpr int kCopyId = 30201;
constexpr int kClearId = 30202;

std::wstring Timestamp() {
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    wchar_t buffer[16] = {};
    ::swprintf_s(buffer, L"%02u:%02u:%02u ", time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

}  // namespace

OutputLog& OutputLog::Instance() {
    static OutputLog instance;
    return instance;
}

void OutputLog::Create(HWND owner) {
    if (window_ != nullptr) {
        return;
    }
    owner_ = owner;

    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &OutputLog::WindowProc;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kLogClass;
        ::RegisterClassExW(&windowClass);
        classRegistered = true;
    }

    RECT ownerRect{};
    if (owner != nullptr) {
        ::GetWindowRect(owner, &ownerRect);
    }
    const int width = 760;
    const int height = 420;
    const int x = ownerRect.left + 80;
    const int y = ownerRect.top + 80;

    window_ = ::CreateWindowExW(0, kLogClass, L"ArgpiSuite — Format Output Log",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height, owner,
                                nullptr, ::GetModuleHandleW(nullptr), this);
    if (window_ == nullptr) {
        return;
    }

    edit_ = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                  ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                              0, 0, 100, 100, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
                              ::GetModuleHandleW(nullptr), nullptr);
    copyButton_ = ::CreateWindowExW(0, L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0,
                                    0, 80, 26, window_,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyId)),
                                    ::GetModuleHandleW(nullptr), nullptr);
    clearButton_ = ::CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                     0, 0, 80, 26, window_,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearId)),
                                     ::GetModuleHandleW(nullptr), nullptr);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        font_ = ::CreateFontIndirectW(&ncm.lfMessageFont);
    }
    if (font_ == nullptr) {
        font_ = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    }
    if (edit_ != nullptr) {
        ::SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        if (!pending_.empty()) {
            AppendRaw(pending_);
            pending_.clear();
        }
    }
    Layout();
}

void OutputLog::Layout() {
    if (window_ == nullptr) {
        return;
    }
    RECT client{};
    ::GetClientRect(window_, &client);
    const int margin = 8;
    const int buttonHeight = 26;
    const int buttonWidth = 84;
    const int editHeight = client.bottom - buttonHeight - margin * 3;
    const int editWidth = client.right - margin * 2;
    if (edit_ != nullptr) {
        ::MoveWindow(edit_, margin, margin, editWidth, editHeight, TRUE);
    }
    if (clearButton_ != nullptr) {
        ::MoveWindow(clearButton_, client.right - margin - buttonWidth,
                     client.bottom - margin - buttonHeight, buttonWidth, buttonHeight, TRUE);
    }
    if (copyButton_ != nullptr) {
        ::MoveWindow(copyButton_, client.right - margin * 2 - buttonWidth * 2,
                     client.bottom - margin - buttonHeight, buttonWidth, buttonHeight, TRUE);
    }
}

void OutputLog::AppendRaw(const std::wstring& message) {
    if (edit_ == nullptr) {
        pending_.append(message);
        return;
    }
    const int length = ::GetWindowTextLengthW(edit_);
    ::SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    ::SendMessageW(edit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(message.c_str()));
}

void OutputLog::AppendLine(const std::wstring& message) {
    std::wstring line = Timestamp();
    line.append(message);
    line.append(L"\r\n");
    if (edit_ == nullptr) {
        pending_.append(line);
        return;
    }
    AppendRaw(line);
    ::SendMessageW(edit_, EM_SCROLLCARET, 0, 0);
}

void OutputLog::Clear() {
    pending_.clear();
    if (edit_ != nullptr) {
        ::SetWindowTextW(edit_, L"");
    }
}

void OutputLog::Show(HWND owner) {
    Create(owner);
    if (window_ == nullptr) {
        return;
    }
    ::ShowWindow(window_, SW_SHOWNORMAL);
    ::SetForegroundWindow(window_);
}

void OutputLog::Destroy() {
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
        window_ = nullptr;
        edit_ = nullptr;
        copyButton_ = nullptr;
        clearButton_ = nullptr;
    }
    font_ = nullptr;
    owner_ = nullptr;
    pending_.clear();
}

LRESULT CALLBACK OutputLog::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    OutputLog* self = reinterpret_cast<OutputLog*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<OutputLog*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self != nullptr) {
        return self->HandleMessage(window, message, wParam, lParam);
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OutputLog::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kCopyId) {
                ::SendMessageW(edit_, WM_COPY, 0, 0);
                return 0;
            }
            if (LOWORD(wParam) == kClearId) {
                Clear();
                return 0;
            }
            break;
        case WM_CLOSE:
            ::ShowWindow(window_, SW_HIDE);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 420;
            info->ptMinTrackSize.y = 240;
            return 0;
        }
        default:
            break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace ArgpiSuite::Plugin

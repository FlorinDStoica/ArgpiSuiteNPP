#include "NotImplemented.h"

#include <windows.h>

#include <string>

namespace ArgpiSuite::Plugin {
namespace {

constexpr wchar_t kToastClass[] = L"ArgpiSuiteNotImplementedToast";
constexpr UINT_PTR kToastTimer = 1;
constexpr int kToastWidth = 460;
constexpr int kToastHeight = 64;

LRESULT CALLBACK ToastProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TIMER:
            ::KillTimer(window, kToastTimer);
            ::DestroyWindow(window);
            return 0;
        case WM_LBUTTONUP:
            ::DestroyWindow(window);
            return 0;
        case WM_NCHITTEST:
            return HTCLIENT;
        default:
            break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

namespace {

void ShowToast(HWND owner, const wchar_t* title, const std::wstring& text) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &ToastProc;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_INFOBK + 1);
        windowClass.lpszClassName = kToastClass;
        ::RegisterClassExW(&windowClass);
        registered = true;
    }

    RECT ownerRect{};
    if (owner != nullptr && ::GetWindowRect(owner, &ownerRect)) {
        // place it just above the status bar, right aligned
        const int x = ownerRect.right - kToastWidth - 32;
        const int y = ownerRect.bottom - kToastHeight - 96;
        ownerRect = {x, y, x + kToastWidth, y + kToastHeight};
    } else {
        ownerRect = {200, 200, 200 + kToastWidth, 200 + kToastHeight};
    }

    HWND window = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kToastClass, title,
        WS_POPUP | WS_BORDER, ownerRect.left, ownerRect.top, kToastWidth, kToastHeight, owner,
        nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        return;
    }

    HWND labelWindow = ::CreateWindowExW(
        0, L"STATIC", text.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX | SS_CENTERIMAGE, 8, 4,
        kToastWidth - 24, kToastHeight - 12, window, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (labelWindow != nullptr) {
        ::SendMessageW(labelWindow, WM_SETFONT,
                       reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }

    ::ShowWindow(window, SW_SHOWNOACTIVATE);
    ::SetTimer(window, kToastTimer, 3500, nullptr);
}

}  // namespace

void ShowNotImplemented(HWND owner, const wchar_t* label) {
    std::wstring text = label != nullptr ? label : L"This feature";
    text += L"\nThis feature is planned but not implemented yet.";
    ShowToast(owner, L"ArgpiSuite — not implemented", text);
}

void ShowFormatUnsupported(HWND owner, const wchar_t* language) {
    std::wstring text = language != nullptr ? language : L"This language";
    text += L" formatting is not supported yet.\nSupported: JSON, JSON5 and JSONC.";
    ShowToast(owner, L"ArgpiSuite — unsupported language", text);
}

}  // namespace ArgpiSuite::Plugin

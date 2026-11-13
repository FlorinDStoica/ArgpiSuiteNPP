// ArgpiSuite plugin — modeless output log window (no .rc resources).
#pragma once

#include <windows.h>

#include <string>

namespace ArgpiSuite::Plugin {

/// A small modeless log window with Copy / Clear. Created lazily on the UI
/// thread; worker threads must marshal their messages through PostMessage.
class OutputLog {
public:
    static OutputLog& Instance();

    void AppendLine(const std::wstring& message);
    void AppendRaw(const std::wstring& message);
    void Clear();
    void Show(HWND owner);
    void Destroy();

    [[nodiscard]] bool Exists() const { return window_ != nullptr; }

private:
    OutputLog() = default;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Create(HWND owner);
    void Layout();

    HWND window_ = nullptr;
    HWND edit_ = nullptr;
    HWND copyButton_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND owner_ = nullptr;
    HFONT font_ = nullptr;
    std::wstring pending_;  // messages logged before the window exists
};

}  // namespace ArgpiSuite::Plugin

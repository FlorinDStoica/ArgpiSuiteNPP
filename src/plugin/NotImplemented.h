// ArgpiSuite plugin — shared "not implemented yet" toast.
#pragma once

#include <windows.h>

namespace ArgpiSuite::Plugin {

/// Shows a small, non-modal, non-activating toast near the Notepad++ window.
/// Used by every Encrypt / Decrypt and Generate Password command.
void ShowNotImplemented(HWND owner, const wchar_t* label);

/// Explains that the detected language has no native formatter in this build.
/// The list of supported languages matches `Format::IsLanguageImplemented`.
void ShowFormatUnsupported(HWND owner, const wchar_t* language);

}  // namespace ArgpiSuite::Plugin

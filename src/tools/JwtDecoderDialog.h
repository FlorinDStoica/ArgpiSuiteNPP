// ArgpiSuite JWT (JSON Web Token) Decoder Dialog.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

namespace ArgpiSuite::Tools {

using ToolInsertCallback = std::function<bool(const std::string& utf8Text)>;

/// Shows the modal JWT Decoder & Verifier dialog.
void ShowJwtDecoderDialog(HWND parent, const std::string& initialTokenUtf8,
                          ToolInsertCallback onInsert);

}  // namespace ArgpiSuite::Tools


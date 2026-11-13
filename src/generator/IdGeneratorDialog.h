// ArgpiSuite UUID / GUID & ULID Bulk & Custom Generator Dialog.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

#include "plugin/Settings.h"

namespace ArgpiSuite::Generator {

/// Callback invoked when the user clicks "Insert at Cursor".
/// Receives the generated UTF-8 text and the number of IDs generated.
/// Returns true if insertion succeeded.
using InsertCallback = std::function<bool(const std::string& utf8Text, int count)>;

/// Shows the modal Bulk / Custom UUID, GUID & ULID Generator dialog.
void ShowIdGeneratorDialog(HWND parent, Plugin::Settings& settings,
                           const std::string& documentEol, InsertCallback onInsert);

}  // namespace ArgpiSuite::Generator


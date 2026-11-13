// ArgpiSuite Hash & Checksum Tool Dialog.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <functional>
#include <string>

#include "HashService.h"
#include "plugin/Settings.h"

namespace ArgpiSuite::Hash {

struct HashDialogInput {
    bool hasSelection = false;
    std::string selectionBytes;
    std::string documentBytes;
    std::wstring activeFilePath;
    bool useOverrideAlgorithm = false;
    HashAlgorithm overrideAlgorithm = HashAlgorithm::Sha256;
};

using HashInsertCallback = std::function<bool(const std::string& utf8Text)>;

/// Shows the modal Hash & Checksum Tool dialog.
void ShowHashDialog(HWND parent, Plugin::Settings& settings, const HashDialogInput& input,
                    HashInsertCallback onInsert);

}  // namespace ArgpiSuite::Hash


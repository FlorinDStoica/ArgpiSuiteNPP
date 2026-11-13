// ArgpiSuite plugin — Scintilla document access with one-undo-step writes.
#pragma once

#include <windows.h>

#include <cstddef>
#include <string>

namespace ArgpiSuite::Plugin {

struct ScintillaDocument {
    std::string raw;    // bytes exactly as stored in the Scintilla buffer
    std::string utf8;   // `raw` converted to UTF-8 (== raw when codepage is UTF-8)
    int codepage = 65001;
    bool readOnly = false;
};

struct CaretSnapshot {
    std::size_t position = 0;
    int firstVisibleLine = 0;
};

/// Reads the whole current document. Returns false with `error` when Scintilla
/// cannot be queried.
bool ReadDocument(HWND scintilla, ScintillaDocument& document, std::wstring& error);

/// Reads the selected text as UTF-8. `start`/`end` are raw Scintilla offsets.
bool ReadSelection(HWND scintilla, std::string& utf8, std::size_t& start, std::size_t& end,
                   std::wstring& error);

CaretSnapshot CaptureCaret(HWND scintilla);

/// Converts UTF-8 text to the document codepage.
std::string EncodeForDocument(std::string_view utf8, int codepage);

/// Writes `newUtf8` into the buffer inside one undo action. Unchanged output is
/// never written. Returns false only on a real failure.
bool ApplyFormattedText(HWND scintilla, const ScintillaDocument& document,
                        std::string_view newUtf8, const CaretSnapshot& caret,
                        std::wstring& error);

/// Replaces only the selected range (used by Format Selection). The selection
/// bytes must be the exact slice `[start, end)` of the document.
bool ApplyFormattedSelection(HWND scintilla, std::size_t start, std::size_t end,
                             std::string_view originalSelectionBytes, std::string_view newUtf8,
                             int codepage, const CaretSnapshot& caret, std::wstring& error);

/// Maps a UTF-8 byte offset to a Scintilla position. Only valid for UTF-8
/// documents; for other codepages the whole document is replaced anyway.
std::size_t Utf8OffsetToRawOffset(std::string_view raw, std::string_view utf8, std::size_t offset);

}  // namespace ArgpiSuite::Plugin

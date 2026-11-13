#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "TextApply.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "Scintilla.h"

namespace ArgpiSuite::Plugin {
namespace {

constexpr int kUtf8Codepage = 65001;

std::intptr_t SendSci(HWND scintilla, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    return static_cast<std::intptr_t>(::SendMessageW(scintilla, message, wParam, lParam));
}

/// Scintilla stores bytes; for legacy codepages we convert through UTF-16.
std::string ConvertToUtf8(std::string_view raw, int codepage) {
    if (codepage == kUtf8Codepage || codepage == 0) {
        return std::string(raw);
    }
    const int wideLength = ::MultiByteToWideChar(codepage, 0, raw.data(),
                                                 static_cast<int>(raw.size()), nullptr, 0);
    if (wideLength <= 0) {
        return std::string(raw);
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    ::MultiByteToWideChar(codepage, 0, raw.data(), static_cast<int>(raw.size()), wide.data(),
                          wideLength);
    const int utf8Length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0,
                                                 nullptr, nullptr);
    if (utf8Length <= 0) {
        return std::string(raw);
    }
    std::string utf8(static_cast<std::size_t>(utf8Length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(), utf8Length, nullptr,
                          nullptr);
    return utf8;
}

bool IsUtf8Continuation(char ch) {
    return (static_cast<unsigned char>(ch) & 0xC0) == 0x80;
}

}  // namespace

std::string EncodeForDocument(std::string_view utf8, int codepage) {
    if (codepage == kUtf8Codepage || codepage == 0) {
        return std::string(utf8);
    }
    const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                                 static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLength <= 0) {
        return std::string(utf8);
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                          wideLength);
    const int encodedLength = ::WideCharToMultiByte(codepage, 0, wide.data(), wideLength, nullptr,
                                                    0, nullptr, nullptr);
    if (encodedLength <= 0) {
        return std::string(utf8);
    }
    std::string encoded(static_cast<std::size_t>(encodedLength), '\0');
    ::WideCharToMultiByte(codepage, 0, wide.data(), wideLength, encoded.data(), encodedLength,
                          nullptr, nullptr);
    return encoded;
}

bool ReadDocument(HWND scintilla, ScintillaDocument& document, std::wstring& error) {
    if (scintilla == nullptr) {
        error = L"No active editor view.";
        return false;
    }
    const std::intptr_t length = SendSci(scintilla, SCI_GETTEXTLENGTH);
    if (length < 0) {
        error = L"Scintilla returned a negative document length.";
        return false;
    }
    std::string raw(static_cast<std::size_t>(length) + 1, '\0');
    SendSci(scintilla, SCI_GETTEXT, static_cast<WPARAM>(raw.size()),
            reinterpret_cast<LPARAM>(raw.data()));
    raw.resize(static_cast<std::size_t>(length));

    document.raw = std::move(raw);
    document.codepage = static_cast<int>(SendSci(scintilla, SCI_GETCODEPAGE));
    document.readOnly = SendSci(scintilla, SCI_GETREADONLY) != 0;
    document.utf8 = ConvertToUtf8(document.raw, document.codepage);
    error.clear();
    return true;
}

bool ReadSelection(HWND scintilla, std::string& utf8, std::size_t& start, std::size_t& end,
                   std::wstring& error) {
    if (scintilla == nullptr) {
        error = L"No active editor view.";
        return false;
    }
    const std::intptr_t selectionStart = SendSci(scintilla, SCI_GETSELECTIONSTART);
    const std::intptr_t selectionEnd = SendSci(scintilla, SCI_GETSELECTIONEND);
    if (selectionStart < 0 || selectionEnd < selectionStart) {
        error = L"Invalid selection.";
        return false;
    }
    start = static_cast<std::size_t>(selectionStart);
    end = static_cast<std::size_t>(selectionEnd);

    std::string selected(static_cast<std::size_t>(selectionEnd - selectionStart) + 1, '\0');
    SendSci(scintilla, SCI_GETSELTEXT, static_cast<WPARAM>(selected.size()),
            reinterpret_cast<LPARAM>(selected.data()));
    selected.resize(static_cast<std::size_t>(selectionEnd - selectionStart));

    const int codepage = static_cast<int>(SendSci(scintilla, SCI_GETCODEPAGE));
    utf8 = ConvertToUtf8(selected, codepage);
    error.clear();
    return true;
}

CaretSnapshot CaptureCaret(HWND scintilla) {
    CaretSnapshot snapshot;
    if (scintilla == nullptr) {
        return snapshot;
    }
    snapshot.position = static_cast<std::size_t>(SendSci(scintilla, SCI_GETCURRENTPOS));
    snapshot.firstVisibleLine = static_cast<int>(SendSci(scintilla, SCI_GETFIRSTVISIBLELINE));
    return snapshot;
}

std::size_t Utf8OffsetToRawOffset(std::string_view raw, std::string_view utf8, std::size_t offset) {
    if (raw.size() == utf8.size()) {
        return std::min(offset, raw.size());
    }
    // Legacy codepages: offsets cannot be mapped safely, so callers replace the
    // whole document instead of using this function.
    return std::min(offset, raw.size());
}

bool ApplyFormattedText(HWND scintilla, const ScintillaDocument& document,
                        std::string_view newUtf8, const CaretSnapshot& caret,
                        std::wstring& error) {
    error.clear();
    if (scintilla == nullptr) {
        error = L"No active editor view.";
        return false;
    }
    if (document.readOnly) {
        error = L"The document is read-only.";
        return false;
    }
    if (newUtf8.empty()) {
        return true;  // empty documents are never rewritten
    }

    const std::string newRaw = EncodeForDocument(newUtf8, document.codepage);
    if (newRaw == document.raw) {
        return true;  // unchanged: no edit, no undo entry, no dirty flag
    }

    std::size_t start = 0;
    std::size_t oldEnd = document.raw.size();
    std::size_t newEnd = newRaw.size();

    if (document.codepage == kUtf8Codepage || document.codepage == 0) {
        const std::size_t maxPrefix = std::min(document.raw.size(), newRaw.size());
        while (start < maxPrefix && document.raw[start] == newRaw[start]) {
            ++start;
        }
        while (start > 0 &&
               ((start < document.raw.size() && IsUtf8Continuation(document.raw[start])) ||
                (start < newRaw.size() && IsUtf8Continuation(newRaw[start])))) {
            --start;
        }
        std::size_t suffix = 0;
        while (suffix < document.raw.size() - start && suffix < newRaw.size() - start &&
               document.raw[document.raw.size() - 1 - suffix] ==
                   newRaw[newRaw.size() - 1 - suffix]) {
            ++suffix;
        }
        while (suffix > 0 && (IsUtf8Continuation(document.raw[document.raw.size() - suffix]) ||
                              IsUtf8Continuation(newRaw[newRaw.size() - suffix]))) {
            --suffix;
        }
        oldEnd = document.raw.size() - suffix;
        newEnd = newRaw.size() - suffix;
    }

    const std::string_view replacement(newRaw.data() + start, newEnd - start);

    const std::size_t oldLength = document.raw.size();
    SendSci(scintilla, SCI_BEGINUNDOACTION);
    SendSci(scintilla, SCI_SETTARGETRANGE, static_cast<WPARAM>(start), static_cast<LPARAM>(oldEnd));
    SendSci(scintilla, SCI_REPLACETARGET, static_cast<WPARAM>(replacement.size()),
            reinterpret_cast<LPARAM>(replacement.data()));
    SendSci(scintilla, SCI_ENDUNDOACTION);

    const std::size_t newLength = oldLength - (oldEnd - start) + replacement.size();
    std::size_t newCaret = caret.position;
    if (newCaret >= oldEnd) {
        newCaret = newCaret - (oldEnd - start) + replacement.size();
    } else if (newCaret > start) {
        newCaret = start + replacement.size();
    }
    newCaret = std::min(newCaret, newLength);

    SendSci(scintilla, SCI_GOTOPOS, static_cast<WPARAM>(newCaret));
    if (caret.firstVisibleLine > 0) {
        SendSci(scintilla, SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(caret.firstVisibleLine));
    }
    return true;
}

bool ApplyFormattedSelection(HWND scintilla, std::size_t start, std::size_t end,
                             std::string_view originalSelectionBytes, std::string_view newUtf8,
                             int codepage, const CaretSnapshot& caret, std::wstring& error) {
    error.clear();
    if (scintilla == nullptr) {
        error = L"No active editor view.";
        return false;
    }
    if (end < start) {
        error = L"Invalid selection.";
        return false;
    }
    const std::string newRaw = EncodeForDocument(newUtf8, codepage);
    const std::string original(originalSelectionBytes);
    if (newRaw == original) {
        return true;
    }

    std::size_t prefix = 0;
    std::size_t suffix = 0;
    if (codepage == kUtf8Codepage || codepage == 0) {
        const std::size_t maxPrefix = std::min(original.size(), newRaw.size());
        while (prefix < maxPrefix && original[prefix] == newRaw[prefix]) {
            ++prefix;
        }
        while (prefix > 0 &&
               ((prefix < original.size() && IsUtf8Continuation(original[prefix])) ||
                (prefix < newRaw.size() && IsUtf8Continuation(newRaw[prefix])))) {
            --prefix;
        }
        while (suffix < original.size() - prefix && suffix < newRaw.size() - prefix &&
               original[original.size() - 1 - suffix] == newRaw[newRaw.size() - 1 - suffix]) {
            ++suffix;
        }
        while (suffix > 0 &&
               (IsUtf8Continuation(original[original.size() - suffix]) ||
                IsUtf8Continuation(newRaw[newRaw.size() - suffix]))) {
            --suffix;
        }
    }

    const std::size_t targetStart = start + prefix;
    const std::size_t targetEnd = end - suffix;
    const std::string_view replacement(newRaw.data() + prefix, newRaw.size() - prefix - suffix);

    const std::size_t oldLength = SendSci(scintilla, SCI_GETLENGTH) > 0
                                      ? static_cast<std::size_t>(SendSci(scintilla, SCI_GETLENGTH))
                                      : end;
    SendSci(scintilla, SCI_BEGINUNDOACTION);
    SendSci(scintilla, SCI_SETTARGETRANGE, static_cast<WPARAM>(targetStart),
            static_cast<LPARAM>(targetEnd));
    SendSci(scintilla, SCI_REPLACETARGET, static_cast<WPARAM>(replacement.size()),
            reinterpret_cast<LPARAM>(replacement.data()));
    SendSci(scintilla, SCI_ENDUNDOACTION);

    const std::size_t delta =
        replacement.size() - (targetEnd - targetStart);
    std::size_t newCaret = caret.position;
    if (newCaret >= targetEnd) {
        newCaret += delta;
    } else if (newCaret > targetStart) {
        newCaret = targetStart + replacement.size();
    }
    newCaret = std::min(newCaret, oldLength + delta);

    SendSci(scintilla, SCI_GOTOPOS, static_cast<WPARAM>(newCaret));
    if (caret.firstVisibleLine > 0) {
        SendSci(scintilla, SCI_SETFIRSTVISIBLELINE, static_cast<WPARAM>(caret.firstVisibleLine));
    }
    return true;
}

}  // namespace ArgpiSuite::Plugin

// ArgpiSuite native formatter — UTF-8 helpers and terminal display width.
// Zero Win32 / Notepad++ dependencies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ArgpiSuite::Format {

/// Decodes one UTF-8 code point starting at `index`.
/// Advances `index` on success and returns true. Returns false on malformed input.
bool Utf8Decode(std::string_view text, std::size_t& index, char32_t& codePoint);

/// Appends `codePoint` as UTF-8 to `out`.
void Utf8Append(std::string& out, char32_t codePoint);

/// Returns the number of bytes of the code point starting at `index` (at least 1).
std::size_t Utf8CodePointLength(std::string_view text, std::size_t index);

/// Rounds `index` down to the nearest code-point boundary.
std::size_t Utf8AlignDown(std::string_view text, std::size_t index);

/// Rounds `index` up to the nearest code-point boundary.
std::size_t Utf8AlignUp(std::string_view text, std::size_t index);

/// Number of code points in `text` (malformed bytes count as one).
std::size_t Utf8Length(std::string_view text);

/// Converts a UTF-8 byte offset into a 1-based line/column pair.
void Utf8LineColumn(std::string_view text, std::size_t offset, int& line, int& column);

/// Terminal columns used by `text`: wide East-Asian code points count 2,
/// combining marks and zero-width characters count 0, everything else 1.
int DisplayWidth(std::string_view text);

/// True for code points that must not start a line (combining marks, ZWJ, ...).
bool IsCombiningCodePoint(char32_t codePoint);

/// True for East-Asian wide / fullwidth code points and emoji.
bool IsWideCodePoint(char32_t codePoint);

}  // namespace ArgpiSuite::Format

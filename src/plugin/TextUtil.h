// ArgpiSuite plugin — UTF-8 / UTF-16 helpers shared by the Win32 layer.
// This file is the only place that converts between the two encodings.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Plugin {

/// Converts UTF-16 to UTF-8. Invalid surrogates become U+FFFD.
std::string ToUtf8(std::wstring_view text);

/// Converts UTF-8 to UTF-16. Invalid sequences become U+FFFD.
std::wstring ToWide(std::string_view text);

std::wstring ToLower(std::wstring_view text);

/// Case-insensitive comparison using the invariant locale (ASCII folding plus
/// `CompareStringOrdinal`, so Turkish dotless i cannot surprise us).
bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right);

/// Removes leading and trailing whitespace (space, tab, CR, LF).
std::wstring Trim(std::wstring_view text);

/// Splits a comma separated list and trims each element. Empty entries are
/// dropped.
std::vector<std::wstring> SplitList(std::wstring_view text);

}  // namespace ArgpiSuite::Plugin

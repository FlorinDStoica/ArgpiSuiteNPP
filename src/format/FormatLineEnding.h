// ArgpiSuite native formatter — line ending utilities.
#pragma once

#include <string>
#include <string_view>

namespace ArgpiSuite::Format {

enum class Eol { None, Lf, Crlf, Cr, Mixed };

Eol DetectEol(std::string_view text);

const char* EolName(Eol eol);
const char* EolBytes(Eol eol);  // "" for None/Mixed

/// Replaces every CRLF / CR / LF with `target`. If target is None or Mixed the
/// input is returned unchanged.
std::string NormalizeEol(std::string_view text, Eol target);

}  // namespace ArgpiSuite::Format

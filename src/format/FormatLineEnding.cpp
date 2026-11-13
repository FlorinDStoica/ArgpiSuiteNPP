#include "FormatLineEnding.h"

namespace ArgpiSuite::Format {

Eol DetectEol(std::string_view text) {
    bool sawLf = false;
    bool sawCrlf = false;
    bool sawCr = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                sawCrlf = true;
                ++i;
            } else {
                sawCr = true;
            }
        } else if (text[i] == '\n') {
            sawLf = true;
        }
    }
    const int kinds = (sawLf ? 1 : 0) + (sawCrlf ? 1 : 0) + (sawCr ? 1 : 0);
    if (kinds == 0) {
        return Eol::None;
    }
    if (kinds > 1) {
        return Eol::Mixed;
    }
    if (sawCrlf) {
        return Eol::Crlf;
    }
    return sawCr ? Eol::Cr : Eol::Lf;
}

const char* EolName(Eol eol) {
    switch (eol) {
        case Eol::Lf:
            return "lf";
        case Eol::Crlf:
            return "crlf";
        case Eol::Cr:
            return "cr";
        case Eol::Mixed:
            return "mixed";
        case Eol::None:
        default:
            return "auto";
    }
}

const char* EolBytes(Eol eol) {
    switch (eol) {
        case Eol::Lf:
            return "\n";
        case Eol::Crlf:
            return "\r\n";
        case Eol::Cr:
            return "\r";
        case Eol::Mixed:
        case Eol::None:
        default:
            return "";
    }
}

std::string NormalizeEol(std::string_view text, Eol target) {
    const char* replacement = EolBytes(target);
    if (replacement[0] == '\0') {
        return std::string(text);
    }
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            out.append(replacement);
        } else if (text[i] == '\n') {
            out.append(replacement);
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

}  // namespace ArgpiSuite::Format

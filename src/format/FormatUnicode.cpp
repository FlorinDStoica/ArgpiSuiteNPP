#include "FormatUnicode.h"

#include <algorithm>
#include <array>

namespace ArgpiSuite::Format {
namespace {

struct Range {
    char32_t first;
    char32_t last;
};

// Sorted, non-overlapping ranges. Kept intentionally compact: it covers the
// categories that matter for line-width decisions.
constexpr Range kCombining[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x05BF, 0x05BF},
    {0x05C1, 0x05C2}, {0x05C4, 0x05C5}, {0x05C7, 0x05C7}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0670, 0x0670}, {0x06D6, 0x06DC}, {0x06DF, 0x06E4},
    {0x06E7, 0x06E8}, {0x06EA, 0x06ED}, {0x0711, 0x0711}, {0x0730, 0x074A},
    {0x07A6, 0x07B0}, {0x07EB, 0x07F3}, {0x0816, 0x0819}, {0x081B, 0x0823},
    {0x0825, 0x0827}, {0x0829, 0x082D}, {0x0859, 0x085B}, {0x08D3, 0x08E1},
    {0x08E3, 0x0903}, {0x093A, 0x093C}, {0x0941, 0x0948}, {0x094D, 0x094D},
    {0x0951, 0x0957}, {0x0962, 0x0963}, {0x0981, 0x0981}, {0x09BC, 0x09BC},
    {0x09C1, 0x09C4}, {0x09CD, 0x09CD}, {0x0A01, 0x0A02}, {0x0A3C, 0x0A3C},
    {0x0A41, 0x0A51}, {0x0A70, 0x0A71}, {0x0A75, 0x0A75}, {0x0ABC, 0x0ABC},
    {0x0AC1, 0x0AC8}, {0x0ACD, 0x0ACD}, {0x0B3C, 0x0B3C}, {0x0B3F, 0x0B3F},
    {0x0B41, 0x0B44}, {0x0B4D, 0x0B4D}, {0x0C3E, 0x0C40}, {0x0C46, 0x0C56},
    {0x0CBC, 0x0CBC}, {0x0CCC, 0x0CCD}, {0x0D41, 0x0D44}, {0x0D4D, 0x0D4D},
    {0x0DCA, 0x0DCA}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E},
    {0x0EB1, 0x0EB1}, {0x0EB4, 0x0EBC}, {0x0EC8, 0x0ECD}, {0x0F18, 0x0F19},
    {0x0F35, 0x0F35}, {0x0F37, 0x0F37}, {0x0F39, 0x0F39}, {0x0F71, 0x0F84},
};

constexpr Range kWide[] = {
    {0x1100, 0x115F}, {0x2329, 0x232A}, {0x2E80, 0x303E}, {0x3041, 0x33FF},
    {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xA960, 0xA97F},
    {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F},
    {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1B000, 0x1B12F}, {0x1F004, 0x1F004},
    {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F320},
    {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA},
    {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E},
    {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A},
};

constexpr Range kZeroWidth[] = {
    {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x2064}, {0x2066, 0x206F},
    {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0xE0000, 0xE007F},
    {0xE0100, 0xE01EF}, {0x1D173, 0x1D17A}, {0xE0020, 0xE007F}, {0x1160, 0x11FF},
    {0xD7B0, 0xD7FF}, {0x2065, 0x2065},
};

template <std::size_t N>
bool InRanges(const Range (&ranges)[N], char32_t cp) {
    std::size_t lo = 0;
    std::size_t hi = N;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        if (cp < ranges[mid].first) {
            hi = mid;
        } else if (cp > ranges[mid].last) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

}  // namespace

bool Utf8Decode(std::string_view text, std::size_t& index, char32_t& codePoint) {
    if (index >= text.size()) {
        return false;
    }
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte < 0x80) {
        codePoint = byte;
        ++index;
        return true;
    }
    std::size_t length = 0;
    char32_t value = 0;
    if ((byte & 0xE0) == 0xC0) {
        length = 2;
        value = byte & 0x1F;
    } else if ((byte & 0xF0) == 0xE0) {
        length = 3;
        value = byte & 0x0F;
    } else if ((byte & 0xF8) == 0xF0) {
        length = 4;
        value = byte & 0x07;
    } else {
        return false;
    }
    if (index + length > text.size()) {
        return false;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const auto continuation = static_cast<unsigned char>(text[index + i]);
        if ((continuation & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (continuation & 0x3F);
    }
    // Reject overlong encodings, surrogates and out-of-range values.
    if ((length == 2 && value < 0x80) || (length == 3 && value < 0x800) ||
        (length == 4 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }
    codePoint = value;
    index += length;
    return true;
}

void Utf8Append(std::string& out, char32_t codePoint) {
    if (codePoint < 0x80) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else if (codePoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}

std::size_t Utf8CodePointLength(std::string_view text, std::size_t index) {
    if (index >= text.size()) {
        return 0;
    }
    const auto byte = static_cast<unsigned char>(text[index]);
    std::size_t length = 1;
    if ((byte & 0xE0) == 0xC0) {
        length = 2;
    } else if ((byte & 0xF0) == 0xE0) {
        length = 3;
    } else if ((byte & 0xF8) == 0xF0) {
        length = 4;
    }
    return std::min(length, text.size() - index);
}

std::size_t Utf8AlignDown(std::string_view text, std::size_t index) {
    if (index > text.size()) {
        index = text.size();
    }
    while (index > 0 && index < text.size() &&
           (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) {
        --index;
    }
    return index;
}

std::size_t Utf8AlignUp(std::string_view text, std::size_t index) {
    if (index >= text.size()) {
        return text.size();
    }
    while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) {
        ++index;
    }
    return index;
}

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t next = i + Utf8CodePointLength(text, i);
        i = next == i ? i + 1 : next;
        ++count;
    }
    return count;
}

void Utf8LineColumn(std::string_view text, std::size_t offset, int& line, int& column) {
    line = 1;
    column = 1;
    if (offset > text.size()) {
        offset = text.size();
    }
    for (std::size_t i = 0; i < offset;) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if (byte == '\r') {
            ++line;
            column = 1;
            ++i;
            if (i < offset && text[i] == '\n') {
                ++i;
            }
        } else if (byte == '\n') {
            ++line;
            column = 1;
            ++i;
        } else {
            i += Utf8CodePointLength(text, i);
            ++column;
        }
    }
}

bool IsCombiningCodePoint(char32_t codePoint) {
    return InRanges(kCombining, codePoint) || InRanges(kZeroWidth, codePoint) ||
           (codePoint >= 0x1AB0 && codePoint <= 0x1AFF) ||
           (codePoint >= 0x1DC0 && codePoint <= 0x1DFF) ||
           (codePoint >= 0x20D0 && codePoint <= 0x20FF) ||
           (codePoint >= 0x302A && codePoint <= 0x302D) ||
           (codePoint >= 0x3099 && codePoint <= 0x309A) ||
           (codePoint >= 0xFE00 && codePoint <= 0xFE0F);
}

bool IsWideCodePoint(char32_t codePoint) {
    return InRanges(kWide, codePoint) ||
           (codePoint >= 0x1F300 && codePoint <= 0x1FAFF) ||
           (codePoint >= 0x20000 && codePoint <= 0x3FFFD);
}

int DisplayWidth(std::string_view text) {
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
        char32_t cp = 0;
        const std::size_t before = i;
        if (!Utf8Decode(text, i, cp)) {
            i = before + 1;
            width += 1;
            continue;
        }
        if (cp == '\t') {
            width += 1;
        } else if (cp < 0x20 || cp == 0x7F) {
            // Control characters occupy no columns.
        } else if (IsCombiningCodePoint(cp)) {
            // zero width
        } else if (IsWideCodePoint(cp)) {
            width += 2;
        } else {
            width += 1;
        }
    }
    return width;
}

}  // namespace ArgpiSuite::Format

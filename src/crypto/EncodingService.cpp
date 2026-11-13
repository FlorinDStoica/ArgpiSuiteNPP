#include "EncodingService.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <vector>

#include "plugin/TextUtil.h"

namespace ArgpiSuite::Crypto {
namespace {

// --- Base16 Helpers ------------------------------------------------------

const char kHexUpper[] = "0123456789ABCDEF";
const char kHexLower[] = "0123456789abcdef";

int HexDigitVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// --- Base64 Helpers ------------------------------------------------------

const char kBase64Std[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kBase64Url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int Base64CharVal(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

// --- Base85 Alphabets ----------------------------------------------------

// Z85 alphabet (RFC 32 / ZeroMQ)
const char kZ85Alphabet[86] =
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?&<>()[]{}@%$#";

// RFC 1924 alphabet
const char kRfc1924Alphabet[86] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "!#$%&()*+-;<=>?@^_`{|}~";

}  // namespace

EncodingService& EncodingService::Instance() {
    static EncodingService instance;
    return instance;
}

bool EncodingService::ValidateAlphabet(std::wstring_view alphabet, std::wstring& error) {
    if (alphabet.size() < 2) {
        error = L"Alphabet must contain at least 2 characters.";
        return false;
    }
    if (alphabet.size() > 256) {
        error = L"Alphabet cannot exceed 256 characters.";
        return false;
    }
    std::unordered_set<wchar_t> seen;
    for (wchar_t ch : alphabet) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            error = L"Alphabet cannot contain whitespace characters.";
            return false;
        }
        if (seen.find(ch) != seen.end()) {
            error = std::wstring(L"Alphabet contains duplicate character: '") + ch + L"'";
            return false;
        }
        seen.insert(ch);
    }
    error.clear();
    return true;
}

// --- Base16 --------------------------------------------------------------

EncodingResult EncodingService::EncodeBase16(const std::vector<uint8_t>& data,
                                            const Base16Options& options) {
    EncodingResult result;
    if (data.empty()) {
        result.ok = true;
        return result;
    }
    const char* lut = options.uppercase ? kHexUpper : kHexLower;
    const std::string delim = Plugin::ToUtf8(options.delimiter);
    std::string out;
    out.reserve(data.size() * (2 + delim.size()));

    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0 && !delim.empty()) {
            out.append(delim);
        }
        out.push_back(lut[(data[i] >> 4) & 0x0F]);
        out.push_back(lut[data[i] & 0x0F]);
    }
    result.ok = true;
    result.text = std::move(out);
    return result;
}

EncodingResult EncodingService::DecodeBase16(std::string_view text,
                                            const Base16Options& /*options*/) {
    EncodingResult result;
    std::vector<uint8_t> bytes;
    bytes.reserve(text.size() / 2);

    int highNibble = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ':' || c == '-' || c == ',') {
            continue;
        }
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            ++i;  // skip '0x' prefix
            continue;
        }
        const int val = HexDigitVal(c);
        if (val < 0) {
            result.message = L"Invalid hexadecimal character found.";
            return result;
        }
        if (highNibble < 0) {
            highNibble = val;
        } else {
            bytes.push_back(static_cast<uint8_t>((highNibble << 4) | val));
            highNibble = -1;
        }
    }
    if (highNibble >= 0) {
        result.message = L"Incomplete hexadecimal pair (odd number of hex digits).";
        return result;
    }
    result.ok = true;
    result.bytes = std::move(bytes);
    result.text = std::string(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
    return result;
}

// --- Base64 --------------------------------------------------------------

EncodingResult EncodingService::EncodeBase64(const std::vector<uint8_t>& data,
                                            const Base64Options& options) {
    EncodingResult result;
    if (data.empty()) {
        result.ok = true;
        return result;
    }
    const char* lut = options.urlSafe ? kBase64Url : kBase64Std;
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i < data.size()) {
        const size_t rem = data.size() - i;
        const uint32_t b0 = data[i++];
        const uint32_t b1 = (rem >= 2) ? data[i++] : 0;
        const uint32_t b2 = (rem >= 3) ? data[i++] : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(lut[(triple >> 18) & 0x3F]);
        out.push_back(lut[(triple >> 12) & 0x3F]);

        if (rem >= 3) {
            out.push_back(lut[(triple >> 6) & 0x3F]);
            out.push_back(lut[triple & 0x3F]);
        } else if (rem == 2) {
            out.push_back(lut[(triple >> 6) & 0x3F]);
            if (options.padding) out.push_back('=');
        } else {
            if (options.padding) {
                out.push_back('=');
                out.push_back('=');
            }
        }
    }

    if (options.lineWrap > 0) {
        std::string wrapped;
        wrapped.reserve(out.size() + (out.size() / options.lineWrap) * 2);
        for (size_t idx = 0; idx < out.size(); ++idx) {
            if (idx > 0 && idx % options.lineWrap == 0) {
                wrapped.push_back('\r');
                wrapped.push_back('\n');
            }
            wrapped.push_back(out[idx]);
        }
        out = std::move(wrapped);
    }

    result.ok = true;
    result.text = std::move(out);
    return result;
}

EncodingResult EncodingService::DecodeBase64(std::string_view text,
                                            const Base64Options& /*options*/) {
    EncodingResult result;
    std::vector<uint8_t> bytes;
    bytes.reserve((text.size() * 3) / 4);

    uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == '=') {
            continue;
        }
        const int val = Base64CharVal(c);
        if (val < 0) {
            result.message = L"Invalid character in Base64 string.";
            return result;
        }
        buf = (buf << 6) | static_cast<uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }

    result.ok = true;
    result.bytes = std::move(bytes);
    result.text = std::string(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
    return result;
}

// --- Base85 --------------------------------------------------------------

EncodingResult EncodingService::EncodeBase85(const std::vector<uint8_t>& data,
                                            Base85Variant variant) {
    EncodingResult result;
    if (data.empty()) {
        result.ok = true;
        return result;
    }

    std::string out;
    const bool isAscii85 = (variant == Base85Variant::Ascii85);
    const char* alphabet = nullptr;
    if (variant == Base85Variant::Z85) {
        alphabet = kZ85Alphabet;
    } else if (variant == Base85Variant::Rfc1924) {
        alphabet = kRfc1924Alphabet;
    }

    if (isAscii85) {
        out.append("<~");
    }

    size_t i = 0;
    while (i < data.size()) {
        const size_t rem = data.size() - i;
        uint32_t val = 0;
        for (int b = 0; b < 4; ++b) {
            val <<= 8;
            if (i < data.size()) {
                val |= data[i++];
            }
        }

        if (isAscii85 && rem >= 4 && val == 0) {
            out.push_back('z');
            continue;
        }

        char block[5];
        for (int p = 4; p >= 0; --p) {
            const uint32_t digit = val % 85;
            val /= 85;
            if (isAscii85) {
                block[p] = static_cast<char>(digit + 33);
            } else {
                block[p] = alphabet[digit];
            }
        }

        const size_t emitCount = (rem >= 4) ? 5 : (rem + 1);
        out.append(block, emitCount);
    }

    if (isAscii85) {
        out.append("~>");
    }

    result.ok = true;
    result.text = std::move(out);
    return result;
}

EncodingResult EncodingService::DecodeBase85(std::string_view text,
                                            Base85Variant variant) {
    EncodingResult result;
    std::string_view clean = text;

    // Strip Ascii85 delimiters if present
    if (clean.size() >= 4 && clean.substr(0, 2) == "<~" && clean.substr(clean.size() - 2) == "~>") {
        clean = clean.substr(2, clean.size() - 4);
        variant = Base85Variant::Ascii85;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve((clean.size() * 4) / 5);

    const bool isAscii85 = (variant == Base85Variant::Ascii85);
    const char* alphabet = (variant == Base85Variant::Z85) ? kZ85Alphabet : kRfc1924Alphabet;

    auto charToDigit = [&](char c) -> int {
        if (isAscii85) {
            if (c >= 33 && c <= 117) return c - 33;
            return -1;
        }
        const char* pos = std::strchr(alphabet, c);
        if (pos == nullptr) return -1;
        return static_cast<int>(pos - alphabet);
    };

    uint32_t digits[5] = {};
    int count = 0;

    for (size_t i = 0; i < clean.size(); ++i) {
        const char c = clean[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        if (isAscii85 && c == 'z' && count == 0) {
            for (int k = 0; k < 4; ++k) bytes.push_back(0);
            continue;
        }
        const int d = charToDigit(c);
        if (d < 0) {
            result.message = L"Invalid Base85 character encountered.";
            return result;
        }
        digits[count++] = static_cast<uint32_t>(d);
        if (count == 5) {
            uint64_t val = 0;
            for (int k = 0; k < 5; ++k) {
                val = val * 85 + digits[k];
            }
            if (val > 0xFFFFFFFFULL) {
                result.message = L"Base85 value overflow.";
                return result;
            }
            bytes.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
            bytes.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
            bytes.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
            bytes.push_back(static_cast<uint8_t>(val & 0xFF));
            count = 0;
        }
    }

    if (count > 1) {
        // Handle partial final block
        for (int k = count; k < 5; ++k) {
            digits[k] = 84;  // pad with max digit value
        }
        uint64_t val = 0;
        for (int k = 0; k < 5; ++k) {
            val = val * 85 + digits[k];
        }
        for (int k = 0; k < count - 1; ++k) {
            bytes.push_back(static_cast<uint8_t>((val >> (24 - k * 8)) & 0xFF));
        }
    } else if (count == 1) {
        result.message = L"Truncated Base85 block (single orphan character).";
        return result;
    }

    result.ok = true;
    result.bytes = std::move(bytes);
    result.text = std::string(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
    return result;
}

// --- Custom Base (Arbitrary Radix) ---------------------------------------

EncodingResult EncodingService::EncodeCustom(const std::vector<uint8_t>& data,
                                            std::wstring_view alphabetWide) {
    EncodingResult result;
    std::wstring err;
    if (!ValidateAlphabet(alphabetWide, err)) {
        result.message = err;
        return result;
    }
    if (data.empty()) {
        result.ok = true;
        return result;
    }

    const std::string alphabet = Plugin::ToUtf8(alphabetWide);
    const size_t radix = alphabet.size();

    // Count leading zeros
    size_t leadingZeros = 0;
    while (leadingZeros < data.size() && data[leadingZeros] == 0) {
        ++leadingZeros;
    }

    // Copy remaining bytes into working buffer for big-integer division
    std::vector<uint8_t> num(data.begin() + leadingZeros, data.end());
    std::string encodedDigits;

    while (!num.empty()) {
        uint32_t remainder = 0;
        std::vector<uint8_t> quotient;
        quotient.reserve(num.size());

        for (uint8_t byteVal : num) {
            const uint32_t acc = (remainder << 8) | byteVal;
            const uint32_t q = acc / static_cast<uint32_t>(radix);
            remainder = acc % static_cast<uint32_t>(radix);
            if (!quotient.empty() || q > 0) {
                quotient.push_back(static_cast<uint8_t>(q));
            }
        }
        encodedDigits.push_back(alphabet[remainder]);
        num = std::move(quotient);
    }

    // Append leading zero symbols
    for (size_t z = 0; z < leadingZeros; ++z) {
        encodedDigits.push_back(alphabet[0]);
    }

    // Digits were produced in reverse order
    std::reverse(encodedDigits.begin(), encodedDigits.end());

    result.ok = true;
    result.text = std::move(encodedDigits);
    return result;
}

EncodingResult EncodingService::DecodeCustom(std::string_view text,
                                            std::wstring_view alphabetWide) {
    EncodingResult result;
    std::wstring err;
    if (!ValidateAlphabet(alphabetWide, err)) {
        result.message = err;
        return result;
    }
    if (text.empty()) {
        result.ok = true;
        return result;
    }

    const std::string alphabet = Plugin::ToUtf8(alphabetWide);
    const size_t radix = alphabet.size();

    // Skip any leading whitespace
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    std::string_view clean = text.substr(start, end - start);

    // Count leading zero characters
    size_t leadingZeros = 0;
    while (leadingZeros < clean.size() && clean[leadingZeros] == alphabet[0]) {
        ++leadingZeros;
    }

    std::vector<uint8_t> decodedBytes;

    for (size_t i = leadingZeros; i < clean.size(); ++i) {
        const char ch = clean[i];
        const size_t pos = alphabet.find(ch);
        if (pos == std::string::npos) {
            result.message = std::wstring(L"Character '") + static_cast<wchar_t>(ch) +
                             L"' is not in the custom alphabet.";
            return result;
        }

        uint32_t carry = static_cast<uint32_t>(pos);
        for (int idx = static_cast<int>(decodedBytes.size()) - 1; idx >= 0; --idx) {
            const uint32_t acc = decodedBytes[idx] * static_cast<uint32_t>(radix) + carry;
            decodedBytes[idx] = static_cast<uint8_t>(acc & 0xFF);
            carry = acc >> 8;
        }
        while (carry > 0) {
            decodedBytes.insert(decodedBytes.begin(), static_cast<uint8_t>(carry & 0xFF));
            carry >>= 8;
        }
    }

    // Prepend leading zero bytes
    std::vector<uint8_t> finalBytes;
    finalBytes.reserve(leadingZeros + decodedBytes.size());
    for (size_t z = 0; z < leadingZeros; ++z) {
        finalBytes.push_back(0);
    }
    finalBytes.insert(finalBytes.end(), decodedBytes.begin(), decodedBytes.end());

    result.ok = true;
    result.bytes = std::move(finalBytes);
    result.text = std::string(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
    return result;
}

}  // namespace ArgpiSuite::Crypto


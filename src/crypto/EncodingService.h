// ArgpiSuite Encoding / Decoding Service.
// Pure reversible data representations (no password/key):
// - Base16 (Hexadecimal: uppercase, lowercase, formatted)
// - Base64 (RFC 4648 standard and URL-safe)
// - Base85 (Ascii85 / Adobe, Z85 / ZeroMQ, RFC 1924)
// - Custom Base (Arbitrary user-defined alphabet, e.g. Base58, Base62, etc.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Crypto {

enum class Base85Variant {
    Ascii85, // Adobe / btoa standard with <~ ~> and 'z' compression
    Z85,     // ZeroMQ string-safe 85
    Rfc1924  // IPv6 base85
};

struct Base16Options {
    bool uppercase = true;
    std::wstring delimiter; // empty, " ", ":", "-"
};

struct Base64Options {
    bool urlSafe = false;
    bool padding = true;
    int lineWrap = 0; // 0 = no wrap, or 64/76
};

struct EncodingResult {
    bool ok = false;
    std::string text;
    std::vector<uint8_t> bytes;
    std::wstring message;
};

class EncodingService {
public:
    static EncodingService& Instance();

    // --- Base16 (Hex) ---
    EncodingResult EncodeBase16(const std::vector<uint8_t>& data, const Base16Options& options = {});
    EncodingResult DecodeBase16(std::string_view text, const Base16Options& options = {});

    // --- Base64 ---
    EncodingResult EncodeBase64(const std::vector<uint8_t>& data, const Base64Options& options = {});
    EncodingResult DecodeBase64(std::string_view text, const Base64Options& options = {});

    // --- Base85 ---
    EncodingResult EncodeBase85(const std::vector<uint8_t>& data, Base85Variant variant = Base85Variant::Ascii85);
    EncodingResult DecodeBase85(std::string_view text, Base85Variant variant = Base85Variant::Ascii85);

    // --- Custom Base ---
    // Encodes arbitrary bytes using custom alphabet (length >= 2, unique chars).
    EncodingResult EncodeCustom(const std::vector<uint8_t>& data, std::wstring_view alphabet);
    EncodingResult DecodeCustom(std::string_view text, std::wstring_view alphabet);

    // Alphabet validator: checks length >= 2 and all characters are distinct.
    static bool ValidateAlphabet(std::wstring_view alphabet, std::wstring& error);
};

}  // namespace ArgpiSuite::Crypto


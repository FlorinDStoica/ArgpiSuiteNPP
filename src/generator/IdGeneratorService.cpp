#include "IdGeneratorService.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "password/CryptoHash.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Generator {
namespace {

constexpr int kMaxBulkCount = 100000;
constexpr const char kHexLower[] = "0123456789abcdef";
constexpr const char kHexUpper[] = "0123456789ABCDEF";
constexpr const char kCrockfordUpper[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr const char kCrockfordLower[] = "0123456789abcdefghjkmnpqrstvwxyz";

uint64_t CurrentUnixEpochMs() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string ApplyQuotes(std::string_view value, QuoteStyle quotes) {
    if (quotes == QuoteStyle::Single) {
        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('\'');
        out.append(value);
        out.push_back('\'');
        return out;
    }
    if (quotes == QuoteStyle::Double) {
        std::string out;
        out.reserve(value.size() + 2);
        out.push_back('"');
        out.append(value);
        out.push_back('"');
        return out;
    }
    return std::string(value);
}

std::string ResolveSeparator(BulkSeparator sep, std::string_view eol) {
    const std::string_view effectiveEol = eol.empty() ? std::string_view("\r\n") : eol;
    switch (sep) {
        case BulkSeparator::Newline:
            return std::string(effectiveEol);
        case BulkSeparator::CommaSpace:
            return ", ";
        case BulkSeparator::CommaNewline:
            return std::string(",") + std::string(effectiveEol);
        case BulkSeparator::Space:
            return " ";
    }
    return std::string(effectiveEol);
}

bool Increment80Bit(uint8_t bytes[10]) {
    for (int i = 9; i >= 0; --i) {
        if (bytes[i] < 0xFFu) {
            ++bytes[i];
            return true;
        }
        bytes[i] = 0;
    }
    return false;  // Overflowed all 80 bits.
}

}  // namespace

IdGeneratorService& IdGeneratorService::Instance() {
    static IdGeneratorService instance;
    return instance;
}

bool IdGeneratorService::GenerateUuidV4Bytes(uint8_t out[16]) {
    if (!Password::SecureRandomBytes(out, 16)) {
        return false;
    }
    out[6] = static_cast<uint8_t>((out[6] & 0x0Fu) | 0x40u);  // Version 4
    out[8] = static_cast<uint8_t>((out[8] & 0x3Fu) | 0x80u);  // Variant 10xx
    return true;
}

bool IdGeneratorService::GenerateUuidV7Bytes(uint8_t out[16]) {
    uint8_t randBytes[10] = {};
    if (!Password::SecureRandomBytes(randBytes, sizeof(randBytes))) {
        return false;
    }

    uint64_t tsMs = CurrentUnixEpochMs();
    uint16_t seq = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tsMs < lastUuidV7Ms_) {
            tsMs = lastUuidV7Ms_;
        }
        if (tsMs == lastUuidV7Ms_) {
            if (lastUuidV7Seq_ >= 0x0FFFu) {
                ++tsMs;
                lastUuidV7Ms_ = tsMs;
                lastUuidV7Seq_ = 0;
            } else {
                ++lastUuidV7Seq_;
            }
            seq = lastUuidV7Seq_;
        } else {
            lastUuidV7Ms_ = tsMs;
            // Seed with 11 random bits so each millisecond starts unpredictably
            // while still leaving >= 2048 monotonic steps before overflow.
            lastUuidV7Seq_ = static_cast<uint16_t>(((static_cast<uint16_t>(randBytes[0]) << 8) |
                                                    static_cast<uint16_t>(randBytes[1])) &
                                                   0x07FFu);
            seq = lastUuidV7Seq_;
        }
    }

    // 48-bit big-endian Unix ms timestamp
    out[0] = static_cast<uint8_t>((tsMs >> 40) & 0xFFu);
    out[1] = static_cast<uint8_t>((tsMs >> 32) & 0xFFu);
    out[2] = static_cast<uint8_t>((tsMs >> 24) & 0xFFu);
    out[3] = static_cast<uint8_t>((tsMs >> 16) & 0xFFu);
    out[4] = static_cast<uint8_t>((tsMs >> 8) & 0xFFu);
    out[5] = static_cast<uint8_t>(tsMs & 0xFFu);

    // Version 7 (4 bits) + 12-bit monotonic rand_a
    out[6] = static_cast<uint8_t>(0x70u | ((seq >> 8) & 0x0Fu));
    out[7] = static_cast<uint8_t>(seq & 0xFFu);

    // Variant 10xx (2 bits) + 62-bit rand_b
    std::memcpy(out + 8, randBytes + 2, 8);
    out[8] = static_cast<uint8_t>((out[8] & 0x3Fu) | 0x80u);
    return true;
}

bool IdGeneratorService::GenerateUlidParts(uint64_t& outTimestampMs, uint8_t outRand10[10]) {
    uint8_t freshRand[10] = {};
    if (!Password::SecureRandomBytes(freshRand, sizeof(freshRand))) {
        return false;
    }

    uint64_t tsMs = CurrentUnixEpochMs();

    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasUlidState_ || tsMs > lastUlidMs_) {
        lastUlidMs_ = tsMs;
        std::memcpy(lastUlidRand_, freshRand, 10);
        hasUlidState_ = true;
    } else {
        tsMs = lastUlidMs_;
        if (!Increment80Bit(lastUlidRand_)) {
            ++lastUlidMs_;
            tsMs = lastUlidMs_;
            std::memcpy(lastUlidRand_, freshRand, 10);
        }
    }

    outTimestampMs = tsMs;
    std::memcpy(outRand10, lastUlidRand_, 10);
    return true;
}

std::string IdGeneratorService::FormatUuidBytes(const uint8_t bytes[16],
                                                const IdFormatOptions& format) {
    const char* hex = format.uppercase ? kHexUpper : kHexLower;
    std::string out;
    out.reserve(32 + (format.hyphens ? 4 : 0) + (format.braces ? 2 : 0) + 2);

    if (format.braces) {
        out.push_back('{');
    }

    for (int i = 0; i < 16; ++i) {
        if (format.hyphens && (i == 4 || i == 6 || i == 8 || i == 10)) {
            out.push_back('-');
        }
        out.push_back(hex[(bytes[i] >> 4) & 0x0Fu]);
        out.push_back(hex[bytes[i] & 0x0Fu]);
    }

    if (format.braces) {
        out.push_back('}');
    }

    return ApplyQuotes(out, format.quotes);
}

std::string IdGeneratorService::EncodeUlid(uint64_t timestampMs, const uint8_t rand10[10],
                                           bool uppercase) {
    const char* alpha = uppercase ? kCrockfordUpper : kCrockfordLower;
    std::string out(26, '0');

    // 48-bit timestamp -> 10 Crockford Base32 chars (5 bits each, MSB first).
    // Top 2 bits of the 50-bit representation are always 0.
    for (int i = 9; i >= 0; --i) {
        out[static_cast<size_t>(i)] = alpha[timestampMs & 0x1Fu];
        timestampMs >>= 5;
    }

    // 80-bit random payload (10 bytes) -> 16 Crockford Base32 chars (5 bits each).
    int bitBuffer = 0;
    int bitCount = 0;
    size_t outIdx = 10;
    for (int i = 0; i < 10; ++i) {
        bitBuffer = (bitBuffer << 8) | rand10[i];
        bitCount += 8;
        while (bitCount >= 5) {
            bitCount -= 5;
            out[outIdx++] = alpha[(bitBuffer >> bitCount) & 0x1Fu];
        }
    }

    return out;
}

IdGenerateResult IdGeneratorService::GenerateSingle(IdType type, const IdFormatOptions& format) {
    IdGenerateResult result;
    if (type == IdType::Ulid) {
        uint64_t tsMs = 0;
        uint8_t rand10[10] = {};
        if (!GenerateUlidParts(tsMs, rand10)) {
            result.message = L"CSPRNG failure while generating ULID.";
            return result;
        }
        result.text = ApplyQuotes(EncodeUlid(tsMs, rand10, format.uppercase), format.quotes);
        result.ok = true;
        result.generatedCount = 1;
        return result;
    }

    uint8_t bytes[16] = {};
    const bool ok = (type == IdType::UuidV7) ? GenerateUuidV7Bytes(bytes) : GenerateUuidV4Bytes(bytes);
    if (!ok) {
        result.message = L"CSPRNG failure while generating UUID.";
        return result;
    }

    result.text = FormatUuidBytes(bytes, format);
    result.ok = true;
    result.generatedCount = 1;
    return result;
}

IdGenerateResult IdGeneratorService::GenerateBulk(const BulkGenerateOptions& options) {
    IdGenerateResult result;
    const int count = std::clamp(options.count, 1, kMaxBulkCount);
    const std::string sep = ResolveSeparator(options.separator, options.eol);

    const size_t approxPerId = 40 + sep.size();
    result.text.reserve(static_cast<size_t>(count) * approxPerId);

    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            result.text.append(sep);
        }
        const IdGenerateResult single = GenerateSingle(options.type, options.format);
        if (!single.ok) {
            result.ok = false;
            result.text.clear();
            result.message = single.message;
            return result;
        }
        result.text.append(single.text);
    }

    result.ok = true;
    result.generatedCount = count;
    return result;
}

std::wstring IdTypeName(IdType type) {
    switch (type) {
        case IdType::UuidV4:
            return L"uuid_v4";
        case IdType::UuidV7:
            return L"uuid_v7";
        case IdType::Guid:
            return L"guid";
        case IdType::Ulid:
            return L"ulid";
    }
    return L"uuid_v4";
}

IdType ParseIdType(std::wstring_view name) {
    if (Plugin::EqualsIgnoreCase(name, L"uuid_v7") || Plugin::EqualsIgnoreCase(name, L"v7")) {
        return IdType::UuidV7;
    }
    if (Plugin::EqualsIgnoreCase(name, L"guid")) {
        return IdType::Guid;
    }
    if (Plugin::EqualsIgnoreCase(name, L"ulid")) {
        return IdType::Ulid;
    }
    return IdType::UuidV4;
}

std::wstring QuoteStyleName(QuoteStyle quotes) {
    switch (quotes) {
        case QuoteStyle::None:
            return L"none";
        case QuoteStyle::Single:
            return L"single";
        case QuoteStyle::Double:
            return L"double";
    }
    return L"none";
}

QuoteStyle ParseQuoteStyle(std::wstring_view name) {
    if (Plugin::EqualsIgnoreCase(name, L"single")) {
        return QuoteStyle::Single;
    }
    if (Plugin::EqualsIgnoreCase(name, L"double")) {
        return QuoteStyle::Double;
    }
    return QuoteStyle::None;
}

std::wstring BulkSeparatorName(BulkSeparator sep) {
    switch (sep) {
        case BulkSeparator::Newline:
            return L"newline";
        case BulkSeparator::CommaSpace:
            return L"comma_space";
        case BulkSeparator::CommaNewline:
            return L"comma_newline";
        case BulkSeparator::Space:
            return L"space";
    }
    return L"newline";
}

BulkSeparator ParseBulkSeparator(std::wstring_view name) {
    if (Plugin::EqualsIgnoreCase(name, L"comma_space")) {
        return BulkSeparator::CommaSpace;
    }
    if (Plugin::EqualsIgnoreCase(name, L"comma_newline")) {
        return BulkSeparator::CommaNewline;
    }
    if (Plugin::EqualsIgnoreCase(name, L"space")) {
        return BulkSeparator::Space;
    }
    return BulkSeparator::Newline;
}

}  // namespace ArgpiSuite::Generator


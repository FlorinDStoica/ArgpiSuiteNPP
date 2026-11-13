// ArgpiSuite ID Generator Service.
// Generates cryptographically random and time-ordered identifiers:
// - UUID v4 (RFC 9562 random)
// - UUID v7 (RFC 9562 Unix-epoch ms timestamp + monotonic counter)
// - GUID (Microsoft-style UUID formatting)
// - ULID (48-bit ms timestamp + 80-bit monotonic randomness in Crockford Base32)
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Generator {

enum class IdType : int {
    UuidV4 = 0,
    UuidV7 = 1,
    Guid = 2,
    Ulid = 3,
};

enum class QuoteStyle : int {
    None = 0,
    Single = 1,
    Double = 2,
};

enum class BulkSeparator : int {
    Newline = 0,
    CommaSpace = 1,
    CommaNewline = 2,
    Space = 3,
};

struct IdFormatOptions {
    bool uppercase = false;
    bool hyphens = true;   // Applies to UUID / GUID
    bool braces = false;   // Applies to UUID / GUID ({...})
    QuoteStyle quotes = QuoteStyle::None;
};

struct BulkGenerateOptions {
    IdType type = IdType::UuidV4;
    IdFormatOptions format;
    int count = 1;
    BulkSeparator separator = BulkSeparator::Newline;
    std::string eol = "\r\n";
};

struct IdGenerateResult {
    bool ok = false;
    std::string text;
    std::wstring message;
    int generatedCount = 0;
};

class IdGeneratorService {
public:
    static IdGeneratorService& Instance();

    /// Generates a single formatted ID.
    IdGenerateResult GenerateSingle(IdType type, const IdFormatOptions& format);

    /// Generates `options.count` IDs (clamped to 1..100000) joined by `options.separator`.
    IdGenerateResult GenerateBulk(const BulkGenerateOptions& options);

    /// Formats 16 raw UUID bytes according to `format`.
    static std::string FormatUuidBytes(const uint8_t bytes[16], const IdFormatOptions& format);

    /// Encodes a 48-bit timestamp (ms) and 80-bit random payload (10 bytes) as a 26-char ULID.
    static std::string EncodeUlid(uint64_t timestampMs, const uint8_t rand10[10], bool uppercase);

private:
    IdGeneratorService() = default;

    bool GenerateUuidV4Bytes(uint8_t out[16]);
    bool GenerateUuidV7Bytes(uint8_t out[16]);
    bool GenerateUlidParts(uint64_t& outTimestampMs, uint8_t outRand10[10]);

    std::mutex mutex_;
    uint64_t lastUuidV7Ms_ = 0;
    uint16_t lastUuidV7Seq_ = 0;
    uint64_t lastUlidMs_ = 0;
    uint8_t lastUlidRand_[10] = {};
    bool hasUlidState_ = false;
};

// --- String Conversion Helpers -------------------------------------------

std::wstring IdTypeName(IdType type);
IdType ParseIdType(std::wstring_view name);

std::wstring QuoteStyleName(QuoteStyle quotes);
QuoteStyle ParseQuoteStyle(std::wstring_view name);

std::wstring BulkSeparatorName(BulkSeparator sep);
BulkSeparator ParseBulkSeparator(std::wstring_view name);

}  // namespace ArgpiSuite::Generator


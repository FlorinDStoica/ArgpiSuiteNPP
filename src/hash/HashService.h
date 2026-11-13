// ArgpiSuite Hashing & Checksum Service.
// Computes cryptographic digests, double hashes, CRC32, and HMACs over
// in-memory buffers or files on disk via Windows CNG and BLAKE2b.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Hash {

enum class HashAlgorithm : int {
    Md5 = 0,
    Sha1 = 1,
    Sha256 = 2,
    DoubleSha256 = 3,
    Sha384 = 4,
    Sha512 = 5,
    Blake2b256 = 6,
    Blake2b512 = 7,
    Crc32 = 8,
    HmacMd5 = 9,
    HmacSha1 = 10,
    HmacSha256 = 11,
    HmacSha384 = 12,
    HmacSha512 = 13,
};

struct HashOptions {
    HashAlgorithm algorithm = HashAlgorithm::Sha256;
    bool uppercase = false;
    std::string hmacKeyUtf8;  // Raw key bytes (or hex-decoded if hmacKeyIsHex is true)
    bool hmacKeyIsHex = false;
};

struct HashResult {
    bool ok = false;
    std::string hex;
    std::string base64;
    std::vector<uint8_t> rawBytes;
    int bitLength = 0;
    uint64_t inputBytes = 0;
    std::wstring message;
};

class HashService {
public:
    static HashService& Instance();

    /// Computes a hash or HMAC over an in-memory buffer.
    HashResult Compute(const uint8_t* data, size_t length, const HashOptions& options);
    HashResult Compute(std::string_view data, const HashOptions& options);

    /// Streams a file from disk in 64 KiB chunks and computes its hash or HMAC.
    HashResult ComputeFile(const std::wstring& filePath, const HashOptions& options);

    /// Compares two hex digests ignoring whitespace and case (constant-time on length match).
    static bool VerifyChecksum(std::string_view computedHex, std::string_view expectedHex);

    /// Returns true if `algorithm` requires an HMAC key.
    static bool IsHmac(HashAlgorithm algorithm);

    /// Parses hex string (ignoring spaces/colons/hyphens). Returns false on invalid hex.
    static bool ParseHexKey(std::string_view hex, std::vector<uint8_t>& outBytes,
                            std::wstring& error);
};

// --- Metadata & String Helpers -------------------------------------------

std::wstring AlgorithmDisplayName(HashAlgorithm alg);
std::wstring AlgorithmId(HashAlgorithm alg);
HashAlgorithm ParseAlgorithmId(std::wstring_view id);

}  // namespace ArgpiSuite::Hash


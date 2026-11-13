#include "HashService.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "crypto/EncodingService.h"
#include "password/CryptoHash.h"
#include "plugin/TextUtil.h"

#pragma comment(lib, "bcrypt.lib")

namespace ArgpiSuite::Hash {
namespace {

constexpr size_t kFileChunkSize = 65536;

std::string FormatHex(const uint8_t* data, size_t length, bool uppercase) {
    static constexpr char kLower[] = "0123456789abcdef";
    static constexpr char kUpper[] = "0123456789ABCDEF";
    const char* digits = uppercase ? kUpper : kLower;
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out.push_back(digits[(data[i] >> 4) & 0x0Fu]);
        out.push_back(digits[data[i] & 0x0Fu]);
    }
    return out;
}

uint32_t UpdateCrc32(uint32_t crc, const uint8_t* data, size_t length) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

LPCWSTR CngAlgId(HashAlgorithm alg) {
    switch (alg) {
        case HashAlgorithm::Md5:
        case HashAlgorithm::HmacMd5:
            return BCRYPT_MD5_ALGORITHM;
        case HashAlgorithm::Sha1:
        case HashAlgorithm::HmacSha1:
            return BCRYPT_SHA1_ALGORITHM;
        case HashAlgorithm::Sha256:
        case HashAlgorithm::DoubleSha256:
        case HashAlgorithm::HmacSha256:
            return BCRYPT_SHA256_ALGORITHM;
        case HashAlgorithm::Sha384:
        case HashAlgorithm::HmacSha384:
            return BCRYPT_SHA384_ALGORITHM;
        case HashAlgorithm::Sha512:
        case HashAlgorithm::HmacSha512:
            return BCRYPT_SHA512_ALGORITHM;
        default:
            return nullptr;
    }
}

class CngHasher {
public:
    ~CngHasher() { Close(); }

    bool Init(LPCWSTR algId, const uint8_t* key, size_t keyLen, bool isHmac) {
        Close();
        const ULONG flags = isHmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0;
        NTSTATUS status = ::BCryptOpenAlgorithmProvider(&hAlg_, algId, nullptr, flags);
        if (!BCRYPT_SUCCESS(status)) {
            return false;
        }
        DWORD objSize = 0;
        DWORD cb = 0;
        status = ::BCryptGetProperty(hAlg_, BCRYPT_OBJECT_LENGTH,
                                     reinterpret_cast<PUCHAR>(&objSize), sizeof(objSize), &cb, 0);
        if (!BCRYPT_SUCCESS(status)) {
            return false;
        }
        status = ::BCryptGetProperty(hAlg_, BCRYPT_HASH_LENGTH,
                                     reinterpret_cast<PUCHAR>(&hashLen_), sizeof(hashLen_), &cb, 0);
        if (!BCRYPT_SUCCESS(status)) {
            return false;
        }
        hashObject_.resize(objSize);
        status = ::BCryptCreateHash(
            hAlg_, &hHash_, hashObject_.data(), objSize,
            isHmac ? const_cast<PUCHAR>(key) : nullptr,
            isHmac ? static_cast<ULONG>(keyLen) : 0, 0);
        return BCRYPT_SUCCESS(status);
    }

    bool Update(const uint8_t* data, size_t length) {
        if (length == 0 || data == nullptr) {
            return true;
        }
        const NTSTATUS status = ::BCryptHashData(
            hHash_, const_cast<PUCHAR>(data), static_cast<ULONG>(length), 0);
        return BCRYPT_SUCCESS(status);
    }

    bool Finish(std::vector<uint8_t>& outDigest) {
        outDigest.resize(hashLen_);
        const NTSTATUS status = ::BCryptFinishHash(hHash_, outDigest.data(), hashLen_, 0);
        return BCRYPT_SUCCESS(status);
    }

    void Close() {
        if (hHash_ != nullptr) {
            ::BCryptDestroyHash(hHash_);
            hHash_ = nullptr;
        }
        if (hAlg_ != nullptr) {
            ::BCryptCloseAlgorithmProvider(hAlg_, 0);
            hAlg_ = nullptr;
        }
    }

private:
    BCRYPT_ALG_HANDLE hAlg_ = nullptr;
    BCRYPT_HASH_HANDLE hHash_ = nullptr;
    DWORD hashLen_ = 0;
    std::vector<uint8_t> hashObject_;
};

bool ResolveHmacKey(const HashOptions& options, std::vector<uint8_t>& outKey, std::wstring& error) {
    if (!HashService::IsHmac(options.algorithm)) {
        return true;
    }
    if (options.hmacKeyIsHex) {
        return HashService::ParseHexKey(options.hmacKeyUtf8, outKey, error);
    }
    outKey.assign(options.hmacKeyUtf8.begin(), options.hmacKeyUtf8.end());
    return true;
}

HashResult FinalizeDigest(std::vector<uint8_t> digest, uint64_t inputBytes, bool uppercase) {
    HashResult res;
    res.ok = true;
    res.rawBytes = std::move(digest);
    res.bitLength = static_cast<int>(res.rawBytes.size() * 8);
    res.inputBytes = inputBytes;
    res.hex = FormatHex(res.rawBytes.data(), res.rawBytes.size(), uppercase);

    Crypto::Base64Options b64Opts;
    b64Opts.padding = true;
    const Crypto::EncodingResult b64 = Crypto::EncodingService::Instance().EncodeBase64(
        res.rawBytes, b64Opts);
    if (b64.ok) {
        res.base64 = b64.text;
    }
    return res;
}

std::string NormalizeHex(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch) || ch == ':' || ch == '-') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(uch)));
    }
    return out;
}

}  // namespace

HashService& HashService::Instance() {
    static HashService instance;
    return instance;
}

bool HashService::IsHmac(HashAlgorithm algorithm) {
    return algorithm >= HashAlgorithm::HmacMd5 && algorithm <= HashAlgorithm::HmacSha512;
}

bool HashService::ParseHexKey(std::string_view hex, std::vector<uint8_t>& outBytes,
                              std::wstring& error) {
    outBytes.clear();
    std::string cleaned;
    cleaned.reserve(hex.size());
    for (char ch : hex) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch) || ch == ':' || ch == '-') {
            continue;
        }
        if (!std::isxdigit(uch)) {
            error = L"HMAC hex key contains non-hexadecimal characters.";
            return false;
        }
        cleaned.push_back(ch);
    }
    if ((cleaned.size() % 2) != 0) {
        error = L"HMAC hex key must have an even number of hex digits.";
        return false;
    }
    outBytes = Password::HexToBytes(cleaned);
    return true;
}

HashResult HashService::Compute(std::string_view data, const HashOptions& options) {
    return Compute(reinterpret_cast<const uint8_t*>(data.data()), data.size(), options);
}

HashResult HashService::Compute(const uint8_t* data, size_t length, const HashOptions& options) {
    HashResult res;
    std::vector<uint8_t> hmacKey;
    if (!ResolveHmacKey(options, hmacKey, res.message)) {
        return res;
    }

    if (options.algorithm == HashAlgorithm::Crc32) {
        const uint32_t crc = (data != nullptr && length > 0) ? UpdateCrc32(0, data, length) : 0u;
        std::vector<uint8_t> bytes = {
            static_cast<uint8_t>((crc >> 24) & 0xFFu),
            static_cast<uint8_t>((crc >> 16) & 0xFFu),
            static_cast<uint8_t>((crc >> 8) & 0xFFu),
            static_cast<uint8_t>(crc & 0xFFu),
        };
        return FinalizeDigest(std::move(bytes), length, options.uppercase);
    }

    if (options.algorithm == HashAlgorithm::Blake2b256 ||
        options.algorithm == HashAlgorithm::Blake2b512) {
        const size_t outLen = (options.algorithm == HashAlgorithm::Blake2b256) ? 32 : 64;
        std::vector<uint8_t> digest(outLen);
        if (!Password::Blake2bRaw(data, length, digest.data(), outLen)) {
            res.message = L"BLAKE2b computation failed.";
            return res;
        }
        return FinalizeDigest(std::move(digest), length, options.uppercase);
    }

    const LPCWSTR algId = CngAlgId(options.algorithm);
    if (algId == nullptr) {
        res.message = L"Unsupported hash algorithm.";
        return res;
    }

    CngHasher hasher;
    if (!hasher.Init(algId, hmacKey.data(), hmacKey.size(), IsHmac(options.algorithm)) ||
        !hasher.Update(data, length)) {
        res.message = L"Windows CNG hash initialization or update failed.";
        return res;
    }

    std::vector<uint8_t> digest;
    if (!hasher.Finish(digest)) {
        res.message = L"Windows CNG hash finalization failed.";
        return res;
    }

    if (options.algorithm == HashAlgorithm::DoubleSha256) {
        CngHasher second;
        std::vector<uint8_t> secondDigest;
        if (!second.Init(BCRYPT_SHA256_ALGORITHM, nullptr, 0, false) ||
            !second.Update(digest.data(), digest.size()) ||
            !second.Finish(secondDigest)) {
            res.message = L"Double SHA-256 second pass failed.";
            return res;
        }
        digest = std::move(secondDigest);
    }

    return FinalizeDigest(std::move(digest), length, options.uppercase);
}

HashResult HashService::ComputeFile(const std::wstring& filePath, const HashOptions& options) {
    HashResult res;
    std::vector<uint8_t> hmacKey;
    if (!ResolveHmacKey(options, hmacKey, res.message)) {
        return res;
    }

    HANDLE hFile = ::CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        res.message = L"Could not open file for hashing.";
        return res;
    }

    // For BLAKE2b, read into buffer or stream? If file <= 64 MiB we can read; for larger files
    // CNG algorithms stream with constant 64 KiB memory.
    std::vector<uint8_t> buffer(kFileChunkSize);
    uint64_t totalBytes = 0;

    if (options.algorithm == HashAlgorithm::Crc32) {
        uint32_t crc = 0;
        DWORD bytesRead = 0;
        while (::ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
               bytesRead > 0) {
            crc = UpdateCrc32(crc, buffer.data(), bytesRead);
            totalBytes += bytesRead;
        }
        ::CloseHandle(hFile);
        std::vector<uint8_t> bytes = {
            static_cast<uint8_t>((crc >> 24) & 0xFFu),
            static_cast<uint8_t>((crc >> 16) & 0xFFu),
            static_cast<uint8_t>((crc >> 8) & 0xFFu),
            static_cast<uint8_t>(crc & 0xFFu),
        };
        return FinalizeDigest(std::move(bytes), totalBytes, options.uppercase);
    }

    if (options.algorithm == HashAlgorithm::Blake2b256 ||
        options.algorithm == HashAlgorithm::Blake2b512) {
        LARGE_INTEGER fileSize{};
        if (!::GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart > 128LL * 1024LL * 1024LL) {
            ::CloseHandle(hFile);
            res.message = L"File is too large for in-memory BLAKE2b (max 128 MiB).";
            return res;
        }
        std::vector<uint8_t> allData(static_cast<size_t>(fileSize.QuadPart));
        size_t offset = 0;
        DWORD bytesRead = 0;
        while (offset < allData.size() &&
               ::ReadFile(hFile, allData.data() + offset,
                          static_cast<DWORD>(std::min<size_t>(kFileChunkSize, allData.size() - offset)),
                          &bytesRead, nullptr) &&
               bytesRead > 0) {
            offset += bytesRead;
        }
        ::CloseHandle(hFile);
        return Compute(allData.data(), offset, options);
    }

    const LPCWSTR algId = CngAlgId(options.algorithm);
    if (algId == nullptr) {
        ::CloseHandle(hFile);
        res.message = L"Unsupported hash algorithm.";
        return res;
    }

    CngHasher hasher;
    if (!hasher.Init(algId, hmacKey.data(), hmacKey.size(), IsHmac(options.algorithm))) {
        ::CloseHandle(hFile);
        res.message = L"Windows CNG hash initialization failed.";
        return res;
    }

    DWORD bytesRead = 0;
    BOOL readOk = TRUE;
    while ((readOk = ::ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead,
                                nullptr)) != FALSE &&
           bytesRead > 0) {
        if (!hasher.Update(buffer.data(), bytesRead)) {
            ::CloseHandle(hFile);
            res.message = L"Windows CNG hash update failed.";
            return res;
        }
        totalBytes += bytesRead;
    }
    ::CloseHandle(hFile);

    if (!readOk) {
        res.message = L"Error reading file.";
        return res;
    }

    std::vector<uint8_t> digest;
    if (!hasher.Finish(digest)) {
        res.message = L"Windows CNG hash finalization failed.";
        return res;
    }

    if (options.algorithm == HashAlgorithm::DoubleSha256) {
        CngHasher second;
        std::vector<uint8_t> secondDigest;
        if (!second.Init(BCRYPT_SHA256_ALGORITHM, nullptr, 0, false) ||
            !second.Update(digest.data(), digest.size()) ||
            !second.Finish(secondDigest)) {
            res.message = L"Double SHA-256 second pass failed.";
            return res;
        }
        digest = std::move(secondDigest);
    }

    return FinalizeDigest(std::move(digest), totalBytes, options.uppercase);
}

bool HashService::VerifyChecksum(std::string_view computedHex, std::string_view expectedHex) {
    const std::string a = NormalizeHex(computedHex);
    const std::string b = NormalizeHex(expectedHex);
    if (a.empty() || a.size() != b.size()) {
        return false;
    }
    return Password::ConstantTimeCompare(a, b);
}

std::wstring AlgorithmDisplayName(HashAlgorithm alg) {
    switch (alg) {
        case HashAlgorithm::Md5: return L"MD5";
        case HashAlgorithm::Sha1: return L"SHA-1";
        case HashAlgorithm::Sha256: return L"SHA-256";
        case HashAlgorithm::DoubleSha256: return L"2xSHA-256 (Double SHA-256)";
        case HashAlgorithm::Sha384: return L"SHA-384";
        case HashAlgorithm::Sha512: return L"SHA-512";
        case HashAlgorithm::Blake2b256: return L"BLAKE2b-256";
        case HashAlgorithm::Blake2b512: return L"BLAKE2b-512";
        case HashAlgorithm::Crc32: return L"CRC32 (IEEE 802.3)";
        case HashAlgorithm::HmacMd5: return L"HMAC-MD5";
        case HashAlgorithm::HmacSha1: return L"HMAC-SHA1";
        case HashAlgorithm::HmacSha256: return L"HMAC-SHA256";
        case HashAlgorithm::HmacSha384: return L"HMAC-SHA384";
        case HashAlgorithm::HmacSha512: return L"HMAC-SHA512";
    }
    return L"SHA-256";
}

std::wstring AlgorithmId(HashAlgorithm alg) {
    switch (alg) {
        case HashAlgorithm::Md5: return L"md5";
        case HashAlgorithm::Sha1: return L"sha1";
        case HashAlgorithm::Sha256: return L"sha256";
        case HashAlgorithm::DoubleSha256: return L"2xsha256";
        case HashAlgorithm::Sha384: return L"sha384";
        case HashAlgorithm::Sha512: return L"sha512";
        case HashAlgorithm::Blake2b256: return L"blake2b256";
        case HashAlgorithm::Blake2b512: return L"blake2b512";
        case HashAlgorithm::Crc32: return L"crc32";
        case HashAlgorithm::HmacMd5: return L"hmac_md5";
        case HashAlgorithm::HmacSha1: return L"hmac_sha1";
        case HashAlgorithm::HmacSha256: return L"hmac_sha256";
        case HashAlgorithm::HmacSha384: return L"hmac_sha384";
        case HashAlgorithm::HmacSha512: return L"hmac_sha512";
    }
    return L"sha256";
}

HashAlgorithm ParseAlgorithmId(std::wstring_view id) {
    if (Plugin::EqualsIgnoreCase(id, L"md5")) return HashAlgorithm::Md5;
    if (Plugin::EqualsIgnoreCase(id, L"sha1")) return HashAlgorithm::Sha1;
    if (Plugin::EqualsIgnoreCase(id, L"2xsha256")) return HashAlgorithm::DoubleSha256;
    if (Plugin::EqualsIgnoreCase(id, L"sha384")) return HashAlgorithm::Sha384;
    if (Plugin::EqualsIgnoreCase(id, L"sha512")) return HashAlgorithm::Sha512;
    if (Plugin::EqualsIgnoreCase(id, L"blake2b256")) return HashAlgorithm::Blake2b256;
    if (Plugin::EqualsIgnoreCase(id, L"blake2b512")) return HashAlgorithm::Blake2b512;
    if (Plugin::EqualsIgnoreCase(id, L"crc32")) return HashAlgorithm::Crc32;
    if (Plugin::EqualsIgnoreCase(id, L"hmac_md5")) return HashAlgorithm::HmacMd5;
    if (Plugin::EqualsIgnoreCase(id, L"hmac_sha1")) return HashAlgorithm::HmacSha1;
    if (Plugin::EqualsIgnoreCase(id, L"hmac_sha256")) return HashAlgorithm::HmacSha256;
    if (Plugin::EqualsIgnoreCase(id, L"hmac_sha384")) return HashAlgorithm::HmacSha384;
    if (Plugin::EqualsIgnoreCase(id, L"hmac_sha512")) return HashAlgorithm::HmacSha512;
    return HashAlgorithm::Sha256;
}

}  // namespace ArgpiSuite::Hash


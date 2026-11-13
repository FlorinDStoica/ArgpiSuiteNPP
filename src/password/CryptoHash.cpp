#include "CryptoHash.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace ArgpiSuite::Password {
namespace {

// =========================================================================
// CSPRNG & Rejection Sampling
// =========================================================================

constexpr const char* kRadix64 =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

constexpr const char* kBase64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

bool SecureRandomBytes(uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
        return false;
    }
    const NTSTATUS status = ::BCryptGenRandom(
        nullptr, buffer, static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return BCRYPT_SUCCESS(status);
}

uint32_t SecureRandomInt(uint32_t max) {
    if (max <= 1) {
        return 0;
    }
    const uint64_t range = 0x100000000ULL;
    const uint64_t limit = range - (range % max);
    uint32_t value = 0;
    do {
        if (!SecureRandomBytes(reinterpret_cast<uint8_t*>(&value), sizeof(value))) {
            return 0;
        }
    } while (value >= limit);
    return value % max;
}

std::string BytesToHex(const uint8_t* data, size_t length) {
    static const char hexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        hex.push_back(hexChars[(data[i] >> 4) & 0x0F]);
        hex.push_back(hexChars[data[i] & 0x0F]);
    }
    return hex;
}

std::vector<uint8_t> HexToBytes(const std::string& hex) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        const int hi = hexVal(hex[i]);
        const int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

bool ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t length) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < length; ++i) {
        diff |= (a[i] ^ b[i]);
    }
    return diff == 0;
}

bool ConstantTimeCompare(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    return ConstantTimeCompare(reinterpret_cast<const uint8_t*>(a.data()),
                               reinterpret_cast<const uint8_t*>(b.data()), a.size());
}

// =========================================================================
// Windows CNG Hashes
// =========================================================================

namespace {

std::string HashCng(LPCWSTR algId, const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = ::BCryptOpenAlgorithmProvider(&hAlg, algId, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;
    status = ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                                 reinterpret_cast<PUCHAR>(&hashObjectSize),
                                 sizeof(DWORD), &dataSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    DWORD hashLength = 0;
    status = ::BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                                 reinterpret_cast<PUCHAR>(&hashLength),
                                 sizeof(DWORD), &dataSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    std::vector<uint8_t> hashObject(hashObjectSize);
    std::vector<uint8_t> hashBuf(hashLength);
    BCRYPT_HASH_HANDLE hHash = nullptr;

    status = ::BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize,
                                nullptr, 0, 0);
    if (BCRYPT_SUCCESS(status)) {
        status = ::BCryptHashData(hHash,
                                  reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                                  static_cast<ULONG>(input.size()), 0);
        if (BCRYPT_SUCCESS(status)) {
            status = ::BCryptFinishHash(hHash, hashBuf.data(), hashLength, 0);
        }
        ::BCryptDestroyHash(hHash);
    }

    ::BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }
    return BytesToHex(hashBuf.data(), hashBuf.size());
}

}  // namespace

std::string HashSha256(const std::string& inputUtf8) {
    return HashCng(BCRYPT_SHA256_ALGORITHM, inputUtf8);
}

std::string HashSha512(const std::string& inputUtf8) {
    return HashCng(BCRYPT_SHA512_ALGORITHM, inputUtf8);
}

std::string HashSha1(const std::string& inputUtf8) {
    return HashCng(BCRYPT_SHA1_ALGORITHM, inputUtf8);
}

std::string HashMd5(const std::string& inputUtf8) {
    return HashCng(BCRYPT_MD5_ALGORITHM, inputUtf8);
}

std::string HashSha256Secret(const std::string& passwordUtf8, const std::string& secretKeyUtf8) {
    return HashSha256(secretKeyUtf8 + ":" + passwordUtf8);
}

std::string HashPbkdf2Secret(const std::string& passwordUtf8, const std::string& secretKeyUtf8,
                             int iterations, int keyLengthBytes) {
    if (iterations < 1 || keyLengthBytes < 1) {
        return {};
    }

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = ::BCryptOpenAlgorithmProvider(
        &hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    std::vector<uint8_t> derived(static_cast<size_t>(keyLengthBytes));
    status = ::BCryptDeriveKeyPBKDF2(
        hAlg,
        reinterpret_cast<PUCHAR>(const_cast<char*>(passwordUtf8.data())),
        static_cast<ULONG>(passwordUtf8.size()),
        reinterpret_cast<PUCHAR>(const_cast<char*>(secretKeyUtf8.data())),
        static_cast<ULONG>(secretKeyUtf8.size()),
        static_cast<ULONGLONG>(iterations),
        derived.data(),
        static_cast<ULONG>(derived.size()),
        0);

    ::BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }
    return BytesToHex(derived.data(), derived.size());
}

// =========================================================================
// BCrypt (Eksblowfish)
// =========================================================================

namespace {

// Initial state for Blowfish P-array and S-boxes (digits of pi)
static const uint32_t kOrigP[18] = {
    0x243f6a88U, 0x85a308d3U, 0x13198a2eU, 0x03707344U,
    0xa4093822U, 0x299f31d0U, 0x082efa98U, 0xec4e6c89U,
    0x452821e6U, 0x38d01377U, 0xbe5466cfU, 0x34e90c6cU,
    0xc0ac29b7U, 0xc97c50ddU, 0x3f84d5b5U, 0xb5470917U,
    0x9216d5d9U, 0x8979fb1bU
};

static const uint32_t kOrigS[4][256] = {
#include "CryptoTablesS0.inl"
,
#include "CryptoTablesS1.inl"
,
#include "CryptoTablesS2.inl"
,
#include "CryptoTablesS3.inl"
};

// BCrypt Radix64 encode
std::string Radix64Encode(const uint8_t* data, size_t length) {
    std::string out;
    size_t c = 0;
    while (c < length) {
        uint32_t a = data[c++];
        out.push_back(kRadix64[(a >> 2) & 0x3f]);
        a = (a & 0x03) << 4;
        if (c >= length) {
            out.push_back(kRadix64[a & 0x3f]);
            break;
        }
        uint32_t f = data[c++];
        a |= (f >> 4) & 0x0f;
        out.push_back(kRadix64[a & 0x3f]);
        a = (f & 0x0f) << 2;
        if (c >= length) {
            out.push_back(kRadix64[a & 0x3f]);
            break;
        }
        f = data[c++];
        a |= (f >> 6) & 0x03;
        out.push_back(kRadix64[a & 0x3f]);
        out.push_back(kRadix64[f & 0x3f]);
    }
    return out;
}

// BCrypt Radix64 decode
std::vector<uint8_t> Radix64Decode(const std::string& str, size_t maxBytes) {
    auto r64Val = [](char ch) -> int {
        const char* p = std::strchr(kRadix64, ch);
        return p ? static_cast<int>(p - kRadix64) : -1;
    };
    std::vector<uint8_t> out;
    out.reserve(maxBytes);
    size_t c = 0;
    const size_t len = str.size();
    while (c < len && out.size() < maxBytes) {
        int g = (c < len) ? r64Val(str[c++]) : -1;
        int m = (c < len) ? r64Val(str[c++]) : -1;
        if (g < 0 || m < 0) break;
        uint32_t h = (static_cast<uint32_t>(g) << 2) | ((static_cast<uint32_t>(m) & 0x30) >> 4);
        out.push_back(static_cast<uint8_t>(h));
        if (out.size() >= maxBytes || c >= len) break;

        g = r64Val(str[c++]);
        if (g < 0) break;
        h = ((static_cast<uint32_t>(m) & 0x0f) << 4) | ((static_cast<uint32_t>(g) & 0x3c) >> 2);
        out.push_back(static_cast<uint8_t>(h));
        if (out.size() >= maxBytes || c >= len) break;

        m = r64Val(str[c++]);
        if (m < 0) break;
        h = ((static_cast<uint32_t>(g) & 0x03) << 6) | static_cast<uint32_t>(m);
        out.push_back(static_cast<uint8_t>(h));
    }
    return out;
}

struct BlowfishContext {
    uint32_t P[18];
    uint32_t S[4][256];

    void Init() {
        std::memcpy(P, kOrigP, sizeof(P));
        std::memcpy(S, kOrigS, sizeof(S));
    }

    void Encrypt(uint32_t& xl, uint32_t& xr) const {
        uint32_t l = xl;
        uint32_t r = xr;
        for (int i = 0; i < 16; ++i) {
            l ^= P[i];
            r ^= ((S[0][(l >> 24) & 0xFF] + S[1][(l >> 16) & 0xFF]) ^
                  S[2][(l >> 8) & 0xFF]) + S[3][l & 0xFF];
            std::swap(l, r);
        }
        std::swap(l, r);
        r ^= P[16];
        l ^= P[17];
        xl = l;
        xr = r;
    }

    void ExpandKeyWithSalt(const uint8_t* key, size_t keyLen, const uint8_t* salt, size_t saltLen) {
        size_t keyOff = 0;
        size_t saltOff = 0;
        auto getWord = [](const uint8_t* src, size_t srcLen, size_t& off) -> uint32_t {
            uint32_t word = 0;
            for (int i = 0; i < 4; ++i) {
                word = (word << 8) | (srcLen > 0 ? src[off] : 0);
                off = (off + 1) % (srcLen > 0 ? srcLen : 1);
            }
            return word;
        };

        for (int i = 0; i < 18; ++i) {
            P[i] ^= getWord(key, keyLen, keyOff);
        }

        uint32_t l = 0, r = 0;
        for (int i = 0; i < 18; i += 2) {
            l ^= getWord(salt, saltLen, saltOff);
            r ^= getWord(salt, saltLen, saltOff);
            Encrypt(l, r);
            P[i] = l;
            P[i + 1] = r;
        }

        for (int s = 0; s < 4; ++s) {
            for (int i = 0; i < 256; i += 2) {
                l ^= getWord(salt, saltLen, saltOff);
                r ^= getWord(salt, saltLen, saltOff);
                Encrypt(l, r);
                S[s][i] = l;
                S[s][i + 1] = r;
            }
        }
    }

    void ExpandKey(const uint8_t* key, size_t keyLen) {
        size_t keyOff = 0;
        auto getWord = [](const uint8_t* src, size_t srcLen, size_t& off) -> uint32_t {
            uint32_t word = 0;
            for (int i = 0; i < 4; ++i) {
                word = (word << 8) | (srcLen > 0 ? src[off] : 0);
                off = (off + 1) % (srcLen > 0 ? srcLen : 1);
            }
            return word;
        };

        for (int i = 0; i < 18; ++i) {
            P[i] ^= getWord(key, keyLen, keyOff);
        }

        uint32_t l = 0, r = 0;
        for (int i = 0; i < 18; i += 2) {
            Encrypt(l, r);
            P[i] = l;
            P[i + 1] = r;
        }

        for (int s = 0; s < 4; ++s) {
            for (int i = 0; i < 256; i += 2) {
                Encrypt(l, r);
                S[s][i] = l;
                S[s][i + 1] = r;
            }
        }
    }
};

}  // namespace

std::string BcryptHash(const std::string& passwordUtf8, int cost, const uint8_t* salt16) {
    cost = std::min(31, std::max(4, cost));

    uint8_t generatedSalt[16];
    if (salt16 == nullptr) {
        if (!SecureRandomBytes(generatedSalt, sizeof(generatedSalt))) {
            return {};
        }
        salt16 = generatedSalt;
    }

    // BCrypt password buffer includes trailing null byte and is capped at 72 bytes.
    std::vector<uint8_t> pwdBytes;
    pwdBytes.reserve(std::min<size_t>(passwordUtf8.size() + 1, 72));
    for (size_t i = 0; i < passwordUtf8.size() && pwdBytes.size() < 71; ++i) {
        pwdBytes.push_back(static_cast<uint8_t>(passwordUtf8[i]));
    }
    pwdBytes.push_back(0);  // terminating null

    BlowfishContext ctx;
    ctx.Init();
    ctx.ExpandKeyWithSalt(pwdBytes.data(), pwdBytes.size(), salt16, 16);

    const uint32_t rounds = 1U << cost;
    for (uint32_t r = 0; r < rounds; ++r) {
        ctx.ExpandKey(pwdBytes.data(), pwdBytes.size());
        ctx.ExpandKey(salt16, 16);
    }

    // "OrpheanBeholderScryDoubt" (6 32-bit words, 3 64-bit blocks)
    uint32_t ctext[6] = {
        0x4f727068U, 0x65616e42U,
        0x65686f6cU, 0x64657253U,
        0x63727944U, 0x6f756274U
    };

    for (int i = 0; i < 64; ++i) {
        for (int b = 0; b < 6; b += 2) {
            ctx.Encrypt(ctext[b], ctext[b + 1]);
        }
    }

    // Convert 6 words to 23 bytes (standard BCrypt ignores the 24th byte)
    uint8_t ctextBytes[23];
    for (int i = 0; i < 23; ++i) {
        ctextBytes[i] = static_cast<uint8_t>((ctext[i >> 2] >> (8 * (3 - (i & 3)))) & 0xFF);
    }

    std::string saltEnc = Radix64Encode(salt16, 16);
    std::string hashEnc = Radix64Encode(ctextBytes, 23);

    char costBuf[8] = {};
    sprintf_s(costBuf, "%02d", cost);

    return std::string("$2y$") + costBuf + "$" + saltEnc + hashEnc;
}

bool BcryptVerify(const std::string& passwordUtf8, const std::string& hash) {
    if (hash.size() != 60) {
        return false;
    }
    if (hash.compare(0, 3, "$2a") != 0 && hash.compare(0, 3, "$2b") != 0 &&
        hash.compare(0, 3, "$2y") != 0) {
        return false;
    }
    if (hash[3] != '$' || hash[6] != '$') {
        return false;
    }

    const int cost = std::atoi(hash.substr(4, 2).c_str());
    if (cost < 4 || cost > 31) {
        return false;
    }

    const std::string saltStr = hash.substr(7, 22);
    const std::vector<uint8_t> saltBytes = Radix64Decode(saltStr, 16);
    if (saltBytes.size() != 16) {
        return false;
    }

    const std::string computed = BcryptHash(passwordUtf8, cost, saltBytes.data());
    if (computed.size() != 60) {
        return false;
    }

    // Compare starting from the salt ($2y$10$<salt><hash>)
    return ConstantTimeCompare(computed.substr(7), hash.substr(7));
}

// =========================================================================
// Argon2id (RFC 9106) & Blake2b
// =========================================================================

namespace {

static const uint64_t kBlake2bIV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const uint8_t kBlake2bSigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}
};

inline uint64_t Rotr64(uint64_t w, unsigned c) {
    return (w >> c) | (w << (64 - c));
}

class Blake2b {
public:
    Blake2b() { Reset(64); }
    explicit Blake2b(size_t outlen) { Reset(outlen); }

    void Reset(size_t outlen) {
        outlen_ = outlen;
        std::memcpy(h_, kBlake2bIV, sizeof(h_));
        h_[0] ^= 0x01010000ULL ^ static_cast<uint64_t>(outlen);
        t_[0] = 0;
        t_[1] = 0;
        c_ = 0;
        std::memset(buf_, 0, sizeof(buf_));
    }

    void Update(const void* data, size_t len) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        while (len > 0) {
            if (c_ == 128) {
                IncrementCounter(128);
                Compress(false);
                c_ = 0;
            }
            const size_t take = std::min(len, static_cast<size_t>(128 - c_));
            std::memcpy(buf_ + c_, p, take);
            c_ += take;
            p += take;
            len -= take;
        }
    }

    void Final(uint8_t* out) {
        IncrementCounter(c_);
        std::memset(buf_ + c_, 0, 128 - c_);
        Compress(true);
        for (size_t i = 0; i < outlen_; ++i) {
            out[i] = static_cast<uint8_t>((h_[i >> 3] >> (8 * (i & 7))) & 0xFF);
        }
    }

private:
    void IncrementCounter(size_t inc) {
        t_[0] += inc;
        if (t_[0] < inc) {
            t_[1]++;
        }
    }

    void Compress(bool last) {
        uint64_t m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = (static_cast<uint64_t>(buf_[i * 8 + 0])) |
                   (static_cast<uint64_t>(buf_[i * 8 + 1]) << 8) |
                   (static_cast<uint64_t>(buf_[i * 8 + 2]) << 16) |
                   (static_cast<uint64_t>(buf_[i * 8 + 3]) << 24) |
                   (static_cast<uint64_t>(buf_[i * 8 + 4]) << 32) |
                   (static_cast<uint64_t>(buf_[i * 8 + 5]) << 40) |
                   (static_cast<uint64_t>(buf_[i * 8 + 6]) << 48) |
                   (static_cast<uint64_t>(buf_[i * 8 + 7]) << 56);
        }

        uint64_t v[16];
        std::memcpy(v, h_, sizeof(h_));
        std::memcpy(v + 8, kBlake2bIV, sizeof(kBlake2bIV));
        v[12] ^= t_[0];
        v[13] ^= t_[1];
        if (last) {
            v[14] = ~v[14];
        }

        auto G = [&](int a, int b, int c, int d, uint64_t x, uint64_t y) {
            v[a] = v[a] + v[b] + x;
            v[d] = Rotr64(v[d] ^ v[a], 32);
            v[c] = v[c] + v[d];
            v[b] = Rotr64(v[b] ^ v[c], 24);
            v[a] = v[a] + v[b] + y;
            v[d] = Rotr64(v[d] ^ v[a], 16);
            v[c] = v[c] + v[d];
            v[b] = Rotr64(v[b] ^ v[c], 63);
        };

        for (int r = 0; r < 12; ++r) {
            G(0, 4, 8, 12, m[kBlake2bSigma[r][0]], m[kBlake2bSigma[r][1]]);
            G(1, 5, 9, 13, m[kBlake2bSigma[r][2]], m[kBlake2bSigma[r][3]]);
            G(2, 6, 10, 14, m[kBlake2bSigma[r][4]], m[kBlake2bSigma[r][5]]);
            G(3, 7, 11, 15, m[kBlake2bSigma[r][6]], m[kBlake2bSigma[r][7]]);
            G(0, 5, 10, 15, m[kBlake2bSigma[r][8]], m[kBlake2bSigma[r][9]]);
            G(1, 6, 11, 12, m[kBlake2bSigma[r][10]], m[kBlake2bSigma[r][11]]);
            G(2, 7, 8, 13, m[kBlake2bSigma[r][12]], m[kBlake2bSigma[r][13]]);
            G(3, 4, 9, 14, m[kBlake2bSigma[r][14]], m[kBlake2bSigma[r][15]]);
        }

        for (int i = 0; i < 8; ++i) {
            h_[i] ^= v[i] ^ v[i + 8];
        }
    }

    uint64_t h_[8];
    uint64_t t_[2];
    uint8_t buf_[128];
    size_t c_ = 0;
    size_t outlen_ = 64;
};

// Variable-length hash function H' (RFC 9106 Section 3.2.2)
void VariableLengthBlake2b(uint8_t* out, size_t outlen, const uint8_t* in, size_t inlen) {
    uint8_t outlenBytes[4] = {
        static_cast<uint8_t>(outlen & 0xFF),
        static_cast<uint8_t>((outlen >> 8) & 0xFF),
        static_cast<uint8_t>((outlen >> 16) & 0xFF),
        static_cast<uint8_t>((outlen >> 24) & 0xFF)
    };

    if (outlen <= 64) {
        Blake2b ctx(outlen);
        ctx.Update(outlenBytes, 4);
        ctx.Update(in, inlen);
        ctx.Final(out);
        return;
    }

    // outlen > 64: A_1 = Blake2b_64(outlen || in), A_2 = Blake2b_64(A_1), ...
    const size_t k = (outlen + 31) / 32 - 2;
    uint8_t A[64];
    Blake2b ctx1(64);
    ctx1.Update(outlenBytes, 4);
    ctx1.Update(in, inlen);
    ctx1.Final(A);

    std::memcpy(out, A, 32);
    size_t outOffset = 32;

    for (size_t i = 1; i <= k; ++i) {
        Blake2b ctx(64);
        ctx.Update(A, 64);
        ctx.Final(A);

        if (i < k) {
            std::memcpy(out + outOffset, A, 32);
            outOffset += 32;
        } else {
            // Last block
            const size_t remaining = outlen - 32 * k;
            Blake2b lastCtx(remaining);
            lastCtx.Update(A, 64);
            lastCtx.Final(out + outOffset);
        }
    }
}

// 1024-byte block for Argon2
struct Argon2Block {
    uint64_t v[128];

    void XorWith(const Argon2Block& other) {
        for (int i = 0; i < 128; ++i) {
            v[i] ^= other.v[i];
        }
    }

    void CopyFrom(const Argon2Block& other) {
        std::memcpy(v, other.v, sizeof(v));
    }
};

// Argon2 round permutation GB (RFC 9106 Section 3.4)
inline void GB(uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d) {
    auto fBF = [](uint64_t x, uint64_t y) -> uint64_t {
        const uint64_t xLo = static_cast<uint32_t>(x);
        const uint64_t yLo = static_cast<uint32_t>(y);
        return x + y + 2 * xLo * yLo;
    };
    a = fBF(a, b);
    d = Rotr64(d ^ a, 32);
    c = fBF(c, d);
    b = Rotr64(b ^ c, 24);
    a = fBF(a, b);
    d = Rotr64(d ^ a, 16);
    c = fBF(c, d);
    b = Rotr64(b ^ c, 63);
}

// Compression function G (RFC 9106 Section 3.4)
void Argon2G(Argon2Block& dest, const Argon2Block& X, const Argon2Block& Y) {
    Argon2Block R;
    for (int i = 0; i < 128; ++i) {
        R.v[i] = X.v[i] ^ Y.v[i];
    }
    Argon2Block Q = R;

    // Row-wise permutation on 8 rows of 16 uint64s
    for (int i = 0; i < 8; ++i) {
        uint64_t* row = Q.v + i * 16;
        GB(row[0], row[4], row[8], row[12]);
        GB(row[1], row[5], row[9], row[13]);
        GB(row[2], row[6], row[10], row[14]);
        GB(row[3], row[7], row[11], row[15]);
        GB(row[0], row[5], row[10], row[15]);
        GB(row[1], row[6], row[11], row[12]);
        GB(row[2], row[7], row[8], row[13]);
        GB(row[3], row[4], row[9], row[14]);
    }

    // Column-wise permutation on 8 columns of 16 uint64s
    for (int i = 0; i < 8; ++i) {
        uint64_t* col0 = Q.v + i * 2;
        uint64_t* col1 = Q.v + i * 2 + 1;
        GB(col0[0], col0[32], col0[64], col0[96]);
        GB(col1[0], col1[32], col1[64], col1[96]);
        GB(col0[16], col0[48], col0[80], col0[112]);
        GB(col1[16], col1[48], col1[80], col1[112]);
        GB(col0[0], col1[32], col0[80], col1[112]);
        GB(col1[0], col0[48], col1[64], col0[96]);
        GB(col0[16], col1[48], col0[64], col1[96]);
        GB(col1[16], col0[32], col1[80], col0[112]);
    }

    for (int i = 0; i < 128; ++i) {
        dest.v[i] = R.v[i] ^ Q.v[i];
    }
}

// Unpadded Base64 encoding
std::string Base64EncodeUnpadded(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len * 4 + 2) / 3);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = (i + 1 < len) ? data[i + 1] : 0;
        const uint32_t c = (i + 2 < len) ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;

        out.push_back(kBase64[(triple >> 18) & 0x3F]);
        out.push_back(kBase64[(triple >> 12) & 0x3F]);
        if (i + 1 < len) {
            out.push_back(kBase64[(triple >> 6) & 0x3F]);
        }
        if (i + 2 < len) {
            out.push_back(kBase64[triple & 0x3F]);
        }
    }
    return out;
}

std::vector<uint8_t> Base64Decode(const std::string& in) {
    auto b64Val = [](char c) -> int {
        const char* p = std::strchr(kBase64, c);
        return p ? static_cast<int>(p - kBase64) : -1;
    };
    std::vector<uint8_t> out;
    out.reserve((in.size() * 3) / 4);
    uint32_t val = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') break;
        const int v = b64Val(c);
        if (v < 0) continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
        }
    }
    return out;
}

}  // namespace

std::string Argon2idHash(const std::string& passwordUtf8, const Argon2idParams& params,
                         const uint8_t* salt16) {
    const uint32_t t = std::min(10, std::max(1, params.t));
    const uint32_t p = std::min(8, std::max(1, params.p));
    const uint32_t hashLength = std::min(64, std::max(16, params.hashLength));
    // m must be at least 8 * p
    const uint32_t minM = 8 * p;
    const uint32_t m = std::min(65536U, std::max(minM, static_cast<uint32_t>(params.m)));

    uint8_t generatedSalt[16];
    if (salt16 == nullptr) {
        if (!SecureRandomBytes(generatedSalt, sizeof(generatedSalt))) {
            return {};
        }
        salt16 = generatedSalt;
    }

    // 1. Establish H_0 = Blake2b_64(LE32(p), LE32(tag_length), LE32(m), LE32(t), LE32(v=19), LE32(type=2),
    //                               LE32(pwd_len), pwd, LE32(salt_len), salt, LE32(0), LE32(0))
    auto appendLe32 = [](std::vector<uint8_t>& buf, uint32_t val) {
        buf.push_back(static_cast<uint8_t>(val & 0xFF));
        buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    };

    std::vector<uint8_t> h0Input;
    appendLe32(h0Input, p);
    appendLe32(h0Input, hashLength);
    appendLe32(h0Input, m);
    appendLe32(h0Input, t);
    appendLe32(h0Input, 0x13);  // version 19
    appendLe32(h0Input, 2);     // type 2 = Argon2id
    appendLe32(h0Input, static_cast<uint32_t>(passwordUtf8.size()));
    h0Input.insert(h0Input.end(), passwordUtf8.begin(), passwordUtf8.end());
    appendLe32(h0Input, 16);  // salt length
    h0Input.insert(h0Input.end(), salt16, salt16 + 16);
    appendLe32(h0Input, 0);   // secret length
    appendLe32(h0Input, 0);   // ad length

    uint8_t H_0[64];
    Blake2b b0(64);
    b0.Update(h0Input.data(), h0Input.size());
    b0.Final(H_0);

    // 2. Memory allocation: mm = 4 * p * floor(m / (4 * p))
    const uint32_t mm = 4 * p * (m / (4 * p));
    const uint32_t q = mm / p;
    const uint32_t segmentLength = q / 4;

    std::vector<std::vector<Argon2Block>> B(p, std::vector<Argon2Block>(q));

    // 3. Initialize B[i][0] and B[i][1]
    for (uint32_t i = 0; i < p; ++i) {
        uint8_t blockInput[72];
        std::memcpy(blockInput, H_0, 64);
        blockInput[64] = 0; blockInput[65] = 0; blockInput[66] = 0; blockInput[67] = 0;
        blockInput[68] = static_cast<uint8_t>(i & 0xFF);
        blockInput[69] = static_cast<uint8_t>((i >> 8) & 0xFF);
        blockInput[70] = static_cast<uint8_t>((i >> 16) & 0xFF);
        blockInput[71] = static_cast<uint8_t>((i >> 24) & 0xFF);
        VariableLengthBlake2b(reinterpret_cast<uint8_t*>(B[i][0].v), 1024, blockInput, 72);

        blockInput[64] = 1;
        VariableLengthBlake2b(reinterpret_cast<uint8_t*>(B[i][1].v), 1024, blockInput, 72);
    }

    // 4. Passes and slices
    for (uint32_t pass = 0; pass < t; ++pass) {
        for (uint32_t slice = 0; slice < 4; ++slice) {
            for (uint32_t lane = 0; lane < p; ++lane) {
                // In pass 0, slice 0 & 1 use data-independent indexing
                const bool dataIndependent = (pass == 0 && (slice == 0 || slice == 1));

                Argon2Block addressBlock;
                Argon2Block inputBlock;
                std::memset(&inputBlock, 0, sizeof(inputBlock));
                uint32_t addressIndex = 0;

                auto nextAddresses = [&](uint64_t& j1, uint64_t& j2) {
                    if (addressIndex % 128 == 0) {
                        inputBlock.v[0] = pass;
                        inputBlock.v[1] = lane;
                        inputBlock.v[2] = slice;
                        inputBlock.v[3] = mm;
                        inputBlock.v[4] = t;
                        inputBlock.v[5] = 2;  // Argon2id
                        inputBlock.v[6] = (addressIndex / 128) + 1;
                        Argon2G(addressBlock, inputBlock, inputBlock);
                        Argon2G(addressBlock, addressBlock, inputBlock);
                    }
                    const uint64_t word = addressBlock.v[addressIndex % 128];
                    ++addressIndex;
                    j1 = static_cast<uint32_t>(word);
                    j2 = static_cast<uint32_t>(word >> 32);
                };

                const uint32_t startIndex = (pass == 0 && slice == 0) ? 2 : 0;
                for (uint32_t index = startIndex; index < segmentLength; ++index) {
                    const uint32_t currIndex = slice * segmentLength + index;
                    const uint32_t prevIndex = (currIndex == 0) ? (q - 1) : (currIndex - 1);

                    uint64_t j1 = 0, j2 = 0;
                    if (dataIndependent) {
                        nextAddresses(j1, j2);
                    } else {
                        j1 = static_cast<uint32_t>(B[lane][prevIndex].v[0]);
                        j2 = static_cast<uint32_t>(B[lane][prevIndex].v[0] >> 32);
                    }

                    const uint32_t refLane = (pass == 0 && slice == 0) ? lane : (static_cast<uint32_t>(j2 % p));

                    // Reference area
                    uint32_t refAreaSize = 0;
                    if (pass == 0) {
                        if (slice == 0) {
                            refAreaSize = currIndex - 1;
                        } else if (refLane == lane) {
                            refAreaSize = currIndex - 1;
                        } else {
                            refAreaSize = slice * segmentLength - ((index == 0) ? 1 : 0);
                        }
                    } else {
                        if (refLane == lane) {
                            refAreaSize = q - segmentLength + index - 1;
                        } else {
                            refAreaSize = q - segmentLength - ((index == 0) ? 1 : 0);
                        }
                    }

                    // Pseudo-random index within reference area
                    const uint64_t x = (j1 * j1) >> 32;
                    const uint64_t y = (static_cast<uint64_t>(refAreaSize) * x) >> 32;
                    const uint32_t relPos = static_cast<uint32_t>(static_cast<uint64_t>(refAreaSize) - 1 - y);

                    uint32_t startPos = 0;
                    if (pass != 0) {
                        startPos = (slice == 3) ? 0 : ((slice + 1) * segmentLength);
                    }
                    const uint32_t refIndex = (startPos + relPos) % q;

                    Argon2Block nextBlock;
                    Argon2G(nextBlock, B[lane][prevIndex], B[refLane][refIndex]);
                    if (pass > 0) {
                        B[lane][currIndex].XorWith(nextBlock);
                    } else {
                        B[lane][currIndex].CopyFrom(nextBlock);
                    }
                }
            }
        }
    }

    // 5. Compute final block C = XOR over last block of all lanes
    Argon2Block C = B[0][q - 1];
    for (uint32_t i = 1; i < p; ++i) {
        C.XorWith(B[i][q - 1]);
    }

    // 6. Output tag = VariableLengthBlake2b(hashLength, C)
    std::vector<uint8_t> tag(hashLength);
    VariableLengthBlake2b(tag.data(), hashLength, reinterpret_cast<const uint8_t*>(C.v), 1024);

    const std::string saltB64 = Base64EncodeUnpadded(salt16, 16);
    const std::string tagB64 = Base64EncodeUnpadded(tag.data(), tag.size());

    std::ostringstream ss;
    ss << "$argon2id$v=19$m=" << m << ",t=" << t << ",p=" << p << "$" << saltB64 << "$" << tagB64;
    return ss.str();
}

bool Argon2idVerify(const std::string& passwordUtf8, const std::string& hash) {
    // Expected format: $argon2id$v=19$m=<m>,t=<t>,p=<p>$<salt>$<tag>
    if (hash.compare(0, 10, "$argon2id$") != 0) {
        return false;
    }

    size_t pos = 10;
    // Version
    if (hash.compare(pos, 5, "v=19$") != 0) {
        return false;
    }
    pos += 5;

    // Parameters: m=...,t=...,p=...$
    const size_t nextDollar = hash.find('$', pos);
    if (nextDollar == std::string::npos) {
        return false;
    }
    const std::string paramStr = hash.substr(pos, nextDollar - pos);
    pos = nextDollar + 1;

    Argon2idParams params;
    std::istringstream pss(paramStr);
    std::string token;
    while (std::getline(pss, token, ',')) {
        if (token.rfind("m=", 0) == 0) {
            params.m = std::atoi(token.substr(2).c_str());
        } else if (token.rfind("t=", 0) == 0) {
            params.t = std::atoi(token.substr(2).c_str());
        } else if (token.rfind("p=", 0) == 0) {
            params.p = std::atoi(token.substr(2).c_str());
        }
    }

    // Salt
    const size_t saltDollar = hash.find('$', pos);
    if (saltDollar == std::string::npos) {
        return false;
    }
    const std::string saltB64 = hash.substr(pos, saltDollar - pos);
    const std::string expectedTagB64 = hash.substr(saltDollar + 1);

    const std::vector<uint8_t> saltBytes = Base64Decode(saltB64);
    if (saltBytes.size() != 16) {
        return false;
    }
    const std::vector<uint8_t> expectedTagBytes = Base64Decode(expectedTagB64);
    if (expectedTagBytes.empty()) {
        return false;
    }
    params.hashLength = static_cast<int>(expectedTagBytes.size());

    const std::string computed = Argon2idHash(passwordUtf8, params, saltBytes.data());
    return ConstantTimeCompare(computed, hash);
}

bool Argon2idRaw(const uint8_t* password, size_t passwordLen,
                 const uint8_t* salt, size_t saltLen,
                 int t, int m, int p, uint8_t* output, size_t outputLen) {
    if (password == nullptr || salt == nullptr || output == nullptr || outputLen < 1 || saltLen < 1) {
        return false;
    }
    Argon2idParams params;
    params.t = t;
    params.m = m;
    params.p = p;
    params.hashLength = static_cast<int>(outputLen);
    const std::string passStr(reinterpret_cast<const char*>(password), passwordLen);
    const std::string hash = Argon2idHash(passStr, params, salt);
    if (hash.empty()) {
        return false;
    }
    const size_t lastDollar = hash.rfind('$');
    if (lastDollar == std::string::npos) {
        return false;
    }
    const std::vector<uint8_t> tag = Base64Decode(hash.substr(lastDollar + 1));
    if (tag.size() < outputLen) {
        return false;
    }
    std::memcpy(output, tag.data(), outputLen);
    return true;
}

bool Pbkdf2Sha256Raw(const uint8_t* password, size_t passwordLen,
                     const uint8_t* salt, size_t saltLen,
                     int iterations, uint8_t* output, size_t outputLen) {
    if (password == nullptr || salt == nullptr || output == nullptr ||
        iterations < 1 || outputLen < 1) {
        return false;
    }
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = ::BCryptOpenAlgorithmProvider(
        &hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) {
        return false;
    }
    status = ::BCryptDeriveKeyPBKDF2(
        hAlg,
        const_cast<PUCHAR>(password), static_cast<ULONG>(passwordLen),
        const_cast<PUCHAR>(salt), static_cast<ULONG>(saltLen),
        static_cast<ULONGLONG>(iterations),
        output, static_cast<ULONG>(outputLen), 0);
    ::BCryptCloseAlgorithmProvider(hAlg, 0);
    return BCRYPT_SUCCESS(status);
}

bool Blake2bRaw(const uint8_t* data, size_t dataLen, uint8_t* output, size_t outputLen) {
    if (output == nullptr || outputLen < 1 || outputLen > 64) {
        return false;
    }
    Blake2b ctx(outputLen);
    if (data != nullptr && dataLen > 0) {
        ctx.Update(data, dataLen);
    }
    ctx.Final(output);
    return true;
}

}  // namespace ArgpiSuite::Password

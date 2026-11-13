// ArgpiSuite Password & Cryptographic Hashing Service.
// Implements BCrypt (Eksblowfish), Argon2id (RFC 9106), Blake2b, SHA-256,
// SHA-512, SHA-1, MD5, and PBKDF2-HMAC-SHA256 via Windows CNG.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ArgpiSuite::Password {

/// Fill buffer with cryptographically secure random bytes via Windows CNG.
bool SecureRandomBytes(uint8_t* buffer, size_t length);

/// Unbiased random integer in [0, max) using CSPRNG rejection sampling.
uint32_t SecureRandomInt(uint32_t max);

/// Convert raw bytes to lowercase hexadecimal string.
std::string BytesToHex(const uint8_t* data, size_t length);

/// Decode lowercase/uppercase hexadecimal string to bytes.
std::vector<uint8_t> HexToBytes(const std::string& hex);

/// Constant-time memory comparison to resist timing attacks.
bool ConstantTimeCompare(const uint8_t* a, const uint8_t* b, size_t length);
bool ConstantTimeCompare(const std::string& a, const std::string& b);

// --- Standard Hashes (Windows CNG) ---------------------------------------

std::string HashSha256(const std::string& inputUtf8);
std::string HashSha512(const std::string& inputUtf8);
std::string HashSha1(const std::string& inputUtf8);
std::string HashMd5(const std::string& inputUtf8);

/// SHA-256 over `secretKey + ":" + password`.
std::string HashSha256Secret(const std::string& passwordUtf8, const std::string& secretKeyUtf8);

/// PBKDF2-HMAC-SHA256 using `secretKey` as salt and `password` as key material.
std::string HashPbkdf2Secret(const std::string& passwordUtf8, const std::string& secretKeyUtf8,
                             int iterations, int keyLengthBytes);

// --- BCrypt (Eksblowfish) ------------------------------------------------

/// Hash password using BCrypt with specified cost (rounds, 4..31, default 10).
/// Produces standard PHP/Joomla `$2y$<cost>$<salt22><hash31>` format.
/// If `salt16` is nullptr, 16 CSPRNG bytes are generated.
std::string BcryptHash(const std::string& passwordUtf8, int cost = 10,
                       const uint8_t* salt16 = nullptr);

/// Verify password against a BCrypt hash ($2a$, $2b$, or $2y$).
bool BcryptVerify(const std::string& passwordUtf8, const std::string& hash);

// --- Argon2id (RFC 9106) --------------------------------------------------

struct Argon2idParams {
    int t = 2;          // time cost (iterations, 1..10)
    int m = 19456;      // memory cost in KiB (8..65536, default 19 MiB)
    int p = 1;          // parallelism (lanes, 1..8)
    int hashLength = 32;// tag output length in bytes (16..64)
};

/// Hash password using Argon2id.
/// Produces standard `$argon2id$v=19$m=<m>,t=<t>,p=<p>$<salt_b64>$<hash_b64>` format.
/// If `salt16` is nullptr, 16 CSPRNG bytes are generated.
std::string Argon2idHash(const std::string& passwordUtf8, const Argon2idParams& params,
                         const uint8_t* salt16 = nullptr);

/// Verify password against an Argon2id hash ($argon2id$).
bool Argon2idVerify(const std::string& passwordUtf8, const std::string& hash);

// --- Raw KDF functions ----------------------------------------------------

bool Pbkdf2Sha256Raw(const uint8_t* password, size_t passwordLen,
                     const uint8_t* salt, size_t saltLen,
                     int iterations, uint8_t* output, size_t outputLen);

bool Argon2idRaw(const uint8_t* password, size_t passwordLen,
                 const uint8_t* salt, size_t saltLen,
                 int t, int m, int p, uint8_t* output, size_t outputLen);

/// Computes an unkeyed BLAKE2b digest of `outputLen` bytes (1..64).
bool Blake2bRaw(const uint8_t* data, size_t dataLen, uint8_t* output, size_t outputLen);

}  // namespace ArgpiSuite::Password


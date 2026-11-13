// ArgpiSuite Crypto — cryptographic encryption and decryption service.
// Supports AES-256-GCM, AES-256-CBC, AES-128-GCM, AES-128-CBC
// with PBKDF2-HMAC-SHA256 and Argon2id key derivation via Windows CNG.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Crypto {

enum class CryptoStatus {
    Ok,
    AuthFailed,       // incorrect passphrase or tampered ciphertext
    InvalidContainer, // not an ArgpiSuite encrypted container
    InvalidArgument,  // empty passphrase, missing file, etc.
    Failure           // system CNG failure or IO error
};

struct CryptoOptions {
    std::wstring cipher = L"aes-256-gcm"; // "aes-256-gcm", "aes-256-cbc", "aes-128-gcm", "aes-128-cbc"
    std::wstring kdf = L"pbkdf2-sha256";  // "pbkdf2-sha256", "argon2id"
    int iterations = 600000;              // PBKDF2 iterations
    int argon2t = 2;                      // Argon2id time cost
    int argon2m = 19456;                  // Argon2id memory cost in KiB
    int argon2p = 1;                      // Argon2id parallelism
    bool armorBase64 = true;              // text armor format
};

struct CryptoResult {
    CryptoStatus status = CryptoStatus::Failure;
    std::vector<unsigned char> bytes;
    std::wstring message;
};

class ICryptoService {
public:
    virtual ~ICryptoService() = default;

    /// Encrypts `plain` bytes with `passphrase`.
    /// Returns versioned container (ARGPICR1 header + salt + nonce + tag + ciphertext).
    virtual CryptoResult Encrypt(const std::vector<unsigned char>& plain,
                                 std::wstring_view passphrase,
                                 const CryptoOptions& options) = 0;

    /// Decrypts a container produced by Encrypt.
    virtual CryptoResult Decrypt(const std::vector<unsigned char>& container,
                                 std::wstring_view passphrase) = 0;

    /// Encodes a binary container into armored text banner with Base64 body.
    virtual std::string ArmorMessage(const std::vector<unsigned char>& container,
                                     const CryptoOptions& options) = 0;

    /// Extracts binary container from armored text banner or raw Base64.
    virtual bool UnarmorMessage(std::string_view text,
                                std::vector<unsigned char>& container) = 0;

    /// Encrypts an entire file from disk to disk atomically.
    virtual CryptoResult EncryptFileOnDisk(const std::wstring& srcPath,
                                           const std::wstring& dstPath,
                                           std::wstring_view passphrase,
                                           const CryptoOptions& options) = 0;

    /// Decrypts an entire file from disk to disk atomically.
    virtual CryptoResult DecryptFileOnDisk(const std::wstring& srcPath,
                                           const std::wstring& dstPath,
                                           std::wstring_view passphrase) = 0;
};

class CryptoService final : public ICryptoService {
public:
    static CryptoService& Instance();

    CryptoResult Encrypt(const std::vector<unsigned char>& plain,
                         std::wstring_view passphrase,
                         const CryptoOptions& options) override;

    CryptoResult Decrypt(const std::vector<unsigned char>& container,
                         std::wstring_view passphrase) override;

    std::string ArmorMessage(const std::vector<unsigned char>& container,
                             const CryptoOptions& options) override;

    bool UnarmorMessage(std::string_view text,
                        std::vector<unsigned char>& container) override;

    CryptoResult EncryptFileOnDisk(const std::wstring& srcPath,
                                   const std::wstring& dstPath,
                                   std::wstring_view passphrase,
                                   const CryptoOptions& options) override;

    CryptoResult DecryptFileOnDisk(const std::wstring& srcPath,
                                   const std::wstring& dstPath,
                                   std::wstring_view passphrase) override;
};

}  // namespace ArgpiSuite::Crypto

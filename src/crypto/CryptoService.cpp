#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "CryptoService.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "EncodingService.h"
#include "password/CryptoHash.h"
#include "plugin/TextUtil.h"

#pragma comment(lib, "bcrypt.lib")

#ifndef STATUS_AUTH_TAG_MISMATCH
#define STATUS_AUTH_TAG_MISMATCH ((NTSTATUS)0xC000A002L)
#endif

namespace ArgpiSuite::Crypto {
namespace {

constexpr uint8_t kMagic[8] = {'A', 'R', 'G', 'P', 'I', 'C', 'R', '1'};
constexpr uint8_t kVersion = 0x01;

enum CipherId : uint8_t {
    CipherAes256Gcm = 0x01,
    CipherAes256Cbc = 0x02,
    CipherAes128Gcm = 0x03,
    CipherAes128Cbc = 0x04,
};

enum KdfId : uint8_t {
    KdfPbkdf2Sha256 = 0x01,
    KdfArgon2id     = 0x02,
};

CipherId ParseCipher(std::wstring_view name) {
    if (Plugin::EqualsIgnoreCase(name, L"aes-256-cbc")) return CipherAes256Cbc;
    if (Plugin::EqualsIgnoreCase(name, L"aes-128-gcm")) return CipherAes128Gcm;
    if (Plugin::EqualsIgnoreCase(name, L"aes-128-cbc")) return CipherAes128Cbc;
    return CipherAes256Gcm;
}

std::wstring CipherName(CipherId id) {
    switch (id) {
        case CipherAes256Gcm: return L"AES-256-GCM";
        case CipherAes256Cbc: return L"AES-256-CBC";
        case CipherAes128Gcm: return L"AES-128-GCM";
        case CipherAes128Cbc: return L"AES-128-CBC";
        default: return L"Unknown";
    }
}

KdfId ParseKdf(std::wstring_view name) {
    if (Plugin::EqualsIgnoreCase(name, L"argon2id")) return KdfArgon2id;
    return KdfPbkdf2Sha256;
}

std::wstring KdfName(KdfId id) {
    switch (id) {
        case KdfPbkdf2Sha256: return L"PBKDF2-SHA256";
        case KdfArgon2id:     return L"Argon2id";
        default:              return L"Unknown";
    }
}

size_t CipherKeySize(CipherId id) {
    switch (id) {
        case CipherAes128Gcm:
        case CipherAes128Cbc:
            return 16;
        default:
            return 32;
    }
}

size_t CipherNonceSize(CipherId id) {
    switch (id) {
        case CipherAes256Gcm:
        case CipherAes128Gcm:
            return 12; // 96-bit standard GCM nonce
        default:
            return 16; // 128-bit CBC IV
    }
}

size_t CipherTagSize(CipherId id) {
    switch (id) {
        case CipherAes256Gcm:
        case CipherAes128Gcm:
            return 16; // 128-bit GCM tag
        default:
            return 32; // 256-bit HMAC-SHA256 tag
    }
}

bool DeriveKeyMaterial(KdfId kdf, std::wstring_view passphrase, const uint8_t* salt,
                       size_t saltLen, int iterations, int argon2t, int argon2m, int argon2p,
                       uint8_t* keyOut, size_t keyLen) {
    const std::string passUtf8 = Plugin::ToUtf8(passphrase);
    if (kdf == KdfArgon2id) {
        return Password::Argon2idRaw(reinterpret_cast<const uint8_t*>(passUtf8.data()),
                                     passUtf8.size(), salt, saltLen, argon2t, argon2m,
                                     argon2p, keyOut, keyLen);
    }
    return Password::Pbkdf2Sha256Raw(reinterpret_cast<const uint8_t*>(passUtf8.data()),
                                     passUtf8.size(), salt, saltLen, iterations, keyOut, keyLen);
}

// Compute HMAC-SHA256 using CNG
bool ComputeHmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen,
                       uint8_t* tagOut32) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                    BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(status)) {
        return false;
    }

    BCRYPT_HASH_HANDLE hHash = nullptr;
    status = ::BCryptCreateHash(hAlg, &hHash, nullptr, 0, const_cast<PUCHAR>(key),
                                static_cast<ULONG>(keyLen), 0);
    if (BCRYPT_SUCCESS(status)) {
        status = ::BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(dataLen), 0);
        if (BCRYPT_SUCCESS(status)) {
            status = ::BCryptFinishHash(hHash, tagOut32, 32, 0);
        }
        ::BCryptDestroyHash(hHash);
    }
    ::BCryptCloseAlgorithmProvider(hAlg, 0);
    return BCRYPT_SUCCESS(status);
}

}  // namespace

CryptoService& CryptoService::Instance() {
    static CryptoService instance;
    return instance;
}

CryptoResult CryptoService::Encrypt(const std::vector<unsigned char>& plain,
                                   std::wstring_view passphrase,
                                   const CryptoOptions& options) {
    CryptoResult result;
    if (passphrase.empty()) {
        result.status = CryptoStatus::InvalidArgument;
        result.message = L"Passphrase cannot be empty.";
        return result;
    }

    const CipherId cipherId = ParseCipher(options.cipher);
    const KdfId kdfId = ParseKdf(options.kdf);
    const size_t keySize = CipherKeySize(cipherId);
    const size_t nonceSize = CipherNonceSize(cipherId);
    const size_t tagSize = CipherTagSize(cipherId);
    const bool isGcm = (cipherId == CipherAes256Gcm || cipherId == CipherAes128Gcm);

    // 1. Generate 16-byte random salt
    uint8_t salt[16] = {};
    if (!Password::SecureRandomBytes(salt, sizeof(salt))) {
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to generate cryptographic random salt.";
        return result;
    }

    // 2. Generate random nonce/IV
    std::vector<uint8_t> nonce(nonceSize);
    if (!Password::SecureRandomBytes(nonce.data(), nonce.size())) {
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to generate cryptographic random nonce.";
        return result;
    }

    // 3. Derive key material (if CBC, derive keySize + 32 bytes for HMAC)
    const size_t deriveLen = isGcm ? keySize : (keySize + 32);
    std::vector<uint8_t> keyMaterial(deriveLen);
    const bool kdfOk = DeriveKeyMaterial(kdfId, passphrase, salt, sizeof(salt),
                                         options.iterations, options.argon2t, options.argon2m,
                                         options.argon2p, keyMaterial.data(), deriveLen);
    if (!kdfOk) {
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Key derivation failed.";
        return result;
    }

    // 4. Open AES CNG algorithm provider
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to initialize AES algorithm provider.";
        return result;
    }

    LPCWSTR chainMode = isGcm ? BCRYPT_CHAIN_MODE_GCM : BCRYPT_CHAIN_MODE_CBC;
    status = ::BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                 reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainMode)),
                                 static_cast<ULONG>((::lstrlenW(chainMode) + 1) * sizeof(wchar_t)), 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to set AES chaining mode.";
        return result;
    }

    // 5. Generate symmetric key
    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = ::BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, keyMaterial.data(),
                                          static_cast<ULONG>(keySize), 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to generate symmetric key.";
        return result;
    }

    // Prepare container header up to tag
    // Magic (8) + Version (1) + CipherId (1) + KdfId (1) + Iterations (4) + SaltLen (1) + Salt (16) + NonceLen (1) + Nonce (12/16)
    std::vector<uint8_t> headerAad;
    headerAad.insert(headerAad.end(), kMagic, kMagic + 8);
    headerAad.push_back(kVersion);
    headerAad.push_back(static_cast<uint8_t>(cipherId));
    headerAad.push_back(static_cast<uint8_t>(kdfId));
    uint32_t itersWord = static_cast<uint32_t>(options.iterations);
    if (kdfId == KdfArgon2id) {
        const uint32_t t = static_cast<uint32_t>(options.argon2t & 0xFF);
        const uint32_t m = static_cast<uint32_t>(options.argon2m & 0xFFFF);
        const uint32_t p = static_cast<uint32_t>(options.argon2p & 0xFF);
        itersWord = t | (m << 8) | (p << 24);
    }
    headerAad.push_back(static_cast<uint8_t>(itersWord & 0xFF));
    headerAad.push_back(static_cast<uint8_t>((itersWord >> 8) & 0xFF));
    headerAad.push_back(static_cast<uint8_t>((itersWord >> 16) & 0xFF));
    headerAad.push_back(static_cast<uint8_t>((itersWord >> 24) & 0xFF));
    headerAad.push_back(static_cast<uint8_t>(sizeof(salt)));
    headerAad.insert(headerAad.end(), salt, salt + sizeof(salt));
    headerAad.push_back(static_cast<uint8_t>(nonce.size()));
    headerAad.insert(headerAad.end(), nonce.begin(), nonce.end());

    std::vector<uint8_t> tag(tagSize, 0);
    std::vector<uint8_t> ciphertext;

    if (isGcm) {
        // Query ciphertext length
        DWORD cbCipher = 0;
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = nonce.data();
        authInfo.cbNonce = static_cast<ULONG>(nonce.size());
        authInfo.pbAuthData = headerAad.data();
        authInfo.cbAuthData = static_cast<ULONG>(headerAad.size());
        authInfo.pbTag = tag.data();
        authInfo.cbTag = static_cast<ULONG>(tag.size());

        ciphertext.resize(plain.size());
        status = ::BCryptEncrypt(
            hKey, const_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()),
            &authInfo, nullptr, 0, ciphertext.data(), static_cast<ULONG>(ciphertext.size()),
            &cbCipher, 0);

        if (!BCRYPT_SUCCESS(status)) {
            ::BCryptDestroyKey(hKey);
            ::BCryptCloseAlgorithmProvider(hAlg, 0);
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            result.status = CryptoStatus::Failure;
            result.message = L"BCrypt AES-GCM encryption failed.";
            return result;
        }
        ciphertext.resize(cbCipher);
    } else {
        // CBC with PKCS#7 block padding
        DWORD cbCipher = 0;
        status = ::BCryptEncrypt(hKey, const_cast<PUCHAR>(plain.data()),
                                 static_cast<ULONG>(plain.size()), nullptr,
                                 nonce.data(), static_cast<ULONG>(nonce.size()),
                                 nullptr, 0, &cbCipher, BCRYPT_BLOCK_PADDING);
        if (!BCRYPT_SUCCESS(status)) {
            ::BCryptDestroyKey(hKey);
            ::BCryptCloseAlgorithmProvider(hAlg, 0);
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            result.status = CryptoStatus::Failure;
            result.message = L"BCrypt AES-CBC length query failed.";
            return result;
        }

        ciphertext.resize(cbCipher);
        std::vector<uint8_t> ivCopy = nonce;
        status = ::BCryptEncrypt(hKey, const_cast<PUCHAR>(plain.data()),
                                 static_cast<ULONG>(plain.size()), nullptr,
                                 ivCopy.data(), static_cast<ULONG>(ivCopy.size()),
                                 ciphertext.data(), static_cast<ULONG>(ciphertext.size()),
                                 &cbCipher, BCRYPT_BLOCK_PADDING);
        if (!BCRYPT_SUCCESS(status)) {
            ::BCryptDestroyKey(hKey);
            ::BCryptCloseAlgorithmProvider(hAlg, 0);
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            result.status = CryptoStatus::Failure;
            result.message = L"BCrypt AES-CBC encryption failed.";
            return result;
        }
        ciphertext.resize(cbCipher);

        // Compute HMAC-SHA256 over headerAad + ciphertext using HMAC key
        const uint8_t* hmacKey = keyMaterial.data() + keySize;
        std::vector<uint8_t> hmacInput = headerAad;
        hmacInput.insert(hmacInput.end(), ciphertext.begin(), ciphertext.end());
        ComputeHmacSha256(hmacKey, 32, hmacInput.data(), hmacInput.size(), tag.data());
    }

    ::BCryptDestroyKey(hKey);
    ::BCryptCloseAlgorithmProvider(hAlg, 0);
    ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());

    // 6. Build final binary container
    std::vector<unsigned char> container = std::move(headerAad);
    container.push_back(static_cast<uint8_t>(tag.size()));
    container.insert(container.end(), tag.begin(), tag.end());

    // 8 bytes ciphertext length (little endian)
    const uint64_t cipherLen = ciphertext.size();
    for (int i = 0; i < 8; ++i) {
        container.push_back(static_cast<uint8_t>((cipherLen >> (i * 8)) & 0xFF));
    }
    container.insert(container.end(), ciphertext.begin(), ciphertext.end());

    result.status = CryptoStatus::Ok;
    result.bytes = std::move(container);
    result.message = L"Encrypted successfully with " + CipherName(cipherId) + L".";
    return result;
}

CryptoResult CryptoService::Decrypt(const std::vector<unsigned char>& container,
                                   std::wstring_view passphrase) {
    CryptoResult result;
    if (passphrase.empty()) {
        result.status = CryptoStatus::InvalidArgument;
        result.message = L"Passphrase cannot be empty.";
        return result;
    }

    // Minimum header: Magic(8) + Ver(1) + Cipher(1) + Kdf(1) + Iters(4) + SaltLen(1) + Salt(16) + NonceLen(1) + Nonce(12) + TagLen(1) + Tag(16) + Len(8) = 70
    if (container.size() < 70) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Data is too short to be a valid ArgpiSuite encrypted container.";
        return result;
    }

    // Verify magic
    if (std::memcmp(container.data(), kMagic, 8) != 0) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Invalid container magic header.";
        return result;
    }

    size_t offset = 8;
    const uint8_t version = container[offset++];
    if (version != kVersion) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Unsupported container version.";
        return result;
    }

    const CipherId cipherId = static_cast<CipherId>(container[offset++]);
    const KdfId kdfId = static_cast<KdfId>(container[offset++]);

    uint32_t iterations = 0;
    for (int i = 0; i < 4; ++i) {
        iterations |= (static_cast<uint32_t>(container[offset++]) << (i * 8));
    }
    int pbkdf2Iters = static_cast<int>(iterations);
    int argon2t = 2;
    int argon2m = 19456;
    int argon2p = 1;
    if (kdfId == KdfArgon2id) {
        argon2t = static_cast<int>(iterations & 0xFF);
        argon2m = static_cast<int>((iterations >> 8) & 0xFFFF);
        argon2p = static_cast<int>((iterations >> 24) & 0xFF);
        if (argon2t < 1) argon2t = 2;
        if (argon2m < 8) argon2m = 19456;
        if (argon2p < 1) argon2p = 1;
    }

    const uint8_t saltLen = container[offset++];
    if (offset + saltLen > container.size()) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Corrupted container salt field.";
        return result;
    }
    const uint8_t* salt = container.data() + offset;
    offset += saltLen;

    const uint8_t nonceLen = container[offset++];
    if (offset + nonceLen > container.size()) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Corrupted container nonce field.";
        return result;
    }
    const uint8_t* nonce = container.data() + offset;
    offset += nonceLen;

    // headerAad is everything from start up to offset (before tagLen)
    const std::vector<uint8_t> headerAad(container.begin(), container.begin() + offset);

    const uint8_t tagLen = container[offset++];
    if (offset + tagLen > container.size()) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Corrupted container authentication tag.";
        return result;
    }
    const uint8_t* tag = container.data() + offset;
    offset += tagLen;

    uint64_t cipherLen = 0;
    for (int i = 0; i < 8; ++i) {
        cipherLen |= (static_cast<uint64_t>(container[offset++]) << (i * 8));
    }

    if (offset + cipherLen > container.size()) {
        result.status = CryptoStatus::InvalidContainer;
        result.message = L"Ciphertext size mismatch (truncated container).";
        return result;
    }
    const uint8_t* ciphertext = container.data() + offset;

    const bool isGcm = (cipherId == CipherAes256Gcm || cipherId == CipherAes128Gcm);
    const size_t keySize = CipherKeySize(cipherId);
    const size_t deriveLen = isGcm ? keySize : (keySize + 32);

    // Derive key material
    std::vector<uint8_t> keyMaterial(deriveLen);
    const bool kdfOk = DeriveKeyMaterial(kdfId, passphrase, salt, saltLen, pbkdf2Iters,
                                         argon2t, argon2m, argon2p, keyMaterial.data(), deriveLen);
    if (!kdfOk) {
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Key derivation failed.";
        return result;
    }

    // If CBC, verify HMAC-SHA256 before decrypting!
    if (!isGcm) {
        if (tagLen != 32) {
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            result.status = CryptoStatus::AuthFailed;
            result.message = L"Decryption failed: Incorrect passphrase or authentication check failed.";
            return result;
        }
        const uint8_t* hmacKey = keyMaterial.data() + keySize;
        std::vector<uint8_t> hmacInput = headerAad;
        hmacInput.insert(hmacInput.end(), ciphertext, ciphertext + cipherLen);
        uint8_t computedTag[32] = {};
        ComputeHmacSha256(hmacKey, 32, hmacInput.data(), hmacInput.size(), computedTag);

        if (!Password::ConstantTimeCompare(tag, computedTag, 32)) {
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            result.status = CryptoStatus::AuthFailed;
            result.message = L"Decryption failed: Incorrect passphrase or corrupted data.";
            return result;
        }
    }

    // Initialize AES CNG provider
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = ::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to initialize AES algorithm provider.";
        return result;
    }

    LPCWSTR chainMode = isGcm ? BCRYPT_CHAIN_MODE_GCM : BCRYPT_CHAIN_MODE_CBC;
    status = ::BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                 reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(chainMode)),
                                 static_cast<ULONG>((::lstrlenW(chainMode) + 1) * sizeof(wchar_t)), 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to set AES chaining mode.";
        return result;
    }

    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = ::BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, keyMaterial.data(),
                                          static_cast<ULONG>(keySize), 0);
    if (!BCRYPT_SUCCESS(status)) {
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
        ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to generate symmetric key.";
        return result;
    }

    std::vector<unsigned char> plaintext;
    DWORD cbPlain = 0;

    if (isGcm) {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
        BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
        authInfo.pbNonce = const_cast<PUCHAR>(nonce);
        authInfo.cbNonce = static_cast<ULONG>(nonceLen);
        authInfo.pbAuthData = const_cast<PUCHAR>(headerAad.data());
        authInfo.cbAuthData = static_cast<ULONG>(headerAad.size());
        authInfo.pbTag = const_cast<PUCHAR>(tag);
        authInfo.cbTag = static_cast<ULONG>(tagLen);

        plaintext.resize(static_cast<size_t>(cipherLen));
        status = ::BCryptDecrypt(hKey, const_cast<PUCHAR>(ciphertext),
                                 static_cast<ULONG>(cipherLen), &authInfo,
                                 nullptr, 0, plaintext.data(),
                                 static_cast<ULONG>(plaintext.size()), &cbPlain, 0);

        if (status == STATUS_AUTH_TAG_MISMATCH || !BCRYPT_SUCCESS(status)) {
            ::BCryptDestroyKey(hKey);
            ::BCryptCloseAlgorithmProvider(hAlg, 0);
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            ::SecureZeroMemory(plaintext.data(), plaintext.size());
            result.status = CryptoStatus::AuthFailed;
            result.message = L"Decryption failed: Incorrect passphrase or authentication check failed.";
            return result;
        }
        plaintext.resize(cbPlain);
    } else {
        std::vector<uint8_t> ivCopy(nonce, nonce + nonceLen);
        plaintext.resize(static_cast<size_t>(cipherLen));
        status = ::BCryptDecrypt(hKey, const_cast<PUCHAR>(ciphertext),
                                 static_cast<ULONG>(cipherLen), nullptr,
                                 ivCopy.data(), static_cast<ULONG>(ivCopy.size()),
                                 plaintext.data(), static_cast<ULONG>(plaintext.size()),
                                 &cbPlain, BCRYPT_BLOCK_PADDING);

        if (!BCRYPT_SUCCESS(status)) {
            ::BCryptDestroyKey(hKey);
            ::BCryptCloseAlgorithmProvider(hAlg, 0);
            ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());
            ::SecureZeroMemory(plaintext.data(), plaintext.size());
            result.status = CryptoStatus::AuthFailed;
            result.message = L"Decryption failed: Incorrect passphrase or padding check failed.";
            return result;
        }
        plaintext.resize(cbPlain);
    }

    ::BCryptDestroyKey(hKey);
    ::BCryptCloseAlgorithmProvider(hAlg, 0);
    ::SecureZeroMemory(keyMaterial.data(), keyMaterial.size());

    result.status = CryptoStatus::Ok;
    result.bytes = std::move(plaintext);
    result.message = L"Decrypted successfully.";
    return result;
}

std::string CryptoService::ArmorMessage(const std::vector<unsigned char>& container,
                                       const CryptoOptions& options) {
    Base64Options b64Opts;
    b64Opts.lineWrap = 64;
    const EncodingResult b64Res = EncodingService::Instance().EncodeBase64(container, b64Opts);

    const CipherId cId = ParseCipher(options.cipher);
    const KdfId kId = ParseKdf(options.kdf);

    std::ostringstream ss;
    ss << "-----BEGIN ARGPISUITE ENCRYPTED MESSAGE-----\r\n";
    ss << "Version: 1\r\n";
    ss << "Cipher: " << Plugin::ToUtf8(CipherName(cId)) << "\r\n";
    ss << "KDF: " << Plugin::ToUtf8(KdfName(kId)) << "\r\n\r\n";
    ss << b64Res.text << "\r\n";
    ss << "-----END ARGPISUITE ENCRYPTED MESSAGE-----\r\n";
    return ss.str();
}

bool CryptoService::UnarmorMessage(std::string_view text,
                                  std::vector<unsigned char>& container) {
    std::string_view clean = text;

    // Check for BEGIN banner
    const std::string_view beginTag = "-----BEGIN ARGPISUITE ENCRYPTED MESSAGE-----";
    const std::string_view endTag = "-----END ARGPISUITE ENCRYPTED MESSAGE-----";

    const size_t beginPos = clean.find(beginTag);
    if (beginPos != std::string_view::npos) {
        const size_t endPos = clean.find(endTag, beginPos + beginTag.size());
        if (endPos != std::string_view::npos) {
            std::string_view body = clean.substr(beginPos + beginTag.size(),
                                                endPos - (beginPos + beginTag.size()));
            // Find empty line separating headers from base64
            const size_t dnl = body.find("\n\n");
            const size_t drnl = body.find("\r\n\r\n");
            if (drnl != std::string_view::npos) {
                body = body.substr(drnl + 4);
            } else if (dnl != std::string_view::npos) {
                body = body.substr(dnl + 2);
            }
            const EncodingResult b64Res = EncodingService::Instance().DecodeBase64(body);
            if (b64Res.ok && b64Res.bytes.size() >= 8 &&
                std::memcmp(b64Res.bytes.data(), kMagic, 8) == 0) {
                container = std::move(b64Res.bytes);
                return true;
            }
        }
    }

    // Try decoding entire string as Base64 directly
    const EncodingResult b64Direct = EncodingService::Instance().DecodeBase64(clean);
    if (b64Direct.ok && b64Direct.bytes.size() >= 8 &&
        std::memcmp(b64Direct.bytes.data(), kMagic, 8) == 0) {
        container = std::move(b64Direct.bytes);
        return true;
    }

    // Direct binary check
    if (clean.size() >= 8 && std::memcmp(clean.data(), kMagic, 8) == 0) {
        container.assign(clean.begin(), clean.end());
        return true;
    }

    return false;
}

CryptoResult CryptoService::EncryptFileOnDisk(const std::wstring& srcPath,
                                               const std::wstring& dstPath,
                                               std::wstring_view passphrase,
                                               const CryptoOptions& options) {
    CryptoResult result;
    std::ifstream in(srcPath, std::ios::binary);
    if (!in.is_open()) {
        result.status = CryptoStatus::InvalidArgument;
        result.message = L"Could not open source file for reading.";
        return result;
    }

    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(buffer.data()), size);
    }
    in.close();

    CryptoResult encRes = Encrypt(buffer, passphrase, options);
    if (encRes.status != CryptoStatus::Ok) {
        return encRes;
    }

    // Write to temporary file first for atomic replacement
    const std::wstring tmpPath = dstPath + L".tmp";
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        result.status = CryptoStatus::Failure;
        result.message = L"Could not create temporary output file.";
        return result;
    }
    out.write(reinterpret_cast<const char*>(encRes.bytes.data()), encRes.bytes.size());
    out.close();

    if (!::MoveFileExW(tmpPath.c_str(), dstPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to replace destination file.";
        return result;
    }

    result.status = CryptoStatus::Ok;
    result.message = L"File encrypted successfully (" + std::to_wstring(encRes.bytes.size()) + L" bytes).";
    return result;
}

CryptoResult CryptoService::DecryptFileOnDisk(const std::wstring& srcPath,
                                               const std::wstring& dstPath,
                                               std::wstring_view passphrase) {
    CryptoResult result;
    std::ifstream in(srcPath, std::ios::binary);
    if (!in.is_open()) {
        result.status = CryptoStatus::InvalidArgument;
        result.message = L"Could not open encrypted file for reading.";
        return result;
    }

    in.seekg(0, std::ios::end);
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<unsigned char> raw(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(raw.data()), size);
    }
    in.close();

    std::vector<unsigned char> container;
    const std::string_view rawStr(reinterpret_cast<const char*>(raw.data()), raw.size());
    if (!UnarmorMessage(rawStr, container)) {
        container = std::move(raw);
    }

    CryptoResult decRes = Decrypt(container, passphrase);
    if (decRes.status != CryptoStatus::Ok) {
        return decRes;
    }

    const std::wstring tmpPath = dstPath + L".tmp";
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        result.status = CryptoStatus::Failure;
        result.message = L"Could not create temporary output file.";
        return result;
    }
    out.write(reinterpret_cast<const char*>(decRes.bytes.data()), decRes.bytes.size());
    out.close();

    if (!::MoveFileExW(tmpPath.c_str(), dstPath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        result.status = CryptoStatus::Failure;
        result.message = L"Failed to replace destination file.";
        return result;
    }

    result.status = CryptoStatus::Ok;
    result.message = L"File decrypted successfully (" + std::to_wstring(decRes.bytes.size()) + L" bytes).";
    return result;
}

}  // namespace ArgpiSuite::Crypto

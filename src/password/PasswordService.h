// ArgpiSuite Password & Cryptographic Hashing Service.
// Implements all features of the offline HTML Password Generator:
// - Password generation with complexity profiles, character classes, exclusions
// - Password strength and entropy calculation
// - Diceware passphrase generation
// - Hashing: BCrypt (Joomla & Custom), Argon2id, SHA-256, SHA-512, MD5, SHA-1,
//   SHA-256 with secret key, PBKDF2-SHA256 with secret key
// - Hash verification (BCrypt and Argon2id)
#pragma once

#include <string>
#include <vector>

#include "CryptoHash.h"

namespace ArgpiSuite::Password {

enum class Complexity : int {
    Basic = 0,      // Lowercase only, length 12
    Balanced = 1,   // Lowercase, Uppercase, Numbers, length 16
    Strong = 2,     // Lowercase, Uppercase, Numbers, Symbols, length 20
    Elite = 3       // Lowercase, Uppercase, Numbers, Symbols, Exclude Ambiguous, length 32
};

enum class Preset : int {
    Custom = 0,
    Joomla = 1,
    WordPress = 2,
    Drupal = 3,
    Magento = 4
};

enum class Algorithm : int {
    JoomlaBcrypt = 0,
    CustomBcrypt = 1,
    Argon2id = 2,
    Sha256 = 3,
    Sha512 = 4,
    Md5 = 5,
    Sha1 = 6,
    Sha256Secret = 7,
    Pbkdf2Secret = 8
};

struct PasswordPolicy {
    int length = 16;
    bool useLower = true;
    bool useUpper = true;
    bool useDigits = true;
    bool useSymbols = false;
    bool avoidAmbiguous = false;
    std::wstring excludeChars;
};

struct PasswordResult {
    bool ok = false;
    std::wstring value;
    std::wstring message;
    int poolSize = 0;
    double entropyBits = 0.0;
    std::wstring strengthLabel;
};

struct HashParameters {
    Algorithm algorithm = Algorithm::JoomlaBcrypt;
    std::wstring secretKey;
    int bcryptCost = 10;
    Argon2idParams argon2{2, 19456, 1, 32};
    int pbkdf2Iterations = 200000;
    int pbkdf2KeyLength = 32;
};

struct HashResult {
    bool ok = false;
    std::wstring hash;
    std::wstring algorithmInfo;
    int hashLength = 0;
    std::wstring warning;
    std::wstring errorMessage;
};

struct VerifyResult {
    bool attempted = false;
    bool match = false;
    std::wstring message;
};

// --- Helpers & Profiles --------------------------------------------------

PasswordPolicy PolicyForComplexity(Complexity complexity);
std::wstring ComplexityName(Complexity complexity);
Complexity ParseComplexity(const std::wstring& name);

void ApplyPreset(Preset preset, Algorithm& alg, std::wstring& secretKey);
std::wstring PresetName(Preset preset);
Preset MatchPreset(Algorithm alg, const std::wstring& secretKey);

std::wstring AlgorithmName(Algorithm alg);
std::wstring AlgorithmDisplayName(Algorithm alg);
std::wstring AlgorithmDescription(Algorithm alg);
Algorithm ParseAlgorithm(const std::wstring& name);

int EstimatePoolSize(const std::wstring& password);
double EstimateEntropy(const std::wstring& password, int poolSize = 0);
std::wstring StrengthRating(double entropyBits);

// --- Password & Hash Service Interface -----------------------------------

class PasswordService {
public:
    static PasswordService& Instance();

    /// Generates a password conforming to the policy with guaranteed class distribution.
    PasswordResult Generate(const PasswordPolicy& policy);

    /// Generates a passphrase with `words` count separated by `separator`.
    PasswordResult GeneratePassphrase(int words, wchar_t separator);

    /// Computes the hash for the given password and parameters.
    HashResult ComputeHash(const std::wstring& password, const HashParameters& params);

    /// Verifies a password against a hash string ($2y$, $2a$, $2b$, or $argon2id$).
    VerifyResult Verify(const std::wstring& password, const std::wstring& hash);
};

}  // namespace ArgpiSuite::Password

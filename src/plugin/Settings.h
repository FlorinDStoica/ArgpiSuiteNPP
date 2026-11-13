// ArgpiSuite plugin — INI backed settings.
// Everything lives in <plugin config dir>\ArgpiSuite.ini (UTF-16).
#pragma once

#include <windows.h>

#include <string>

#include "format/FormatOptions.h"

namespace ArgpiSuite::Plugin {

/// Settings for the Encrypt / Decrypt submenu.
struct CryptoSettings {
    bool enabled = true;
    std::wstring cipher = L"aes-256-gcm";
    std::wstring kdf = L"pbkdf2-sha256";
    int iterations = 600000;
    bool armorBase64 = true;
};

/// Settings for the Encoding / Decoding submenu.
struct EncodingSettings {
    std::wstring customAlphabet = L"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    bool base16Uppercase = true;
    std::wstring base16Delimiter;
    bool base64UrlSafe = false;
    bool base64Padding = true;
    std::wstring base85Variant = L"ascii85";
};

/// Settings for the Generate Password submenu.
struct PasswordSettings {
    int length = 16;
    bool useLower = true;
    bool useUpper = true;
    bool useDigits = true;
    bool useSymbols = false;
    bool avoidAmbiguous = false;
    std::wstring excludeChars;
    std::wstring complexity = L"balanced";
    std::wstring preset = L"joomla";
    std::wstring algorithm = L"joomla_bcrypt";
    bool rememberSecret = false;
    std::wstring secretKey;
    int bcryptCost = 10;
    int argon2t = 2;
    int argon2m = 19456;
    int argon2p = 1;
    int argon2HashLength = 32;
    int pbkdf2Iterations = 200000;
    int pbkdf2KeyLength = 32;
    bool autoClearClipboard = true;
    int clipboardClearSeconds = 30;

    int minSymbols = 2;
    int minDigits = 2;
    int words = 4;
    std::wstring separator = L"-";
};

/// Settings for the UUID / GUID & ULID Generator submenu.
struct GeneratorSettings {
    std::wstring defaultUuidVersion = L"v4";  // "v4" or "v7"
    bool uuidUppercase = false;
    bool uuidHyphens = true;
    bool uuidBraces = false;
    bool guidUppercase = true;
    bool guidBraces = true;
    bool ulidLowercase = false;
    std::wstring bulkType = L"uuid_v4";
    int bulkCount = 10;
    std::wstring bulkSeparator = L"newline";
    std::wstring quotes = L"none";
};

/// Settings for the Hashing submenu.
struct HashSettings {
    bool uppercase = false;
    std::wstring quickAction = L"copy";  // "copy" or "insert"
    std::wstring lastAlgorithm = L"sha256";
    bool hmacKeyIsHex = false;
};

struct Settings {
    Format::FormatOptions format;

    // Plugin-only format knobs.
    bool formatOnSave = false;
    std::wstring extensionFilter =
        L"json,jsonc,json5,css,scss,less,js,mjs,cjs,html,htm,xhtml,xml,php,java,kt,kts,gradle,c,h,cpp,hpp,cc,cxx";
    bool requireConfig = false;
    std::wstring ignoreFile;
    std::wstring logLevel = L"info";

    CryptoSettings crypto;
    PasswordSettings password;
    EncodingSettings encoding;
    GeneratorSettings generator;
    HashSettings hashing;

    /// Absolute path of the INI file this struct was loaded from.
    std::wstring path;

    /// True when any key was read from disk (false on the very first run).
    bool loadedFromDisk = false;

    /// Reads every key, then creates the file with defaults if it is missing.
    void Load(const std::wstring& iniPath);

    /// Writes every key and creates the parent directory when needed.
    void Save() const;

    /// Human readable dump of the effective formatting options.
    std::wstring Describe() const;
};

/// `%NPP_PLUGINSCONFIGDIR%\ArgpiSuite.ini`, or the DLL directory when Notepad++
/// does not answer the message.
std::wstring DefaultIniPath(HWND nppHandle, HMODULE module);
std::wstring ModuleDirectory(HMODULE module);

}  // namespace ArgpiSuite::Plugin

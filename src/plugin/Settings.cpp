#include "Settings.h"

#include <windows.h>

#include <iterator>
#include <filesystem>
#include <string>

#include "TextUtil.h"
#include "Notepad_plus_msgs.h"
#include "format/FormatLineEnding.h"

namespace ArgpiSuite::Plugin {
namespace {

constexpr const wchar_t* kFormatSection = L"Format";
constexpr const wchar_t* kCryptoSection = L"Crypto";
constexpr const wchar_t* kPasswordSection = L"Password";
constexpr const wchar_t* kEncodingSection = L"Encoding";
constexpr const wchar_t* kGeneratorSection = L"Generator";
constexpr const wchar_t* kHashingSection = L"Hashing";

std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                        const std::wstring& fallback) {
    wchar_t buffer[4096] = {};
    const DWORD copied = ::GetPrivateProfileStringW(section, key, fallback.c_str(), buffer,
                                                    static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (copied == 0) {
        return fallback;
    }
    return std::wstring(buffer, copied);
}

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(
        ::GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

bool ReadBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool fallback) {
    return ReadInt(path, section, key, fallback ? 1 : 0) != 0;
}

void WriteString(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                 const std::wstring& value) {
    ::WritePrivateProfileStringW(section, key, value.c_str(), path.c_str());
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value) {
    WriteString(path, section, key, std::to_wstring(value));
}

void WriteBool(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool value) {
    WriteInt(path, section, key, value ? 1 : 0);
}

/// WritePrivateProfileStringW keeps the encoding of an existing file, so a
/// fresh file is created as UTF-16LE to keep non-ASCII settings intact.
void EnsureParentDirectory(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path.substr(0, slash)), error);
}

void EnsureUtf16File(const std::wstring& path) {
    EnsureParentDirectory(path);
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return;
    }
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    const unsigned char bom[2] = {0xFF, 0xFE};
    DWORD written = 0;
    ::WriteFile(file, bom, sizeof(bom), &written, nullptr);
    ::CloseHandle(file);
}

Format::Eol ParseEolName(const std::wstring& name) {
    const std::string utf8 = ToUtf8(name);
    Format::Eol eol = Format::Eol::None;
    if (Format::ParseEol(utf8, eol)) {
        return eol;
    }
    return Format::Eol::None;
}

}  // namespace

std::wstring ModuleDirectory(HMODULE module) {
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD length = ::GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return {};
    }
    std::wstring path(buffer, length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(0, slash);
}

std::wstring DefaultIniPath(HWND nppHandle, HMODULE module) {
    wchar_t directory[MAX_PATH * 4] = {};
    if (nppHandle != nullptr) {
        ::SendMessageW(nppHandle, NPPM_GETPLUGINSCONFIGDIR, static_cast<WPARAM>(std::size(directory)),
                       reinterpret_cast<LPARAM>(directory));
    }
    std::wstring base(directory);
    if (base.empty()) {
        base = ModuleDirectory(module);
    }
    if (base.empty()) {
        return L"ArgpiSuite.ini";
    }
    if (base.back() != L'\\' && base.back() != L'/') {
        base.push_back(L'\\');
    }
    base.append(L"ArgpiSuite.ini");
    return base;
}

void Settings::Load(const std::wstring& iniPath) {
    path = iniPath;
    loadedFromDisk = ::GetFileAttributesW(iniPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    format.printWidth = ReadInt(path, kFormatSection, L"printWidth", format.printWidth);
    format.tabWidth = ReadInt(path, kFormatSection, L"tabWidth", format.tabWidth);
    format.useTabs = ReadBool(path, kFormatSection, L"useTabs", format.useTabs);
    format.minify = ReadBool(path, kFormatSection, L"minify", format.minify);
    format.semi = ReadBool(path, kFormatSection, L"semi", format.semi);
    format.singleQuote = ReadBool(path, kFormatSection, L"singleQuote", format.singleQuote);
    format.jsxSingleQuote = ReadBool(path, kFormatSection, L"jsxSingleQuote", format.jsxSingleQuote);
    format.quoteProps = Format::QuoteProps::AsNeeded;
    Format::ParseQuoteProps(ToUtf8(ReadString(path, kFormatSection, L"quoteProps",
                                               ToWide(Format::ToString(format.quoteProps)))),
                            format.quoteProps);
    format.trailingComma = Format::TrailingComma::All;
    Format::ParseTrailingComma(ToUtf8(ReadString(path, kFormatSection, L"trailingComma",
                                                 ToWide(Format::ToString(format.trailingComma)))),
                               format.trailingComma);
    format.bracketSpacing = ReadBool(path, kFormatSection, L"bracketSpacing", format.bracketSpacing);
    format.bracketSameLine =
        ReadBool(path, kFormatSection, L"bracketSameLine", format.bracketSameLine);
    format.arrowParens = Format::ArrowParens::Always;
    Format::ParseArrowParens(ToUtf8(ReadString(path, kFormatSection, L"arrowParens",
                                               ToWide(Format::ToString(format.arrowParens)))),
                             format.arrowParens);
    format.proseWrap = Format::ProseWrap::Preserve;
    Format::ParseProseWrap(ToUtf8(ReadString(path, kFormatSection, L"proseWrap",
                                             ToWide(Format::ToString(format.proseWrap)))),
                           format.proseWrap);
    format.htmlWhitespaceSensitivity = Format::HtmlWhitespaceSensitivity::Css;
    Format::ParseHtmlWhitespaceSensitivity(
        ToUtf8(ReadString(path, kFormatSection, L"htmlWhitespaceSensitivity",
                          ToWide(Format::ToString(format.htmlWhitespaceSensitivity)))),
        format.htmlWhitespaceSensitivity);
    format.endOfLine = ParseEolName(ReadString(path, kFormatSection, L"endOfLine", L"auto"));
    format.embeddedLanguageFormatting = Format::EmbeddedLanguageFormatting::Auto;
    Format::ParseEmbeddedLanguageFormatting(
        ToUtf8(ReadString(path, kFormatSection, L"embeddedLanguageFormatting",
                          ToWide(Format::ToString(format.embeddedLanguageFormatting)))),
        format.embeddedLanguageFormatting);
    format.singleAttributePerLine =
        ReadBool(path, kFormatSection, L"singleAttributePerLine", format.singleAttributePerLine);
    format.objectWrap = Format::ObjectWrap::Preserve;
    Format::ParseObjectWrap(ToUtf8(ReadString(path, kFormatSection, L"objectWrap",
                                              ToWide(Format::ToString(format.objectWrap)))),
                            format.objectWrap);
    format.experimentalTernaries =
        ReadBool(path, kFormatSection, L"experimentalTernaries", format.experimentalTernaries);

    format.timeoutMs = ReadInt(path, kFormatSection, L"timeoutMs", format.timeoutMs);
    formatOnSave = ReadBool(path, kFormatSection, L"formatOnSave", formatOnSave);
    extensionFilter = ReadString(path, kFormatSection, L"extensionFilter", extensionFilter);
    requireConfig = ReadBool(path, kFormatSection, L"requireConfig", requireConfig);
    ignoreFile = ReadString(path, kFormatSection, L"ignoreFile", ignoreFile);
    logLevel = ReadString(path, kFormatSection, L"logLevel", logLevel);

    crypto.enabled = ReadBool(path, kCryptoSection, L"enabled", crypto.enabled);
    crypto.cipher = ReadString(path, kCryptoSection, L"cipher", crypto.cipher);
    crypto.kdf = ReadString(path, kCryptoSection, L"kdf", crypto.kdf);
    crypto.iterations = ReadInt(path, kCryptoSection, L"iterations", crypto.iterations);
    crypto.armorBase64 = ReadBool(path, kCryptoSection, L"armorBase64", crypto.armorBase64);

    encoding.customAlphabet = ReadString(path, kEncodingSection, L"customAlphabet", encoding.customAlphabet);
    encoding.base16Uppercase = ReadBool(path, kEncodingSection, L"base16Uppercase", encoding.base16Uppercase);
    encoding.base16Delimiter = ReadString(path, kEncodingSection, L"base16Delimiter", encoding.base16Delimiter);
    encoding.base64UrlSafe = ReadBool(path, kEncodingSection, L"base64UrlSafe", encoding.base64UrlSafe);
    encoding.base64Padding = ReadBool(path, kEncodingSection, L"base64Padding", encoding.base64Padding);
    encoding.base85Variant = ReadString(path, kEncodingSection, L"base85Variant", encoding.base85Variant);

    password.length = ReadInt(path, kPasswordSection, L"length", password.length);
    password.useLower = ReadBool(path, kPasswordSection, L"useLower", password.useLower);
    password.useUpper = ReadBool(path, kPasswordSection, L"useUpper", password.useUpper);
    password.useDigits = ReadBool(path, kPasswordSection, L"useDigits", password.useDigits);
    password.useSymbols = ReadBool(path, kPasswordSection, L"useSymbols", password.useSymbols);
    password.avoidAmbiguous =
        ReadBool(path, kPasswordSection, L"avoidAmbiguous", password.avoidAmbiguous);
    password.excludeChars = ReadString(path, kPasswordSection, L"excludeChars", password.excludeChars);
    password.complexity = ReadString(path, kPasswordSection, L"complexity", password.complexity);
    password.preset = ReadString(path, kPasswordSection, L"preset", password.preset);
    password.algorithm = ReadString(path, kPasswordSection, L"algorithm", password.algorithm);
    password.rememberSecret = ReadBool(path, kPasswordSection, L"rememberSecret", password.rememberSecret);
    if (password.rememberSecret) {
        password.secretKey = ReadString(path, kPasswordSection, L"secretKey", password.secretKey);
    } else {
        password.secretKey.clear();
    }
    password.bcryptCost = ReadInt(path, kPasswordSection, L"bcryptCost", password.bcryptCost);
    password.argon2t = ReadInt(path, kPasswordSection, L"argon2t", password.argon2t);
    password.argon2m = ReadInt(path, kPasswordSection, L"argon2m", password.argon2m);
    password.argon2p = ReadInt(path, kPasswordSection, L"argon2p", password.argon2p);
    password.argon2HashLength = ReadInt(path, kPasswordSection, L"argon2HashLength", password.argon2HashLength);
    password.pbkdf2Iterations = ReadInt(path, kPasswordSection, L"pbkdf2Iterations", password.pbkdf2Iterations);
    password.pbkdf2KeyLength = ReadInt(path, kPasswordSection, L"pbkdf2KeyLength", password.pbkdf2KeyLength);
    password.autoClearClipboard = ReadBool(path, kPasswordSection, L"autoClearClipboard", password.autoClearClipboard);
    password.clipboardClearSeconds = ReadInt(path, kPasswordSection, L"clipboardClearSeconds", password.clipboardClearSeconds);

    password.minSymbols = ReadInt(path, kPasswordSection, L"minSymbols", password.minSymbols);
    password.minDigits = ReadInt(path, kPasswordSection, L"minDigits", password.minDigits);
    password.words = ReadInt(path, kPasswordSection, L"words", password.words);
    password.separator = ReadString(path, kPasswordSection, L"separator", password.separator);

    generator.defaultUuidVersion =
        ReadString(path, kGeneratorSection, L"defaultUuidVersion", generator.defaultUuidVersion);
    generator.uuidUppercase =
        ReadBool(path, kGeneratorSection, L"uuidUppercase", generator.uuidUppercase);
    generator.uuidHyphens =
        ReadBool(path, kGeneratorSection, L"uuidHyphens", generator.uuidHyphens);
    generator.uuidBraces =
        ReadBool(path, kGeneratorSection, L"uuidBraces", generator.uuidBraces);
    generator.guidUppercase =
        ReadBool(path, kGeneratorSection, L"guidUppercase", generator.guidUppercase);
    generator.guidBraces =
        ReadBool(path, kGeneratorSection, L"guidBraces", generator.guidBraces);
    generator.ulidLowercase =
        ReadBool(path, kGeneratorSection, L"ulidLowercase", generator.ulidLowercase);
    generator.bulkType = ReadString(path, kGeneratorSection, L"bulkType", generator.bulkType);
    generator.bulkCount = ReadInt(path, kGeneratorSection, L"bulkCount", generator.bulkCount);
    generator.bulkSeparator =
        ReadString(path, kGeneratorSection, L"bulkSeparator", generator.bulkSeparator);
    generator.quotes = ReadString(path, kGeneratorSection, L"quotes", generator.quotes);

    hashing.uppercase = ReadBool(path, kHashingSection, L"uppercase", hashing.uppercase);
    hashing.quickAction = ReadString(path, kHashingSection, L"quickAction", hashing.quickAction);
    hashing.lastAlgorithm =
        ReadString(path, kHashingSection, L"lastAlgorithm", hashing.lastAlgorithm);
    hashing.hmacKeyIsHex = ReadBool(path, kHashingSection, L"hmacKeyIsHex", hashing.hmacKeyIsHex);

    if (!loadedFromDisk) {
        Save();
    }
}

void Settings::Save() const {
    if (path.empty()) {
        return;
    }
    EnsureUtf16File(path);

    WriteInt(path, kFormatSection, L"printWidth", format.printWidth);
    WriteInt(path, kFormatSection, L"tabWidth", format.tabWidth);
    WriteBool(path, kFormatSection, L"useTabs", format.useTabs);
    WriteBool(path, kFormatSection, L"minify", format.minify);
    WriteBool(path, kFormatSection, L"semi", format.semi);
    WriteBool(path, kFormatSection, L"singleQuote", format.singleQuote);
    WriteBool(path, kFormatSection, L"jsxSingleQuote", format.jsxSingleQuote);
    WriteString(path, kFormatSection, L"quoteProps", ToWide(Format::ToString(format.quoteProps)));
    WriteString(path, kFormatSection, L"trailingComma",
                ToWide(Format::ToString(format.trailingComma)));
    WriteBool(path, kFormatSection, L"bracketSpacing", format.bracketSpacing);
    WriteBool(path, kFormatSection, L"bracketSameLine", format.bracketSameLine);
    WriteString(path, kFormatSection, L"arrowParens", ToWide(Format::ToString(format.arrowParens)));
    WriteString(path, kFormatSection, L"proseWrap", ToWide(Format::ToString(format.proseWrap)));
    WriteString(path, kFormatSection, L"htmlWhitespaceSensitivity",
                ToWide(Format::ToString(format.htmlWhitespaceSensitivity)));
    WriteString(path, kFormatSection, L"endOfLine", ToWide(Format::ToString(format.endOfLine)));
    WriteString(path, kFormatSection, L"embeddedLanguageFormatting",
                ToWide(Format::ToString(format.embeddedLanguageFormatting)));
    WriteBool(path, kFormatSection, L"singleAttributePerLine", format.singleAttributePerLine);
    WriteString(path, kFormatSection, L"objectWrap", ToWide(Format::ToString(format.objectWrap)));
    WriteBool(path, kFormatSection, L"experimentalTernaries", format.experimentalTernaries);

    WriteInt(path, kFormatSection, L"timeoutMs", format.timeoutMs);
    WriteBool(path, kFormatSection, L"formatOnSave", formatOnSave);
    WriteString(path, kFormatSection, L"extensionFilter", extensionFilter);
    WriteBool(path, kFormatSection, L"requireConfig", requireConfig);
    WriteString(path, kFormatSection, L"ignoreFile", ignoreFile);
    WriteString(path, kFormatSection, L"logLevel", logLevel);

    WriteBool(path, kCryptoSection, L"enabled", crypto.enabled);
    WriteString(path, kCryptoSection, L"cipher", crypto.cipher);
    WriteString(path, kCryptoSection, L"kdf", crypto.kdf);
    WriteInt(path, kCryptoSection, L"iterations", crypto.iterations);

    WriteInt(path, kPasswordSection, L"length", password.length);
    WriteBool(path, kPasswordSection, L"useLower", password.useLower);
    WriteBool(path, kPasswordSection, L"useUpper", password.useUpper);
    WriteBool(path, kPasswordSection, L"useDigits", password.useDigits);
    WriteBool(path, kPasswordSection, L"useSymbols", password.useSymbols);
    WriteBool(path, kPasswordSection, L"avoidAmbiguous", password.avoidAmbiguous);
    WriteString(path, kPasswordSection, L"excludeChars", password.excludeChars);
    WriteString(path, kPasswordSection, L"complexity", password.complexity);
    WriteString(path, kPasswordSection, L"preset", password.preset);
    WriteString(path, kPasswordSection, L"algorithm", password.algorithm);
    WriteBool(path, kPasswordSection, L"rememberSecret", password.rememberSecret);
    if (password.rememberSecret) {
        WriteString(path, kPasswordSection, L"secretKey", password.secretKey);
    } else {
        WriteString(path, kPasswordSection, L"secretKey", L"");
    }
    WriteInt(path, kPasswordSection, L"bcryptCost", password.bcryptCost);
    WriteInt(path, kPasswordSection, L"argon2t", password.argon2t);
    WriteInt(path, kPasswordSection, L"argon2m", password.argon2m);
    WriteInt(path, kPasswordSection, L"argon2p", password.argon2p);
    WriteInt(path, kPasswordSection, L"argon2HashLength", password.argon2HashLength);
    WriteInt(path, kPasswordSection, L"pbkdf2Iterations", password.pbkdf2Iterations);
    WriteInt(path, kPasswordSection, L"pbkdf2KeyLength", password.pbkdf2KeyLength);
    WriteBool(path, kPasswordSection, L"autoClearClipboard", password.autoClearClipboard);
    WriteInt(path, kPasswordSection, L"clipboardClearSeconds", password.clipboardClearSeconds);

    WriteInt(path, kPasswordSection, L"minSymbols", password.minSymbols);
    WriteInt(path, kPasswordSection, L"minDigits", password.minDigits);
    WriteInt(path, kPasswordSection, L"words", password.words);
    WriteString(path, kPasswordSection, L"separator", password.separator);

    WriteBool(path, kCryptoSection, L"enabled", crypto.enabled);
    WriteString(path, kCryptoSection, L"cipher", crypto.cipher);
    WriteString(path, kCryptoSection, L"kdf", crypto.kdf);
    WriteInt(path, kCryptoSection, L"iterations", crypto.iterations);
    WriteBool(path, kCryptoSection, L"armorBase64", crypto.armorBase64);

    WriteString(path, kEncodingSection, L"customAlphabet", encoding.customAlphabet);
    WriteBool(path, kEncodingSection, L"base16Uppercase", encoding.base16Uppercase);
    WriteString(path, kEncodingSection, L"base16Delimiter", encoding.base16Delimiter);
    WriteBool(path, kEncodingSection, L"base64UrlSafe", encoding.base64UrlSafe);
    WriteBool(path, kEncodingSection, L"base64Padding", encoding.base64Padding);
    WriteString(path, kEncodingSection, L"base85Variant", encoding.base85Variant);

    WriteString(path, kGeneratorSection, L"defaultUuidVersion", generator.defaultUuidVersion);
    WriteBool(path, kGeneratorSection, L"uuidUppercase", generator.uuidUppercase);
    WriteBool(path, kGeneratorSection, L"uuidHyphens", generator.uuidHyphens);
    WriteBool(path, kGeneratorSection, L"uuidBraces", generator.uuidBraces);
    WriteBool(path, kGeneratorSection, L"guidUppercase", generator.guidUppercase);
    WriteBool(path, kGeneratorSection, L"guidBraces", generator.guidBraces);
    WriteBool(path, kGeneratorSection, L"ulidLowercase", generator.ulidLowercase);
    WriteString(path, kGeneratorSection, L"bulkType", generator.bulkType);
    WriteInt(path, kGeneratorSection, L"bulkCount", generator.bulkCount);
    WriteString(path, kGeneratorSection, L"bulkSeparator", generator.bulkSeparator);
    WriteString(path, kGeneratorSection, L"quotes", generator.quotes);

    WriteBool(path, kHashingSection, L"uppercase", hashing.uppercase);
    WriteString(path, kHashingSection, L"quickAction", hashing.quickAction);
    WriteString(path, kHashingSection, L"lastAlgorithm", hashing.lastAlgorithm);
    WriteBool(path, kHashingSection, L"hmacKeyIsHex", hashing.hmacKeyIsHex);
}

std::wstring Settings::Describe() const {
    std::wstring text;
    text += L"INI file: " + (path.empty() ? std::wstring(L"<not loaded>") : path) + L"\r\n\r\n[Format]\r\n";
    text += ToWide(Format::DescribeOptions(format));
    text += L"formatOnSave=" + std::to_wstring(formatOnSave ? 1 : 0) + L"\r\n";
    text += L"timeoutMs=" + std::to_wstring(format.timeoutMs) + L"\r\n";
    text += L"extensionFilter=" + extensionFilter + L"\r\n";
    text += L"requireConfig=" + std::to_wstring(requireConfig ? 1 : 0) + L"\r\n";
    text += L"ignoreFile=" + (ignoreFile.empty() ? std::wstring(L"<none>") : ignoreFile) + L"\r\n";
    text += L"logLevel=" + logLevel + L"\r\n";
    return text;
}

}  // namespace ArgpiSuite::Plugin

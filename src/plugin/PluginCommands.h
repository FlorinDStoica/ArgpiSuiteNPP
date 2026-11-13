// ArgpiSuite plugin — command table and handlers.
//
// Command indices are stored in user shortcuts, so keep the order stable:
// the nine Format commands come first, then the seven Crypto stubs and the
// five Password stubs. Never reorder them; append new commands at the end.
#pragma once

#include <windows.h>

#include <vector>

#include "MenuBuilder.h"
#include "PluginInterface.h"

namespace ArgpiSuite::Plugin {

enum class Cmd : int {
    // --- Format (10 items) ---
    FormatDocument = 0,
    FormatSelection = 1,
    FormatMinifyDocument = 2,
    CheckFormatting = 3,
    ToggleFormatOnSave = 4,
    FormatAllOpenDocuments = 5,
    FormatFolder = 6,
    ShowResolvedConfig = 7,
    FormatSettings = 8,
    ShowFormatLog = 9,

    // --- Encrypt / Decrypt (7 items) ---
    CryptoEncryptSelection = 10,
    CryptoDecryptSelection = 11,
    CryptoEncryptDocument = 12,
    CryptoDecryptDocument = 13,
    CryptoEncryptFile = 14,
    CryptoDecryptFile = 15,
    CryptoSettings = 16,

    // --- Encode / Decode (9 items) ---
    EncodingBase16Encode = 17,
    EncodingBase16Decode = 18,
    EncodingBase64Encode = 19,
    EncodingBase64Decode = 20,
    EncodingBase85Encode = 21,
    EncodingBase85Decode = 22,
    EncodingCustomEncode = 23,
    EncodingCustomDecode = 24,
    EncodingSettings = 25,

    // --- Password Generator (5 items) ---
    PasswordGenerate = 26,
    PasswordGeneratePassphrase = 27,
    PasswordInsertAtCaret = 28,
    PasswordCopyToClipboard = 29,
    PasswordSettings = 30,

    // --- UUID / GUID & ULID Generator (3 items) ---
    GeneratorUuid = 31,
    GeneratorGuid = 32,
    GeneratorUlid = 33,

    // --- Hashing (3 items) ---
    HashMd5 = 34,
    HashSha256 = 35,
    HashSha512 = 36,

    // --- Tools (4 items) ---
    ToolJwtDecoder = 37,
    ToolPathQuerying = 38,
    ToolCsvTsvAlignment = 39,
    ToolSmartDeduplication = 40,

    // --- UUID / GUID & ULID Generator (appended to keep indices stable) ---
    GeneratorUuidV4 = 41,
    GeneratorUuidV7 = 42,
    GeneratorBulkDialog = 43,

    // --- Hashing (appended to keep indices stable) ---
    HashDoubleSha256 = 44,
    HashHmacSha256 = 45,
    HashCrc32 = 46,
    HashToolDialog = 47,

    // --- About ---
    About = 48,

    Count = 49,
};

constexpr int kCommandCount = static_cast<int>(Cmd::Count);

struct CommandSpec {
    const wchar_t* name;
    PFUNCPLUGINCMD handler;
    ShortcutKey* shortcut;
    bool checked;
};

/// Registration table, index == Cmd value.
const CommandSpec* CommandTable(int& count);

/// Declarative submenu description used by MenuBuilder.
const Menu::SubmenuGroup* SubmenuGroups(int& count);

/// Called from NPPN_READY: installs the submenus and restores check states.
void OnReady();
/// Called from NPPN_FILEBEFORESAVE: formats when Format on Save is enabled.
void OnFileBeforeSave();
/// Called from NPPN_SHUTDOWN: saves settings and destroys owned windows.
void OnShutdown();

// --- Format ---------------------------------------------------------------
void CommandFormatDocument();
void CommandFormatSelection();
void CommandFormatMinifyDocument();
void CommandCheckFormatting();
void CommandToggleFormatOnSave();
void CommandFormatAllOpenDocuments();
void CommandFormatFolder();
void CommandShowResolvedConfig();
void CommandFormatSettings();
void CommandShowFormatLog();

// --- Encrypt / Decrypt ---------------------------------------------------
void CommandCryptoEncryptSelection();
void CommandCryptoDecryptSelection();
void CommandCryptoEncryptDocument();
void CommandCryptoDecryptDocument();
void CommandCryptoEncryptFile();
void CommandCryptoDecryptFile();
void CommandCryptoSettings();

// --- Generate Password ---------------------------------------------------
void CommandPasswordGenerate();
void CommandPasswordGeneratePassphrase();
void CommandPasswordInsertAtCaret();
void CommandPasswordCopyToClipboard();
void CommandPasswordSettings();

// --- Encoding / Decoding -------------------------------------------------
void CommandEncodingBase16Encode();
void CommandEncodingBase16Decode();
void CommandEncodingBase64Encode();
void CommandEncodingBase64Decode();
void CommandEncodingBase85Encode();
void CommandEncodingBase85Decode();
void CommandEncodingCustomEncode();
void CommandEncodingCustomDecode();
void CommandEncodingSettings();

// --- UUID / GUID & ULID Generator ----------------------------------------
void CommandGeneratorUuid();
void CommandGeneratorGuid();
void CommandGeneratorUlid();
void CommandGeneratorUuidV4();
void CommandGeneratorUuidV7();
void CommandGeneratorBulkDialog();

// --- Hashing -------------------------------------------------------------
void CommandHashMd5();
void CommandHashSha256();
void CommandHashSha512();
void CommandHashDoubleSha256();
void CommandHashHmacSha256();
void CommandHashCrc32();
void CommandHashToolDialog();

// --- Tools ---------------------------------------------------------------
void CommandToolJwtDecoder();
void CommandToolPathQuerying();
void CommandToolCsvTsvAlignment();
void CommandToolSmartDeduplication();

// --- About ---------------------------------------------------------------
void CommandAbout();

// --- helpers shared with the notification handlers -----------------------
std::wstring CurrentFilePath(HWND nppHandle);
void UpdateFormatOnSaveCheck();

}  // namespace ArgpiSuite::Plugin

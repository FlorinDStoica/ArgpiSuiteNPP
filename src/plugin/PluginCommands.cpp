// ArgpiSuite plugin — command table and handlers.
//
// The command indices are frozen in PluginCommands.h. This file owns the menu
// descriptions, the classification of the current buffer and every handler.
// All formatter calls go through FormatBufferDocument so the safety rules
// ("no write on error", "no dirty flag when unchanged", one undo step) are
// implemented exactly once.

#include "PluginCommands.h"

#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ConfigResolver.h"
#include "FormatWorker.h"
#include "NotImplemented.h"
#include "Notepad_plus_msgs.h"
#include "OutputLog.h"
#include "PluginDefinition.h"
#include "PluginHost.h"
#include "Scintilla.h"
#include "SettingsDialog.h"
#include "TextApply.h"
#include "TextUtil.h"
#include "crypto/CryptoDialog.h"
#include "crypto/CryptoService.h"
#include "crypto/EncodingDialog.h"
#include "crypto/EncodingService.h"
#include "format/Format.h"
#include "format/FormatLanguage.h"
#include "generator/IdGeneratorDialog.h"
#include "generator/IdGeneratorService.h"
#include "hash/HashDialog.h"
#include "hash/HashService.h"
#include "password/PasswordDialog.h"
#include "password/PasswordService.h"
#include "tools/CsvToolDialog.h"
#include "tools/DedupeSortDialog.h"
#include "tools/JwtDecoderDialog.h"
#include "tools/PathQueryDialog.h"

extern FuncItem funcItem[nbFunc];

namespace ArgpiSuite::Plugin {
namespace {

constexpr std::size_t kMaxPathLength = 32768;
constexpr std::size_t kLargeDocumentBytes = 5u * 1024u * 1024u;

ShortcutKey MakeFormatDocumentShortcut() {
    ShortcutKey shortcut;
    shortcut._isCtrl = true;
    shortcut._isAlt = true;
    shortcut._isShift = true;
    shortcut._key = 'P';
    return shortcut;
}

ShortcutKey gFormatDocumentShortcut = MakeFormatDocumentShortcut();

const CommandSpec kCommands[kCommandCount] = {
    // --- Format (10 items) ---
    {L"Format Document", &CommandFormatDocument, &gFormatDocumentShortcut, false},
    {L"Format Selection", &CommandFormatSelection, nullptr, false},
    {L"Minify Document", &CommandFormatMinifyDocument, nullptr, false},
    {L"Check Formatting", &CommandCheckFormatting, nullptr, false},
    {L"Toggle Format on Save", &CommandToggleFormatOnSave, nullptr, false},
    {L"Format All Open Documents", &CommandFormatAllOpenDocuments, nullptr, false},
    {L"Format Folder…", &CommandFormatFolder, nullptr, false},
    {L"Show Resolved Config", &CommandShowResolvedConfig, nullptr, false},
    {L"Format Settings…", &CommandFormatSettings, nullptr, false},
    {L"Show Format Log", &CommandShowFormatLog, nullptr, false},

    // --- Encrypt / Decrypt (7 items) ---
    {L"Encrypt Selection", &CommandCryptoEncryptSelection, nullptr, false},
    {L"Decrypt Selection", &CommandCryptoDecryptSelection, nullptr, false},
    {L"Encrypt Document", &CommandCryptoEncryptDocument, nullptr, false},
    {L"Decrypt Document", &CommandCryptoDecryptDocument, nullptr, false},
    {L"Encrypt File…", &CommandCryptoEncryptFile, nullptr, false},
    {L"Decrypt File…", &CommandCryptoDecryptFile, nullptr, false},
    {L"Encryption Settings…", &CommandCryptoSettings, nullptr, false},

    // --- Encode / Decode (9 items) ---
    {L"Base16 Encode", &CommandEncodingBase16Encode, nullptr, false},
    {L"Base16 Decode", &CommandEncodingBase16Decode, nullptr, false},
    {L"Base64 Encode", &CommandEncodingBase64Encode, nullptr, false},
    {L"Base64 Decode", &CommandEncodingBase64Decode, nullptr, false},
    {L"Base85 Encode", &CommandEncodingBase85Encode, nullptr, false},
    {L"Base85 Decode", &CommandEncodingBase85Decode, nullptr, false},
    {L"Custom Base Encode", &CommandEncodingCustomEncode, nullptr, false},
    {L"Custom Base Decode", &CommandEncodingCustomDecode, nullptr, false},
    {L"Settings…", &CommandEncodingSettings, nullptr, false},

    // --- Password Generator (5 items) ---
    {L"Generate Password", &CommandPasswordGenerate, nullptr, false},
    {L"Generate Passphrase", &CommandPasswordGeneratePassphrase, nullptr, false},
    {L"Insert at Caret", &CommandPasswordInsertAtCaret, nullptr, false},
    {L"Copy to Clipboard", &CommandPasswordCopyToClipboard, nullptr, false},
    {L"Password Settings…", &CommandPasswordSettings, nullptr, false},

    // --- UUID / GUID & ULID Generator (3 items) ---
    {L"UUID", &CommandGeneratorUuid, nullptr, false},
    {L"GUID", &CommandGeneratorGuid, nullptr, false},
    {L"ULID", &CommandGeneratorUlid, nullptr, false},

    // --- Hashing (3 items) ---
    {L"MD5", &CommandHashMd5, nullptr, false},
    {L"SHA-256", &CommandHashSha256, nullptr, false},
    {L"SHA-512", &CommandHashSha512, nullptr, false},

    // --- Tools (4 items) ---
    {L"JWT (JSON Web Token) Decoder", &CommandToolJwtDecoder, nullptr, false},
    {L"JSON / XML / SQL Path Querying", &CommandToolPathQuerying, nullptr, false},
    {L"CSV / TSV Column Alignment & Filtering", &CommandToolCsvTsvAlignment, nullptr, false},
    {L"Smart Deduplication & Sorting:", &CommandToolSmartDeduplication, nullptr, false},

    // --- UUID / GUID & ULID Generator (appended) ---
    {L"UUID v4 (Random)", &CommandGeneratorUuidV4, nullptr, false},
    {L"UUID v7 (Timestamp)", &CommandGeneratorUuidV7, nullptr, false},
    {L"Bulk / Custom Generator…", &CommandGeneratorBulkDialog, nullptr, false},

    // --- Hashing (appended) ---
    {L"2xSHA-256 (Double SHA-256)", &CommandHashDoubleSha256, nullptr, false},
    {L"HMAC-SHA256…", &CommandHashHmacSha256, nullptr, false},
    {L"CRC32", &CommandHashCrc32, nullptr, false},
    {L"Hash & Checksum Tool…", &CommandHashToolDialog, nullptr, false},

    // --- About ---
    {L"About…", &CommandAbout, nullptr, false},
};

const wchar_t* const kEncodeDecodeLabels[] = {
    L"Encode",
    L"Decode",
};

const Menu::ChildSubmenu kEncodingVariants[] = {
    {L"Base16", static_cast<int>(Cmd::EncodingBase16Encode), 2, false, -1, kEncodeDecodeLabels},
    {L"Base64", static_cast<int>(Cmd::EncodingBase64Encode), 2, false, -1, kEncodeDecodeLabels},
    {L"Base85", static_cast<int>(Cmd::EncodingBase85Encode), 2, false, -1, kEncodeDecodeLabels},
    {L"Custom", static_cast<int>(Cmd::EncodingCustomEncode), 2, false, -1, kEncodeDecodeLabels},
};

const Menu::ChildSubmenu kGeneratorChildren[] = {
    {L"UUID Versions", static_cast<int>(Cmd::GeneratorUuidV4), 2, true},
};

const Menu::ChildSubmenu kHashingChildren[] = {
    {L"More Algorithms", static_cast<int>(Cmd::HashDoubleSha256), 3, true},
};

const Menu::SubmenuGroup kSubmenuGroups[] = {
    {L"Format", L"Format: ", static_cast<int>(Cmd::FormatDocument), 10, false},
    {L"Encrypt / Decrypt", L"Crypto: ", static_cast<int>(Cmd::CryptoEncryptSelection), 7, true},
    {L"Encode / Decode", L"Encode: ", static_cast<int>(Cmd::EncodingBase16Encode), 0, false,
     static_cast<int>(Cmd::EncodingSettings), kEncodingVariants,
     static_cast<int>(sizeof(kEncodingVariants) / sizeof(kEncodingVariants[0]))},
    {L"Password Generator", L"Password: ", static_cast<int>(Cmd::PasswordGenerate), 5, false},
    {L"UUID / GUID & ULID Generator", L"ID Gen: ", static_cast<int>(Cmd::GeneratorUuid), 3, false,
     static_cast<int>(Cmd::GeneratorBulkDialog), kGeneratorChildren,
     static_cast<int>(sizeof(kGeneratorChildren) / sizeof(kGeneratorChildren[0]))},
    {L"Hashing", L"Hash: ", static_cast<int>(Cmd::HashMd5), 3, false,
     static_cast<int>(Cmd::HashToolDialog), kHashingChildren,
     static_cast<int>(sizeof(kHashingChildren) / sizeof(kHashingChildren[0]))},
    {L"Tools", L"Tool: ", static_cast<int>(Cmd::ToolJwtDecoder), 4, true},
};

constexpr int kSubmenuGroupCount =
    static_cast<int>(sizeof(kSubmenuGroups) / sizeof(kSubmenuGroups[0]));

/// Keeps `PluginHost::busy_` correct even when a handler exits through an
/// exception, and restores it for the next notification.
class BusyGuard {
public:
    explicit BusyGuard(PluginHost& host) : host_(host) { host_.SetBusy(true); }
    ~BusyGuard() { host_.SetBusy(false); }

    BusyGuard(const BusyGuard&) = delete;
    BusyGuard& operator=(const BusyGuard&) = delete;

private:
    PluginHost& host_;
};

/// Cooperative watchdog. FormatText only reads the flag, so a watchdog thread
/// is enough and no Scintilla or menu call ever leaves the UI thread.
class FormatTimeout {
public:
    explicit FormatTimeout(int timeoutMs) {
        if (timeoutMs <= 0) {
            return;
        }
        try {
            worker_ = std::thread([this, timeoutMs]() { Watch(timeoutMs); });
        } catch (...) {
            // Running without a watchdog is better than failing the command.
        }
    }

    ~FormatTimeout() { Finish(); }

    FormatTimeout(const FormatTimeout&) = delete;
    FormatTimeout& operator=(const FormatTimeout&) = delete;

    [[nodiscard]] const std::atomic_bool* CancelFlag() const { return &cancel_; }

    void Finish() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void Watch(int timeoutMs) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!wake_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                            [this]() { return finished_; })) {
            cancel_.store(true, std::memory_order_relaxed);
        }
    }

    std::atomic_bool cancel_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool finished_ = false;
};

enum class BufferStatus {
    Applied,
    Unchanged,
    WouldChange,
    Skipped,
    Unsupported,
    Error,
};

struct BufferFormatResult {
    BufferStatus status = BufferStatus::Error;
    Format::Language language = Format::Language::Unknown;
    std::wstring message;
};

std::wstring DescribeError(const Format::FormatError& error) {
    std::wstring message = L"error: " + ToWide(error.message);
    if (error.line > 0) {
        message += L" at line " + std::to_wstring(error.line) + L", column " +
                   std::to_wstring(error.column);
    }
    return message;
}

std::size_t CountLines(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    std::size_t lines = 0;
    for (char ch : text) {
        if (ch == '\n') {
            ++lines;
        }
    }
    if (text.back() != '\n') {
        ++lines;
    }
    return lines;
}

void MoveCaretToError(HWND scintilla, const ScintillaDocument& document,
                      const Format::FormatError& error) {
    const std::size_t raw = Utf8OffsetToRawOffset(document.raw, document.utf8, error.offset);
    ::SendMessageW(scintilla, SCI_GOTOPOS, static_cast<WPARAM>(raw), 0);
    ::SendMessageW(scintilla, SCI_SCROLLCARET, 0, 0);
}

Format::Language LanguageForDocument(HWND npp, const std::wstring& path) {
    int languageId = L_TEXT;
    ::SendMessageW(npp, NPPM_GETCURRENTLANGTYPE, 0,
                   reinterpret_cast<LPARAM>(&languageId));

    switch (languageId) {
        case L_JSON5:
            return Format::Language::Json5;
        case L_JSON:
            return path.empty() ? Format::Language::Json : Format::DetectJsonDialect(ToUtf8(path));
        case L_CSS:
            if (!path.empty()) {
                const Format::Language byPath = Format::DetectLanguageFromPath(ToUtf8(path));
                if (byPath == Format::Language::Scss || byPath == Format::Language::Less) {
                    return byPath;
                }
            }
            return Format::Language::Css;
        case L_YAML:
            return Format::Language::Yaml;
        case L_JAVASCRIPT:
        case L_JS_EMBEDDED:
            return Format::Language::JavaScript;
        case L_TYPESCRIPT:
            return Format::Language::TypeScript;
        case L_HTML:
            return Format::Language::Html;
        case L_XML:
            return Format::Language::Xml;
        case L_PHP:
            return Format::Language::Php;
        case L_JAVA:
            return Format::Language::Java;
        case L_C:
            return Format::Language::C;
        case L_CPP:
            return Format::Language::Cpp;
        default:
            break;
    }

    if (!path.empty()) {
        return Format::DetectLanguageFromPath(ToUtf8(path));
    }
    return Format::Language::Unknown;
}

/// Falls back to sniffing the buffer when Notepad++ reports an unknown or
/// unsupported language. This makes pasted or unsaved JSON work even when the
/// editor still shows "Normal text", PHP, HTML, etc.
Format::Language ResolveLanguage(HWND npp, const std::wstring& path, std::string_view text) {
    const Format::Language detected = LanguageForDocument(npp, path);
    if (Format::IsLanguageImplemented(detected)) {
        return detected;
    }
    const Format::Language sniffed = Format::DetectLanguageFromContent(text);
    return Format::IsLanguageImplemented(sniffed) ? sniffed : detected;
}

std::wstring UnsupportedMessage(Format::Language language) {
    return std::wstring(Format::LanguageDisplayName(language)) +
           L" formatting is not supported in this build";
}

bool IsExtensionAllowed(const Settings& settings, const std::wstring& path) {
    const std::wstring extension = ToLower(ExtensionOf(path));
    if (extension.empty()) {
        return false;
    }
    for (std::wstring filter : SplitList(settings.extensionFilter)) {
        filter = ToLower(filter);
        if (!filter.empty() && filter.front() == L'.') {
            filter.erase(0, 1);
        }
        if (filter == extension) {
            return true;
        }
    }
    return false;
}

/// Resolves the config for `path`; empty paths (unsaved buffers) use the INI
/// defaults so a stray `.editorconfig` in Notepad++'s working directory can
/// never influence an unrelated buffer.
ResolvedConfig ResolveConfig(ConfigResolver& resolver, const Settings& settings,
                             const std::wstring& path) {
    if (path.empty()) {
        ResolvedConfig fallback;
        fallback.options = settings.format;
        return fallback;
    }
    return resolver.Resolve(path);
}

BufferFormatResult FormatBufferDocument(HWND npp, HWND scintilla, const std::wstring& path,
                                        bool apply, const Settings& settings,
                                        ConfigResolver& resolver, bool minifyOverride = false) {
    BufferFormatResult result;

    ScintillaDocument document;
    std::wstring error;
    if (!ReadDocument(scintilla, document, error)) {
        result.status = BufferStatus::Error;
        result.message = error;
        return result;
    }
    if (document.utf8.empty()) {
        result.status = BufferStatus::Unchanged;
        result.message = L"document is empty";
        return result;
    }
    if (document.utf8.size() > kLargeDocumentBytes) {
        OutputLog::Instance().AppendLine(
            L"warning: the document is larger than 5 MB; formatting may take a while");
    }
    if (apply && document.readOnly) {
        result.status = BufferStatus::Error;
        result.message = L"the document is read-only";
        return result;
    }

    ResolvedConfig config = ResolveConfig(resolver, settings, path);
    if (minifyOverride) {
        config.options.minify = true;
    }
    if (settings.requireConfig && !config.hasConfig) {
        result.status = BufferStatus::Skipped;
        result.message = L"skipped (no configuration file found)";
        return result;
    }
    if (!path.empty() && resolver.IsIgnored(path)) {
        result.status = BufferStatus::Skipped;
        result.message = L"skipped (ignored by .formatignore / .prettierignore)";
        return result;
    }

    result.language = ResolveLanguage(npp, path, document.utf8);
    if (!Format::IsLanguageImplemented(result.language)) {
        result.status = BufferStatus::Unsupported;
        result.message = UnsupportedMessage(result.language);
        return result;
    }

    const CaretSnapshot caret = CaptureCaret(scintilla);

    FormatTimeout timeout(settings.format.timeoutMs);
    const Format::FormatResult formatted =
        Format::FormatText(document.utf8, result.language, config.options, timeout.CancelFlag());
    timeout.Finish();

    if (formatted.canceled) {
        result.status = BufferStatus::Error;
        result.message = L"formatting timed out after " +
                         std::to_wstring(settings.format.timeoutMs) + L" ms";
        return result;
    }
    if (!formatted.ok) {
        result.status = BufferStatus::Error;
        result.message = DescribeError(formatted.error);
        MoveCaretToError(scintilla, document, formatted.error);
        return result;
    }
    if (formatted.output.empty()) {
        result.status = BufferStatus::Unchanged;
        result.message = L"nothing to format";
        return result;
    }
    if (formatted.output == document.utf8) {
        result.status = BufferStatus::Unchanged;
        result.message = L"already formatted (no change)";
        if (!formatted.idempotent) {
            result.message += L" — warning: the formatter is not idempotent";
        }
        return result;
    }
    if (!apply) {
        result.status = BufferStatus::WouldChange;
        result.message = L"would reformat (" + std::to_wstring(CountLines(document.utf8)) +
                         L" → " + std::to_wstring(CountLines(formatted.output)) + L" lines)";
        return result;
    }

    if (!ApplyFormattedText(scintilla, document, formatted.output, caret, error)) {
        result.status = BufferStatus::Error;
        result.message = error;
        return result;
    }

    result.status = BufferStatus::Applied;
    result.message = L"formatted (" + std::to_wstring(CountLines(document.utf8)) + L" → " +
                     std::to_wstring(CountLines(formatted.output)) + L" lines)";
    if (!formatted.idempotent) {
        result.message += L" — warning: the formatter is not idempotent";
    }
    return result;
}

void Log(const std::wstring& message) { OutputLog::Instance().AppendLine(message); }

void ReportError(const std::wstring& message) {
    PluginHost& host = PluginHost::Instance();
    OutputLog::Instance().Show(host.Npp());
    OutputLog::Instance().AppendLine(message);
}

void AppendBlock(const std::wstring& text) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(L'\n', start);
        const std::wstring line =
            end == std::wstring::npos ? text.substr(start) : text.substr(start, end - start);
        OutputLog::Instance().AppendLine(line);
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
}

void RunDocumentFormat(bool apply, const wchar_t* commandName, bool minifyOverride = false) {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }

    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    BusyGuard busy(host);
    const Settings& settings = host.GetSettings();
    const std::wstring path = CurrentFilePath(host.Npp());
    ConfigResolver resolver(settings);
    const BufferFormatResult result =
        FormatBufferDocument(host.Npp(), scintilla, path, apply, settings, resolver,
                             minifyOverride);

    const std::wstring prefix = commandName != nullptr ? std::wstring(commandName) + L": " : L"";
    if (result.status == BufferStatus::Error) {
        ReportError(prefix + result.message);
    } else {
        Log(prefix + result.message);
        if (result.status == BufferStatus::Unsupported) {
            ShowFormatUnsupported(host.Npp(), Format::LanguageDisplayName(result.language));
        }
    }
}

}  // namespace

const CommandSpec* CommandTable(int& count) {
    count = kCommandCount;
    return kCommands;
}

const Menu::SubmenuGroup* SubmenuGroups(int& count) {
    count = kSubmenuGroupCount;
    return kSubmenuGroups;
}

std::wstring CurrentFilePath(HWND nppHandle) {
    if (nppHandle == nullptr) {
        return {};
    }
    wchar_t buffer[kMaxPathLength] = {};
    const LRESULT succeeded = ::SendMessageW(
        nppHandle, NPPM_GETFULLCURRENTPATH, static_cast<WPARAM>(std::size(buffer)),
        reinterpret_cast<LPARAM>(buffer));
    if (succeeded == FALSE) {
        return {};
    }
    return std::wstring(buffer);
}

void UpdateFormatOnSaveCheck() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const int commandId = funcItem[static_cast<int>(Cmd::ToggleFormatOnSave)]._cmdID;
    if (commandId <= 0) {
        return;
    }
    ::SendMessageW(host.Npp(), NPPM_SETMENUITEMCHECK, static_cast<WPARAM>(commandId),
                   host.GetSettings().formatOnSave ? TRUE : FALSE);
}

void OnReady() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }

    int groupCount = 0;
    const Menu::SubmenuGroup* groups = SubmenuGroups(groupCount);
    std::wstring error;
    if (!Menu::InstallSubmenus(host.Npp(), funcItem, kCommandCount, NPP_PLUGIN_NAME, groups,
                               groupCount, error)) {
        Menu::ApplyFlatPrefixes(funcItem, kCommandCount, groups, groupCount);
        Log(L"Submenus are unavailable, using flat menu items: " + error);
    }

    UpdateFormatOnSaveCheck();
}

void OnFileBeforeSave() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    if (!settings.formatOnSave) {
        return;
    }

    const std::wstring path = CurrentFilePath(host.Npp());
    if (path.empty() || !IsExtensionAllowed(settings, path)) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        return;
    }

    BusyGuard busy(host);
    ConfigResolver resolver(settings);
    const BufferFormatResult result =
        FormatBufferDocument(host.Npp(), scintilla, path, true, settings, resolver);
    if (result.status == BufferStatus::Applied) {
        Log(L"Format on save: " + path + L" — " + result.message);
    } else if (result.status == BufferStatus::Error) {
        Log(L"Format on save failed: " + path + L" — " + result.message);
    }
}

void OnShutdown() {
    OutputLog::Instance().Destroy();
    PluginHost::Instance().Shutdown();
}

void CommandFormatDocument() { RunDocumentFormat(true, L"Format Document"); }

void CommandFormatMinifyDocument() { RunDocumentFormat(true, L"Minify Document", true); }

void CommandFormatSelection() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }

    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    std::string selection;
    std::size_t start = 0;
    std::size_t end = 0;
    std::wstring error;
    if (!ReadSelection(scintilla, selection, start, end, error)) {
        ReportError(L"Format Selection: " + error);
        return;
    }
    if (start == end) {
        // Without a selection the command behaves like Format Document, which
        // is also what Notepad++ users expect from range formatting commands.
        RunDocumentFormat(true, L"Format Selection");
        return;
    }

    BusyGuard busy(host);
    const Settings& settings = host.GetSettings();
    const std::wstring path = CurrentFilePath(host.Npp());
    ConfigResolver resolver(settings);

    if (::SendMessageW(scintilla, SCI_GETREADONLY, 0, 0) != 0) {
        ReportError(L"Format Selection: the document is read-only");
        return;
    }

    const ResolvedConfig config = ResolveConfig(resolver, settings, path);
    if (settings.requireConfig && !config.hasConfig) {
        Log(L"Format Selection: skipped (no configuration file found)");
        return;
    }
    if (!path.empty() && resolver.IsIgnored(path)) {
        Log(L"Format Selection: skipped (ignored)");
        return;
    }

    const Format::Language language = ResolveLanguage(host.Npp(), path, selection);
    if (!Format::IsLanguageImplemented(language)) {
        Log(L"Format Selection: " + UnsupportedMessage(language));
        ShowFormatUnsupported(host.Npp(), Format::LanguageDisplayName(language));
        return;
    }

    FormatTimeout timeout(settings.format.timeoutMs);
    const Format::FormatResult formatted =
        Format::FormatText(selection, language, config.options, timeout.CancelFlag());
    timeout.Finish();

    if (formatted.canceled) {
        ReportError(L"Format Selection: formatting timed out");
        return;
    }
    if (!formatted.ok) {
        ::SendMessageW(scintilla, SCI_GOTOPOS, static_cast<WPARAM>(start), 0);
        ::SendMessageW(scintilla, SCI_SCROLLCARET, 0, 0);
        ReportError(L"Format Selection: " + DescribeError(formatted.error));
        return;
    }
    if (formatted.output.empty()) {
        Log(L"Format Selection: nothing to format");
        return;
    }
    if (formatted.output == selection) {
        Log(L"Format Selection: already formatted (no change)");
        return;
    }

    const int codepage =
        static_cast<int>(::SendMessageW(scintilla, SCI_GETCODEPAGE, 0, 0));
    const std::string originalBytes = EncodeForDocument(selection, codepage);
    const CaretSnapshot caret = CaptureCaret(scintilla);
    if (!ApplyFormattedSelection(scintilla, start, end, originalBytes, formatted.output, codepage,
                                 caret, error)) {
        ReportError(L"Format Selection: " + error);
        return;
    }
    Log(L"Format Selection: formatted");
}

void CommandCheckFormatting() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }

    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    BusyGuard busy(host);
    const Settings& settings = host.GetSettings();
    const std::wstring path = CurrentFilePath(host.Npp());
    ConfigResolver resolver(settings);
    const BufferFormatResult result =
        FormatBufferDocument(host.Npp(), scintilla, path, false, settings, resolver);

    OutputLog::Instance().Show(host.Npp());
    OutputLog::Instance().AppendLine(L"Check Formatting: " + result.message);
    if (result.status == BufferStatus::Unsupported) {
        ShowFormatUnsupported(host.Npp(), Format::LanguageDisplayName(result.language));
    }
}

void CommandToggleFormatOnSave() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    Settings& settings = host.GetSettings();
    settings.formatOnSave = !settings.formatOnSave;
    host.SaveSettings();
    UpdateFormatOnSaveCheck();
    Log(std::wstring(L"Format on save is now ") +
        (settings.formatOnSave ? L"enabled." : L"disabled."));
}

void CommandFormatAllOpenDocuments() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }

    const Settings& settings = host.GetSettings();
    BusyGuard busy(host);

    const int count = static_cast<int>(
        ::SendMessageW(host.Npp(), NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES));
    if (count <= 0) {
        Log(L"Format All Open Documents: no open documents");
        return;
    }

    std::vector<std::vector<wchar_t>> buffers(
        static_cast<std::size_t>(count), std::vector<wchar_t>(kMaxPathLength));
    std::vector<wchar_t*> pointers;
    pointers.reserve(buffers.size());
    for (std::vector<wchar_t>& buffer : buffers) {
        pointers.push_back(buffer.data());
    }

    const int copied = static_cast<int>(::SendMessageW(
        host.Npp(), NPPM_GETOPENFILENAMES_DEPRECATED,
        reinterpret_cast<WPARAM>(pointers.data()), static_cast<LPARAM>(count)));
    if (copied <= 0) {
        Log(L"Format All Open Documents: no file paths available");
        return;
    }

    OutputLog::Instance().Show(host.Npp());
    Log(L"Format All Open Documents: processing " + std::to_wstring(copied) + L" file(s)");

    ConfigResolver resolver(settings);
    int formatted = 0;
    int unchanged = 0;
    int skipped = 0;
    int failed = 0;

    for (int index = 0; index < copied && index < count; ++index) {
        const std::wstring path(pointers[static_cast<std::size_t>(index)]);
        if (path.empty()) {
            ++skipped;
            continue;
        }
        if (!IsExtensionAllowed(settings, path)) {
            ++skipped;
            continue;
        }
        if (!Format::IsLanguageImplemented(Format::DetectLanguageFromPath(ToUtf8(path)))) {
            Log(L"  unsupported: " + path);
            ++skipped;
            continue;
        }
        if (::SendMessageW(host.Npp(), NPPM_DOOPEN, 0,
                           reinterpret_cast<LPARAM>(path.c_str())) == FALSE) {
            Log(L"  failed to open: " + path);
            ++failed;
            continue;
        }
        HWND scintilla = host.CurrentScintilla();
        if (scintilla == nullptr) {
            ++failed;
            continue;
        }

        const BufferFormatResult result =
            FormatBufferDocument(host.Npp(), scintilla, path, true, settings, resolver);
        switch (result.status) {
            case BufferStatus::Applied:
                ::SendMessageW(host.Npp(), NPPM_SAVECURRENTFILE, 0, 0);
                Log(L"  formatted: " + path);
                ++formatted;
                break;
            case BufferStatus::Unchanged:
                ++unchanged;
                break;
            case BufferStatus::WouldChange:
                ++unchanged;
                break;
            case BufferStatus::Skipped:
                Log(L"  skipped: " + path + L" — " + result.message);
                ++skipped;
                break;
            case BufferStatus::Unsupported:
                Log(L"  unsupported: " + path + L" — " + result.message);
                ++skipped;
                break;
            case BufferStatus::Error:
                Log(L"  failed: " + path + L" — " + result.message);
                ++failed;
                break;
        }
    }

    Log(L"Format All Open Documents finished: " + std::to_wstring(formatted) + L" formatted, " +
        std::to_wstring(unchanged) + L" unchanged, " + std::to_wstring(failed) + L" failed, " +
        std::to_wstring(skipped) + L" skipped");
}

void CommandFormatFolder() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    OutputLog::Instance().Show(host.Npp());
    FormatFolderInteractive(host.Npp(), host.GetSettings());
}

void CommandShowResolvedConfig() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }

    const Settings& settings = host.GetSettings();
    const std::wstring path = CurrentFilePath(host.Npp());
    ConfigResolver resolver(settings);
    const ResolvedConfig config = ResolveConfig(resolver, settings, path);

    Format::Language language = LanguageForDocument(host.Npp(), path);
    if (!Format::IsLanguageImplemented(language)) {
        ScintillaDocument document;
        std::wstring readError;
        HWND scintilla = host.CurrentScintilla();
        if (scintilla != nullptr && ReadDocument(scintilla, document, readError)) {
            language = ResolveLanguage(host.Npp(), path, document.utf8);
        }
    }

    OutputLog::Instance().Show(host.Npp());
    OutputLog::Instance().AppendLine(
        path.empty() ? std::wstring(L"Resolved configuration (unsaved buffer)")
                     : L"Resolved configuration for " + path);
    OutputLog::Instance().AppendLine(L"  language: " +
                                     std::wstring(Format::LanguageDisplayName(language)));
    OutputLog::Instance().AppendLine(
        L"  config file: " + (config.hasConfig ? config.configPath : std::wstring(L"<none>")));
    OutputLog::Instance().AppendLine(L"  INI file: " +
                                     (settings.path.empty() ? std::wstring(L"<none>")
                                                            : settings.path));
    AppendBlock(ToWide(Format::DescribeOptions(config.options)));
    if (!config.notes.empty()) {
        std::size_t start = 0;
        while (start <= config.notes.size()) {
            const std::size_t end = config.notes.find(L'\n', start);
            const std::wstring note = end == std::wstring::npos
                                          ? config.notes.substr(start)
                                          : config.notes.substr(start, end - start);
            if (!note.empty()) {
                OutputLog::Instance().AppendLine(L"  note: " + note);
            }
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }
    }
}

void CommandFormatSettings() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }

    Settings& settings = host.GetSettings();
    if (!ShowFormatSettings(host.Npp(), settings)) {
        return;
    }
    host.SaveSettings();
    UpdateFormatOnSaveCheck();
    Log(L"Format settings saved to " + settings.path);
}

void CommandShowFormatLog() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    OutputLog::Instance().Show(host.Npp());
}

// --- Encrypt / Decrypt & Encoding Helpers --------------------------------

namespace {

bool PickOpenFile(HWND owner, const wchar_t* title, const wchar_t* filter, std::wstring& outPath) {
    wchar_t filename[kMaxPathLength] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (::GetOpenFileNameW(&ofn)) {
        outPath = filename;
        return true;
    }
    return false;
}

bool PickSaveFile(HWND owner, const wchar_t* title, const wchar_t* filter,
                  const std::wstring& defaultPath, std::wstring& outPath) {
    wchar_t filename[kMaxPathLength] = {};
    if (!defaultPath.empty()) {
        wcsncpy_s(filename, defaultPath.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = static_cast<DWORD>(std::size(filename));
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (::GetSaveFileNameW(&ofn)) {
        outPath = filename;
        return true;
    }
    return false;
}

using TextTransformFn = std::function<bool(std::string_view input, std::string& output, std::wstring& error)>;

void TransformSelectionOrDocument(const wchar_t* actionName, TextTransformFn transform) {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    std::string selUtf8;
    std::size_t selStart = 0;
    std::size_t selEnd = 0;
    std::wstring readErr;
    const bool hasSelection = ReadSelection(scintilla, selUtf8, selStart, selEnd, readErr) && !selUtf8.empty();

    if (hasSelection) {
        std::string transformed;
        std::wstring err;
        if (!transform(selUtf8, transformed, err)) {
            if (!err.empty()) {
                ::MessageBoxW(host.Npp(), err.c_str(), actionName, MB_OK | MB_ICONERROR);
                ReportError(std::wstring(actionName) + L" failed: " + err);
            }
            return;
        }
        CaretSnapshot caret = CaptureCaret(scintilla);
        std::wstring applyErr;
        if (ApplyFormattedSelection(scintilla, selStart, selEnd, selUtf8, transformed, 65001, caret, applyErr)) {
            Log(std::wstring(actionName) + L" applied to selection.");
        } else {
            ReportError(std::wstring(actionName) + L" failed: " + applyErr);
        }
    } else {
        ScintillaDocument doc;
        if (!ReadDocument(scintilla, doc, readErr)) {
            ReportError(L"Failed to read document: " + readErr);
            return;
        }
        std::string transformed;
        std::wstring err;
        if (!transform(doc.utf8, transformed, err)) {
            if (!err.empty()) {
                ::MessageBoxW(host.Npp(), err.c_str(), actionName, MB_OK | MB_ICONERROR);
                ReportError(std::wstring(actionName) + L" failed: " + err);
            }
            return;
        }
        CaretSnapshot caret = CaptureCaret(scintilla);
        std::wstring applyErr;
        if (ApplyFormattedText(scintilla, doc, transformed, caret, applyErr)) {
            Log(std::wstring(actionName) + L" applied to document.");
        } else {
            ReportError(std::wstring(actionName) + L" failed: " + applyErr);
        }
    }
}

}  // namespace

// --- Encrypt / Decrypt Handlers ------------------------------------------

void CommandCryptoEncryptSelection() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    std::string text;
    std::size_t selStart = 0;
    std::size_t selEnd = 0;
    std::wstring readErr;
    const bool isSel = ReadSelection(scintilla, text, selStart, selEnd, readErr) && !text.empty();
    ScintillaDocument doc;
    if (!isSel) {
        if (!ReadDocument(scintilla, doc, readErr)) {
            ReportError(L"Failed to read document: " + readErr);
            return;
        }
        text = doc.utf8;
    }

    const std::wstring targetDesc = isSel ? (L"Target: Selected text (" + std::to_wstring(text.size()) + L" bytes)")
                                         : (L"Target: Entire document (" + std::to_wstring(text.size()) + L" bytes)");

    const Crypto::EncryptDialogResult dlg =
        Crypto::ShowEncryptDialog(host.Npp(), targetDesc, host.GetSettings(), true);
    if (!dlg.ok) {
        return;
    }

    const std::vector<unsigned char> plain(text.begin(), text.end());
    const Crypto::CryptoResult encRes =
        Crypto::CryptoService::Instance().Encrypt(plain, dlg.passphrase, dlg.options);

    if (encRes.status != Crypto::CryptoStatus::Ok) {
        ::MessageBoxW(host.Npp(), encRes.message.c_str(), L"Encryption Error", MB_OK | MB_ICONERROR);
        ReportError(L"Encryption failed: " + encRes.message);
        return;
    }

    std::string outputText;
    if (dlg.options.armorBase64) {
        outputText = Crypto::CryptoService::Instance().ArmorMessage(encRes.bytes, dlg.options);
    } else {
        Crypto::Base64Options b64;
        b64.lineWrap = 64;
        const Crypto::EncodingResult b64Res = Crypto::EncodingService::Instance().EncodeBase64(encRes.bytes, b64);
        outputText = b64Res.text;
    }

    CaretSnapshot caret = CaptureCaret(scintilla);
    std::wstring applyErr;
    if (isSel) {
        if (ApplyFormattedSelection(scintilla, selStart, selEnd, text, outputText, 65001, caret, applyErr)) {
            Log(L"Encrypted selection (" + std::to_wstring(outputText.size()) + L" chars).");
        } else {
            ReportError(L"Failed to write encrypted selection: " + applyErr);
        }
    } else {
        if (ApplyFormattedText(scintilla, doc, outputText, caret, applyErr)) {
            Log(L"Encrypted document (" + std::to_wstring(outputText.size()) + L" chars).");
        } else {
            ReportError(L"Failed to write encrypted document: " + applyErr);
        }
    }
}

void CommandCryptoDecryptSelection() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    std::string text;
    std::size_t selStart = 0;
    std::size_t selEnd = 0;
    std::wstring readErr;
    const bool isSel = ReadSelection(scintilla, text, selStart, selEnd, readErr) && !text.empty();
    ScintillaDocument doc;
    if (!isSel) {
        if (!ReadDocument(scintilla, doc, readErr)) {
            ReportError(L"Failed to read document: " + readErr);
            return;
        }
        text = doc.utf8;
    }

    const std::wstring targetDesc = isSel ? (L"Target: Selected text (" + std::to_wstring(text.size()) + L" chars)")
                                         : (L"Target: Entire document (" + std::to_wstring(text.size()) + L" chars)");

    std::vector<unsigned char> container;
    const bool unarmored = Crypto::CryptoService::Instance().UnarmorMessage(text, container);
    std::wstring info;
    if (unarmored) {
        info = L"Detected ArgpiSuite encrypted container.";
    }

    const Crypto::DecryptDialogResult dlg = Crypto::ShowDecryptDialog(host.Npp(), targetDesc, info);
    if (!dlg.ok) {
        return;
    }

    if (!unarmored) {
        container.assign(text.begin(), text.end());
    }

    const Crypto::CryptoResult decRes =
        Crypto::CryptoService::Instance().Decrypt(container, dlg.passphrase);

    if (decRes.status != Crypto::CryptoStatus::Ok) {
        ::MessageBoxW(host.Npp(), decRes.message.c_str(), L"Decryption Error", MB_OK | MB_ICONERROR);
        ReportError(L"Decryption failed: " + decRes.message);
        return;
    }

    const std::string plainUtf8(reinterpret_cast<const char*>(decRes.bytes.data()), decRes.bytes.size());
    CaretSnapshot caret = CaptureCaret(scintilla);
    std::wstring applyErr;
    if (isSel) {
        if (ApplyFormattedSelection(scintilla, selStart, selEnd, text, plainUtf8, 65001, caret, applyErr)) {
            Log(L"Decrypted selection successfully (" + std::to_wstring(plainUtf8.size()) + L" chars).");
        } else {
            ReportError(L"Failed to write decrypted selection: " + applyErr);
        }
    } else {
        if (ApplyFormattedText(scintilla, doc, plainUtf8, caret, applyErr)) {
            Log(L"Decrypted document successfully (" + std::to_wstring(plainUtf8.size()) + L" chars).");
        } else {
            ReportError(L"Failed to write decrypted document: " + applyErr);
        }
    }
}

void CommandCryptoEncryptDocument() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    ScintillaDocument doc;
    std::wstring readErr;
    if (!ReadDocument(scintilla, doc, readErr)) {
        ReportError(L"Failed to read document: " + readErr);
        return;
    }

    const std::wstring targetDesc = L"Target: Entire document (" + std::to_wstring(doc.utf8.size()) + L" bytes)";
    const Crypto::EncryptDialogResult dlg =
        Crypto::ShowEncryptDialog(host.Npp(), targetDesc, host.GetSettings(), true);
    if (!dlg.ok) {
        return;
    }

    const std::vector<unsigned char> plain(doc.utf8.begin(), doc.utf8.end());
    const Crypto::CryptoResult encRes =
        Crypto::CryptoService::Instance().Encrypt(plain, dlg.passphrase, dlg.options);

    if (encRes.status != Crypto::CryptoStatus::Ok) {
        ::MessageBoxW(host.Npp(), encRes.message.c_str(), L"Encryption Error", MB_OK | MB_ICONERROR);
        ReportError(L"Encryption failed: " + encRes.message);
        return;
    }

    std::string outputText;
    if (dlg.options.armorBase64) {
        outputText = Crypto::CryptoService::Instance().ArmorMessage(encRes.bytes, dlg.options);
    } else {
        Crypto::Base64Options b64;
        b64.lineWrap = 64;
        outputText = Crypto::EncodingService::Instance().EncodeBase64(encRes.bytes, b64).text;
    }

    CaretSnapshot caret = CaptureCaret(scintilla);
    std::wstring applyErr;
    if (ApplyFormattedText(scintilla, doc, outputText, caret, applyErr)) {
        Log(L"Encrypted document (" + std::to_wstring(outputText.size()) + L" chars).");
    } else {
        ReportError(L"Failed to write encrypted document: " + applyErr);
    }
}

void CommandCryptoDecryptDocument() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized() || host.IsBusy()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        ReportError(L"No active editor view.");
        return;
    }

    ScintillaDocument doc;
    std::wstring readErr;
    if (!ReadDocument(scintilla, doc, readErr)) {
        ReportError(L"Failed to read document: " + readErr);
        return;
    }

    std::vector<unsigned char> container;
    const bool unarmored = Crypto::CryptoService::Instance().UnarmorMessage(doc.utf8, container);
    const std::wstring info = unarmored ? L"Detected ArgpiSuite encrypted container." : L"";

    const Crypto::DecryptDialogResult dlg =
        Crypto::ShowDecryptDialog(host.Npp(), L"Target: Entire document", info);
    if (!dlg.ok) {
        return;
    }

    if (!unarmored) {
        container.assign(doc.utf8.begin(), doc.utf8.end());
    }

    const Crypto::CryptoResult decRes =
        Crypto::CryptoService::Instance().Decrypt(container, dlg.passphrase);

    if (decRes.status != Crypto::CryptoStatus::Ok) {
        ::MessageBoxW(host.Npp(), decRes.message.c_str(), L"Decryption Error", MB_OK | MB_ICONERROR);
        ReportError(L"Decryption failed: " + decRes.message);
        return;
    }

    const std::string plainUtf8(reinterpret_cast<const char*>(decRes.bytes.data()), decRes.bytes.size());
    CaretSnapshot caret = CaptureCaret(scintilla);
    std::wstring applyErr;
    if (ApplyFormattedText(scintilla, doc, plainUtf8, caret, applyErr)) {
        Log(L"Decrypted document successfully (" + std::to_wstring(plainUtf8.size()) + L" chars).");
    } else {
        ReportError(L"Failed to write decrypted document: " + applyErr);
    }
}

void CommandCryptoEncryptFile() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }

    std::wstring srcPath;
    if (!PickOpenFile(host.Npp(), L"Select File to Encrypt",
                      L"All Files (*.*)\0*.*\0Text Files (*.txt)\0*.txt\0\0", srcPath)) {
        return;
    }

    const std::wstring defDst = srcPath + L".enc";
    std::wstring dstPath;
    if (!PickSaveFile(host.Npp(), L"Save Encrypted File As",
                      L"Encrypted Files (*.enc)\0*.enc\0All Files (*.*)\0*.*\0\0", defDst, dstPath)) {
        return;
    }

    const Crypto::EncryptDialogResult dlg =
        Crypto::ShowEncryptDialog(host.Npp(), L"Target File: " + srcPath, host.GetSettings(), false);
    if (!dlg.ok) {
        return;
    }

    const Crypto::CryptoResult res =
        Crypto::CryptoService::Instance().EncryptFileOnDisk(srcPath, dstPath, dlg.passphrase, dlg.options);
    if (res.status == Crypto::CryptoStatus::Ok) {
        ::MessageBoxW(host.Npp(), res.message.c_str(), L"File Encrypted", MB_OK | MB_ICONINFORMATION);
        Log(res.message);
    } else {
        ::MessageBoxW(host.Npp(), res.message.c_str(), L"Encryption Failed", MB_OK | MB_ICONERROR);
        ReportError(res.message);
    }
}

void CommandCryptoDecryptFile() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }

    std::wstring srcPath;
    if (!PickOpenFile(host.Npp(), L"Select Encrypted File to Decrypt",
                      L"Encrypted Files (*.enc)\0*.enc\0All Files (*.*)\0*.*\0\0", srcPath)) {
        return;
    }

    std::wstring defDst = srcPath;
    if (defDst.size() > 4 && defDst.substr(defDst.size() - 4) == L".enc") {
        defDst.erase(defDst.size() - 4);
    } else {
        defDst += L".dec";
    }

    std::wstring dstPath;
    if (!PickSaveFile(host.Npp(), L"Save Decrypted File As",
                      L"All Files (*.*)\0*.*\0\0", defDst, dstPath)) {
        return;
    }

    const Crypto::DecryptDialogResult dlg =
        Crypto::ShowDecryptDialog(host.Npp(), L"Target File: " + srcPath);
    if (!dlg.ok) {
        return;
    }

    const Crypto::CryptoResult res =
        Crypto::CryptoService::Instance().DecryptFileOnDisk(srcPath, dstPath, dlg.passphrase);
    if (res.status == Crypto::CryptoStatus::Ok) {
        ::MessageBoxW(host.Npp(), res.message.c_str(), L"File Decrypted", MB_OK | MB_ICONINFORMATION);
        Log(res.message);
    } else {
        ::MessageBoxW(host.Npp(), res.message.c_str(), L"Decryption Failed", MB_OK | MB_ICONERROR);
        ReportError(res.message);
    }
}

void CommandCryptoSettings() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    Crypto::ShowCryptoSettingsDialog(host.Npp(), host.GetSettings());
}

// --- Encoding / Decoding Handlers ----------------------------------------

void CommandEncodingBase16Encode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    Crypto::Base16Options opts;
    opts.uppercase = s.encoding.base16Uppercase;
    opts.delimiter = s.encoding.base16Delimiter;

    TransformSelectionOrDocument(L"Base16 Encode", [opts](std::string_view input, std::string& out, std::wstring& err) {
        const std::vector<uint8_t> bytes(input.begin(), input.end());
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().EncodeBase16(bytes, opts);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingBase16Decode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    Crypto::Base16Options opts;
    opts.uppercase = s.encoding.base16Uppercase;
    opts.delimiter = s.encoding.base16Delimiter;

    TransformSelectionOrDocument(L"Base16 Decode", [opts](std::string_view input, std::string& out, std::wstring& err) {
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().DecodeBase16(input, opts);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingBase64Encode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    Crypto::Base64Options opts;
    opts.urlSafe = s.encoding.base64UrlSafe;
    opts.padding = s.encoding.base64Padding;

    TransformSelectionOrDocument(L"Base64 Encode", [opts](std::string_view input, std::string& out, std::wstring& err) {
        const std::vector<uint8_t> bytes(input.begin(), input.end());
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().EncodeBase64(bytes, opts);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingBase64Decode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    Crypto::Base64Options opts;
    opts.urlSafe = s.encoding.base64UrlSafe;
    opts.padding = s.encoding.base64Padding;

    TransformSelectionOrDocument(L"Base64 Decode", [opts](std::string_view input, std::string& out, std::wstring& err) {
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().DecodeBase64(input, opts);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingBase85Encode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    Crypto::Base85Variant var = Crypto::Base85Variant::Ascii85;
    if (Plugin::EqualsIgnoreCase(s.encoding.base85Variant, L"z85")) var = Crypto::Base85Variant::Z85;
    else if (Plugin::EqualsIgnoreCase(s.encoding.base85Variant, L"rfc1924")) var = Crypto::Base85Variant::Rfc1924;

    TransformSelectionOrDocument(L"Base85 Encode", [var](std::string_view input, std::string& out, std::wstring& err) {
        const std::vector<uint8_t> bytes(input.begin(), input.end());
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().EncodeBase85(bytes, var);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingBase85Decode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    Crypto::Base85Variant var = Crypto::Base85Variant::Ascii85;
    if (Plugin::EqualsIgnoreCase(s.encoding.base85Variant, L"z85")) var = Crypto::Base85Variant::Z85;
    else if (Plugin::EqualsIgnoreCase(s.encoding.base85Variant, L"rfc1924")) var = Crypto::Base85Variant::Rfc1924;

    TransformSelectionOrDocument(L"Base85 Decode", [var](std::string_view input, std::string& out, std::wstring& err) {
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().DecodeBase85(input, var);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingCustomEncode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    const std::wstring alpha = s.encoding.customAlphabet;

    TransformSelectionOrDocument(L"Custom Base Encode", [alpha](std::string_view input, std::string& out, std::wstring& err) {
        const std::vector<uint8_t> bytes(input.begin(), input.end());
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().EncodeCustom(bytes, alpha);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingCustomDecode() {
    PluginHost& host = PluginHost::Instance();
    const Settings& s = host.GetSettings();
    const std::wstring alpha = s.encoding.customAlphabet;

    TransformSelectionOrDocument(L"Custom Base Decode", [alpha](std::string_view input, std::string& out, std::wstring& err) {
        const Crypto::EncodingResult res = Crypto::EncodingService::Instance().DecodeCustom(input, alpha);
        if (res.ok) {
            out = res.text;
            return true;
        }
        err = res.message;
        return false;
    });
}

void CommandEncodingSettings() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    Crypto::ShowEncodingSettingsDialog(host.Npp(), host.GetSettings());
}

// --- Generate Password Handlers ------------------------------------------

void CommandPasswordGenerate() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    Password::ShowPasswordDialog(host.Npp(), host.GetSettings());
}

void CommandPasswordGeneratePassphrase() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    const wchar_t sep = settings.password.separator.empty() ? L'-' : settings.password.separator[0];
    const Password::PasswordResult res =
        Password::PasswordService::Instance().GeneratePassphrase(settings.password.words, sep);
    if (!res.ok) {
        Log(L"Passphrase generation failed: " + res.message);
        return;
    }

    HWND scintilla = host.CurrentScintilla();
    if (scintilla != nullptr) {
        const std::string utf8 = ToUtf8(res.value);
        ::SendMessageW(scintilla, SCI_BEGINUNDOACTION, 0, 0);
        ::SendMessageW(scintilla, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(utf8.c_str()));
        ::SendMessageW(scintilla, SCI_ENDUNDOACTION, 0, 0);
        Log(L"Passphrase inserted at caret (" + std::to_wstring(settings.password.words) + L" words).");
    } else {
        // Fallback to clipboard
        if (::OpenClipboard(host.Npp())) {
            ::EmptyClipboard();
            const size_t bytes = (res.value.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem != nullptr) {
                void* ptr = ::GlobalLock(hMem);
                if (ptr != nullptr) {
                    std::memcpy(ptr, res.value.c_str(), bytes);
                    ::GlobalUnlock(hMem);
                    ::SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            ::CloseClipboard();
            Log(L"Passphrase copied to clipboard (" + std::to_wstring(settings.password.words) + L" words).");
        }
    }
}

void CommandPasswordInsertAtCaret() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    Password::PasswordPolicy policy;
    policy.length = settings.password.length;
    policy.useLower = settings.password.useLower;
    policy.useUpper = settings.password.useUpper;
    policy.useDigits = settings.password.useDigits;
    policy.useSymbols = settings.password.useSymbols;
    policy.avoidAmbiguous = settings.password.avoidAmbiguous;
    policy.excludeChars = settings.password.excludeChars;

    const Password::PasswordResult res = Password::PasswordService::Instance().Generate(policy);
    if (!res.ok) {
        Log(L"Password generation failed: " + res.message);
        return;
    }

    HWND scintilla = host.CurrentScintilla();
    if (scintilla != nullptr) {
        const std::string utf8 = ToUtf8(res.value);
        ::SendMessageW(scintilla, SCI_BEGINUNDOACTION, 0, 0);
        ::SendMessageW(scintilla, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(utf8.c_str()));
        ::SendMessageW(scintilla, SCI_ENDUNDOACTION, 0, 0);
        Log(L"Password inserted at caret (" + std::to_wstring(policy.length) + L" chars).");
    }
}

void CommandPasswordCopyToClipboard() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    Password::PasswordPolicy policy;
    policy.length = settings.password.length;
    policy.useLower = settings.password.useLower;
    policy.useUpper = settings.password.useUpper;
    policy.useDigits = settings.password.useDigits;
    policy.useSymbols = settings.password.useSymbols;
    policy.avoidAmbiguous = settings.password.avoidAmbiguous;
    policy.excludeChars = settings.password.excludeChars;

    const Password::PasswordResult res = Password::PasswordService::Instance().Generate(policy);
    if (!res.ok) {
        Log(L"Password generation failed: " + res.message);
        return;
    }

    if (::OpenClipboard(host.Npp())) {
        ::EmptyClipboard();
        const size_t bytes = (res.value.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (hMem != nullptr) {
            void* ptr = ::GlobalLock(hMem);
            if (ptr != nullptr) {
                std::memcpy(ptr, res.value.c_str(), bytes);
                ::GlobalUnlock(hMem);
                ::SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        ::CloseClipboard();
        Log(L"Password copied to clipboard (" + std::to_wstring(policy.length) + L" chars).");
    }
}

void CommandPasswordSettings() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    Password::ShowPasswordDialog(host.Npp(), host.GetSettings());
}

// --- UUID / GUID & ULID Generator ----------------------------------------
namespace {

std::string ActiveDocumentEol(HWND scintilla) {
    if (scintilla == nullptr) {
        return "\r\n";
    }
    const LRESULT eolMode = ::SendMessageW(scintilla, SCI_GETEOLMODE, 0, 0);
    if (eolMode == SC_EOL_LF) {
        return "\n";
    }
    if (eolMode == SC_EOL_CR) {
        return "\r";
    }
    return "\r\n";
}

bool InsertGeneratedAtCaret(HWND scintilla, const std::string& utf8Text) {
    if (scintilla == nullptr || ::SendMessageW(scintilla, SCI_GETREADONLY, 0, 0) != 0) {
        return false;
    }
    ::SendMessageW(scintilla, SCI_BEGINUNDOACTION, 0, 0);
    ::SendMessageW(scintilla, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(utf8Text.c_str()));
    ::SendMessageW(scintilla, SCI_ENDUNDOACTION, 0, 0);
    return true;
}

void InsertSingleGeneratedId(Generator::IdType type, const Generator::IdFormatOptions& format,
                             const wchar_t* label) {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Generator::IdGenerateResult res =
        Generator::IdGeneratorService::Instance().GenerateSingle(type, format);
    if (!res.ok) {
        Log(std::wstring(label) + L" generation failed: " + res.message);
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (!InsertGeneratedAtCaret(scintilla, res.text)) {
        Log(std::wstring(label) + L" could not be inserted (editor unavailable or read-only).");
        return;
    }
    Log(std::wstring(label) + L" inserted at cursor: " + ToWide(res.text));
}

Generator::IdFormatOptions UuidFormatFromSettings(const Settings& settings) {
    Generator::IdFormatOptions fmt;
    fmt.uppercase = settings.generator.uuidUppercase;
    fmt.hyphens = settings.generator.uuidHyphens;
    fmt.braces = settings.generator.uuidBraces;
    fmt.quotes = Generator::ParseQuoteStyle(settings.generator.quotes);
    return fmt;
}

}  // namespace

void CommandGeneratorUuid() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    const Generator::IdType type =
        EqualsIgnoreCase(settings.generator.defaultUuidVersion, L"v7")
            ? Generator::IdType::UuidV7
            : Generator::IdType::UuidV4;
    InsertSingleGeneratedId(type, UuidFormatFromSettings(settings), L"UUID");
}

void CommandGeneratorGuid() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    Generator::IdFormatOptions fmt;
    fmt.uppercase = settings.generator.guidUppercase;
    fmt.hyphens = true;
    fmt.braces = settings.generator.guidBraces;
    fmt.quotes = Generator::ParseQuoteStyle(settings.generator.quotes);
    InsertSingleGeneratedId(Generator::IdType::Guid, fmt, L"GUID");
}

void CommandGeneratorUlid() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    const Settings& settings = host.GetSettings();
    Generator::IdFormatOptions fmt;
    fmt.uppercase = !settings.generator.ulidLowercase;
    fmt.quotes = Generator::ParseQuoteStyle(settings.generator.quotes);
    InsertSingleGeneratedId(Generator::IdType::Ulid, fmt, L"ULID");
}

void CommandGeneratorUuidV4() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    InsertSingleGeneratedId(Generator::IdType::UuidV4,
                            UuidFormatFromSettings(host.GetSettings()), L"UUID v4");
}

void CommandGeneratorUuidV7() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    InsertSingleGeneratedId(Generator::IdType::UuidV7,
                            UuidFormatFromSettings(host.GetSettings()), L"UUID v7");
}

void CommandGeneratorBulkDialog() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    const std::string eol = ActiveDocumentEol(scintilla);
    Generator::ShowIdGeneratorDialog(
        host.Npp(), host.GetSettings(), eol,
        [&host](const std::string& utf8Text, int count) -> bool {
            HWND target = host.CurrentScintilla();
            if (!InsertGeneratedAtCaret(target, utf8Text)) {
                return false;
            }
            Log(L"Inserted " + std::to_wstring(count) + L" generated ID(s) at cursor.");
            return true;
        });
}

// --- Hashing -------------------------------------------------------------
namespace {

bool CopyHashToClipboard(HWND owner, const std::string& utf8) {
    const std::wstring wide = ToWide(utf8);
    if (!::OpenClipboard(owner)) {
        return false;
    }
    ::EmptyClipboard();
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != nullptr) {
        void* ptr = ::GlobalLock(hMem);
        if (ptr != nullptr) {
            std::memcpy(ptr, wide.c_str(), bytes);
            ::GlobalUnlock(hMem);
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    ::CloseClipboard();
    return true;
}

void RunQuickHash(Hash::HashAlgorithm alg) {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    if (scintilla == nullptr) {
        return;
    }

    std::string selUtf8;
    std::size_t start = 0;
    std::size_t end = 0;
    std::wstring error;
    const bool hasSel = ReadSelection(scintilla, selUtf8, start, end, error) && start != end;

    std::string payload;
    std::wstring sourceLabel;
    if (hasSel) {
        payload = std::move(selUtf8);
        sourceLabel = L"selection";
    } else {
        ScintillaDocument doc;
        if (!ReadDocument(scintilla, doc, error)) {
            Log(L"Hash failed: " + error);
            return;
        }
        payload = std::move(doc.raw);
        sourceLabel = L"document";
    }

    const Settings& settings = host.GetSettings();
    Hash::HashOptions opts;
    opts.algorithm = alg;
    opts.uppercase = settings.hashing.uppercase;

    const Hash::HashResult res = Hash::HashService::Instance().Compute(payload, opts);
    if (!res.ok) {
        Log(Hash::AlgorithmDisplayName(alg) + L" failed: " + res.message);
        return;
    }

    const std::wstring algName = Hash::AlgorithmDisplayName(alg);
    if (EqualsIgnoreCase(settings.hashing.quickAction, L"insert")) {
        if (!InsertGeneratedAtCaret(scintilla, res.hex)) {
            Log(algName + L" could not be inserted (editor is read-only).");
            return;
        }
        Log(algName + L" (" + sourceLabel + L", " + std::to_wstring(res.inputBytes) +
            L" bytes) inserted: " + ToWide(res.hex));
    } else {
        const bool copied = CopyHashToClipboard(host.Npp(), res.hex);
        Log(algName + L" (" + sourceLabel + L", " + std::to_wstring(res.inputBytes) +
            L" bytes): " + ToWide(res.hex) +
            (copied ? L" [copied to clipboard]" : L" [clipboard unavailable]"));
    }
}

void OpenHashToolDialog(bool useOverride, Hash::HashAlgorithm overrideAlg) {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    Hash::HashDialogInput input;
    input.useOverrideAlgorithm = useOverride;
    input.overrideAlgorithm = overrideAlg;
    input.activeFilePath = CurrentFilePath(host.Npp());

    if (scintilla != nullptr) {
        std::string selUtf8;
        std::size_t start = 0;
        std::size_t end = 0;
        std::wstring error;
        if (ReadSelection(scintilla, selUtf8, start, end, error) && start != end) {
            input.hasSelection = true;
            input.selectionBytes = std::move(selUtf8);
        }
        ScintillaDocument doc;
        if (ReadDocument(scintilla, doc, error)) {
            input.documentBytes = std::move(doc.raw);
        }
    }

    Hash::ShowHashDialog(
        host.Npp(), host.GetSettings(), input,
        [&host](const std::string& utf8Text) -> bool {
            HWND target = host.CurrentScintilla();
            if (!InsertGeneratedAtCaret(target, utf8Text)) {
                return false;
            }
            Log(L"Hash digest inserted at cursor: " + ToWide(utf8Text));
            return true;
        });
}

}  // namespace

void CommandHashMd5() {
    RunQuickHash(Hash::HashAlgorithm::Md5);
}

void CommandHashSha256() {
    RunQuickHash(Hash::HashAlgorithm::Sha256);
}

void CommandHashSha512() {
    RunQuickHash(Hash::HashAlgorithm::Sha512);
}

void CommandHashDoubleSha256() {
    RunQuickHash(Hash::HashAlgorithm::DoubleSha256);
}

void CommandHashHmacSha256() {
    OpenHashToolDialog(true, Hash::HashAlgorithm::HmacSha256);
}

void CommandHashCrc32() {
    RunQuickHash(Hash::HashAlgorithm::Crc32);
}

void CommandHashToolDialog() {
    OpenHashToolDialog(false, Hash::HashAlgorithm::Sha256);
}

// --- Tools ---------------------------------------------------------------
namespace {

bool ApplyToolOutputToEditor(HWND scintilla, const std::string& utf8Text, bool applyToSelection,
                             std::size_t selStart, std::size_t selEnd,
                             const std::string& originalSelBytes,
                             const ScintillaDocument& doc) {
    if (scintilla == nullptr || ::SendMessageW(scintilla, SCI_GETREADONLY, 0, 0) != 0) {
        return false;
    }
    const CaretSnapshot caret = CaptureCaret(scintilla);
    std::wstring error;
    if (applyToSelection) {
        return ApplyFormattedSelection(scintilla, selStart, selEnd, originalSelBytes, utf8Text,
                                       doc.codepage, caret, error);
    }
    return ApplyFormattedText(scintilla, doc, utf8Text, caret, error);
}

}  // namespace

void CommandToolJwtDecoder() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    std::string initialToken;
    if (scintilla != nullptr) {
        std::string selUtf8;
        std::size_t start = 0;
        std::size_t end = 0;
        std::wstring error;
        if (ReadSelection(scintilla, selUtf8, start, end, error) && start != end) {
            initialToken = std::move(selUtf8);
        } else {
            ScintillaDocument doc;
            if (ReadDocument(scintilla, doc, error)) {
                const std::string trimmed = ToUtf8(Trim(ToWide(doc.utf8)));
                if (trimmed.compare(0, 4, "eyJ") == 0 || trimmed.compare(0, 10, "Bearer eyJ") == 0) {
                    initialToken = trimmed;
                }
            }
        }
    }

    Tools::ShowJwtDecoderDialog(
        host.Npp(), initialToken, [&host](const std::string& utf8Text) -> bool {
            HWND target = host.CurrentScintilla();
            if (!InsertGeneratedAtCaret(target, utf8Text)) {
                return false;
            }
            Log(L"Decoded JWT JSON inserted at cursor.");
            return true;
        });
}

void CommandToolPathQuerying() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    Tools::PathQueryDialogInput input;
    std::size_t selStart = 0;
    std::size_t selEnd = 0;
    ScintillaDocument doc;
    std::string rawSelBytes;

    if (scintilla != nullptr) {
        std::wstring error;
        if (ReadDocument(scintilla, doc, error)) {
            input.documentText = doc.utf8;
        }
        std::string selUtf8;
        if (ReadSelection(scintilla, selUtf8, selStart, selEnd, error) && selStart != selEnd) {
            input.hasSelection = true;
            input.selectionText = selUtf8;
            if (selEnd <= doc.raw.size() && selStart < selEnd) {
                rawSelBytes = doc.raw.substr(selStart, selEnd - selStart);
            }
        }
    }

    Tools::ShowPathQueryDialog(
        host.Npp(), host.GetSettings(), input,
        [&](const std::string& utf8Text, bool applyToSelection) -> bool {
            HWND target = host.CurrentScintilla();
            if (!ApplyToolOutputToEditor(target, utf8Text, applyToSelection, selStart, selEnd,
                                         rawSelBytes, doc)) {
                return false;
            }
            Log(L"Path query result applied to editor.");
            return true;
        });
}

void CommandToolCsvTsvAlignment() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    Tools::CsvDialogInput input;
    input.documentEol = ActiveDocumentEol(scintilla);
    std::size_t selStart = 0;
    std::size_t selEnd = 0;
    ScintillaDocument doc;
    std::string rawSelBytes;

    if (scintilla != nullptr) {
        std::wstring error;
        if (ReadDocument(scintilla, doc, error)) {
            input.documentText = doc.utf8;
        }
        std::string selUtf8;
        if (ReadSelection(scintilla, selUtf8, selStart, selEnd, error) && selStart != selEnd) {
            input.hasSelection = true;
            input.selectionText = selUtf8;
            if (selEnd <= doc.raw.size() && selStart < selEnd) {
                rawSelBytes = doc.raw.substr(selStart, selEnd - selStart);
            }
        }
    }

    Tools::ShowCsvToolDialog(
        host.Npp(), host.GetSettings(), input,
        [&](const std::string& utf8Text, bool applyToSelection) -> bool {
            HWND target = host.CurrentScintilla();
            if (!ApplyToolOutputToEditor(target, utf8Text, applyToSelection, selStart, selEnd,
                                         rawSelBytes, doc)) {
                return false;
            }
            Log(L"CSV / TSV transformation applied to editor.");
            return true;
        });
}

void CommandToolSmartDeduplication() {
    PluginHost& host = PluginHost::Instance();
    if (!host.IsInitialized()) {
        return;
    }
    HWND scintilla = host.CurrentScintilla();
    Tools::DedupeSortDialogInput input;
    input.documentEol = ActiveDocumentEol(scintilla);
    std::size_t selStart = 0;
    std::size_t selEnd = 0;
    ScintillaDocument doc;
    std::string rawSelBytes;

    if (scintilla != nullptr) {
        std::wstring error;
        if (ReadDocument(scintilla, doc, error)) {
            input.documentText = doc.utf8;
        }
        std::string selUtf8;
        if (ReadSelection(scintilla, selUtf8, selStart, selEnd, error) && selStart != selEnd) {
            input.hasSelection = true;
            input.selectionText = selUtf8;
            if (selEnd <= doc.raw.size() && selStart < selEnd) {
                rawSelBytes = doc.raw.substr(selStart, selEnd - selStart);
            }
        }
    }

    Tools::ShowDedupeSortDialog(
        host.Npp(), host.GetSettings(), input,
        [&](const std::string& utf8Text, bool applyToSelection) -> bool {
            HWND target = host.CurrentScintilla();
            if (!ApplyToolOutputToEditor(target, utf8Text, applyToSelection, selStart, selEnd,
                                         rawSelBytes, doc)) {
                return false;
            }
            Log(L"Deduplication / sorting applied to editor.");
            return true;
        });
}

void CommandAbout() {
    const wchar_t* const aboutText =
        L"ArgpiSuiteNPP v5.0\n"
        L"All-in-One Native Developer Suite for Notepad++\n\n"
        L"Includes: Code Formatter, Encrypt / Decrypt, Encode / Decode, "
        L"Password Generator, UUID / GUID & ULID Generator, Hashing, and Developer Tools.\n\n"
        L"Credits:\n"
        L"• Author: Florin D. Stoica (ArgpiSuiteNPP)\n"
        L"• Notepad++ Plugin Template & Headers: Don HO & Notepad++ contributors\n"
        L"• Formatting Behavior Reference: Prettier 3.x\n"
        L"• Cryptography & Hashing: Windows CNG (BCrypt)\n\n"
        L"License: MIT License (open source, provided without warranty)\n"
        L"GitHub: https://github.com/FlorinDStoica/ArgpiSuiteNPP.git\n\n"
        L"Note: This is the first release and may contain bugs. "
        L"Please report any issues on GitHub.";

    ::MessageBoxW(PluginHost::Instance().Npp(), aboutText, L"About ArgpiSuiteNPP",
                  MB_OK | MB_ICONINFORMATION);
}

}  // namespace ArgpiSuite::Plugin

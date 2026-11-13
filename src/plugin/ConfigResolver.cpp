#include "ConfigResolver.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "TextUtil.h"
#include "format/FormatLineEnding.h"
#include "format/json/FormatJsonParser.h"

namespace ArgpiSuite::Plugin {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxConfigDepth = 64;

std::string ReadBytes(const std::wstring& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string bytes = buffer.str();
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    return bytes;
}

bool Exists(const std::wstring& path) {
    std::error_code error;
    return fs::exists(fs::path(path), error);
}

std::int64_t StampOf(const std::wstring& path) {
    std::error_code error;
    const fs::file_time_type stamp = fs::last_write_time(fs::path(path), error);
    if (error) {
        return 0;
    }
    return static_cast<std::int64_t>(stamp.time_since_epoch().count());
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* name) {
    if (directory.empty()) {
        return name;
    }
    std::wstring path = directory;
    if (path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path.append(name);
    return path;
}

std::vector<std::wstring> SplitPathComponents(const std::wstring& path) {
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find_first_of(L"\\/", start);
        const std::size_t end = slash == std::wstring::npos ? path.size() : slash;
        if (end > start) {
            parts.push_back(path.substr(start, end - start));
        }
        if (slash == std::wstring::npos) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

std::wstring JoinComponents(const std::vector<std::wstring>& parts, std::size_t count) {
    std::wstring path;
    for (std::size_t i = 0; i < count && i < parts.size(); ++i) {
        if (i != 0) {
            path.push_back(L'\\');
        }
        path.append(parts[i]);
    }
    return path;
}

std::vector<std::wstring> ExpandBraces(const std::wstring& pattern) {
    const std::size_t open = pattern.find(L'{');
    if (open == std::wstring::npos) {
        return {pattern};
    }
    const std::size_t close = pattern.find(L'}', open + 1);
    if (close == std::wstring::npos) {
        return {pattern};
    }
    const std::wstring prefix = pattern.substr(0, open);
    const std::wstring suffix = pattern.substr(close + 1);
    const std::wstring inner = pattern.substr(open + 1, close - open - 1);
    std::vector<std::wstring> results;
    std::size_t start = 0;
    while (start <= inner.size()) {
        const std::size_t comma = inner.find(L',', start);
        const std::size_t end = comma == std::wstring::npos ? inner.size() : comma;
        const std::wstring alternative = inner.substr(start, end - start);
        for (const std::wstring& expanded : ExpandBraces(prefix + alternative + suffix)) {
            results.push_back(expanded);
        }
        if (comma == std::wstring::npos) {
            break;
        }
        start = comma + 1;
    }
    return results;
}

bool SectionMatches(const std::wstring& section, const std::wstring& fileName,
                    const std::wstring& relativePath) {
    for (const std::wstring& alternative : ExpandBraces(section)) {
        const std::wstring trimmed = Trim(alternative);
        if (trimmed.empty() || trimmed == L"*") {
            return true;
        }
        if (trimmed.find_first_of(L"\\/") != std::wstring::npos) {
            if (MatchPathGlob(trimmed, relativePath) || MatchPathGlob(trimmed, fileName)) {
                return true;
            }
        } else if (MatchPathGlob(trimmed, fileName)) {
            return true;
        }
    }
    return false;
}

int ParseInteger(const std::wstring& text, int fallback) {
    try {
        std::size_t consumed = 0;
        const int value = std::stoi(text, &consumed);
        if (consumed == 0) {
            return fallback;
        }
        return value;
    } catch (...) {
        return fallback;
    }
}

bool ApplyJsonOption(Format::FormatOptions& options, std::string_view key,
                     const Format::JsonNode& value);

bool ApplyJsonOptionText(Format::FormatOptions& options, std::string_view key,
                         std::string_view text) {
    if (key == "printWidth") {
        const int parsed = ParseInteger(ToWide(text), options.printWidth);
        if (parsed > 0) {
            options.printWidth = parsed;
        }
        return true;
    }
    if (key == "tabWidth") {
        const int parsed = ParseInteger(ToWide(text), options.tabWidth);
        if (parsed > 0) {
            options.tabWidth = parsed;
        }
        return true;
    }
    if (key == "quoteProps") {
        Format::ParseQuoteProps(text, options.quoteProps);
        return true;
    }
    if (key == "trailingComma") {
        Format::ParseTrailingComma(text, options.trailingComma);
        return true;
    }
    if (key == "arrowParens") {
        Format::ParseArrowParens(text, options.arrowParens);
        return true;
    }
    if (key == "proseWrap") {
        Format::ParseProseWrap(text, options.proseWrap);
        return true;
    }
    if (key == "htmlWhitespaceSensitivity") {
        Format::ParseHtmlWhitespaceSensitivity(text, options.htmlWhitespaceSensitivity);
        return true;
    }
    if (key == "endOfLine") {
        Format::ParseEol(text, options.endOfLine);
        return true;
    }
    if (key == "embeddedLanguageFormatting") {
        Format::ParseEmbeddedLanguageFormatting(text, options.embeddedLanguageFormatting);
        return true;
    }
    if (key == "objectWrap") {
        Format::ParseObjectWrap(text, options.objectWrap);
        return true;
    }
    return false;
}

bool ApplyJsonOptionBool(Format::FormatOptions& options, std::string_view key, bool value) {
    if (key == "useTabs") {
        options.useTabs = value;
        return true;
    }
    if (key == "semi") {
        options.semi = value;
        return true;
    }
    if (key == "singleQuote") {
        options.singleQuote = value;
        return true;
    }
    if (key == "jsxSingleQuote") {
        options.jsxSingleQuote = value;
        return true;
    }
    if (key == "bracketSpacing") {
        options.bracketSpacing = value;
        return true;
    }
    if (key == "bracketSameLine") {
        options.bracketSameLine = value;
        return true;
    }
    if (key == "singleAttributePerLine") {
        options.singleAttributePerLine = value;
        return true;
    }
    if (key == "experimentalTernaries") {
        options.experimentalTernaries = value;
        return true;
    }
    return false;
}

bool ApplyJsonOption(Format::FormatOptions& options, std::string_view key,
                     const Format::JsonNode& value) {
    bool boolean = false;
    if (Format::JsonScalarBool(value, boolean) && ApplyJsonOptionBool(options, key, boolean)) {
        return true;
    }
    const std::string text = Format::JsonScalarText(value);
    if (text.empty()) {
        return false;
    }
    return ApplyJsonOptionText(options, key, text);
}

void ApplyConfigObject(Format::FormatOptions& options, const Format::JsonDocument& document,
                       const Format::JsonNode& object) {
    for (const Format::JsonMember& member : object.members) {
        const Format::JsonNode* value = Format::JsonMemberValue(document, object, member.key);
        if (value != nullptr) {
            ApplyJsonOption(options, member.key, *value);
        }
    }
}

Format::Language DialectForFile(const std::wstring& path) {
    const std::wstring extension = ToLower(ExtensionOf(path));
    if (extension == L"json5") {
        return Format::Language::Json5;
    }
    if (extension == L"jsonc") {
        return Format::Language::Jsonc;
    }
    return Format::Language::Json;
}

/// Reads a JSON-family config. Returns false when the file does not parse.
bool ReadConfigObject(const std::wstring& path, const std::string& bytes, std::string_view key,
                      Format::JsonDocument& document, int& rootId) {
    Format::Language dialect = DialectForFile(path);
    if (dialect == Format::Language::Json) {
        // `.formatrc` / `.prettierrc` without an extension commonly use JSON5.
        Format::JsonParseResult parsed = Format::ParseJson(bytes, Format::Language::Json5);
        if (!parsed.ok) {
            parsed = Format::ParseJson(bytes, Format::Language::Json);
        }
        if (!parsed.ok) {
            return false;
        }
        document = std::move(parsed.document);
    } else {
        Format::JsonParseResult parsed = Format::ParseJson(bytes, dialect);
        if (!parsed.ok) {
            return false;
        }
        document = std::move(parsed.document);
    }
    if (document.root == Format::kNoJsonNode) {
        return false;
    }
    rootId = document.root;
    if (!key.empty()) {
        const Format::JsonNode& root = document.Node(document.root);
        const Format::JsonNode* nested = Format::JsonMemberValue(document, root, key);
        if (nested == nullptr || nested->kind != Format::JsonKind::Object) {
            return false;
        }
        for (std::size_t i = 0; i < document.nodes.size(); ++i) {
            if (&document.nodes[i] == nested) {
                rootId = static_cast<int>(i);
                break;
            }
        }
    }
    return document.Node(rootId).kind == Format::JsonKind::Object;
}

std::wstring AppendNote(const std::wstring& notes, const std::wstring& note) {
    if (notes.empty()) {
        return note;
    }
    return notes + L"\n" + note;
}

}  // namespace

std::wstring ParentDirectory(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, slash);
}

std::wstring FileNameOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring ExtensionOf(const std::wstring& path) {
    const std::wstring name = FileNameOf(path);
    const std::size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) {
        return {};
    }
    return name.substr(dot + 1);
}

void ConfigResolver::ClearCache() {
    cache_.clear();
    ignoreCache_.clear();
}

ConfigResolver::CacheEntry& ConfigResolver::GetOrBuild(const std::wstring& directory,
                                                       const std::wstring& fileName) {
    for (auto& entry : cache_) {
        if (entry.first == directory) {
            return entry.second;
        }
    }

    CacheEntry entry;
    entry.options = fallback_.format;

    // Collect .editorconfig files from the document folder up to the root, then
    // apply them root-first so the nearest one wins.
    std::vector<std::wstring> directoryChain;
    std::wstring current = directory;
    for (std::size_t depth = 0; depth < kMaxConfigDepth && !current.empty(); ++depth) {
        directoryChain.push_back(current);
        const std::wstring parent = ParentDirectory(current);
        if (parent == current) {
            break;
        }
        current = parent;
    }

    for (auto it = directoryChain.rbegin(); it != directoryChain.rend(); ++it) {
        const std::wstring editorconfig = JoinPath(*it, L".editorconfig");
        if (!Exists(editorconfig)) {
            continue;
        }
        std::wstring relativePath = directory;
        if (!it->empty() && relativePath.size() > it->size() &&
            relativePath.compare(0, it->size(), *it) == 0) {
            relativePath.erase(0, it->size());
            if (!relativePath.empty() &&
                (relativePath[0] == L'\\' || relativePath[0] == L'/')) {
                relativePath.erase(0, 1);
            }
        }
        relativePath.append(fileName);
        const std::string bytes = ReadBytes(editorconfig);
        if (bytes.empty()) {
            continue;
        }
        const std::wstring text = ToWide(bytes);
        std::wistringstream stream(text);
        std::wstring line;
        bool sectionActive = false;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            line = Trim(line);
            if (line.empty() || line[0] == L'#' || line[0] == L';') {
                continue;
            }
            if (line.front() == L'[' && line.back() == L']') {
                const std::wstring section = line.substr(1, line.size() - 2);
                sectionActive = SectionMatches(section, fileName, relativePath);
                continue;
            }
            if (!sectionActive) {
                continue;
            }
            const std::size_t equals = line.find(L'=');
            if (equals == std::wstring::npos) {
                continue;
            }
            const std::wstring key = ToLower(Trim(line.substr(0, equals)));
            const std::wstring value = ToLower(Trim(line.substr(equals + 1)));
            if (key == L"indent_style") {
                entry.options.useTabs = value == L"tab";
            } else if (key == L"indent_size") {
                entry.options.tabWidth = ParseInteger(value, entry.options.tabWidth);
            } else if (key == L"tab_width") {
                entry.options.tabWidth = ParseInteger(value, entry.options.tabWidth);
            } else if (key == L"max_line_length") {
                entry.options.printWidth = ParseInteger(value, entry.options.printWidth);
            } else if (key == L"end_of_line") {
                if (value == L"lf") {
                    entry.options.endOfLine = Format::Eol::Lf;
                } else if (value == L"crlf") {
                    entry.options.endOfLine = Format::Eol::Crlf;
                } else if (value == L"cr") {
                    entry.options.endOfLine = Format::Eol::Cr;
                }
            }
        }
    }

    // Nearest Prettier-style config wins; JavaScript configs are reported and
    // skipped without executing anything.
    static const wchar_t* kConfigNames[] = {
        L".formatrc",      L".formatrc.json",  L".formatrc.json5",  L".formatrc.jsonc",
        L".prettierrc",    L".prettierrc.json", L".prettierrc.json5", L".prettierrc.jsonc",
    };
    static const wchar_t* kSkippedNames[] = {
        L".formatrc.js",     L".formatrc.cjs",     L".formatrc.mjs",     L".formatrc.ts",
        L".prettierrc.js",   L".prettierrc.cjs",   L".prettierrc.mjs",   L".prettierrc.ts",
        L"format.config.js", L"prettier.config.js", L".formatrc.yaml",    L".formatrc.yml",
        L".prettierrc.yaml", L".prettierrc.yml",
    };

    for (const std::wstring& dir : directoryChain) {
        for (const wchar_t* skipped : kSkippedNames) {
            const std::wstring path = JoinPath(dir, skipped);
            if (Exists(path)) {
                entry.notes = AppendNote(entry.notes, L"skipped unsupported config: " + path);
            }
        }

        std::wstring found;
        for (const wchar_t* name : kConfigNames) {
            const std::wstring path = JoinPath(dir, name);
            if (Exists(path)) {
                found = path;
                break;
            }
        }

        const std::wstring packageJson = JoinPath(dir, L"package.json");
        if (found.empty() && Exists(packageJson)) {
            const std::string bytes = ReadBytes(packageJson);
            Format::JsonDocument document;
            int rootId = -1;
            if (ReadConfigObject(packageJson, bytes, "format", document, rootId)) {
                ApplyConfigObject(entry.options, document, document.Node(rootId));
                entry.configPath = packageJson;
                entry.hasConfig = true;
                entry.stamp = StampOf(packageJson);
                break;
            }
        }

        if (found.empty()) {
            continue;
        }

        const std::string bytes = ReadBytes(found);
        Format::JsonDocument document;
        int rootId = -1;
        if (!ReadConfigObject(found, bytes, {}, document, rootId)) {
            entry.notes = AppendNote(entry.notes, L"could not parse config: " + found);
            continue;
        }
        ApplyConfigObject(entry.options, document, document.Node(rootId));
        entry.configPath = found;
        entry.hasConfig = true;
        entry.stamp = StampOf(found);
        break;
    }

    cache_.emplace_back(directory, entry);
    return cache_.back().second;
}

ResolvedConfig ConfigResolver::Resolve(const std::wstring& filePath) {
    ResolvedConfig resolved;
    resolved.options = fallback_.format;
    resolved.options.filepath = ToUtf8(filePath);

    const std::wstring directory = ParentDirectory(filePath);
    const std::wstring fileName = FileNameOf(filePath);
    CacheEntry& entry = GetOrBuild(directory, fileName);

    const std::int64_t currentStamp = entry.configPath.empty() ? 0 : StampOf(entry.configPath);
    if (!entry.configPath.empty() && currentStamp != entry.stamp) {
        ClearCache();
        return Resolve(filePath);
    }

    resolved.options = entry.options;
    resolved.options.filepath = ToUtf8(filePath);
    resolved.hasConfig = entry.hasConfig;
    resolved.configPath = entry.configPath;
    resolved.notes = entry.notes;
    return resolved;
}

std::wstring ConfigResolver::FindIgnoreFile(const std::wstring& directory) {
    std::wstring current = directory;
    for (std::size_t depth = 0; depth < kMaxConfigDepth && !current.empty(); ++depth) {
        const std::wstring formatIgnore = JoinPath(current, L".formatignore");
        if (Exists(formatIgnore)) {
            return formatIgnore;
        }
        const std::wstring prettierIgnore = JoinPath(current, L".prettierignore");
        if (Exists(prettierIgnore)) {
            return prettierIgnore;
        }
        const std::wstring parent = ParentDirectory(current);
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return {};
}

bool ConfigResolver::IsIgnored(const std::wstring& filePath) {
    const std::wstring directory = ParentDirectory(filePath);

    auto check = [&](const std::wstring& ignorePath) -> bool {
        if (ignorePath.empty()) {
            return false;
        }
        IgnoreList* list = nullptr;
        for (auto& entry : ignoreCache_) {
            if (entry.first == ignorePath) {
                list = &entry.second;
                break;
            }
        }
        if (list == nullptr) {
            IgnoreList loaded;
            std::wstring error;
            if (!loaded.Load(ignorePath, error)) {
                return false;
            }
            ignoreCache_.emplace_back(ignorePath, std::move(loaded));
            list = &ignoreCache_.back().second;
        }
        const std::wstring ignoreDirectory = ParentDirectory(ignorePath);
        std::wstring relative = filePath;
        if (!ignoreDirectory.empty() && relative.size() > ignoreDirectory.size() &&
            relative.compare(0, ignoreDirectory.size(), ignoreDirectory) == 0) {
            relative.erase(0, ignoreDirectory.size());
            if (!relative.empty() && (relative[0] == L'\\' || relative[0] == L'/')) {
                relative.erase(0, 1);
            }
        }
        return list->IsIgnored(relative, false);
    };

    const std::wstring discovered = FindIgnoreFile(directory);
    if (check(discovered)) {
        return true;
    }

    if (!fallback_.ignoreFile.empty()) {
        std::wstring configured = fallback_.ignoreFile;
        const bool absolute = configured.size() > 1 && configured[1] == L':' &&
                              (configured[0] == L'\\' || configured[0] == L'/');
        if (!absolute) {
            configured = JoinPath(directory, configured.c_str());
        }
        if (configured != discovered && check(configured)) {
            return true;
        }
    }
    return false;
}

}  // namespace ArgpiSuite::Plugin

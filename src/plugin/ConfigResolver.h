// ArgpiSuite plugin — resolves Prettier style configuration files per document.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "GlobMatcher.h"
#include "Settings.h"
#include "format/FormatOptions.h"

namespace ArgpiSuite::Plugin {

struct ResolvedConfig {
    Format::FormatOptions options;  // INI defaults with config overlays applied
    bool hasConfig = false;         // true when a .formatrc/.prettierrc/package.json was used
    std::wstring configPath;        // winning config file, empty when none
    std::wstring notes;             // skipped files / parse problems, newline separated
};

/// Walks up from the document folder, applies `.editorconfig` (root to leaf)
/// and the nearest `.formatrc*` / `.prettierrc*` / `package.json#format`.
/// JavaScript based configs are never executed, only reported.
class ConfigResolver {
public:
    explicit ConfigResolver(const Settings& fallback) : fallback_(fallback) {}

    ResolvedConfig Resolve(const std::wstring& filePath);

    /// True when the path is covered by `.formatignore` / `.prettierignore` or
    /// the additional ignore file configured in the INI.
    bool IsIgnored(const std::wstring& filePath);

    /// Finds the ignore file that applies to `directory`, if any.
    std::wstring FindIgnoreFile(const std::wstring& directory);

    void ClearCache();

private:
    struct CacheEntry {
        std::wstring configPath;
        bool hasConfig = false;
        Format::FormatOptions options;
        std::wstring notes;
        std::int64_t stamp = 0;
    };

    const Settings& fallback_;
    std::vector<std::pair<std::wstring, CacheEntry>> cache_;
    std::vector<std::pair<std::wstring, IgnoreList>> ignoreCache_;

    CacheEntry& GetOrBuild(const std::wstring& directory, const std::wstring& fileName);
};

/// Directory part of a path (without trailing separator).
std::wstring ParentDirectory(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);
std::wstring ExtensionOf(const std::wstring& path);

}  // namespace ArgpiSuite::Plugin

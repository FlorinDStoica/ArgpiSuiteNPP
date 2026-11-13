// ArgpiSuite plugin — small glob / gitignore engine.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ArgpiSuite::Plugin {

/// Matches a single path against a glob. `*` and `?` never cross `/`, `**`
/// crosses directory boundaries. Matching is case-insensitive (Windows).
bool MatchPathGlob(std::wstring_view pattern, std::wstring_view path);

/// Parsed `.formatignore` / `.prettierignore`. Implements the gitignore subset
/// we need: comments, `!` negation, trailing `/` for directories, leading `/`
/// anchoring and `**` globs. The last matching rule wins.
class IgnoreList {
public:
    bool Load(const std::wstring& file, std::wstring& error);

    /// `relativePath` is relative to the directory that owns the ignore file.
    bool IsIgnored(std::wstring_view relativePath, bool isDirectory) const;

    [[nodiscard]] bool Empty() const { return rules_.empty(); }
    [[nodiscard]] const std::wstring& Source() const { return source_; }
    [[nodiscard]] std::size_t RuleCount() const { return rules_.size(); }

private:
    struct Rule {
        std::wstring pattern;
        bool negate = false;
        bool directoryOnly = false;
        bool anchored = false;
        bool hasSlash = false;
    };

    std::vector<Rule> rules_;
    std::wstring source_;
};

}  // namespace ArgpiSuite::Plugin

#include "GlobMatcher.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

#include "TextUtil.h"

namespace ArgpiSuite::Plugin {
namespace {

std::wstring Normalize(std::wstring_view text) {
    std::wstring out(text);
    std::replace(out.begin(), out.end(), L'\\', L'/');
    return ToLower(out);
}

bool MatchClass(std::wstring_view pattern, std::size_t& index, wchar_t ch) {
    // `index` points at '['.
    std::size_t i = index + 1;
    bool negate = false;
    if (i < pattern.size() && (pattern[i] == L'!' || pattern[i] == L'^')) {
        negate = true;
        ++i;
    }
    bool matched = false;
    bool closed = false;
    while (i < pattern.size()) {
        if (pattern[i] == L']' && i > index + 1) {
            closed = true;
            ++i;
            break;
        }
        wchar_t first = pattern[i];
        if (first == L'\\' && i + 1 < pattern.size()) {
            first = pattern[++i];
        }
        wchar_t last = first;
        if (i + 2 < pattern.size() && pattern[i + 1] == L'-' && pattern[i + 2] != L']') {
            last = pattern[i + 2];
            i += 3;
        } else {
            ++i;
        }
        if (ch >= first && ch <= last) {
            matched = true;
        }
    }
    if (!closed) {
        return false;
    }
    index = i;
    return matched != negate;
}

bool MatchGlobRecursive(std::wstring_view pattern, std::size_t patternIndex, std::wstring_view text,
                        std::size_t textIndex) {
    while (patternIndex < pattern.size()) {
        const wchar_t token = pattern[patternIndex];
        if (token == L'*') {
            const bool doubleStar =
                patternIndex + 1 < pattern.size() && pattern[patternIndex + 1] == L'*';
            if (doubleStar) {
                std::size_t after = patternIndex + 2;
                if (after < pattern.size() && pattern[after] == L'/') {
                    if (MatchGlobRecursive(pattern, after + 1, text, textIndex)) {
                        return true;
                    }
                    for (std::size_t i = textIndex; i < text.size(); ++i) {
                        if (text[i] == L'/' &&
                            MatchGlobRecursive(pattern, after + 1, text, i + 1)) {
                            return true;
                        }
                    }
                    return false;
                }
                for (std::size_t i = textIndex; i <= text.size(); ++i) {
                    if (MatchGlobRecursive(pattern, after, text, i)) {
                        return true;
                    }
                }
                return false;
            }
            for (std::size_t i = textIndex;; ++i) {
                if (MatchGlobRecursive(pattern, patternIndex + 1, text, i)) {
                    return true;
                }
                if (i >= text.size() || text[i] == L'/') {
                    return false;
                }
            }
        }
        if (token == L'?') {
            if (textIndex >= text.size() || text[textIndex] == L'/') {
                return false;
            }
            ++patternIndex;
            ++textIndex;
            continue;
        }
        if (token == L'[') {
            if (textIndex >= text.size()) {
                return false;
            }
            std::size_t next = patternIndex;
            if (!MatchClass(pattern, next, text[textIndex])) {
                return false;
            }
            patternIndex = next;
            ++textIndex;
            continue;
        }
        wchar_t literal = token;
        if (literal == L'\\' && patternIndex + 1 < pattern.size()) {
            literal = pattern[patternIndex + 1];
            ++patternIndex;
        }
        if (textIndex >= text.size() || text[textIndex] != literal) {
            return false;
        }
        ++patternIndex;
        ++textIndex;
    }
    return textIndex == text.size();
}

bool MatchComponent(const std::wstring& pattern, std::wstring_view component) {
    return MatchGlobRecursive(pattern, 0, component, 0);
}

bool MatchAnyComponent(const std::wstring& pattern, const std::wstring& path) {
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find(L'/', start);
        const std::size_t end = slash == std::wstring::npos ? path.size() : slash;
        if (MatchComponent(pattern, std::wstring_view(path).substr(start, end - start))) {
            return true;
        }
        if (slash == std::wstring::npos) {
            break;
        }
        start = slash + 1;
    }
    return false;
}

/// Tests the pattern against the full path and every directory prefix, so a
/// `build/` rule also covers `build/sub/file.txt`.
bool MatchWithDirectoryPrefixes(const std::wstring& pattern, const std::wstring& path) {
    if (MatchGlobRecursive(pattern, 0, path, 0)) {
        return true;
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == L'/' &&
            MatchGlobRecursive(pattern, 0, std::wstring_view(path).substr(0, i), 0)) {
            return true;
        }
    }
    return false;
}

std::wstring ReadTextFile(const std::wstring& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string bytes = buffer.str();
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    return ToWide(bytes);
}

}  // namespace

bool MatchPathGlob(std::wstring_view pattern, std::wstring_view path) {
    const std::wstring normalizedPattern = Normalize(pattern);
    const std::wstring normalizedPath = Normalize(path);
    return MatchGlobRecursive(normalizedPattern, 0, normalizedPath, 0);
}

bool IgnoreList::Load(const std::wstring& file, std::wstring& error) {
    rules_.clear();
    source_ = file;

    const std::wstring text = ReadTextFile(file);
    if (text.empty()) {
        error = L"ignore file is empty or unreadable: " + file;
        return false;
    }

    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        line = Trim(line);
        if (line.empty() || line[0] == L'#') {
            continue;
        }
        Rule rule;
        if (line[0] == L'!') {
            rule.negate = true;
            line.erase(0, 1);
        }
        if (!line.empty() && line.back() == L'/') {
            rule.directoryOnly = true;
            line.pop_back();
        }
        if (!line.empty() && line[0] == L'/') {
            rule.anchored = true;
            line.erase(0, 1);
        }
        if (line.empty()) {
            continue;
        }
        rule.hasSlash = line.find(L'/') != std::wstring::npos;
        rule.pattern = Normalize(line);
        rules_.push_back(std::move(rule));
    }
    error.clear();
    return true;
}

bool IgnoreList::IsIgnored(std::wstring_view relativePath, bool /*isDirectory*/) const {
    const std::wstring path = Normalize(relativePath);
    bool ignored = false;
    for (const Rule& rule : rules_) {
        bool matches = false;
        if (rule.anchored || rule.hasSlash) {
            matches = rule.directoryOnly ? MatchWithDirectoryPrefixes(rule.pattern, path)
                                         : MatchGlobRecursive(rule.pattern, 0, path, 0);
        } else if (rule.directoryOnly) {
            matches = MatchWithDirectoryPrefixes(rule.pattern, path);
        } else {
            matches = MatchAnyComponent(rule.pattern, path);
        }
        if (matches) {
            ignored = !rule.negate;
        }
    }
    return ignored;
}

}  // namespace ArgpiSuite::Plugin

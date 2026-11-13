#include "TextUtil.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

namespace ArgpiSuite::Plugin {

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed,
                          nullptr, nullptr);
    return out;
}

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int needed =
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                          needed);
    return out;
}

std::wstring ToLower(std::wstring_view text) {
    std::wstring out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return out;
}

bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    if (left.empty()) {
        return true;
    }
    return ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                  static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::wstring Trim(std::wstring_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    auto isSpace = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L'\f' ||
               ch == L'\v';
    };
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return std::wstring(text.substr(begin, end - begin));
}

std::vector<std::wstring> SplitList(std::wstring_view text) {
    std::vector<std::wstring> items;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(L',', start);
        const std::size_t end = comma == std::wstring_view::npos ? text.size() : comma;
        std::wstring item = Trim(text.substr(start, end - start));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
        if (comma == std::wstring_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return items;
}

}  // namespace ArgpiSuite::Plugin

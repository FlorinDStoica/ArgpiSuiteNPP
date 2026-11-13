// ArgpiSuite Smart Deduplication & Sorting Service.
#pragma once

#include <string>
#include <string_view>

namespace ArgpiSuite::Tools {

enum class DedupeSortOperation : int {
    DeduplicatePreserveOrder = 0,
    SortAlphabetical = 1,
    SortByLength = 2,
    SortNumericNatural = 3,
    SortJsonKeys = 4,
    RemoveEmptyLines = 5,
    ReverseLines = 6,
};

struct DedupeSortOptions {
    DedupeSortOperation operation = DedupeSortOperation::DeduplicatePreserveOrder;
    bool descending = false;
    bool caseSensitive = true;
    bool trimWhitespace = false;
    bool ignoreEmptyLines = false;
    std::string eol = "\r\n";
};

struct DedupeSortResult {
    bool ok = false;
    int inputLines = 0;
    int outputLines = 0;
    int removedCount = 0;
    std::string output;
    std::wstring message;
};

class DedupeSortService {
public:
    static DedupeSortService& Instance();

    DedupeSortResult Process(std::string_view text, const DedupeSortOptions& options);
};

}  // namespace ArgpiSuite::Tools


// ArgpiSuite CSV / TSV Column Alignment & Filtering Service.
#pragma once

#include <string>
#include <string_view>

namespace ArgpiSuite::Tools {

enum class CsvOperation : int {
    AlignColumns = 0,
    CompactUnalign = 1,
    ExtractColumns = 2,
    DeleteColumns = 3,
    FilterRows = 4,
};

struct CsvOptions {
    char delimiter = '\0';  // '\0' = auto-detect (',', '\t', ';', '|')
    bool hasHeader = true;
    CsvOperation operation = CsvOperation::AlignColumns;
    std::string columnSpec;   // e.g. "1,3" or "id,email" (or filter column for FilterRows)
    std::string filterValue;  // Used by FilterRows
    bool filterInvert = false;
    bool filterCaseSensitive = false;
    std::string eol = "\r\n";
};

struct CsvResult {
    bool ok = false;
    char detectedDelimiter = ',';
    int rowCount = 0;
    int columnCount = 0;
    std::string output;
    std::wstring message;
};

class CsvToolService {
public:
    static CsvToolService& Instance();

    static char DetectDelimiter(std::string_view text);

    CsvResult Process(std::string_view text, const CsvOptions& options);
};

}  // namespace ArgpiSuite::Tools


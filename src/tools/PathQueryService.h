// ArgpiSuite JSON / XML / SQL Path Querying Service.
#pragma once

#include <string>
#include <string_view>

namespace ArgpiSuite::Tools {

enum class QueryMode : int {
    Auto = 0,
    JsonPath = 1,
    XPath = 2,
    SqlInsert = 3,
};

struct PathQueryResult {
    bool ok = false;
    QueryMode detectedMode = QueryMode::JsonPath;
    int matchCount = 0;
    std::string output;
    std::wstring message;
};

class PathQueryService {
public:
    static PathQueryService& Instance();

    /// Detects the most likely query mode from `documentText`.
    static QueryMode DetectMode(std::string_view documentText);

    /// Evaluates `query` against `documentText` in the specified (or auto-detected) mode.
    PathQueryResult Execute(std::string_view documentText, std::string_view query,
                            QueryMode mode = QueryMode::Auto);
};

}  // namespace ArgpiSuite::Tools


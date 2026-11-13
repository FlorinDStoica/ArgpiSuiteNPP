#include "CsvToolService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace ArgpiSuite::Tools {
namespace {

std::string TrimAscii(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && (sv[s] == ' ' || sv[s] == '\t')) {
        ++s;
    }
    size_t e = sv.size();
    while (e > s && (sv[e - 1] == ' ' || sv[e - 1] == '\t')) {
        --e;
    }
    return std::string(sv.substr(s, e - s));
}

std::string ToLowerAscii(std::string_view sv) {
    std::string out(sv);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::vector<std::vector<std::string>> ParseCsv(std::string_view text, char delim) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> currentRow;
    std::string cell;
    bool inQuote = false;
    bool fieldStarted = false;

    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (inQuote) {
            if (ch == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    cell.push_back('"');
                    ++i;
                } else {
                    inQuote = false;
                }
            } else {
                cell.push_back(ch);
            }
        } else {
            if (ch == '"' && !fieldStarted) {
                inQuote = true;
                fieldStarted = true;
            } else if (ch == delim) {
                currentRow.push_back(std::move(cell));
                cell.clear();
                fieldStarted = false;
            } else if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
                currentRow.push_back(std::move(cell));
                cell.clear();
                fieldStarted = false;
                if (!(currentRow.size() == 1 && currentRow[0].empty())) {
                    rows.push_back(std::move(currentRow));
                }
                currentRow.clear();
            } else {
                cell.push_back(ch);
                if (ch != ' ' && ch != '\t') {
                    fieldStarted = true;
                }
            }
        }
    }

    if (fieldStarted || !cell.empty() || !currentRow.empty()) {
        currentRow.push_back(std::move(cell));
        if (!(currentRow.size() == 1 && currentRow[0].empty())) {
            rows.push_back(std::move(currentRow));
        }
    }
    return rows;
}

std::string FormatCsvCell(std::string_view cell, char delim) {
    bool needsQuote = false;
    for (char ch : cell) {
        if (ch == delim || ch == '"' || ch == '\n' || ch == '\r') {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote) {
        return std::string(cell);
    }
    std::string out = "\"";
    for (char ch : cell) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

std::vector<size_t> ResolveColumnIndices(const std::string& spec,
                                         const std::vector<std::vector<std::string>>& rows,
                                         bool hasHeader, size_t maxCols) {
    std::vector<size_t> indices;
    std::set<size_t> seen;

    size_t pos = 0;
    while (pos <= spec.size()) {
        const size_t comma = spec.find(',', pos);
        const std::string token = TrimAscii(
            comma == std::string::npos ? std::string_view(spec).substr(pos)
                                       : std::string_view(spec).substr(pos, comma - pos));
        if (!token.empty()) {
            bool resolved = false;
            if (std::isdigit(static_cast<unsigned char>(token[0]))) {
                const int idx1 = std::atoi(token.c_str());
                if (idx1 >= 1 && static_cast<size_t>(idx1) <= maxCols) {
                    const size_t idx0 = static_cast<size_t>(idx1 - 1);
                    if (seen.insert(idx0).second) {
                        indices.push_back(idx0);
                    }
                    resolved = true;
                }
            }
            if (!resolved && hasHeader && !rows.empty()) {
                const std::string lowerTok = ToLowerAscii(token);
                for (size_t c = 0; c < rows[0].size(); ++c) {
                    if (ToLowerAscii(TrimAscii(rows[0][c])) == lowerTok) {
                        if (seen.insert(c).second) {
                            indices.push_back(c);
                        }
                        break;
                    }
                }
            }
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return indices;
}

}  // namespace

CsvToolService& CsvToolService::Instance() {
    static CsvToolService instance;
    return instance;
}

char CsvToolService::DetectDelimiter(std::string_view text) {
    const char candidates[] = {',', '\t', ';', '|'};
    int counts[4] = {0, 0, 0, 0};
    bool inQuote = false;
    int lines = 0;

    for (size_t i = 0; i < text.size() && lines < 10; ++i) {
        const char ch = text[i];
        if (ch == '"') {
            inQuote = !inQuote;
        } else if (!inQuote) {
            if (ch == '\n') {
                ++lines;
            } else {
                for (int c = 0; c < 4; ++c) {
                    if (ch == candidates[c]) {
                        ++counts[c];
                    }
                }
            }
        }
    }

    int bestIdx = 0;
    for (int c = 1; c < 4; ++c) {
        if (counts[c] > counts[bestIdx]) {
            bestIdx = c;
        }
    }
    return counts[bestIdx] > 0 ? candidates[bestIdx] : ',';
}

CsvResult CsvToolService::Process(std::string_view text, const CsvOptions& options) {
    CsvResult res;
    const char delim = (options.delimiter == '\0') ? DetectDelimiter(text) : options.delimiter;
    res.detectedDelimiter = delim;
    const std::string eol = options.eol.empty() ? "\r\n" : options.eol;

    std::vector<std::vector<std::string>> rows = ParseCsv(text, delim);
    if (rows.empty()) {
        res.message = L"No CSV / TSV rows found.";
        return res;
    }

    size_t maxCols = 0;
    for (const auto& r : rows) {
        maxCols = std::max(maxCols, r.size());
    }

    if (options.operation == CsvOperation::AlignColumns) {
        std::vector<std::vector<std::string>> formatted(rows.size());
        std::vector<size_t> colWidths(maxCols, 0);
        for (size_t r = 0; r < rows.size(); ++r) {
            formatted[r].resize(rows[r].size());
            for (size_t c = 0; c < rows[r].size(); ++c) {
                formatted[r][c] = FormatCsvCell(TrimAscii(rows[r][c]), delim);
                colWidths[c] = std::max(colWidths[c], formatted[r][c].size());
            }
        }

        std::string out;
        for (size_t r = 0; r < formatted.size(); ++r) {
            for (size_t c = 0; c < formatted[r].size(); ++c) {
                out += formatted[r][c];
                if (c + 1 < formatted[r].size()) {
                    const size_t pad = (colWidths[c] > formatted[r][c].size())
                                           ? (colWidths[c] - formatted[r][c].size())
                                           : 0;
                    out.append(pad, ' ');
                    out.push_back(delim);
                    out.push_back(' ');
                }
            }
            out += eol;
        }
        res.ok = true;
        res.rowCount = static_cast<int>(formatted.size());
        res.columnCount = static_cast<int>(maxCols);
        res.output = std::move(out);
        return res;
    }

    if (options.operation == CsvOperation::CompactUnalign) {
        std::string out;
        for (const auto& r : rows) {
            for (size_t c = 0; c < r.size(); ++c) {
                if (c > 0) out.push_back(delim);
                out += FormatCsvCell(TrimAscii(r[c]), delim);
            }
            out += eol;
        }
        res.ok = true;
        res.rowCount = static_cast<int>(rows.size());
        res.columnCount = static_cast<int>(maxCols);
        res.output = std::move(out);
        return res;
    }

    if (options.operation == CsvOperation::ExtractColumns ||
        options.operation == CsvOperation::DeleteColumns) {
        const std::vector<size_t> targetCols =
            ResolveColumnIndices(options.columnSpec, rows, options.hasHeader, maxCols);
        if (targetCols.empty()) {
            res.message = L"Specify valid column indices (1-based) or header names (e.g. 1,3).";
            return res;
        }

        std::vector<size_t> keepCols;
        if (options.operation == CsvOperation::ExtractColumns) {
            keepCols = targetCols;
        } else {
            std::set<size_t> delSet(targetCols.begin(), targetCols.end());
            for (size_t c = 0; c < maxCols; ++c) {
                if (delSet.find(c) == delSet.end()) {
                    keepCols.push_back(c);
                }
            }
        }

        std::string out;
        for (const auto& r : rows) {
            for (size_t i = 0; i < keepCols.size(); ++i) {
                if (i > 0) out.push_back(delim);
                const size_t c = keepCols[i];
                if (c < r.size()) {
                    out += FormatCsvCell(TrimAscii(r[c]), delim);
                }
            }
            out += eol;
        }
        res.ok = true;
        res.rowCount = static_cast<int>(rows.size());
        res.columnCount = static_cast<int>(keepCols.size());
        res.output = std::move(out);
        return res;
    }

    // FilterRows
    const std::vector<size_t> filterCols =
        ResolveColumnIndices(options.columnSpec, rows, options.hasHeader, maxCols);
    if (filterCols.empty()) {
        res.message = L"Specify a column index (1-based) or header name to filter on.";
        return res;
    }
    const size_t filterCol = filterCols[0];
    const std::string needle =
        options.filterCaseSensitive ? options.filterValue : ToLowerAscii(options.filterValue);

    std::string out;
    int keptRows = 0;
    for (size_t r = 0; r < rows.size(); ++r) {
        bool keep = false;
        if (options.hasHeader && r == 0) {
            keep = true;
        } else {
            std::string cell =
                (filterCol < rows[r].size()) ? TrimAscii(rows[r][filterCol]) : std::string();
            if (!options.filterCaseSensitive) {
                cell = ToLowerAscii(cell);
            }
            const bool matched = needle.empty() || (cell.find(needle) != std::string::npos);
            keep = options.filterInvert ? !matched : matched;
        }
        if (keep) {
            ++keptRows;
            for (size_t c = 0; c < rows[r].size(); ++c) {
                if (c > 0) out.push_back(delim);
                out += FormatCsvCell(TrimAscii(rows[r][c]), delim);
            }
            out += eol;
        }
    }

    res.ok = true;
    res.rowCount = keptRows;
    res.columnCount = static_cast<int>(maxCols);
    res.output = std::move(out);
    return res;
}

}  // namespace ArgpiSuite::Tools


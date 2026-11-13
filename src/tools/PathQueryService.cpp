#include "PathQueryService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "format/Format.h"
#include "format/FormatLanguage.h"
#include "format/FormatOptions.h"
#include "format/json/FormatJsonParser.h"
#include "plugin/TextUtil.h"

namespace ArgpiSuite::Tools {
namespace {

std::string TrimAscii(std::string_view sv) {
    size_t s = 0;
    while (s < sv.size() && std::isspace(static_cast<unsigned char>(sv[s]))) {
        ++s;
    }
    size_t e = sv.size();
    while (e > s && std::isspace(static_cast<unsigned char>(sv[e - 1]))) {
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

std::string Unquote(std::string_view sv) {
    const std::string t = TrimAscii(sv);
    if (t.size() >= 2 && ((t.front() == '\'' && t.back() == '\'') ||
                          (t.front() == '"' && t.back() == '"'))) {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

// =========================================================================
// JSONPath / JMESPath Evaluator (on Format::JsonDocument)
// =========================================================================

std::string EscapeJsonString(std::string_view s) {
    std::string out = "\"";
    for (unsigned char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

void SerializeJsonCompact(const Format::JsonDocument& doc, int nodeId, std::string& out) {
    const Format::JsonNode& n = doc.Node(nodeId);
    switch (n.kind) {
        case Format::JsonKind::Null:
            out += "null";
            break;
        case Format::JsonKind::Bool:
            out += (n.boolean ? "true" : "false");
            break;
        case Format::JsonKind::Number:
            out += n.raw;
            break;
        case Format::JsonKind::String:
            out += EscapeJsonString(n.value);
            break;
        case Format::JsonKind::Array:
            out.push_back('[');
            for (size_t i = 0; i < n.elements.size(); ++i) {
                if (i > 0) out.push_back(',');
                SerializeJsonCompact(doc, n.elements[i], out);
            }
            out.push_back(']');
            break;
        case Format::JsonKind::Object:
            out.push_back('{');
            for (size_t i = 0; i < n.members.size(); ++i) {
                if (i > 0) out.push_back(',');
                out += EscapeJsonString(n.members[i].key);
                out.push_back(':');
                SerializeJsonCompact(doc, n.members[i].value, out);
            }
            out.push_back('}');
            break;
    }
}

std::string PrettyPrintJsonString(std::string_view rawJson) {
    Format::FormatOptions opts;
    opts.tabWidth = 2;
    opts.endOfLine = Format::Eol::Crlf;
    const Format::FormatResult formatted =
        Format::FormatText(rawJson, Format::Language::Json, opts);
    if (formatted.ok) {
        std::string out = formatted.output;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
            out.pop_back();
        }
        return out;
    }
    return std::string(rawJson);
}

void CollectRecursiveProperty(const Format::JsonDocument& doc, int nodeId, std::string_view key,
                              std::vector<int>& out) {
    const Format::JsonNode& n = doc.Node(nodeId);
    if (n.kind == Format::JsonKind::Object) {
        for (const auto& m : n.members) {
            if (key == "*" || m.key == key) {
                out.push_back(m.value);
            }
            CollectRecursiveProperty(doc, m.value, key, out);
        }
    } else if (n.kind == Format::JsonKind::Array) {
        for (int elem : n.elements) {
            CollectRecursiveProperty(doc, elem, key, out);
        }
    }
}

bool EvalJsonPredicate(const Format::JsonDocument& doc, int nodeId, std::string_view expr) {
    std::string e = TrimAscii(expr);
    if (e.size() >= 2 && e.front() == '(' && e.back() == ')') {
        e = TrimAscii(std::string_view(e).substr(1, e.size() - 2));
    }
    if (e.compare(0, 2, "@.") == 0) {
        e = e.substr(2);
    }

    const char* ops[] = {"==", "!=", ">=", "<=", ">", "<"};
    std::string foundOp;
    size_t opPos = std::string::npos;
    for (const char* op : ops) {
        opPos = e.find(op);
        if (opPos != std::string::npos) {
            foundOp = op;
            break;
        }
    }

    const Format::JsonNode& obj = doc.Node(nodeId);
    if (obj.kind != Format::JsonKind::Object) {
        return false;
    }

    if (foundOp.empty()) {
        const auto* val = Format::JsonMemberValue(doc, obj, TrimAscii(e));
        if (val == nullptr) return false;
        if (val->kind == Format::JsonKind::Bool) return val->boolean;
        return val->kind != Format::JsonKind::Null;
    }

    const std::string lhsKey = TrimAscii(e.substr(0, opPos));
    const std::string rhsRaw = TrimAscii(e.substr(opPos + foundOp.size()));
    const auto* lhsNode = Format::JsonMemberValue(doc, obj, lhsKey);
    if (lhsNode == nullptr) {
        return false;
    }

    if (lhsNode->kind == Format::JsonKind::Number && !rhsRaw.empty() &&
        (std::isdigit(static_cast<unsigned char>(rhsRaw.front())) || rhsRaw.front() == '-')) {
        try {
            const double a = std::stod(lhsNode->raw);
            const double b = std::stod(rhsRaw);
            if (foundOp == "==") return a == b;
            if (foundOp == "!=") return a != b;
            if (foundOp == ">") return a > b;
            if (foundOp == ">=") return a >= b;
            if (foundOp == "<") return a < b;
            if (foundOp == "<=") return a <= b;
        } catch (...) {
            return false;
        }
    }

    const std::string lhsStr = Format::JsonScalarText(*lhsNode);
    const std::string rhsStr = Unquote(rhsRaw);
    if (foundOp == "==") return lhsStr == rhsStr;
    if (foundOp == "!=") return lhsStr != rhsStr;
    if (foundOp == ">") return lhsStr > rhsStr;
    if (foundOp == ">=") return lhsStr >= rhsStr;
    if (foundOp == "<") return lhsStr < rhsStr;
    if (foundOp == "<=") return lhsStr <= rhsStr;
    return false;
}

PathQueryResult ExecuteJsonPath(std::string_view text, std::string_view queryRaw) {
    PathQueryResult res;
    res.detectedMode = QueryMode::JsonPath;

    const Format::JsonParseResult parsed = Format::ParseJson(text, Format::Language::Json5);
    if (!parsed.ok || parsed.document.root == Format::kNoJsonNode) {
        res.message = L"Invalid JSON: " + Plugin::ToWide(parsed.error.message);
        return res;
    }

    std::string q = TrimAscii(queryRaw);
    if (q.empty() || q == "$") {
        std::string compact;
        SerializeJsonCompact(parsed.document, parsed.document.root, compact);
        res.ok = true;
        res.matchCount = 1;
        res.output = PrettyPrintJsonString(compact);
        return res;
    }

    if (q.front() == '$') {
        q.erase(0, 1);
    }

    std::vector<int> current = {parsed.document.root};
    size_t pos = 0;

    while (pos < q.size() && !current.empty()) {
        std::vector<int> next;
        if (q.compare(pos, 2, "..") == 0) {
            pos += 2;
            size_t end = pos;
            while (end < q.size() && q[end] != '.' && q[end] != '[') {
                ++end;
            }
            const std::string key = q.substr(pos, end - pos);
            pos = end;
            for (int id : current) {
                CollectRecursiveProperty(parsed.document, id, key.empty() ? "*" : key, next);
            }
        } else if (q[pos] == '.') {
            ++pos;
            size_t end = pos;
            while (end < q.size() && q[end] != '.' && q[end] != '[') {
                ++end;
            }
            const std::string key = q.substr(pos, end - pos);
            pos = end;
            for (int id : current) {
                const auto& node = parsed.document.Node(id);
                if (node.kind == Format::JsonKind::Object) {
                    if (key == "*") {
                        for (const auto& m : node.members) {
                            next.push_back(m.value);
                        }
                    } else if (const auto* val = Format::JsonMemberValue(parsed.document, node, key)) {
                        next.push_back(static_cast<int>(val - parsed.document.nodes.data()));
                    }
                }
            }
        } else if (q[pos] == '[') {
            const size_t close = q.find(']', pos + 1);
            if (close == std::string::npos) {
                res.message = L"Unclosed '[' in JSONPath expression.";
                return res;
            }
            const std::string inner = TrimAscii(q.substr(pos + 1, close - pos - 1));
            pos = close + 1;

            if (inner == "*") {
                for (int id : current) {
                    const auto& node = parsed.document.Node(id);
                    if (node.kind == Format::JsonKind::Array) {
                        for (int el : node.elements) next.push_back(el);
                    } else if (node.kind == Format::JsonKind::Object) {
                        for (const auto& m : node.members) next.push_back(m.value);
                    }
                }
            } else if (inner.compare(0, 1, "?") == 0) {
                const std::string pred = TrimAscii(inner.substr(1));
                for (int id : current) {
                    const auto& node = parsed.document.Node(id);
                    if (node.kind == Format::JsonKind::Array) {
                        for (int el : node.elements) {
                            if (EvalJsonPredicate(parsed.document, el, pred)) {
                                next.push_back(el);
                            }
                        }
                    } else if (EvalJsonPredicate(parsed.document, id, pred)) {
                        next.push_back(id);
                    }
                }
            } else if (!inner.empty() && (inner.front() == '\'' || inner.front() == '"')) {
                const std::string key = Unquote(inner);
                for (int id : current) {
                    const auto& node = parsed.document.Node(id);
                    if (node.kind == Format::JsonKind::Object) {
                        if (const auto* val = Format::JsonMemberValue(parsed.document, node, key)) {
                            next.push_back(static_cast<int>(val - parsed.document.nodes.data()));
                        }
                    }
                }
            } else if (inner.find(':') != std::string::npos) {
                const size_t colon = inner.find(':');
                const std::string sStart = TrimAscii(inner.substr(0, colon));
                const std::string sEnd = TrimAscii(inner.substr(colon + 1));
                for (int id : current) {
                    const auto& node = parsed.document.Node(id);
                    if (node.kind != Format::JsonKind::Array) continue;
                    const int n = static_cast<int>(node.elements.size());
                    int startIdx = sStart.empty() ? 0 : std::atoi(sStart.c_str());
                    int endIdx = sEnd.empty() ? n : std::atoi(sEnd.c_str());
                    if (startIdx < 0) startIdx = std::max(0, n + startIdx);
                    if (endIdx < 0) endIdx = std::max(0, n + endIdx);
                    startIdx = std::clamp(startIdx, 0, n);
                    endIdx = std::clamp(endIdx, 0, n);
                    for (int i = startIdx; i < endIdx; ++i) {
                        next.push_back(node.elements[static_cast<size_t>(i)]);
                    }
                }
            } else {
                const int idx = std::atoi(inner.c_str());
                for (int id : current) {
                    const auto& node = parsed.document.Node(id);
                    if (node.kind != Format::JsonKind::Array) continue;
                    const int n = static_cast<int>(node.elements.size());
                    const int resolved = (idx < 0) ? (n + idx) : idx;
                    if (resolved >= 0 && resolved < n) {
                        next.push_back(node.elements[static_cast<size_t>(resolved)]);
                    }
                }
            }
        } else {
            // Implicit leading property without '.'
            size_t end = pos;
            while (end < q.size() && q[end] != '.' && q[end] != '[') {
                ++end;
            }
            const std::string key = q.substr(pos, end - pos);
            pos = end;
            for (int id : current) {
                const auto& node = parsed.document.Node(id);
                if (node.kind == Format::JsonKind::Object) {
                    if (const auto* val = Format::JsonMemberValue(parsed.document, node, key)) {
                        next.push_back(static_cast<int>(val - parsed.document.nodes.data()));
                    }
                }
            }
        }
        current = std::move(next);
    }

    res.ok = true;
    res.matchCount = static_cast<int>(current.size());
    if (current.empty()) {
        res.output = "[]";
        return res;
    }
    if (current.size() == 1) {
        std::string compact;
        SerializeJsonCompact(parsed.document, current[0], compact);
        res.output = PrettyPrintJsonString(compact);
        return res;
    }

    std::string arr = "[";
    for (size_t i = 0; i < current.size(); ++i) {
        if (i > 0) arr.push_back(',');
        SerializeJsonCompact(parsed.document, current[i], arr);
    }
    arr.push_back(']');
    res.output = PrettyPrintJsonString(arr);
    return res;
}

// =========================================================================
// Lightweight XML Parser & XPath Evaluator
// =========================================================================

struct XmlAttr {
    std::string name;
    std::string value;
};

struct XmlNode {
    std::string tag;
    std::string text;
    std::vector<XmlAttr> attrs;
    std::vector<XmlNode> children;
};

bool ParseXmlElement(std::string_view src, size_t& pos, XmlNode& out) {
    auto skipWs = [&]() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
            ++pos;
        }
    };

    while (pos < src.size()) {
        skipWs();
        if (pos >= src.size() || src[pos] != '<') {
            return false;
        }
        if (src.compare(pos, 4, "<!--") == 0) {
            const size_t end = src.find("-->", pos + 4);
            pos = (end == std::string_view::npos) ? src.size() : (end + 3);
            continue;
        }
        if (src.compare(pos, 2, "<?") == 0) {
            const size_t end = src.find("?>", pos + 2);
            pos = (end == std::string_view::npos) ? src.size() : (end + 2);
            continue;
        }
        if (src.compare(pos, 2, "<!") == 0) {
            const size_t end = src.find('>', pos + 2);
            pos = (end == std::string_view::npos) ? src.size() : (end + 1);
            continue;
        }
        break;
    }

    if (pos >= src.size() || src[pos] != '<' || pos + 1 >= src.size() || src[pos + 1] == '/') {
        return false;
    }
    ++pos;  // skip '<'

    const size_t tagStart = pos;
    while (pos < src.size() && !std::isspace(static_cast<unsigned char>(src[pos])) &&
           src[pos] != '>' && src[pos] != '/') {
        ++pos;
    }
    out.tag = std::string(src.substr(tagStart, pos - tagStart));

    while (pos < src.size()) {
        skipWs();
        if (pos >= src.size()) break;
        if (src.compare(pos, 2, "/>") == 0) {
            pos += 2;
            return true;
        }
        if (src[pos] == '>') {
            ++pos;
            break;
        }
        const size_t attrStart = pos;
        while (pos < src.size() && src[pos] != '=' &&
               !std::isspace(static_cast<unsigned char>(src[pos])) && src[pos] != '>') {
            ++pos;
        }
        std::string attrName(src.substr(attrStart, pos - attrStart));
        skipWs();
        std::string attrVal;
        if (pos < src.size() && src[pos] == '=') {
            ++pos;
            skipWs();
            if (pos < src.size() && (src[pos] == '"' || src[pos] == '\'')) {
                const char q = src[pos++];
                const size_t vStart = pos;
                while (pos < src.size() && src[pos] != q) ++pos;
                attrVal = std::string(src.substr(vStart, pos - vStart));
                if (pos < src.size()) ++pos;
            }
        }
        if (!attrName.empty()) {
            out.attrs.push_back({std::move(attrName), std::move(attrVal)});
        }
    }

    while (pos < src.size()) {
        if (src.compare(pos, 2, "</") == 0) {
            const size_t closeEnd = src.find('>', pos + 2);
            pos = (closeEnd == std::string_view::npos) ? src.size() : (closeEnd + 1);
            return true;
        }
        if (src.compare(pos, 9, "<![CDATA[") == 0) {
            const size_t end = src.find("]]>", pos + 9);
            if (end == std::string_view::npos) {
                out.text.append(src.substr(pos + 9));
                pos = src.size();
            } else {
                out.text.append(src.substr(pos + 9, end - (pos + 9)));
                pos = end + 3;
            }
            continue;
        }
        if (src.compare(pos, 4, "<!--") == 0) {
            const size_t end = src.find("-->", pos + 4);
            pos = (end == std::string_view::npos) ? src.size() : (end + 3);
            continue;
        }
        if (src[pos] == '<') {
            XmlNode child;
            if (ParseXmlElement(src, pos, child)) {
                out.children.push_back(std::move(child));
            } else {
                ++pos;
            }
            continue;
        }
        const size_t txtStart = pos;
        while (pos < src.size() && src[pos] != '<') ++pos;
        out.text.append(src.substr(txtStart, pos - txtStart));
    }
    return true;
}

std::string SerializeXmlNode(const XmlNode& node) {
    std::string out = "<" + node.tag;
    for (const auto& a : node.attrs) {
        out += " " + a.name + "=\"" + a.value + "\"";
    }
    const std::string trimmedText = TrimAscii(node.text);
    if (node.children.empty() && trimmedText.empty()) {
        out += " />";
        return out;
    }
    out += ">";
    if (!trimmedText.empty()) {
        out += trimmedText;
    }
    for (const auto& ch : node.children) {
        out += SerializeXmlNode(ch);
    }
    out += "</" + node.tag + ">";
    return out;
}

void CollectXmlDescendants(const XmlNode& node, std::string_view tag,
                           std::vector<const XmlNode*>& out) {
    if (tag == "*" || node.tag == tag) {
        out.push_back(&node);
    }
    for (const auto& ch : node.children) {
        CollectXmlDescendants(ch, tag, out);
    }
}

PathQueryResult ExecuteXPath(std::string_view text, std::string_view queryRaw) {
    PathQueryResult res;
    res.detectedMode = QueryMode::XPath;

    XmlNode docRoot;
    docRoot.tag = "__doc__";
    size_t pos = 0;
    XmlNode rootElem;
    if (!ParseXmlElement(text, pos, rootElem)) {
        res.message = L"Could not parse XML document.";
        return res;
    }
    docRoot.children.push_back(std::move(rootElem));

    std::string q = TrimAscii(queryRaw);
    if (q.empty() || q == "/") {
        res.ok = true;
        res.matchCount = 1;
        res.output = SerializeXmlNode(docRoot.children[0]);
        return res;
    }

    std::vector<const XmlNode*> current = {&docRoot};
    std::vector<std::string> stringResults;
    bool isStringResult = false;

    size_t i = 0;
    while (i < q.size() && !current.empty()) {
        bool recursive = false;
        if (q.compare(i, 2, "//") == 0) {
            recursive = true;
            i += 2;
        } else if (q[i] == '/') {
            ++i;
        }
        if (i >= q.size()) break;

        size_t stepEnd = i;
        int bracketDepth = 0;
        while (stepEnd < q.size()) {
            if (q[stepEnd] == '[') ++bracketDepth;
            else if (q[stepEnd] == ']') --bracketDepth;
            else if (q[stepEnd] == '/' && bracketDepth == 0) break;
            ++stepEnd;
        }

        std::string step = q.substr(i, stepEnd - i);
        i = stepEnd;

        std::string pred;
        const size_t br = step.find('[');
        if (br != std::string::npos && step.back() == ']') {
            pred = TrimAscii(step.substr(br + 1, step.size() - br - 2));
            step = step.substr(0, br);
        }

        if (step == "text()") {
            isStringResult = true;
            for (const XmlNode* n : current) {
                const std::string t = TrimAscii(n->text);
                if (!t.empty()) stringResults.push_back(t);
            }
            break;
        }

        if (!step.empty() && step[0] == '@') {
            isStringResult = true;
            const std::string attrName = step.substr(1);
            for (const XmlNode* n : current) {
                for (const auto& a : n->attrs) {
                    if (attrName == "*" || a.name == attrName) {
                        stringResults.push_back(a.value);
                    }
                }
            }
            break;
        }

        std::vector<const XmlNode*> next;
        for (const XmlNode* n : current) {
            if (recursive) {
                for (const auto& ch : n->children) {
                    CollectXmlDescendants(ch, step, next);
                }
            } else {
                for (const auto& ch : n->children) {
                    if (step == "*" || ch.tag == step) {
                        next.push_back(&ch);
                    }
                }
            }
        }

        if (!pred.empty() && !next.empty()) {
            std::vector<const XmlNode*> filtered;
            if (pred == "last()") {
                filtered.push_back(next.back());
            } else if (std::isdigit(static_cast<unsigned char>(pred[0]))) {
                const int idx1 = std::atoi(pred.c_str());
                if (idx1 >= 1 && idx1 <= static_cast<int>(next.size())) {
                    filtered.push_back(next[static_cast<size_t>(idx1 - 1)]);
                }
            } else if (pred[0] == '@') {
                const size_t eq = pred.find('=');
                if (eq != std::string::npos) {
                    const std::string attr = TrimAscii(pred.substr(1, eq - 1));
                    const std::string val = Unquote(pred.substr(eq + 1));
                    for (const XmlNode* n : next) {
                        for (const auto& a : n->attrs) {
                            if (a.name == attr && a.value == val) {
                                filtered.push_back(n);
                                break;
                            }
                        }
                    }
                } else {
                    const std::string attr = pred.substr(1);
                    for (const XmlNode* n : next) {
                        for (const auto& a : n->attrs) {
                            if (a.name == attr) {
                                filtered.push_back(n);
                                break;
                            }
                        }
                    }
                }
            } else {
                filtered = std::move(next);
            }
            next = std::move(filtered);
        }

        current = std::move(next);
    }

    res.ok = true;
    if (isStringResult) {
        res.matchCount = static_cast<int>(stringResults.size());
        for (size_t k = 0; k < stringResults.size(); ++k) {
            if (k > 0) res.output += "\r\n";
            res.output += stringResults[k];
        }
        return res;
    }

    res.matchCount = static_cast<int>(current.size());
    for (size_t k = 0; k < current.size(); ++k) {
        if (k > 0) res.output += "\r\n";
        res.output += SerializeXmlNode(*current[k]);
    }
    return res;
}

// =========================================================================
// SQL INSERT Dump Querying
// =========================================================================

std::vector<std::string> SplitSqlTuple(std::string_view tuple) {
    std::vector<std::string> cols;
    std::string cur;
    bool inQuote = false;
    char quoteChar = 0;
    for (size_t i = 0; i < tuple.size(); ++i) {
        const char ch = tuple[i];
        if (inQuote) {
            cur.push_back(ch);
            if (ch == quoteChar) {
                if (i + 1 < tuple.size() && tuple[i + 1] == quoteChar) {
                    cur.push_back(tuple[++i]);
                } else {
                    inQuote = false;
                }
            }
        } else {
            if (ch == '\'' || ch == '"') {
                inQuote = true;
                quoteChar = ch;
                cur.push_back(ch);
            } else if (ch == ',') {
                cols.push_back(TrimAscii(cur));
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
    }
    cols.push_back(TrimAscii(cur));
    return cols;
}

PathQueryResult ExecuteSqlInsertQuery(std::string_view text, std::string_view queryRaw) {
    PathQueryResult res;
    res.detectedMode = QueryMode::SqlInsert;

    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;

    const std::string lowerText = ToLowerAscii(text);
    size_t searchPos = 0;
    while (true) {
        const size_t insPos = lowerText.find("insert into", searchPos);
        if (insPos == std::string::npos) break;
        const size_t parenOpen = text.find('(', insPos);
        const size_t valKw = lowerText.find("values", insPos);
        if (valKw == std::string::npos) break;

        if (parenOpen != std::string::npos && parenOpen < valKw) {
            const size_t parenClose = text.find(')', parenOpen + 1);
            if (parenClose != std::string::npos && parenClose < valKw && headers.empty()) {
                headers = SplitSqlTuple(text.substr(parenOpen + 1, parenClose - parenOpen - 1));
                for (auto& h : headers) {
                    if (h.size() >= 2 && ((h.front() == '`' && h.back() == '`') ||
                                          (h.front() == '"' && h.back() == '"'))) {
                        h = h.substr(1, h.size() - 2);
                    }
                }
            }
        }

        size_t p = valKw + 6;
        while (p < text.size() && text[p] != ';') {
            if (text[p] == '(') {
                size_t start = p + 1;
                bool inQ = false;
                char qc = 0;
                int depth = 1;
                size_t end = start;
                while (end < text.size() && depth > 0) {
                    const char ch = text[end];
                    if (inQ) {
                        if (ch == qc) {
                            if (end + 1 < text.size() && text[end + 1] == qc) ++end;
                            else inQ = false;
                        }
                    } else {
                        if (ch == '\'' || ch == '"') {
                            inQ = true;
                            qc = ch;
                        } else if (ch == '(') {
                            ++depth;
                        } else if (ch == ')') {
                            --depth;
                            if (depth == 0) break;
                        }
                    }
                    ++end;
                }
                if (depth == 0) {
                    rows.push_back(SplitSqlTuple(text.substr(start, end - start)));
                    p = end + 1;
                    continue;
                }
            }
            ++p;
        }
        searchPos = p + 1;
    }

    if (rows.empty()) {
        res.message = L"No INSERT INTO ... VALUES (...) statements found.";
        return res;
    }

    // Parse query: [SELECT cols] [WHERE col = val]
    std::string q = TrimAscii(queryRaw);
    std::string lowerQ = ToLowerAscii(q);
    std::string selectPart = "*";
    std::string wherePart;

    const size_t wherePos = lowerQ.find(" where ");
    if (wherePos != std::string::npos) {
        wherePart = TrimAscii(q.substr(wherePos + 7));
        q = TrimAscii(q.substr(0, wherePos));
        lowerQ = ToLowerAscii(q);
    }
    if (lowerQ.compare(0, 7, "select ") == 0) {
        selectPart = TrimAscii(q.substr(7));
    } else if (!q.empty()) {
        selectPart = q;
    }

    std::vector<size_t> colIndices;
    if (selectPart == "*" || selectPart.empty()) {
        const size_t maxCols = !headers.empty() ? headers.size() : rows[0].size();
        for (size_t c = 0; c < maxCols; ++c) colIndices.push_back(c);
    } else {
        for (const auto& name : SplitSqlTuple(selectPart)) {
            const std::string clean = ToLowerAscii(TrimAscii(name));
            bool found = false;
            for (size_t c = 0; c < headers.size(); ++c) {
                if (ToLowerAscii(headers[c]) == clean) {
                    colIndices.push_back(c);
                    found = true;
                    break;
                }
            }
            if (!found && !clean.empty() && std::isdigit(static_cast<unsigned char>(clean[0]))) {
                const size_t idx1 = static_cast<size_t>(std::atoi(clean.c_str()));
                if (idx1 >= 1 && idx1 <= rows[0].size()) {
                    colIndices.push_back(idx1 - 1);
                }
            }
        }
    }

    // Optional WHERE col = val
    int whereCol = -1;
    std::string whereVal;
    bool whereNot = false;
    if (!wherePart.empty()) {
        size_t opPos = wherePart.find("!=");
        if (opPos != std::string::npos) {
            whereNot = true;
        } else {
            opPos = wherePart.find('=');
        }
        if (opPos != std::string::npos) {
            const std::string wCol = ToLowerAscii(TrimAscii(wherePart.substr(0, opPos)));
            whereVal = Unquote(wherePart.substr(opPos + (whereNot ? 2 : 1)));
            for (size_t c = 0; c < headers.size(); ++c) {
                if (ToLowerAscii(headers[c]) == wCol) {
                    whereCol = static_cast<int>(c);
                    break;
                }
            }
        }
    }

    std::string out;
    if (!headers.empty()) {
        for (size_t i = 0; i < colIndices.size(); ++i) {
            if (i > 0) out += "\t";
            const size_t ci = colIndices[i];
            out += (ci < headers.size()) ? headers[ci] : ("col" + std::to_string(ci + 1));
        }
        out += "\r\n";
    }

    int matches = 0;
    for (const auto& r : rows) {
        if (whereCol >= 0) {
            if (static_cast<size_t>(whereCol) >= r.size()) continue;
            const std::string cell = Unquote(r[static_cast<size_t>(whereCol)]);
            const bool eq = (cell == whereVal);
            if (whereNot ? eq : !eq) continue;
        }
        ++matches;
        for (size_t i = 0; i < colIndices.size(); ++i) {
            if (i > 0) out += "\t";
            const size_t ci = colIndices[i];
            out += (ci < r.size()) ? Unquote(r[ci]) : "";
        }
        out += "\r\n";
    }

    res.ok = true;
    res.matchCount = matches;
    res.output = std::move(out);
    return res;
}

}  // namespace

PathQueryService& PathQueryService::Instance() {
    static PathQueryService instance;
    return instance;
}

QueryMode PathQueryService::DetectMode(std::string_view documentText) {
    const std::string trimmed = TrimAscii(documentText);
    if (!trimmed.empty()) {
        if (trimmed.front() == '{' || trimmed.front() == '[') {
            return QueryMode::JsonPath;
        }
        if (trimmed.front() == '<') {
            return QueryMode::XPath;
        }
    }
    const std::string lower = ToLowerAscii(documentText.substr(0, std::min<size_t>(4096, documentText.size())));
    if (lower.find("insert into") != std::string::npos) {
        return QueryMode::SqlInsert;
    }
    return QueryMode::JsonPath;
}

PathQueryResult PathQueryService::Execute(std::string_view documentText, std::string_view query,
                                          QueryMode mode) {
    const QueryMode effective = (mode == QueryMode::Auto) ? DetectMode(documentText) : mode;
    switch (effective) {
        case QueryMode::XPath:
            return ExecuteXPath(documentText, query);
        case QueryMode::SqlInsert:
            return ExecuteSqlInsertQuery(documentText, query);
        case QueryMode::JsonPath:
        case QueryMode::Auto:
            return ExecuteJsonPath(documentText, query);
    }
    return ExecuteJsonPath(documentText, query);
}

}  // namespace ArgpiSuite::Tools

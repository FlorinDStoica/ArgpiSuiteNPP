// ArgpiSuite native formatter — JSON / JSON5 / JSONC parser.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "FormatError.h"
#include "FormatLanguage.h"

namespace ArgpiSuite::Format {

inline constexpr int kNoJsonNode = -1;

enum class JsonKind { Null, Bool, Number, String, Array, Object };

struct JsonMember {
    std::string key;     // decoded key
    std::string keyRaw;  // raw lexeme (quoted string or unquoted identifier)
    std::size_t keyStart = 0;
    std::size_t keyEnd = 0;
    int value = kNoJsonNode;
};

struct JsonNode {
    JsonKind kind = JsonKind::Null;
    std::size_t start = 0;
    std::size_t end = 0;
    bool boolean = false;
    std::string raw;    // raw lexeme (numbers / strings)
    std::string value;  // decoded string value
    std::vector<int> elements;
    std::vector<JsonMember> members;

    int parent = kNoJsonNode;
    bool multiline = false;       // newline between the opener and the first child
    bool hasTrailingComma = false;
    bool blankLineBefore = false;  // blank line between previous sibling and this node
    bool ignored = false;          // // prettier-ignore immediately before this node

    std::vector<int> leadingComments;
    std::vector<int> trailingComments;
    std::vector<int> danglingComments;
};

struct JsonComment {
    enum class Kind { Line, Block } kind = Kind::Line;
    std::string text;  // includes the comment markers, trailing whitespace trimmed
    std::size_t start = 0;
    std::size_t end = 0;
    bool ownLine = false;
    bool printed = false;
};

class JsonDocument {
public:
    std::vector<JsonNode> nodes;
    std::vector<JsonComment> comments;
    int root = kNoJsonNode;
    Language dialect = Language::Json;

    [[nodiscard]] const JsonNode& Node(int id) const {
        static const JsonNode kEmpty{};
        if (id < 0 || static_cast<std::size_t>(id) >= nodes.size()) {
            return kEmpty;
        }
        return nodes[static_cast<std::size_t>(id)];
    }
};

struct JsonParseResult {
    bool ok = false;
    JsonDocument document;
    FormatError error;
};

/// Parses `text`. Comments and trailing commas are accepted (matching Prettier's
/// JSON handling); JSON5 syntax is only accepted when `dialect` is Json5.
JsonParseResult ParseJson(std::string_view text, Language dialect);

/// Attaches trivia comments to the closest nodes and evaluates prettier-ignore
/// pragmas. Must be called once after parsing.
void AttachJsonComments(JsonDocument& document, std::string_view source);

/// Config helpers (used by the plugin's .formatrc reader).
const JsonNode* JsonMemberValue(const JsonDocument& document, const JsonNode& object,
                                std::string_view key);
std::string JsonScalarText(const JsonNode& node);
bool JsonScalarBool(const JsonNode& node, bool& out);

}  // namespace ArgpiSuite::Format

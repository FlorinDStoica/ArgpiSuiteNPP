#include "FormatJsonEquivalence.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace ArgpiSuite::Format {
namespace {

bool NumbersEqual(const std::string& a, const std::string& b) {
    if (a == b) {
        return true;
    }
    char* endA = nullptr;
    char* endB = nullptr;
    const double left = std::strtod(a.c_str(), &endA);
    const double right = std::strtod(b.c_str(), &endB);
    if (endA == a.c_str() || endB == b.c_str()) {
        return false;
    }
    if (std::isnan(left) && std::isnan(right)) {
        return true;
    }
    return left == right;
}

bool EquivalentNodes(const JsonDocument& left, int leftId, const JsonDocument& right, int rightId,
                     int depth) {
    if (depth > 2048) {
        return false;
    }
    const JsonNode& a = left.Node(leftId);
    const JsonNode& b = right.Node(rightId);
    if (a.kind != b.kind) {
        return false;
    }
    switch (a.kind) {
        case JsonKind::Null:
            return true;
        case JsonKind::Bool:
            return a.boolean == b.boolean;
        case JsonKind::String:
            return a.value == b.value;
        case JsonKind::Number:
            return NumbersEqual(a.raw, b.raw);
        case JsonKind::Array:
            if (a.elements.size() != b.elements.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.elements.size(); ++i) {
                if (!EquivalentNodes(left, a.elements[i], right, b.elements[i], depth + 1)) {
                    return false;
                }
            }
            return true;
        case JsonKind::Object:
            if (a.members.size() != b.members.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.members.size(); ++i) {
                if (a.members[i].key != b.members[i].key) {
                    return false;
                }
                if (!EquivalentNodes(left, a.members[i].value, right, b.members[i].value,
                                     depth + 1)) {
                    return false;
                }
            }
            return true;
        default:
            return false;
    }
}

}  // namespace

bool JsonEquivalent(const JsonDocument& left, const JsonDocument& right) {
    if (left.root == kNoJsonNode || right.root == kNoJsonNode) {
        return left.root == right.root;
    }
    return EquivalentNodes(left, left.root, right, right.root, 0);
}

}  // namespace ArgpiSuite::Format

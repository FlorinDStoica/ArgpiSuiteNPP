// ArgpiSuite native formatter — structural JSON equivalence.
#pragma once

#include "FormatJsonParser.h"

namespace ArgpiSuite::Format {

/// Compares two parsed documents ignoring comments, whitespace and positions.
/// Numbers are compared by numeric value.
bool JsonEquivalent(const JsonDocument& left, const JsonDocument& right);

}  // namespace ArgpiSuite::Format

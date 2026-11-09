#pragma once

#include "ir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace neura {

struct VerificationIssue {
    Span span;
    std::string message;
};

// Checks the invariants that lowering establishes and that later stages rely
// on: symbol references and kinds, operation and region shape, operand types
// of typed operations, local scoping, loop control placement, and returns.
// Issues are listed in a deterministic order: symbols first, then bodies in
// program order, depth first.
std::vector<VerificationIssue> verify_ir(const IrProgram& program);

// Throws a Diagnostic at the first issue. `stage` names the boundary being
// checked, for example "before optimization".
void require_valid_ir(const IrProgram& program, std::string_view stage);

} // namespace neura

#pragma once

#include "neura-ir/ir.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neura::optimization {

// Each pass rewrites `program` in place and returns the number of rewrites
// it applied. Every rewrite of a pass that runs in rounds removes at least
// one node, or keeps the node count and moves nodes out of a loop, which
// bounds the number of rewrites by the size of the program. Inlining runs
// between rounds and removes one call per rewrite. See
// docs/IrOptimization.md for each pass's preconditions.
std::size_t fold_constants(IrProgram& program);
std::size_t simplify_short_circuits(IrProgram& program);
std::size_t propagate_copies(IrProgram& program);
std::size_t eliminate_common_subexpressions(IrProgram& program);
std::size_t hoist_loop_invariants(IrProgram& program);
std::size_t simplify_control_flow(IrProgram& program);
std::size_t remove_unreachable(IrProgram& program);
std::size_t remove_dead_stores(IrProgram& program);
std::size_t remove_pure_discards(IrProgram& program);
std::size_t inline_calls(IrProgram& program);

// A function whose inline.directive is Always but that inline-calls never
// inlines, with the reason in words ("it is recursive").
struct InlineRefusal {
    SymbolId function;
    std::string reason;
};
std::vector<InlineRefusal> inline_refusals(IrProgram& program);

} // namespace neura::optimization

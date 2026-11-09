#pragma once

#include "neura-ir/ir.hpp"

#include <cstddef>

namespace neura::optimization {

// Each pass rewrites `program` in place and returns the number of rewrites
// it applied. Every rewrite removes at least one node, which bounds the
// number of rewrites by the size of the program. See docs/IrOptimization.md
// for each pass's preconditions.
std::size_t fold_constants(IrProgram& program);
std::size_t simplify_short_circuits(IrProgram& program);
std::size_t simplify_control_flow(IrProgram& program);
std::size_t remove_unreachable(IrProgram& program);
std::size_t remove_pure_discards(IrProgram& program);

} // namespace neura::optimization

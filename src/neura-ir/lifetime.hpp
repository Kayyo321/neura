#pragma once

#include "ir.hpp"

#include <vector>

namespace neura {

// The operations of docs/design/Lifetime030.md: Cleanup, Pending and Yield.

inline bool lifetime_operation(IrOp op) {
    return op == IrOp::Cleanup || op == IrOp::Pending || op == IrOp::Yield;
}

// True when a node of `nodes`, or a node nested in one, is a Cleanup, a
// Pending or a Yield. The IR optimizer leaves such a body as lowering wrote
// it, and inlines no call into it or of it.
bool uses_lifetime_operations(const std::vector<IrNode>& nodes);

// The number of Yield statements in `nodes`, nested ones included.
std::size_t count_yields(const std::vector<IrNode>& nodes);

// docs/design/Lifetime030.md, `using`: a return inside `using` passes its
// value to close_outcome, so the function's `Result` may not carry storage
// that belongs to the frame. Lowering checks the written result type; this
// check runs after specialization, where a generic function's `Result<T>`
// has become, say, `Result<int32[]>`, and throws E0473 at the `using`
// instead of the storage check's E0451 inside the library.
void check_using_results(const IrProgram& program);

} // namespace neura

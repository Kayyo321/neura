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

} // namespace neura

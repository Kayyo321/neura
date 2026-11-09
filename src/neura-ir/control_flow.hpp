#pragma once

#include "ir.hpp"

#include <vector>

namespace neura {

// Structural control-flow facts shared by lowering, the verifier, and the
// optimizer. Keeping one definition means the frontend's return rule, the
// verifier's check of it, and the optimizer's unreachable-code analysis agree
// for every statement form, including loops with a latch region.

// True when a break in `region` leaves the loop that owns `region`. A break
// inside a nested loop leaves that loop instead, so nested loops are not
// searched. A loop's latch may not contain break, continue, or return.
bool breaks_out(const std::vector<IrNode>& region);

// True for a loop whose condition is the literal `true` and that no break
// leaves: control never reaches the statement after it.
bool infinite_loop(const IrNode& loop);

// True when every path through `region` ends in a return or in an infinite
// loop. A non-void function body must satisfy this.
bool always_returns(const std::vector<IrNode>& region);

// True when control cannot continue to the statement after `statement` in the
// same region: return, break, continue, a block whose region never
// completes, a branch whose arms both never complete, or an infinite loop.
bool never_completes(const IrNode& statement);

} // namespace neura

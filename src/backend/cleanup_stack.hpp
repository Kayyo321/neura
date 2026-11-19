#pragma once

#include "neura-ir/ir.hpp"

#include <cstdint>
#include <vector>

namespace neura {

// The exit work of the blocks a function is inside (docs/design/
// Lifetime030.md, "Exit order"). FunctionCompiler pushes an entry when a
// block enters something that must be undone when the block is left, and
// every exit from the block, whatever its kind, emits the work of the
// entries it leaves, innermost first.
//
// The agreed order of one block's exit is: its Cleanup entries (deferred
// calls and `using` closes, last registered first), then the join of its
// task scope, then the release of its region. A `scope` block therefore
// enters its region before ScopeEnter, so that the region's entry lies
// below the task scope's.
enum class ExitWorkKind : std::uint8_t {
    // A block's region (docs/design/Storage030.md): leaving it releases the
    // region when the block allocates in one.
    Region,
    // A TaskScope statement's body: leaving it emits ScopeExit.
    TaskScope,
    // An IrOp::Cleanup statement: leaving its block emits one of its lists.
    Cleanup,
};

// How a block is left, which selects a Cleanup's list: a return runs the
// return list when there is one, every other exit the other list.
enum class ExitEdge : std::uint8_t { Fall, Jump, Return };

// The slots that hold the value a return carries while the exit work of
// the blocks it leaves runs: IrOp::Pending reads and assigns them.
struct PendingSlots {
    std::uint32_t first;
    std::size_t count;
};

} // namespace neura

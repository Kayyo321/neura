#pragma once

#include "jit/mir.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace neura::jit::mir {

// The register allocator of tier 3 (design 8.3): linear scan on SSA with
// lifetime intervals with holes and interval splitting (Wimmer and Franz,
// "Linear Scan Register Allocation on SSA Form", CGO 2010).
//
// - Liveness is computed per block on the loop nesting forest: one backward
//   pass in postorder without back edges, then every value live into a loop
//   header is made live through every block of that loop.
// - Intervals are built from it, with use positions that need a register or
//   take any location; fixed register uses and defs become short intervals
//   in their register with a move to or from the value, and clobbers block
//   their registers for the instruction.
// - The scan allocates each class on its own, splitting intervals at block
//   boundaries and before instructions, and spilling the interval whose next
//   use is furthest away. A value whose frame slot is free over its whole
//   lifetime spills to that slot; others share native stack slots.
// - Resolution adds the moves where a split interval changes location inside
//   a block, on every edge for values and phis, and puts each parallel move in
//   order (sequentialize below).
//
// The result is the same for the same function: every tie is broken by
// position, interval id or register number.

struct AllocationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Allocates `function`. Throws AllocationError when the function breaks the
// contract of mir.hpp (not SSA, a critical edge, an irreducible loop, a
// constraint that cannot hold) or when one instruction needs more registers
// at once than its class has.
Allocation allocate(const Function& function);

// Puts a parallel move in an order that runs one move after another: every
// move of `moves` reads its source before any writes its destination. The
// destinations must be distinct, and no move may use r11, xmm15 or
// `spare_slot`.
//
// A cycle is broken by saving one of its sources in r11 (a GPR value) or
// xmm15 (an XMM value). A move between two memory locations goes through the
// scratch register of its class; when the cycle being broken holds such a
// move of the saved value's class besides the one whose source it saves, the
// value is saved in the native stack slot `spare_slot` instead, so that the
// scratch register stays free for those moves, and `spare_used` is set.
std::vector<Move> sequentialize(const std::vector<Move>& moves, std::uint32_t spare_slot, bool& spare_used);

}  // namespace neura::jit::mir

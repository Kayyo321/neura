#pragma once

#include "jit/mir.hpp"

#include <string>
#include <vector>

namespace neura::jit::mir {

// An independent check of a register allocation (regalloc.hpp) against the
// function it was made for. It shares no code with the allocator beyond
// mir.hpp. It runs every block symbolically with a map from each location to
// the vreg it holds, starting empty at the entry blocks and computed to a
// fixed point at the other blocks' entries (where the maps of the
// predecessors meet, a location keeps a vreg only if every predecessor agrees),
// and then verifies, in the execution model of mir.hpp:
//
// - every use reads a location that holds exactly its vreg, and every state
//   use one that still holds it after the instruction's clobbers, temps and
//   defs; a def, and an edge for the phis of its successor, makes a new
//   instance of its vreg, so every older copy of the vreg (say a spill from
//   an earlier pass through a loop) stops holding it;
// - Register, Fixed and Any constraints hold, with registers from the
//   allocatable sets; a SameAsUse def is in the register of its use;
// - temps are allocatable, distinct, not clobbered, and distinct from the
//   instruction's use and def registers; clobbered registers and temps hold
//   nothing after the instruction (so a value kept there fails at its next
//   read);
// - every move reads a location that holds a value of its class, never moves
//   between two memory locations, and touches only allocatable registers, r11
//   or xmm15; nothing in r11 or xmm15 outlives its move list;
// - on every edge, each phi with a location receives its input there;
// - each location claimed at a block's entry holds that vreg;
// - a frame slot only ever holds a vreg whose spill home it is (on an edge,
//   once the phis have their values);
// - no edge is critical.
//
// It returns the problems found (at most 32), none when the allocation is
// valid.
std::vector<std::string> check(const Function& function, const Allocation& allocation);

}  // namespace neura::jit::mir

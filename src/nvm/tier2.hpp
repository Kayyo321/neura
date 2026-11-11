#pragma once

#include "lift/lowered_block.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace neura::nvm::tier2 {

// Tier 2 of the executor (docs/runtime/NVM.md, "Execution tiers"): a region
// is a set of blocks of one function, entered at one block, whose proven
// blocks run one after another without returning to the per-step
// dispatcher. A region program is the region compiled to one flat register
// program. It is the input of the native code generator (src/jit) as well
// as of the portable interpreter in tier2_steps.inc, so it says everything
// explicitly:
//
// - Registers. Register r < slots.size() caches frame slot slots[r] for the
//   whole region; the others are temporaries and values loaded at entry. A
//   register holds a value in its slot encoding, 64 bits.
// - Entry. The executor loads every slot register from the frame (value and
//   initialized flag), refuses to enter when a slot in `entry_reads` is not
//   initialized (no other read in the region can find one uninitialized:
//   every other read follows a write on every path), loads `constants`, and
//   refuses when the region computes floats in an environment other than
//   the default.
// - Ops. Straight-line typed operations (Lift: one lowered instruction;
//   Mask, Move, Gather, Store), control flow between the region's blocks
//   (Branch, Switch, Jump), and the effects a block's exit has inside the
//   region (Store into a slot register, an inline state operation, a poll,
//   and Complete, which counts the block's step).
// - Deoptimization points. Every op that can fail (a check of a lowered
//   instruction, a reference check, an inline state operation that
//   traps, an invalid selector) and every exit the region does not run
//   (Deopt) names the image block whose execution it belongs to. A block
//   commits its exit (Store, State's result, Poll, Complete) only after
//   every op that can fail, so at each deoptimization point the slot
//   registers hold the frame as it was when that block was entered: the
//   slot map of the region is the deoptimization map of every point. The
//   executor writes the slot registers back and lets tier 1 run that block.
// - Budget checks. Each checkpoint (the entry, and every block a loop
//   returns to) begins with Budget: the steps and polls any path can take
//   before the next checkpoint or the region's end must fit in what remains
//   of the step limit, of the scheduler's quantum, and of the caller's
//   block allowance; otherwise the region deoptimizes there and tier 1 runs
//   the blocks one at a time.
// - Leave ends the region at a block outside it, after its last block
//   committed.
// - Optimization (local to the region, each step justified there): a
//   block's Complete is folded into the Jump or Leave after it; constant
//   instructions become `immediates`; a Mask is dropped where an analysis of
//   the bits each register can hold shows it changes nothing (slot
//   registers are checked at entry against their types' widths); a checked
//   addition or subtraction of 1 becomes unchecked where a comparison on
//   every path to it shows it cannot overflow (`eliminated_checks`); pure
//   operations whose results are unused are removed; and a lifted
//   instruction writes a slot register directly (flag `defines`) instead of
//   through a Store when nothing between them can deoptimize or reads the
//   slot.
enum class Code : std::uint8_t {
    // Checkpoint `a`: deoptimize to `block` unless the budget covers
    // checkpoints[a].
    Budget,
    // dst = instructions[index](a, b); deoptimize to `block` when its check
    // fails (the block's trap exit, or a decline; tier 1 decides which).
    Lift,
    // dst = a & mask(width).
    Mask,
    // dst = a.
    Move,
    // dst = the bits gathers[index, index + width) (lane i from bit `bit` of
    // register `reg`): the output lanes of a block whose graph has no
    // neuron.
    Gather,
    // dst (a slot register) = a, and its slot becomes initialized.
    Store,
    // pc = (a & 1) ? target : other.
    Branch,
    // Exit index a: deoptimize to `block` when it is not below `width` (an
    // invalid selector); pc = operands[index + a].
    Switch,
    // pc = target.
    Jump,
    // a must be a live reference of type `index` (nil when the type allows
    // it), as the commit checks a reference read from lanes; deoptimize to
    // `block` otherwise.
    CheckRef,
    // The state operation of exits[index] on arguments operands[other,
    // other + width), its result into dst (none when dst is `absent`);
    // deoptimize to `block` when it traps. The operation is one of
    // inline_state_operation's.
    State,
    // One poll of a continue exit (counted toward the scheduler's quantum).
    Poll,
    // `block` committed: one step.
    Complete,
    // The region ends; execution continues at image block `target`.
    Leave,
    // Deoptimize to `block`: tier 1 runs it from its entry state.
    Deopt,
};

// Op flags.
inline constexpr std::uint8_t completes = 1;  // Jump, Leave: the block committed, one step
inline constexpr std::uint8_t defines = 2;    // Lift into a slot register: its slot is initialized

struct Op {
    Code code = Code::Deopt;
    std::uint8_t width = 0;
    std::uint8_t flags = 0;
    std::uint32_t dst = 0;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t target = 0;
    std::uint32_t other = 0;
    std::uint32_t block = 0;
    std::uint32_t index = 0;
};

struct GatherBit {
    std::uint32_t reg;
    std::uint32_t bit;
};

// The most blocks, and polls, any path from a checkpoint takes before it
// reaches the next checkpoint or leaves the region.
struct Checkpoint {
    std::uint32_t block = 0;
    std::uint64_t steps = 0;
    std::uint64_t polls = 0;
};

struct Region {
    std::uint32_t function = 0;
    std::uint32_t entry = 0;
    // The image blocks of the region; blocks[0] is the entry.
    std::vector<std::uint32_t> blocks;
    // Register r < slots.size() caches frame slot slots[r].
    std::vector<std::uint32_t> slots;
    // Slot registers the region may write, written back (with their
    // initialized flags) whenever it ends or deoptimizes.
    std::vector<std::uint32_t> written;
    // Slot registers read before any write on some path from the entry:
    // they must be initialized when the region is entered.
    std::vector<std::uint32_t> entry_reads;
    struct Constant {
        std::uint32_t reg;
        std::uint32_t index;
        std::uint8_t width;
    };
    // Registers loaded at entry from the constant pool, masked to `width`.
    std::vector<Constant> constants;
    // Registers loaded at entry with a value known when the region was
    // compiled (a lifted block's constant operations).
    std::vector<std::pair<std::uint32_t, std::uint64_t>> immediates;
    // Slot registers whose value must fit in `width` bits at entry, as
    // every value of the slot's type does; the region is not entered
    // otherwise. The optimizer relies on it to drop masks.
    std::vector<std::pair<std::uint32_t, std::uint8_t>> entry_canonical;
    std::uint32_t register_count = 0;
    std::vector<Op> code;
    std::vector<lift::Instruction> instructions;
    std::vector<GatherBit> gathers;
    std::vector<std::uint32_t> operands;
    std::vector<const nvr::Exit*> exits;
    std::vector<Checkpoint> checkpoints;
    bool uses_float = false;
    // Overflow checks removed by the region's range analysis (each proven
    // never to fail; the image keeps them).
    std::uint32_t eliminated_checks = 0;
};

// Whether a state operation runs inside a region: one that only reads or
// writes existing heap or global storage, never allocates, grows, frees, or
// builds text, and calls no kernel. A region calls the executor's own
// state-operation code for it, with the same checks and traps.
bool inline_state_operation(const nvr::Exit& exit);

// How tier 1 runs a block, as region formation needs it.
struct BlockForm {
    enum class Kind : std::uint8_t { None, Lifted, Wired, Empty };
    Kind kind = Kind::None;
    const lift::LoweredBlock* lifted = nullptr;
    // Not yet settled (the block may still be lifted later).
    bool pending = false;
};

struct Formation {
    std::shared_ptr<const Region> region;
    // Why no region was formed, for the statistics.
    std::string reason;
    // Blocks the region would have reached that are not settled yet.
    bool pending = false;
};

// Forms and compiles the region of `function` entered at `entry`: the
// blocks reachable from it through continue exits and inline state
// operations whose tier-1 form is lifted, wired, or empty, at most
// `max_blocks` of them. No region when the entry has no exit that stays in
// the region.
Formation form_region(const nvr::Image& image, std::uint32_t function, std::uint32_t entry,
                      const std::function<BlockForm(std::uint32_t block)>& form, std::size_t max_blocks = 128);

// A readable listing of a region program, for tests and debugging.
std::string describe(const Region& region);

} // namespace neura::nvm::tier2

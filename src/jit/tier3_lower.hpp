#pragma once

#include "jit/mir.hpp"
#include "lift/lowered_block.hpp"
#include "nvm/tier3.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neura::jit::native {

// The lowering of tier 3 (tier3_lower.cpp): the IR of a native program as
// machine IR for the register allocator, and what the emitter
// (tier3_emit.cpp) needs to turn each machine instruction into code.

// Why a function cannot be lowered; the program then stays on the program
// interpreter with this reason.
struct LoweringError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// What one machine instruction does. mir::Inst::opcode is the index of its
// Step in Lowered::steps.
enum class Kind : std::uint8_t {
    Const,           // defs[0] = imm
    RunConst,        // defs[0] = run constant `index`, masked to `width`
    EntryLoad,       // defs[0] (any location) = frame slot `index`
    EntryFlag,       // defs[0] = the initialized flag of slot `index`
    CounterAdd,      // C (r12) += imm
    Lift,            // the IR Lift `inst` (LiftShape)
    Mask,            // defs[0] = uses[0] & mask(width)
    Gather,          // defs[0] = bit `bits[i]` of uses[i], for each i
    Budget,          // deoptimizes when C + imm > room
    SwitchCheck,     // deoptimizes when uses[0] >= imm
    Guard,           // deoptimizes when uses[0] & 1 is 0
    StateCall,       // inline state operation exits[index] of the uses
    CheckReference,  // reference check of uses[0] against type `index`
    WriteBack,       // writes the physical frame from frame state `state`
    Helper,          // R3 helper exits[index] of the uses
    Call,            // function imm through exits[index] of the uses
    Return,          // returns uses[0] (or nothing)
    Leave,           // ends at block imm of the innermost frame
    Deopt,           // deoptimizes to frame state `state`
    Branch,          // uses[0] & 1: succs[0], else succs[1]
    CompareBranch,   // the compare Lift `inst`: succs[0] when it holds, else succs[1]
    Switch,          // succs[uses[0]], of imm successors
    Dispatch,        // dispatch_tables[imm] on uses[0]
};

struct Step {
    Kind kind = Kind::Const;
    // The IR instruction it comes from.
    std::uint32_t inst = nvm::tier3::none;
    // The frame state a stub materializes or a write-back writes.
    std::uint32_t state = nvm::tier3::none;
    // Why its stub deoptimizes (an nvm::tier3::Reason).
    std::uint8_t reason = 0;
    std::uint64_t imm = 0;
    std::uint32_t index = 0;
    std::uint8_t width = 0;
    // Lift and CompareBranch: the static widths of the operands (their IR
    // types) and the constant second operand, when it is one.
    std::uint8_t a_width = 64;
    std::uint8_t b_width = 64;
    bool b_constant = false;
    std::uint64_t b_value = 0;
    // Lift: whether the operation is checked. An unchecked Multiply, Negate,
    // Divide, Remainder, ShiftLeft, ShiftRight, Index or ConvertInteger (the
    // passes proved that lift::evaluate reports no failure for its operands)
    // computes evaluate's result only.
    bool checked = true;
    // Gather: the bit of each use.
    std::vector<std::uint32_t> bits;
    // The SSA values the frame state names, each with the index of the state
    // use (mir::Inst::state_uses) that holds it: several values can be one
    // virtual register.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> state_values;
    // The values of the frame state (a check's or a write-back's) whose bits
    // are known: (SSA value, bits). They have no state use.
    std::vector<std::pair<std::uint32_t, std::uint64_t>> state_constants;
};

struct Lowered {
    mir::Function function;
    std::vector<Step> steps;
    // By machine block: whether a loop starts there (its code is aligned).
    std::vector<bool> loop_head;
    // By IR entry (Function::entries): its machine block.
    std::vector<std::uint32_t> entry_blocks;
    // The largest argument array of a helper call, in words.
    std::uint32_t argument_words = 0;
};

// The code of one unit: the blocks the IR entries `entries` reach, entered
// at those entries.
Lowered lower(const nvm::tier3::Function& function, const std::vector<std::uint32_t>& entries);

// The entries of a function, in units each of whose code the allocator can
// take: the allocator refuses a loop entered other than at its header, so an
// entry into the middle of a loop (the resume block of a call in a loop, the
// head of an inner loop) cannot share the code of the loop's other entries.
// The first unit is entry 0 and every entry that can share its code without
// adding phis to a loop (an entry at a loop head can not); every other entry
// is a unit of its own.
std::vector<std::vector<std::uint32_t>> entry_units(const nvm::tier3::Function& function);

// How a Lift (or the compare of a CompareBranch) is compiled: where its
// operands are, where its result goes, and what it needs besides.
struct LiftShape {
    enum class Use : std::uint8_t {
        None,
        Register,
        // Encoded as an immediate (Step::b_value): no machine operand.
        Immediate,
        Rax,
        Rcx,
    };
    enum class Def : std::uint8_t { None, Register, SameAsA, Rax, Rdx };
    Use a = Use::Register;
    Use b = Use::None;
    Def def = Def::Register;
    std::uint8_t temps = 0;
    std::uint8_t xmm_temps = 0;
    std::uint64_t clobbers = 0;
    // The reference code through the evaluate helper (an operation the code
    // generator does not compile inline).
    bool evaluate = false;
};

// The shape of `in` given the step's operand widths and constant.
LiftShape lift_shape(const lift::Instruction& in, const Step& step);

// Whether a division or remainder by the constant `value` is compiled without
// a divide instruction: by zero (it always fails), and unsigned by a power of
// two, or signed by 1, -1 or a positive power of two (of its width).
bool constant_divisor(const lift::Instruction& in, std::uint64_t value);

// The arity of a lifted instruction (0, 1 or 2).
unsigned lift_arity(const lift::Instruction& in);

// Whether a 64-bit operation can take `value` as a sign-extended 32-bit
// immediate.
inline bool simm32(std::uint64_t value) {
    const auto v = static_cast<std::int64_t>(value);
    return v >= INT32_MIN && v <= INT32_MAX;
}

} // namespace neura::jit::native

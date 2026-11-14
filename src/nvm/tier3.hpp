#pragma once

#include "lift/lowered_block.hpp"
#include "nvr/image.hpp"
#include "tier2.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neura::nvm::tier3 {

// Tier 3 of the executor (docs/runtime/NVM.md, "Execution tiers"): a native
// program is one function program (tier2::Region with `program` set) turned
// into SSA form, with the frame written lazily. A frame slot's value lives in
// an SSA value; the frame itself is written only where something can observe
// it: before a helper, before a call, at every ending, and when the code
// deoptimizes. The IR interpreter (tier3_interp.cpp) runs this IR with the
// semantics native code has: it is the reference for every later pass and
// for the code generator.
//
// - Frame states. Every operation that can deoptimize, and every helper and
//   call, names a FrameState: for the physical frame and each inlined frame,
//   the value of every slot and initialized flag, and the block tier 1 runs
//   next. A check's state is the state at the entry of its image block (tier
//   1 re-runs the block, so the check's failure is the reference's trap); a
//   helper's and a call's is the state after the block's commit.
// - Counters. `C` counts the blocks completed since the dispatcher entered
//   the native activation chain; `K` the steps inline kernels charged (none
//   yet). Both are SSA values, flushed to the run's steps where reference
//   code can read them.
// - Budget. `Budget(P)` deoptimizes when C + K + P exceeds the room, the
//   steps left before the step limit or the caller's allowance; P is the
//   longest run of completed blocks to the next Budget or ending.

inline constexpr std::uint32_t none = nvr::absent;

enum class Type : std::uint8_t { Int, F32, F64, Ref, Ptr, None };

struct ValueType {
    Type type = Type::None;
    // 1..64 for Int, 64 for Ref, 32 and 64 for floats, 0 for Ptr and None.
    std::uint8_t width = 0;
    bool operator==(const ValueType&) const = default;
};

enum class Op : std::uint8_t {
    // Pure values.
    Const,         // imm = bits
    RunConst,      // imm = image constant index (a text or bytes handle the run assigned); type.width masks it
    EntryLoad,     // imm = slot: the slot's value in the frame when the activation starts
    EntryFlag,     // imm = slot: its initialized flag then
    EntryCounter,  // imm = 0: C, 1: K, as the context holds them (at an entry, and after a native call)
    Phi,           // extras: one operand per predecessor, in predecessor order
    Lift,          // `lifted` on operands[0], operands[1]; Checked: deoptimizes (Checks) when its check fails
    Mask,          // operands[0] & mask(imm)
    Gather,        // extras: `imm` pairs (value, bit); lane i is bit `bit` of `value`
    CounterAdd,    // operands[0] + imm
    // Checks: no effect; each names a frame state to deoptimize to.
    Budget,        // operands[0] = C, imm = P
    SwitchCheck,   // deoptimizes (Selectors) when operands[0] >= imm
    Guard,         // deoptimizes (Guards) when operands[0] & 1 is 0
    // Effects.
    StateCall,     // an inline state operation of exits[imm] on the extras (Fault: StateTraps), or, with
                   // CheckReference, a reference check of operands[0] against type imm (Fault: References)
    WriteBack,     // writes the physical frame from the state: its slots, flags and block
    Helper,        // exits[aux] on the extras under R3: operands[0] = C (the block's step is in it)
    Call,          // function imm through exits[aux] on the extras; operands[0] = C (the call block's step in it)
    Return,        // operands[0] = C before the return block's step, operands[1] = the value (none: void)
    // Terminators.
    Jump,
    Branch,        // operands[0] & 1: succs[0] when set, else succs[1]
    Switch,        // succs[operands[0]]
    Dispatch,      // dispatch_tables[imm] on operands[0]: succs[exit]
    Leave,         // the activation ends at image block imm; operands[0] = C
    Deopt,         // tier 1 runs the state's block; operands[0] = C
};

enum Flag : std::uint16_t {
    Checked = 1,         // may deoptimize
    Effect = 2,          // changes state outside the SSA values
    CheckReference = 4,  // StateCall: the reference check
};

struct Inst {
    Op op = Op::Const;
    ValueType type;
    std::uint32_t operands[3] = {none, none, none};
    // The variable-length operands of Phi, Gather, StateCall, Helper and Call:
    // Function::extras[extra, extra + count).
    std::uint32_t extra = 0;
    std::uint32_t count = 0;
    std::uint64_t imm = 0;
    // The index of the exit in Function::exits (StateCall, Helper, Call).
    std::uint32_t aux = none;
    lift::Instruction lifted;
    std::uint32_t state = none;
    // For an op whose check a guard replaced: the Guard that covers it (none
    // otherwise). It dominates the op (verifier rule V10).
    std::uint32_t guard = none;
    // The image block whose execution the op belongs to, its function, and
    // the position of the program op it came from (program order).
    std::uint32_t origin_block = none;
    std::uint32_t origin_function = none;
    std::uint32_t origin_seq = 0;
    std::uint16_t flags = 0;
};

struct Block {
    std::vector<std::uint32_t> insts;
    std::vector<std::uint32_t> preds;
    std::vector<std::uint32_t> succs;
    std::uint32_t loop_depth = 0;
    bool cold = false;
    // The image block whose code this is, and its function.
    std::uint32_t origin_block = none;
    std::uint32_t origin_function = none;
    // The first IR block of an image block's execution (its label): the end
    // of one execution of an image block and the start of the next.
    bool image_start = false;
    // For the block an entry starts at: the index of the program entry.
    std::uint32_t entry = none;
};

struct StateSlot {
    enum class Kind : std::uint8_t {
        // The frame already holds the value (an inlined frame: zero).
        Unchanged,
        Value,
        Const,
    };
    Kind kind = Kind::Unchanged;
    // The SSA value (Value), or the bits (Const).
    std::uint64_t value = 0;
    bool operator==(const StateSlot&) const = default;
};

struct StateFlag {
    enum class Kind : std::uint8_t { Unchanged, One, Value };
    Kind kind = Kind::Unchanged;
    std::uint32_t value = 0;
    bool operator==(const StateFlag&) const = default;
};

// What tier 1 needs at one point to go on as if it had run every block
// itself. A chain of states, innermost first through `caller`, describes the
// frames from the innermost to the physical frame (the frame on the stack
// when the activation started); only the innermost carries the counters.
struct FrameState {
    std::uint32_t function = none;
    // The block tier 1 runs next in this frame (every frame but the
    // innermost: its call block).
    std::uint32_t block = none;
    // The index in Function::programs of the function's program: the
    // `called_from` of the frame this level's callee pushes.
    std::uint32_t program = 0;
    // Inlined frames: where the caller receives the result.
    std::uint32_t return_slot = none;
    std::uint32_t return_block = 0;
    std::vector<StateSlot> slots;
    std::vector<StateFlag> flags;
    std::uint32_t caller = none;
    // SSA values of C and K (K: none while no inline kernel runs).
    std::uint32_t blocks = none;
    std::uint32_t kernel_steps = none;
};

struct Entry {
    // The image block and the index of the program entry (Region::entries).
    std::uint32_t block = 0;
    // The IR block holding the entry's loads, with one edge to its target.
    std::uint32_t ir_block = 0;
};

struct Function {
    std::vector<Inst> insts;
    std::vector<Block> blocks;
    std::vector<FrameState> states;
    std::vector<std::uint32_t> extras;
    std::vector<Entry> entries;
    std::vector<const nvr::Exit*> exits;
    std::vector<tier2::Region::DispatchTable> dispatch_tables;
    // programs[0] is the function's own program.
    std::vector<const tier2::Region*> programs;
    const tier2::Region* region = nullptr;
    // The image function, its slot count, and the type of each slot.
    std::uint32_t function = none;
    std::uint32_t slot_count = 0;
    std::uint32_t parameter_count = 0;
    std::vector<ValueType> slot_types;
    bool uses_float = false;
    // The deepest inlining (none yet): a native activation is entered only
    // with room for the frames it may materialize.
    std::uint32_t max_inline_depth = 0;
};

// A native program: the IR of one function program.
struct Program {
    Function ir;
};

struct BuildResult {
    std::unique_ptr<Program> program;
    // Why there is no program ("tier 3: REASON" in the statistics).
    std::string reason;
};

// Builds the IR of `program` (a function program of `image`) and verifies it
// when `verify` is set (NEURA_TIER3_VERIFY=1, and debug builds).
BuildResult build(const nvr::Image& image, const tier2::Region& program, bool verify);

// The violations of the verifier's rules (V1-V8, V10) in `function`; empty
// when it is well formed. V9 (floats) passes: nothing yet makes a float
// value deferred. Each message starts with the rule's name.
std::vector<std::string> verify(const nvr::Image& image, const Function& function);

// NEURA_TIER3_VERIFY=1.
bool verify_enabled();

// A readable listing, for tests and debugging.
std::string describe(const Function& function);

// --- The IR interpreter (tier3_interp.cpp) ---------------------------------

enum class Status : std::uint8_t {
    Ok = 0,
    // A Fault: the operation's check failed (the run deoptimizes) or, for a
    // call, the frame limit.
    Fault = 1,
    // Any other exception: the helper kept it for the executor to rethrow.
    Exception = 2,
    // A helper's Fault became the run's trap (R3).
    Trapped = 3,
};

enum class Reason : std::uint8_t { Budget, Checks, Selectors, References, StateTraps, Guards, Exits };
inline constexpr unsigned reason_count = 7;

enum class Outcome : std::uint8_t {
    // The activation returned its value to a native caller.
    Return,
    // The activation ended at image block `next_block`, outside native code.
    Leave,
    Deopt,
    Trapped,
    Exception,
};

struct Context;

// What native code asks the executor for. Each function is a static member
// of the executor; the context's `owner` is the executor.
struct Helpers {
    // The frame the activation starts in: its slots and initialized flags.
    void (*frame)(Context*, std::uint64_t** slots, std::uint8_t** initialized);
    std::uint64_t (*run_constant)(Context*, std::uint32_t index);
    Status (*state)(Context*, const nvr::Exit*, const std::uint64_t* arguments, std::uint32_t count,
                    std::uint64_t* result);
    Status (*check_reference)(Context*, std::uint32_t type, std::uint64_t bits);
    // R3: flushes the steps (C and K are in the context), runs the reference
    // state operation, and turns a Fault into the run's trap.
    Status (*helper)(Context*, const nvr::Exit*, const std::uint64_t* arguments, std::uint32_t count,
                     std::uint64_t* result);
    // Writes the physical frame from `state` (values indexed by SSA value).
    void (*write_back)(Context*, const Function*, std::uint32_t state, const std::uint64_t* values);
    // Materializes every frame of `state`, flushes and ends: the context's
    // C, `next_block` and `reason` say how.
    void (*deoptimize)(Context*, const Function*, std::uint32_t state, Reason, const std::uint64_t* values);
    // Ends the activation at image block `block` of the innermost frame.
    void (*leave)(Context*, std::uint32_t block);
    // commit_exit's Call: flushes, pushes the callee's frame. Trapped for a
    // fault (the frame limit).
    Status (*call)(Context*, const nvr::Exit*, std::uint32_t callee, const std::uint64_t* arguments,
                   std::uint32_t count, const tier2::Region* caller);
    // The native code a call to `callee` runs, or null: the callee's frame
    // is pushed, and Leave at its block 0 follows when this is null.
    const Program* (*callee_code)(Context*, std::uint32_t callee);
    // commit_exit's Return for the innermost frame, after the preconditions
    // (Fault: they do not hold, the run deoptimizes). On Ok the frame is
    // popped, the caller's result slot written, `next_block` is the caller's
    // return block.
    Status (*ret)(Context*, std::uint64_t value, bool has_value, std::uint64_t blocks);
};

struct Context {
    void* owner = nullptr;
    const Helpers* helpers = nullptr;
    // Blocks completed since the dispatcher entered the activation chain,
    // inline kernel steps, and what has been added to the run's steps.
    std::uint64_t C = 0;
    std::uint64_t K = 0;
    std::uint64_t flushed = 0;
    // The most C + K + P a Budget lets pass; the executor recomputes it
    // after every flush.
    std::uint64_t room = 0;
    std::uint64_t allowance = 0;
    std::uint32_t native_depth = 0;
    bool float_default = true;
    // The program running (the innermost activation's), for statistics.
    const tier2::Region* region = nullptr;
    // Where the ending went on, and why it deoptimized.
    std::uint32_t next_block = 0;
    Reason reason = Reason::Budget;
    // One value array per native depth, reused.
    std::vector<std::vector<std::uint64_t>> pool;
};

inline constexpr std::uint32_t max_native_depth = 256;

struct Result {
    Outcome outcome = Outcome::Leave;
    std::uint64_t value = 0;
};

// Runs `program` from the entry `entry_index`. `from_native`: the caller is
// an activation of native code that continues when this one returns.
//
// A call to a function with native code runs the callee's IR in a nested
// activation (up to `max_native_depth` deep) and goes on when it returns
// Return; any other outcome ends this activation too, touching nothing.
// Native code does the same with a native call. Past the depth, or when the
// callee has no native code, the call ends the activation with Leave at the
// callee's block 0.
Result interpret(Context& context, const Program& program, std::uint32_t entry_index, bool from_native);

} // namespace neura::nvm::tier3

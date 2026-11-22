#pragma once

#include "jit/jit.hpp"
#include "lift/lowered_block.hpp"
#include "nvr/image.hpp"
#include "tier2.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
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
//   the native activation chain; `K` the steps inline kernels (Sqrt)
//   charged. C is an SSA value; K is one in a function with inline kernels
//   (Function::inline_kernels) and otherwise stays what the context holds.
//   Both are flushed to the run's steps where reference code can read them.
// - Budget. `Budget(P)` deoptimizes when C + K + P exceeds the steps left
//   before the step limit, or C + P the caller's allowance; P is the longest
//   run of completed blocks and kernel charges to the next Budget or ending.
//   The context's room is for C with the context's K taken off: a Budget
//   compares C + P (no inline kernels) or C + (K - context.K) + P with it.
// - Passes (tier3_passes.cpp, run on the built IR by `optimize`): constant
//   folding, copy propagation, value numbering, range analysis, check
//   elimination, loop-invariant code motion with guarded loop entries, dead
//   code removal, and the lengths of the Budgets. Each keeps every
//   observable of the IR interpreter's run: the exits taken, the frames at
//   every ending, the steps, and the traps. What they make that the builder
//   does not (every form is the IR interpreter's, and tier3_interp.cpp
//   defines it):
//   - A Lift of Add or Subtract whose check was proven away is AddWrapping or
//     SubtractWrapping. A Lift of Multiply, Negate, Divide, Remainder,
//     ShiftLeft, ShiftRight, Index or ConvertInteger whose check was proven
//     away has no Checked flag and no state; it computes the result of
//     lift::evaluate, and the proof is that evaluate reports no failure for
//     its operands. A division of that kind runs only where the proof holds
//     (the passes never move it), so a code generator may use the hardware
//     division.
//   - A Const may have a float type: a folded float operation.
//   - A value a Mask of which was dropped may be wider than the Mask's type
//     (its interval fits the narrower width).
//   - A preheader (Block::preheader) holds phis, hoisted pure ops, hoisted
//     Checked Lifts, Guards, and a Jump to the loop head. A hoisted Checked
//     Lift and a Guard name a state that is the head's entry state with the
//     head's phis replaced by their preheader operands: the failing check
//     deoptimizes to the loop head with the step count of the preheader.
//   - A Guard's operand is a comparison Lift (i1) in the preheader.

inline constexpr std::uint32_t none = nvr::absent;

// `bits` with a NaN of the binary32 (`width` 32: the low 32 bits) or binary64
// format replaced by that format's canonical quiet NaN (Canon).
inline std::uint64_t canonical_nan(std::uint64_t bits, unsigned width) {
    if (width == 32) {
        const auto low = bits & 0xFFFFFFFFULL;
        return (low & 0x7F800000ULL) == 0x7F800000ULL && (low & 0x007FFFFFULL) != 0 ? 0x7FC00000ULL : bits;
    }
    return (bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL && (bits & 0x000FFFFFFFFFFFFFULL) != 0
               ? 0x7FF8000000000000ULL
               : bits;
}

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
    Budget,        // operands[0] = C, operands[1] = K (none without inline kernels), imm = P
    SwitchCheck,   // deoptimizes (Selectors) when operands[0] >= imm
    // Deoptimizes (Guards) when operands[0] & 1 is 0, to the frame state `state`. A Guard stands in a
    // loop's preheader and asserts a fact the loop's eliminated checks rest on: the loop head's entry
    // state (the head's own state with each of its phis replaced by what the preheader gives it) is
    // its state, so tier 1 re-runs the loop from its head, where it traps exactly as the reference
    // does when the fact does not hold. An operation whose check a guard replaced names it in
    // `guard`, and the guard dominates the operation (V10).
    Guard,
    // Effects.
    StateCall,     // an inline state operation of exits[imm] on the extras (Fault: StateTraps), or, with
                   // CheckReference, a reference check of operands[0] against type imm (Fault: References)
    WriteBack,     // writes the physical frame from the state: its slots, flags and block
    Helper,        // exits[aux] on the extras under R3: operands[0] = C (the block's step is in it), operands[1] = K
    Call,          // function imm through exits[aux] on the extras; operands[0] = C (the call block's step in it),
                   // operands[1] = K
    Return,        // operands[0] = C before the return block's step, operands[1] = the value (none: void),
                   // operands[2] = K; the extras are the words after the first of a function with several
                   // result words (NVR 1.7), at most Context::max_result_words - 1
    // Terminators.
    Jump,
    Branch,        // operands[0] & 1: succs[0] when set, else succs[1]
    Switch,        // succs[operands[0]]
    Dispatch,      // dispatch_tables[imm] on operands[0]: succs[exit]
    Leave,         // the activation ends at image block imm; operands[0] = C, operands[1] = K
    Deopt,         // tier 1 runs the state's block; operands[0] = C

    // Heap access through the published layout (heap.hpp), the inline form
    // of the state operations (docs/runtime/NVM.md, "Execution tiers"). An
    // entry is a Ptr: the address of its header. The checks deoptimize to
    // their state (reason References with flag ReferenceCheck, else
    // StateTraps); tier 1 re-runs the block and traps there as the
    // reference does.
    CheckNotNil,      // deoptimizes when the handle operands[0] is 0
    Lookup,           // Ptr: the entry of handle operands[0]; deoptimizes when there is none. imm bit 0: nil gives a
                      // null Ptr instead
    CheckKind,        // deoptimizes unless entry operands[0] passes type_tests[imm]; a null entry passes
    CheckInit,        // deoptimizes unless field imm of object entry operands[0] is initialized
    CheckIndex,       // deoptimizes unless operands[0] < operands[1] (unsigned)
    CheckGlobalInit,  // deoptimizes unless global imm is initialized
    // Heap reads: no effect; each reads words a heap write, a Widen, a
    // helper or a call may change (the passes keep them after those).
    TestKind,         // 1 when entry operands[0] is not null and passes type_tests[imm], else 0
    LoadTag,          // the type word (u32 at 4) of entry operands[0]
    LoadField,        // field imm of object entry operands[0]; aux: the entry's initialized-word count when every
                      // layout it can have shares it, else none (the count word decides)
    LoadLength,       // the count word (u64 at 8)
    LoadData,         // Ptr: the element data pointer (at 16)
    LoadStorage,      // the storage width byte (at 3)
    LoadElement,      // element operands[1] of data operands[0], stored at width operands[2]; imm: the declared width
    LoadVersion,      // a list's version (u64 at 32)
    LoadGlobal,       // global imm
    Select,           // extras[operands[0]] (operands[0] is below count)
    // Heap writes: effects.
    StoreField,       // field imm of object entry operands[0] = operands[1], and its initialized bit; aux as LoadField
    StoreElement,     // element operands[1] of data operands[0] at width operands[2] = operands[3]; imm: declared
                      // width. The value fits the declared width.
    StoreGlobal,      // global imm = operands[0], initialized
    Widen,            // element operands[1] of the array or list with handle operands[0] = operands[2] through
                      // Heap::set_element, which widens the storage first when the value does not fit it
    // Allocation and append with a fast path (design 6.7, 6.8): what the
    // helper exits[aux] would do, done inline when the heap's state shows
    // the reference would neither collect, nor trap, nor move the collection
    // threshold, nor take a new page or chunk; otherwise the helper itself
    // under R3 (Helper), after writing back the frame of `state` (the state
    // after the block's commit). operands[0] = C with the block's step.
    AllocObject,      // a new object of exits[aux] (layout: the exit's immediate; imm: its field count)
    Append,           // the list entry operands[1] (checked) gets operands[2]; extras: the helper's arguments; imm:
                      // the list's declared element width in bytes
    // Floats (design 7). Canon: operands[0] with a NaN of the value's format
    // (F32 or F64) replaced by the format's canonical quiet NaN; no other
    // value changes. Sqrt: the square root of the binary64 operands[0]
    // rounded to nearest even, a NaN for a negative or NaN operand; with
    // Deferred, the NaN is the hardware's (the kernel neura.math.sqrt gives
    // Canon of it). Both pure.
    Canon,
    Sqrt,
    // Word `imm` (above 0) of the result of the Call operands[0], which follows it in its block: the callee's
    // Return left it in Context::result_words, and nothing else runs between the two. The value of a call's
    // first word is the Call itself.
    ResultWord,
};

enum Flag : std::uint16_t {
    Checked = 1,         // may deoptimize
    Effect = 2,          // changes state outside the SSA values
    CheckReference = 4,  // StateCall: the reference check
    ReferenceCheck = 8,  // a heap check made for a CheckRef (deoptimization reason References)
    // A Lift of FloatAdd, FloatSubtract, FloatMultiply or FloatDivide (binary32
    // or binary64), or a Sqrt, whose NaN result is the hardware's rather than
    // the canonical NaN: NaN exactly where the reference's is, and equal to
    // it elsewhere (tier3_float.cpp places the Canons that make every
    // observed bit the reference's).
    Deferred = 16,
};

struct Inst {
    Op op = Op::Const;
    ValueType type;
    std::uint32_t operands[4] = {none, none, none, none};
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
    // otherwise). It dominates the op (verifier rule V10). A NotEqual
    // comparison that exits an induction loop and whose true edge the
    // analysis reads as "the variable is below the bound" names the Guard
    // that asserts the variable starts at or below it, and its `imm` says
    // which: bit 0 is the operand holding the variable, bit 1 set when it
    // counts down, bit 2 set when the order is signed.
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
    // A preheader the passes made: a block with one successor, the loop head,
    // that every entry into the loop passes through.
    bool preheader = false;
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

// The kind and type an entry must have: the static type of a reference
// (typed_entry's check, machine.cpp), or for a class test the layouts that
// derive from a class.
struct TypeTest {
    // The EntryKind byte (heap.hpp) the entry has.
    std::uint8_t kind = 0;
    // Text and bytes: the kind alone.
    bool any_type = false;
    // Objects: the layouts accepted; arrays and lists: the type indices
    // (every index the same canonical type as the static type). Ascending.
    std::vector<std::uint32_t> types;
    // Objects: the initialized-word count (ceil(fields / 64)) every accepted
    // layout has, or none when they differ.
    std::uint32_t words = 0xffffffffU;
    bool operator==(const TypeTest&) const = default;
};

struct Entry {
    // The image block and the index of the program entry (Region::entries).
    std::uint32_t block = 0;
    // The IR block holding the entry's loads, with one edge to its target.
    std::uint32_t ir_block = 0;
};

// The variants of a native program; a cache holds at most one native program
// per program version and variant. Standard inlines; Profile (--profile)
// and Checkable (NEURA_TIER_CHECK=1) do not, so that every frame is the
// reference's at every call and return.
enum class Variant : std::uint8_t { Standard, Profile, Checkable };
inline constexpr unsigned variant_count = 3;

struct Function {
    std::vector<Inst> insts;
    std::vector<Block> blocks;
    std::vector<FrameState> states;
    std::vector<std::uint32_t> extras;
    std::vector<Entry> entries;
    std::vector<const nvr::Exit*> exits;
    std::vector<tier2::Region::DispatchTable> dispatch_tables;
    // The tests of CheckKind and TestKind.
    std::vector<TypeTest> type_tests;
    // For each dispatch table: the index in `programs` of the program it
    // came from, and its index in that program's dispatch_tables (the
    // speculation record of its Dispatch op).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> dispatch_origins;
    // programs[0] is the function's own program.
    std::vector<const tier2::Region*> programs;
    const tier2::Region* region = nullptr;
    // The image function, its slot count, and the type of each slot.
    std::uint32_t function = none;
    std::uint32_t slot_count = 0;
    std::uint32_t parameter_count = 0;
    std::vector<ValueType> slot_types;
    bool uses_float = false;
    // NEURA_TIER3_VERIFY=1: after every inline allocation and append the
    // code asks the executor to check the heap's consistency.
    bool check_heap = false;
    // The deepest inlining: a native activation is entered only with room
    // for the frames it may materialize.
    std::uint32_t max_inline_depth = 0;
    // What the inliner did: calls inlined (at every level), and dispatch
    // sites given class tests from recorded tags.
    std::uint64_t inlined_calls = 0;
    std::uint64_t class_tested_dispatches = 0;
    // The variant it is built for. Machine code of the standard variant
    // pushes and pops the frames of its calls itself where there is room;
    // the profile variant leaves them to the helpers, which add the steps
    // to the running function at every call and return (--profile).
    Variant variant = Variant::Standard;
    // The slot count of each function the program calls (not inlined): the
    // size of the frame a call pushes.
    std::map<std::uint32_t, std::uint32_t> call_frames;
    // The result words (1 or more) of each function the program calls (not
    // inlined): a Return writes that many slots of the caller's frame.
    std::map<std::uint32_t, std::uint32_t> call_results;
    // The widths (lanes, at most 64) of the parameters of each function the
    // program calls (not inlined): the widths an argument must fit for the
    // callee's entry (a call's enter helper checks them, and so does native
    // code that enters from the callee's cell).
    std::map<std::uint32_t, std::vector<std::uint8_t>> call_widths;
    // What the passes did, by kind (tier3_passes.cpp lists the names), for
    // the tests and for NEURA_TIER_DUMP.
    std::map<std::string, std::uint64_t> pass_counts;
    // Whether the function has inline kernels (a Sqrt): its K is an SSA
    // value, which Budget, Helper, Call, Return and Leave name and every
    // frame state holds. Without them K is not an SSA value at all, and the
    // context's K stays what the activation chain has charged.
    bool inline_kernels = false;
    // Whether the function has an EnterRegion exit (NVR 1.7; from the image,
    // lifted or not, tier2::Region::enters_region): its frame may own storage
    // regions, so its Return goes through the return helper, which releases
    // them, and native code never pops its frame in place.
    bool enters_region = false;
    // By SSA value, after place_canons (tier3_float.cpp): 32 or 64 for a
    // value whose bits a frame write canonicalizes in that format (a
    // Deferred result, or a value of only such results and canonical ones);
    // 0 for a value whose bits are the reference's. Empty before.
    std::vector<std::uint8_t> frame_canon;
    // By entry (after place_canons; empty before): the float slots (F32 or
    // F64) whose value at that entry must be no NaN but the canonical NaN of
    // the slot's format. The run enters only then: a frame write
    // canonicalizes a phi of a deferred value and of such an entry value.
    std::vector<std::vector<std::uint32_t>> clean_slots;
};

struct Context;

// A native program: the IR of one function program and, where tier 3 has a
// code generator (x86-64 Linux, src/jit/tier3_native.hpp), its machine code.
// Without code the IR interpreter runs the IR.
struct Program {
    Function ir;
    // The code of entry 0, read-only and executable; null without code.
    std::shared_ptr<jit::Program> machine;
    // Entry zero may have its own graph, with all interpreter resume roots
    // removed. It owns the same code mapping and uses its own IR metadata.
    std::unique_ptr<Program> function_entry;
    const Program& for_entry(std::uint32_t index) const {
        return index == 0 && function_entry ? *function_entry : *this;
    }
    // The offset in `machine` of each IR entry's code (Function::entries),
    // or jit::native::lazy for an entry whose code is made when a run first
    // enters there; and the offset of the stub a run calls to enter code.
    std::vector<std::uint32_t> entry_code;
    std::uint32_t trampoline = 0;
    // Whether to check the register allocation of code made later.
    bool verify = false;

    // The code of IR entry `entry`, made now the first time a lazy entry is
    // asked for (by one thread; the others wait): null when it could not be
    // made. `bytes` and `failure` say what this call made, for statistics.
    const std::uint8_t* entry(std::uint32_t entry, std::uint64_t& bytes, std::string& failure) const;
    // Runs the code at `address` (an entry's) with `context` filled as
    // run_native_program fills it; the context's `outcome` says how it ended.
    void enter(Context& context, const std::uint8_t* address) const;

    // The lazy entries' code: by entry, its address once made (or `failed`),
    // and the code, kept as long as the program.
    mutable std::mutex lazy_mutex;
    mutable std::unique_ptr<std::atomic<const std::uint8_t*>[]> lazy_entries;
    mutable std::vector<std::unique_ptr<jit::Program>> lazy_code;
};

// The speculation record of one Dispatch op of a function program: the first
// `size` distinct tags the program interpreter and the IR interpreter
// selected there, in the order they were first seen, each held as the tag
// plus one (0 is empty), and whether a further distinct tag came. Relaxed
// atomics: runs sharing a cache record concurrently. The records decide only
// what the inliner inlines (tier3_inline.cpp); nothing a run observes
// depends on them.
struct DispatchRecord {
    static constexpr unsigned size = 8;
    std::atomic<std::uint64_t> tags[size] = {};
    std::atomic<std::uint8_t> megamorphic = 0;

    void record(std::uint64_t tag);
    // The recorded tags in recording order; false when megamorphic.
    bool read(std::vector<std::uint64_t>& tags) const;
};

// What the inliner reads from the cache: the published function program of a
// function (null when it has none), and the speculation record of a dispatch
// table of a program (null when there is none).
struct InlineSource {
    std::function<const tier2::Region*(std::uint32_t function)> program;
    std::function<const DispatchRecord*(const tier2::Region& program, std::uint32_t table)> record;
};

// Inlines calls into `function` (the IR of a function program, built and not
// yet optimized), as tier3_inline.cpp describes: direct calls, and the calls
// of dispatch and closure sites behind class tests on recorded tags. Verifies
// the result when `verify` is set; a violation prints it and aborts.
void inline_calls(const nvr::Image& image, Function& function, const InlineSource& source, bool verify);

struct BuildResult {
    std::unique_ptr<Program> program;
    // Why there is no program ("tier 3: REASON" in the statistics).
    std::string reason;
};

// `variant`: the profile and checkable variants write the heap and the
// globals through the reference state operations (which log the writes of
// NEURA_TIER_CHECK=1) instead of inline, and allocate and append through the
// helpers.
// Builds the IR of `program` (a function program of `image`) and verifies it
// when `verify` is set (NEURA_TIER3_VERIFY=1, and debug builds).
BuildResult build(const nvr::Image& image, const tier2::Region& program, bool verify,
                  Variant variant = Variant::Standard);

// The violations of the verifier's rules (V1-V10) in `function`; empty when
// it is well formed. Each message starts with the rule's name.
std::vector<std::string> verify(const nvr::Image& image, const Function& function);

// NEURA_TIER3_VERIFY=1.
bool verify_enabled();

// Runs the passes over a built `function` (the pipeline of tier3_passes.cpp:
// fold and propagate, number values, analyse ranges, eliminate checks, hoist
// out of loops, number again, remove dead values, recompute the Budgets),
// verifying after each pass when `verify` is set; a violation prints the
// pass and the IR and aborts. `NEURA_TIER3_PASSES=0` (a testing aid, read
// once) turns the pipeline off in TierCache::compile_native.
void optimize(const nvr::Image& image, Function& function, bool verify);

// Marks the float arithmetic and the Sqrts Deferred and places the Canons
// that make every observed bit the reference's (tier3_float.cpp, design 7.2),
// then fills Function::frame_canon. Runs last, after the passes; verifies the
// result when `verify` is set.
void place_canons(const nvr::Image& image, Function& function, bool verify);
bool passes_enabled();

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
    // popped, the caller's result slot written (and, for a function with
    // several result words, the slots after it from the context's
    // result_words), `next_block` is the caller's return block.
    Status (*ret)(Context*, std::uint64_t value, bool has_value, std::uint64_t blocks, std::uint32_t words);
    // Heap::set_element on the live array or list with handle `handle`
    // (Widen); Exception when it throws.
    Status (*set_element)(Context*, std::uint64_t handle, std::uint64_t index, std::uint64_t bits);
    // Checks the heap's own consistency and the executor's cell accounting
    // after an inline allocation or append, and stops the process with a
    // report when they do not hold (NEURA_TIER3_VERIFY=1).
    void (*check_heap)(Context*);
    // Records `tag` in the speculation record of dispatch table `table` of
    // `program` (DispatchRecord).
    void (*record)(Context*, const tier2::Region* program, std::uint32_t table, std::uint64_t tag);
    // The slow path of an allocation or append inside inlined code (design
    // 3.6): every frame of `state` materialized from `values` (the physical
    // frame written, each inlined frame pushed), the helper of `exit` run
    // under R3 with C in the context, its result stored into the innermost
    // frame's result slot, and the activation ended at the exit's resume
    // block (`next_block`). Trapped for the helper's fault.
    Status (*inlined_helper)(Context*, const Function*, std::uint32_t state, const std::uint64_t* values,
                             const nvr::Exit* exit, const std::uint64_t* arguments, std::uint32_t count,
                             std::uint64_t* result);
};

// The frame stack of a run as native code sees it (machine.cpp, FrameStack):
// `size` frame headers of `capacity` at `headers`, and the slot and
// initialized-flag arenas, `slot_size` used of `slot_capacity`. These are
// the stack's own storage, which only the stack grows (by doubling); native
// code pushes a frame without growing it and pops one, exactly as
// FrameStack::push and pop_back do, and calls a helper otherwise.
struct FrameRecord {
    void* headers = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::uint64_t* slots = nullptr;
    std::uint8_t* initialized = nullptr;
    std::size_t slot_size = 0;
    std::size_t slot_capacity = 0;
};

// The offsets native code uses in a FrameRecord and in a frame header
// (FrameStack's FrameHeader, which machine.cpp asserts them for).
namespace frame_offsets {
inline constexpr std::int32_t headers = 0;
inline constexpr std::int32_t size = 8;
inline constexpr std::int32_t capacity = 16;
inline constexpr std::int32_t slots = 24;
inline constexpr std::int32_t initialized = 32;
inline constexpr std::int32_t slot_size = 40;
inline constexpr std::int32_t slot_capacity = 48;
inline constexpr std::int32_t header_bytes = 40;
inline constexpr std::int32_t function = 0;
inline constexpr std::int32_t block = 4;
inline constexpr std::int32_t return_slot = 8;
inline constexpr std::int32_t return_block = 12;
inline constexpr std::int32_t base = 16;
inline constexpr std::int32_t boundary = 24;
inline constexpr std::int32_t called_from = 32;
} // namespace frame_offsets

static_assert(std::is_standard_layout_v<FrameRecord>);
#ifndef NEURA_WASM_RUNTIME
static_assert(offsetof(FrameRecord, headers) == frame_offsets::headers &&
              offsetof(FrameRecord, size) == frame_offsets::size &&
              offsetof(FrameRecord, capacity) == frame_offsets::capacity &&
              offsetof(FrameRecord, slots) == frame_offsets::slots &&
              offsetof(FrameRecord, initialized) == frame_offsets::initialized &&
              offsetof(FrameRecord, slot_size) == frame_offsets::slot_size &&
              offsetof(FrameRecord, slot_capacity) == frame_offsets::slot_capacity);
#endif

// How a helper of native code went (Context::status).
enum class NativeStatus : std::uint32_t {
    Ok = 0,
    // The operation's check failed: the code deoptimizes.
    Fault = 1,
    // An exception other than a Fault, kept for the executor to rethrow.
    Exception = 2,
    // A helper's Fault became the run's trap (R3).
    Trapped = 3,
    // A call left the activation at the callee's block 0 (it has no native
    // code to enter, or the native depth is reached).
    Left = 4,
};

// The helpers native code calls (System V AMD64, the context first), each a
// static member of the executor in tier3_steps.inc. Every one sets
// `status`, catches every exception, and leaves `slots`, `flags` and
// `block` describing the innermost frame, which native code reloads after
// it: a push or pop may move the frame arenas.
struct NativeHelpers {
    // An inline state operation (exits[exit] of the running program) on
    // `count` arguments: its result; Fault deoptimizes (StateTraps).
    std::uint64_t (*state)(Context*, std::uint32_t exit, const std::uint64_t* arguments, std::uint32_t count);
    // A reference check: Fault deoptimizes (References).
    std::uint64_t (*check_reference)(Context*, std::uint32_t type, std::uint64_t bits);
    // R3, with C in the context: its result, or Trapped.
    std::uint64_t (*helper)(Context*, std::uint32_t exit, const std::uint64_t* arguments, std::uint32_t count);
    // commit_exit's Call, with C in the context: the callee's frame pushed
    // (Trapped at the frame limit), then the address of the callee's native
    // entry 0, with `program` set to it, or Left.
    std::uint64_t (*call)(Context*, std::uint32_t exit, std::uint32_t callee, const std::uint64_t* arguments,
                          std::uint32_t count);
    // commit_exit's Return of `value` with C = `blocks` before the return
    // block's step, and `words` more words in the context's result_words
    // (NVR 1.7): Fault deoptimizes (Exits); on Ok the value comes back and C in
    // the context counts the return block.
    std::uint64_t (*ret)(Context*, std::uint64_t value, std::uint64_t blocks, std::uint32_t words);
    // Ends the activation at block `block` of the innermost frame, with C in
    // the context.
    std::uint64_t (*leave)(Context*, std::uint32_t block);
    // Materializes frame state `state` of the running program from `values`
    // and ends the activation, for `reason` (a Reason).
    std::uint64_t (*deoptimize)(Context*, std::uint32_t state, std::uint32_t reason);
    // lift::evaluate of the Lift instruction `inst` of the running program:
    // its result; Fault when its check fails.
    std::uint64_t (*evaluate)(Context*, std::uint32_t inst, std::uint64_t a, std::uint64_t b);
    // What `call` does after the push, for a frame of `callee` native code
    // pushed itself (with C in the context): the address of the callee's
    // native entry 0, with `program` set to it, or Left.
    std::uint64_t (*enter)(Context*, std::uint32_t callee);
};

// What native code needs to enter the native code of a callee without the
// enter helper (Context::callee_cells, one per function of the image, kept by
// the run): the callee's entry, its program and region, and the frames its
// code needs below its own. Zero (`entry` null) until the helper has entered
// the callee once, and again whenever the run's program of the function
// changes. Only for the standard variant and callees without floating-point
// code or entry-0 clean slots, whose entry conditions all depend on the
// function's program alone (see State::tier3_cache_callee).
struct CalleeCell {
    const std::uint8_t* entry = nullptr;
    const Program* program = nullptr;
    const tier2::Region* region = nullptr;
    std::uint64_t frames = 0;
};

#ifdef _WIN32
// MSVC checked iterators make std::vector non-standard-layout. Keep the
// executor-owned arrays behind a pointer so Context's native ABI and all
// offsetof assertions remain valid, including in Debug builds.
class ValuePool {
public:
    ValuePool() = default;
    ~ValuePool() { delete values_; }
    ValuePool(const ValuePool&) = delete;
    ValuePool& operator=(const ValuePool&) = delete;
    bool empty() const { return values_ == nullptr || values_->empty(); }
    void resize(std::size_t size) {
        if (values_ == nullptr) values_ = new std::vector<std::vector<std::uint64_t>>;
        values_->resize(size);
    }
    std::vector<std::uint64_t>& operator[](std::size_t index) { return (*values_)[index]; }

private:
    std::vector<std::vector<std::uint64_t>>* values_ = nullptr;
};
static_assert(std::is_standard_layout_v<ValuePool>);
#endif

// What a native activation chain reads and writes. The fields up to
// `functions` are native code's, at the offsets of `offsets` below (asserted
// after the struct); the rest are the executor's.
struct Context {
    // Blocks completed since the dispatcher entered the activation chain,
    // and inline kernel steps. Native code keeps C in r12 and stores it here
    // before every helper that reads it.
    std::uint64_t C = 0;
    std::uint64_t K = 0;
    // The most C + P a Budget lets pass, the kernel steps K up to now taken
    // off (a function with inline kernels adds the ones it charged since);
    // the executor recomputes it after every flush, and machine code takes
    // off the kernel steps it charged where it stores K without one.
    std::uint64_t room = 0;
    // The innermost frame: its slots, initialized flags and block.
    std::uint64_t* slots = nullptr;
    std::uint8_t* flags = nullptr;
    std::uint32_t* block = nullptr;
    // The program of the running activation. A native callee's entry saves
    // it, and its caller restores it when the callee returns.
    const Program* program = nullptr;
    // The values a deoptimization stub stores, by SSA value of the running
    // program, for `deoptimize`; and the run's constant values.
    std::uint64_t* values = nullptr;
    const std::uint64_t* run_constants = nullptr;
    // A NativeStatus.
    std::uint32_t status = 0;
    // How the code ended (an Outcome), from the stub that entered it.
    std::uint32_t outcome = 0;
    std::uint32_t native_depth = 0;
    // Where the ending went on.
    std::uint32_t next_block = 0;
    NativeHelpers functions{};
    // The frame stack (FrameRecord), the frame limit, and the calls and
    // returns native code made by pushing and popping frames itself.
    FrameRecord* frames = nullptr;
    std::uint64_t max_frames = 0;
    std::uint64_t inline_calls = 0;
    std::uint64_t inline_returns = 0;
    CalleeCell* callee_cells = nullptr;
    // Calls native code entered from a callee's cell, for the statistics.
    std::uint64_t cell_entries = 0;
    // The words after the first that the last Return of a function with several result words left
    // (result_words[i] is word i + 1). A Return writes them before it pops the frame (the `ret`
    // helpers take their count), and the Call that follows reads them (ResultWord) before anything
    // else runs.
    static constexpr std::uint32_t max_result_words = 64;
    std::uint64_t result_words[max_result_words] = {};

    void* owner = nullptr;
    const Helpers* helpers = nullptr;
    // What has been added to the run's steps.
    std::uint64_t flushed = 0;
    std::uint64_t allowance = 0;
    bool float_default = true;
    // The program running (the innermost activation's), for statistics.
    const tier2::Region* region = nullptr;
    // Why it deoptimized.
    Reason reason = Reason::Budget;
    // One value array per native depth, reused.
#ifdef _WIN32
    ValuePool pool;
#else
    std::vector<std::vector<std::uint64_t>> pool;
#endif

    // The heap and the globals, as the executor publishes them (heap.hpp,
    // "Published layout"): the handle table and allocator records, the
    // global words and flags, and the executor's own cell accounting. Native
    // code allocates inline only when `fast_allocation` is set (the pool is
    // in use, no fleet shares the cells, no heap log records writes).
    const void* handle_table = nullptr;
    void* allocator = nullptr;
    std::uint64_t* globals = nullptr;
    std::uint8_t* global_initialized = nullptr;
    std::uint64_t* live_cells = nullptr;
    std::uint64_t* next_collection = nullptr;
    std::uint64_t* peak_cells = nullptr;
    std::uint64_t max_handles = 0;
    std::uint64_t fast_allocation = 0;
    // Inline allocations and appends made, for the statistics.
    std::uint64_t fast_allocations = 0;
    std::uint64_t fast_appends = 0;
    // The heap's helpers for native code (System V AMD64, the context
    // first); each sets `status`.
    struct HeapFunctions {
        // Widen: Exception when the store throws.
        std::uint64_t (*set_element)(Context*, std::uint64_t handle, std::uint64_t index, std::uint64_t bits);
        void (*check_heap)(Context*);
        // Helpers::inlined_helper for the running program, from the values
        // its stub stored; C is in the context.
        std::uint64_t (*inlined_helper)(Context*, std::uint32_t exit, const std::uint64_t* arguments, std::uint32_t count,
                                        std::uint32_t state);
        // Grows the full list whose entry is the second argument (Heap::grow);
        // status Ok, or Exception when the allocation fails.
        std::uint64_t (*grow_list)(Context*, void* entry);
    } heap_functions{};
};

// The offsets native code uses.
namespace offsets {
inline constexpr std::int32_t C = 0;
inline constexpr std::int32_t K = 8;
inline constexpr std::int32_t room = 16;
inline constexpr std::int32_t slots = 24;
inline constexpr std::int32_t flags = 32;
inline constexpr std::int32_t block = 40;
inline constexpr std::int32_t program = 48;
inline constexpr std::int32_t values = 56;
inline constexpr std::int32_t run_constants = 64;
inline constexpr std::int32_t status = 72;
inline constexpr std::int32_t outcome = 76;
inline constexpr std::int32_t native_depth = 80;
inline constexpr std::int32_t next_block = 84;
inline constexpr std::int32_t state = 88;
inline constexpr std::int32_t check_reference = 96;
inline constexpr std::int32_t helper = 104;
inline constexpr std::int32_t call = 112;
inline constexpr std::int32_t ret = 120;
inline constexpr std::int32_t leave = 128;
inline constexpr std::int32_t deoptimize = 136;
inline constexpr std::int32_t evaluate = 144;
inline constexpr std::int32_t enter = 152;
inline constexpr std::int32_t frames = 160;
inline constexpr std::int32_t max_frames = 168;
inline constexpr std::int32_t inline_calls = 176;
inline constexpr std::int32_t inline_returns = 184;
inline constexpr std::int32_t callee_cells = 192;
inline constexpr std::int32_t cell_entries = 200;
inline constexpr std::int32_t result_words = 208;
} // namespace offsets

static_assert(std::is_standard_layout_v<Context>);
#ifndef NEURA_WASM_RUNTIME
static_assert(offsetof(Context, C) == offsets::C && offsetof(Context, K) == offsets::K &&
              offsetof(Context, room) == offsets::room && offsetof(Context, slots) == offsets::slots &&
              offsetof(Context, flags) == offsets::flags && offsetof(Context, block) == offsets::block &&
              offsetof(Context, program) == offsets::program && offsetof(Context, values) == offsets::values &&
              offsetof(Context, run_constants) == offsets::run_constants &&
              offsetof(Context, status) == offsets::status && offsetof(Context, outcome) == offsets::outcome &&
              offsetof(Context, native_depth) == offsets::native_depth &&
              offsetof(Context, next_block) == offsets::next_block);
static_assert(offsetof(Context, functions) + offsetof(NativeHelpers, state) == offsets::state &&
              offsetof(Context, functions) + offsetof(NativeHelpers, check_reference) == offsets::check_reference &&
              offsetof(Context, functions) + offsetof(NativeHelpers, helper) == offsets::helper &&
              offsetof(Context, functions) + offsetof(NativeHelpers, call) == offsets::call &&
              offsetof(Context, functions) + offsetof(NativeHelpers, ret) == offsets::ret &&
              offsetof(Context, functions) + offsetof(NativeHelpers, leave) == offsets::leave &&
              offsetof(Context, functions) + offsetof(NativeHelpers, deoptimize) == offsets::deoptimize &&
              offsetof(Context, functions) + offsetof(NativeHelpers, evaluate) == offsets::evaluate &&
              offsetof(Context, functions) + offsetof(NativeHelpers, enter) == offsets::enter);
static_assert(offsetof(Context, callee_cells) == offsets::callee_cells &&
              offsetof(Context, cell_entries) == offsets::cell_entries &&
              offsetof(Context, result_words) == offsets::result_words);
static_assert(offsetof(Context, frames) == offsets::frames && offsetof(Context, max_frames) == offsets::max_frames &&
              offsetof(Context, inline_calls) == offsets::inline_calls &&
              offsetof(Context, inline_returns) == offsets::inline_returns);
#endif

// The fields of a CalleeCell.
namespace cell_offsets {
inline constexpr std::int32_t entry = static_cast<std::int32_t>(offsetof(CalleeCell, entry));
inline constexpr std::int32_t program = static_cast<std::int32_t>(offsetof(CalleeCell, program));
inline constexpr std::int32_t region = static_cast<std::int32_t>(offsetof(CalleeCell, region));
inline constexpr std::int32_t frames = static_cast<std::int32_t>(offsetof(CalleeCell, frames));
inline constexpr std::int32_t size = static_cast<std::int32_t>(sizeof(CalleeCell));
} // namespace cell_offsets

// The offsets of the heap's fields (Context, "The heap and the globals").
namespace offsets {
inline constexpr std::int32_t region = static_cast<std::int32_t>(offsetof(Context, region));
inline constexpr std::int32_t handle_table = static_cast<std::int32_t>(offsetof(Context, handle_table));
inline constexpr std::int32_t allocator = static_cast<std::int32_t>(offsetof(Context, allocator));
inline constexpr std::int32_t globals = static_cast<std::int32_t>(offsetof(Context, globals));
inline constexpr std::int32_t global_initialized = static_cast<std::int32_t>(offsetof(Context, global_initialized));
inline constexpr std::int32_t live_cells = static_cast<std::int32_t>(offsetof(Context, live_cells));
inline constexpr std::int32_t next_collection = static_cast<std::int32_t>(offsetof(Context, next_collection));
inline constexpr std::int32_t peak_cells = static_cast<std::int32_t>(offsetof(Context, peak_cells));
inline constexpr std::int32_t max_handles = static_cast<std::int32_t>(offsetof(Context, max_handles));
inline constexpr std::int32_t fast_allocation = static_cast<std::int32_t>(offsetof(Context, fast_allocation));
inline constexpr std::int32_t fast_allocations = static_cast<std::int32_t>(offsetof(Context, fast_allocations));
inline constexpr std::int32_t fast_appends = static_cast<std::int32_t>(offsetof(Context, fast_appends));
inline constexpr std::int32_t set_element =
    static_cast<std::int32_t>(offsetof(Context, heap_functions) + offsetof(Context::HeapFunctions, set_element));
inline constexpr std::int32_t check_heap =
    static_cast<std::int32_t>(offsetof(Context, heap_functions) + offsetof(Context::HeapFunctions, check_heap));
inline constexpr std::int32_t grow_list =
    static_cast<std::int32_t>(offsetof(Context, heap_functions) + offsetof(Context::HeapFunctions, grow_list));
inline constexpr std::int32_t inlined_helper =
    static_cast<std::int32_t>(offsetof(Context, heap_functions) + offsetof(Context::HeapFunctions, inlined_helper));
} // namespace offsets

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

// The fast paths of AllocObject and Append on the heap the context
// publishes, which native code repeats instruction for instruction: true when
// they ran (the handle in `handle`); false, having changed nothing, when the
// reference would collect, trap, move the collection threshold, take a page
// or a chunk, or widen (a full list grows inside native code instead).
bool allocate_inline(Context& context, std::uint32_t layout, std::uint64_t fields, std::uint64_t& handle);
// A full list grows here when the context has the grow helper
// (Context::heap_functions); `failed` is set when that fails (the status of
// the context says how).
bool append_inline(Context& context, std::uint64_t entry, std::uint64_t value, bool* failed = nullptr);

} // namespace neura::nvm::tier3

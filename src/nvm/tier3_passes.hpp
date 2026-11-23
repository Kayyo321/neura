#pragma once

#include "tier3.hpp"

#include <map>
#include <string>
#include <vector>

// What the passes of tier 3 (tier3_passes.cpp, tier3_range.cpp,
// tier3_licm.cpp) share: the control-flow graph with its dominators, where
// each instruction sits, value replacement, and the pass entry points. Every
// pass visits blocks in reverse postorder and values in id order, and keeps
// no state that depends on an address or a hash order.
namespace neura::nvm::tier3::detail {

using Counts = std::map<std::string, std::uint64_t>;

// Reverse postorder from the entry blocks, and the dominators with a virtual
// root over them (so a block an entry reaches directly is dominated only by
// itself and the root).
struct Cfg {
    explicit Cfg(const Function& function);

    std::uint32_t root = 0;
    // The reachable blocks in reverse postorder.
    std::vector<std::uint32_t> rpo;
    // Position in `rpo`, or none; indexed by block, with the root last.
    std::vector<std::uint32_t> number;
    std::vector<std::uint32_t> idom;
    // The dominator tree, in reverse postorder; indexed by block and root.
    std::vector<std::vector<std::uint32_t>> children;

    bool reachable(std::uint32_t block) const { return number[block] != none; }
    // Block `a` dominates block `b` (every path from an entry to b passes a).
    bool dominates(std::uint32_t a, std::uint32_t b) const;
};

// The block and position of each instruction in the blocks (none for one
// that is in no block).
struct Places {
    explicit Places(const Function& function);
    std::vector<std::uint32_t> block;
    std::vector<std::uint32_t> position;
};

// An op that changes state outside the SSA values (the verifier's notion).
bool is_effect(const Inst& in);
// A heap check: deoptimizes, changes nothing (Lookup also gives the entry).
bool is_heap_check(Op op);
// A heap read: no effect and no check; it reads words a write may change.
bool is_heap_read(Op op);

// An operand reference as a position: below operand_slots it is
// operands[position], from it extras[extra + position - operand_slots].
inline constexpr unsigned operand_slots = sizeof(Inst::operands) / sizeof(Inst::operands[0]);

// The positions of the values an instruction reads (a phi's inputs, and the
// variable-length operands of the ops that have them, included).
template <typename Visit>
void for_each_operand_position(const Inst& in, Visit&& visit) {
    for (unsigned k = 0; k < operand_slots; ++k) {
        if (in.operands[k] != none) visit(k);
    }
    switch (in.op) {
    case Op::Gather:
        for (std::uint32_t i = 0; i < in.count; i += 2) visit(operand_slots + i);
        break;
    case Op::Phi:
    case Op::StateCall:
    case Op::Helper:
    case Op::Call:
    case Op::Select:
    case Op::AllocObject:
    case Op::Append:
    case Op::Return:
        for (std::uint32_t i = 0; i < in.count; ++i) visit(operand_slots + i);
        break;
    default: break;
    }
}

// The value at an operand position of `in`.
inline std::uint32_t operand_value(const Function& f, const Inst& in, unsigned position) {
    return position < operand_slots ? in.operands[position] : f.extras[in.extra + position - operand_slots];
}

// The values an instruction reads.
template <typename Visit>
void for_each_operand(const Function& f, const Inst& in, Visit&& visit) {
    for_each_operand_position(in, [&](unsigned position) { visit(operand_value(f, in, position)); });
}

// The values a frame state (and its callers) names.
template <typename Visit>
void for_each_state_value(const Function& f, std::uint32_t state, Visit&& visit) {
    for (std::uint32_t depth = 0; state != none && depth < 64; ++depth) {
        const auto& s = f.states[state];
        if (s.blocks != none) visit(s.blocks);
        if (s.kernel_steps != none) visit(s.kernel_steps);
        for (const auto& slot : s.slots) {
            if (slot.kind == StateSlot::Kind::Value) visit(static_cast<std::uint32_t>(slot.value));
        }
        for (const auto& flag : s.flags) {
            if (flag.kind == StateFlag::Kind::Value) visit(flag.value);
        }
        state = s.caller;
    }
}

// Replaces values by others, applied at once to every operand and state.
class Replacements {
public:
    explicit Replacements(std::size_t size);
    void set(std::uint32_t from, std::uint32_t to) { to_[from] = to; }
    std::uint32_t find(std::uint32_t v);
    bool any() const { return any_; }
    void grow(std::size_t size);
    // Rewrites every instruction and state; a slot whose value became a
    // Const gets the constant itself.
    void apply(Function& function);

private:
    std::vector<std::uint32_t> to_;
    bool any_ = false;
};

// Removes the edge from `from` to `to` (one occurrence): the predecessor and
// the phi operands that came with it.
void remove_edge_target(Function& f, std::uint32_t from, std::uint32_t to);
// Deletes the blocks no entry reaches and renumbers the others in order.
bool remove_unreachable(Function& f);
// Renumbers the values and states that are still used.
void compact(Function& f);
// Recomputes the P of every Budget on the final graph. Throws when a loop has
// no Budget.
void compute_budgets(Function& f);

// Constant folding, trivial phis, and unreachable blocks, to a fixed point.
bool simplify(Function& f, Counts& counts);
// Trivial SSA aliases and phis that join identical constants.
bool copy_phis(Function& f, Counts& counts);
// Value numbering over pure ops and identical checks.
bool number_values(Function& f, Counts& counts);
// Pure values nothing reads.
bool remove_dead(Function& f, Counts& counts);
// Range analysis, check elimination and the guards of induction variables.
bool eliminate_checks(const nvr::Image& image, Function& f, Counts& counts);
// Tests only: checks the range analysis of the lifted instruction `l` (result
// `result_width` bits wide, operands declared `dx` and `dy` bits wide in the
// register intervals given) against lift::evaluate on the pairs of values in
// them: every result a passing execution gives is in the computed range, and a
// check the analysis removes never fails. Empty when sound, else what failed.
std::string audit_lift(const lift::Instruction& l, unsigned result_width, unsigned dx, unsigned dy, bool has_y, std::uint64_t x_lo,
                       std::uint64_t x_hi, std::uint64_t y_lo, std::uint64_t y_hi, std::uint64_t samples);
// Tests only: checks the narrowing of two ranges by a comparison (`op`, true
// or false as `sense`, at `w` bits) against the pairs of values in the
// intervals that satisfy it.
std::string audit_refine(lift::Opcode op, bool sense, bool is_signed, unsigned w, unsigned dx, unsigned dy, std::uint64_t x_lo,
                         std::uint64_t x_hi, std::uint64_t y_lo, std::uint64_t y_hi, std::uint64_t samples);
// Preheaders and loop-invariant code motion.
bool hoist_invariants(const nvr::Image& image, Function& f, Counts& counts,
                      std::span<const LoopSite> checked_loops = {});
// Bounds checks covered by stable heap metadata and a preheader guard.
bool guard_heap_bounds(Function& f, Counts& counts, std::span<const LoopSite> checked_loops = {});

// A natural loop with a single header that dominates it.
struct Loop {
    std::uint32_t header = 0;
    std::vector<std::uint32_t> latches;
    // The blocks of the body (the header included), ascending.
    std::vector<std::uint32_t> body;
    std::vector<std::uint8_t> in_body;
    std::uint32_t depth = 0;
};
// The loops whose header dominates every block of the body, innermost first.
std::vector<Loop> find_loops(const Function& f, const Cfg& cfg);
// The state tier 1 resumes with at the entry of the header's image block,
// for use in the preheader; none when the header is not a checkpoint with an
// entry state (the loop is then left alone).
std::uint32_t head_state(const Function& f, const Loop& loop);
// Gives `loop` a preheader on its non-back-edge predecessors and returns it.
// The header's phis take their entry operands from phis of the preheader.
std::uint32_t make_preheader(Function& f, const Loop& loop);
// A copy of the header's entry state, in the preheader: the header's phis
// replaced by what the preheader gives them.
std::uint32_t preheader_state(Function& f, const Loop& loop, std::uint32_t preheader);
bool loop_preflight_disabled(const Function& f, const Loop& loop, std::span<const LoopSite> checked_loops);
// The value the preheader gives the header's phi `phi`.
std::uint32_t entry_operand(const Function& f, const Loop& loop, std::uint32_t phi);

// --- Floats (tier3_float.cpp, design 7.2) ---------------------------------------------------------

// These producers compute a fresh binary32 and clear the rest of its word.
// FloatNegate only flips one bit, including when its input is a raw word.
inline bool rounded_binary32(const Inst& in) {
    if (in.op != Op::Lift || in.type.type != Type::F32) return false;
    switch (in.lifted.op) {
    case lift::Opcode::FloatAdd:
    case lift::Opcode::FloatSubtract:
    case lift::Opcode::FloatMultiply:
    case lift::Opcode::FloatDivide:
    case lift::Opcode::FloatFromInteger:
    case lift::Opcode::FloatConvert: return true;
    default: return false;
    }
}

// What a value's bits are, measured against the reference's (the program
// interpreter, whose float arithmetic gives the canonical NaN):
enum class NanClass : std::uint8_t {
    // The reference's bits, and no NaN but the canonical NaN: of either
    // format (a constant that is neither format's NaN), or of binary32 or
    // binary64.
    Clean,
    Clean32,
    Clean64,
    // The reference's bits.
    Exact,
    // A NaN exactly where the reference's is, which is then its format's
    // canonical NaN, and the reference's bits elsewhere: Canon in that format
    // gives the reference's bits (a Deferred result, or a phi of such results
    // and clean values).
    Deferred32,
    Deferred64,
    // A NaN exactly where the reference's is (in that format), the
    // reference's bits elsewhere, but no Canon gives the reference's NaN (a
    // negated Deferred result, a phi of a Deferred result and an exact value).
    Mixed32,
    Mixed64,
    // None of these.
    Mixed,
};

// The class of every value (design 7.2): a fixed point over the phis.
std::vector<NanClass> nan_classes(const Function& f);

// By entry: the float slots whose entry value the classes take as clean and
// that reach a deferred phi through phis and abs: their Function::clean_slots.
std::vector<std::vector<std::uint32_t>> clean_slots_needed(const Function& f, const std::vector<NanClass>& classes);

// The format (32 or 64) of a deferred or mixed class; 0 for the others.
unsigned nan_format(NanClass c);
// Whether the class is the reference's bits (a clean or exact class).
bool exact(NanClass c);

// How an op reads one of its operands' bits (design 7.2).
enum class BitsUse : std::uint8_t {
    // Not at all (an integer counter), or only as a NaN-insensitive float
    // operand of the format: any NaN gives the result the reference's NaN
    // gives.
    Insensitive32,
    Insensitive64,
    // The result is the operand with its sign changed or cleared, or the
    // operand itself (FloatNegate, abs, a phi).
    Propagating,
    // The bits leave the float domain.
    Sensitive,
};

// The value operand of an abs (a Lift BitAnd, typed F32 or F64, of a value
// and the Const that is the magnitude mask of that format), or none.
std::uint32_t abs_operand(const Function& f, const Inst& in);

// The operand positions of `in` (for_each_operand_position), as a list.
std::vector<unsigned> value_positions(const Inst& in);
// How `in` reads its operand at `position`.
BitsUse bits_use(const Function& f, const Inst& in, unsigned position);

} // namespace neura::nvm::tier3::detail

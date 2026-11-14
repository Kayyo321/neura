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

// The values an instruction reads, including its frame state's.
template <typename Visit>
void for_each_operand(const Function& f, const Inst& in, Visit&& visit) {
    for (const auto operand : in.operands) {
        if (operand != none) visit(operand);
    }
    switch (in.op) {
    case Op::Gather:
        for (std::uint32_t i = 0; i < in.count; i += 2) visit(f.extras[in.extra + i]);
        break;
    case Op::Phi:
    case Op::StateCall:
    case Op::Helper:
    case Op::Call:
        for (std::uint32_t i = 0; i < in.count; ++i) visit(f.extras[in.extra + i]);
        break;
    default: break;
    }
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
bool hoist_invariants(Function& f, Counts& counts);

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
// The value the preheader gives the header's phi `phi`.
std::uint32_t entry_operand(const Function& f, const Loop& loop, std::uint32_t phi);

} // namespace neura::nvm::tier3::detail

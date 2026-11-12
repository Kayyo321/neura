#pragma once

#include "circuit/circuit.hpp"
#include "nvr/image.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace neura::lift {

// The values a wire can take, as a closed interval.
struct Interval {
    std::int64_t lo = 0;
    std::int64_t hi = 0;
};

// An exact proof that reference evaluation of a graph (docs/runtime/NVM.md,
// "Graphs and continuations") cannot overflow for any assignment of 0 or 1
// to its input lanes. Every input lane starts as [0, 1]. Each node is
// visited in stored order: its bias, then each edge in stored (ascending
// source) order, where the product weight * [lo, hi] and the running partial
// sum are each required to stay within signed 64-bit arithmetic for every
// value in their intervals; identity keeps the final sum's interval and ReLU
// clamps it at zero. The analysis uses checked 64-bit arithmetic on the
// interval bounds and gives up where a bound leaves the range, so it cannot
// overflow itself. Intervals over-approximate the reachable values, so a
// failure means only that overflow was not excluded.
//
// Intervals alone lose the correlation between terms: a sum lane reads its
// carry, a selection reads both halves of a choice. Two sound refinements
// narrow a node's interval after its own checks, for its readers:
//
// - Wire groups: wires whose weighted sum is known to lie in a small range.
//   A step pair, P = relu(L + b) and Q = relu(L + b - 1) over the same
//   weighted sum L, gives P - Q in [0, 1] for integer L. Two halves of a
//   selection on adjacent wires, read with equal weights, form a group when
//   case analysis shows their sum in [0, 1]. A node that reads a group whole,
//   in proportion k, adds k times the group's range.
// - Case analysis: a node still wider than [0, 1] is evaluated for every
//   assignment of a small cut of wires below it, each over its interval and
//   each whole group as one variable, which covers every input.
// - Elimination: when enumerating the cut's assignments together is too
//   costly (a carry chain links every bit of an adder, so a test of its sum
//   reads a cut as wide as the adder), the same range is computed by dynamic
//   programming along the cone in stored order: a state assigns values to
//   the frontier (free wires introduced so far and computed nodes a later
//   node still reads) and carries the range of the part of the node's sum
//   already known; a wire leaves the frontier after its last reader and
//   states that agree on the rest merge, keeping the union of their ranges.
//   Each node's edges are added one at a time to a running sum, so a free
//   wire read only by that node is summed out at once. The result is the
//   range enumeration would find, at a cost that grows with the frontier.
//   The float circuits' sums and selections, whose correlations intervals
//   lose stage after stage, are narrowed this way, node by node, and their
//   output lanes then follow.
//
// The checks of each node in reference order use its sources' refined
// bounds, which are sound, so the overflow proof stays exact in the sense
// above: it fails only where overflow was not excluded.
struct WireGroup {
    // (wire, coefficient); the first coefficient is 1.
    std::vector<std::pair<std::uint32_t, std::int64_t>> members;
    Interval range;
};

// How the refined analysis uses advice (prove_no_overflow).
enum class AdviceUse {
    // Not at all: the analysis as it runs without advice.
    none,
    // The cut the advice names is tried first, then the search.
    first,
    // A range found from the advice's cut stands: the search is not run
    // after it, and two sources are tried as the halves of a selection only
    // when the advice names one gadget instance for both.
    decisive,
};

struct OverflowProof {
    bool safe = false;
    // When not safe: the case analysis ran out of the work it was given, so
    // nothing was decided. More work may prove the graph; `wires`, `groups`,
    // and `group_of` then hold what the nodes before `node` settled.
    bool exhausted = false;
    // When not safe: the first node whose arithmetic was not proven in range.
    std::size_t node = 0;
    // When safe: the interval of every wire, inputs first.
    std::vector<Interval> wires;
    // Wire groups, and each wire's group or nvr::absent.
    std::vector<WireGroup> groups;
    std::vector<std::uint32_t> group_of;
    // How the analysis used its advice (prove_no_overflow).
    AdviceUse advice_use = AdviceUse::none;
};

// With `refine` off, the analysis skips both refinements above except the
// step pairs: every interval is still sound, only wider, so a proof found
// this way is as valid as a refined one and is much cheaper to find. The
// lift library tries it first and refines only when it is not enough.
//
// `work`, when given, bounds the case analysis: each node it evaluates for
// one assignment costs one unit, taken from *work. A node whose sources keep
// their values from the previous assignment is not evaluated again, and an
// enumeration the analysis has already made is not made again, so neither
// costs anything. When it runs out, the proof stops as `exhausted`. The
// bound changes only whether an answer is found, never which: a proof found
// within it is the proof found without.
//
// `resume`, when given, is an earlier refined analysis of the same graph
// that ran out of work: it kept the intervals and groups of every node it
// settled (`wires` then ends before `node`), and the analysis continues
// from there. A resumed proof is the proof an unbounded analysis finds.
//
// `advice`, when given, names for each node the wires that bound the gadget
// instance that built it (backend::GadgetAdvice, recorded while the runtime
// regenerates the block). The refined analysis tries those cuts first; it
// establishes every interval and group itself, so advice that is absent,
// wrong, or names another graph changes only how long the proof takes and
// whether one is found, never makes a false proof.
//
// `use` says how (AdviceUse). The decisive uses are much faster where the
// advice is right and may fail to prove a graph where it is not, so a
// caller that sees the analysis fail runs it again with the next use, down
// to AdviceUse::none. Every use establishes every fact itself. A resumed
// analysis must use the same setting.
//
// Precondition: every node reads only earlier wires, its edges name
// strictly ascending sources (so no wire twice), and the graph has fewer
// than 2^31 wires, as the loader requires of a graph it accepts. For a graph
// that does not meet it, the proof is not safe and `node` names the first
// node that breaks it (0 when there are too many wires); nothing else is
// analysed. lanes_are_bits accepts no lane of such a graph either, nor of a
// proof whose wires or group entries do not match the graph's wire count.
using ProofAdvice = backend::GadgetAdvice;
OverflowProof prove_no_overflow(const nvr::Graph& graph, bool refine = true, std::uint64_t* work = nullptr,
                                const OverflowProof* resume = nullptr, const ProofAdvice* advice = nullptr,
                                AdviceUse use = AdviceUse::none);

// How lanes_are_bits established each lane.
struct BitEvidence {
    std::size_t intervals = 0;
    std::size_t one_hot = 0;
    std::size_t cases = 0;
    std::size_t eliminated = 0;
};

// Whether each of the given output lanes is exactly 0 or 1 on every input
// assignment. Wires are integers, so a wire whose interval lies within
// [0, 1] is a bit. Sum lanes and selector lanes add terms that are not
// independent (a sum bit subtracts twice its carry; a selector adds exit
// indicators of which at most one holds), so two exact arguments complete
// the intervals: a sum of pairwise contradictory pattern neurons (equality
// tests that require opposite values of some bit) has at most one term set;
// and a bounded case analysis evaluates the lane for every assignment of a
// cut of wires below it, each over its interval, with a wire group as one
// variable. Each argument covers every input assignment; none samples.
// Without `cases`, only the first two arguments are tried. `work` bounds the
// case analysis as for prove_no_overflow; when it runs out the result is
// false and *work is zero.
bool lanes_are_bits(const OverflowProof& proof, const nvr::Graph& graph, std::span<const std::uint32_t> lanes,
                    BitEvidence* evidence = nullptr, bool cases = true, std::uint64_t* work = nullptr,
                    const ProofAdvice* advice = nullptr);

} // namespace neura::lift

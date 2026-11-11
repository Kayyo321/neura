#pragma once

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
//
// The checks of each node in reference order use its sources' refined
// bounds, which are sound, so the overflow proof stays exact in the sense
// above: it fails only where overflow was not excluded.
struct WireGroup {
    // (wire, coefficient); the first coefficient is 1.
    std::vector<std::pair<std::uint32_t, std::int64_t>> members;
    Interval range;
};

struct OverflowProof {
    bool safe = false;
    // When not safe: the first node whose arithmetic was not proven in range.
    std::size_t node = 0;
    // When safe: the interval of every wire, inputs first.
    std::vector<Interval> wires;
    // Wire groups, and each wire's group or nvr::absent.
    std::vector<WireGroup> groups;
    std::vector<std::uint32_t> group_of;
};

OverflowProof prove_no_overflow(const nvr::Graph& graph);

// How lanes_are_bits established each lane.
struct BitEvidence {
    std::size_t intervals = 0;
    std::size_t one_hot = 0;
    std::size_t cases = 0;
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
bool lanes_are_bits(const OverflowProof& proof, const nvr::Graph& graph, std::span<const std::uint32_t> lanes,
                    BitEvidence* evidence = nullptr);

} // namespace neura::lift

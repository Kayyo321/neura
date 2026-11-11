#include "overflow.hpp"

#include "checked.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <numeric>

namespace neura::lift {
namespace {

bool within_bits(const Interval& interval) { return interval.lo >= 0 && interval.hi <= 1; }

// Checked k * [lo, hi].
std::optional<Interval> scale(std::int64_t k, const Interval& interval) {
    std::int64_t a = 0;
    std::int64_t b = 0;
    if (!checked_multiply(k, interval.lo, a) || !checked_multiply(k, interval.hi, b)) return std::nullopt;
    return Interval{std::min(a, b), std::max(a, b)};
}

class Analysis {
public:
    Analysis(const nvr::Graph& graph, OverflowProof& proof, std::uint64_t* budget = nullptr)
        : graph_(graph), proof_(proof), values_(std::size_t{graph.input_lanes} + graph.nodes.size(), 0),
          budget_(budget) {
        if (proof_.group_of.size() != values_.size()) proof_.group_of.assign(values_.size(), nvr::absent);
    }

    const nvr::Node& node(std::uint32_t wire) const { return graph_.nodes[wire - graph_.input_lanes]; }

    // Whether the case analysis ran out of the caller's work.
    bool exhausted() const { return exhausted_; }

    // Step pairs: ReLU nodes P = relu(L + b) and Q = relu(L + b - 1) over the
    // same weighted sum L. For integer L, P - Q is 1 when L + b >= 1 and 0
    // otherwise: a group (P, +1), (Q, -1) with range [0, 1]. The circuit
    // library's threshold gadget (at_least) has this form; carries and
    // multiplier digits are such differences.
    void find_step_pairs() {
        const auto inputs = graph_.input_lanes;
        std::map<std::vector<std::pair<std::uint32_t, std::int64_t>>, std::vector<std::uint32_t>> by_edges;
        for (std::uint32_t n = 0; n < graph_.nodes.size(); ++n) {
            const auto& current = graph_.nodes[n];
            if (current.activation != nvr::Activation::Relu || current.edges.empty()) continue;
            std::vector<std::pair<std::uint32_t, std::int64_t>> key;
            key.reserve(current.edges.size());
            for (const auto& edge : current.edges) key.emplace_back(edge.source, edge.weight);
            auto& same = by_edges[std::move(key)];
            const auto wire = inputs + n;
            for (const auto other : same) {
                if (proof_.group_of[other] != nvr::absent) continue;
                const auto other_bias = node(other).bias;
                std::uint32_t upper = nvr::absent;
                std::uint32_t lower = nvr::absent;
                if (current.bias != std::numeric_limits<std::int64_t>::max() && other_bias == current.bias + 1) {
                    upper = other;
                    lower = wire;
                } else if (current.bias != std::numeric_limits<std::int64_t>::min() && other_bias == current.bias - 1) {
                    upper = wire;
                    lower = other;
                }
                if (upper != nvr::absent) {
                    add_group({{upper, 1}, {lower, -1}}, {0, 1});
                    break;
                }
            }
            same.push_back(wire);
        }
    }

    void add_group(std::vector<std::pair<std::uint32_t, std::int64_t>> members, Interval range) {
        const auto id = static_cast<std::uint32_t>(proof_.groups.size());
        for (const auto& member : members) proof_.group_of[member.first] = id;
        proof_.groups.push_back({std::move(members), range});
    }

    // The factor k with which `reader` reads every member of group `g` as k *
    // coefficient, 0 when it reads none of them, or nothing when it reads
    // them otherwise.
    std::optional<std::int64_t> factor(const nvr::Node& reader, std::uint32_t g) const {
        const auto& members = proof_.groups[g].members;
        std::optional<std::int64_t> k;
        std::size_t found = 0;
        for (const auto& edge : reader.edges) {
            if (proof_.group_of[edge.source] != g) continue;
            ++found;
            const auto it = std::find_if(members.begin(), members.end(),
                                         [&](const auto& m) { return m.first == edge.source; });
            if (edge.weight % it->second != 0) return std::nullopt;
            const auto ratio = edge.weight / it->second;
            if (k && *k != ratio) return std::nullopt;
            k = ratio;
        }
        if (found == 0) return std::int64_t{0};
        if (found != members.size()) return std::nullopt;
        return k;
    }

    // A node's final sum, reading each group it reads whole as k * range.
    std::optional<Interval> grouped_sum(const nvr::Node& reader) const {
        std::int64_t lo = reader.bias;
        std::int64_t hi = reader.bias;
        std::set<std::uint32_t> done;
        for (const auto& edge : reader.edges) {
            const auto g = proof_.group_of[edge.source];
            std::optional<Interval> term;
            if (g != nvr::absent) {
                if (done.contains(g)) continue;
                if (const auto k = factor(reader, g)) {
                    done.insert(g);
                    term = scale(*k, proof_.groups[g].range);
                    if (!term) return std::nullopt;
                }
            }
            if (!term) term = scale(edge.weight, proof_.wires[edge.source]);
            if (!term || !checked_add(lo, term->lo, lo) || !checked_add(hi, term->hi, hi)) return std::nullopt;
        }
        return Interval{lo, hi};
    }

    // Bounded case analysis: the range of `top`'s value (a node, or a
    // virtual node summing wires), computed by choosing a cut of wires below
    // it, treating each cut wire as free over its interval, and evaluating
    // the nodes between the cut and `top` for every assignment. A real
    // execution gives the cut one of these assignments, so `top`'s value on
    // every input lies within the range found.
    //
    // The terms of `top`'s sum are split into groups that share no free
    // variable; each is enumerated alone and the ranges are added, which is
    // sound for any sum (an equality test of 64 lanes is 64 groups of two
    // variables, not one of 128). A group of terms with too many assignments
    // is split by conditioning on the variable most of its terms read.
    //
    // A wire group (a step pair, or two exclusive halves of a selection)
    // whose members every node of the cone reads in proportion is one free
    // variable over the group's range: its first member, whose coefficient
    // is 1, takes the value and the others 0, which gives every such reader
    // the right sum. A cut neuron whose sources are all in the cut or
    // computed is computed instead of left free, and a free neuron can be
    // replaced by its sources; both only remove assignments, so every range
    // found is sound and their intersection is kept.
    std::optional<Interval> range(const nvr::Node& top, std::uint64_t budget, int rounds) {
        work_ = budget * 256;
        const auto inputs = graph_.input_lanes;
        std::set<std::uint32_t> cut;
        std::set<std::uint32_t> computed;
        const auto expand = [&](std::uint32_t wire) {
            cut.erase(wire);
            computed.insert(wire);
            for (const auto& edge : node(wire).edges) {
                if (!computed.contains(edge.source)) cut.insert(edge.source);
            }
        };
        for (const auto& edge : top.edges) cut.insert(edge.source);
        std::optional<Interval> best;
        for (int round = 0; round < rounds; ++round) {
            // A neuron computed from cut and computed wires alone is not free.
            for (bool changed = true; changed;) {
                changed = false;
                for (const auto wire : cut) {
                    if (wire >= inputs && !node(wire).edges.empty() &&
                        std::all_of(node(wire).edges.begin(), node(wire).edges.end(), [&](const nvr::Edge& e) {
                            return cut.contains(e.source) || computed.contains(e.source);
                        })) {
                        cut.erase(wire);
                        computed.insert(wire);
                        changed = true;
                        break;
                    }
                }
            }
            // The nodes between the cut and `top`, in stored order.
            std::set<std::uint32_t> cone;
            std::vector<std::uint32_t> pending;
            for (const auto& edge : top.edges) pending.push_back(edge.source);
            while (!pending.empty()) {
                const auto wire = pending.back();
                pending.pop_back();
                if (cut.contains(wire) || cone.contains(wire)) continue;
                cone.insert(wire);
                for (const auto& edge : node(wire).edges) pending.push_back(edge.source);
            }
            // Free variables: whole wire groups, and other wires over their
            // intervals, which must be small.
            std::vector<Variable> variables;
            std::map<std::uint32_t, std::size_t> variable_of;
            std::optional<std::uint32_t> too_wide;
            for (const auto wire : cut) {
                if (variable_of.contains(wire)) continue;
                const auto g = proof_.group_of[wire];
                if (g != nvr::absent && whole_group(g, cut, cone, top)) {
                    const auto& group = proof_.groups[g];
                    for (const auto& member : group.members) variable_of[member.first] = variables.size();
                    variables.push_back({group.members.front().first, g, group.range.lo, group.range.hi});
                    continue;
                }
                const auto& interval = proof_.wires[wire];
                if (interval.hi - interval.lo > 255 || interval.hi < interval.lo) {
                    too_wide = wire;
                    break;
                }
                variable_of[wire] = variables.size();
                variables.push_back({wire, nvr::absent, interval.lo, interval.hi});
            }
            if (too_wide) {
                if (*too_wide < inputs) return best;
                expand(*too_wide);
                continue;
            }
            std::map<std::uint32_t, std::vector<std::size_t>> depends;
            for (const auto wire : cone) {
                std::set<std::size_t> found;
                for (const auto& edge : node(wire).edges) {
                    if (const auto v = variable_of.find(edge.source); v != variable_of.end()) {
                        found.insert(v->second);
                    } else {
                        const auto& d = depends.at(edge.source);
                        found.insert(d.begin(), d.end());
                    }
                }
                depends[wire].assign(found.begin(), found.end());
            }
            std::vector<std::vector<std::size_t>> term_variables;
            for (const auto& edge : top.edges) {
                if (const auto v = variable_of.find(edge.source); v != variable_of.end()) {
                    term_variables.push_back({v->second});
                } else {
                    term_variables.push_back(depends.at(edge.source));
                }
            }
            std::vector<std::size_t> all_terms(top.edges.size());
            for (std::size_t k = 0; k < all_terms.size(); ++k) all_terms[k] = k;
            std::vector<std::uint8_t> fixed(variables.size(), 0);
            const auto sum = sum_range(top, all_terms, term_variables, variables, fixed, cone, budget, 0);
            std::int64_t lo = top.bias;
            std::int64_t hi = top.bias;
            if (!sum || !checked_add(lo, sum->lo, lo) || !checked_add(hi, sum->hi, hi)) return best;
            if (top.activation == nvr::Activation::Relu) {
                lo = std::max<std::int64_t>(lo, 0);
                hi = std::max<std::int64_t>(hi, 0);
            }
            best = best ? Interval{std::max(best->lo, lo), std::min(best->hi, hi)} : Interval{lo, hi};
            if (within_bits(*best) || best->hi - best->lo <= 0) return best;
            // Refine: expand the deepest free neuron, every member of a group.
            const auto deepest = std::find_if(variables.rbegin(), variables.rend(),
                                              [&](const Variable& v) { return v.wire >= inputs; });
            if (deepest == variables.rend()) return best;
            if (deepest->group != nvr::absent) {
                for (const auto& member : proof_.groups[deepest->group].members) expand(member.first);
            } else {
                expand(deepest->wire);
            }
        }
        return best;
    }

    // Exact elimination along the cone (docs/runtime/NVM.md, "Execution
    // tiers"): the range of `top`'s value over every assignment of a cut of
    // free wires below it, like range(), but computed by dynamic programming
    // instead of by enumerating the cut's assignments together. The cone's
    // nodes are visited in stored order, which is topological. A state
    // assigns a value to each wire of the frontier: the free variables
    // introduced so far and the computed nodes that a later node still
    // reads. Each state also carries the range of the part of `top`'s sum
    // whose terms are already known. A free variable is introduced just
    // before its first reader, branching every state over its range; a node
    // is computed in each state from its sources' values; a wire leaves the
    // frontier after its last reader, and states that then agree on the
    // frontier merge, keeping the union of their ranges. Merging forgets only
    // which assignment produced a partial sum, never a reachable value, so
    // the final range covers exactly the values of `top` over the cut's
    // assignments, as enumeration would find; the difference is the cost,
    // which grows with the frontier rather than with the cut. A carry chain
    // keeps a frontier of one carry, where enumeration needs every bit below
    // it.
    //
    // The cut starts at `top`'s sources and moves down one level per round,
    // every free neuron being replaced by its sources; a neuron whose
    // sources are all cut or computed is computed. Free variables are whole
    // wire groups and wires whose interval is at most 256 values wide, as in
    // range(). Each round's range is sound, so their intersection is kept.
    // The analysis stops when the range is within [0, 1], or after `rounds`
    // rounds, or when the states exceed `max_states`: an answer within the
    // work and the state bound is the same answer, so neither bound decides
    // what is proven, only whether.
    std::optional<Interval> exact_range(const nvr::Node& top, std::size_t max_states, int rounds) {
        work_ = static_cast<std::uint64_t>(max_states) * 4096;
        const auto inputs = graph_.input_lanes;
        // Wire marks, reset on return: 1 cut, 2 computed. Every computed
        // wire is reachable from `top` through computed wires (the cut only
        // moves down), so the cone is the computed wires.
        auto& mark = marks_;
        if (mark.size() != values_.size()) mark.assign(values_.size(), 0);
        if (variable_of_.size() != values_.size()) variable_of_.assign(values_.size(), -1);
        std::vector<std::uint32_t> touched;
        struct Reset {
            std::vector<std::uint8_t>& mark;
            const std::vector<std::uint32_t>& touched;
            ~Reset() {
                for (const auto wire : touched) mark[wire] = 0;
            }
        } reset{mark, touched};
        std::vector<std::uint32_t> cut;
        std::vector<std::uint32_t> cone;
        for (const auto& edge : top.edges) {
            if (mark[edge.source] == 0) {
                mark[edge.source] = 1;
                touched.push_back(edge.source);
                cut.push_back(edge.source);
            }
        }
        std::optional<Interval> best;
        std::vector<Variable> variables;
        std::vector<std::uint32_t> groups;
        std::vector<std::uint8_t> group_ok;
        for (int round = 0; round < rounds; ++round) {
            // A neuron computed from cut and computed wires alone is not
            // free. Moving a wire from the cut to the computed set changes no
            // other wire's condition, so one pass finds them all.
            std::sort(cut.begin(), cut.end());
            std::vector<std::uint32_t> remaining;
            for (const auto wire : cut) {
                if (wire >= inputs && !node(wire).edges.empty() &&
                    std::all_of(node(wire).edges.begin(), node(wire).edges.end(),
                                [&](const nvr::Edge& e) { return mark[e.source] != 0; })) {
                    mark[wire] = 2;
                    cone.push_back(wire);
                } else {
                    remaining.push_back(wire);
                }
            }
            cut = std::move(remaining);
            std::sort(cone.begin(), cone.end());
            // Groups that may be one variable: every member in the cut, and
            // `top` and every cone node reading them in proportion.
            groups.clear();
            for (const auto wire : cut) {
                const auto g = proof_.group_of[wire];
                if (g != nvr::absent) groups.push_back(g);
            }
            std::sort(groups.begin(), groups.end());
            groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
            group_ok.assign(groups.size(), 1);
            const auto group_index = [&](std::uint32_t g) {
                return static_cast<std::size_t>(std::lower_bound(groups.begin(), groups.end(), g) - groups.begin());
            };
            for (std::size_t i = 0; i < groups.size(); ++i) {
                const auto& members = proof_.groups[groups[i]].members;
                if (members.front().second != 1 ||
                    !std::all_of(members.begin(), members.end(), [&](const auto& m) { return mark[m.first] == 1; }) ||
                    !factor(top, groups[i])) {
                    group_ok[i] = 0;
                }
            }
            for (const auto wire : cone) {
                const auto& reader = node(wire);
                for (const auto& edge : reader.edges) {
                    const auto g = proof_.group_of[edge.source];
                    if (g == nvr::absent || mark[edge.source] != 1) continue;
                    const auto i = group_index(g);
                    if (i < groups.size() && groups[i] == g && group_ok[i] && !factor(reader, g)) group_ok[i] = 0;
                }
            }
            variables.clear();
            bool usable = true;
            for (const auto wire : cut) {
                if (variable_of_[wire] >= 0) continue;
                const auto g = proof_.group_of[wire];
                if (g != nvr::absent) {
                    const auto i = group_index(g);
                    if (group_ok[i]) {
                        const auto& group = proof_.groups[g];
                        for (const auto& member : group.members) {
                            variable_of_[member.first] = static_cast<std::int32_t>(variables.size());
                        }
                        variables.push_back({group.members.front().first, g, group.range.lo, group.range.hi});
                        continue;
                    }
                }
                const auto& interval = proof_.wires[wire];
                if (interval.hi - interval.lo > 255 || interval.hi < interval.lo) {
                    usable = false;
                    break;
                }
                variable_of_[wire] = static_cast<std::int32_t>(variables.size());
                variables.push_back({wire, nvr::absent, interval.lo, interval.hi});
            }
            std::optional<Interval> found;
            if (usable) found = eliminate(top, variables, cone, max_states);
            for (const auto wire : cut) variable_of_[wire] = -1;
            if (exhausted_) return best;
            if (found) {
                std::int64_t lo = top.bias;
                std::int64_t hi = top.bias;
                if (checked_add(lo, found->lo, lo) && checked_add(hi, found->hi, hi)) {
                    if (top.activation == nvr::Activation::Relu) {
                        lo = std::max<std::int64_t>(lo, 0);
                        hi = std::max<std::int64_t>(hi, 0);
                    }
                    best = best ? Interval{std::max(best->lo, lo), std::min(best->hi, hi)} : Interval{lo, hi};
                    if (within_bits(*best) || best->hi - best->lo <= 0) return best;
                }
            }
            // One level down: every free neuron is replaced by its sources.
            std::vector<std::uint32_t> next;
            bool expanded = false;
            for (const auto wire : cut) {
                if (wire < inputs) {
                    next.push_back(wire);
                    continue;
                }
                expanded = true;
                mark[wire] = 2;
                cone.push_back(wire);
            }
            if (!expanded) return best;
            for (const auto wire : cut) {
                if (wire < inputs) continue;
                for (const auto& edge : node(wire).edges) {
                    if (mark[edge.source] == 0) {
                        mark[edge.source] = 1;
                        touched.push_back(edge.source);
                        next.push_back(edge.source);
                    }
                }
            }
            cut = std::move(next);
        }
        return best;
    }

private:
    struct Variable {
        // The wire that carries the value: the wire, or a group's first member.
        std::uint32_t wire;
        std::uint32_t group = nvr::absent;
        std::int64_t lo = 0;
        std::int64_t hi = 0;
    };

    // The range of the sum of `top`'s weighted terms (without its bias) over
    // every assignment of `variables`, by elimination along the cone `order`
    // (ascending; see exact_range). variable_of_ maps each cut wire to its
    // variable. Nothing when the states exceed `max_states`, when the
    // arithmetic leaves 64 bits, or when the work runs out (exhausted_ when
    // the caller's work is spent).
    //
    // States are rows of `width` values, one per frontier key, with the
    // range of the known part of `top`'s sum beside each. Rows stay distinct
    // while a node's sum accumulates (a column changes by an amount the
    // other columns determine); they can coincide when a column is dropped
    // or a ReLU clamps, and are merged then.
    std::optional<Interval> eliminate(const nvr::Node& top, const std::vector<Variable>& variables,
                                      const std::vector<std::uint32_t>& order, std::size_t max_states) {
        constexpr std::int64_t none = -1;
        const auto end = static_cast<std::int64_t>(order.size());
        // Keys: a cone wire w is w; variable v is -(v + 1).
        const auto key_of = [&](std::uint32_t wire) -> std::int64_t {
            const auto v = variable_of_[wire];
            return v >= 0 ? -static_cast<std::int64_t>(v) - 1 : static_cast<std::int64_t>(wire);
        };
        std::map<std::int64_t, std::int64_t> last_use;
        for (std::int64_t pos = 0; pos < end; ++pos) {
            for (const auto& edge : node(order[static_cast<std::size_t>(pos)]).edges) {
                last_use[key_of(edge.source)] = pos;
            }
        }
        const auto last = [&](std::int64_t key) {
            const auto it = last_use.find(key);
            return it == last_use.end() ? none : it->second;
        };
        // `top`'s terms by key.
        std::map<std::int64_t, std::vector<std::pair<std::uint32_t, std::int64_t>>> top_terms;
        for (const auto& edge : top.edges) top_terms[key_of(edge.source)].emplace_back(edge.source, edge.weight);
        const auto contribution = [&](std::int64_t key, std::int64_t value, std::int64_t& out) {
            out = 0;
            const auto it = top_terms.find(key);
            if (it == top_terms.end()) return true;
            for (const auto& [wire, weight] : it->second) {
                std::int64_t actual = value;
                if (key < 0) {
                    const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
                    if (variable.group != nvr::absent && wire != variable.wire) actual = 0;
                }
                std::int64_t product = 0;
                if (!checked_multiply(actual, weight, product) || !checked_add(out, product, out)) return false;
            }
            return true;
        };
        // Terms over variables no cone node reads are independent of the
        // rest and add their ranges directly.
        Interval independent{0, 0};
        for (const auto& [key, terms] : top_terms) {
            if (key >= 0 || last(key) != none) continue;
            const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
            std::int64_t lo = std::numeric_limits<std::int64_t>::max();
            std::int64_t hi = std::numeric_limits<std::int64_t>::min();
            for (auto value = variable.lo; value <= variable.hi; ++value) {
                std::int64_t part = 0;
                if (!contribution(key, value, part)) return std::nullopt;
                lo = std::min(lo, part);
                hi = std::max(hi, part);
            }
            if (!checked_add(independent.lo, lo, independent.lo) || !checked_add(independent.hi, hi, independent.hi)) {
                return std::nullopt;
            }
        }

        std::vector<std::int64_t> frontier;
        std::vector<std::int64_t> rows;
        std::vector<Interval> sums{Interval{0, 0}};
        std::size_t width = 0;
        const auto count = [&]() { return sums.size(); };
        const auto slot_of = [&](std::int64_t key) -> std::size_t {
            for (std::size_t i = 0; i < frontier.size(); ++i) {
                if (frontier[i] == key) return i;
            }
            return frontier.size();
        };
        const auto charge = [&](std::uint64_t units) {
            if (work_ < units) return false;
            work_ -= units;
            if (budget_) {
                if (*budget_ < units) {
                    *budget_ = 0;
                    exhausted_ = true;
                    return false;
                }
                *budget_ -= units;
            }
            return true;
        };
        // Sorts the rows and merges equal ones, uniting their ranges.
        std::vector<std::size_t> index;
        const auto merge = [&]() {
            const auto n = count();
            if (n < 2) return;
            index.resize(n);
            std::iota(index.begin(), index.end(), std::size_t{0});
            const auto row = [&](std::size_t i) { return rows.data() + i * width; };
            std::sort(index.begin(), index.end(), [&](std::size_t a, std::size_t b) {
                return std::lexicographical_compare(row(a), row(a) + width, row(b), row(b) + width);
            });
            std::vector<std::int64_t> merged_rows;
            std::vector<Interval> merged_sums;
            merged_rows.reserve(rows.size());
            merged_sums.reserve(n);
            for (std::size_t k = 0; k < n; ++k) {
                const auto i = index[k];
                if (!merged_sums.empty() &&
                    std::equal(row(i), row(i) + width, merged_rows.end() - static_cast<std::ptrdiff_t>(width))) {
                    auto& range = merged_sums.back();
                    range.lo = std::min(range.lo, sums[i].lo);
                    range.hi = std::max(range.hi, sums[i].hi);
                    continue;
                }
                merged_rows.insert(merged_rows.end(), row(i), row(i) + width);
                merged_sums.push_back(sums[i]);
            }
            rows = std::move(merged_rows);
            sums = std::move(merged_sums);
        };
        // Drops column `column` and merges.
        const auto drop = [&](std::size_t column) {
            const auto n = count();
            std::vector<std::int64_t> kept;
            kept.reserve(n * (width - 1));
            for (std::size_t i = 0; i < n; ++i) {
                for (std::size_t c = 0; c < width; ++c) {
                    if (c != column) kept.push_back(rows[i * width + c]);
                }
            }
            rows = std::move(kept);
            frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(column));
            --width;
            merge();
        };
        // Appends a column: each row repeated once per value of `variable`,
        // or (no variable) one value for every row.
        const auto introduce = [&](std::int64_t key) -> bool {
            const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
            const auto values = static_cast<std::uint64_t>(variable.hi - variable.lo + 1);
            const auto n = count();
            if (n * values > max_states || !charge(n * values)) return false;
            std::vector<std::int64_t> grown;
            std::vector<Interval> grown_sums;
            grown.reserve(n * values * (width + 1));
            grown_sums.reserve(n * values);
            for (std::size_t i = 0; i < n; ++i) {
                for (auto value = variable.lo; value <= variable.hi; ++value) {
                    std::int64_t part = 0;
                    Interval range = sums[i];
                    if (!contribution(key, value, part) || !checked_add(range.lo, part, range.lo) ||
                        !checked_add(range.hi, part, range.hi)) {
                        return false;
                    }
                    grown.insert(grown.end(), rows.begin() + static_cast<std::ptrdiff_t>(i * width),
                                 rows.begin() + static_cast<std::ptrdiff_t>((i + 1) * width));
                    grown.push_back(value);
                    grown_sums.push_back(range);
                }
            }
            rows = std::move(grown);
            sums = std::move(grown_sums);
            frontier.push_back(key);
            ++width;
            return true;
        };
        const auto append_constant = [&](std::int64_t key, std::int64_t value) {
            const auto n = count();
            std::vector<std::int64_t> grown;
            grown.reserve(n * (width + 1));
            for (std::size_t i = 0; i < n; ++i) {
                grown.insert(grown.end(), rows.begin() + static_cast<std::ptrdiff_t>(i * width),
                             rows.begin() + static_cast<std::ptrdiff_t>((i + 1) * width));
                grown.push_back(value);
            }
            rows = std::move(grown);
            frontier.push_back(key);
            ++width;
        };

        // The running sum's key while a node is computed.
        constexpr std::int64_t running = std::numeric_limits<std::int64_t>::min();
        struct Read {
            std::size_t slot;
            std::int64_t weight;
        };
        std::vector<Read> reads;
        const auto apply = [&]() -> bool {
            if (reads.empty()) return true;
            const auto n = count();
            if (!charge(n * (reads.size() + 1))) return false;
            const auto sum_slot = slot_of(running);
            for (std::size_t i = 0; i < n; ++i) {
                auto* row = rows.data() + i * width;
                for (const auto& read : reads) {
                    std::int64_t product = 0;
                    if (!checked_multiply(row[read.slot], read.weight, product) ||
                        !checked_add(row[sum_slot], product, row[sum_slot])) {
                        return false;
                    }
                }
            }
            reads.clear();
            return true;
        };
        std::map<std::int64_t, std::size_t> last_edge;
        for (std::int64_t pos = 0; pos < end; ++pos) {
            const auto wire = order[static_cast<std::size_t>(pos)];
            const auto& n = node(wire);
            // The running sum starts at the bias. A free variable is
            // introduced at its first reader and, when this node is its last
            // and no later edge of it reads the variable, summed out at once,
            // so a node with many sources adds one column, not one per source.
            append_constant(running, n.bias);
            last_edge.clear();
            for (std::size_t e = 0; e < n.edges.size(); ++e) last_edge[key_of(n.edges[e].source)] = e;
            for (std::size_t e = 0; e < n.edges.size(); ++e) {
                const auto& edge = n.edges[e];
                const auto key = key_of(edge.source);
                auto slot = slot_of(key);
                if (slot == frontier.size()) {
                    // Only a free variable can be new here: a cone node is
                    // computed before its readers, and stays until its last.
                    if (key >= 0 || !apply() || !introduce(key)) return std::nullopt;
                    slot = frontier.size() - 1;
                }
                bool zero = false;
                if (key < 0) {
                    const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
                    // A group member other than the carrier is 0.
                    zero = variable.group != nvr::absent && edge.source != variable.wire;
                }
                if (!zero) reads.push_back({slot, edge.weight});
                if (last_edge.at(key) == e && last(key) == pos) {
                    if (!apply()) return std::nullopt;
                    drop(slot);
                }
            }
            if (!apply()) return std::nullopt;
            const auto states = count();
            if (!charge(states)) return std::nullopt;
            const auto key = static_cast<std::int64_t>(wire);
            const auto sum_slot = slot_of(running);
            bool clamped = false;
            for (std::size_t i = 0; i < states; ++i) {
                auto& value = rows[i * width + sum_slot];
                if (n.activation == nvr::Activation::Relu && value < 0) {
                    value = 0;
                    clamped = true;
                }
                std::int64_t part = 0;
                if (!contribution(key, value, part) || !checked_add(sums[i].lo, part, sums[i].lo) ||
                    !checked_add(sums[i].hi, part, sums[i].hi)) {
                    return std::nullopt;
                }
            }
            frontier[sum_slot] = key;
            if (last(key) > pos) {
                if (clamped) merge();
            } else {
                drop(sum_slot);
            }
            if (count() > max_states) return std::nullopt;
        }
        Interval total{std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min()};
        for (const auto& range : sums) {
            total.lo = std::min(total.lo, range.lo);
            total.hi = std::max(total.hi, range.hi);
        }
        if (!checked_add(total.lo, independent.lo, total.lo) || !checked_add(total.hi, independent.hi, total.hi)) {
            return std::nullopt;
        }
        return total;
    }

    // Whether group `g` lies whole in the cut and every reader reads it in
    // proportion.
    bool whole_group(std::uint32_t g, const std::set<std::uint32_t>& cut, const std::set<std::uint32_t>& cone,
                     const nvr::Node& top) const {
        const auto& members = proof_.groups[g].members;
        if (members.front().second != 1) return false;
        for (const auto& member : members) {
            if (!cut.contains(member.first)) return false;
        }
        if (!factor(top, g)) return false;
        for (const auto wire : cone) {
            if (!factor(node(wire), g)) return false;
        }
        return true;
    }

    void assign(const Variable& variable, std::int64_t value) {
        values_[variable.wire] = value;
        if (variable.group != nvr::absent) {
            for (const auto& member : proof_.groups[variable.group].members) {
                if (member.first != variable.wire) values_[member.first] = 0;
            }
        }
    }

    // The range of the sum of `top`'s terms `terms`, with the variables
    // marked in `fixed` held at their current values. Terms are split into
    // groups that share no free variable, whose ranges add up. A group with
    // too many assignments is split by conditioning on the variable most of
    // its terms read: the union over that variable's values of the group's
    // range.
    std::optional<Interval> sum_range(const nvr::Node& top, const std::vector<std::size_t>& terms,
                                      const std::vector<std::vector<std::size_t>>& term_variables,
                                      const std::vector<Variable>& variables, std::vector<std::uint8_t>& fixed,
                                      const std::set<std::uint32_t>& cone, std::uint64_t budget, int depth) {
        std::map<std::size_t, std::size_t> parent;
        const auto root = [&](std::size_t v) {
            while (parent.at(v) != v) v = parent[v] = parent.at(parent.at(v));
            return v;
        };
        for (const auto k : terms) {
            std::optional<std::size_t> first;
            for (const auto v : term_variables[k]) {
                if (fixed[v]) continue;
                parent.try_emplace(v, v);
                if (first) {
                    parent[root(v)] = root(*first);
                } else {
                    first = v;
                }
            }
        }
        std::map<std::size_t, std::vector<std::size_t>> groups;
        for (const auto k : terms) {
            const auto it = std::find_if(term_variables[k].begin(), term_variables[k].end(),
                                         [&](std::size_t v) { return !fixed[v]; });
            groups[it == term_variables[k].end() ? variables.size() : root(*it)].push_back(k);
        }
        Interval total{0, 0};
        for (const auto& [id, group] : groups) {
            std::vector<std::size_t> free;
            if (id < variables.size()) {
                for (const auto& entry : parent) {
                    if (root(entry.first) == id) free.push_back(entry.first);
                }
            }
            std::uint64_t assignments = 1;
            for (const auto v : free) {
                assignments *= static_cast<std::uint64_t>(variables[v].hi - variables[v].lo + 1);
                if (assignments > budget) break;
            }
            std::optional<Interval> found;
            if (assignments <= budget) {
                found = enumerate(top, group, free, variables, cone);
            } else if (depth < 8) {
                // Condition on the variable the most terms of the group read.
                std::map<std::size_t, std::size_t> reads;
                for (const auto k : group) {
                    for (const auto v : term_variables[k]) {
                        if (!fixed[v]) ++reads[v];
                    }
                }
                const auto pick = std::max_element(reads.begin(), reads.end(), [](const auto& x, const auto& y) {
                                      return x.second < y.second;
                                  })->first;
                fixed[pick] = 1;
                for (auto value = variables[pick].lo; value <= variables[pick].hi; ++value) {
                    assign(variables[pick], value);
                    const auto part = sum_range(top, group, term_variables, variables, fixed, cone, budget, depth + 1);
                    if (!part) {
                        found.reset();
                        break;
                    }
                    found = found ? Interval{std::min(found->lo, part->lo), std::max(found->hi, part->hi)} : *part;
                }
                fixed[pick] = 0;
            }
            if (!found || !checked_add(total.lo, found->lo, total.lo) || !checked_add(total.hi, found->hi, total.hi)) {
                return std::nullopt;
            }
        }
        return total;
    }

    // The range of the sum of `terms` over every assignment of `free`, with
    // other variables at their current values.
    std::optional<Interval> enumerate(const nvr::Node& top, const std::vector<std::size_t>& terms,
                                      const std::vector<std::size_t>& free, const std::vector<Variable>& variables,
                                      const std::set<std::uint32_t>& cone) {
        std::uint64_t assignments = 1;
        for (const auto v : free) assignments *= static_cast<std::uint64_t>(variables[v].hi - variables[v].lo + 1);
        // The cone nodes these terms read, in stored order.
        std::set<std::uint32_t> needed;
        std::vector<std::uint32_t> pending;
        for (const auto k : terms) pending.push_back(top.edges[k].source);
        while (!pending.empty()) {
            const auto wire = pending.back();
            pending.pop_back();
            if (!cone.contains(wire) || needed.contains(wire)) continue;
            needed.insert(wire);
            for (const auto& edge : node(wire).edges) pending.push_back(edge.source);
        }
        std::int64_t lo = std::numeric_limits<std::int64_t>::max();
        std::int64_t hi = std::numeric_limits<std::int64_t>::min();
        std::vector<std::int64_t> choice(free.size());
        for (std::size_t i = 0; i < free.size(); ++i) choice[i] = variables[free[i]].lo;
        // The same nodes in the same stored order, as a flat list: the loop
        // below visits them once per assignment.
        const std::vector<std::uint32_t> ordered(needed.begin(), needed.end());
        for (std::uint64_t count = 0; count < assignments; ++count) {
            if (work_ < ordered.size() + 1) return std::nullopt;
            work_ -= ordered.size() + 1;
            if (budget_) {
                if (*budget_ < ordered.size() + 1) {
                    *budget_ = 0;
                    exhausted_ = true;
                    return std::nullopt;
                }
                *budget_ -= ordered.size() + 1;
            }
            for (std::size_t i = 0; i < free.size(); ++i) assign(variables[free[i]], choice[i]);
            for (const auto wire : ordered) {
                const auto& n = node(wire);
                std::int64_t sum = n.bias;
                for (const auto& edge : n.edges) {
                    std::int64_t product = 0;
                    if (!checked_multiply(values_[edge.source], edge.weight, product) ||
                        !checked_add(sum, product, sum)) {
                        return std::nullopt;
                    }
                }
                values_[wire] = n.activation == nvr::Activation::Relu ? std::max<std::int64_t>(sum, 0) : sum;
            }
            std::int64_t sum = 0;
            for (const auto k : terms) {
                std::int64_t product = 0;
                if (!checked_multiply(values_[top.edges[k].source], top.edges[k].weight, product) ||
                    !checked_add(sum, product, sum)) {
                    return std::nullopt;
                }
            }
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
            for (std::size_t i = 0; i < free.size(); ++i) {
                if (++choice[i] <= variables[free[i]].hi) break;
                choice[i] = variables[free[i]].lo;
            }
        }
        return Interval{lo, hi};
    }

    const nvr::Graph& graph_;
    OverflowProof& proof_;
    std::vector<std::int64_t> values_;
    // exact_range's wire marks and variables, all clear between calls.
    std::vector<std::uint8_t> marks_;
    std::vector<std::int32_t> variable_of_;
    std::uint64_t work_ = 0;
    std::uint64_t* budget_ = nullptr;
    bool exhausted_ = false;
};

// A pattern neuron: a ReLU whose edges all have weight +1 or -1 from bit
// wires, with bias 1 - (number of +1 edges). It is 1 exactly when every +1
// source is 1 and every -1 source is 0, and 0 otherwise: any mismatch lowers
// the sum by at least one. The compiler's equality tests have this form.
// Returns the required value of each source wire.
std::optional<std::vector<std::pair<std::uint32_t, int>>> pattern(const nvr::Graph& graph, const OverflowProof& proof,
                                                                  std::uint32_t wire) {
    if (wire < graph.input_lanes) return std::nullopt;
    const auto& node = graph.nodes[wire - graph.input_lanes];
    if (node.activation != nvr::Activation::Relu || node.edges.empty()) return std::nullopt;
    std::vector<std::pair<std::uint32_t, int>> required;
    std::int64_t ones = 0;
    for (const auto& edge : node.edges) {
        if ((edge.weight != 1 && edge.weight != -1) || !within_bits(proof.wires[edge.source])) return std::nullopt;
        required.emplace_back(edge.source, edge.weight == 1 ? 1 : 0);
        ones += edge.weight == 1 ? 1 : 0;
    }
    if (node.bias != 1 - ones) return std::nullopt;
    return required;
}

bool conflict(const std::vector<std::pair<std::uint32_t, int>>& a, const std::vector<std::pair<std::uint32_t, int>>& b) {
    // Both are sorted by wire, as edges are.
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].first < b[j].first) {
            ++i;
        } else if (b[j].first < a[i].first) {
            ++j;
        } else {
            if (a[i].second != b[j].second) return true;
            ++i;
            ++j;
        }
    }
    return false;
}

// A node whose sources are pairwise exclusive pattern neurons takes the
// value bias, or bias + weight of the one source that is 1.
bool one_hot_bit(const nvr::Graph& graph, const OverflowProof& proof, std::uint32_t wire) {
    if (wire < graph.input_lanes) return false;
    const auto& node = graph.nodes[wire - graph.input_lanes];
    if (node.edges.empty() || node.edges.size() > 4096) return false;
    std::vector<std::vector<std::pair<std::uint32_t, int>>> patterns;
    for (const auto& edge : node.edges) {
        auto found = pattern(graph, proof, edge.source);
        if (!found) return false;
        patterns.push_back(std::move(*found));
    }
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        for (std::size_t j = i + 1; j < patterns.size(); ++j) {
            if (!conflict(patterns[i], patterns[j])) return false;
        }
    }
    const auto clamp = [&](std::int64_t value) {
        return node.activation == nvr::Activation::Relu ? std::max<std::int64_t>(value, 0) : value;
    };
    const auto bit = [](std::int64_t value) { return value == 0 || value == 1; };
    if (!bit(clamp(node.bias))) return false;
    for (const auto& edge : node.edges) {
        std::int64_t value = 0;
        if (!checked_add(node.bias, edge.weight, value) || !bit(clamp(value))) return false;
    }
    return true;
}

} // namespace

OverflowProof prove_no_overflow(const nvr::Graph& graph, bool refine, std::uint64_t* work,
                                const OverflowProof* resume) {
    const bool exact = refine;
    OverflowProof proof;
    // A refined analysis that ran out of work kept every node it settled:
    // continue from the first one it did not. Each node's result depends
    // only on the nodes before it, so the proof is the one an unbounded
    // run finds.
    const bool resuming = refine && resume && resume->exhausted &&
                          resume->wires.size() == std::size_t{graph.input_lanes} + resume->node &&
                          resume->group_of.size() == std::size_t{graph.input_lanes} + graph.nodes.size();
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        for (const auto& edge : graph.nodes[n].edges) {
            if (edge.source >= graph.input_lanes + n) {
                // Not a valid graph: a node reads itself or a later wire.
                proof.node = n;
                return proof;
            }
        }
    }
    std::size_t first = 0;
    if (resuming) {
        proof.wires = resume->wires;
        proof.groups = resume->groups;
        proof.group_of = resume->group_of;
        first = resume->node;
    }
    Analysis analysis(graph, proof, work);
    if (!resuming) analysis.find_step_pairs();
    auto& wires = proof.wires;
    wires.reserve(std::size_t{graph.input_lanes} + graph.nodes.size());
    for (std::uint32_t i = 0; !resuming && i < graph.input_lanes; ++i) {
        wires.push_back({0, 1});
    }
    for (std::size_t n = first; n < graph.nodes.size(); ++n) {
        const auto& node = graph.nodes[n];
        // Groups this node adds, undone if its analysis runs out of work.
        const auto groups_before = proof.groups.size();
        // The reference order: bias, then each edge in stored order, with
        // every product and every partial sum checked.
        std::int64_t lo = node.bias;
        std::int64_t hi = node.bias;
        for (const auto& edge : node.edges) {
            const auto term = scale(edge.weight, wires[edge.source]);
            // Every partial sum reachable at this step lies in [lo, hi], so
            // proving both ends in range proves every reachable sum is.
            if (!term || !checked_add(lo, term->lo, lo) || !checked_add(hi, term->hi, hi)) {
                proof.node = n;
                proof.wires.clear();
                return proof;
            }
        }
        // Two halves of a selection, relu(s + t - 1) and relu(f - s) on
        // adjacent wires, read with equal weights: when case analysis shows
        // their sum is 0 or 1, they become a group, read as one bit.
        for (std::size_t e = 0; refine && e + 1 < node.edges.size(); ++e) {
            const auto a = node.edges[e].source;
            const auto b = node.edges[e + 1].source;
            if (b != a + 1 || a < graph.input_lanes || node.edges[e].weight != node.edges[e + 1].weight ||
                proof.group_of[a] != nvr::absent || proof.group_of[b] != nvr::absent ||
                analysis.node(a).activation != nvr::Activation::Relu ||
                analysis.node(b).activation != nvr::Activation::Relu) {
                continue;
            }
            const nvr::Node sum{nvr::Activation::Identity, 0, {{a, 1}, {b, 1}}};
            auto range = analysis.range(sum, 256, 4);
            if (exact && !(range && within_bits(*range))) range = analysis.exact_range(sum, 256, 3);
            if (range && within_bits(*range)) {
                analysis.add_group({{a, 1}, {b, 1}}, *range);
            }
        }
        // The final sum once more, with each group read whole as k * its
        // range. Each bound is sound, so their intersection is.
        if (const auto grouped = analysis.grouped_sum(node)) {
            lo = std::max(lo, grouped->lo);
            hi = std::min(hi, grouped->hi);
        }
        if (node.activation == nvr::Activation::Relu) {
            lo = std::max<std::int64_t>(lo, 0);
            hi = std::max<std::int64_t>(hi, 0);
        }
        wires.push_back({lo, hi});
        // A wire whose interval is wider than a bit is often a bit whose
        // terms are correlated, such as a sum lane (a + b + c - 2 * carry).
        // Case analysis over the wires below it narrows the interval, which
        // keeps the intervals of later nodes, and their overflow checks,
        // from compounding the looseness.
        if (refine && !within_bits(wires.back()) && !node.edges.empty()) {
            if (const auto range = analysis.range(node, 256, 4)) {
                wires.back().lo = std::max(wires.back().lo, range->lo);
                wires.back().hi = std::min(wires.back().hi, range->hi);
            }
            if (exact && !within_bits(wires.back())) {
                if (const auto range = analysis.exact_range(node, 256, 3)) {
                    wires.back().lo = std::max(wires.back().lo, range->lo);
                    wires.back().hi = std::min(wires.back().hi, range->hi);
                }
            }
        }
        if (analysis.exhausted()) {
            // Keep the settled nodes for a resumed analysis; this one starts
            // over.
            proof.node = n;
            wires.pop_back();
            for (auto g = groups_before; g < proof.groups.size(); ++g) {
                for (const auto& member : proof.groups[g].members) proof.group_of[member.first] = nvr::absent;
            }
            proof.groups.resize(groups_before);
            proof.exhausted = true;
            return proof;
        }
    }
    proof.safe = true;
    return proof;
}

bool lanes_are_bits(const OverflowProof& proof, const nvr::Graph& graph, std::span<const std::uint32_t> lanes,
                    BitEvidence* evidence, bool use_cases, std::uint64_t* work) {
    if (!proof.safe) {
        return false;
    }
    OverflowProof copy;
    std::optional<Analysis> cases;
    for (const auto lane : lanes) {
        if (lane >= graph.outputs.size() || graph.outputs[lane] >= proof.wires.size()) {
            return false;
        }
        const auto wire = graph.outputs[lane];
        if (within_bits(proof.wires[wire])) {
            if (evidence) ++evidence->intervals;
            continue;
        }
        if (one_hot_bit(graph, proof, wire)) {
            if (evidence) ++evidence->one_hot;
            continue;
        }
        if (use_cases && wire >= graph.input_lanes) {
            if (!cases) {
                copy = proof;
                cases.emplace(graph, copy, work);
            }
            const auto& top = graph.nodes[wire - graph.input_lanes];
            const auto range = cases->range(top, std::uint64_t{1} << 14, 48);
            if (cases->exhausted()) return false;
            if (range && within_bits(*range)) {
                if (evidence) ++evidence->cases;
                continue;
            }
            const auto exact = cases->exact_range(top, std::size_t{1} << 12, 64);
            if (cases->exhausted()) return false;
            if (exact && within_bits(*exact)) {
                if (evidence) ++evidence->eliminated;
                continue;
            }
        }
        return false;
    }
    return true;
}

} // namespace neura::lift

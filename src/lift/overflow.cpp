#include "overflow.hpp"

#include "checked.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <set>

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

private:
    struct Variable {
        // The wire that carries the value: the wire, or a group's first member.
        std::uint32_t wire;
        std::uint32_t group = nvr::absent;
        std::int64_t lo = 0;
        std::int64_t hi = 0;
    };

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

OverflowProof prove_no_overflow(const nvr::Graph& graph, bool refine, std::uint64_t* work) {
    OverflowProof proof;
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        for (const auto& edge : graph.nodes[n].edges) {
            if (edge.source >= graph.input_lanes + n) {
                // Not a valid graph: a node reads itself or a later wire.
                proof.node = n;
                return proof;
            }
        }
    }
    Analysis analysis(graph, proof, work);
    analysis.find_step_pairs();
    auto& wires = proof.wires;
    wires.reserve(std::size_t{graph.input_lanes} + graph.nodes.size());
    for (std::uint32_t i = 0; i < graph.input_lanes; ++i) {
        wires.push_back({0, 1});
    }
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        const auto& node = graph.nodes[n];
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
            if (const auto range = analysis.range(sum, 256, 4); range && within_bits(*range)) {
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
        }
        if (analysis.exhausted()) {
            proof.node = n;
            proof.wires.clear();
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
            const auto range = cases->range(graph.nodes[wire - graph.input_lanes], std::uint64_t{1} << 14, 48);
            if (cases->exhausted()) return false;
            if (range && within_bits(*range)) {
                if (evidence) ++evidence->cases;
                continue;
            }
        }
        return false;
    }
    return true;
}

} // namespace neura::lift

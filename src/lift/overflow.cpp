#include "overflow.hpp"

#include "checked.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <bit>
#include <limits>
#include <map>
#include <numeric>
#include <optional>

namespace neura::lift {
namespace {

bool within_bits(const Interval& interval) { return interval.lo >= 0 && interval.hi <= 1; }

// range() from a cut the advice names: the enumeration it may make per
// group of terms before conditioning (and so its work), and its rounds.
constexpr std::uint64_t advice_budget = 256;
constexpr int advice_rounds = 2;

// checked_multiply and checked_add for the inner loops, which use the result
// only on success: the compiler builtins decide exactly what those decide,
// whether the true result is representable, without a division. On failure
// `result` is unspecified.
bool multiply_fits(std::int64_t a, std::int64_t b, std::int64_t& result) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(a, b, &result);
#else
    return checked_multiply(a, b, result);
#endif
}

bool add_fits(std::int64_t a, std::int64_t b, std::int64_t& result) {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_add_overflow(a, b, &result);
#else
    return checked_add(a, b, result);
#endif
}

// Checked k * [lo, hi].
std::optional<Interval> scale(std::int64_t k, const Interval& interval) {
    std::int64_t a = 0;
    std::int64_t b = 0;
    if (!checked_multiply(k, interval.lo, a) || !checked_multiply(k, interval.hi, b)) return std::nullopt;
    return Interval{std::min(a, b), std::max(a, b)};
}

class Analysis {
public:
    Analysis(const nvr::Graph& graph, OverflowProof& proof, std::uint64_t* budget = nullptr,
             const ProofAdvice* advice = nullptr, AdviceUse use = AdviceUse::none)
        : graph_(graph), proof_(proof), values_(std::size_t{graph.input_lanes} + graph.nodes.size(), 0),
          budget_(budget), advice_(advice), decisive_(use == AdviceUse::decisive) {
        if (proof_.group_of.size() != values_.size()) proof_.group_of.assign(values_.size(), nvr::absent);
        const auto wires = values_.size();
        cone_marks_.assign(wires, 0);
        start_marks_.assign(wires, 0);
        cone_position_.assign(wires, -1);
        variable_of_.assign(wires, -1);
        visited_.assign(wires, 0);
        free_index_.assign(wires, -1);
        needed_position_.assign(wires, -1);
        last_use_wire_.assign(wires, -1);
        top_key_wire_.assign(wires, -1);
        last_edge_wire_.assign(wires, -1);
        other_index_.assign(wires, -1);
    }

    const nvr::Node& node(std::uint32_t wire) const { return graph_.nodes[wire - graph_.input_lanes]; }

    // The wires the advice names as bounding the gadget instances of
    // `wires`: the instance's input signals, where a signal of one wire is
    // that wire and a signal summing several is replaced by the inputs of the
    // instances that built its wires, which a cut through the separate wires
    // would treat as independent. Nothing without advice, or when the advice
    // names a wire or instance the graph does not have, or nests too deep,
    // or names too many wires. The cut is only where range() starts; it
    // decides nothing by itself.
    std::optional<std::vector<std::uint32_t>> advice_cut(std::initializer_list<std::uint32_t> wires) const {
        if (!advice_) return std::nullopt;
        const auto& advice = *advice_;
        const auto count = static_cast<std::uint32_t>(values_.size());
        const auto inputs = graph_.input_lanes;
        if (advice.instance.size() != graph_.nodes.size()) return std::nullopt;
        std::vector<std::uint32_t> cut;
        const auto instance_of = [&](std::uint32_t wire) -> std::uint32_t {
            if (wire < inputs || wire >= count) return nvr::absent;
            const auto i = advice.instance[wire - inputs];
            return i < advice.inputs.size() ? i : nvr::absent;
        };
        bool valid = true;
        // Each instance adds its inputs once.
        std::vector<std::uint32_t> seen;
        const std::function<void(std::uint32_t, int)> add = [&](std::uint32_t instance, int depth) {
            if (std::find(seen.begin(), seen.end(), instance) != seen.end()) return;
            seen.push_back(instance);
            if (seen.size() > advice_instances) {
                valid = false;
                return;
            }
            for (const auto& signal : advice.inputs[instance]) {
                // A signal whose wires are exactly an established group is
                // one variable of range() already.
                const auto g =
                    signal.empty() || signal.front() >= count ? nvr::absent : proof_.group_of[signal.front()];
                const bool group = g != nvr::absent && proof_.groups[g].members.size() == signal.size() &&
                                   std::all_of(signal.begin(), signal.end(), [&](std::uint32_t wire) {
                                       return wire < count && proof_.group_of[wire] == g;
                                   });
                for (const auto wire : signal) {
                    if (wire >= count) {
                        valid = false;
                        return;
                    }
                    const auto inner = instance_of(wire);
                    if (signal.size() == 1 || group || inner == nvr::absent || depth >= advice_depth) {
                        if (std::find(cut.begin(), cut.end(), wire) == cut.end()) cut.push_back(wire);
                    } else {
                        add(inner, depth + 1);
                    }
                    if (!valid || cut.size() > advice_wires) {
                        valid = false;
                        return;
                    }
                }
            }
        };
        for (const auto wire : wires) {
            const auto instance = instance_of(wire);
            if (instance == nvr::absent) return std::nullopt;
            add(instance, 0);
            if (!valid) return std::nullopt;
        }
        std::sort(cut.begin(), cut.end());
        cut.erase(std::unique(cut.begin(), cut.end()), cut.end());
        return cut;
    }

    // Whether the advice names different gadget instances for wires a and
    // b: the halves of a selection come from one instance, so a pair from
    // two is not tried as a group. Without advice for both, they are tried.
    bool apart(std::uint32_t a, std::uint32_t b) const {
        if (!advice_ || !decisive_) return false;
        const auto& advice = *advice_;
        const auto inputs = graph_.input_lanes;
        if (advice.instance.size() != graph_.nodes.size() || a < inputs || b < inputs) return false;
        const auto x = advice.instance[a - inputs];
        const auto y = advice.instance[b - inputs];
        return x != nvr::absent && y != nvr::absent && x != y;
    }

    // The range of the virtual sum `sum` of wires a and b from the cut the
    // advice names for both, or nothing without one.
    std::optional<Interval> pair_range(const nvr::Node& sum, std::uint32_t a, std::uint32_t b) {
        const auto cut = advice_cut({a, b});
        if (!cut) return std::nullopt;
        return range(sum, advice_budget, decisive_ ? 1 : advice_rounds, &*cut);
    }

    // The kept result for key_ (with its hash), if any.
    const std::optional<Interval>* memo_find(std::uint64_t hash) const {
        if (memo_.empty()) return nullptr;
        const auto mask = memo_.size() - 1;
        for (auto slot = static_cast<std::size_t>(hash) & mask;; slot = (slot + 1) & mask) {
            const auto& entry = memo_[slot];
            if (entry.length == std::numeric_limits<std::uint32_t>::max()) return nullptr;
            if (entry.hash == hash && entry.length == key_.size() &&
                std::equal(key_.begin(), key_.end(), memo_keys_.begin() + entry.offset)) {
                return &entry.result;
            }
        }
    }

    // Keeps `result` for key_, while the kept keys fit max_enumerated_words.
    void memo_keep(std::uint64_t hash, std::optional<Interval> result) {
        if (memo_keys_.size() + key_.size() > max_enumerated_words) return;
        if (2 * (memo_count_ + 1) > memo_.size()) {
            std::vector<MemoEntry> grown(std::max<std::size_t>(64, 2 * memo_.size()));
            const auto mask = grown.size() - 1;
            for (const auto& entry : memo_) {
                if (entry.length == std::numeric_limits<std::uint32_t>::max()) continue;
                auto slot = static_cast<std::size_t>(entry.hash) & mask;
                while (grown[slot].length != std::numeric_limits<std::uint32_t>::max()) slot = (slot + 1) & mask;
                grown[slot] = entry;
            }
            memo_ = std::move(grown);
        }
        const auto mask = memo_.size() - 1;
        auto slot = static_cast<std::size_t>(hash) & mask;
        while (memo_[slot].length != std::numeric_limits<std::uint32_t>::max()) slot = (slot + 1) & mask;
        memo_[slot] = {hash, static_cast<std::uint32_t>(memo_keys_.size()), static_cast<std::uint32_t>(key_.size()),
                       result};
        memo_keys_.insert(memo_keys_.end(), key_.begin(), key_.end());
        ++memo_count_;
    }

    // Whether the advice's cuts stand without the search (prove_no_overflow).
    bool decisive() const { return decisive_; }

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
        auto& done = done_;
        done.clear();
        for (const auto& edge : reader.edges) {
            const auto g = proof_.group_of[edge.source];
            std::optional<Interval> term;
            if (g != nvr::absent) {
                if (std::find(done.begin(), done.end(), g) != done.end()) continue;
                if (const auto k = factor(reader, g)) {
                    done.push_back(g);
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
    //
    // With `start`, the cut starts at those wires instead of `top`'s
    // sources: every other wire between them and `top` is computed, and an
    // input lane reached on the way is cut. Any such cut bounds the cone, so
    // what is found is as sound as from any other; a start that leaves more
    // than `start_cone` wires to compute is not tried.
    std::optional<Interval> range(const nvr::Node& top, std::uint64_t budget, int rounds,
                                  const std::vector<std::uint32_t>* start = nullptr) {
        work_ = budget * 256;
        const auto inputs = graph_.input_lanes;
        // Wire marks, reset on return: 1 cut, 2 computed. Every computed
        // wire is reachable from `top` through computed wires (the cut only
        // moves down), so the cone below is the computed wires, which
        // enumerate() reads as cone_marks_ == 2.
        auto& mark = cone_marks_;
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
        const auto add_cut = [&](std::uint32_t wire) {
            if (mark[wire] != 0) return;
            mark[wire] = 1;
            touched.push_back(wire);
            cut.push_back(wire);
        };
        const auto expand = [&](std::uint32_t wire) {
            mark[wire] = 2;
            cone.push_back(wire);
            for (const auto& edge : node(wire).edges) add_cut(edge.source);
        };
        if (start) {
            for (const auto wire : *start) start_marks_[wire] = 1;
            std::vector<std::uint32_t> pending;
            for (const auto& edge : top.edges) pending.push_back(edge.source);
            bool too_large = false;
            while (!pending.empty()) {
                const auto wire = pending.back();
                pending.pop_back();
                if (mark[wire] != 0) continue;
                if (wire < inputs || start_marks_[wire] != 0) {
                    add_cut(wire);
                    continue;
                }
                if (cone.size() >= start_cone) {
                    too_large = true;
                    break;
                }
                mark[wire] = 2;
                touched.push_back(wire);
                cone.push_back(wire);
                for (const auto& edge : node(wire).edges) pending.push_back(edge.source);
            }
            for (const auto wire : *start) start_marks_[wire] = 0;
            if (too_large) return std::nullopt;
        } else {
            for (const auto& edge : top.edges) add_cut(edge.source);
        }
        std::optional<Interval> best;
        std::vector<Variable> variables;
        std::vector<std::uint32_t> groups;
        std::vector<std::uint64_t> depends;
        std::vector<std::vector<std::size_t>> term_variables;
        for (int round = 0; round < rounds; ++round) {
            // The cut in ascending order, without the wires expanded since.
            std::erase_if(cut, [&](std::uint32_t wire) { return mark[wire] != 1; });
            std::sort(cut.begin(), cut.end());
            // A neuron computed from cut and computed wires alone is not
            // free. Moving a wire from the cut to the computed set changes no
            // other wire's condition, so one pass finds them all.
            std::size_t remaining = 0;
            for (const auto wire : cut) {
                if (wire >= inputs && !node(wire).edges.empty() &&
                    std::all_of(node(wire).edges.begin(), node(wire).edges.end(),
                                [&](const nvr::Edge& e) { return mark[e.source] != 0; })) {
                    mark[wire] = 2;
                    cone.push_back(wire);
                } else {
                    cut[remaining++] = wire;
                }
            }
            cut.resize(remaining);
            std::sort(cone.begin(), cone.end());
            // Groups that may be one variable: every member in the cut, and
            // `top` and every cone node reading them in proportion. A node
            // that reads no member reads the group in proportion 0.
            whole_groups(top, cut, cone, groups);
            // Free variables: whole wire groups, and other wires over their
            // intervals, which must be small.
            variables.clear();
            std::optional<std::uint32_t> too_wide;
            for (const auto wire : cut) {
                if (variable_of_[wire] >= 0) continue;
                const auto g = proof_.group_of[wire];
                if (g != nvr::absent && group_slot_[g] == 1) {
                    const auto& group = proof_.groups[g];
                    for (const auto& member : group.members) {
                        variable_of_[member.first] = static_cast<std::int32_t>(variables.size());
                    }
                    variables.push_back({group.members.front().first, g, group.range.lo, group.range.hi});
                    continue;
                }
                const auto& interval = proof_.wires[wire];
                if (interval.hi - interval.lo > 255 || interval.hi < interval.lo) {
                    too_wide = wire;
                    break;
                }
                variable_of_[wire] = static_cast<std::int32_t>(variables.size());
                variables.push_back({wire, nvr::absent, interval.lo, interval.hi});
            }
            for (const auto g : groups) group_slot_[g] = -1;
            if (too_wide) {
                for (const auto wire : cut) variable_of_[wire] = -1;
                if (*too_wide < inputs) return best;
                expand(*too_wide);
                continue;
            }
            // The variables each cone node depends on, as a bit set per
            // node; the cone is in stored order, so a node's sources come
            // before it.
            const auto words = (variables.size() + 63) / 64;
            depends.assign(cone.size() * words, 0);
            for (std::size_t i = 0; i < cone.size(); ++i) {
                cone_position_[cone[i]] = static_cast<std::int32_t>(i);
                auto* found = depends.data() + i * words;
                for (const auto& edge : node(cone[i]).edges) {
                    if (const auto v = variable_of_[edge.source]; v >= 0) {
                        found[static_cast<std::size_t>(v) / 64] |= std::uint64_t{1} << (v % 64);
                    } else {
                        const auto* d = depends.data() + static_cast<std::size_t>(cone_position_[edge.source]) * words;
                        for (std::size_t w = 0; w < words; ++w) found[w] |= d[w];
                    }
                }
            }
            term_variables.resize(top.edges.size());
            for (std::size_t k = 0; k < top.edges.size(); ++k) {
                auto& list = term_variables[k];
                list.clear();
                const auto source = top.edges[k].source;
                if (const auto v = variable_of_[source]; v >= 0) {
                    list.push_back(static_cast<std::size_t>(v));
                    continue;
                }
                const auto* d = depends.data() + static_cast<std::size_t>(cone_position_[source]) * words;
                for (std::size_t w = 0; w < words; ++w) {
                    for (auto bits = d[w]; bits != 0; bits &= bits - 1) {
                        list.push_back(w * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
                    }
                }
            }
            for (const auto wire : cut) variable_of_[wire] = -1;
            std::vector<std::size_t> all_terms(top.edges.size());
            for (std::size_t k = 0; k < all_terms.size(); ++k) all_terms[k] = k;
            std::vector<std::uint8_t> fixed(variables.size(), 0);
            const auto sum = sum_range(top, all_terms, term_variables, variables, fixed, budget, 0);
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
        // Keys: a cone wire w is w; variable v is -(v + 1). Per-key tables
        // are indexed by wire or by variable, and reset on return.
        const auto key_of = [&](std::uint32_t wire) -> std::int64_t {
            const auto v = variable_of_[wire];
            return v >= 0 ? -static_cast<std::int64_t>(v) - 1 : static_cast<std::int64_t>(wire);
        };
        if (last_use_variable_.size() < variables.size()) last_use_variable_.resize(variables.size(), none);
        if (top_key_variable_.size() < variables.size()) top_key_variable_.resize(variables.size(), -1);
        if (last_edge_variable_.size() < variables.size()) last_edge_variable_.resize(variables.size(), -1);
        const auto last_use = [&](std::int64_t key) -> std::int64_t& {
            return key < 0 ? last_use_variable_[static_cast<std::size_t>(-key - 1)]
                           : last_use_wire_[static_cast<std::size_t>(key)];
        };
        const auto top_key = [&](std::int64_t key) -> std::int32_t& {
            return key < 0 ? top_key_variable_[static_cast<std::size_t>(-key - 1)]
                           : top_key_wire_[static_cast<std::size_t>(key)];
        };
        const auto last_edge = [&](std::int64_t key) -> std::int32_t& {
            return key < 0 ? last_edge_variable_[static_cast<std::size_t>(-key - 1)]
                           : last_edge_wire_[static_cast<std::size_t>(key)];
        };
        // `top`'s terms by key: top_keys_ lists the keys ascending, each
        // with its terms (wire, weight) in edge order in top_terms_.
        top_keys_.clear();
        top_terms_.clear();
        top_order_.clear();
        for (std::size_t e = 0; e < top.edges.size(); ++e) top_order_.emplace_back(key_of(top.edges[e].source), e);
        std::stable_sort(top_order_.begin(), top_order_.end(),
                         [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const auto& [key, e] : top_order_) {
            if (top_keys_.empty() || top_keys_.back().key != key) {
                top_key(key) = static_cast<std::int32_t>(top_keys_.size());
                const auto at = static_cast<std::uint32_t>(top_terms_.size());
                top_keys_.push_back({key, at, at});
            }
            top_terms_.emplace_back(top.edges[e].source, top.edges[e].weight);
            ++top_keys_.back().end;
        }
        for (std::int64_t pos = 0; pos < end; ++pos) {
            for (const auto& edge : node(order[static_cast<std::size_t>(pos)]).edges) {
                last_use(key_of(edge.source)) = pos;
            }
        }
        struct Reset {
            Analysis& a;
            const std::vector<std::uint32_t>& order;
            ~Reset() {
                for (const auto wire : order) {
                    for (const auto& edge : a.node(wire).edges) {
                        const auto v = a.variable_of_[edge.source];
                        if (v >= 0) {
                            a.last_use_variable_[static_cast<std::size_t>(v)] = none;
                        } else {
                            a.last_use_wire_[edge.source] = none;
                        }
                    }
                }
                for (const auto& entry : a.top_keys_) {
                    if (entry.key < 0) {
                        a.top_key_variable_[static_cast<std::size_t>(-entry.key - 1)] = -1;
                    } else {
                        a.top_key_wire_[static_cast<std::size_t>(entry.key)] = -1;
                    }
                }
            }
        } reset{*this, order};
        const auto last = [&](std::int64_t key) { return last_use(key); };
        const auto contribution = [&](std::int64_t key, std::int64_t value, std::int64_t& out) {
            out = 0;
            const auto index = top_key(key);
            if (index < 0) return true;
            const auto& entry = top_keys_[static_cast<std::size_t>(index)];
            for (auto t = entry.begin; t < entry.end; ++t) {
                const auto [wire, weight] = top_terms_[t];
                std::int64_t actual = value;
                if (key < 0) {
                    const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
                    if (variable.group != nvr::absent && wire != variable.wire) actual = 0;
                }
                std::int64_t product = 0;
                if (!multiply_fits(actual, weight, product) || !add_fits(out, product, out)) return false;
            }
            return true;
        };
        // Terms over variables no cone node reads are independent of the
        // rest and add their ranges directly, in ascending key order.
        Interval independent{0, 0};
        for (const auto& entry : top_keys_) {
            const auto key = entry.key;
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

        // The states: rows_ holds count() rows of `width` values,
        // state_sums_ the range beside each. The order of the rows decides nothing:
        // every step treats each row alike, and merging keeps one row per
        // distinct value, so only the set of rows and their ranges matter.
        auto& frontier = frontier_;
        frontier.clear();
        auto& rows = rows_;
        auto& sums = state_sums_;
        rows.clear();
        sums.assign(1, Interval{0, 0});
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
        // Copies the rows into rows_spare_ with column `skip` left out (none
        // to keep every column), keeping one row per distinct value with the
        // union of the ranges of the rows equal to it, and makes the result
        // the states. Equal rows are found by hashing.
        const auto rebuild = [&](std::size_t skip) {
            const auto n = count();
            const auto kept = skip < width ? width - 1 : width;
            const auto before = std::min(skip, width);
            auto& out = rows_spare_;
            auto& out_sums = sums_spare_;
            out.resize(n * kept);
            out_sums.clear();
            std::size_t capacity = 16;
            while (capacity < 2 * n) capacity *= 2;
            constexpr auto empty = std::numeric_limits<std::uint32_t>::max();
            table_.assign(capacity, empty);
            std::size_t distinct = 0;
            for (std::size_t i = 0; i < n; ++i) {
                // The row without the column, written where it goes if new.
                const auto* row = rows.data() + i * width;
                auto* target = out.data() + distinct * kept;
                std::copy_n(row, before, target);
                if (before < width) std::copy(row + before + 1, row + width, target + before);
                std::uint64_t hash = 0x9e3779b97f4a7c15ULL;
                for (std::size_t c = 0; c < kept; ++c) {
                    hash = (hash ^ static_cast<std::uint64_t>(target[c])) * 0xff51afd7ed558ccdULL;
                    hash ^= hash >> 32;
                }
                for (auto slot = static_cast<std::size_t>(hash) & (capacity - 1);; slot = (slot + 1) & (capacity - 1)) {
                    const auto index = table_[slot];
                    if (index == empty) {
                        table_[slot] = static_cast<std::uint32_t>(distinct);
                        out_sums.push_back(sums[i]);
                        ++distinct;
                        break;
                    }
                    const auto* other = out.data() + static_cast<std::size_t>(index) * kept;
                    if (std::equal(target, target + kept, other)) {
                        auto& range = out_sums[index];
                        range.lo = std::min(range.lo, sums[i].lo);
                        range.hi = std::max(range.hi, sums[i].hi);
                        break;
                    }
                }
            }
            out.resize(distinct * kept);
            std::swap(rows, out);
            std::swap(sums, out_sums);
        };
        // Merges equal rows, uniting their ranges.
        const auto merge = [&]() {
            if (count() < 2) return;
            rebuild(std::numeric_limits<std::size_t>::max());
        };
        // Drops column `column` and merges.
        const auto drop = [&](std::size_t column) {
            rebuild(column);
            frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(column));
            --width;
        };
        // Appends a column: each row repeated once per value of `variable`,
        // or (no variable) one value for every row.
        const auto introduce = [&](std::int64_t key) -> bool {
            const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
            const auto values = static_cast<std::uint64_t>(variable.hi - variable.lo + 1);
            const auto n = count();
            if (n * values > max_states || !charge(n * values)) return false;
            auto& grown = rows_spare_;
            auto& grown_sums = sums_spare_;
            grown.clear();
            grown_sums.clear();
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
            std::swap(rows, grown);
            std::swap(sums, grown_sums);
            frontier.push_back(key);
            ++width;
            return true;
        };
        const auto append_constant = [&](std::int64_t key, std::int64_t value) {
            const auto n = count();
            auto& grown = rows_spare_;
            grown.resize(n * (width + 1));
            for (std::size_t i = 0; i < n; ++i) {
                std::copy_n(rows.begin() + static_cast<std::ptrdiff_t>(i * width), width,
                            grown.begin() + static_cast<std::ptrdiff_t>(i * (width + 1)));
                grown[i * (width + 1) + width] = value;
            }
            std::swap(rows, grown);
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
                    if (!multiply_fits(row[read.slot], read.weight, product) ||
                        !add_fits(row[sum_slot], product, row[sum_slot])) {
                        return false;
                    }
                }
            }
            reads.clear();
            return true;
        };
        for (std::int64_t pos = 0; pos < end; ++pos) {
            const auto wire = order[static_cast<std::size_t>(pos)];
            const auto& n = node(wire);
            // The running sum starts at the bias. A free variable is
            // introduced at its first reader and, when this node is its last
            // and no later edge of it reads the variable, summed out at once,
            // so a node with many sources adds one column, not one per source.
            append_constant(running, n.bias);
            for (std::size_t e = 0; e < n.edges.size(); ++e) {
                last_edge(key_of(n.edges[e].source)) = static_cast<std::int32_t>(e);
            }
            bool failed = false;
            for (std::size_t e = 0; e < n.edges.size() && !failed; ++e) {
                const auto& edge = n.edges[e];
                const auto key = key_of(edge.source);
                auto slot = slot_of(key);
                if (slot == frontier.size()) {
                    // Only a free variable can be new here: a cone node is
                    // computed before its readers, and stays until its last.
                    if (key >= 0 || !apply() || !introduce(key)) {
                        failed = true;
                        break;
                    }
                    slot = frontier.size() - 1;
                }
                bool zero = false;
                if (key < 0) {
                    const auto& variable = variables[static_cast<std::size_t>(-key - 1)];
                    // A group member other than the carrier is 0.
                    zero = variable.group != nvr::absent && edge.source != variable.wire;
                }
                if (!zero) reads.push_back({slot, edge.weight});
                if (last_edge(key) == static_cast<std::int32_t>(e) && last(key) == pos) {
                    if (!apply()) {
                        failed = true;
                        break;
                    }
                    drop(slot);
                }
            }
            for (const auto& edge : n.edges) last_edge(key_of(edge.source)) = -1;
            if (failed || !apply()) return std::nullopt;
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

    // The groups with a member in range()'s cut, listed in `groups`, each
    // marked in group_slot_ 1 when it may be one free variable and 0 when
    // not; the caller resets the marks to -1. A group may be one variable
    // when its first coefficient is 1, every member is in the cut, and
    // `top` and every node of the cone read it in proportion. A node that
    // reads no member reads it in proportion 0, and a node that reads a
    // member of a group lying whole in the cut reads that member from the
    // cut, so only the cone's edges from cut wires need checking.
    void whole_groups(const nvr::Node& top, const std::vector<std::uint32_t>& cut,
                      const std::vector<std::uint32_t>& cone, std::vector<std::uint32_t>& groups) {
        if (group_slot_.size() < proof_.groups.size()) group_slot_.resize(proof_.groups.size(), -1);
        const auto& mark = cone_marks_;
        groups.clear();
        for (const auto wire : cut) {
            const auto g = proof_.group_of[wire];
            if (g == nvr::absent || group_slot_[g] != -1) continue;
            groups.push_back(g);
            const auto& members = proof_.groups[g].members;
            const bool whole =
                members.front().second == 1 &&
                std::all_of(members.begin(), members.end(), [&](const auto& m) { return mark[m.first] == 1; }) &&
                factor(top, g);
            group_slot_[g] = whole ? 1 : 0;
        }
        for (const auto wire : cone) {
            const auto& reader = node(wire);
            for (const auto& edge : reader.edges) {
                const auto g = proof_.group_of[edge.source];
                if (g == nvr::absent || mark[edge.source] != 1 || group_slot_[g] != 1) continue;
                if (!factor(reader, g)) group_slot_[g] = 0;
            }
        }
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
                                      std::uint64_t budget, int depth) {
        // Union-find over the free variables the terms read, identified by
        // their indices: each joins the set of its term's first free
        // variable. Each depth of the conditioning has its own buffers, all
        // reset on return.
        auto& s = sums_[static_cast<std::size_t>(depth)];
        constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
        if (s.parent.size() < variables.size()) s.parent.resize(variables.size(), none);
        s.touched.clear();
        struct Reset {
            SumScratch& s;
            ~Reset() {
                for (const auto v : s.touched) s.parent[v] = none;
            }
        } reset{s};
        const auto root = [&](std::size_t v) {
            while (s.parent[v] != v) {
                s.parent[v] = s.parent[s.parent[v]];
                v = s.parent[v];
            }
            return v;
        };
        for (const auto k : terms) {
            std::size_t first = none;
            for (const auto v : term_variables[k]) {
                if (fixed[v]) continue;
                if (s.parent[v] == none) {
                    s.parent[v] = v;
                    s.touched.push_back(v);
                }
                if (first != none) {
                    const auto joined = root(first);
                    s.parent[root(v)] = joined;
                } else {
                    first = v;
                }
            }
        }
        // The terms by the root of their first free variable, ascending, in
        // term order within a root; terms without one last.
        s.keyed.clear();
        for (const auto k : terms) {
            const auto it = std::find_if(term_variables[k].begin(), term_variables[k].end(),
                                         [&](std::size_t v) { return !fixed[v]; });
            s.keyed.emplace_back(it == term_variables[k].end() ? variables.size() : root(*it), k);
        }
        std::stable_sort(s.keyed.begin(), s.keyed.end(),
                         [](const auto& x, const auto& y) { return x.first < y.first; });
        // The free variables by root, ascending within a root.
        s.members.clear();
        std::sort(s.touched.begin(), s.touched.end());
        for (const auto v : s.touched) s.members.emplace_back(root(v), v);
        std::stable_sort(s.members.begin(), s.members.end(),
                         [](const auto& x, const auto& y) { return x.first < y.first; });
        Interval total{0, 0};
        std::size_t member = 0;
        for (std::size_t begin = 0; begin < s.keyed.size();) {
            const auto id = s.keyed[begin].first;
            auto end = begin;
            s.group.clear();
            for (; end < s.keyed.size() && s.keyed[end].first == id; ++end) s.group.push_back(s.keyed[end].second);
            begin = end;
            s.free.clear();
            while (member < s.members.size() && s.members[member].first < id) ++member;
            for (; member < s.members.size() && s.members[member].first == id; ++member) {
                s.free.push_back(s.members[member].second);
            }
            std::uint64_t assignments = 1;
            for (const auto v : s.free) {
                assignments *= static_cast<std::uint64_t>(variables[v].hi - variables[v].lo + 1);
                if (assignments > budget) break;
            }
            std::optional<Interval> found;
            if (assignments <= budget) {
                found = enumerate(top, s.group, s.free, variables);
            } else if (depth < max_depth) {
                // Condition on the variable the most terms of the group
                // read, the first such variable on a tie.
                if (s.reads.size() < variables.size()) s.reads.resize(variables.size(), 0);
                s.counted.clear();
                for (const auto k : s.group) {
                    for (const auto v : term_variables[k]) {
                        if (fixed[v]) continue;
                        if (s.reads[v]++ == 0) s.counted.push_back(v);
                    }
                }
                std::sort(s.counted.begin(), s.counted.end());
                auto pick = s.counted.front();
                for (const auto v : s.counted) {
                    if (s.reads[v] > s.reads[pick]) pick = v;
                }
                for (const auto v : s.counted) s.reads[v] = 0;
                fixed[pick] = 1;
                for (auto value = variables[pick].lo; value <= variables[pick].hi; ++value) {
                    assign(variables[pick], value);
                    const auto part = sum_range(top, s.group, term_variables, variables, fixed, budget, depth + 1);
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
    // other variables at their current values. The nodes between are those
    // of range()'s cone (cone_marks_ 2) that the terms read.
    //
    // Each assignment costs the work of evaluating every such node, as
    // charged to work_, which bounds this range() call and so decides what
    // it finds. The evaluation itself is incremental: the free variables
    // advance as an odometer, the first fastest, and after the first
    // assignment only the nodes that depend on a variable that changed are
    // evaluated again; every other node keeps the value it had, which is
    // the value evaluating it again would give, without overflow. The
    // caller's work (budget_) is charged for the nodes evaluated.
    //
    // Results are kept by a key that states the whole computation with the
    // wires renamed, so that isomorphic gadget instances (the cells of a
    // column multiplier, the steps of a long division) share one result.
    // A wire read is named by its role: the i-th evaluated node in stored
    // order, the f-th free variable, or the e-th other wire in the order
    // first read. The key lists the counts; each free variable's range;
    // each evaluated node's activation, bias and edges (name of the source,
    // weight) in stored order; the terms (name, weight) in order; and the
    // value of each other wire, which stays fixed during the enumeration (a
    // group member other than its carrier is 0). That is everything the
    // evaluations read, in the order they read it, with checked arithmetic
    // in the same order, so two equal keys describe the same evaluations up
    // to the renaming and give the same range or the same overflow; the
    // nominal work, (nodes + 1) per assignment, follows from the key too. A
    // result found again charges work_ what finding it charges and nothing
    // to budget_; one that ran out of work is not kept.
    std::optional<Interval> enumerate(const nvr::Node& top, const std::vector<std::size_t>& terms,
                                      const std::vector<std::size_t>& free, const std::vector<Variable>& variables) {
        std::uint64_t assignments = 1;
        for (const auto v : free) assignments *= static_cast<std::uint64_t>(variables[v].hi - variables[v].lo + 1);
        // The cone nodes these terms read, in stored order.
        if (++epoch_ == 0) {
            std::fill(visited_.begin(), visited_.end(), 0);
            epoch_ = 1;
        }
        auto& needed = needed_;
        needed.clear();
        pending_.clear();
        for (const auto k : terms) pending_.push_back(top.edges[k].source);
        while (!pending_.empty()) {
            const auto wire = pending_.back();
            pending_.pop_back();
            if (cone_marks_[wire] != 2 || visited_[wire] == epoch_) continue;
            visited_[wire] = epoch_;
            needed.push_back(wire);
            for (const auto& edge : node(wire).edges) pending_.push_back(edge.source);
        }
        std::sort(needed.begin(), needed.end());
        const auto per_assignment = static_cast<std::uint64_t>(needed.size()) + 1;
        auto& choice = choice_;
        choice.resize(free.size());
        for (std::size_t i = 0; i < free.size(); ++i) {
            choice[i] = variables[free[i]].lo;
            assign(variables[free[i]], choice[i]);
            free_index_[variables[free[i]].wire] = static_cast<std::int32_t>(i);
        }
        for (std::size_t i = 0; i < needed.size(); ++i) needed_position_[needed[i]] = static_cast<std::int32_t>(i);
        struct Reset {
            Analysis& a;
            const std::vector<std::size_t>& free;
            const std::vector<Variable>& variables;
            ~Reset() {
                for (const auto v : free) a.free_index_[variables[v].wire] = -1;
                for (const auto wire : a.needed_) a.needed_position_[wire] = -1;
            }
        } reset{*this, free, variables};
        // The key: what the evaluations below read, with wires renamed.
        auto& key = key_;
        key.clear();
        key.push_back(static_cast<std::int64_t>(needed.size()));
        key.push_back(static_cast<std::int64_t>(terms.size()));
        key.push_back(static_cast<std::int64_t>(free.size()));
        for (const auto v : free) {
            key.push_back(variables[v].lo);
            key.push_back(variables[v].hi);
        }
        auto& others = others_;
        others.clear();
        const auto name = [&](std::uint32_t wire) -> std::int64_t {
            if (const auto p = needed_position_[wire]; p >= 0) return p;
            const auto base = static_cast<std::int64_t>(needed.size());
            if (const auto f = free_index_[wire]; f >= 0) return base + f;
            if (other_index_[wire] < 0) {
                other_index_[wire] = static_cast<std::int32_t>(others.size());
                others.push_back(wire);
            }
            return base + static_cast<std::int64_t>(free.size()) + other_index_[wire];
        };
        for (const auto wire : needed) {
            const auto& n = node(wire);
            key.push_back(n.activation == nvr::Activation::Relu ? 1 : 0);
            key.push_back(n.bias);
            key.push_back(static_cast<std::int64_t>(n.edges.size()));
            for (const auto& edge : n.edges) {
                key.push_back(name(edge.source));
                key.push_back(edge.weight);
            }
        }
        for (const auto k : terms) {
            key.push_back(name(top.edges[k].source));
            key.push_back(top.edges[k].weight);
        }
        for (const auto wire : others) {
            key.push_back(values_[wire]);
            other_index_[wire] = -1;
        }
        const auto hash = KeyHash{}(key);
        if (const auto found = memo_find(hash)) {
            if (work_ < assignments * per_assignment) return std::nullopt;
            work_ -= assignments * per_assignment;
            return *found;
        }
        const auto keep = [&](std::optional<Interval> result) {
            memo_keep(hash, result);
            return result;
        };
        // For each variable i, the nodes (by position) that depend on a
        // variable at most i, in stored order: after the odometer changes
        // variables 0 to i, exactly these change.
        const auto variables_free = free.size();
        auto& level = level_;
        level.assign(needed.size(), variables_free);
        for (std::size_t i = 0; i < needed.size(); ++i) {
            for (const auto& edge : node(needed[i]).edges) {
                if (const auto p = needed_position_[edge.source]; p >= 0) {
                    level[i] = std::min(level[i], level[static_cast<std::size_t>(p)]);
                } else if (const auto f = free_index_[edge.source]; f >= 0) {
                    level[i] = std::min(level[i], static_cast<std::size_t>(f));
                }
            }
        }
        if (changed_.size() < variables_free) changed_.resize(variables_free);
        for (std::size_t j = 0; j < variables_free; ++j) {
            changed_[j].clear();
            for (std::size_t i = 0; i < needed.size(); ++i) {
                if (level[i] <= j) changed_[j].push_back(static_cast<std::uint32_t>(i));
            }
        }
        all_.resize(needed.size());
        std::iota(all_.begin(), all_.end(), 0U);
        std::int64_t lo = std::numeric_limits<std::int64_t>::max();
        std::int64_t hi = std::numeric_limits<std::int64_t>::min();
        const std::vector<std::uint32_t>* evaluate = &all_;
        for (std::uint64_t count = 0; count < assignments; ++count) {
            if (work_ < per_assignment) return std::nullopt;
            work_ -= per_assignment;
            const auto units = static_cast<std::uint64_t>(evaluate->size()) + 1;
            if (budget_) {
                if (*budget_ < units) {
                    *budget_ = 0;
                    exhausted_ = true;
                    return std::nullopt;
                }
                *budget_ -= units;
            }
            for (const auto position : *evaluate) {
                const auto wire = needed[position];
                const auto& n = node(wire);
                std::int64_t sum = n.bias;
                for (const auto& edge : n.edges) {
                    std::int64_t product = 0;
                    if (!multiply_fits(values_[edge.source], edge.weight, product) || !add_fits(sum, product, sum)) {
                        return keep(std::nullopt);
                    }
                }
                values_[wire] = n.activation == nvr::Activation::Relu ? std::max<std::int64_t>(sum, 0) : sum;
            }
            std::int64_t sum = 0;
            for (const auto k : terms) {
                std::int64_t product = 0;
                if (!multiply_fits(values_[top.edges[k].source], top.edges[k].weight, product) ||
                    !add_fits(sum, product, sum)) {
                    return keep(std::nullopt);
                }
            }
            lo = std::min(lo, sum);
            hi = std::max(hi, sum);
            if (count + 1 == assignments) break;
            for (std::size_t i = 0; i < variables_free; ++i) {
                const auto& variable = variables[free[i]];
                if (++choice[i] <= variable.hi) {
                    assign(variable, choice[i]);
                    evaluate = &changed_[i];
                    break;
                }
                choice[i] = variable.lo;
                assign(variable, choice[i]);
            }
        }
        return keep(Interval{lo, hi});
    }

    // sum_range's buffers for one depth of conditioning; `parent` is all
    // `none` between calls, `reads` all 0.
    struct SumScratch {
        std::vector<std::size_t> parent;
        std::vector<std::size_t> touched;
        std::vector<std::pair<std::size_t, std::size_t>> keyed;
        std::vector<std::pair<std::size_t, std::size_t>> members;
        std::vector<std::size_t> group;
        std::vector<std::size_t> free;
        std::vector<std::size_t> reads;
        std::vector<std::size_t> counted;
    };
    struct TopKey {
        std::int64_t key;
        std::uint32_t begin;
        std::uint32_t end;
    };
    struct KeyHash {
        std::size_t operator()(const std::vector<std::int64_t>& key) const {
            std::uint64_t hash = 0x9e3779b97f4a7c15ULL;
            for (const auto value : key) {
                hash = (hash ^ static_cast<std::uint64_t>(value)) * 0xff51afd7ed558ccdULL;
                hash ^= hash >> 32;
            }
            return static_cast<std::size_t>(hash);
        }
    };
    // The most wires range() computes from a given start.
    static constexpr std::size_t start_cone = 4096;
    // How deep advice_cut() follows signals of several wires, and the most
    // wires its cut may have.
    static constexpr int advice_depth = 8;
    static constexpr std::size_t advice_wires = 64;
    static constexpr std::size_t advice_instances = 64;
    // The deepest conditioning of sum_range.
    static constexpr int max_depth = 8;
    // The most key words memo_keys_ holds; beyond it, results are not kept.
    static constexpr std::size_t max_enumerated_words = std::size_t{1} << 21;

    const nvr::Graph& graph_;
    OverflowProof& proof_;
    std::vector<std::int64_t> values_;
    // exact_range's wire marks and variables, all clear between calls.
    std::vector<std::uint8_t> marks_;
    std::vector<std::int32_t> variable_of_;
    // range()'s wire marks (1 cut, 2 cone), all clear between calls; the
    // position of each cone wire in the cone, valid during a round; and the
    // group marks of whole_groups(), all -1 between rounds.
    std::vector<std::uint8_t> cone_marks_;
    std::vector<std::uint8_t> start_marks_;
    std::vector<std::int32_t> cone_position_;
    std::vector<std::int32_t> group_slot_;
    std::array<SumScratch, max_depth + 1> sums_;
    // enumerate()'s buffers: wires visited in the current epoch; each free
    // carrier's variable and each evaluated node's position, all -1
    // between calls.
    std::vector<std::uint32_t> visited_;
    std::uint32_t epoch_ = 0;
    std::vector<std::uint32_t> needed_;
    std::vector<std::uint32_t> pending_;
    std::vector<std::int64_t> choice_;
    std::vector<std::int32_t> free_index_;
    std::vector<std::int32_t> needed_position_;
    std::vector<std::int64_t> key_;
    std::vector<std::size_t> level_;
    std::vector<std::vector<std::uint32_t>> changed_;
    std::vector<std::uint32_t> all_;
    std::vector<std::int32_t> other_index_;
    // grouped_sum's groups read whole so far.
    mutable std::vector<std::uint32_t> done_;
    std::vector<std::uint32_t> others_;
    // The kept results of enumerate(): an open-addressing table of entries
    // whose keys lie in memo_keys_. An entry with `length` none is empty.
    struct MemoEntry {
        std::uint64_t hash = 0;
        std::uint32_t offset = 0;
        std::uint32_t length = std::numeric_limits<std::uint32_t>::max();
        std::optional<Interval> result;
    };
    std::vector<MemoEntry> memo_;
    std::vector<std::int64_t> memo_keys_;
    std::size_t memo_count_ = 0;
    // eliminate()'s per-key tables, by wire and by variable, all clear
    // (-1) between calls, and its state buffers.
    std::vector<std::int64_t> last_use_wire_;
    std::vector<std::int64_t> last_use_variable_;
    std::vector<std::int32_t> top_key_wire_;
    std::vector<std::int32_t> top_key_variable_;
    std::vector<std::int32_t> last_edge_wire_;
    std::vector<std::int32_t> last_edge_variable_;
    std::vector<TopKey> top_keys_;
    std::vector<std::pair<std::uint32_t, std::int64_t>> top_terms_;
    std::vector<std::pair<std::int64_t, std::size_t>> top_order_;
    std::vector<std::int64_t> frontier_;
    std::vector<std::int64_t> rows_;
    std::vector<std::int64_t> rows_spare_;
    std::vector<Interval> state_sums_;
    std::vector<Interval> sums_spare_;
    std::vector<std::uint32_t> table_;
    std::uint64_t work_ = 0;
    std::uint64_t* budget_ = nullptr;
    const ProofAdvice* advice_ = nullptr;
    bool decisive_ = false;
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
                                const OverflowProof* resume, const ProofAdvice* advice, AdviceUse use) {
    const bool exact = refine;
    OverflowProof proof;
    // A refined analysis that ran out of work kept every node it settled:
    // continue from the first one it did not. Each node's result depends
    // only on the nodes before it, so the proof is the one an unbounded
    // run finds.
    if (use == AdviceUse::none) advice = nullptr;
    if (!advice || !refine) use = AdviceUse::none;
    proof.advice_use = use;
    const bool resuming = refine && resume && resume->exhausted && resume->advice_use == use &&
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
    Analysis analysis(graph, proof, work, refine ? advice : nullptr, use);
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
                analysis.node(b).activation != nvr::Activation::Relu || analysis.apart(a, b)) {
                continue;
            }
            const nvr::Node sum{nvr::Activation::Identity, 0, {{a, 1}, {b, 1}}};
            auto range = analysis.pair_range(sum, a, b);
            if (!(range && within_bits(*range))) range = analysis.range(sum, 256, 4);
            if (exact && !(range && within_bits(*range))) range = analysis.exact_range(sum, 256, 3);
            if (range && within_bits(*range)) {
                analysis.add_group({{a, 1}, {b, 1}}, *range);
            }
        }
        // The halves need not be adjacent (a conversion's selections read
        // halves built far apart): in a small node whose range is still
        // wider than a bit, any two ReLU sources read with equal weights
        // are tried the same way.
        const auto settled = [&] {
            const auto grouped = analysis.grouped_sum(node);
            if (!grouped) return false;
            const auto low = node.activation == nvr::Activation::Relu ? std::max<std::int64_t>(grouped->lo, 0)
                                                                     : grouped->lo;
            const auto high = node.activation == nvr::Activation::Relu ? std::max<std::int64_t>(grouped->hi, 0)
                                                                      : grouped->hi;
            return low >= 0 && high <= 1;
        };
        // Whether the node is settled changes only when a group is added.
        bool is_settled = exact && node.edges.size() <= 6 && settled();
        for (std::size_t e = 0; exact && node.edges.size() <= 6 && e < node.edges.size(); ++e) {
            for (std::size_t f = e + 1; f < node.edges.size() && !is_settled; ++f) {
                const auto a = node.edges[e].source;
                const auto b = node.edges[f].source;
                if (a < graph.input_lanes || node.edges[e].weight != node.edges[f].weight ||
                    proof.group_of[a] != nvr::absent || proof.group_of[b] != nvr::absent ||
                    analysis.node(a).activation != nvr::Activation::Relu ||
                    analysis.node(b).activation != nvr::Activation::Relu || analysis.apart(a, b)) {
                    continue;
                }
                const nvr::Node sum{nvr::Activation::Identity, 0, {{a, 1}, {b, 1}}};
                auto range = analysis.pair_range(sum, a, b);
                if (!(range && within_bits(*range))) range = analysis.range(sum, 256, 4);
                if (!(range && within_bits(*range))) range = analysis.exact_range(sum, 256, 3);
                if (range && within_bits(*range)) {
                    analysis.add_group({{a, 1}, {b, 1}}, *range);
                    is_settled = settled();
                }
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
            // The cut the advice names first. When it gives a range, that is
            // the range of the node over every assignment of its gadget's
            // inputs, which the search narrows only where those inputs are
            // correlated; a decisive analysis lets it stand.
            bool advised = false;
            if (const auto cut = analysis.advice_cut({static_cast<std::uint32_t>(graph.input_lanes + n)})) {
                const auto rounds = analysis.decisive() ? 1 : advice_rounds;
                if (const auto range = analysis.range(node, advice_budget, rounds, &*cut)) {
                    wires.back().lo = std::max(wires.back().lo, range->lo);
                    wires.back().hi = std::min(wires.back().hi, range->hi);
                    advised = analysis.decisive();
                }
            }
            if (!within_bits(wires.back()) && !advised) {
                if (const auto range = analysis.range(node, 256, 4)) {
                    wires.back().lo = std::max(wires.back().lo, range->lo);
                    wires.back().hi = std::min(wires.back().hi, range->hi);
                }
            }
            if (exact && !within_bits(wires.back()) && !advised) {
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
                    BitEvidence* evidence, bool use_cases, std::uint64_t* work, const ProofAdvice* advice) {
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
                cases.emplace(graph, copy, work, advice);
            }
            const auto& top = graph.nodes[wire - graph.input_lanes];
            if (const auto cut = cases->advice_cut({wire})) {
                const auto range = cases->range(top, std::uint64_t{1} << 14, advice_rounds, &*cut);
                if (cases->exhausted()) return false;
                if (range && within_bits(*range)) {
                    if (evidence) ++evidence->cases;
                    continue;
                }
            }
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

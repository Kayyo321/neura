#include "recognize.hpp"

#include "checked.hpp"
#include "circuit/float_format.hpp"
#include "proof.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

namespace neura::lift {
namespace {

std::uint64_t mask(unsigned width) { return width_mask(width); }
bool integer(const nvr::Type& type) { return is_integer(type); }
bool floating(const nvr::Type& type) { return is_floating(type); }

FloatFormat float_of(const nvr::Type& type) {
    if (type.kind == nvr::TypeKind::BFloat) return bfloat16;
    if (type.width == 16) return binary16;
    if (type.width == 32) return binary32;
    return binary64;
}

// Evaluates a graph in place with checked arithmetic: `wires` holds the input
// lanes on entry and every wire on return. False when a product or a sum
// overflows.
bool evaluate_wires(const nvr::Graph& graph, std::vector<std::int64_t>& wires) {
    wires.resize(std::size_t{graph.input_lanes} + graph.nodes.size());
    for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
        const auto& node = graph.nodes[n];
        std::int64_t sum = node.bias;
        for (const auto& edge : node.edges) {
            std::int64_t product = 0;
            if (!checked_multiply(wires[edge.source], edge.weight, product) || !checked_add(sum, product, sum)) {
                return false;
            }
        }
        wires[graph.input_lanes + n] = node.activation == nvr::Activation::Relu ? std::max<std::int64_t>(0, sum) : sum;
    }
    return true;
}

// A word-level view of one block: its inputs, the values its exits read from
// the graph, and samples of the graph's behavior.
class BlockModel {
public:
    BlockModel(const nvr::Image& image, const nvr::Block& block)
        : image_(image), block_(block), graph_(image.graphs.at(block.graph)) {
        std::uint32_t lane = 0;
        for (const auto& value : block.inputs) {
            offsets_.push_back(lane);
            lane += nvr::lane_count(image.types.at(value.type));
        }
        lanes_ = lane;
        for (const auto& exit : block.exits) {
            for (const auto* list : {&exit.arguments}) {
                for (const auto& value : *list) {
                    note(value);
                }
            }
            for (const auto& assignment : exit.assignments) {
                note(assignment.value);
            }
        }
        // Wire dependencies on input lanes.
        support_.resize(graph_.input_lanes + graph_.nodes.size());
        for (std::uint32_t i = 0; i < graph_.input_lanes; ++i) {
            support_[i] = {i};
        }
        for (std::size_t n = 0; n < graph_.nodes.size(); ++n) {
            auto& set = support_[graph_.input_lanes + n];
            for (const auto& edge : graph_.nodes[n].edges) {
                set.insert(support_[edge.source].begin(), support_[edge.source].end());
            }
        }
    }

    const nvr::Image& image() const { return image_; }
    const nvr::Block& block() const { return block_; }
    const nvr::Graph& graph() const { return graph_; }
    std::size_t input_count() const { return block_.inputs.size(); }
    std::uint32_t input_type(std::size_t i) const { return block_.inputs[i].type; }
    std::uint32_t total_lanes() const { return lanes_; }

    // Input values that equality neurons spell: a ReLU whose edges, all of
    // weight +1 or -1, read every lane of one input word, is true only when
    // the word equals the constant those weights spell.
    std::vector<std::pair<std::size_t, std::uint64_t>> spelled() const {
        std::vector<std::pair<std::size_t, std::uint64_t>> result;
        for (const auto& node : graph_.nodes) {
            if (node.activation != nvr::Activation::Relu || node.edges.empty()) {
                continue;
            }
            const auto first = node.edges.front().source;
            if (first >= graph_.input_lanes) {
                continue;
            }
            const auto word = static_cast<std::size_t>(
                std::upper_bound(offsets_.begin(), offsets_.end(), first) - offsets_.begin() - 1);
            const auto width = width_of(block_.inputs[word].type);
            if (node.edges.size() != width) {
                continue;
            }
            std::uint64_t value = 0;
            bool spells = true;
            for (std::size_t b = 0; b < width; ++b) {
                const auto& edge = node.edges[b];
                spells = spells && edge.source == offsets_[word] + b && (edge.weight == 1 || edge.weight == -1);
                value |= edge.weight == 1 ? std::uint64_t{1} << b : 0;
            }
            if (spells) {
                result.emplace_back(word, value);
            }
        }
        return result;
    }
    const std::map<std::uint32_t, std::uint32_t>& targets() const { return targets_; }

    unsigned width_of(std::uint32_t type) const { return nvr::lane_count(image_.types.at(type)); }

    // Input words a set of output lanes depends on.
    std::set<std::size_t> words_of(std::uint32_t first, std::uint32_t count) const {
        std::set<std::size_t> words;
        for (std::uint32_t o = first; o < first + count; ++o) {
            for (const auto lane : support_.at(graph_.outputs.at(o))) {
                const auto word = std::upper_bound(offsets_.begin(), offsets_.end(), lane) - offsets_.begin() - 1;
                words.insert(static_cast<std::size_t>(word));
            }
        }
        return words;
    }

    // Evaluates the block's graph with each input word set to the given
    // value. Returns nothing when the graph overflows.
    std::optional<std::vector<std::int64_t>> outputs(const std::vector<std::uint64_t>& words) const {
        std::vector<std::int64_t> wires(graph_.input_lanes, 0);
        for (std::size_t i = 0; i < words.size(); ++i) {
            const auto width = width_of(block_.inputs[i].type);
            for (unsigned b = 0; b < width; ++b) {
                wires[offsets_[i] + b] = static_cast<std::int64_t>((words[i] >> b) & 1U);
            }
        }
        if (!evaluate_wires(graph_, wires)) {
            return std::nullopt;
        }
        std::vector<std::int64_t> result;
        for (const auto output : graph_.outputs) {
            result.push_back(wires[output]);
        }
        return result;
    }

    static std::uint64_t word(const std::vector<std::int64_t>& outputs, std::uint32_t first, std::uint32_t count) {
        std::uint64_t value = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            value |= static_cast<std::uint64_t>(outputs[first + i] & 1) << i;
        }
        return value;
    }

    // The exit a selector picks.
    std::size_t selected(const std::vector<std::int64_t>& outputs) const {
        const auto lanes = nvr::selector_lanes(block_.exits.size());
        return static_cast<std::size_t>(word(outputs, block_.selector_start, lanes));
    }

private:
    void note(const nvr::Value& value) {
        if (value.kind == nvr::ValueKind::Lanes) {
            targets_.emplace(value.index, value.type);
        }
    }

    const nvr::Image& image_;
    const nvr::Block& block_;
    const nvr::Graph& graph_;
    std::vector<std::uint32_t> offsets_;
    std::uint32_t lanes_ = 0;
    std::map<std::uint32_t, std::uint32_t> targets_;
    std::vector<std::set<std::uint32_t>> support_;
};

// Pseudo-random sample values for each input word, including boundaries.
std::vector<std::vector<std::uint64_t>> samples(const BlockModel& model, std::size_t count) {
    std::vector<std::vector<std::uint64_t>> result;
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (std::size_t s = 0; s < count; ++s) {
        std::vector<std::uint64_t> words;
        for (std::size_t i = 0; i < model.input_count(); ++i) {
            const auto width = model.width_of(model.input_type(i));
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            std::uint64_t value = state;
            switch (s % 6) {
            case 0: value &= 0xff; break;
            case 1: value = s < 6 ? 0 : value; break;
            case 2: value = s < 6 ? 1 : (value & 0xffff); break;
            case 3: value = s < 6 ? mask(width) : value; break;
            default: break;
            }
            words.push_back(value & mask(width));
        }
        result.push_back(std::move(words));
    }
    return result;
}

// The circuit of an expression over the block's input words, as a graph whose
// outputs are the expression's lanes.
nvr::Graph compile(const nvr::Image& image, const BlockModel& model, const Expression& e) {
    std::vector<unsigned> widths;
    for (std::size_t i = 0; i < model.input_count(); ++i) {
        widths.push_back(model.width_of(model.input_type(i)));
    }
    return expression_circuit(image, widths, e);
}

// Runs a compiled expression, or nothing when its graph overflows.
std::optional<std::uint64_t> run(const nvr::Graph& graph, const BlockModel& model,
                                 const std::vector<std::uint64_t>& words) {
    std::vector<std::int64_t> wires(graph.input_lanes, 0);
    std::size_t lane = 0;
    for (std::size_t i = 0; i < words.size(); ++i) {
        for (unsigned b = 0; b < model.width_of(model.input_type(i)); ++b) {
            wires[lane++] = static_cast<std::int64_t>((words[i] >> b) & 1U);
        }
    }
    if (!evaluate_wires(graph, wires)) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < graph.outputs.size(); ++i) {
        value |= static_cast<std::uint64_t>(wires[graph.outputs[i]] & 1) << i;
    }
    return value;
}

// Floating-point values for the candidate filter, through host arithmetic:
// binary64 and binary32 natively, the 16-bit formats in double and rounded
// once more (exact, because double has more than twice their precision).
// Only a filter: a candidate is accepted when its replayed circuit is
// identical to the block's.
double float_decode(std::uint64_t bits, FloatFormat format) {
    const auto f = format.fraction_bits;
    const bool negative = ((bits >> (format.width() - 1)) & 1U) != 0;
    const auto exponent = static_cast<int>((bits >> f) & ((std::uint64_t{1} << format.exponent_bits) - 1));
    const auto fraction = bits & ((std::uint64_t{1} << f) - 1);
    const int bias = static_cast<int>(format.bias());
    double magnitude;
    if (exponent == (1 << format.exponent_bits) - 1) {
        magnitude = fraction == 0 ? INFINITY : NAN;
    } else if (exponent == 0) {
        magnitude = std::ldexp(static_cast<double>(fraction), 1 - bias - static_cast<int>(f));
    } else {
        magnitude = std::ldexp(static_cast<double>(fraction | (std::uint64_t{1} << f)),
                               exponent - bias - static_cast<int>(f));
    }
    return negative ? -magnitude : magnitude;
}

std::uint64_t float_encode(double value, FloatFormat format) {
    if (std::isnan(value)) {
        return format.canonical_nan();
    }
    if (format == binary64) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, 8);
        return bits;
    }
    const auto f = static_cast<int>(format.fraction_bits);
    const auto sign = std::signbit(value) ? std::uint64_t{1} << (format.width() - 1) : 0;
    const auto infinity = ((std::uint64_t{1} << format.exponent_bits) - 1) << f;
    const double magnitude = std::fabs(value);
    if (std::isinf(magnitude)) return sign | infinity;
    if (magnitude == 0) return sign;
    int exp = 0;
    std::frexp(magnitude, &exp);
    const int minimum = 1 - static_cast<int>(format.bias());
    int scale = std::max(exp - 1, minimum) - f;
    double significand = std::nearbyint(std::ldexp(magnitude, -scale));
    if (significand == std::ldexp(1.0, f + 1)) {
        significand /= 2;
        ++scale;
    }
    const auto integer_part = static_cast<std::uint64_t>(significand);
    if (integer_part < (std::uint64_t{1} << f)) return sign | integer_part;
    const auto field = scale + f + static_cast<int>(format.bias());
    if (field >= (1 << format.exponent_bits) - 1) return sign | infinity;
    return sign | static_cast<std::uint64_t>(field) << f | (integer_part - (std::uint64_t{1} << f));
}

std::uint64_t single_bits(float value) {
    if (std::isnan(value)) return binary32.canonical_nan();
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The value lanes of an integer or Boolean expression, computed on words
// rather than circuits. Matches the gadgets' lanes even when a check fails
// (sums wrap, a signed quotient of MIN / -1 is MIN); nothing for a zero
// divisor, whose lanes the gadgets leave unspecified, and for floats.
std::optional<std::uint64_t> semantic(const nvr::Image& image, const Expression& e,
                                      const std::vector<std::uint64_t>& words, const BlockModel& model) {
    const auto& t = image.types.at(e.type);
    const auto width = nvr::lane_count(t);
    const auto m = mask(width);
    const auto signed_value = [](std::uint64_t bits, unsigned w) {
        return w < 64 && ((bits >> (w - 1)) & 1U) != 0 ? static_cast<std::int64_t>(bits | ~mask(w))
                                                       : static_cast<std::int64_t>(bits);
    };
    switch (e.kind) {
    case Expression::Kind::Input:
        return words.at(e.input) & mask(model.width_of(model.input_type(e.input)));
    case Expression::Kind::Immediate:
        return e.bits & m;
    case Expression::Kind::Unary: {
        const auto x = semantic(image, *e.operands[0], words, model);
        if (!x) return std::nullopt;
        if (e.op == "!") return (*x ^ 1U) & 1U;
        if (e.op == "index") return *x;
        if (e.op == "~") return ~*x & m;
        if (floating(t)) return *x ^ (std::uint64_t{1} << (width - 1));
        return (0 - *x) & m;
    }
    case Expression::Kind::Convert: {
        const auto& from = image.types.at(e.operands[0]->type);
        const auto x = semantic(image, *e.operands[0], words, model);
        if (!x) return std::nullopt;
        if (floating(from) && floating(t)) {
            return float_encode(float_decode(*x, float_of(from)), float_of(t));
        }
        if (floating(t)) {
            const bool is_signed = from.kind == nvr::TypeKind::Signed;
            if (float_of(t) == binary32) {
                return single_bits(is_signed ? static_cast<float>(signed_value(*x, from.width)) : static_cast<float>(*x));
            }
            return float_encode(is_signed ? static_cast<double>(signed_value(*x, from.width)) : static_cast<double>(*x),
                                float_of(t));
        }
        if (floating(from)) {
            // The lanes of an out-of-range conversion are unspecified.
            const double truncated = std::trunc(float_decode(*x, float_of(from)));
            if (std::isnan(truncated) || std::fabs(truncated) >= 9.2e18) return std::nullopt;
            return static_cast<std::uint64_t>(static_cast<std::int64_t>(truncated)) & m;
        }
        const auto extended = from.kind == nvr::TypeKind::Signed ? static_cast<std::uint64_t>(signed_value(*x, from.width)) : *x;
        return extended & m;
    }
    case Expression::Kind::Binary: {
        const auto& o = image.types.at(e.operands[0]->type);
        const auto a = semantic(image, *e.operands[0], words, model);
        const auto b = semantic(image, *e.operands[1], words, model);
        if (!a || !b) return std::nullopt;
        if (floating(o)) {
            const auto format = float_of(o);
            const double x = float_decode(*a, format);
            const double y = float_decode(*b, format);
            const auto& op = e.op;
            if (op == "==") return x == y ? 1 : 0;
            if (op == "!=") return x != y ? 1 : 0;
            if (op == "<") return x < y ? 1 : 0;
            if (op == "<=") return x <= y ? 1 : 0;
            if (op == ">") return x > y ? 1 : 0;
            if (op == ">=") return x >= y ? 1 : 0;
            if (format == binary32) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                return single_bits(op == "+" ? fx + fy : op == "-" ? fx - fy : op == "*" ? fx * fy : fx / fy);
            }
            return float_encode(op == "+" ? x + y : op == "-" ? x - y : op == "*" ? x * y : x / y, format);
        }
        const auto w = nvr::lane_count(o);
        const bool is_signed = o.kind == nvr::TypeKind::Signed;
        const auto& op = e.op;
        if (op == "&&") return *a & *b;
        if (op == "||") return *a | *b;
        if (op == "==") return *a == *b ? 1 : 0;
        if (op == "!=") return *a != *b ? 1 : 0;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            const int order = is_signed ? (signed_value(*a, w) < signed_value(*b, w) ? -1 : signed_value(*a, w) > signed_value(*b, w) ? 1 : 0)
                                        : (*a < *b ? -1 : *a > *b ? 1 : 0);
            if (op == "<") return order < 0 ? 1 : 0;
            if (op == "<=") return order <= 0 ? 1 : 0;
            if (op == ">") return order > 0 ? 1 : 0;
            return order >= 0 ? 1 : 0;
        }
        if (op == "&") return *a & *b;
        if (op == "|") return *a | *b;
        if (op == "^") return *a ^ *b;
        if (op == "<<" || op == ">>") {
            // A count outside the width fails; its lanes are unspecified.
            if (*b >= w) return std::nullopt;
            if (op == "<<") return (*a << *b) & m;
            return (is_signed ? static_cast<std::uint64_t>(signed_value(*a, w) >> *b) : *a >> *b) & m;
        }
        if (op == "+" || op == "+~") return (*a + *b) & m;
        if (op == "-") return (*a - *b) & m;
        if (op == "*") return (*a * *b) & m;
        if (*b == 0) return std::nullopt;
        if (!is_signed) return (op == "/" ? *a / *b : *a % *b) & m;
        const auto x = signed_value(*a, w);
        const auto y = signed_value(*b, w);
        const auto mx = x < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(x) : static_cast<std::uint64_t>(x);
        const auto my = y < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(y) : static_cast<std::uint64_t>(y);
        if (op == "/") {
            const auto q = mx / my;
            return ((x < 0) != (y < 0) ? std::uint64_t{0} - q : q) & m;
        }
        const auto r = mx % my;
        return (x < 0 ? std::uint64_t{0} - r : r) & m;
    }
    }
    return std::nullopt;
}

// Candidate operations producing a word of `type` from `words`.
class Candidates {
public:
    Candidates(const BlockModel& model, const std::vector<std::vector<std::uint64_t>>& probes, bool extended)
        : model_(model), image_(model.image()), probes_(probes), extended_(extended) {}

    // Expressions whose value on every probe equals `expected(probe)`.
    std::vector<ExpressionPtr> find(std::uint32_t type, const std::set<std::size_t>& support,
                                    const std::function<std::optional<std::uint64_t>(std::size_t)>& expected) {
        std::vector<ExpressionPtr> result;
        const auto consider = [&](const ExpressionPtr& candidate) {
            if (true) {
                for (std::size_t p = 0; p < probes_.size(); ++p) {
                    const auto want = expected(p);
                    const auto got = want ? semantic(image_, *candidate, probes_[p], model_) : std::nullopt;
                    if (want && got && *got != *want) {
                        return;
                    }
                }
                result.push_back(candidate);
                return;
            }
            const auto graph = compile(image_, model_, *candidate);
            for (std::size_t p = 0; p < probes_.size(); ++p) {
                const auto want = expected(p);
                if (!want) {
                    continue;
                }
                const auto got = run(graph, model_, probes_[p]);
                if (!got || *got != *want) {
                    return;
                }
            }
            result.push_back(candidate);
        };
        for (const auto& candidate : enumerate(type, support)) {
            consider(candidate);
            if (result.size() >= 6) {
                break;
            }
        }
        return result;
    }

private:
    std::vector<std::uint32_t> types_of_kind(bool (*predicate)(const nvr::Type&)) const {
        std::vector<std::uint32_t> result;
        for (std::uint32_t i = 0; i < image_.types.size(); ++i) {
            if (predicate(image_.types[i])) {
                result.push_back(i);
            }
        }
        return result;
    }

    // Operations producing `type` from operands of `operand`.
    std::vector<std::string> operators(std::uint32_t type, std::uint32_t operand) const {
        const auto& t = image_.types.at(type);
        const auto& o = image_.types.at(operand);
        std::vector<std::string> result;
        if (t.kind == nvr::TypeKind::Bool) {
            if (o.kind == nvr::TypeKind::Bool) {
                result = {"&&", "||"};
            }
        } else if (type == operand || (t.kind == o.kind && t.width == o.width)) {
            if (integer(t)) {
                result = {"+", "-", "*", "/", "%"};
                if (extended_) {
                    result.insert(result.end(), {"&", "|", "^", "<<", ">>"});
                }
            } else if (floating(t)) {
                result = {"+", "-", "*", "/"};
            }
        }
        return result;
    }

    // Operands built from one input word: the word, its negation, and its
    // conversions to every numeric type of the image.
    std::vector<ExpressionPtr> atoms(std::size_t i) const {
        const auto u = model_.input_type(i);
        const auto& ut = image_.types.at(u);
        const auto leaf = input(u, i);
        std::vector<ExpressionPtr> result{leaf};
        if (ut.kind == nvr::TypeKind::Bool) {
            result.push_back(unary("!", u, leaf));
        }
        if (integer(ut) || floating(ut)) {
            result.push_back(unary("neg", u, leaf));
            for (std::uint32_t t = 0; t < image_.types.size(); ++t) {
                const auto& target = image_.types[t];
                if (t != u && (integer(target) || floating(target)) &&
                    std::none_of(image_.types.begin(), image_.types.begin() + t,
                                 [&](const nvr::Type& earlier) { return earlier == target; })) {
                    result.push_back(convert(t, leaf));
                }
            }
            if (integer(ut) && extended_) {
                result.push_back(unary("~", u, leaf));
            }
            if (integer(ut)) {
                std::uint32_t u64 = 0;
                while (u64 < image_.types.size() &&
                       !(image_.types[u64].kind == nvr::TypeKind::Unsigned && image_.types[u64].width == 64)) {
                    ++u64;
                }
                if (u64 < image_.types.size()) {
                    result.push_back(unary("index", u64, leaf));
                }
            }
        }
        return result;
    }

    // Constants worth trying for operands of `type`: small values and every
    // constant an equality neuron spells.
    std::vector<ExpressionPtr> constants_of(std::uint32_t type) const {
        const auto& t = image_.types.at(type);
        std::vector<ExpressionPtr> result;
        if (!integer(t) && !floating(t)) {
            return result;
        }
        std::set<std::uint64_t> values{0, 1, 2, 3, 4, 5, 8, 10, 16, 100, mask(t.width)};
        if (floating(t)) {
            const auto format = float_of(t);
            const auto one = format.bias() << format.fraction_bits;
            values = {0, std::uint64_t{1} << (t.width - 1), one, one | (std::uint64_t{1} << (t.width - 1))};
        }
        for (const auto& node : model_.graph().nodes) {
            if (node.activation != nvr::Activation::Relu || node.edges.size() != t.width) {
                continue;
            }
            std::uint64_t c = 0;
            bool spelled = true;
            for (std::size_t b = 0; b < node.edges.size(); ++b) {
                spelled = spelled && (node.edges[b].weight == 1 || node.edges[b].weight == -1);
                c |= node.edges[b].weight == 1 ? std::uint64_t{1} << b : 0;
            }
            if (spelled && values.size() < 24) {
                values.insert(c);
                if (floating(t)) {
                    // Float equality also accepts the zero of the other sign.
                    values.insert(c ^ (std::uint64_t{1} << (t.width - 1)));
                }
            }
        }
        for (const auto value : values) {
            result.push_back(immediate(type, value & mask(t.width)));
        }
        return result;
    }

    std::vector<ExpressionPtr> enumerate(std::uint32_t type, const std::set<std::size_t>& support) {
        std::vector<ExpressionPtr> out;
        const std::vector<std::size_t> words(support.begin(), support.end());
        if (words.empty() || words.size() > 3) {
            return out;
        }
        const auto& t = image_.types.at(type);
        // Level one: atoms, and one operation on atoms and constants.
        std::vector<std::pair<std::set<std::size_t>, ExpressionPtr>> level;
        std::map<std::pair<std::uint32_t, std::vector<std::uint64_t>>, int> seen;
        const auto signature = [&](const ExpressionPtr& e) {
            std::vector<std::uint64_t> values;
            // The first probes and every probe at a spelled constant.
            for (std::size_t p = 0; p < probes_.size(); p = p == 11 ? 40 : p + 1) {
                values.push_back(semantic(image_, *e, probes_[p], model_).value_or(~std::uint64_t{0}));
            }
            return values;
        };
        const auto keep = [&](std::set<std::size_t> used, const ExpressionPtr& e) {
            auto& count = seen[{e->type, signature(e)}];
            if (count >= 2) {
                return;
            }
            ++count;
            level.emplace_back(std::move(used), e);
        };
        for (const auto i : words) {
            for (const auto& atom : atoms(i)) {
                keep({i}, atom);
            }
        }
        const auto base = level;
        for (const auto& [ua, a] : base) {
            const auto operand = a->type;
            const auto& ot = image_.types.at(operand);
            for (const auto& [ub, b] : base) {
                if (b->type != operand) {
                    continue;
                }
                std::set<std::size_t> used = ua;
                used.insert(ub.begin(), ub.end());
                for (const auto& op : operators(operand, operand)) {
                    keep(used, binary(op, operand, a, b));
                }
                if (ot.kind != nvr::TypeKind::Bool || operand == type) {
                    for (const auto& op : comparison_operators(ot)) {
                        keep(used, binary(op, boolean_type(), a, b));
                    }
                }
            }
            for (const auto& c : constants_of(operand)) {
                for (const auto& op : operators(operand, operand)) {
                    keep(ua, binary(op, operand, a, c));
                    keep(ua, binary(op, operand, c, a));
                }
                for (const auto& op : comparison_operators(ot)) {
                    keep(ua, binary(op, boolean_type(), a, c));
                    keep(ua, binary(op, boolean_type(), c, a));
                }
            }
        }
        for (const auto& [used, e] : level) {
            if (e->type == type && used == support) {
                out.push_back(e);
            }
        }
        if (words.size() == 1) {
            constants(type, words[0], out);
        }
        // Level two: one more operation over level-one results, for integer
        // and Boolean results.
        if (integer(t) || t.kind == nvr::TypeKind::Bool) {
            const auto first = level;
            for (const auto& [ua, a] : first) {
                const auto operand = a->type;
                const auto& ot = image_.types.at(operand);
                if (!integer(ot) && ot.kind != nvr::TypeKind::Bool) {
                    continue;
                }
                const auto ops = type == operand ? operators(operand, operand) : comparison_operators(ot);
                if (type != operand && t.kind != nvr::TypeKind::Bool) {
                    continue;
                }
                if (a->kind != Expression::Kind::Binary) {
                    continue;
                }
                for (const auto& [ub, b] : base) {
                    if (b->type != operand) {
                        continue;
                    }
                    std::set<std::size_t> used = ua;
                    used.insert(ub.begin(), ub.end());
                    if (used != support) {
                        continue;
                    }
                    for (const auto& op : ops) {
                        out.push_back(binary(op, type, a, b));
                        out.push_back(binary(op, type, b, a));
                    }
                }
                if (ua == support) {
                    for (const auto& c : constants_of(operand)) {
                        for (const auto& op : ops) {
                            out.push_back(binary(op, type, a, c));
                            out.push_back(binary(op, type, c, a));
                        }
                    }
                }
                if (out.size() > 20000) {
                    break;
                }
            }
        }
        return out;
    }

    std::uint32_t boolean_type() const {
        std::uint32_t index = 0;
        while (index < image_.types.size() && image_.types[index].kind != nvr::TypeKind::Bool) {
            ++index;
        }
        return index;
    }

    // Comparisons produce bool, so an image without a Boolean type has none.
    std::vector<std::string> comparison_operators(const nvr::Type& t) const {
        if (boolean_type() >= image_.types.size()) {
            return {};
        }
        if (t.kind == nvr::TypeKind::Bool) {
            return {"==", "!="};
        }
        return {"==", "!=", "<", "<=", ">", ">="};
    }

    // `x op c` and `c op x` with c recovered from the graph's behavior.
    void constants(std::uint32_t type, std::size_t i, std::vector<ExpressionPtr>& out) {
        const auto u = model_.input_type(i);
        const auto& ut = image_.types.at(u);
        const auto& t = image_.types.at(type);
        const auto leaf = input(u, i);
        const auto width = model_.width_of(u);
        // Probe values of x alone; the other inputs stay zero.
        const auto probe = [&](std::uint64_t x) -> std::optional<std::uint64_t> {
            std::vector<std::uint64_t> words(model_.input_count(), 0);
            words[i] = x & mask(width);
            const auto outputs = model_.outputs(words);
            if (!outputs) {
                return std::nullopt;
            }
            return expected_from_outputs_(*outputs);
        };
        if (integer(ut) && type == u) {
            const auto m = mask(width);
            if (const auto at_zero = probe(0)) {
                out.push_back(binary("+", type, leaf, immediate(u, *at_zero)));
                out.push_back(binary("+~", type, leaf, immediate(u, *at_zero)));
                out.push_back(binary("-", type, leaf, immediate(u, (0 - *at_zero) & m)));
                out.push_back(binary("-", type, immediate(u, *at_zero), leaf));
            }
            if (const auto at_one = probe(1)) {
                out.push_back(binary("*", type, leaf, immediate(u, *at_one)));
            }
            for (const std::uint64_t c : {2ULL, 3ULL, 4ULL, 5ULL, 8ULL, 10ULL, 16ULL, 100ULL}) {
                out.push_back(binary("/", type, leaf, immediate(u, c)));
                out.push_back(binary("%", type, leaf, immediate(u, c)));
            }
        }
        if (t.kind == nvr::TypeKind::Bool && integer(ut)) {
            // The predicate x op c changes value at one boundary; find it by
            // bisection in the type's order.
            const bool is_signed = ut.kind == nvr::TypeKind::Signed;
            const auto order = [&](std::uint64_t key) {  // key in [0, 2^w) maps to values in order
                return is_signed ? (key ^ (std::uint64_t{1} << (width - 1))) & mask(width) : key;
            };
            const auto low = probe(order(0));
            const auto high = probe(order(mask(width)));
            if (low && high && *low != *high) {
                std::uint64_t lo = 0;
                std::uint64_t hi = mask(width);
                while (hi - lo > 1) {
                    const auto mid = lo + (hi - lo) / 2;
                    const auto at = probe(order(mid));
                    if (!at) {
                        break;
                    }
                    (*at == *low ? lo : hi) = mid;
                }
                for (const auto key : {lo, hi}) {
                    const auto c = order(key);
                    for (const auto* op : {"<", "<=", ">", ">="}) {
                        out.push_back(binary(op, type, leaf, immediate(u, c)));
                        out.push_back(binary(op, type, immediate(u, c), leaf));
                    }
                }
            }
            // Equality with a constant is one ReLU whose weights spell the
            // constant: +1 on lanes where it has a one, -1 where a zero.
            const auto& graph = model_.graph();
            for (const auto& node : graph.nodes) {
                if (node.activation != nvr::Activation::Relu || node.edges.size() != width) {
                    continue;
                }
                std::uint64_t c = 0;
                bool spelled = true;
                for (std::size_t b = 0; b < node.edges.size(); ++b) {
                    const auto& edge = node.edges[b];
                    if (edge.weight != 1 && edge.weight != -1) {
                        spelled = false;
                        break;
                    }
                    if (edge.weight == 1) {
                        c |= std::uint64_t{1} << b;
                    }
                }
                if (spelled) {
                    out.push_back(binary("==", type, leaf, immediate(u, c)));
                    out.push_back(binary("!=", type, leaf, immediate(u, c)));
                }
            }
        }
        if (floating(ut) && t.kind == nvr::TypeKind::Bool) {
            // An ordered comparison with a constant changes value once along
            // the float order; bisect the non-NaN encodings in that order.
            const auto sign = std::uint64_t{1} << (width - 1);
            const auto infinity = ((std::uint64_t{1} << float_of(ut).exponent_bits) - 1) << float_of(ut).fraction_bits;
            // Keys 0 .. 2 * (infinity + 1) - 1 enumerate -inf .. -0, +0 .. +inf.
            const auto count = infinity + 1;
            const auto bits_of = [&](std::uint64_t key) {
                return key < count ? (sign | (infinity - key)) : key - count;
            };
            const auto low = probe(bits_of(0));
            const auto high = probe(bits_of(2 * count - 1));
            if (low && high && *low != *high) {
                std::uint64_t lo = 0;
                std::uint64_t hi = 2 * count - 1;
                while (hi - lo > 1) {
                    const auto mid = lo + (hi - lo) / 2;
                    const auto at = probe(bits_of(mid));
                    if (!at) {
                        break;
                    }
                    (*at == *low ? lo : hi) = mid;
                }
                for (const auto key : {lo, hi}) {
                    for (const auto c : {bits_of(key), bits_of(key) ^ sign}) {
                        for (const auto* op : {"<", "<=", ">", ">="}) {
                            out.push_back(binary(op, type, leaf, immediate(u, c)));
                            out.push_back(binary(op, type, immediate(u, c), leaf));
                        }
                    }
                }
            }
        }
        if (floating(ut) && type == u) {
            const auto format = float_of(ut);
            const auto one = format.bias() << format.fraction_bits;
            if (const auto at_one = probe(one)) {
                // c / 1 is c; x / c at 1 is 1 / c, rounded.
                out.push_back(binary("/", type, immediate(u, *at_one), leaf));
                const auto reciprocal = float_encode(1.0 / float_decode(*at_one, format), format);
                out.push_back(binary("/", type, leaf, immediate(u, reciprocal)));
            }
            if (const auto at_zero = probe(0)) {
                out.push_back(binary("+", type, leaf, immediate(u, *at_zero)));
                out.push_back(binary("-", type, immediate(u, *at_zero), leaf));
                out.push_back(binary("-", type, leaf, immediate(u, *at_zero ^ (std::uint64_t{1} << (width - 1)))));
            }
            if (const auto at_one = probe(one)) {
                out.push_back(binary("*", type, leaf, immediate(u, *at_one)));
            }
        }
    }

public:
    std::function<std::optional<std::uint64_t>(const std::vector<std::int64_t>&)> expected_from_outputs_;

private:
    const BlockModel& model_;
    const nvr::Image& image_;
    const std::vector<std::vector<std::uint64_t>>& probes_;
    bool extended_ = false;
};
// The exit a dispatch block takes for each layout, recovered by evaluating
// its graph for every tag, and regenerated the way the converter builds
// dispatch blocks.
bool lift_dispatch(const nvr::Image& image, const nvr::Block& block, LiftedForm& lifted) {
    if (block.inputs.size() != 2 || block.exits.empty() || block.exits.back().kind != nvr::ExitKind::Trap ||
        block.exits.back().reason != static_cast<std::uint32_t>(nvr::TrapReason::InvalidDispatch)) {
        return false;
    }
    const auto& tag = image.types.at(block.inputs[0].type);
    if (tag.kind != nvr::TypeKind::Unsigned || tag.width != 32 || block.inputs[1].kind != nvr::ValueKind::Slot) {
        return false;
    }
    for (std::size_t e = 0; e + 1 < block.exits.size(); ++e) {
        if (block.exits[e].kind != nvr::ExitKind::Call) {
            return false;
        }
    }
    const BlockModel model(image, block);
    std::map<std::size_t, std::vector<std::uint32_t>> exits;
    for (std::uint32_t layout = 0; layout < image.layouts.size(); ++layout) {
        const auto outputs = model.outputs({layout, 0});
        if (!outputs) {
            return false;
        }
        const auto exit = block.exits.size() == 1 ? 0 : model.selected(*outputs);
        if (exit + 1 < block.exits.size()) {
            exits[exit].push_back(layout);
        }
    }
    if (!(dispatch_circuit(block, exits) == image.graphs.at(block.graph))) {
        return false;
    }
    lifted.dispatch = std::move(exits);
    return true;
}

} // namespace

std::optional<std::vector<std::int64_t>> evaluate_reference(const nvr::Graph& graph,
                                                            const std::vector<std::int64_t>& inputs) {
    std::vector<std::int64_t> wires(inputs);
    wires.resize(graph.input_lanes, 0);
    if (!evaluate_wires(graph, wires)) {
        return std::nullopt;
    }
    std::vector<std::int64_t> outputs;
    outputs.reserve(graph.outputs.size());
    for (const auto output : graph.outputs) {
        outputs.push_back(wires[output]);
    }
    return outputs;
}

Recognition recognize(const nvr::Image& image, std::uint32_t function_index, std::uint32_t block_index,
                      const RecognizeOptions& options) {
    const auto& function = image.functions.at(function_index);
    const auto& block = function.blocks.at(block_index);
    const auto& graph = image.graphs.at(block.graph);
    Recognition result;
    const auto unrecognized = [&](const char* reason) {
        result.status = Recognition::Status::Unrecognized;
        result.reason = reason;
        return result;
    };
    const BlockModel model(image, block);
    std::size_t normal_exits = 0;
    for (const auto& exit : block.exits) {
        normal_exits += exit.kind == nvr::ExitKind::Trap ? 0 : 1;
    }
    if (graph.nodes.empty() && model.targets().empty() && block.exits.size() <= 1) {
        result.status = Recognition::Status::Trivial;
        return result;
    }
    // A poll does not change what the graph computes or which exit it
    // selects, so a polling block lifts like any other. A scheduler exit's
    // effect is outside the graph model.
    if (std::ranges::any_of(block.exits, [](const nvr::Exit& exit) {
            return exit.kind == nvr::ExitKind::Scheduler;
        })) {
        return unrecognized("scheduler exit");
    }
    if (lift_dispatch(image, block, result.form)) {
        result.status = Recognition::Status::Lifted;
        return result;
    }

    // Observe the graph on sample inputs.
    auto probes = samples(model, 40);
    for (const auto& [word, value] : model.spelled()) {
        if (probes.size() >= 64) {
            break;
        }
        auto probe = probes[probes.size() % 40];
        probe[word] = value;
        probes.push_back(std::move(probe));
    }
    std::vector<std::vector<std::int64_t>> observed;
    for (const auto& probe : probes) {
        const auto outputs = model.outputs(probe);
        observed.push_back(outputs.value_or(std::vector<std::int64_t>{}));
    }
    // Probes whose selected exit is a trap leave the values unspecified.
    const auto trapped = [&](std::size_t p) {
        if (observed[p].empty()) {
            return true;
        }
        if (block.exits.size() <= 1) {
            return false;
        }
        const auto exit = model.selected(observed[p]);
        return exit >= block.exits.size() || block.exits[exit].kind == nvr::ExitKind::Trap;
    };

    // Candidates for each value the exits read from the graph.
    const auto expired = [&] { return std::chrono::steady_clock::now() >= options.deadline; };
    std::vector<std::pair<std::uint32_t, std::vector<ExpressionPtr>>> options_per_value;
    for (const auto& [lane, type] : model.targets()) {
        if (expired()) return unrecognized("out of time");
        const auto width = model.width_of(type);
        const auto support = model.words_of(lane, width);
        Candidates candidates(model, probes, options.extended);
        candidates.expected_from_outputs_ = [&, lane = lane, width = width](const std::vector<std::int64_t>& outputs)
            -> std::optional<std::uint64_t> { return BlockModel::word(outputs, lane, width); };
        auto found = candidates.find(type, support, [&, lane = lane, width = width](std::size_t p)
                                         -> std::optional<std::uint64_t> {
            if (trapped(p)) {
                return std::nullopt;
            }
            return BlockModel::word(observed[p], lane, width);
        });
        if (found.empty()) {
            return unrecognized("no candidate for a value");
        }
        options_per_value.emplace_back(lane, std::move(found));
    }

    // The condition between two normal exits.
    std::vector<ExpressionPtr> conditions{nullptr};
    std::optional<std::size_t> exit_when_true;
    if (normal_exits == 2) {
        if (expired()) return unrecognized("out of time");
        exit_when_true = 0;
        std::uint32_t boolean = 0;
        while (boolean < image.types.size() && image.types[boolean].kind != nvr::TypeKind::Bool) {
            ++boolean;
        }
        if (boolean == image.types.size()) {
            return unrecognized("no Boolean type for a condition");
        }
        const auto lanes = nvr::selector_lanes(block.exits.size());
        const auto support = model.words_of(block.selector_start, lanes);
        Candidates candidates(model, probes, options.extended);
        candidates.expected_from_outputs_ = [&](const std::vector<std::int64_t>& outputs)
            -> std::optional<std::uint64_t> {
            const auto exit = model.selected(outputs);
            if (exit > 1) {
                return std::nullopt;
            }
            return exit == 0 ? 1 : 0;
        };
        conditions = candidates.find(boolean, support, [&](std::size_t p) -> std::optional<std::uint64_t> {
            if (trapped(p)) {
                return std::nullopt;
            }
            return model.selected(observed[p]) == 0 ? 1 : 0;
        });
        if (conditions.empty()) {
            return unrecognized("no candidate for the exit condition");
        }
    } else if (normal_exits > 2) {
        return unrecognized("more than two normal exits");
    }

    // Try combinations, in every order of the values for blocks with checks.
    std::vector<std::size_t> choice(options_per_value.size(), 0);
    // Each replay builds the block's circuits; bound the work per block.
    std::size_t replays = 0;
    for (std::size_t attempts = 0; attempts < 64 && replays < 160; ++attempts) {
        std::vector<std::pair<std::uint32_t, ExpressionPtr>> values;
        for (std::size_t i = 0; i < options_per_value.size(); ++i) {
            values.emplace_back(options_per_value[i].first, options_per_value[i].second[choice[i]]);
        }
        auto order = values;
        do {
            for (const auto& condition : conditions) {
                if (++replays > 160) {
                    break;
                }
                if (expired()) return unrecognized("out of time");
                Checks checks;
                if (replay_matches(image, block, graph, order, condition, exit_when_true, &checks)) {
                    result.status = Recognition::Status::Lifted;
                    for (const auto& [lane, expression] : order) {
                        result.form.values[lane] = expression;
                    }
                    result.form.order = order;
                    result.form.condition = condition;
                    result.form.exit_when_true = exit_when_true;
                    result.form.checks = std::move(checks);
                    return result;
                }
            }
        } while (order.size() <= 4 &&
                 std::next_permutation(order.begin(), order.end(),
                                       [](const auto& a, const auto& b) { return a.first < b.first; }));
        std::size_t i = 0;
        for (; i < choice.size(); ++i) {
            if (++choice[i] < options_per_value[i].second.size()) {
                break;
            }
            choice[i] = 0;
        }
        if (i == choice.size()) {
            break;
        }
    }
    result.searched = true;
    result.candidates = std::move(options_per_value);
    return unrecognized("no structural match");
}

} // namespace neura::lift

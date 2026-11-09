// Checks the neuron circuits that implement integer operations by evaluating
// their graphs with NVM's checked evaluator: exhaustively for 8-bit operands
// and on boundary and pseudo-random operands for 16, 32, and 64 bits. The
// reference results are computed with ordinary C++ integer arithmetic,
// guarded so that the reference itself never overflows.

#include "backend/circuit.hpp"
#include "fixtures.hpp"

#include <functional>
#include <limits>

namespace neura::testing {
namespace {

using backend::Circuit;
using backend::Signal;
using backend::Word;

enum class Op { Add, Subtract, Multiply, Divide, Remainder, Negate, Equal, Less, ShiftLeft, ShiftRight };

const char* name(Op op) {
    switch (op) {
    case Op::Add: return "+";
    case Op::Subtract: return "-";
    case Op::Multiply: return "*";
    case Op::Divide: return "/";
    case Op::Remainder: return "%";
    case Op::Negate: return "neg";
    case Op::Equal: return "==";
    case Op::Less: return "<";
    case Op::ShiftLeft: return "<<";
    case Op::ShiftRight: return ">>";
    }
    return "?";
}

struct Expected {
    bool fails = false;
    std::uint64_t bits = 0;
};

std::uint64_t mask(unsigned width) {
    return width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

std::int64_t to_signed(std::uint64_t bits, unsigned width) {
    if (width == 64) {
        return static_cast<std::int64_t>(bits);
    }
    const auto sign = std::uint64_t{1} << (width - 1);
    return (bits & sign) != 0 ? static_cast<std::int64_t>(bits | ~mask(width)) : static_cast<std::int64_t>(bits);
}

std::int64_t signed_min(unsigned width) { return width == 64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (width - 1)); }
std::int64_t signed_max(unsigned width) { return width == 64 ? std::numeric_limits<std::int64_t>::max() : (std::int64_t{1} << (width - 1)) - 1; }

Expected signed_result(std::int64_t value, unsigned width, bool overflowed) {
    if (overflowed || value < signed_min(width) || value > signed_max(width)) {
        return {true, 0};
    }
    return {false, static_cast<std::uint64_t>(value) & mask(width)};
}

// The result of `op`, or a failure where the operation traps: overflow, a
// zero divisor, or MIN / -1. Division truncates toward zero and the remainder
// takes the dividend's sign, as C++ defines them.
Expected reference(Op op, std::uint64_t a, std::uint64_t b, unsigned width, bool is_signed) {
    if (op == Op::Equal) {
        return {false, a == b ? 1U : 0U};
    }
    if (op == Op::ShiftLeft || op == Op::ShiftRight) {
        if (b >= width) {
            return {true, 0};
        }
        if (op == Op::ShiftLeft) {
            return {false, (a << b) & mask(width)};
        }
        return {false, is_signed ? static_cast<std::uint64_t>(to_signed(a, width) >> b) & mask(width) : a >> b};
    }
    if (is_signed) {
        const auto x = to_signed(a, width);
        const auto y = to_signed(b, width);
        constexpr auto min64 = std::numeric_limits<std::int64_t>::min();
        constexpr auto max64 = std::numeric_limits<std::int64_t>::max();
        switch (op) {
        case Op::Less: return {false, x < y ? 1U : 0U};
        case Op::Add: {
            const bool overflow = (y > 0 && x > max64 - y) || (y < 0 && x < min64 - y);
            return signed_result(overflow ? 0 : x + y, width, overflow);
        }
        case Op::Subtract: {
            const bool overflow = (y < 0 && x > max64 + y) || (y > 0 && x < min64 + y);
            return signed_result(overflow ? 0 : x - y, width, overflow);
        }
        case Op::Negate:
            return signed_result(x == min64 ? 0 : -x, width, x == min64);
        case Op::Multiply: {
            if (x == 0 || y == 0) {
                return {false, 0};
            }
            const auto mx = x < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(x) : static_cast<std::uint64_t>(x);
            const auto my = y < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(y) : static_cast<std::uint64_t>(y);
            if (mx > std::numeric_limits<std::uint64_t>::max() / my) {
                return {true, 0};
            }
            const auto product = mx * my;
            const bool negative = (x < 0) != (y < 0);
            const auto limit = negative ? static_cast<std::uint64_t>(signed_max(width)) + 1
                                        : static_cast<std::uint64_t>(signed_max(width));
            if (product > limit) {
                return {true, 0};
            }
            return {false, (negative ? std::uint64_t{0} - product : product) & mask(width)};
        }
        case Op::Divide:
            if (y == 0 || (x == signed_min(width) && y == -1)) {
                return {true, 0};
            }
            return {false, static_cast<std::uint64_t>(x / y) & mask(width)};
        case Op::Remainder:
            if (y == 0) {
                return {true, 0};
            }
            // MIN % -1 is 0; the C++ expression would overflow.
            return {false, y == -1 ? 0 : static_cast<std::uint64_t>(x % y) & mask(width)};
        default:
            break;
        }
    } else {
        const auto max = mask(width);
        switch (op) {
        case Op::Less: return {false, a < b ? 1U : 0U};
        case Op::Add: return a > max - b ? Expected{true, 0} : Expected{false, a + b};
        case Op::Subtract: return a < b ? Expected{true, 0} : Expected{false, a - b};
        case Op::Negate: return a != 0 ? Expected{true, 0} : Expected{false, 0};
        case Op::Multiply:
            if (a != 0 && b > max / a) {
                return {true, 0};
            }
            return {false, a * b};
        case Op::Divide:
            return b == 0 ? Expected{true, 0} : Expected{false, a / b};
        case Op::Remainder:
            return b == 0 ? Expected{true, 0} : Expected{false, a % b};
        default:
            break;
        }
    }
    return {};
}

// A graph whose inputs are the lanes of a and then b, and whose outputs are
// the result lanes followed by the failure lane.
nvr::Graph circuit_for(Op op, unsigned width, bool is_signed) {
    Circuit circuit;
    Word a;
    Word b;
    for (unsigned i = 0; i < width; ++i) {
        a.push_back(circuit.input());
    }
    if (op != Op::Negate) {
        for (unsigned i = 0; i < width; ++i) {
            b.push_back(circuit.input());
        }
    }
    backend::Checked result;
    switch (op) {
    case Op::Add: result = backend::add(circuit, a, b, is_signed); break;
    case Op::Subtract: result = backend::subtract(circuit, a, b, is_signed); break;
    case Op::Multiply: result = backend::multiply(circuit, a, b, is_signed); break;
    case Op::Divide: result = backend::divide(circuit, a, b, is_signed); break;
    case Op::Remainder: result = backend::remainder(circuit, a, b, is_signed); break;
    case Op::Negate: result = backend::negate(circuit, a, is_signed); break;
    case Op::Equal: result = {{backend::equal(circuit, a, b)}, Signal::of(0)}; break;
    case Op::Less: result = {{backend::less(circuit, a, b, is_signed)}, Signal::of(0)}; break;
    case Op::ShiftLeft: result = backend::shift(circuit, a, b, false, false); break;
    case Op::ShiftRight: result = backend::shift(circuit, a, b, true, is_signed); break;
    }
    for (const auto& lane : result.value) {
        circuit.output(lane);
    }
    circuit.output(result.failure);
    return circuit.finish();
}

class Checker {
public:
    Checker(Op op, unsigned width, bool is_signed)
        : op_(op), width_(width), signed_(is_signed), graph_(circuit_for(op, width, is_signed)) {}

    void check_pair(std::uint64_t a, std::uint64_t b) {
        wires_.assign(graph_.input_lanes, 0);
        for (unsigned i = 0; i < width_; ++i) {
            wires_[i] = static_cast<std::int64_t>((a >> i) & 1U);
            if (op_ != Op::Negate) {
                wires_[width_ + i] = static_cast<std::int64_t>((b >> i) & 1U);
            }
        }
        if (const auto failed = nvm::evaluate_graph(graph_, wires_)) {
            fail(label(a, b) + ": node " + std::to_string(*failed) + " overflowed");
        }
        std::uint64_t bits = 0;
        const auto lanes = graph_.outputs.size() - 1;
        for (std::size_t i = 0; i < graph_.outputs.size(); ++i) {
            const auto lane = wires_[graph_.outputs[i]];
            if (lane != 0 && lane != 1) {
                fail(label(a, b) + ": output lane " + std::to_string(i) + " is " + std::to_string(lane));
            }
            if (i < lanes) {
                bits |= static_cast<std::uint64_t>(lane) << i;
            }
        }
        const bool fails = wires_[graph_.outputs.back()] == 1;
        const auto expected = reference(op_, a, b, width_, signed_);
        if (fails != expected.fails || (!fails && bits != expected.bits)) {
            fail(label(a, b) + ": circuit gives " + (fails ? "failure" : std::to_string(bits)) + ", expected " +
                 (expected.fails ? "failure" : std::to_string(expected.bits)));
        }
        ++checked_;
    }

    std::size_t checked() const { return checked_; }
    std::size_t nodes() const { return graph_.nodes.size(); }

private:
    std::string label(std::uint64_t a, std::uint64_t b) const {
        return std::string(signed_ ? "int" : "uint") + std::to_string(width_) + " " + std::to_string(a) + " " +
               name(op_) + " " + std::to_string(b);
    }

    Op op_;
    unsigned width_;
    bool signed_;
    nvr::Graph graph_;
    std::vector<std::int64_t> wires_;
    std::size_t checked_ = 0;
};

constexpr Op all_ops[] = {Op::Add, Op::Subtract, Op::Multiply, Op::Divide, Op::Remainder,
                          Op::Negate, Op::Equal, Op::Less};

// Boundary values and deterministic pseudo-random values for a width.
std::vector<std::uint64_t> samples(unsigned width, std::size_t random_count, unsigned bit_step) {
    const auto m = mask(width);
    std::vector<std::uint64_t> values{0, 1, 2, 3, 7, m, m - 1, m - 2, m >> 1, (m >> 1) + 1, (m >> 1) - 1, (m >> 1) + 2};
    for (unsigned bit = 2; bit < width; bit += bit_step) {
        values.push_back(std::uint64_t{1} << bit);
        values.push_back((std::uint64_t{1} << bit) - 1);
        values.push_back(m - (std::uint64_t{1} << bit) + 1);  // the negation of 2^bit
    }
    std::uint64_t state = 0x9e3779b97f4a7c15ULL ^ width;
    for (std::size_t i = 0; i < random_count; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        auto value = state >> (i % 3 == 0 ? 64 - width / 2 : 64 - width);
        values.push_back(value & m);
    }
    return values;
}

} // namespace

NEURA_TEST(circuits, exhaustive_eight_bit_operations) {
    for (const bool is_signed : {false, true}) {
        for (const auto op : all_ops) {
            Checker checker(op, 8, is_signed);
            for (std::uint64_t a = 0; a < 256; ++a) {
                for (std::uint64_t b = 0; b < (op == Op::Negate ? 1U : 256U); ++b) {
                    checker.check_pair(a, b);
                }
            }
        }
    }
}

NEURA_TEST(circuits, wide_operations_on_boundary_and_random_operands) {
    for (const unsigned width : {16U, 32U, 64U}) {
        // The 64-bit multiplier and divider are the largest graphs, so wider
        // types get fewer operands.
        const auto values = width == 16 ? samples(16, 64, 2) : width == 32 ? samples(32, 40, 3) : samples(64, 16, 7);
        for (const bool is_signed : {false, true}) {
            for (const auto op : all_ops) {
                Checker checker(op, width, is_signed);
                for (const auto a : values) {
                    if (op == Op::Negate) {
                        checker.check_pair(a, 0);
                        continue;
                    }
                    for (const auto b : values) {
                        checker.check_pair(a, b);
                    }
                }
            }
        }
    }
}

NEURA_TEST(circuits, shifts_cover_each_lane_and_trap_on_invalid_counts) {
    for (const bool is_signed : {false, true}) {
        for (const auto op : {Op::ShiftLeft, Op::ShiftRight}) {
            Checker byte(op, 8, is_signed);
            for (std::uint64_t value = 0; value < 256; ++value) {
                for (std::uint64_t count = 0; count <= 8; ++count) {
                    byte.check_pair(value, count);
                }
                byte.check_pair(value, 255);
            }
            Checker wide(op, 64, is_signed);
            for (const auto value : samples(64, 8, 8)) {
                for (const auto count : {0ULL, 1ULL, 32ULL, 63ULL, 64ULL, 1ULL << 63}) {
                    wide.check_pair(value, count);
                }
            }
        }
    }
}

NEURA_TEST(circuits, integer_conversions_between_every_format) {
    // Every source value of the 8- and 16-bit formats, and boundary and random
    // values of the wider ones, converted to every format. The value fits when
    // it lies in the target range; otherwise the conversion traps.
    const auto value_of = [](std::uint64_t bits, unsigned width, bool is_signed) -> std::pair<bool, std::uint64_t> {
        if (is_signed && ((bits >> (width - 1)) & 1U) != 0) {
            return {true, (~bits & mask(width)) + 1};  // magnitude of a negative value
        }
        return {false, bits};
    };
    std::size_t checked = 0;
    for (const unsigned from : {8U, 16U, 32U, 64U}) {
        for (const bool from_signed : {false, true}) {
            const auto sources = from <= 16 ? std::vector<std::uint64_t>{} : samples(from, 64, 1);
            std::vector<std::uint64_t> values = sources;
            if (from <= 16) {
                for (std::uint64_t v = 0; v <= mask(from); ++v) {
                    values.push_back(v);
                }
            }
            for (const unsigned to : {8U, 16U, 32U, 64U}) {
                for (const bool to_signed : {false, true}) {
                    Circuit circuit;
                    Word a;
                    for (unsigned i = 0; i < from; ++i) {
                        a.push_back(circuit.input());
                    }
                    const auto result = backend::convert_integer(circuit, a, from_signed, to, to_signed);
                    for (const auto& lane : result.value) {
                        circuit.output(lane);
                    }
                    circuit.output(result.failure);
                    const auto graph = circuit.finish();
                    std::vector<std::int64_t> wires;
                    for (const auto bits : values) {
                        wires.assign(graph.input_lanes, 0);
                        for (unsigned i = 0; i < from; ++i) {
                            wires[i] = static_cast<std::int64_t>((bits >> i) & 1U);
                        }
                        check(!nvm::evaluate_graph(graph, wires), "conversion graph overflowed");
                        std::uint64_t out = 0;
                        for (unsigned i = 0; i < to; ++i) {
                            out |= static_cast<std::uint64_t>(wires[graph.outputs[i]]) << i;
                        }
                        const bool fails = wires[graph.outputs.back()] == 1;
                        const auto [negative, magnitude] = value_of(bits, from, from_signed);
                        const bool fits = negative ? to_signed && magnitude <= (std::uint64_t{1} << (to - 1))
                                                   : magnitude <= (to_signed ? mask(to - 1) : mask(to));
                        const auto expected = (negative ? std::uint64_t{0} - magnitude : magnitude) & mask(to);
                        if (fails != !fits || (fits && out != expected)) {
                            fail(std::string(from_signed ? "int" : "uint") + std::to_string(from) + " " +
                                 std::to_string(bits) + " as " + (to_signed ? "int" : "uint") + std::to_string(to) +
                                 ": circuit gives " + (fails ? "failure" : std::to_string(out)));
                            return;
                        }
                        ++checked;
                    }
                }
            }
        }
    }
    check(checked > 1'000'000, "conversion checks ran");
}

NEURA_TEST(circuits, gadgets_fold_constant_operands) {
    // x + 1 needs no neuron for constant lanes, and comparing with a constant
    // needs one neuron per varying bit pair plus one for the conjunction.
    Circuit circuit;
    Word x;
    for (int i = 0; i < 8; ++i) {
        x.push_back(circuit.input());
    }
    const auto sum = backend::add(circuit, x, backend::constant_word(0, 8), false);
    for (std::size_t i = 0; i < x.size(); ++i) {
        check(sum.value[i] == x[i], "adding zero passes the lanes through");
    }
    check(sum.failure.is_constant(0), "adding zero cannot overflow");
    const auto same = backend::equal(circuit, x, backend::constant_word(0x5a, 8));
    circuit.output(same);
    check_equal(circuit.finish().nodes.size(), std::size_t{1}, "neurons for x == 0x5a");
}

NEURA_TEST(circuits, profile_one_arithmetic_is_checked) {
    // evaluate_graph reports the first node whose sum leaves int64.
    nvr::Graph graph;
    graph.input_lanes = 1;
    graph.nodes = {{nvr::Activation::Identity, std::numeric_limits<std::int64_t>::max(), {{0, 1}}},
                   {nvr::Activation::Relu, 0, {{0, 1}}}};
    graph.outputs = {1, 2};
    std::vector<std::int64_t> wires{0};
    check(!nvm::evaluate_graph(graph, wires), "no overflow for input 0");
    wires = {1};
    const auto failed = nvm::evaluate_graph(graph, wires);
    check(failed && *failed == 0, "the bias plus one overflows node 0");
    graph.nodes = {{nvr::Activation::Identity, 0, {{0, std::numeric_limits<std::int64_t>::min()}}}};
    wires = {-1};
    check(nvm::evaluate_graph(graph, wires).has_value(), "INT64_MIN * -1 overflows");
    graph.nodes = {{nvr::Activation::Relu, -5, {{0, 3}}}};
    wires = {1};
    check(!nvm::evaluate_graph(graph, wires) && wires[1] == 0, "ReLU clamps -2 to 0");
    wires = {2};
    check(!nvm::evaluate_graph(graph, wires) && wires[1] == 1, "ReLU passes 1");
    graph.nodes[0].activation = nvr::Activation::Identity;
    wires = {1};
    check(!nvm::evaluate_graph(graph, wires) && wires[1] == -2, "identity keeps -2");
}

} // namespace neura::testing

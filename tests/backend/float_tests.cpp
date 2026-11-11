// The floating-point circuits against independent references
// (float_reference.hpp): exhaustively for the 6- and 8-lane formats e3m2 and
// e4m3, which exercise every rounding, subnormal, overflow, and special case;
// exhaustively for every float16 and bfloat16 value in unary operations and
// conversions; and on special, boundary, and pseudo-random operands for the
// binary operations of the 16-, 32-, and 64-bit formats.

#include "circuit/float_circuit.hpp"
#include "fixtures.hpp"
#include "ir-optimization/float_reference.hpp"
#include "neura-ir/float.hpp"
#include "nvm/scalar_text.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <functional>

namespace neura::testing {
namespace {

using backend::Circuit;
using backend::Signal;
using backend::Word;
namespace ref = floats;

ref::Format reference_format(FloatFormat format) {
    return {format.exponent_bits, format.fraction_bits};
}

constexpr FloatFormat e3m2{3, 2};
constexpr FloatFormat e4m3{4, 3};

std::uint64_t mask(unsigned width) {
    return width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

// A graph over `inputs` words of the given widths whose outputs are the lanes
// of the built word, followed by an optional failure lane.
class Graph {
public:
    Graph(std::vector<unsigned> inputs, const std::function<backend::Checked(Circuit&, const std::vector<Word>&)>& build)
        : widths_(std::move(inputs)) {
        Circuit circuit;
        std::vector<Word> words;
        for (const auto width : widths_) {
            Word word;
            for (unsigned i = 0; i < width; ++i) {
                word.push_back(circuit.input());
            }
            words.push_back(std::move(word));
        }
        const auto result = build(circuit, words);
        output_width_ = static_cast<unsigned>(result.value.size());
        for (const auto& lane : result.value) {
            circuit.output(lane);
        }
        circuit.output(result.failure);
        graph_ = circuit.finish();
    }

    // Returns the output word; `failed` receives the failure lane.
    std::uint64_t operator()(std::initializer_list<std::uint64_t> values, bool* failed = nullptr) {
        wires_.assign(graph_.input_lanes, 0);
        std::size_t lane = 0;
        auto width = widths_.begin();
        for (const auto value : values) {
            for (unsigned i = 0; i < *width; ++i) {
                wires_[lane++] = static_cast<std::int64_t>((value >> i) & 1U);
            }
            ++width;
        }
        if (nvm::evaluate_graph(graph_, wires_)) {
            fail("a float graph overflowed");
        }
        std::uint64_t result = 0;
        for (unsigned i = 0; i < output_width_; ++i) {
            const auto bit = wires_[graph_.outputs[i]];
            if (bit != 0 && bit != 1) {
                fail("a float graph produced a non-binary lane");
            }
            result |= static_cast<std::uint64_t>(bit) << i;
        }
        if (failed) {
            *failed = wires_[graph_.outputs.back()] == 1;
        }
        return result;
    }

    std::size_t neurons() const { return graph_.nodes.size(); }

private:
    std::vector<unsigned> widths_;
    unsigned output_width_ = 0;
    nvr::Graph graph_;
    std::vector<std::int64_t> wires_;
};

backend::Checked unchecked(Word word) {
    return {std::move(word), Signal::of(0)};
}

Graph binary_graph(ref::Op op, FloatFormat format) {
    const auto w = format.width();
    return Graph({w, w}, [&](Circuit& c, const std::vector<Word>& in) {
        switch (op) {
        case ref::Op::Add: return unchecked(backend::float_add(c, in[0], in[1], format));
        case ref::Op::Subtract: return unchecked(backend::float_subtract(c, in[0], in[1], format));
        case ref::Op::Multiply: return unchecked(backend::float_multiply(c, in[0], in[1], format));
        case ref::Op::Divide: return unchecked(backend::float_divide(c, in[0], in[1], format));
        }
        return unchecked({});
    });
}

const char* name(ref::Op op) {
    switch (op) {
    case ref::Op::Add: return "+";
    case ref::Op::Subtract: return "-";
    case ref::Op::Multiply: return "*";
    case ref::Op::Divide: return "/";
    }
    return "?";
}

constexpr ref::Op all_ops[] = {ref::Op::Add, ref::Op::Subtract, ref::Op::Multiply, ref::Op::Divide};

// Special and boundary encodings plus pseudo-random ones.
std::vector<std::uint64_t> samples(FloatFormat format, std::size_t random_count) {
    const auto w = format.width();
    const auto f = format.fraction_bits;
    const auto sign = std::uint64_t{1} << (w - 1);
    const auto exponent_ones = ((std::uint64_t{1} << format.exponent_bits) - 1) << f;
    const auto one = format.bias() << f;
    std::vector<std::uint64_t> values{
        0, 1, 2, 3, (std::uint64_t{1} << f) - 1, std::uint64_t{1} << f, (std::uint64_t{1} << f) + 1,
        one, one + 1, one - 1, one + (std::uint64_t{1} << (f - 1)), one + (std::uint64_t{1} << f),
        exponent_ones - 1, exponent_ones - (std::uint64_t{1} << f), exponent_ones, exponent_ones + 1,
        exponent_ones | (std::uint64_t{1} << (f - 1)), (format.bias() + f) << f, (format.bias() - f - 2) << f,
    };
    const auto count = values.size();
    for (std::size_t i = 0; i < count; ++i) {
        values.push_back(values[i] | sign);
    }
    std::uint64_t state = 0x2545F4914F6CDD1DULL ^ w;
    for (std::size_t i = 0; i < random_count; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        values.push_back(state & mask(w));
    }
    return values;
}

std::string hex(std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

void check_binary(FloatFormat format, const std::vector<std::uint64_t>& values, bool exact_reference) {
    const auto r = reference_format(format);
    for (const auto op : all_ops) {
        auto graph = binary_graph(op, format);
        for (const auto a : values) {
            for (const auto b : values) {
                const auto actual = graph({a, b});
                const auto expected = ref::binary(op, a, b, r);
                if (actual != expected) {
                    fail(std::to_string(format.width()) + "-bit " + hex(a) + " " + name(op) + " " + hex(b) +
                         ": circuit " + hex(actual) + ", reference " + hex(expected));
                }
                if (exact_reference) {
                    const auto exact = ref::exact_binary(op, a, b, r);
                    if (exact != expected) {
                        fail("the references disagree on " + hex(a) + " " + name(op) + " " + hex(b) + ": exact " +
                             hex(exact) + ", double " + hex(expected));
                    }
                }
            }
        }
    }
}

std::vector<std::uint64_t> every(FloatFormat format) {
    std::vector<std::uint64_t> values;
    for (std::uint64_t v = 0; v <= mask(format.width()); ++v) {
        values.push_back(v);
    }
    return values;
}

void check_comparisons(FloatFormat format, const std::vector<std::uint64_t>& values) {
    const auto w = format.width();
    const auto r = reference_format(format);
    using backend::FloatComparison;
    const std::pair<FloatComparison, ref::Compare> ops[] = {
        {FloatComparison::Equal, ref::Compare::Equal},     {FloatComparison::NotEqual, ref::Compare::NotEqual},
        {FloatComparison::Less, ref::Compare::Less},       {FloatComparison::LessEqual, ref::Compare::LessEqual},
        {FloatComparison::Greater, ref::Compare::Greater}, {FloatComparison::GreaterEqual, ref::Compare::GreaterEqual},
    };
    for (const auto& [op, reference] : ops) {
        Graph graph({w, w}, [&](Circuit& c, const std::vector<Word>& in) {
            return unchecked({backend::float_compare(c, in[0], in[1], format, op)});
        });
        for (const auto a : values) {
            for (const auto b : values) {
                const bool actual = graph({a, b}) != 0;
                if (actual != ref::compare(reference, a, b, r)) {
                    fail(std::to_string(w) + "-bit comparison " + std::to_string(static_cast<int>(op)) + " of " +
                         hex(a) + " and " + hex(b) + " gives " + (actual ? "true" : "false"));
                }
            }
        }
    }
}

} // namespace

NEURA_TEST(floats, tiny_formats_exhaustively) {
    // Every pair of e3m2 and e4m3 values, against both references.
    check_binary(e3m2, every(e3m2), true);
    check_binary(e4m3, every(e4m3), true);
    check_comparisons(e4m3, every(e4m3));
}

NEURA_TEST(floats, sixteen_bit_binary_operations) {
    for (const auto format : {binary16, bfloat16}) {
        check_binary(format, samples(format, 100), false);
        check_comparisons(format, samples(format, 60));
    }
}

NEURA_TEST(floats, wide_binary_operations) {
    check_binary(binary32, samples(binary32, 60), false);
    check_comparisons(binary32, samples(binary32, 40));
    check_binary(binary64, samples(binary64, 30), false);
    check_comparisons(binary64, samples(binary64, 30));
}

NEURA_TEST(floats, sixteen_bit_unary_operations_exhaustively) {
    // Negation, widening and narrowing conversions, and conversions to and
    // from integers, for every float16 and bfloat16 encoding.
    for (const auto format : {binary16, bfloat16}) {
        const auto r = reference_format(format);
        const auto w = format.width();
        Graph to32({w}, [&](Circuit& c, const std::vector<Word>& in) {
            return unchecked(backend::float_convert(c, in[0], format, binary32));
        });
        Graph to64({w}, [&](Circuit& c, const std::vector<Word>& in) {
            return unchecked(backend::float_convert(c, in[0], format, binary64));
        });
        const auto other = format == binary16 ? bfloat16 : binary16;
        Graph across({w}, [&](Circuit& c, const std::vector<Word>& in) {
            return unchecked(backend::float_convert(c, in[0], format, other));
        });
        Graph to_int32({w}, [&](Circuit& c, const std::vector<Word>& in) {
            return backend::float_to_integer(c, in[0], format, 32, true);
        });
        Graph to_uint8({w}, [&](Circuit& c, const std::vector<Word>& in) {
            return backend::float_to_integer(c, in[0], format, 8, false);
        });
        Graph from_int16({16}, [&](Circuit& c, const std::vector<Word>& in) {
            return unchecked(backend::float_from_integer(c, in[0], true, format));
        });
        Graph from_uint16({16}, [&](Circuit& c, const std::vector<Word>& in) {
            return unchecked(backend::float_from_integer(c, in[0], false, format));
        });
        for (std::uint64_t v = 0; v <= mask(w); ++v) {
            const auto label = std::to_string(w) + "-bit " + hex(v);
            check_equal(backend::float_negate(backend::constant_word(v, w)) == backend::constant_word(v ^ (1ULL << (w - 1)), w),
                        true, label + " negation flips the sign");
            check_equal(to32({v}), ref::convert(v, r, reference_format(binary32)), label + " to float32");
            check_equal(to64({v}), ref::convert(v, r, reference_format(binary64)), label + " to float64");
            check_equal(across({v}), ref::convert(v, r, reference_format(other)), label + " across 16-bit formats");
            const double value = ref::decode(v, r);
            const double truncated = std::trunc(value);
            bool failed = false;
            const auto as_int32 = to_int32({v}, &failed);
            const bool int32_fits = !std::isnan(value) && truncated >= -2147483648.0 && truncated <= 2147483647.0;
            check_equal(failed, !int32_fits, label + " to int32 traps exactly when out of range");
            if (int32_fits) {
                check_equal(as_int32, static_cast<std::uint64_t>(static_cast<std::int64_t>(truncated)) & 0xffffffffU,
                            label + " to int32");
            }
            const auto as_uint8 = to_uint8({v}, &failed);
            const bool uint8_fits = !std::isnan(value) && truncated >= 0 && truncated <= 255;
            check_equal(failed, !uint8_fits, label + " to uint8 traps exactly when out of range");
            if (uint8_fits) {
                check_equal(as_uint8, static_cast<std::uint64_t>(truncated), label + " to uint8");
            }
            const auto as_signed = static_cast<double>(static_cast<std::int16_t>(static_cast<std::uint16_t>(v)));
            check_equal(from_int16({v}), ref::encode(as_signed, r), label + " from int16");
            check_equal(from_uint16({v}), ref::encode(static_cast<double>(v), r), label + " from uint16");
        }
    }
}

NEURA_TEST(floats, wide_conversions) {
    // Narrowing from float64 and float32 with rounding, overflow, and
    // underflow; integer conversions at the boundaries of int64 and uint64.
    const auto values64 = samples(binary64, 200);
    Graph to32({64}, [&](Circuit& c, const std::vector<Word>& in) {
        return unchecked(backend::float_convert(c, in[0], binary64, binary32));
    });
    Graph to16({64}, [&](Circuit& c, const std::vector<Word>& in) {
        return unchecked(backend::float_convert(c, in[0], binary64, binary16));
    });
    Graph to_int64({64}, [&](Circuit& c, const std::vector<Word>& in) {
        return backend::float_to_integer(c, in[0], binary64, 64, true);
    });
    Graph to_uint64({64}, [&](Circuit& c, const std::vector<Word>& in) {
        return backend::float_to_integer(c, in[0], binary64, 64, false);
    });
    std::vector<std::uint64_t> extra;
    for (const double d : {9223372036854775807.0, -9223372036854775808.0, 18446744073709551615.0, -0.5, 0.75,
                           -1.0, 4294967296.5, 1e-310, 65504.0, 65520.0, 65519.99, 6e-8, 3e-8, 2.9e-8}) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &d, 8);
        extra.push_back(bits);
    }
    auto all = values64;
    all.insert(all.end(), extra.begin(), extra.end());
    for (const auto v : all) {
        const auto label = "float64 " + hex(v);
        check_equal(to32({v}), ref::convert(v, reference_format(binary64), reference_format(binary32)),
                    label + " to float32");
        check_equal(to16({v}), ref::convert(v, reference_format(binary64), reference_format(binary16)),
                    label + " to float16");
        double value = 0;
        std::memcpy(&value, &v, 8);
        const double truncated = std::trunc(value);
        bool failed = false;
        const auto signed_result = to_int64({v}, &failed);
        const bool signed_fits = !std::isnan(value) && truncated >= -9223372036854775808.0 && truncated < 9223372036854775808.0;
        check_equal(failed, !signed_fits, label + " to int64 traps exactly when out of range");
        if (signed_fits) {
            check_equal(signed_result, static_cast<std::uint64_t>(static_cast<std::int64_t>(truncated)), label + " to int64");
        }
        const auto unsigned_result = to_uint64({v}, &failed);
        const bool unsigned_fits = !std::isnan(value) && truncated >= 0 && truncated < 18446744073709551616.0;
        check_equal(failed, !unsigned_fits, label + " to uint64 traps exactly when out of range");
        if (unsigned_fits) {
            check_equal(unsigned_result, static_cast<std::uint64_t>(truncated), label + " to uint64");
        }
    }
    Graph from_int64({64}, [&](Circuit& c, const std::vector<Word>& in) {
        return unchecked(backend::float_from_integer(c, in[0], true, binary64));
    });
    Graph from_uint64_to32({64}, [&](Circuit& c, const std::vector<Word>& in) {
        return unchecked(backend::float_from_integer(c, in[0], false, binary32));
    });
    for (const auto v : samples(binary64, 200)) {
        const double d = static_cast<double>(static_cast<std::int64_t>(v));
        std::uint64_t expected = 0;
        std::memcpy(&expected, &d, 8);
        check_equal(from_int64({v}), expected, "int64 " + hex(v) + " to float64");
        const float f = static_cast<float>(v);
        std::uint32_t expected32 = 0;
        std::memcpy(&expected32, &f, 4);
        check_equal(from_uint64_to32({v}), std::uint64_t{expected32}, "uint64 " + hex(v) + " to float32");
    }
}

NEURA_TEST(floats, literals_round_exactly) {
    // Decimal literals against the C library's correctly rounded strtod and
    // strtof, and the 16-bit formats against rounding of the exact double.
    const char* literals[] = {
        "0.1", "1.5", "3.14159265358979323846264338327950288", "1e308", "1.7976931348623157e308",
        "2.2250738585072014e-308", "4.9406564584124654e-324", "5e-324", "1e-320", "123456789012345678901234567890",
        "0.000000000000000000000000000000000000001", "65504", "65519.9999", "6.1e-5", "5.960464477539063e-8",
        "3.4028234663852886e38", "1.401298464324817e-45", "9007199254740993", "0.30000000000000004", "1e23",
        "8.98846567431158e307", "2.47e-324", "3e-324",
    };
    for (const auto* text : literals) {
        const auto parsed64 = parse_float_literal(text, binary64);
        const double expected = std::strtod(text, nullptr);
        std::uint64_t bits = 0;
        std::memcpy(&bits, &expected, 8);
        if (std::isinf(expected)) {
            check(parsed64.error == FloatLiteralError::Overflow, std::string(text) + " overflows float64");
        } else if (expected == 0) {
            check(parsed64.error == FloatLiteralError::Underflow, std::string(text) + " underflows float64");
        } else {
            check(!parsed64.error && parsed64.bits == bits, std::string(text) + " as float64");
        }
        const auto parsed32 = parse_float_literal(text, binary32);
        const float expected32 = std::strtof(text, nullptr);
        std::uint32_t bits32 = 0;
        std::memcpy(&bits32, &expected32, 4);
        if (std::isinf(expected32)) {
            check(parsed32.error == FloatLiteralError::Overflow, std::string(text) + " overflows float32");
        } else if (expected32 == 0) {
            check(parsed32.error == FloatLiteralError::Underflow, std::string(text) + " underflows float32");
        } else {
            check(!parsed32.error && parsed32.bits == bits32, std::string(text) + " as float32");
        }
    }
    // Ties and boundaries that double rounding would get wrong: 1 + 2^-11 +
    // 2^-40 is just above the float16 halfway point between 1 and 1 + 2^-10.
    check_equal(parse_float_literal("1.00048828125000090949470177292823791503906250", binary16).bits,
                std::uint64_t{0x3c01}, "just above a float16 tie rounds up");
    check_equal(parse_float_literal("1.00048828125", binary16).bits, std::uint64_t{0x3c00},
                "an exact float16 tie rounds to even");
    check_equal(parse_float_literal("-0.0", binary64).bits, std::uint64_t{0x8000000000000000ULL}, "-0.0");
    check(parse_float_literal("65520", binary16).error == FloatLiteralError::Overflow, "65520 overflows float16");
    check_equal(parse_float_literal("65519", binary16).bits, std::uint64_t{0x7bff}, "65519 rounds to 65504");
    check(parse_float_literal("1e-8", binary16).error == FloatLiteralError::Underflow, "1e-8 underflows float16");
    check_equal(parse_float_literal("3.0e-8", binary16).bits, std::uint64_t{1}, "3e-8 rounds up to the least subnormal");
    check_equal(parse_float_literal("1", bfloat16).bits, std::uint64_t{0x3f80}, "1 as bfloat16");
    check(parse_float_literal("1x", binary64).error == FloatLiteralError::Malformed, "1x is malformed");
}

NEURA_TEST(floats, shortest_scalar_text_round_trips) {
    const auto check_pattern = [](FloatFormat format, nvr::TypeKind kind, std::uint64_t bits) {
        const nvr::Type type{kind, static_cast<std::uint8_t>(format.width())};
        const auto text = nvm::scalar_text(type, bits);
        const auto exponent_mask = mask(format.exponent_bits);
        const auto exponent = (bits >> format.fraction_bits) & exponent_mask;
        if (exponent == exponent_mask) {
            const bool nan = (bits & mask(format.fraction_bits)) != 0;
            const bool negative = (bits >> (format.width() - 1)) != 0;
            check_equal(text, std::string(nan ? "NaN" : negative ? "-Infinity" : "Infinity"), "special value text");
            return;
        }
        const auto parsed = parse_float_literal(text, format);
        check(!parsed.error && parsed.bits == bits, "round trip of " + hex(bits) + " through " + text);
    };
    for (std::uint64_t bits = 0; bits < 65536; ++bits) {
        check_pattern(binary16, nvr::TypeKind::Float, bits);
        check_pattern(bfloat16, nvr::TypeKind::BFloat, bits);
    }
    for (const auto format : {binary32, binary64}) {
        for (const auto bits : samples(format, 256)) {
            check_pattern(format, nvr::TypeKind::Float, bits);
        }
    }
    const nvr::Type f64{nvr::TypeKind::Float, 64};
    check_equal(nvm::scalar_text(f64, 1), std::string("5.0e-324"), "least subnormal shortest digits");
    check_equal(nvm::scalar_text(f64, parse_float_literal("1e-4", binary64).bits), std::string("0.0001"),
                "fixed lower threshold");
    check_equal(nvm::scalar_text(f64, parse_float_literal("1e16", binary64).bits), std::string("1.0e16"),
                "scientific upper threshold");
}

} // namespace neura::testing

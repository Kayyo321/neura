#include "float_circuit.hpp"

#include <algorithm>
#include <bit>
#include <vector>

namespace neura::backend {
namespace {

// The fields of an encoded value, with the significand and exponent in the
// form arithmetic needs: an explicit leading bit, and exponent 1 for
// subnormals and zeros.
struct Unpacked {
    Signal sign;
    Word exponent;     // biased; exponent_bits lanes
    Word significand;  // fraction_bits + 1 lanes
    Signal nan;
    Signal infinity;
    Signal zero;
};

Word slice(const Word& word, std::size_t first, std::size_t count) {
    return Word(word.begin() + static_cast<std::ptrdiff_t>(first),
                word.begin() + static_cast<std::ptrdiff_t>(first + count));
}

Word inverted(const Word& word) {
    Word result;
    for (const auto& bit : word) {
        result.push_back(Circuit::logical_not(bit));
    }
    return result;
}

Word choose_word(Circuit& circuit, const Signal& select, const Word& when_true, const Word& when_false) {
    Word result;
    for (std::size_t i = 0; i < when_true.size(); ++i) {
        result.push_back(circuit.choose(select, when_true[i], when_false[i]));
    }
    return result;
}

Word resized(Word word, std::size_t width, const Signal& fill = Signal::of(0)) {
    word.resize(width, fill);
    return word;
}

Signal any_bit(Circuit& circuit, const Word& word) {
    return circuit.any(word);
}

Signal none(Circuit& circuit, const Word& word) {
    return Circuit::logical_not(circuit.any(word));
}

Unpacked unpack(Circuit& circuit, const Word& word, FloatFormat format) {
    const auto f = format.fraction_bits;
    const auto e = format.exponent_bits;
    Unpacked result;
    result.sign = word[f + e];
    const auto fraction = slice(word, 0, f);
    const auto field = slice(word, f, e);
    const auto exponent_zero = none(circuit, field);
    const auto exponent_ones = circuit.all(field);
    const auto fraction_zero = none(circuit, fraction);
    result.nan = circuit.logical_and(exponent_ones, Circuit::logical_not(fraction_zero));
    result.infinity = circuit.logical_and(exponent_ones, fraction_zero);
    result.zero = circuit.logical_and(exponent_zero, fraction_zero);
    result.significand = fraction;
    result.significand.push_back(Circuit::logical_not(exponent_zero));
    result.exponent = field;
    result.exponent[0] = circuit.logical_or(field[0], exponent_zero);
    return result;
}

Word constant_float(std::uint64_t bits, FloatFormat format) {
    return constant_word(bits, format.width());
}

Word infinity(const Signal& sign, FloatFormat format) {
    auto word = constant_word(((std::uint64_t{1} << format.exponent_bits) - 1) << format.fraction_bits, format.width());
    word.back() = sign;
    return word;
}

Word signed_zero(const Signal& sign, FloatFormat format) {
    auto word = constant_word(0, format.width());
    word.back() = sign;
    return word;
}

// A signed exponent of `width` lanes holding `value` (two's complement).
Word signed_constant(std::int64_t value, std::size_t width) {
    return constant_word(static_cast<std::uint64_t>(value), static_cast<unsigned>(width));
}

Word add_words(Circuit& circuit, const Word& a, const Word& b) {
    return add_with_carry(circuit, a, b, Signal::of(0)).value;
}

Word subtract_words(Circuit& circuit, const Word& a, const Word& b) {
    return add_with_carry(circuit, a, inverted(b), Signal::of(1)).value;
}

// x >= 1 for a signed word: not negative and not zero.
Signal at_least_one(Circuit& circuit, const Word& x) {
    return circuit.logical_and(Circuit::logical_not(x.back()), any_bit(circuit, x));
}

// Shifts `word` right by the unsigned `amount`, ORing every lane shifted out
// into `sticky`. Amounts of the word's width or more shift everything out.
Word shift_right_sticky(Circuit& circuit, Word word, const Word& amount, Signal& sticky) {
    const auto width = word.size();
    std::vector<Signal> large;
    std::size_t stages = 0;
    for (std::size_t k = 0; k < amount.size(); ++k) {
        if (k >= 63 || (std::size_t{1} << k) >= width) {
            large.push_back(amount[k]);
        } else {
            stages = k + 1;
        }
    }
    if (!large.empty()) {
        const auto beyond = circuit.any(large);
        sticky = circuit.logical_or(sticky, circuit.logical_and(beyond, any_bit(circuit, word)));
        word = choose_word(circuit, beyond, constant_word(0, static_cast<unsigned>(width)), word);
    }
    for (std::size_t k = 0; k < stages; ++k) {
        const auto distance = std::size_t{1} << k;
        Word shifted;
        for (std::size_t i = 0; i < width; ++i) {
            shifted.push_back(i + distance < width ? word[i + distance] : Signal::of(0));
        }
        const auto lost = any_bit(circuit, slice(word, 0, distance));
        sticky = circuit.logical_or(sticky, circuit.logical_and(amount[k], lost));
        word = choose_word(circuit, amount[k], shifted, word);
    }
    return word;
}

// Shifts left so the top lane is set, at most `limit` places when a limit
// applies: while the exponent stays at least 1 (`limited`), or without bound.
// Returns the shift applied through `exponent`.
Word normalize_left(Circuit& circuit, Word word, Word& exponent, bool limited) {
    const auto width = word.size();
    std::size_t top = 0;
    while ((std::size_t{1} << (top + 1)) < width) {
        ++top;
    }
    for (std::size_t k = top + 1; k-- > 0;) {
        const auto distance = std::size_t{1} << k;
        if (distance >= width) {
            continue;
        }
        const auto leading_zeros = none(circuit, slice(word, width - distance, distance));
        const auto candidate =
            subtract_words(circuit, exponent, signed_constant(static_cast<std::int64_t>(distance), exponent.size()));
        const auto shift = limited ? circuit.logical_and(leading_zeros, at_least_one(circuit, candidate))
                                   : leading_zeros;
        Word shifted;
        for (std::size_t i = 0; i < width; ++i) {
            shifted.push_back(i >= distance ? word[i - distance] : Signal::of(0));
        }
        word = choose_word(circuit, shift, shifted, word);
        exponent = choose_word(circuit, shift, candidate, exponent);
    }
    return word;
}

// Rounds and encodes the value (significand / 2^(n-1)) * 2^(exponent - bias),
// where the significand has n lanes and the exponent is a signed word in the
// output format's bias. `sticky` stands for nonzero lanes below the
// significand.
Word round_pack(Circuit& circuit, FloatFormat format, const Signal& sign, Word exponent, Word significand,
                Signal sticky) {
    const auto f = format.fraction_bits;
    const auto e = format.exponent_bits;
    // At least the significand, a guard lane, and one more.
    if (significand.size() < f + 3) {
        Word padded(f + 3 - significand.size(), Signal::of(0));
        padded.insert(padded.end(), significand.begin(), significand.end());
        significand = std::move(padded);
    }
    const auto n = significand.size();
    significand = normalize_left(circuit, significand, exponent, true);

    // Values below the normal range keep exponent 1 and shift right.
    const auto below = Circuit::logical_not(at_least_one(circuit, exponent));
    const auto one = signed_constant(1, exponent.size());
    const auto distance = subtract_words(circuit, one, exponent);
    const auto amount = choose_word(circuit, below, distance, constant_word(0, static_cast<unsigned>(exponent.size())));
    significand = shift_right_sticky(circuit, significand, amount, sticky);
    exponent = choose_word(circuit, below, one, exponent);

    const auto kept = slice(significand, n - (f + 1), f + 1);
    const auto guard = significand[n - f - 2];
    sticky = circuit.logical_or(sticky, any_bit(circuit, slice(significand, 0, n - f - 2)));
    const auto round_up = circuit.logical_and(guard, circuit.logical_or(sticky, kept[0]));
    const auto rounded = add_with_carry(circuit, kept, constant_word(0, f + 1), round_up);
    const auto carry = rounded.failure;
    // A carry out leaves the significand at 1.000...: the exponent grows by
    // one and the fraction is zero.
    exponent = add_with_carry(circuit, exponent, constant_word(0, static_cast<unsigned>(exponent.size())), carry).value;
    const auto normal = circuit.logical_or(carry, rounded.value[f]);
    Word fraction;
    for (std::size_t i = 0; i < f; ++i) {
        fraction.push_back(circuit.logical_and(Circuit::logical_not(carry), rounded.value[i]));
    }
    const auto maximum = signed_constant((std::int64_t{1} << e) - 1, exponent.size());
    const auto overflow = Circuit::logical_not(less(circuit, exponent, maximum, true));

    Word result = fraction;
    for (std::size_t i = 0; i < e; ++i) {
        result.push_back(circuit.logical_and(normal, exponent[i]));
    }
    result.push_back(sign);
    return choose_word(circuit, overflow, infinity(sign, format), result);
}

// Width for signed exponent arithmetic: room for sums and differences of two
// biased exponents and for normalization shifts.
std::size_t exponent_width(FloatFormat format) {
    return format.exponent_bits + 3;
}

Word widen_exponent(const Word& exponent, std::size_t width) {
    return resized(exponent, width);
}

Word select_special(Circuit& circuit, FloatFormat format, const Signal& nan, const Signal& infinite,
                    const Signal& infinity_sign, const Word& otherwise) {
    const auto with_infinity = choose_word(circuit, infinite, infinity(infinity_sign, format), otherwise);
    return choose_word(circuit, nan, constant_float(format.canonical_nan(), format), with_infinity);
}

Signal magnitude_less(Circuit& circuit, const Word& a, const Word& b, FloatFormat format) {
    const auto lanes = format.width() - 1;
    return less(circuit, slice(a, 0, lanes), slice(b, 0, lanes), false);
}

} // namespace

Word float_add(Circuit& circuit, const Word& a, const Word& b, FloatFormat format) {
    const auto f = format.fraction_bits;
    const auto x_width = exponent_width(format);
    const auto ua = unpack(circuit, a, format);
    const auto ub = unpack(circuit, b, format);
    // Order the operands by magnitude, so the difference of exponents is
    // nonnegative and a subtraction cannot go below zero.
    const auto swap = magnitude_less(circuit, a, b, format);
    const auto sign_x = circuit.choose(swap, ub.sign, ua.sign);
    const auto sign_y = circuit.choose(swap, ua.sign, ub.sign);
    const auto exponent_x = choose_word(circuit, swap, ub.exponent, ua.exponent);
    const auto exponent_y = choose_word(circuit, swap, ua.exponent, ub.exponent);
    const auto significand_x = choose_word(circuit, swap, ub.significand, ua.significand);
    const auto significand_y = choose_word(circuit, swap, ua.significand, ub.significand);

    // Three lanes below the significands: guard, round, and sticky.
    Word x(3, Signal::of(0));
    x.insert(x.end(), significand_x.begin(), significand_x.end());
    Word y(3, Signal::of(0));
    y.insert(y.end(), significand_y.begin(), significand_y.end());
    const auto difference = subtract_words(circuit, exponent_x, exponent_y);
    Signal sticky = Signal::of(0);
    y = shift_right_sticky(circuit, y, difference, sticky);
    y[0] = circuit.logical_or(y[0], sticky);

    const auto subtract = circuit.logical_xor(sign_x, sign_y);
    x.push_back(Signal::of(0));
    y.push_back(Signal::of(0));
    Word addend;
    for (const auto& bit : y) {
        addend.push_back(circuit.logical_xor(bit, subtract));
    }
    const auto sum = add_with_carry(circuit, x, addend, subtract).value;
    const auto exact_zero = none(circuit, sum);
    // x - x is +0; -0 + -0 is -0.
    const auto sign = circuit.choose(exact_zero, circuit.logical_and(sign_x, sign_y), sign_x);
    auto exponent = add_words(circuit, widen_exponent(exponent_x, x_width), signed_constant(1, x_width));
    const auto rounded = round_pack(circuit, format, sign, exponent, sum, Signal::of(0));

    const auto both_infinite = circuit.logical_and(ua.infinity, ub.infinity);
    const auto nan = circuit.any({ua.nan, ub.nan, circuit.logical_and(both_infinite, subtract)});
    const auto infinite = circuit.logical_or(ua.infinity, ub.infinity);
    const auto infinity_sign = circuit.choose(ua.infinity, ua.sign, ub.sign);
    (void)f;
    return select_special(circuit, format, nan, infinite, infinity_sign, rounded);
}

Word float_negate(const Word& a) {
    auto result = a;
    result.back() = Circuit::logical_not(result.back());
    return result;
}

Word float_subtract(Circuit& circuit, const Word& a, const Word& b, FloatFormat format) {
    return float_add(circuit, a, float_negate(b), format);
}

Word float_multiply(Circuit& circuit, const Word& a, const Word& b, FloatFormat format) {
    const auto x_width = exponent_width(format);
    const auto ua = unpack(circuit, a, format);
    const auto ub = unpack(circuit, b, format);
    const auto sign = circuit.logical_xor(ua.sign, ub.sign);
    const auto product = multiply_full(circuit, ua.significand, ub.significand);
    // product / 2^(2f+1) * 2^(ea + eb - bias + 1 - bias)
    auto exponent = add_words(circuit, widen_exponent(ua.exponent, x_width), widen_exponent(ub.exponent, x_width));
    exponent = add_words(circuit, exponent,
                         signed_constant(1 - static_cast<std::int64_t>(format.bias()), x_width));
    const auto rounded = round_pack(circuit, format, sign, exponent, product, Signal::of(0));
    const auto nan = circuit.any({ua.nan, ub.nan, circuit.logical_and(ua.infinity, ub.zero),
                                  circuit.logical_and(ua.zero, ub.infinity)});
    const auto infinite = circuit.logical_or(ua.infinity, ub.infinity);
    return select_special(circuit, format, nan, infinite, sign, rounded);
}

Word float_divide(Circuit& circuit, const Word& a, const Word& b, FloatFormat format) {
    const auto f = format.fraction_bits;
    const auto x_width = exponent_width(format);
    const auto ua = unpack(circuit, a, format);
    const auto ub = unpack(circuit, b, format);
    const auto sign = circuit.logical_xor(ua.sign, ub.sign);
    // Normalize both significands, so that their quotient lies in (1/2, 2).
    auto exponent_a = widen_exponent(ua.exponent, x_width);
    auto exponent_b = widen_exponent(ub.exponent, x_width);
    const auto dividend = normalize_left(circuit, ua.significand, exponent_a, false);
    const auto divisor = normalize_left(circuit, ub.significand, exponent_b, false);

    // Long division producing f + 3 quotient lanes, the first being the
    // units lane, and a sticky lane for a nonzero remainder.
    const auto k = f + 2;
    auto remainder = resized(dividend, f + 2);
    const auto wide_divisor = resized(divisor, f + 2);
    Word quotient(k + 1, Signal::of(0));
    for (std::size_t step = 0; step <= k; ++step) {
        const auto difference = add_with_carry(circuit, remainder, inverted(wide_divisor), Signal::of(1));
        const auto fits = difference.failure;  // no borrow
        quotient[k - step] = fits;
        remainder = choose_word(circuit, fits, difference.value, remainder);
        if (step < k) {
            Word doubled{Signal::of(0)};
            doubled.insert(doubled.end(), remainder.begin(), remainder.end() - 1);
            remainder = std::move(doubled);
        }
    }
    const auto sticky = any_bit(circuit, remainder);
    auto exponent = subtract_words(circuit, exponent_a, exponent_b);
    exponent = add_words(circuit, exponent, signed_constant(static_cast<std::int64_t>(format.bias()), x_width));
    const auto rounded = round_pack(circuit, format, sign, exponent, quotient, sticky);

    const auto nan = circuit.any({ua.nan, ub.nan, circuit.logical_and(ua.zero, ub.zero),
                                  circuit.logical_and(ua.infinity, ub.infinity)});
    const auto infinite = circuit.logical_or(ua.infinity, ub.zero);
    const auto zero = circuit.logical_or(ua.zero, ub.infinity);
    const auto finite = choose_word(circuit, zero, signed_zero(sign, format), rounded);
    return select_special(circuit, format, nan, infinite, sign, finite);
}

Signal float_compare(Circuit& circuit, const Word& a, const Word& b, FloatFormat format, FloatComparison op) {
    const auto ua = unpack(circuit, a, format);
    const auto ub = unpack(circuit, b, format);
    const auto unordered = circuit.logical_or(ua.nan, ub.nan);
    const auto both_zero = circuit.logical_and(ua.zero, ub.zero);
    const auto lanes = format.width() - 1;
    const auto same_bits = equal(circuit, a, b);
    const auto equal_values =
        circuit.logical_and(Circuit::logical_not(unordered), circuit.logical_or(same_bits, both_zero));
    const auto less_than = [&](const Word& x, const Word& y, const Signal& sx, const Signal& sy) {
        // Negative before positive; among negatives the larger magnitude is
        // smaller. Equal zeros are not ordered.
        const auto magnitude_xy = less(circuit, slice(x, 0, lanes), slice(y, 0, lanes), false);
        const auto magnitude_yx = less(circuit, slice(y, 0, lanes), slice(x, 0, lanes), false);
        const auto different_signs = circuit.logical_and(sx, Circuit::logical_not(sy));
        const auto both_positive =
            circuit.logical_and(circuit.logical_and(Circuit::logical_not(sx), Circuit::logical_not(sy)), magnitude_xy);
        const auto both_negative = circuit.logical_and(circuit.logical_and(sx, sy), magnitude_yx);
        const auto ordered = circuit.any({different_signs, both_positive, both_negative});
        return circuit.logical_and(circuit.logical_and(Circuit::logical_not(unordered), Circuit::logical_not(both_zero)),
                                   ordered);
    };
    switch (op) {
    case FloatComparison::Equal: return equal_values;
    case FloatComparison::NotEqual: return Circuit::logical_not(equal_values);
    case FloatComparison::Less: return less_than(a, b, ua.sign, ub.sign);
    case FloatComparison::Greater: return less_than(b, a, ub.sign, ua.sign);
    case FloatComparison::LessEqual: return circuit.logical_or(less_than(a, b, ua.sign, ub.sign), equal_values);
    case FloatComparison::GreaterEqual: return circuit.logical_or(less_than(b, a, ub.sign, ua.sign), equal_values);
    }
    return Signal::of(0);
}

Word float_from_integer(Circuit& circuit, const Word& a, bool is_signed, FloatFormat format) {
    const auto width = a.size();
    const auto x_width = std::max(exponent_width(format), static_cast<std::size_t>(std::bit_width(width)) + 2);
    const Signal sign = is_signed ? a.back() : Signal::of(0);
    // |a| as an unsigned word; the minimum's magnitude is 2^(width-1).
    Word magnitude;
    Signal carry = sign;
    for (const auto& bit : a) {
        const auto flipped = circuit.logical_xor(bit, sign);
        const auto next = circuit.logical_and(flipped, carry);
        magnitude.push_back(circuit.compact(flipped + carry - 2 * next));
        carry = next;
    }
    const auto exponent =
        signed_constant(static_cast<std::int64_t>(format.bias() + width - 1), std::max(x_width, std::size_t{12}));
    return round_pack(circuit, format, sign, exponent, magnitude, Signal::of(0));
}

Checked float_to_integer(Circuit& circuit, const Word& a, FloatFormat format, unsigned width, bool is_signed) {
    const auto f = format.fraction_bits;
    const auto unpacked = unpack(circuit, a, format);
    const auto x_width = std::max<std::size_t>(format.exponent_bits + 2, std::bit_width(f + width + format.bias()) + 2);
    const auto exponent = widen_exponent(unpacked.exponent, x_width);
    // magnitude = floor(significand * 2^(exponent - bias - f)), computed as
    // (significand << width) >> (f + width + bias - exponent).
    Word placed(width, Signal::of(0));
    placed.insert(placed.end(), unpacked.significand.begin(), unpacked.significand.end());
    const auto distance =
        subtract_words(circuit, signed_constant(static_cast<std::int64_t>(f + width + format.bias()), x_width), exponent);
    const auto negative_distance = distance.back();
    Signal sticky = Signal::of(0);
    const auto shifted = shift_right_sticky(circuit, placed, distance, sticky);
    const auto magnitude = slice(shifted, 0, width);
    // The exponent is at least bias + width exactly when the magnitude needs
    // more than `width` lanes.
    const auto limit =
        signed_constant(static_cast<std::int64_t>(format.bias() + width), x_width);
    const auto too_large = Circuit::logical_not(less(circuit, exponent, limit, true));
    std::vector<Signal> failures{unpacked.nan, unpacked.infinity, too_large, negative_distance};
    Word value;
    if (is_signed) {
        // Magnitudes up to 2^(width-1) - 1, or exactly 2^(width-1) when
        // negative.
        const auto top = magnitude[width - 1];
        const auto rest_zero = none(circuit, slice(magnitude, 0, width - 1));
        failures.push_back(circuit.logical_and(top, Circuit::logical_not(circuit.logical_and(unpacked.sign, rest_zero))));
        Signal carry = unpacked.sign;
        for (const auto& bit : magnitude) {
            const auto flipped = circuit.logical_xor(bit, unpacked.sign);
            const auto next = circuit.logical_and(flipped, carry);
            value.push_back(circuit.compact(flipped + carry - 2 * next));
            carry = next;
        }
    } else {
        failures.push_back(circuit.logical_and(unpacked.sign, any_bit(circuit, magnitude)));
        value = magnitude;
    }
    return {std::move(value), circuit.any(failures)};
}

Word float_convert(Circuit& circuit, const Word& a, FloatFormat from, FloatFormat to) {
    if (from == to) {
        return a;
    }
    const auto x_width = std::max(exponent_width(from), exponent_width(to)) + 1;
    const auto unpacked = unpack(circuit, a, from);
    auto exponent = widen_exponent(unpacked.exponent, x_width);
    exponent = add_words(circuit, exponent,
                         signed_constant(static_cast<std::int64_t>(to.bias()) - static_cast<std::int64_t>(from.bias()),
                                         x_width));
    const auto rounded = round_pack(circuit, to, unpacked.sign, exponent, unpacked.significand, Signal::of(0));
    return select_special(circuit, to, unpacked.nan, unpacked.infinity, unpacked.sign, rounded);
}

} // namespace neura::backend

#include "circuit.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace neura::backend {

namespace {

Signal combine(const Signal& a, std::int64_t fa, const Signal& b, std::int64_t fb) {
    Signal result;
    result.constant = fa * a.constant + fb * b.constant;
    result.terms.reserve(a.terms.size() + b.terms.size());
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.terms.size() || j < b.terms.size()) {
        if (j == b.terms.size() || (i < a.terms.size() && a.terms[i].first < b.terms[j].first)) {
            result.terms.emplace_back(a.terms[i].first, fa * a.terms[i].second);
            ++i;
        } else if (i == a.terms.size() || b.terms[j].first < a.terms[i].first) {
            result.terms.emplace_back(b.terms[j].first, fb * b.terms[j].second);
            ++j;
        } else {
            const auto weight = fa * a.terms[i].second + fb * b.terms[j].second;
            if (weight != 0) {
                result.terms.emplace_back(a.terms[i].first, weight);
            }
            ++i;
            ++j;
        }
    }
    std::erase_if(result.terms, [](const auto& term) { return term.second == 0; });
    return result;
}

} // namespace

Signal operator+(const Signal& left, const Signal& right) { return combine(left, 1, right, 1); }
Signal operator-(const Signal& left, const Signal& right) { return combine(left, 1, right, -1); }
Signal operator*(std::int64_t factor, const Signal& signal) { return combine(signal, factor, Signal{}, 0); }
Signal operator+(const Signal& left, std::int64_t right) { return left + Signal::of(right); }
Signal operator-(std::int64_t left, const Signal& right) { return Signal::of(left) - right; }

Circuit::Gadget::Gadget(Circuit& circuit, std::initializer_list<const Signal*> inputs, bool opaque)
    : circuit_(&circuit) {
    if (!circuit.recording_) return;
    std::vector<Signal> signals;
    signals.reserve(inputs.size());
    for (const auto* input : inputs) signals.push_back(*input);
    open(std::move(signals), opaque);
}

Circuit::Gadget::Gadget(Circuit& circuit, const std::vector<Signal>& inputs, bool opaque) : circuit_(&circuit) {
    if (!circuit.recording_) return;
    open(inputs, opaque);
}

void Circuit::Gadget::open(std::vector<Signal> inputs, bool opaque) {
    auto& c = *circuit_;
    // Inside an opaque instance, its neurons stay its own.
    if (!c.open_.empty() && c.open_.back().second) return;
    c.open_.emplace_back(static_cast<std::uint32_t>(c.instances_.size()), opaque);
    c.instances_.push_back(std::move(inputs));
    open_ = true;
}

Circuit::Gadget::~Gadget() {
    if (open_) circuit_->open_.pop_back();
}

void Circuit::created() {
    if (recording_) neuron_instance_.push_back(open_.empty() ? nvr::absent : open_.back().first);
}

GadgetAdvice Circuit::gadget_advice() const {
    // The neurons and wires of finish().
    std::vector<std::uint8_t> needed(neurons_.size(), 0);
    for (auto wire : outputs_) {
        if ((wire & neuron_wire) != 0) needed[wire & ~neuron_wire] = 1;
    }
    for (std::size_t i = neurons_.size(); i-- > 0;) {
        if (!needed[i]) continue;
        for (const auto& [wire, weight] : neurons_[i].input.terms) {
            if ((wire & neuron_wire) != 0) needed[wire & ~neuron_wire] = 1;
        }
    }
    std::vector<std::uint32_t> position(neurons_.size(), nvr::absent);
    std::uint32_t kept = 0;
    for (std::size_t i = 0; i < neurons_.size(); ++i) {
        if (needed[i]) position[i] = inputs_ + kept++;
    }
    GadgetAdvice advice;
    if (!recording_ || neuron_instance_.size() != neurons_.size()) return advice;
    advice.instance.reserve(kept);
    for (std::size_t i = 0; i < neurons_.size(); ++i) {
        if (needed[i]) advice.instance.push_back(neuron_instance_[i]);
    }
    advice.inputs.reserve(instances_.size());
    for (const auto& inputs : instances_) {
        auto& mapped = advice.inputs.emplace_back();
        for (const auto& signal : inputs) {
            auto& wires = mapped.emplace_back();
            for (const auto& [wire, weight] : signal.terms) {
                const auto final = (wire & neuron_wire) == 0 ? wire : position[wire & ~neuron_wire];
                if (final != nvr::absent) wires.push_back(final);
            }
        }
    }
    return advice;
}

Signal Circuit::input() {
    return Signal::wire(inputs_++);
}

Signal Circuit::relu(const Signal& value) {
    if (value.is_constant()) {
        return Signal::of(std::max<std::int64_t>(0, value.constant));
    }
    neurons_.push_back({nvr::Activation::Relu, value});
    created();
    return Signal::wire(neuron_wire | static_cast<WireId>(neurons_.size() - 1));
}

WireId Circuit::materialize(const Signal& value) {
    if (value.constant == 0 && value.terms.size() == 1 && value.terms.front().second == 1) {
        return value.terms.front().first;
    }
    neurons_.push_back({nvr::Activation::Identity, value});
    created();
    return neuron_wire | static_cast<WireId>(neurons_.size() - 1);
}

Signal Circuit::compact(const Signal& value) {
    constexpr std::size_t limit = 4;
    return value.terms.size() > limit ? Signal::wire(materialize(value)) : value;
}

Signal Circuit::logical_and(const Signal& a, const Signal& b) {
    const Gadget gadget(*this, {&a, &b});
    if (a.is_constant()) {
        return a.constant == 0 ? Signal::of(0) : b;
    }
    if (b.is_constant()) {
        return b.constant == 0 ? Signal::of(0) : a;
    }
    if (a == b) {
        return a;
    }
    return relu(a + b - Signal::of(1));
}

Signal Circuit::logical_or(const Signal& a, const Signal& b) {
    const Gadget gadget(*this, {&a, &b});
    if (a.is_constant()) {
        return a.constant != 0 ? Signal::of(1) : b;
    }
    if (b.is_constant()) {
        return b.constant != 0 ? Signal::of(1) : a;
    }
    if (a == b) {
        return a;
    }
    return 1 - relu(1 - a - b);
}

Signal Circuit::logical_xor(const Signal& a, const Signal& b) {
    const Gadget gadget(*this, {&a, &b});
    if (a.is_constant()) {
        return a.constant == 0 ? b : logical_not(b);
    }
    if (b.is_constant()) {
        return b.constant == 0 ? a : logical_not(a);
    }
    if (a == b) {
        return Signal::of(0);
    }
    return compact(a + b - 2 * logical_and(a, b));
}

Signal Circuit::choose(const Signal& select, const Signal& when_true, const Signal& when_false) {
    const Gadget gadget(*this, {&select, &when_true, &when_false}, true);
    if (select.is_constant()) {
        return select.constant != 0 ? when_true : when_false;
    }
    if (when_true == when_false) {
        return when_true;
    }
    if (when_true.is_constant() && when_false.is_constant()) {
        if (when_true.constant == 1 && when_false.constant == 0) {
            return select;
        }
        if (when_true.constant == 0 && when_false.constant == 1) {
            return logical_not(select);
        }
    }
    return logical_and(select, when_true) + logical_and(logical_not(select), when_false);
}

Signal Circuit::all(const std::vector<Signal>& bits) {
    const Gadget gadget(*this, bits);
    std::vector<const Signal*> open;
    for (const auto& bit : bits) {
        if (bit.is_constant(0)) {
            return Signal::of(0);
        }
        if (!bit.is_constant()) {
            open.push_back(&bit);
        }
    }
    if (open.empty()) {
        return Signal::of(1);
    }
    if (open.size() == 1) {
        return *open.front();
    }
    Signal sum;
    for (const auto* bit : open) {
        sum = sum + *bit;
    }
    return relu(sum - Signal::of(static_cast<std::int64_t>(open.size()) - 1));
}

Signal Circuit::any(const std::vector<Signal>& bits) {
    const Gadget gadget(*this, bits);
    std::vector<const Signal*> open;
    for (const auto& bit : bits) {
        if (bit.is_constant()) {
            if (bit.constant != 0) {
                return Signal::of(1);
            }
            continue;
        }
        open.push_back(&bit);
    }
    if (open.empty()) {
        return Signal::of(0);
    }
    if (open.size() == 1) {
        return *open.front();
    }
    Signal sum;
    for (const auto* bit : open) {
        sum = sum + *bit;
    }
    return 1 - relu(1 - sum);
}

Signal Circuit::at_least(const Signal& value, std::int64_t threshold) {
    const Gadget gadget(*this, {&value});
    if (value.is_constant()) {
        return Signal::of(value.constant >= threshold ? 1 : 0);
    }
    return relu(value - Signal::of(threshold - 1)) - relu(value - Signal::of(threshold));
}

std::uint32_t Circuit::output(const Signal& value) {
    outputs_.push_back(materialize(value));
    return static_cast<std::uint32_t>(outputs_.size() - 1);
}

nvr::Graph Circuit::finish() const {
    std::vector<std::uint8_t> needed(neurons_.size(), 0);
    for (auto wire : outputs_) {
        if ((wire & neuron_wire) != 0) {
            needed[wire & ~neuron_wire] = 1;
        }
    }
    for (std::size_t i = neurons_.size(); i-- > 0;) {
        if (!needed[i]) {
            continue;
        }
        for (const auto& [wire, weight] : neurons_[i].input.terms) {
            if ((wire & neuron_wire) != 0) {
                needed[wire & ~neuron_wire] = 1;
            }
        }
    }
    std::vector<std::uint32_t> position(neurons_.size(), 0);
    std::uint32_t kept = 0;
    for (std::size_t i = 0; i < neurons_.size(); ++i) {
        if (needed[i]) {
            position[i] = inputs_ + kept++;
        }
    }
    auto final_wire = [&](WireId wire) {
        return (wire & neuron_wire) == 0 ? wire : position[wire & ~neuron_wire];
    };

    nvr::Graph graph;
    graph.input_lanes = inputs_;
    graph.nodes.reserve(kept);
    for (std::size_t i = 0; i < neurons_.size(); ++i) {
        if (!needed[i]) {
            continue;
        }
        nvr::Node node;
        node.activation = neurons_[i].activation;
        node.bias = neurons_[i].input.constant;
        // Renumbering preserves order, so edges stay sorted by source.
        for (const auto& [wire, weight] : neurons_[i].input.terms) {
            node.edges.push_back({final_wire(wire), weight});
        }
        graph.nodes.push_back(std::move(node));
    }
    for (auto wire : outputs_) {
        graph.outputs.push_back(final_wire(wire));
    }
    return graph;
}

Word constant_word(std::uint64_t bits, unsigned width) {
    Word word;
    word.reserve(width);
    for (unsigned i = 0; i < width; ++i) {
        // Words can be wider than 64 lanes (the float circuits shift
        // significands in wide words); lanes past bit 63 are zero.
        word.push_back(Signal::of(i < 64 ? static_cast<std::int64_t>((bits >> i) & 1U) : 0));
    }
    return word;
}

bool constant_value(const Word& word, std::uint64_t& bits) {
    bits = 0;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (!word[i].is_constant()) {
            return false;
        }
        bits |= static_cast<std::uint64_t>(word[i].constant & 1) << i;
    }
    return true;
}

namespace {

struct BitSum {
    Signal sum;
    Signal carry;
};

// One column of an adder: the sum bit and carry of three bits.
BitSum full_add(Circuit& circuit, const Signal& a, const Signal& b, const Signal& c) {
    const Circuit::Gadget gadget(circuit, {&a, &b, &c}, true);
    std::int64_t known = 0;
    std::vector<const Signal*> open;
    for (const auto* bit : {&a, &b, &c}) {
        if (bit->is_constant()) {
            known += bit->constant;
        } else {
            open.push_back(bit);
        }
    }
    const Signal total = a + b + c;
    Signal carry;
    if (open.empty()) {
        carry = Signal::of(known >= 2 ? 1 : 0);
    } else if (open.size() == 1) {
        carry = known >= 2 ? Signal::of(1) : known == 1 ? *open[0] : Signal::of(0);
    } else if (open.size() == 2) {
        carry = known == 0 ? circuit.logical_and(*open[0], *open[1]) : circuit.logical_or(*open[0], *open[1]);
    } else {
        carry = circuit.at_least(total, 2);
    }
    return {circuit.compact(total - 2 * carry), carry};
}

struct Addition {
    Word sum;
    // carries[i] enters column i; carries[width] leaves the top column.
    std::vector<Signal> carries;
};

Addition ripple(Circuit& circuit, const Word& a, const Word& b, Signal carry) {
    Addition result;
    result.carries.push_back(carry);
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto column = full_add(circuit, a[i], b[i], carry);
        result.sum.push_back(column.sum);
        carry = column.carry;
        result.carries.push_back(carry);
    }
    return result;
}

Word invert(const Word& word) {
    Word result;
    result.reserve(word.size());
    for (const auto& bit : word) {
        result.push_back(Circuit::logical_not(bit));
    }
    return result;
}

Signal signed_overflow(Circuit& circuit, const Addition& addition) {
    const auto width = addition.sum.size();
    return circuit.logical_xor(addition.carries[width - 1], addition.carries[width]);
}

// `negative ? -x : x` modulo 2^width, used for magnitudes and results.
Word conditional_negate(Circuit& circuit, const Word& x, const Signal& negative) {
    if (negative.is_constant(0)) {
        return x;
    }
    Word result;
    result.reserve(x.size());
    Signal carry = negative;
    for (const auto& bit : x) {
        const auto flipped = circuit.logical_xor(bit, negative);
        const auto next = circuit.logical_and(flipped, carry);
        result.push_back(circuit.compact(flipped + carry - 2 * next));
        carry = next;
    }
    return result;
}

struct Column {
    Signal bit;
    Signal carry;
    std::int64_t carry_max = 0;
    std::vector<Signal> carry_bits;
};

// Splits an integer-valued signal in [0, maximum] into its low bit and the
// remaining value divided by two. `parts`, when given, are the signals that
// sum to `value` (a multiplier column's carry and partial products), which
// gadget recording names as the inputs of the column's sum.
Column split(Circuit& circuit, Signal value, std::int64_t maximum, const std::vector<Signal>* parts = nullptr) {
    Column result;
    if (maximum <= 1) {
        result.bit = value;
        result.carry = Signal::of(0);
        return result;
    }
    // One identity neuron sums the column, so each threshold below reads
    // that wire instead of every partial product.
    const Circuit::Gadget column = parts ? Circuit::Gadget(circuit, *parts) : Circuit::Gadget(circuit, {&value});
    value = circuit.compact(value);
    const Circuit::Gadget digits(circuit, {&value}, true);
    const auto bits = static_cast<int>(std::bit_width(static_cast<std::uint64_t>(maximum)));
    for (int t = bits - 1; t >= 1; --t) {
        const auto place = std::int64_t{1} << t;
        const auto digit = circuit.at_least(value, place);
        value = value - place * digit;
        result.carry = result.carry + (place / 2) * digit;
        result.carry_bits.push_back(digit);
    }
    result.bit = circuit.compact(value);
    result.carry_max = maximum / 2;
    return result;
}

Checked unsigned_multiply(Circuit& circuit, const Word& a, const Word& b) {
    const auto width = a.size();
    Checked result;
    Signal carry;
    std::int64_t carry_max = 0;
    std::vector<Signal> carry_bits;
    std::vector<Signal> parts;
    for (std::size_t k = 0; k < width; ++k) {
        Signal column = carry;
        std::int64_t maximum = carry_max;
        parts.assign(circuit.recording() ? 1 : 0, carry);
        for (std::size_t i = 0; i <= k; ++i) {
            const auto product = circuit.logical_and(a[k - i], b[i]);
            if (!product.is_constant(0)) {
                column = column + product;
                maximum += 1;
                if (circuit.recording()) parts.push_back(product);
            }
        }
        auto split_column = split(circuit, column, maximum, circuit.recording() ? &parts : nullptr);
        result.value.push_back(split_column.bit);
        carry = split_column.carry;
        carry_max = split_column.carry_max;
        carry_bits = std::move(split_column.carry_bits);
    }
    // The product reaches 2^width when a partial product lands at or above
    // that column, or when the low columns carry into it.
    std::vector<Signal> high = carry_bits;
    Signal suffix = Signal::of(0);
    std::vector<Signal> suffixes(width + 1, Signal::of(0));
    for (std::size_t m = width; m-- > 0;) {
        suffix = circuit.logical_or(a[m], suffix);
        suffixes[m] = suffix;
    }
    for (std::size_t i = 1; i < width; ++i) {
        high.push_back(circuit.logical_and(b[i], suffixes[width - i]));
    }
    result.failure = circuit.any(high);
    return result;
}

struct Division {
    Word quotient;
    Word remainder;
};

// Restoring division, one quotient bit per column from the top.
Division unsigned_divide(Circuit& circuit, const Word& a, const Word& b) {
    const auto width = a.size();
    Division result;
    result.quotient.assign(width, Signal::of(0));
    Word remainder = constant_word(0, static_cast<unsigned>(width));
    Word divisor = b;
    divisor.push_back(Signal::of(0));
    const auto inverted = invert(divisor);
    for (std::size_t i = width; i-- > 0;) {
        Word shifted;
        shifted.push_back(a[i]);
        shifted.insert(shifted.end(), remainder.begin(), remainder.end());
        const auto difference = ripple(circuit, shifted, inverted, Signal::of(1));
        const auto fits = difference.carries.back();
        result.quotient[i] = fits;
        for (std::size_t k = 0; k < width; ++k) {
            remainder[k] = circuit.choose(fits, difference.sum[k], shifted[k]);
        }
    }
    result.remainder = remainder;
    return result;
}

} // namespace

Checked add_with_carry(Circuit& circuit, const Word& a, const Word& b, const Signal& carry) {
    auto sum = ripple(circuit, a, b, carry);
    return {std::move(sum.sum), sum.carries.back()};
}

Word multiply_full(Circuit& circuit, const Word& a, const Word& b) {
    // The column multiplier of unsigned_multiply, continued through every
    // column of the product.
    const auto width = a.size() + b.size();
    Word result;
    Signal carry;
    std::int64_t carry_max = 0;
    std::vector<Signal> parts;
    for (std::size_t k = 0; k < width; ++k) {
        Signal column = carry;
        std::int64_t maximum = carry_max;
        parts.assign(circuit.recording() ? 1 : 0, carry);
        for (std::size_t i = 0; i < b.size(); ++i) {
            if (k < i || k - i >= a.size()) {
                continue;
            }
            const auto product = circuit.logical_and(a[k - i], b[i]);
            if (!product.is_constant(0)) {
                column = column + product;
                maximum += 1;
                if (circuit.recording()) parts.push_back(product);
            }
        }
        auto split_column = split(circuit, column, maximum, circuit.recording() ? &parts : nullptr);
        result.push_back(split_column.bit);
        carry = split_column.carry;
        carry_max = split_column.carry_max;
    }
    return result;
}

Checked add(Circuit& circuit, const Word& a, const Word& b, bool is_signed) {
    auto sum = ripple(circuit, a, b, Signal::of(0));
    Signal failure = is_signed ? signed_overflow(circuit, sum) : sum.carries.back();
    return {std::move(sum.sum), failure};
}

Checked subtract(Circuit& circuit, const Word& a, const Word& b, bool is_signed) {
    auto difference = ripple(circuit, a, invert(b), Signal::of(1));
    Signal failure = is_signed ? signed_overflow(circuit, difference)
                               : Circuit::logical_not(difference.carries.back());
    return {std::move(difference.sum), failure};
}

Checked negate(Circuit& circuit, const Word& a, bool is_signed) {
    return subtract(circuit, constant_word(0, static_cast<unsigned>(a.size())), a, is_signed);
}

Checked multiply(Circuit& circuit, const Word& a, const Word& b, bool is_signed) {
    if (!is_signed) {
        return unsigned_multiply(circuit, a, b);
    }
    const auto width = a.size();
    const auto& sign_a = a.back();
    const auto& sign_b = b.back();
    const auto product = unsigned_multiply(circuit, conditional_negate(circuit, a, sign_a),
                                           conditional_negate(circuit, b, sign_b));
    const auto negative = circuit.logical_xor(sign_a, sign_b);
    // A negative result may reach -2^(width-1); a positive one stays below
    // 2^(width-1).
    const std::vector<Signal> low(product.value.begin(), product.value.end() - 1);
    const auto beyond_minimum = circuit.logical_or(Circuit::logical_not(negative), circuit.any(low));
    const auto overflow = circuit.logical_or(product.failure,
                                             circuit.logical_and(product.value[width - 1], beyond_minimum));
    return {conditional_negate(circuit, product.value, negative), overflow};
}

Checked divide(Circuit& circuit, const Word& a, const Word& b, bool is_signed) {
    const auto zero_divisor = circuit.all(invert(b));
    if (!is_signed) {
        auto division = unsigned_divide(circuit, a, b);
        return {std::move(division.quotient), zero_divisor};
    }
    // Dividing magnitudes truncates toward zero. The only quotient out of
    // range is 2^(width-1) with a positive sign, from MIN / -1.
    const auto width = a.size();
    const auto& sign_a = a.back();
    const auto& sign_b = b.back();
    auto division = unsigned_divide(circuit, conditional_negate(circuit, a, sign_a),
                                    conditional_negate(circuit, b, sign_b));
    const auto negative = circuit.logical_xor(sign_a, sign_b);
    const auto overflow = circuit.logical_and(Circuit::logical_not(negative), division.quotient[width - 1]);
    return {conditional_negate(circuit, division.quotient, negative), circuit.logical_or(zero_divisor, overflow)};
}

Checked remainder(Circuit& circuit, const Word& a, const Word& b, bool is_signed) {
    const auto zero_divisor = circuit.all(invert(b));
    if (!is_signed) {
        auto division = unsigned_divide(circuit, a, b);
        return {std::move(division.remainder), zero_divisor};
    }
    // |a| mod |b| is below |b| <= 2^(width-1), so it fits, and it takes the
    // dividend's sign.
    const auto& sign_a = a.back();
    const auto& sign_b = b.back();
    auto division = unsigned_divide(circuit, conditional_negate(circuit, a, sign_a),
                                    conditional_negate(circuit, b, sign_b));
    return {conditional_negate(circuit, division.remainder, sign_a), zero_divisor};
}

Checked convert_integer(Circuit& circuit, const Word& a, bool from_signed, unsigned width, bool to_signed) {
    const auto from = a.size();
    const Signal sign = from_signed ? a.back() : Signal::of(0);
    const auto extended = [&](std::size_t i) { return i < from ? a[i] : sign; };
    Checked result;
    for (std::size_t i = 0; i < width; ++i) {
        result.value.push_back(extended(i));
    }
    // The value fits when every source bit at or above the target's top
    // magnitude bit equals the source's sign, and a negative value is only
    // converted to a signed type.
    std::vector<Signal> out_of_range;
    const std::size_t first = to_signed ? width - 1 : width;
    const std::size_t magnitude_bits = from_signed ? from - 1 : from;
    for (std::size_t i = first; i < magnitude_bits; ++i) {
        out_of_range.push_back(from_signed ? circuit.logical_xor(a[i], sign) : a[i]);
    }
    if (from_signed && !to_signed) {
        out_of_range.push_back(sign);
    }
    result.failure = circuit.any(out_of_range);
    return result;
}

Checked shift(Circuit& circuit, Word word, const Word& count, bool right, bool sign_extend) {
    const auto width = word.size();
    const auto fill = right && sign_extend ? word.back() : Signal::of(0);
    std::size_t bit = 0;
    // Each stage shifts by one power of two. Valid integer widths are powers
    // of two, so any set count lane above those stages makes the count invalid.
    for (std::size_t distance = 1; bit < count.size() && distance < width; distance *= 2, ++bit) {
        if (count[bit].is_constant(0)) {
            continue;
        }
        Word next;
        next.reserve(width);
        for (std::size_t i = 0; i < width; ++i) {
            const auto shifted = right ? (i + distance < width ? word[i + distance] : fill)
                                       : (i >= distance ? word[i - distance] : fill);
            next.push_back(circuit.choose(count[bit], shifted, word[i]));
        }
        word = std::move(next);
    }
    const Word high(count.begin() + static_cast<std::ptrdiff_t>(bit), count.end());
    return {std::move(word), circuit.any(high)};
}

Signal equal(Circuit& circuit, const Word& a, const Word& b) {
    std::vector<Signal> same;
    same.reserve(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        same.push_back(Circuit::logical_not(circuit.logical_xor(a[i], b[i])));
    }
    return circuit.all(same);
}

Signal less(Circuit& circuit, const Word& a, const Word& b, bool is_signed) {
    // a < b exactly when a - b borrows. Flipping the sign bits maps signed
    // order onto unsigned order.
    Word left = a;
    Word right = b;
    if (is_signed) {
        left.back() = Circuit::logical_not(left.back());
        right.back() = Circuit::logical_not(right.back());
    }
    Signal carry = Signal::of(1);
    const auto inverted = invert(right);
    for (std::size_t i = 0; i < left.size(); ++i) {
        carry = full_add(circuit, left[i], inverted[i], carry).carry;
    }
    return Circuit::logical_not(carry);
}

} // namespace neura::backend

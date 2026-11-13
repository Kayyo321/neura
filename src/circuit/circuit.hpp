#pragma once

#include "nvr/image.hpp"

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace neura::backend {

// Identifies a wire while a graph is under construction. Input lanes and
// neurons are numbered in separate spaces so that inputs can be added after
// neurons; finish() renumbers them into the NVR wire order.
using WireId = std::uint32_t;
inline constexpr WireId neuron_wire = 0x80000000U;

// An affine combination of wires, `constant + sum(weight * wire)`. Signals
// are free: only the neurons they feed are stored in the graph. Gadgets
// below take signals whose value is known to be 0 or 1 ("bits") or a small
// nonnegative integer, and they keep every intermediate sum far inside the
// checked signed 64-bit range of graph profile 1.
struct Signal {
    // Sorted by wire, with nonzero weights.
    std::vector<std::pair<WireId, std::int64_t>> terms;
    std::int64_t constant = 0;

    static Signal of(std::int64_t value) { return Signal{{}, value}; }
    static Signal wire(WireId id) { return Signal{{{id, 1}}, 0}; }

    bool is_constant() const { return terms.empty(); }
    bool is_constant(std::int64_t value) const { return terms.empty() && constant == value; }

    bool operator==(const Signal&) const = default;
};

Signal operator+(const Signal& left, const Signal& right);
Signal operator-(const Signal& left, const Signal& right);
Signal operator*(std::int64_t factor, const Signal& signal);
Signal operator+(const Signal& left, std::int64_t right);
Signal operator-(std::int64_t left, const Signal& right);

// Lanes of a fixed-width value, least significant first.
using Word = std::vector<Signal>;

// Which gadget instance built each neuron of a finished graph, and the
// signals that instance was given: where the overflow analysis of src/lift
// may look first for the wires that bound a neuron. It is advice only; the
// analysis establishes every fact itself.
struct GadgetAdvice {
    // By graph node: the instance that built it, or nvr::absent.
    std::vector<std::uint32_t> instance;
    // By instance: its input signals, each as the graph wires of its terms.
    // A wire of a dropped neuron is left out.
    std::vector<std::vector<std::vector<std::uint32_t>>> inputs;
};

class Circuit {
public:
    // Records, from now on, the gadget instance of each new neuron
    // (gadget_advice). Recording changes no neuron, wire, or output; it is
    // off unless asked for.
    void record_gadgets() { recording_ = true; }
    bool recording() const { return recording_; }

    // A gadget instance, open while the object lives: the neurons created
    // meanwhile belong to it, unless an instance opened inside it is open.
    // An opaque instance keeps every neuron created inside it, including
    // those of gadgets it calls. Without recording it does nothing.
    class Gadget {
    public:
        Gadget(Circuit& circuit, std::initializer_list<const Signal*> inputs, bool opaque = false);
        Gadget(Circuit& circuit, const std::vector<Signal>& inputs, bool opaque = false);
        ~Gadget();
        Gadget(const Gadget&) = delete;
        Gadget& operator=(const Gadget&) = delete;

    private:
        void open(std::vector<Signal> inputs, bool opaque);
        Circuit* circuit_;
        bool open_ = false;
    };

    // The advice recorded for the graph finish() returns.
    GadgetAdvice gadget_advice() const;

    // A new input lane. Lanes are numbered in the order they are created.
    Signal input();
    std::uint32_t input_count() const { return inputs_; }

    // A new ReLU neuron fed by `value`.
    Signal relu(const Signal& value);
    // A wire carrying `value`: the wire itself for a plain wire, otherwise a
    // new identity neuron.
    WireId materialize(const Signal& value);
    // Replaces a signal that has grown many terms by an identity neuron.
    Signal compact(const Signal& value);

    // Bit gadgets. Each folds constant operands and uses at most two
    // neurons, so a gadget's cost is visible in the graph.
    Signal logical_and(const Signal& a, const Signal& b);
    Signal logical_or(const Signal& a, const Signal& b);
    Signal logical_xor(const Signal& a, const Signal& b);
    static Signal logical_not(const Signal& a) { return 1 - a; }
    // `select ? when_true : when_false`.
    Signal choose(const Signal& select, const Signal& when_true, const Signal& when_false);
    Signal all(const std::vector<Signal>& bits);
    Signal any(const std::vector<Signal>& bits);
    // [value >= threshold] for an integer-valued signal, using
    // relu(value - threshold + 1) - relu(value - threshold).
    Signal at_least(const Signal& value, std::int64_t threshold);

    // Appends an output lane carrying `value` and returns its index.
    std::uint32_t output(const Signal& value);
    std::uint32_t output_count() const { return static_cast<std::uint32_t>(outputs_.size()); }

    // The graph in NVR wire order. Neurons that no output depends on are
    // dropped.
    nvr::Graph finish() const;

private:
    struct Neuron {
        nvr::Activation activation;
        Signal input;
    };

    // Records the instance of the neuron just created.
    void created();

    std::uint32_t inputs_ = 0;
    std::vector<Neuron> neurons_;
    std::vector<WireId> outputs_;
    // Gadget recording: each neuron's instance (by neuron, when recording),
    // each instance's inputs, and the open instances, innermost last, with
    // whether each is opaque.
    bool recording_ = false;
    std::vector<std::uint32_t> neuron_instance_;
    std::vector<std::vector<Signal>> instances_;
    std::vector<std::pair<std::uint32_t, bool>> open_;
};

// Result of an integer operation and the condition under which the
// operation has no defined result. `failure` is a bit.
struct Checked {
    Word value;
    Signal failure;
};

Word constant_word(std::uint64_t bits, unsigned width);
// The value of a word whose lanes are all constant.
bool constant_value(const Word& word, std::uint64_t& bits);

// Two's-complement and unsigned arithmetic on words of equal width. For
// add, subtract, negate, and multiply, `failure` is set exactly when the
// mathematical result is outside the type's range.
Checked add(Circuit& circuit, const Word& a, const Word& b, bool is_signed);
Checked subtract(Circuit& circuit, const Word& a, const Word& b, bool is_signed);
Checked negate(Circuit& circuit, const Word& a, bool is_signed);
Checked multiply(Circuit& circuit, const Word& a, const Word& b, bool is_signed);
// The quotient rounded toward zero. `failure` is set when the divisor is zero
// or the quotient is out of range (MIN / -1).
Checked divide(Circuit& circuit, const Word& a, const Word& b, bool is_signed);
// The remainder of truncating division, with the dividend's sign. `failure`
// is set when the divisor is zero. MIN % -1 is 0.
Checked remainder(Circuit& circuit, const Word& a, const Word& b, bool is_signed);
// `a` converted from a `from_signed` integer of a.size() bits to a `to_signed`
// integer of `width` bits. `failure` is set when the value is outside the
// target range.
Checked convert_integer(Circuit& circuit, const Word& a, bool from_signed, unsigned width, bool to_signed);

// Fixed-width shift with a zero or sign-bit fill. Counts outside the word's
// width set failure. The count is encoded in least-significant-first lanes.
Checked shift(Circuit& circuit, Word word, const Word& count, bool right, bool sign_extend);

// a + b + carry on unsigned words of equal width. `failure` is the carry out
// of the top lane.
Checked add_with_carry(Circuit& circuit, const Word& a, const Word& b, const Signal& carry);
// The exact product of two unsigned words, a.size() + b.size() lanes wide.
Word multiply_full(Circuit& circuit, const Word& a, const Word& b);

Signal equal(Circuit& circuit, const Word& a, const Word& b);
Signal less(Circuit& circuit, const Word& a, const Word& b, bool is_signed);

} // namespace neura::backend

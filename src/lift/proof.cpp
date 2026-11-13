#include "proof.hpp"

#include "circuit/block_synth.hpp"
#include "circuit/circuit.hpp"
#include "circuit/float_circuit.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace neura::lift {
namespace {

using backend::BlockSynthesizer;
using backend::Checked;
using backend::Circuit;
using backend::ExitSpec;
using backend::Operand;
using backend::Signal;
using backend::Word;

constexpr std::uint32_t missing = 0xfffffffeU;

// The image tables seen by a replayed block: types and constants are looked
// up, never added, and the produced graph is captured for comparison.
class ImageTables final : public backend::BlockTables {
public:
    explicit ImageTables(const nvr::Image& image) : image_(image) {}

    const nvr::Type& type(std::uint32_t index) const override { return image_.types.at(index); }

    std::uint32_t constant(std::uint32_t type, std::vector<std::uint8_t> payload) override {
        for (std::uint32_t i = 0; i < image_.constants.size(); ++i) {
            if (image_.constants[i].type == type && image_.constants[i].payload == payload) {
                return i;
            }
        }
        return missing;
    }

    std::uint32_t graph(const nvr::Graph& graph) override {
        graph_ = graph;
        return missing;
    }

    nvr::Graph graph_;

private:
    const nvr::Image& image_;
};

FloatFormat float_of(const nvr::Type& type) {
    if (type.kind == nvr::TypeKind::BFloat) return bfloat16;
    if (type.width == 16) return binary16;
    if (type.width == 32) return binary32;
    return binary64;
}

// Builds an expression into a circuit the way the converter does: operands
// first, left to right, and each check registered right after the operation
// that can fail. `fail` receives (reason, condition).
class Builder {
public:
    Builder(const nvr::Image& image, Circuit& circuit, const std::vector<Word>& inputs,
            std::function<void(std::uint32_t, const Signal&)> fail)
        : image_(image), circuit_(circuit), inputs_(inputs), fail_(std::move(fail)) {}

    // A shared form (LiftedForm::shared) builds each expression node once,
    // however many times it is used, as the converter reuses a value it has
    // computed. Otherwise every use builds the node again.
    void share() { shared_ = true; }

    Word build(const Expression& e) {
        if (!shared_) return compute(e);
        if (const auto it = built_.find(&e); it != built_.end()) return it->second;
        auto word = compute(e);
        built_.emplace(&e, word);
        return word;
    }

private:
    Word compute(const Expression& e) {
        const auto& type = image_.types.at(e.type);
        switch (e.kind) {
        case Expression::Kind::Input:
            return inputs_.at(e.input);
        case Expression::Kind::Immediate:
            return backend::constant_word(e.bits, nvr::lane_count(type));
        case Expression::Kind::Unary: {
            const auto operand = build(*e.operands[0]);
            if (e.op == "!") {
                return {Circuit::logical_not(operand.front())};
            }
            if (e.op == "~") {
                auto word = operand;
                std::ranges::transform(word, word.begin(), Circuit::logical_not);
                return word;
            }
            if (e.op == "hash") {
                return value_hash(image_.types.at(e.operands[0]->type), operand);
            }
            if (e.op == "index") {
                // to_index: a negative signed index is a bounds failure.
                const auto& from = image_.types.at(e.operands[0]->type);
                auto word = operand;
                if (from.kind == nvr::TypeKind::Signed) {
                    fail_(2, word.back());
                }
                word.resize(64, Signal::of(0));
                return word;
            }
            if (is_floating(type)) {
                return backend::float_negate(operand);
            }
            auto result = backend::negate(circuit_, operand, type.kind == nvr::TypeKind::Signed);
            fail_(1, result.failure);
            return result.value;
        }
        case Expression::Kind::Convert: {
            const auto operand = build(*e.operands[0]);
            const auto& from = image_.types.at(e.operands[0]->type);
            if (is_floating(from) && is_floating(type)) {
                return backend::float_convert(circuit_, operand, float_of(from), float_of(type));
            }
            if (is_floating(type)) {
                return backend::float_from_integer(circuit_, operand, from.kind == nvr::TypeKind::Signed,
                                                   float_of(type));
            }
            Checked result = is_floating(from)
                ? backend::float_to_integer(circuit_, operand, float_of(from), type.width,
                                            type.kind == nvr::TypeKind::Signed)
                : backend::convert_integer(circuit_, operand, from.kind == nvr::TypeKind::Signed, type.width,
                                           type.kind == nvr::TypeKind::Signed);
            fail_(1, result.failure);
            return result.value;
        }
        case Expression::Kind::Binary: {
            const auto a = build(*e.operands[0]);
            const auto b = build(*e.operands[1]);
            const auto& operand_type = image_.types.at(e.operands[0]->type);
            return binary(e.op, operand_type, a, b);
        }
        }
        throw std::logic_error("unknown expression kind");
    }

    Word binary(const std::string& op, const nvr::Type& t, const Word& a, const Word& b) {
        if (t.kind == nvr::TypeKind::Bool) {
            if (op == "&&") return {circuit_.logical_and(a.front(), b.front())};
            if (op == "||") return {circuit_.logical_or(a.front(), b.front())};
            const auto same = Circuit::logical_not(circuit_.logical_xor(a.front(), b.front()));
            return {op == "==" ? same : Circuit::logical_not(same)};
        }
        if (is_floating(t)) {
            const auto format = float_of(t);
            if (is_comparison(op)) {
                using backend::FloatComparison;
                const auto kind = op == "==" ? FloatComparison::Equal
                                : op == "!=" ? FloatComparison::NotEqual
                                : op == "<"  ? FloatComparison::Less
                                : op == "<=" ? FloatComparison::LessEqual
                                : op == ">"  ? FloatComparison::Greater
                                             : FloatComparison::GreaterEqual;
                return {backend::float_compare(circuit_, a, b, format, kind)};
            }
            if (op == "+") return backend::float_add(circuit_, a, b, format);
            if (op == "-") return backend::float_subtract(circuit_, a, b, format);
            if (op == "*") return backend::float_multiply(circuit_, a, b, format);
            return backend::float_divide(circuit_, a, b, format);
        }
        const bool is_signed = t.kind == nvr::TypeKind::Signed;
        if (op == "==") return {backend::equal(circuit_, a, b)};
        if (op == "!=") return {Circuit::logical_not(backend::equal(circuit_, a, b))};
        if (op == "<") return {backend::less(circuit_, a, b, is_signed)};
        if (op == ">") return {backend::less(circuit_, b, a, is_signed)};
        if (op == "<=") return {Circuit::logical_not(backend::less(circuit_, b, a, is_signed))};
        if (op == ">=") return {Circuit::logical_not(backend::less(circuit_, a, b, is_signed))};
        if (op == "&" || op == "|" || op == "^") {
            Word result;
            result.reserve(a.size());
            for (std::size_t i = 0; i < a.size(); ++i) {
                result.push_back(op == "&"   ? circuit_.logical_and(a[i], b[i])
                                 : op == "|" ? circuit_.logical_or(a[i], b[i])
                                             : circuit_.logical_xor(a[i], b[i]));
            }
            return result;
        }
        Checked result;
        if (op == "<<" || op == ">>") {
            result = backend::shift(circuit_, a, b, op == ">>", is_signed);
            fail_(1, result.failure);
            return result.value;
        }
        if (op == "+~") {
            return backend::add(circuit_, a, b, is_signed).value;
        }
        if (op == "-~") {
            return backend::subtract(circuit_, a, b, is_signed).value;
        }
        if (op == "+") {
            result = backend::add(circuit_, a, b, is_signed);
        } else if (op == "-") {
            result = backend::subtract(circuit_, a, b, is_signed);
        } else if (op == "*") {
            result = backend::multiply(circuit_, a, b, is_signed);
        } else if (op == "/") {
            result = backend::divide(circuit_, a, b, is_signed);
        } else {
            result = backend::remainder(circuit_, a, b, is_signed);
        }
        fail_(1, result.failure);
        return result.value;
    }

    // hash_code of a scalar as the converter builds it: a float's NaNs
    // replaced by its canonical NaN, then a Boolean selects one of two
    // constants, a 64-bit word is folded by exclusive or of its halves, and
    // a narrower one is extended to 32 lanes, with its sign for a signed
    // integer.
    Word value_hash(const nvr::Type& type, Word word) {
        if (is_floating(type)) {
            const auto format = float_of(type);
            const auto first_exponent = word.begin() + format.fraction_bits;
            const auto nan = circuit_.logical_and(circuit_.all({first_exponent, word.end() - 1}),
                                                  circuit_.any({word.begin(), first_exponent}));
            const auto canonical = backend::constant_word(format.canonical_nan(), format.width());
            for (std::size_t bit = 0; bit < word.size(); ++bit) {
                word[bit] = circuit_.choose(nan, canonical[bit], word[bit]);
            }
        }
        if (type.kind == nvr::TypeKind::Bool) {
            const auto true_hash = backend::constant_word(1231, 32);
            const auto false_hash = backend::constant_word(1237, 32);
            Word hash;
            for (std::size_t bit = 0; bit < 32; ++bit) {
                hash.push_back(circuit_.choose(word.front(), true_hash[bit], false_hash[bit]));
            }
            return hash;
        }
        if (word.size() == 64) {
            for (std::size_t bit = 0; bit < 32; ++bit) word[bit] = circuit_.logical_xor(word[bit], word[bit + 32]);
            word.resize(32);
        } else if (word.size() < 32) {
            const auto extension = type.kind == nvr::TypeKind::Signed ? word.back() : Signal::of(0);
            word.resize(32, extension);
        }
        return word;
    }

    const nvr::Image& image_;
    Circuit& circuit_;
    const std::vector<Word>& inputs_;
    std::function<void(std::uint32_t, const Signal&)> fail_;
    bool shared_ = false;
    std::map<const Expression*, Word> built_;
};

} // namespace

std::optional<nvr::Block> replay(const nvr::Image& image, const nvr::Block& original,
                                 const std::vector<std::pair<std::uint32_t, ExpressionPtr>>& values,
                                 const ExpressionPtr& condition, std::optional<std::size_t> exit_when_true,
                                 nvr::Graph& graph, Checks* checks, const std::vector<ExpressionPtr>* schedule,
                                 backend::GadgetAdvice* advice) {
    ImageTables tables(image);
    BlockSynthesizer synth(tables);
    if (advice) synth.circuit().record_gadgets();
    std::vector<Word> inputs;
    for (const auto& value : original.inputs) {
        Operand operand;
        operand.kind = value.kind == nvr::ValueKind::Slot ? Operand::Kind::Slot : Operand::Kind::Constant;
        operand.type = value.type;
        operand.index = value.index;
        inputs.push_back(synth.lanes(operand));
    }
    const ExpressionPtr* current = nullptr;
    Builder builder(image, synth.circuit(), inputs, [&](std::uint32_t reason, const Signal& condition_signal) {
        if (synth.fail_when(condition_signal, reason) && checks && current) {
            checks->emplace_back(reason, *current);
        }
    });
    std::map<std::uint32_t, Word> words;
    if (schedule) builder.share();
    try {
        if (schedule) {
            for (const auto& expression : *schedule) {
                current = &expression;
                builder.build(*expression);
            }
        }
        for (const auto& [lane, expression] : values) {
            current = &expression;
            words[lane] = builder.build(*expression);
        }
    } catch (const std::exception&) {
        // A form that names a missing input, a type of the wrong width, or an
        // operand of the wrong shape describes no block.
        return std::nullopt;
    }
    Signal when = Signal::of(1);
    if (condition) {
        current = &condition;
        try {
            const auto word = builder.build(*condition);
            if (word.size() != 1) {
                return std::nullopt;
            }
            when = word.front();
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    const auto as_operand = [&](const nvr::Value& value) -> Operand {
        Operand operand;
        operand.type = value.type;
        operand.index = value.index;
        switch (value.kind) {
        case nvr::ValueKind::Slot: operand.kind = Operand::Kind::Slot; break;
        case nvr::ValueKind::Constant: operand.kind = Operand::Kind::Constant; break;
        case nvr::ValueKind::Lanes:
            operand.kind = Operand::Kind::Bits;
            operand.word = words.at(value.index);
            break;
        }
        return operand;
    };
    std::vector<ExitSpec> exits;
    std::size_t non_trap = 0;
    try {
        for (const auto& exit : original.exits) {
            if (exit.kind == nvr::ExitKind::Trap && non_trap > 0 && exit.reason != 0 &&
                std::any_of(original.exits.begin(), original.exits.end(),
                            [](const nvr::Exit& e) { return e.kind != nvr::ExitKind::Trap; })) {
                // Traps after the normal exits come from checks; close() adds them.
                continue;
            }
            ExitSpec spec;
            spec.kind = exit.kind;
            for (const auto& assignment : exit.assignments) {
                spec.assignments.emplace_back(assignment.slot, as_operand(assignment.value));
            }
            for (const auto& argument : exit.arguments) {
                spec.arguments.push_back(as_operand(argument));
            }
            spec.target = exit.target;
            spec.operation = exit.operation;
            spec.immediate = exit.immediate;
            spec.result_slot = exit.result_slot;
            spec.resume = exit.resume;
            spec.error_slot = exit.error_slot;
            spec.error_resume = exit.error_resume;
            spec.reason = exit.reason;
            spec.scheduler = exit.scheduler;
            spec.poll = exit.poll;
            if (exit_when_true) {
                spec.when = non_trap == *exit_when_true ? when : Circuit::logical_not(when);
            }
            exits.push_back(std::move(spec));
            ++non_trap;
        }
    } catch (const std::exception&) {
        // An exit reads lanes for which the form has no value.
        return std::nullopt;
    }
    nvr::Block block;
    try {
        block = synth.close(std::move(exits));
    } catch (const std::exception&) {
        return std::nullopt;
    }
    block.graph = original.graph;
    graph = tables.graph_;
    if (advice) *advice = synth.circuit().gadget_advice();
    return block;
}

bool replay_matches(const nvr::Image& image, const nvr::Block& original, const nvr::Graph& graph,
                    const std::vector<std::pair<std::uint32_t, ExpressionPtr>>& values, const ExpressionPtr& condition,
                    std::optional<std::size_t> exit_when_true, Checks* checks,
                    const std::vector<ExpressionPtr>* schedule, backend::GadgetAdvice* advice) {
    nvr::Graph regenerated;
    Checks found;
    const auto block =
        replay(image, original, values, condition, exit_when_true, regenerated, &found, schedule, advice);
    if (!block || !(*block == original) || !(regenerated == graph)) {
        return false;
    }
    if (checks) {
        *checks = std::move(found);
    }
    return true;
}

nvr::Graph dispatch_circuit(const nvr::Block& block, const std::map<std::size_t, std::vector<std::uint32_t>>& exits) {
    // One equality test per layout, ordered by layout, summed per exit; the
    // receiver lanes pass through.
    Circuit circuit;
    Word tag_word;
    Word receiver;
    for (int i = 0; i < 32; ++i) {
        tag_word.push_back(circuit.input());
    }
    for (int i = 0; i < 64; ++i) {
        receiver.push_back(circuit.input());
    }
    std::vector<std::pair<std::uint32_t, std::size_t>> classes;
    for (const auto& [exit, layouts] : exits) {
        for (const auto layout : layouts) {
            classes.emplace_back(layout, exit);
        }
    }
    std::sort(classes.begin(), classes.end());
    std::vector<Signal> indicators(block.exits.size());
    for (const auto& [layout, exit] : classes) {
        if (exit >= indicators.size()) {
            return {};
        }
        indicators[exit] = indicators[exit] + backend::equal(circuit, tag_word, backend::constant_word(layout, 32));
    }
    if (indicators.empty()) {
        return {};
    }
    Signal none = Signal::of(1);
    for (std::size_t e = 0; e + 1 < block.exits.size(); ++e) {
        none = none - indicators[e];
    }
    indicators.back() = none;
    if (block.exits.size() > 1) {
        const auto lanes = nvr::selector_lanes(block.exits.size());
        for (unsigned j = 0; j < lanes; ++j) {
            Signal bit;
            for (std::size_t e = 0; e < block.exits.size(); ++e) {
                if ((e >> j) & 1U) {
                    bit = bit + indicators[e];
                }
            }
            circuit.output(bit);
        }
        for (const auto& lane : receiver) {
            circuit.output(lane);
        }
    }
    return circuit.finish();
}

bool prove_structure(const nvr::Image& image, const nvr::Block& block, const nvr::Graph& graph,
                     const LiftedForm& form, Checks* checks, backend::GadgetAdvice* advice) {
    // The gadgets assume well-typed operands; an ill-typed form describes no
    // block. The check also makes the order list exactly the form's values,
    // so the proof covers exactly what a lowering computes.
    if (form_type_error(image, block, form)) {
        return false;
    }
    if (!form.dispatch.empty()) {
        // A dispatch form claims nothing else about the block.
        if (!form.checks.empty() || block.inputs.size() != 2 ||
            nvr::lane_count(image.types.at(block.inputs[0].type)) != 32 ||
            nvr::lane_count(image.types.at(block.inputs[1].type)) != 64) {
            return false;
        }
        return dispatch_circuit(block, form.dispatch) == graph;
    }
    return replay_matches(image, block, graph, form.computation_order(), form.condition, form.exit_when_true,
                          checks, form.shared ? &form.schedule : nullptr, advice);
}

nvr::Graph expression_circuit(const nvr::Image& image, const std::vector<unsigned>& input_widths,
                              const Expression& expression) {
    Circuit circuit;
    std::vector<Word> inputs;
    for (const auto width : input_widths) {
        Word word;
        for (unsigned b = 0; b < width; ++b) {
            word.push_back(circuit.input());
        }
        inputs.push_back(std::move(word));
    }
    Builder builder(image, circuit, inputs, [](std::uint32_t, const Signal&) {});
    for (const auto& lane : builder.build(expression)) {
        circuit.output(lane);
    }
    return circuit.finish();
}

} // namespace neura::lift

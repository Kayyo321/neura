#include "lifter.hpp"

#include "lift/proof.hpp"
#include "lift/recognize.hpp"
#include "nvm/machine.hpp"

#include <algorithm>
#include <cstdio>
#include <map>

namespace neura::decompiler {
namespace {

std::uint64_t mask(unsigned width) { return lift::width_mask(width); }

bool floating(const nvr::Type& type) { return lift::is_floating(type); }

// Graph lanes of each input word, and evaluation with each word set to a
// value, for the exhaustive check.
class Words {
public:
    Words(const nvr::Image& image, const nvr::Block& block) : graph_(image.graphs.at(block.graph)) {
        std::uint32_t lane = 0;
        for (const auto& value : block.inputs) {
            offsets_.push_back(lane);
            widths_.push_back(nvr::lane_count(image.types.at(value.type)));
            lane += widths_.back();
        }
        total_ = lane;
    }

    std::uint32_t total_lanes() const { return total_; }
    const std::vector<unsigned>& widths() const { return widths_; }

    // The block's graph with each input word set to the given value, or
    // nothing when the graph overflows.
    std::optional<std::vector<std::int64_t>> outputs(const std::vector<std::uint64_t>& words) const {
        std::vector<std::int64_t> wires(graph_.input_lanes, 0);
        for (std::size_t i = 0; i < words.size(); ++i) {
            for (unsigned b = 0; b < widths_[i]; ++b) {
                wires[offsets_[i] + b] = static_cast<std::int64_t>((words[i] >> b) & 1U);
            }
        }
        if (nvm::evaluate_graph(graph_, wires)) {
            return std::nullopt;
        }
        std::vector<std::int64_t> result;
        for (const auto output : graph_.outputs) {
            result.push_back(wires[output]);
        }
        return result;
    }

    // A compiled expression on the same words, or nothing when its graph
    // overflows.
    std::optional<std::uint64_t> run(const nvr::Graph& graph, const std::vector<std::uint64_t>& words) const {
        std::vector<std::int64_t> wires(graph.input_lanes, 0);
        std::size_t lane = 0;
        for (std::size_t i = 0; i < words.size(); ++i) {
            for (unsigned b = 0; b < widths_[i]; ++b) {
                wires[lane++] = static_cast<std::int64_t>((words[i] >> b) & 1U);
            }
        }
        if (nvm::evaluate_graph(graph, wires)) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < graph.outputs.size(); ++i) {
            value |= static_cast<std::uint64_t>(wires[graph.outputs[i]] & 1) << i;
        }
        return value;
    }

    static std::uint64_t word(const std::vector<std::int64_t>& outputs, std::uint32_t first, std::uint32_t count) {
        std::uint64_t value = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            value |= static_cast<std::uint64_t>(outputs[first + i] & 1) << i;
        }
        return value;
    }

private:
    const nvr::Graph& graph_;
    std::vector<std::uint32_t> offsets_;
    std::vector<unsigned> widths_;
    std::uint32_t total_ = 0;
};

} // namespace

LiftedBlock lift_block(const nvr::Image& image, std::uint32_t function_index, std::uint32_t block_index,
                       const LiftOptions& options) {
    const auto& block = image.functions.at(function_index).blocks.at(block_index);
    auto recognition = lift::recognize(image, function_index, block_index);
    LiftedBlock lifted;
    switch (recognition.status) {
    case lift::Recognition::Status::Trivial:
        lifted.evidence = Evidence::None;
        return lifted;
    case lift::Recognition::Status::Lifted:
        static_cast<lift::LiftedForm&>(lifted) = std::move(recognition.form);
        lifted.evidence = Evidence::Structural;
        return lifted;
    case lift::Recognition::Status::Unrecognized:
        break;
    }
    lifted.evidence = Evidence::Circuit;
    if (!recognition.searched) {
        return lifted;
    }

    // No structural match. A small block whose values agree with the graph on
    // every input is lifted by exhaustive evaluation.
    const Words model(image, block);
    std::size_t normal_exits = 0;
    for (const auto& exit : block.exits) {
        normal_exits += exit.kind == nvr::ExitKind::Trap ? 0 : 1;
    }
    // The type of each value the exits read, keyed by its first lane.
    std::map<std::uint32_t, std::uint32_t> targets;
    for (const auto& exit : block.exits) {
        for (const auto& value : exit.arguments) {
            if (value.kind == nvr::ValueKind::Lanes) targets.emplace(value.index, value.type);
        }
        for (const auto& assignment : exit.assignments) {
            if (assignment.value.kind == nvr::ValueKind::Lanes) targets.emplace(assignment.value.index, assignment.value.type);
        }
    }
    const auto& options_per_value = recognition.candidates;
    if (model.total_lanes() <= options.exhaustive_lanes && !options_per_value.empty() &&
        std::all_of(block.exits.begin(), block.exits.end(), [](const nvr::Exit& e) { return e.kind != nvr::ExitKind::Trap; }) &&
        normal_exits <= 1) {
        const auto total = std::uint64_t{1} << model.total_lanes();
        for (const auto& [lane, found] : options_per_value) {
            const auto width = nvr::lane_count(image.types.at(targets.at(lane)));
            ExpressionPtr proven;
            for (const auto& candidate : found) {
                const auto compiled = lift::expression_circuit(image, model.widths(), *candidate);
                bool equal = true;
                for (std::uint64_t bits = 0; bits < total && equal; ++bits) {
                    std::vector<std::uint64_t> words;
                    std::uint64_t rest = bits;
                    for (const auto w : model.widths()) {
                        words.push_back(rest & mask(w));
                        rest >>= w;
                    }
                    const auto outputs = model.outputs(words);
                    const auto got = model.run(compiled, words);
                    equal = outputs && got && Words::word(*outputs, lane, width) == *got;
                }
                if (equal) {
                    proven = candidate;
                    break;
                }
            }
            if (!proven) {
                lifted.evidence = Evidence::Circuit;
                lifted.values.clear();
                return lifted;
            }
            lifted.values[lane] = proven;
        }
        lifted.evidence = Evidence::Exhaustive;
        lifted.lanes_checked = model.total_lanes();
        return lifted;
    }
    return lifted;
}

std::optional<nvr::Block> regenerate(const nvr::Image& image, std::uint32_t function, std::uint32_t block,
                                     const LiftedBlock& lifted, nvr::Graph& graph) {
    const auto& original = image.functions.at(function).blocks.at(block);
    if (lifted.evidence != Evidence::Structural || !lifted.dispatch.empty()) {
        return std::nullopt;
    }
    std::vector<std::pair<std::uint32_t, ExpressionPtr>> values(lifted.values.begin(), lifted.values.end());
    // The order that matched: checks list the order of failing operations;
    // values without checks keep lane order.
    std::vector<std::pair<std::uint32_t, ExpressionPtr>> ordered;
    for (const auto& [reason, expression] : lifted.checks) {
        for (const auto& value : values) {
            if (value.second == expression &&
                std::none_of(ordered.begin(), ordered.end(), [&](const auto& v) { return v.first == value.first; })) {
                ordered.push_back(value);
            }
        }
    }
    for (const auto& value : values) {
        if (std::none_of(ordered.begin(), ordered.end(), [&](const auto& v) { return v.first == value.first; })) {
            ordered.push_back(value);
        }
    }
    return lift::replay(image, original, ordered, lifted.condition, lifted.exit_when_true, graph, nullptr);
}


std::string render(const nvr::Image& image, const Expression& e, const std::vector<std::string>& names) {
    switch (e.kind) {
    case Expression::Kind::Input:
        return e.input < names.size() ? names[e.input] : "in" + std::to_string(e.input);
    case Expression::Kind::Immediate: {
        const auto& t = image.types.at(e.type);
        if (t.kind == nvr::TypeKind::Bool) {
            return e.bits != 0 ? "true" : "false";
        }
        if (t.kind == nvr::TypeKind::Signed) {
            const auto value = t.width < 64 && ((e.bits >> (t.width - 1)) & 1U) != 0
                                   ? static_cast<std::int64_t>(e.bits | ~mask(t.width))
                                   : static_cast<std::int64_t>(e.bits);
            return std::to_string(value);
        }
        if (floating(t)) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "float%u(0x%llx)", t.width, static_cast<unsigned long long>(e.bits));
            return buffer;
        }
        return std::to_string(e.bits);
    }
    case Expression::Kind::Unary: {
        const auto operand = render(image, *e.operands[0], names);
        if (e.op == "!") return "!" + operand;
        if (e.op == "index") return "index(" + operand + ")";
        return "-" + operand;
    }
    case Expression::Kind::Convert: {
        const auto& t = image.types.at(e.type);
        std::string name;
        switch (t.kind) {
        case nvr::TypeKind::Signed: name = "int" + std::to_string(t.width); break;
        case nvr::TypeKind::Unsigned: name = "uint" + std::to_string(t.width); break;
        case nvr::TypeKind::Float: name = "float" + std::to_string(t.width); break;
        case nvr::TypeKind::BFloat: name = "bfloat16"; break;
        default: name = "t" + std::to_string(e.type); break;
        }
        return "(" + render(image, *e.operands[0], names) + " as " + name + ")";
    }
    case Expression::Kind::Binary:
        if (e.op == "+~") {
            return "(" + render(image, *e.operands[0], names) + " + " + render(image, *e.operands[1], names) +
                   " /* unchecked */)";
        }
        return "(" + render(image, *e.operands[0], names) + " " + e.op + " " + render(image, *e.operands[1], names) +
               ")";
    }
    return "?";
}

} // namespace neura::decompiler

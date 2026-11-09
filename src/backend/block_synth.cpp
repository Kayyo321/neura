#include "block_synth.hpp"

#include <algorithm>
#include <stdexcept>

namespace neura::backend {

std::vector<std::uint8_t> immediate_payload(const nvr::Type& type, std::uint64_t bits) {
    const auto bytes = type.kind == nvr::TypeKind::Bool ? 1U : type.width / 8U;
    std::vector<std::uint8_t> result;
    for (unsigned i = 0; i < bytes; ++i) {
        result.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    return result;
}

Word BlockSynthesizer::lanes(const Operand& operand) {
    const auto count = nvr::lane_count(tables_->type(operand.type));
    switch (operand.kind) {
    case Operand::Kind::Immediate:
        return constant_word(operand.bits, count);
    case Operand::Kind::Bits:
        return operand.word;
    case Operand::Kind::Slot:
    case Operand::Kind::Constant: {
        const auto kind = operand.kind == Operand::Kind::Slot ? nvr::ValueKind::Slot : nvr::ValueKind::Constant;
        const auto key = std::make_pair(kind, operand.index);
        if (const auto it = input_lanes_.find(key); it != input_lanes_.end()) {
            return it->second;
        }
        inputs_.push_back({kind, operand.type, operand.index});
        Word word;
        for (unsigned i = 0; i < count; ++i) {
            word.push_back(circuit_.input());
        }
        input_lanes_.emplace(key, word);
        return word;
    }
    case Operand::Kind::Void:
        break;
    }
    throw std::logic_error("a void operand has no lanes");
}

nvr::Value BlockSynthesizer::value(const Operand& operand) {
    switch (operand.kind) {
    case Operand::Kind::Immediate: {
        const auto& t = tables_->type(operand.type);
        return {nvr::ValueKind::Constant, operand.type, tables_->constant(operand.type, immediate_payload(t, operand.bits))};
    }
    case Operand::Kind::Constant:
        return {nvr::ValueKind::Constant, operand.type, operand.index};
    case Operand::Kind::Slot:
        return {nvr::ValueKind::Slot, operand.type, operand.index};
    case Operand::Kind::Bits: {
        std::vector<WireId> wires;
        wires.reserve(operand.word.size());
        for (const auto& lane : operand.word) {
            wires.push_back(circuit_.materialize(lane));
        }
        auto [it, inserted] = output_lanes_.emplace(wires, circuit_.output_count());
        if (inserted) {
            for (const auto wire : wires) {
                circuit_.output(Signal::wire(wire));
            }
        }
        return {nvr::ValueKind::Lanes, operand.type, it->second};
    }
    case Operand::Kind::Void:
        break;
    }
    throw std::logic_error("a void operand has no value");
}

bool BlockSynthesizer::fail_when(const Signal& condition, std::uint32_t reason) {
    if (condition.is_constant(0)) {
        return false;
    }
    if (!failures_.empty() && failures_.back().first == reason) {
        failures_.back().second = circuit_.logical_or(failures_.back().second, condition);
    } else {
        failures_.emplace_back(reason, condition);
    }
    return true;
}

nvr::Block BlockSynthesizer::close(std::vector<ExitSpec> exits) {
    Signal clear = Signal::of(1);
    std::vector<std::pair<std::uint32_t, Signal>> traps;
    for (const auto& [reason, condition] : failures_) {
        const auto first = circuit_.logical_and(condition, clear);
        const auto it = std::find_if(traps.begin(), traps.end(), [&](const auto& trap) { return trap.first == reason; });
        if (it == traps.end()) {
            traps.emplace_back(reason, first);
        } else {
            it->second = it->second + first;
        }
        clear = circuit_.logical_and(clear, Circuit::logical_not(condition));
    }
    std::vector<ExitSpec> selected;
    for (auto& exit : exits) {
        exit.when = circuit_.logical_and(exit.when, clear);
        if (!exit.when.is_constant(0)) {
            selected.push_back(std::move(exit));
        }
    }
    for (const auto& [reason, when] : traps) {
        if (!when.is_constant(0)) {
            ExitSpec trap;
            trap.kind = nvr::ExitKind::Trap;
            trap.reason = reason;
            trap.when = when;
            selected.push_back(std::move(trap));
        }
    }
    if (selected.empty()) {
        throw std::logic_error("a block has no reachable exit");
    }

    nvr::Block block;
    if (selected.size() > 1) {
        block.selector_start = circuit_.output_count();
        const auto lanes = nvr::selector_lanes(selected.size());
        for (unsigned j = 0; j < lanes; ++j) {
            Signal bit;
            for (std::size_t e = 0; e < selected.size(); ++e) {
                if ((e >> j) & 1U) {
                    bit = bit + selected[e].when;
                }
            }
            circuit_.output(bit);
        }
    }
    for (const auto& spec : selected) {
        nvr::Exit exit;
        exit.kind = spec.kind;
        if (spec.kind != nvr::ExitKind::Trap && spec.kind != nvr::ExitKind::Return) {
            for (const auto& [slot, operand] : pending_) {
                if (operand.kind == Operand::Kind::Slot && operand.index == slot) {
                    continue;
                }
                exit.assignments.push_back({slot, value(operand)});
            }
            for (const auto& [slot, operand] : spec.assignments) {
                exit.assignments.push_back({slot, value(operand)});
            }
        }
        for (const auto& argument : spec.arguments) {
            exit.arguments.push_back(value(argument));
        }
        exit.target = spec.target;
        exit.operation = spec.operation;
        exit.immediate = spec.immediate;
        exit.result_slot = spec.result_slot;
        exit.resume = spec.resume;
        exit.error_slot = spec.error_slot;
        exit.error_resume = spec.error_resume;
        exit.reason = spec.reason;
        block.exits.push_back(std::move(exit));
    }
    block.inputs = inputs_;
    block.graph = tables_->graph(circuit_.finish());
    return block;
}

} // namespace neura::backend

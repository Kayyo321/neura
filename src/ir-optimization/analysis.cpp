#include "analysis.hpp"

#include "neura-ir/control_flow.hpp"
#include "neura-ir/integer.hpp"

#include <algorithm>

namespace neura::optimization {
namespace {

bool comparison(const IrNode& node) {
    const auto& op = node.text;
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

} // namespace

std::optional<bool> boolean_literal(const IrNode& node) {
    if (node.op != IrOp::Boolean || node.type.kind != TypeKind::Bool) {
        return {};
    }
    if (node.text == "true") return true;
    if (node.text == "false") return false;
    return {};
}

bool pure_and_total(const IrProgram& program, const IrNode& node) {
    const auto all_operands = [&](const std::vector<IrNode>& nodes) {
        return std::ranges::all_of(nodes, [&](const IrNode& operand) { return pure_and_total(program, operand); });
    };
    switch (node.op) {
    case IrOp::Boolean:
    case IrOp::Integer:
        return node.operands.empty();
    case IrOp::Load: {
        if (!node.operands.empty() || node.symbol >= program.symbols.size()) {
            return false;
        }
        const auto kind = program.symbols[node.symbol].kind;
        return kind == SymbolKind::Local || kind == SymbolKind::Parameter;
    }
    case IrOp::Unary:
        return (node.text == "!" || node.text == "~") && all_operands(node.operands);
    case IrOp::Binary: {
        if ((!comparison(node) && (!is_bitwise_binary(node.text) || is_shift(node.text))) ||
            node.operands.size() != 2) {
            return false;
        }
        const auto kind = node.operands.front().type.kind;
        return (kind == TypeKind::Bool || kind == TypeKind::Integer) && all_operands(node.operands);
    }
    case IrOp::ShortCircuit:
        return all_operands(node.operands) && all_operands(node.body);
    default:
        return false;
    }
}

bool never_completes(const IrNode& statement) {
    return neura::never_completes(statement);
}

} // namespace neura::optimization

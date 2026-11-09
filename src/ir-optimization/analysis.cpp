#include "analysis.hpp"

#include <algorithm>

namespace neura::optimization {
namespace {

bool region_never_completes(const std::vector<IrNode>& region) {
    return std::ranges::any_of(region, never_completes);
}

// Whether a break in `region` targets the loop that owns the region. A break
// inside a nested loop targets that loop, so nested loops are not searched.
bool breaks_out(const std::vector<IrNode>& region) {
    return std::ranges::any_of(region, [](const IrNode& node) {
        switch (node.op) {
        case IrOp::Break: return true;
        case IrOp::Block: return breaks_out(node.body);
        case IrOp::Branch: return breaks_out(node.body) || breaks_out(node.alternative);
        default: return false;
        }
    });
}

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
        return node.text == "!" && all_operands(node.operands);
    case IrOp::Binary: {
        if (!comparison(node) || node.operands.size() != 2) {
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
    switch (statement.op) {
    case IrOp::Return:
    case IrOp::Break:
    case IrOp::Continue:
        return true;
    case IrOp::Block:
        return region_never_completes(statement.body);
    case IrOp::Branch:
        return region_never_completes(statement.body) && region_never_completes(statement.alternative);
    case IrOp::Loop:
        return statement.operands.size() == 1 && boolean_literal(statement.operands.front()) == true &&
               !breaks_out(statement.body);
    default:
        return false;
    }
}

} // namespace neura::optimization

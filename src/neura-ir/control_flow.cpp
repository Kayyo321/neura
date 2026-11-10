#include "control_flow.hpp"

#include <algorithm>

namespace neura {
namespace {

bool true_literal(const IrNode& node) {
    return node.op == IrOp::Boolean && node.type.kind == TypeKind::Bool && node.text == "true";
}

} // namespace

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

bool infinite_loop(const IrNode& loop) {
    return loop.op == IrOp::Loop && loop.operands.size() == 1 && true_literal(loop.operands.front()) &&
           !breaks_out(loop.body);
}

bool always_returns(const std::vector<IrNode>& region) {
    return std::ranges::any_of(region, [](const IrNode& node) {
        switch (node.op) {
        case IrOp::Return: return true;
        case IrOp::TrapMessage: return true;
        case IrOp::Block: return always_returns(node.body);
        case IrOp::Branch: return always_returns(node.body) && always_returns(node.alternative);
        case IrOp::Loop: return infinite_loop(node);
        default: return false;
        }
    });
}

bool never_completes(const IrNode& statement) {
    switch (statement.op) {
    case IrOp::Return:
    case IrOp::Break:
    case IrOp::Continue:
    case IrOp::TrapMessage:
        return true;
    case IrOp::Block:
        return std::ranges::any_of(statement.body, never_completes);
    case IrOp::Branch:
        return std::ranges::any_of(statement.body, never_completes) &&
               std::ranges::any_of(statement.alternative, never_completes);
    case IrOp::Loop:
        return infinite_loop(statement);
    default:
        return false;
    }
}

} // namespace neura

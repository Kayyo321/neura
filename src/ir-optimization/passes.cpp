#include "passes.hpp"

#include "analysis.hpp"
#include "integer_folding.hpp"

#include <string_view>
#include <utility>

namespace neura::optimization {
namespace {

// Visits every node in post-order: a node is visited after its operands and
// regions have been rewritten, so one traversal folds nested constants.
// `rewrite` may replace the node it receives.
template <typename Rewrite>
void post_order(std::vector<IrNode>& nodes, Rewrite& rewrite) {
    for (auto& node : nodes) {
        post_order(node.operands, rewrite);
        post_order(node.body, rewrite);
        post_order(node.alternative, rewrite);
        rewrite(node);
    }
}

template <typename Rewrite>
std::size_t rewrite_nodes(IrProgram& program, Rewrite rewrite) {
    std::size_t count = 0;
    auto counted = [&](IrNode& node) {
        if (rewrite(node)) {
            ++count;
        }
    };
    for (auto& body : program.bodies) {
        post_order(body.instructions, counted);
    }
    return count;
}

// A literal replacing `original` keeps its type and source span, so the
// result still points at the source construct that it summarizes.
IrNode literal_for(const IrNode& original, IrOp op, std::string text) {
    return {op, original.type, original.span, no_symbol, std::move(text), {}, {}, {}, {}};
}

IrNode boolean_for(const IrNode& original, bool value) {
    return literal_for(original, IrOp::Boolean, value ? "true" : "false");
}

std::optional<IntegerOperation> arithmetic(std::string_view op) {
    if (op == "+") return IntegerOperation::Add;
    if (op == "-") return IntegerOperation::Subtract;
    if (op == "*") return IntegerOperation::Multiply;
    if (op == "/") return IntegerOperation::Divide;
    return {};
}

std::optional<bool> compare_with(std::string_view op, int order) {
    if (op == "==") return order == 0;
    if (op == "!=") return order != 0;
    if (op == "<") return order < 0;
    if (op == "<=") return order <= 0;
    if (op == ">") return order > 0;
    if (op == ">=") return order >= 0;
    return {};
}

bool fold_unary(IrNode& node) {
    if (node.operands.size() != 1) {
        return false;
    }
    const auto& operand = node.operands.front();
    if (node.text == "!") {
        if (const auto value = boolean_literal(operand)) {
            node = boolean_for(node, !*value);
            return true;
        }
    } else if (node.text == "-") {
        const auto value = integer_constant(operand);
        const auto result = value ? negate(*value) : std::nullopt;
        if (result) {
            node = literal_for(node, IrOp::Integer, spelling(*result));
            return true;
        }
    }
    return false;
}

bool fold_binary(IrNode& node) {
    if (node.operands.size() != 2 || node.operands[0].type != node.operands[1].type) {
        return false;
    }
    const auto& op = node.text;
    const auto left_bool = boolean_literal(node.operands[0]);
    const auto right_bool = boolean_literal(node.operands[1]);
    if (left_bool && right_bool) {
        if (op != "==" && op != "!=") {
            return false;
        }
        node = boolean_for(node, (*left_bool == *right_bool) == (op == "=="));
        return true;
    }
    const auto left = integer_constant(node.operands[0]);
    const auto right = integer_constant(node.operands[1]);
    if (!left || !right) {
        return false;
    }
    if (const auto operation = arithmetic(op)) {
        const auto result = fold(*operation, *left, *right);
        if (!result) {
            return false;
        }
        node = literal_for(node, IrOp::Integer, spelling(*result));
        return true;
    }
    if (const auto result = compare_with(op, compare(*left, *right))) {
        node = boolean_for(node, *result);
        return true;
    }
    return false;
}

} // namespace

std::size_t fold_constants(IrProgram& program) {
    return rewrite_nodes(program, [](IrNode& node) {
        if (node.op == IrOp::Unary) return fold_unary(node);
        if (node.op == IrOp::Binary) return fold_binary(node);
        return false;
    });
}

} // namespace neura::optimization

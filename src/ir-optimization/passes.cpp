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

// Replaces `node` with one of its own children.
void forward(IrNode& node, IrNode& child) {
    IrNode kept = std::move(child);
    node = std::move(kept);
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

std::size_t simplify_short_circuits(IrProgram& program) {
    return rewrite_nodes(program, [&program](IrNode& node) {
        if (node.op != IrOp::ShortCircuit || node.operands.size() != 1 || node.body.size() != 1 ||
            (node.text != "&&" && node.text != "||")) {
            return false;
        }
        // The left value that decides the result without evaluating the right
        // operand: false for &&, true for ||.
        const bool decisive = node.text == "||";
        if (const auto left = boolean_literal(node.operands.front())) {
            if (*left == decisive) {
                // The right operand is never evaluated.
                node = boolean_for(node, decisive);
            } else {
                // The right operand is always evaluated and is the result.
                forward(node, node.body.front());
            }
            return true;
        }
        if (const auto right = boolean_literal(node.body.front())) {
            if (*right != decisive) {
                // `left && true` and `left || false` are `left`.
                forward(node, node.operands.front());
                return true;
            }
            // `left && false` is false, but `left` still has to be evaluated
            // unless doing so has no observable effect.
            if (pure_and_total(program, node.operands.front())) {
                node = boolean_for(node, decisive);
                return true;
            }
        }
        return false;
    });
}

} // namespace neura::optimization

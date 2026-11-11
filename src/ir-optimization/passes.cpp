#include "passes.hpp"

#include "analysis.hpp"
#include "integer_folding.hpp"

#include <algorithm>
#include <iterator>
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

// Whether a module or file global's initializer is written as a literal
// decides when the global gets its value: a literal one holds it when the
// image loads, any other runs at program start in source order (Modules.md,
// Initialization). Rewrites therefore never replace the root of a global's
// initializer, only the expressions inside it.
bool global_initializer(const IrProgram& program, const IrNode& node) {
    return node.op == IrOp::Initialize && node.symbol < program.symbols.size() &&
           program.symbols[node.symbol].kind == SymbolKind::Global && node.operands.size() == 1;
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
        for (auto& instruction : body.instructions) {
            if (global_initializer(program, instruction)) {
                auto& value = instruction.operands.front();
                post_order(value.operands, counted);
                post_order(value.body, counted);
                post_order(value.alternative, counted);
                continue;
            }
            std::vector<IrNode> single;
            single.push_back(std::move(instruction));
            post_order(single, counted);
            instruction = std::move(single.front());
        }
    }
    return count;
}

// Visits statement regions innermost first. Statement regions are bodies and
// the regions of blocks, branches, loops, and task scopes. The lazily evaluated operand of
// a short-circuit operation is an expression, not a statement region.
template <typename Rewrite>
void statement_regions(std::vector<IrNode>& region, Rewrite& rewrite) {
    for (auto& statement : region) {
        if (statement.op == IrOp::Block || statement.op == IrOp::Branch || statement.op == IrOp::Loop ||
            statement.op == IrOp::TaskScope) {
            statement_regions(statement.body, rewrite);
            statement_regions(statement.alternative, rewrite);
        }
    }
    rewrite(region);
}

template <typename Rewrite>
std::size_t rewrite_regions(IrProgram& program, Rewrite rewrite) {
    std::size_t count = 0;
    auto counted = [&](std::vector<IrNode>& region) { count += rewrite(region); };
    for (auto& body : program.bodies) {
        statement_regions(body.instructions, counted);
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

} // namespace

std::size_t fold_constants(IrProgram& program) {
    return rewrite_nodes(program, fold_scalar_literals);
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

std::size_t simplify_control_flow(IrProgram& program) {
    return rewrite_regions(program, [](std::vector<IrNode>& region) {
        std::size_t count = 0;
        std::vector<IrNode> kept;
        kept.reserve(region.size());
        for (auto& statement : region) {
            const auto condition = (statement.op == IrOp::Branch || statement.op == IrOp::Loop) &&
                                           statement.operands.size() == 1
                                       ? boolean_literal(statement.operands.front())
                                       : std::nullopt;
            if (statement.op == IrOp::Branch && condition) {
                // The selected region keeps its own scope as a block.
                ++count;
                auto& selected = *condition ? statement.body : statement.alternative;
                if (!selected.empty()) {
                    kept.push_back({IrOp::Block, statement.type, statement.span, no_symbol, {}, {},
                                    std::move(selected), {}, {}});
                }
            } else if ((statement.op == IrOp::Loop && condition == false) ||
                       (statement.op == IrOp::Block && statement.body.empty())) {
                // A `while (false)` body never runs, and an empty block does nothing.
                ++count;
            } else {
                kept.push_back(std::move(statement));
            }
        }
        region = std::move(kept);
        return count;
    });
}

std::size_t remove_unreachable(IrProgram& program) {
    return rewrite_regions(program, [](std::vector<IrNode>& region) -> std::size_t {
        const auto last = std::ranges::find_if(region, never_completes);
        if (last == region.end()) {
            return 0;
        }
        const auto removed = static_cast<std::size_t>(std::distance(std::next(last), region.end()));
        region.erase(std::next(last), region.end());
        return removed;
    });
}

std::size_t remove_pure_discards(IrProgram& program) {
    return rewrite_regions(program, [&program](std::vector<IrNode>& region) -> std::size_t {
        return std::erase_if(region, [&program](const IrNode& statement) {
            return statement.op == IrOp::Discard && statement.operands.size() == 1 &&
                   pure_and_total(program, statement.operands.front());
        });
    });
}

} // namespace neura::optimization

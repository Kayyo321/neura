#include "optimizer.hpp"

#include "neura-ir/verifier.hpp"
#include "passes.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <tuple>

namespace neura {
namespace {

using PassFunction = std::size_t (*)(IrProgram&);

struct RegisteredPass {
    OptimizationPass info;
    PassFunction run;
    // Runs once each time the other passes reach a fixed point, instead of
    // in the rounds.
    bool between_rounds = false;
};

const std::vector<RegisteredPass>& registry() {
    using namespace optimization;
    static const std::vector<RegisteredPass> passes{
        {{"fold-constants",
          "fold !, unary -, arithmetic, and comparisons of bool and fixed-width integer literals"},
         fold_constants},
        {{"simplify-short-circuit", "simplify && and || with a literal operand without evaluating more"},
         simplify_short_circuits},
        {{"propagate-copies", "replace reads of a local assigned once from a literal or an unchanged local"},
         propagate_copies},
        {{"eliminate-common-subexpressions",
          "reuse a local's value for a later identical expression whose operands are unchanged"},
         eliminate_common_subexpressions},
        {{"hoist-loop-invariants", "move a loop's pure and total invariant local initializations before it"},
         hoist_loop_invariants},
        {{"simplify-control-flow", "select branches with literal conditions, remove while (false) and empty blocks"},
         simplify_control_flow},
        {{"remove-unreachable", "remove statements after a statement that never completes"},
         remove_unreachable},
        {{"remove-dead-stores", "remove stores to value-typed locals that are never read or are overwritten"},
         remove_dead_stores},
        {{"remove-pure-discards", "remove discarded expressions that have no effect and cannot fail"},
         remove_pure_discards},
        {{"inline-calls", "inline small calls of functions that cannot trap, call, allocate, or touch the heap"},
         inline_calls, true},
    };
    return passes;
}

// The termination measures: the rounds decrease the number of nodes, then
// the sum over nodes of the loops around them, lexicographically; inlining
// decreases the number of calls, which no pass increases.
struct Measure {
    std::size_t nodes = 0;
    std::size_t loop_weight = 0;
    std::size_t calls = 0;
};

void measure(const std::vector<IrNode>& nodes, std::size_t depth, Measure& result) {
    for (const auto& node : nodes) {
        ++result.nodes;
        result.loop_weight += depth;
        if (node.op == IrOp::Call || node.op == IrOp::VirtualCall || node.op == IrOp::CallClosure) ++result.calls;
        const auto inner = node.op == IrOp::Loop ? depth + 1 : depth;
        measure(node.operands, inner, result);
        measure(node.body, inner, result);
        measure(node.alternative, inner, result);
    }
}

Measure measure(const IrProgram& program) {
    Measure result;
    for (const auto& body : program.bodies) {
        measure(body.instructions, 0, result);
    }
    return result;
}

} // namespace

const std::vector<OptimizationPass>& optimization_passes() {
    static const std::vector<OptimizationPass> passes = [] {
        std::vector<OptimizationPass> result;
        for (const auto& pass : registry()) {
            result.push_back(pass.info);
        }
        return result;
    }();
    return passes;
}

std::vector<std::string> all_pass_names() {
    std::vector<std::string> names;
    for (const auto& pass : registry()) {
        names.emplace_back(pass.info.name);
    }
    return names;
}

std::size_t OptimizationReport::rewrites(std::string_view pass) const {
    const auto entry = std::ranges::find(passes, pass, &PassStatistics::name);
    return entry == passes.end() ? 0 : entry->rewrites;
}

std::size_t OptimizationReport::total_rewrites() const {
    std::size_t total = 0;
    for (const auto& pass : passes) {
        total += pass.rewrites;
    }
    return total;
}

OptimizationReport optimize(IrProgram& program, const OptimizationOptions& options) {
    for (auto name = options.passes.begin(); name != options.passes.end(); ++name) {
        if (std::ranges::none_of(registry(), [&](const auto& pass) { return pass.info.name == *name; })) {
            throw std::invalid_argument("unknown optimization pass '" + *name + "'");
        }
        if (std::find(std::next(name), options.passes.end(), *name) != options.passes.end()) {
            throw std::invalid_argument("optimization pass '" + *name + "' is listed more than once");
        }
    }
    std::vector<const RegisteredPass*> enabled;
    OptimizationReport report;
    for (const auto& pass : registry()) {
        if (std::ranges::find(options.passes, pass.info.name) != options.passes.end()) {
            enabled.push_back(&pass);
            report.passes.push_back({std::string(pass.info.name), 0});
        }
    }

    require_valid_ir(program, "before optimization");
    // Every rewrite in a round removes at least one node, or keeps the node
    // count and moves nodes out of a loop, so each round that changes the
    // program strictly decreases (nodes, loop weight) lexicographically.
    // The rounds therefore end; the check below enforces that measure rather
    // than trusting it. A pass that runs between rounds (inlining) adds nodes
    // but removes at least one call per rewrite and never adds one, so the
    // alternation of rounds and inlining ends too.
    auto size = measure(program);
    for (bool inlined = true; inlined;) {
        for (bool changed = true; changed;) {
            changed = false;
            ++report.rounds;
            for (std::size_t i = 0; i < enabled.size(); ++i) {
                if (enabled[i]->between_rounds) continue;
                const auto rewrites = enabled[i]->run(program);
                if (rewrites == 0) {
                    continue;
                }
                const auto stage = "after pass '" + report.passes[i].name + "' in round " +
                                   std::to_string(report.rounds);
                require_valid_ir(program, stage);
                const auto new_size = measure(program);
                if (std::tie(new_size.nodes, new_size.loop_weight) >= std::tie(size.nodes, size.loop_weight)) {
                    throw std::logic_error("pass reported rewrites without shrinking the program " + stage);
                }
                size = new_size;
                report.passes[i].rewrites += rewrites;
                changed = true;
            }
        }
        inlined = false;
        for (std::size_t i = 0; i < enabled.size(); ++i) {
            if (!enabled[i]->between_rounds) continue;
            const auto rewrites = enabled[i]->run(program);
            if (rewrites == 0) {
                continue;
            }
            const auto stage = "after pass '" + report.passes[i].name + "' following round " +
                               std::to_string(report.rounds);
            require_valid_ir(program, stage);
            const auto new_size = measure(program);
            if (new_size.calls >= size.calls) {
                throw std::logic_error("pass reported rewrites without removing a call " + stage);
            }
            size = new_size;
            report.passes[i].rewrites += rewrites;
            inlined = true;
        }
    }
    return report;
}

} // namespace neura

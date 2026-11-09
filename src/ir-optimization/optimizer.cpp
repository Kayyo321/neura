#include "optimizer.hpp"

#include "neura-ir/verifier.hpp"
#include "passes.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace neura {
namespace {

using PassFunction = std::size_t (*)(IrProgram&);

struct RegisteredPass {
    OptimizationPass info;
    PassFunction run;
};

const std::vector<RegisteredPass>& registry() {
    using namespace optimization;
    static const std::vector<RegisteredPass> passes{
        {{"fold-constants",
          "fold !, unary -, arithmetic, and comparisons of bool and fixed-width integer literals"},
         fold_constants},
        {{"simplify-short-circuit", "simplify && and || with a literal operand without evaluating more"},
         simplify_short_circuits},
        {{"simplify-control-flow", "select branches with literal conditions, remove while (false) and empty blocks"},
         simplify_control_flow},
    };
    return passes;
}

std::size_t count_nodes(const std::vector<IrNode>& nodes) {
    std::size_t count = nodes.size();
    for (const auto& node : nodes) {
        count += count_nodes(node.operands) + count_nodes(node.body) + count_nodes(node.alternative);
    }
    return count;
}

std::size_t count_nodes(const IrProgram& program) {
    std::size_t count = 0;
    for (const auto& body : program.bodies) {
        count += count_nodes(body.instructions);
    }
    return count;
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
    // Every rewrite removes at least one node, so each round that changes the
    // program strictly decreases its node count. The loop therefore ends after
    // at most as many changing rounds as the program had nodes; the check
    // below enforces that measure rather than trusting it.
    auto size = count_nodes(program);
    for (bool changed = true; changed;) {
        changed = false;
        ++report.rounds;
        for (std::size_t i = 0; i < enabled.size(); ++i) {
            const auto rewrites = enabled[i]->run(program);
            if (rewrites == 0) {
                continue;
            }
            const auto stage = "after pass '" + report.passes[i].name + "' in round " +
                               std::to_string(report.rounds);
            require_valid_ir(program, stage);
            const auto new_size = count_nodes(program);
            if (new_size >= size) {
                throw std::logic_error("pass reported rewrites without shrinking the program " + stage);
            }
            size = new_size;
            report.passes[i].rewrites += rewrites;
            changed = true;
        }
    }
    return report;
}

} // namespace neura

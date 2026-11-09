#pragma once

#include "neura-ir/ir.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

struct OptimizationPass {
    std::string_view name;
    std::string_view summary;
};

// Every available pass, in the fixed order the pipeline runs them.
const std::vector<OptimizationPass>& optimization_passes();

// Names of every pass, in pipeline order.
std::vector<std::string> all_pass_names();

struct OptimizationOptions {
    // Names of the passes to enable. Enabled passes always run in pipeline
    // order, whatever order they are listed in.
    std::vector<std::string> passes = all_pass_names();
};

struct PassStatistics {
    std::string name;
    std::size_t rewrites = 0;
};

struct OptimizationReport {
    // Rounds run, including the last round, which changed nothing.
    std::size_t rounds = 0;
    // One entry per enabled pass, in pipeline order.
    std::vector<PassStatistics> passes;

    std::size_t rewrites(std::string_view pass) const;
    std::size_t total_rewrites() const;
};

// Rewrites `program` while preserving its observable behavior. The program is
// verified before the first pass and after every pass that changes it; a
// verification failure throws a Diagnostic naming the boundary. The enabled
// passes run in order, in rounds, until a round changes nothing. Throws
// std::invalid_argument for an unknown or repeated pass name.
OptimizationReport optimize(IrProgram& program, const OptimizationOptions& options = {});

} // namespace neura

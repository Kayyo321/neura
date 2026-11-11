#pragma once

#include "fixtures.hpp"

namespace neura::testing {

inline nvm::RunLimits graph_test_limits(const CorpusImage& item) {
    nvm::RunLimits limits;
    // Bound endless-loop fixtures as in the decompiler corpus tests.
    limits.max_steps = 2'000'000;
    limits.profile = true;
    if (item.label.starts_with("traps/range-heap-limit.nra")) {
        limits.max_heap_cells = 32;
    }
    return limits;
}

inline void check_same_trap(const nvm::Trap& actual, const nvm::Trap& expected, const std::string& label) {
    check(actual.kind == expected.kind, label + " trap kind");
    check_equal(actual.reason, expected.reason, label + " trap reason");
    check_equal(actual.detail, expected.detail, label + " trap detail");
    check_equal(actual.function, expected.function, label + " trap function");
    check_equal(actual.block, expected.block, label + " trap block");
    check(actual.callers == expected.callers, label + " trap callers");
}

inline void check_same_run(const nvm::RunResult& actual, const nvm::RunResult& expected, const std::string& label) {
    check_equal(actual.completed, expected.completed, label + " completed");
    check_equal(actual.status, expected.status, label + " status");
    check_same_trap(actual.trap, expected.trap, label);
    check_equal(actual.steps, expected.steps, label + " steps");
    check_equal(actual.host_requests, expected.host_requests, label + " host requests");
    check_equal(actual.peak_heap_cells, expected.peak_heap_cells, label + " peak heap cells");
    check(actual.function_steps == expected.function_steps, label + " function steps");
}

inline void check_same_request(const nvm::HostRequest& actual, const nvm::HostRequest& expected,
                               const std::string& label) {
    check_equal(actual.sequence, expected.sequence, label + " request sequence");
    check_equal(actual.import, expected.import, label + " import");
    check(actual.arguments == expected.arguments, label + " request arguments");
}

} // namespace neura::testing

#pragma once

#include "evaluator.hpp"
#include "support.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace neura::testing {

// The quanta every concurrent fixture runs under (Concurrency.md section
// 10.2): the smallest, so that every poll is a possible switch, three small
// ones that interleave differently, and the default.
inline constexpr std::array<std::size_t, 5> concurrency_quanta{1, 2, 3, 7, 1024};

// Task limits every concurrent fixture runs under: the default, and one low
// enough that fixtures with several live tasks stop at a spawn.
inline constexpr std::array<std::size_t, 2> concurrency_task_limits{65'536, 4};

struct ConcurrencyFixture {
    // The file name without its extension.
    std::string name;
    fs::path path;
    // Whether it comes from tests/concurrency/programs (the compiler's own
    // end-to-end fixtures) or from tests/ir-optimization/fixtures/concurrency.
    bool end_to_end = false;
};

// Every fixture, sorted by name within each directory: first the programs of
// tests/concurrency/programs, then the IR-focused ones.
std::vector<ConcurrencyFixture> concurrency_fixtures(const Environment& environment);

// Lowers `path` and prepares it for evaluation and conversion the way
// backend::build does before optimization: reference storage is materialized
// and function values are specialized.
IrProgram prepare_concurrent(const fs::path& path);

// Runs Main.main of `program` under the scheduler: file and module
// initializers first, then the entry with no arguments.
Outcome run_concurrent(const IrProgram& program, std::size_t quantum, std::size_t max_tasks = 65'536);

// The process status a returned entry stands for: 0 for ExitCode.Ok, 1 for
// ExitCode.Failure, and the payload of ExitCode.Status.
std::optional<int> entry_status(const IrProgram& program, const Outcome& outcome);

// A one-line summary of an Outcome for failure messages.
std::string summarize(const Outcome& outcome);

} // namespace neura::testing

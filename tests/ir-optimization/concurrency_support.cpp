#include "concurrency_support.hpp"

#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"

#include <algorithm>
#include <sstream>

namespace neura::testing {
namespace {

std::vector<fs::path> sorted_sources(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".nra") {
            files.push_back(fs::canonical(entry.path()));
        }
    }
    std::ranges::sort(files);
    return files;
}

} // namespace

std::vector<ConcurrencyFixture> concurrency_fixtures(const Environment& environment) {
    std::vector<ConcurrencyFixture> fixtures;
    for (const auto& file : sorted_sources(environment.source_dir / "tests" / "concurrency" / "programs")) {
        fixtures.push_back({"programs/" + file.stem().string(), file, true});
    }
    for (const auto& file : sorted_sources(environment.fixtures() / "concurrency")) {
        fixtures.push_back({"ir/" + file.stem().string(), file, false});
    }
    return fixtures;
}

IrProgram prepare_concurrent(const fs::path& path) {
    auto program = lower_files({path});
    materialize_reference_storage(program);
    specialize(program);
    return program;
}

Outcome run_concurrent(const IrProgram& program, std::size_t quantum, std::size_t max_tasks) {
    Limits limits;
    // The detached fixture starts twenty thousand tasks; none of the
    // fixtures should reach the budget, so a reached budget is a failure.
    limits.budget = 20'000'000;
    limits.depth = 400;
    limits.quantum = quantum;
    limits.max_tasks = max_tasks;
    Evaluator evaluator(program, limits);
    for (const auto& initializer : evaluator.initialize()) {
        check(initializer.status == Status::Returned, "initializer: " + describe(initializer));
    }
    return evaluator.call_entry(find_symbol(program, "Main.main"));
}

std::optional<int> entry_status(const IrProgram& program, const Outcome& outcome) {
    if (outcome.status != Status::Returned || !outcome.result || outcome.result->kind != Value::Kind::Variant) {
        return std::nullopt;
    }
    const auto& value = *outcome.result;
    const auto& name = program.symbols.at(value.symbol).name;
    if (name == "Ok") return 0;
    if (name == "Failure") return 1;
    if (name == "Status" && value.payload.size() == 1) return static_cast<int>(value.payload.front().magnitude);
    return std::nullopt;
}

std::string summarize(const Outcome& outcome) {
    std::ostringstream out;
    out << describe(outcome.status);
    if (outcome.status == Status::Exited) out << ' ' << outcome.exit_status;
    if (outcome.status != Status::Returned && outcome.status != Status::Exited) out << " (" << outcome.reason << ')';
    out << ", " << outcome.effects.size() << " effect(s), " << outcome.schedule.size() << " tick(s)";
    return out.str();
}

} // namespace neura::testing

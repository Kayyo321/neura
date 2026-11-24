#pragma once

#include "options.hpp"

#include <chrono>
#include <ctime>
#include <set>

namespace neura::cli::benchmark {

// Wall-clock boundaries within the source compilation and artifact export.
struct BuildTiming {
    std::chrono::steady_clock::time_point encoded;
    std::clock_t encoded_cpu = -1;
    std::chrono::steady_clock::time_point written;
    std::optional<std::chrono::steady_clock::time_point> validated;
};

int export_suite(const Options&, const fs::path& image, const fs::path& catalog, BuildTiming&);
int export_program(const Options&, BuildTiming&);
std::string test_failure(const nvm::RunResult&, const nvr::Image&, const std::set<std::string>& source_labels);

} // namespace neura::cli::benchmark

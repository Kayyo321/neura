#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"
#include "nvm/tier1.hpp"
#include "nvr/format.hpp"
#include "runner/runner.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>

namespace {

class Capture final : public neura::nvm::ConsoleSink {
public:
    bool write(std::string_view bytes) override {
        value.append(bytes);
        return true;
    }

    std::string value;
};

double cpu_time() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage failed");
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1'000'000;
}

} // namespace

int main(int argc, char** argv) try {
    if (argc != 7) throw std::runtime_error("usage: warm-neura IMAGE SIZE WARMUPS SAMPLES STEP_LIMIT HEAP_LIMIT");
    const auto image = neura::nvr::load(neura::runner::read_file(argv[1]));
    const std::string size = argv[2];
    const int warmups = std::stoi(argv[3]);
    const int samples = std::stoi(argv[4]);
    if (warmups < 1 || samples < 1) throw std::runtime_error("positive warmups and samples required");

    Capture output;
    Capture errors;
    neura::nvm::DescriptorSource input{0};
    neura::nvm::ConsoleWriteLine console{output};
    neura::nvm::StandardServices services{output, errors, input, {}};
    neura::nvm::HostRegistry registry;
    registry.add(console);
    services.register_all(registry);
    const auto bindings = neura::nvm::bind(image, registry);
    neura::nvm::TierCache tiers(image);
    neura::nvm::RunLimits limits;
    limits.tier_cache = &tiers;
    limits.max_steps = std::stoull(argv[5]);
    limits.max_heap_cells = std::stoull(argv[6]);

    std::string expected;
    std::uint64_t steps = 0;
    std::uint64_t cells = 0;
    std::cerr << std::setprecision(9);
    for (int iteration = 0; iteration < warmups + samples; ++iteration) {
        output.value.clear();
        errors.value.clear();
        const auto cpu_start = cpu_time();
        const auto start = std::chrono::steady_clock::now();
        const auto result = neura::nvm::run(image, bindings, std::span{&size, 1}, limits);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        const auto cpu_seconds = cpu_time() - cpu_start;
        services.end_run();
        if (!result.completed || result.status || !errors.value.empty()) {
            throw std::runtime_error("warmed workload failed");
        }
        if (iteration == 0) {
            expected = output.value;
            steps = result.steps;
            cells = result.peak_heap_cells;
        } else if (output.value != expected || steps != result.steps || cells != result.peak_heap_cells) {
            throw std::runtime_error("warmed workload result changed");
        }
        if (iteration >= warmups) std::cerr << "sample " << elapsed.count() << ' ' << cpu_seconds << '\n';
    }
    std::cerr << "stats: steps " << steps << ", peak heap cells " << cells << '\n';
    std::cout << expected;
    return 0;
} catch (const std::exception& error) {
    std::cerr << "warm-neura: " << error.what() << '\n';
    return 1;
}

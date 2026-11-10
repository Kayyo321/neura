#include "backend/converter.hpp"
#include "graph_test_support.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/fleet.hpp"
#include "nvm/providers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>

namespace neura::testing {
namespace {

struct TestImage {
    nvr::Image image;
    std::uint32_t entries;
};

std::vector<TestImage> compile_area(const fs::path& directory) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".nra") files.push_back(entry.path());
    }
    std::ranges::sort(files);
    std::vector<TestImage> images;
    for (const auto& file : files) {
        auto program = lower_files({file}, {directory / "support"});
        require_valid_ir(program, "before specialization");
        backend::ConversionOptions options;
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto& symbol = program.symbols[id];
            if (symbol.kind == SymbolKind::Function && symbol.exported && symbol.has_body &&
                symbol.name.starts_with("test_") && symbol.parameters.empty() && symbol.generics.empty() &&
                symbol.type.kind == TypeKind::Void && symbol.owner != no_symbol &&
                program.symbols[symbol.owner].kind == SymbolKind::Module &&
                !is_static_scope(program.symbols[symbol.owner]) && program.symbols[symbol.owner].generics.empty() &&
                fs::equivalent(file, symbol.span.file)) {
                options.entries.push_back(id);
            }
        }
        check(!options.entries.empty(), "each benchmark source declares tests");
        materialize_reference_storage(program);
        specialize(program);
        optimize(program);
        auto image = backend::convert(program, options).image;
        images.push_back({nvr::load(nvr::encode(image)), static_cast<std::uint32_t>(options.entries.size())});
    }
    check(!images.empty(), "benchmark area has test files");
    return images;
}

struct AreaResult {
    std::vector<nvm::RunResult> runs;
    std::string output;
    std::string error;
    double seconds = 0;
};

AreaResult run_area(std::vector<TestImage>& images, bool use_fleet) {
    AreaResult result;
    nvm::StringSink output;
    nvm::StringSink error;
    nvm::StringSource input("");
    nvm::ConsoleWriteLine console(output);
    nvm::StandardServices services(output, error, input, {});
    nvm::HostRegistry registry;
    registry.add(console);
    services.register_all(registry);
    const auto finish = [&](const nvm::RunResult& run) {
        services.end_run();
        result.runs.push_back(run);
    };
    const auto start = std::chrono::steady_clock::now();
    for (auto& [image, entries] : images) {
        const auto bindings = nvm::bind(image, registry);
        if (use_fleet) {
            nvm::Fleet fleet(image, {});
            for (std::uint32_t i = 0; i < entries; ++i) fleet.add(i, {});
            nvm::run_fleet(fleet, bindings, [&](std::size_t, const nvm::RunResult& run) { finish(run); });
        } else {
            for (std::uint32_t i = 0; i < entries; ++i) {
                image.entry = i;
                finish(nvm::run(image, bindings, {}));
            }
            image.entry = 0;
        }
    }
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.output = output.contents();
    result.error = error.contents();
    return result;
}

} // namespace

// Opt-in measurement so CTest does not depend on wall time.
NEURA_TEST(fleet_benchmark, stdlib_areas) {
    constexpr std::size_t sample_count = 9;
    constexpr double bound = 1.10;
    bool within_bound = true;
    for (const auto* area : {"array-deque", "core", "integers"}) {
        auto images = compile_area(environment.source_dir / "tests" / "stdlib" / area);
        const auto reference = run_area(images, false);
        check(!reference.runs.empty(), "serial runner executed the tests");
        for (const auto& run : reference.runs) {
            check(run.completed && run.status == 0, "serial standard library test passed");
        }
        run_area(images, true);
        std::array<double, sample_count> serial_samples{};
        std::array<double, sample_count> fleet_samples{};
        for (std::size_t sample = 0; sample < sample_count; ++sample) {
            const auto first = run_area(images, sample % 2 != 0);
            const auto second = run_area(images, sample % 2 == 0);
            const auto& serial = sample % 2 == 0 ? first : second;
            const auto& fleet = sample % 2 == 0 ? second : first;
            check_equal(fleet.runs.size(), serial.runs.size(), "fleet member count");
            check_equal(fleet.output, serial.output, "fleet standard output");
            check_equal(fleet.error, serial.error, "fleet standard error");
            for (std::size_t i = 0; i < serial.runs.size(); ++i) {
                check_same_run(fleet.runs[i], serial.runs[i], area);
            }
            serial_samples[sample] = serial.seconds;
            fleet_samples[sample] = fleet.seconds;
        }
        std::ranges::sort(serial_samples);
        std::ranges::sort(fleet_samples);
        const auto serial = serial_samples[sample_count / 2];
        const auto fleet = fleet_samples[sample_count / 2];
        const auto ratio = fleet / serial;
        std::cout << area << ": " << reference.runs.size() << " tests in " << images.size()
                  << " images, serial " << serial << " s, fleet " << fleet << " s, ratio " << ratio
                  << " (bound 1.10)" << std::endl;
        within_bound = within_bound && ratio <= bound;
    }
    check(within_bound, "fleet wall time is within the phase 2 bound in every measured area");
}

} // namespace neura::testing

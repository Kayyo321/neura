#include "nvm/machine.hpp"
#include "nvm/tier1.hpp"
#include "nvr/format.hpp"

#include <cstdio>
#include <stdexcept>

// Step 1 smoke driver only. Node supplies the serialized artifact as bytes.
// The browser entry, asynchronous hosts and proof yields are separate.
extern "C" int neura_smoke(const std::uint8_t* bytes, std::size_t size, unsigned mode) {
    try {
        const auto image = neura::nvr::load(std::span(bytes, size));
        auto options = neura::nvm::TierOptions::browser();
        if (mode == 2) options.hint_after = options.search_after = options.region_after = 1;
        neura::nvm::TierCache cache(image, options);

        neura::nvm::RunLimits limits;
        limits.tier = mode == 0 ? 0 : 2;
        limits.jit = neura::jit::Mode::Off;
        limits.tier_cache = &cache;
        if (mode == 3) limits.max_frames = 0;
        if (mode == 4) limits.max_heap_cells = 1;
        if (mode == 5) limits.max_stack_cells = 4;
        std::uint64_t steps = 0;
        std::uint64_t heap = 0;
        std::uint64_t stack = 0;
        unsigned requests = 0;
        int status = 0;
        {
            neura::nvm::Execution execution(image, std::span<const std::string>{}, limits);
            for (;;) {
                const auto event = execution.advance();
                if (event.kind == neura::nvm::Event::Kind::Completed) {
                    status = event.status;
                } else if (event.kind == neura::nvm::Event::Kind::Trapped) {
                    status = neura::nvm::exit_status(event.trap);
                    std::fprintf(stderr, "trap=%s\n", neura::nvm::describe(event.trap, image).c_str());
                } else if (event.kind == neura::nvm::Event::Kind::HostRequest) {
                    const auto& import = image.imports.at(event.request.import);
                    if (import.name != "neura.console.write_line" || import.interface_version != 1 ||
                        event.request.arguments.size() != 1) throw std::runtime_error("unexpected smoke import");
                    const auto& text = event.request.arguments[0].bytes;
                    std::fwrite(text.data(), 1, text.size(), stdout);
                    std::putchar('\n');
                    execution.complete(neura::nvm::HostCompletion::ok());
                    ++requests;
                    continue;
                } else {
                    throw std::runtime_error("unexpected smoke event");
                }
                steps = execution.steps();
                heap = execution.peak_heap_cells();
                stack = execution.peak_stack_cells();
                break;
            }
        }
        const auto stats = cache.statistics();
        std::fprintf(stderr, "steps=%llu peak=%llu requests=%u programs=%llu program_steps=%llu jit_bytes=%llu "
                     "native_programs=%llu stack_peak=%llu\n",
                     static_cast<unsigned long long>(steps), static_cast<unsigned long long>(heap), requests,
                     static_cast<unsigned long long>(stats.programs),
                     static_cast<unsigned long long>(stats.region_counts.program_steps),
                     static_cast<unsigned long long>(stats.jit_bytes), static_cast<unsigned long long>(stats.native_programs),
                     static_cast<unsigned long long>(stack));
        return status;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}

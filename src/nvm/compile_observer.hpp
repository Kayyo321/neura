#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

namespace neura::nvm {

// An opt-in diagnostic of Tier 3 compilation. A shared cache can report this
// work from several threads. The listener must outlive the cache and its runs.
struct CompileEvent {
    std::string_view kind;
    std::string_view outcome;
    std::uint32_t function;
    std::uint32_t program;
    std::uint32_t variant;
    std::uint32_t entry;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    std::thread::id thread;
    std::int64_t cpu_nanoseconds;
};

class CompileObserver {
public:
    virtual ~CompileObserver() = default;
    virtual void compilation(const CompileEvent&) noexcept = 0;
};

} // namespace neura::nvm

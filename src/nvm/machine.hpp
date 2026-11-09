#pragma once

#include "host.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace neura::nvm {

// Bounds a run enforces. Exhaustion traps at the operation that would exceed
// the bound, before the allocation, frame, or block it would start.
struct RunLimits {
    // Block evaluations, counting every block entered.
    std::uint64_t max_steps = 100'000'000;
    // Active frames, including the entry frame.
    std::uint32_t max_frames = 10'000;
    // Live heap storage: one cell per heap entry plus one per object field,
    // collection element, and text or bytes byte.
    std::uint64_t max_heap_cells = std::uint64_t{1} << 26;
    // Reference handles assigned during the run. Handles are never reused.
    std::uint64_t max_handles = std::uint64_t{1} << 32;
};

enum class TrapKind {
    // A trap exit selected by the program, with its reason code.
    Program,
    // Runtime faults detected by NVM.
    GraphOverflow,
    NonBinaryLane,
    InvalidSelector,
    UninitializedSlot,
    UninitializedField,
    UninitializedGlobal,
    InvalidHandle,
    ReferenceType,
    IndexOutOfBounds,
    InvalidText,
    InvalidCompletion,
    // Resource limits.
    StepLimit,
    FrameLimit,
    HeapLimit,
    HandleLimit,
};

struct Trap {
    TrapKind kind = TrapKind::Program;
    // Reason code of a program trap.
    std::uint32_t reason = 0;
    // Function and block that were executing, or nvr::absent before entry.
    std::uint32_t function = nvr::absent;
    std::uint32_t block = nvr::absent;
    std::string detail;
};

std::string_view describe(TrapKind kind);
// A one-line diagnostic for `trap`, with the source location from the image's
// source map when it has one.
std::string describe(const Trap& trap, const nvr::Image& image);

// Evaluates `graph` with the checked arithmetic of graph profile 1. `wires`
// holds the input lanes on entry and every wire on return. Returns the index
// of the first node whose weighted sum overflows signed 64-bit arithmetic,
// or nothing when evaluation succeeds.
std::optional<std::size_t> evaluate_graph(const nvr::Graph& graph, std::vector<std::int64_t>& wires);

struct Event {
    enum class Kind { Completed, Trapped, HostRequest };

    Kind kind = Kind::Completed;
    // The entry function's int32 result, when completed.
    std::int32_t status = 0;
    // The returned value's slot encoding, when completed: the low bits of a
    // Boolean or integer, or a reference handle.
    std::uint64_t result = 0;
    Trap trap;
    HostRequest request;
};

// One run of a validated image. The run owns its frames, heap, globals, and
// counters; constructing another Execution starts again from the image.
class Execution {
public:
    // Throws std::invalid_argument when an argument is not valid UTF-8.
    Execution(const nvr::Image& image, std::span<const std::string> arguments, RunLimits limits = {});
    // Starts at `function` instead of the entry point, for embedders and
    // tests. Every parameter must be a Boolean or integer, given in its slot
    // encoding. Throws std::invalid_argument for a mismatched argument list.
    Execution(const nvr::Image& image, std::uint32_t function, std::span<const std::uint64_t> arguments,
              RunLimits limits = {});
    ~Execution();
    Execution(const Execution&) = delete;
    Execution& operator=(const Execution&) = delete;

    // Runs blocks until the run completes, traps, or selects a host call
    // exit. A host request suspends the run until complete() supplies its
    // response. Advancing a finished run returns its final event again.
    Event advance();

    // Supplies the response to the pending host request. The value is
    // type-checked against the import; an ill-typed response traps.
    void complete(const HostCompletion& completion);

    std::uint64_t steps() const;

private:
    class State;
    std::unique_ptr<State> state_;
};

struct RunResult {
    bool completed = false;
    std::int32_t status = 0;
    Trap trap;
    std::uint64_t steps = 0;
    std::uint64_t host_requests = 0;
};

// Runs `image` to completion, issuing each host request to its bound service
// and waiting for the response before continuing.
RunResult run(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
              RunLimits limits = {});

} // namespace neura::nvm

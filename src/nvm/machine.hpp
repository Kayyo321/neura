#pragma once

#include "host.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace neura::nvm {

// Bounds a run enforces. Exhaustion traps at the operation that would exceed
// the bound, before the allocation, frame, or block it would start.
struct RunLimits {
    // Block evaluations, counting every block entered.
    std::uint64_t max_steps = 100'000'000;
    // Active frames, including the entry frame.
    std::uint32_t max_frames = 10'000;
    // Heap storage after reclaiming unreachable entries: one cell per entry
    // plus one per object field, collection element, and text or bytes byte.
    std::uint64_t max_heap_cells = std::uint64_t{1} << 26;
    // Reference handles assigned during the run. Handles are never reused.
    std::uint64_t max_handles = std::uint64_t{1} << 32;
    // Count the steps of each function, for `neura run --profile`. Counting
    // does not change execution.
    bool profile = false;
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
    NilReference,
    InvalidCast,
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
    // The calling frames below the trapping one, innermost first, each as
    // its function and the block that made the call.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> callers{};
};

std::string_view describe(TrapKind kind);
// The process status for a run that ends in `trap`. Each class of trap uses
// the status of the POSIX signal its native equivalent raises (128 + n on
// Linux), named by Std.Program.ExitCode: arithmetic 136, bounds, nil and
// uninitialized reads and frame depth 139, explicit failures and failed
// casts 134, invalid dispatch and malformed graphs 132, heap and handle
// exhaustion 137, and the step limit 152.
int exit_status(const Trap& trap);
// A one-line diagnostic for `trap`, with a source location when available.
// Test reports omit internal function and block numbers.
std::string describe(const Trap& trap, const nvr::Image& image, bool include_indices = true);

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
    // type-checked against the import; an ill-typed response traps. After a
    // callback ended the run, the response is ignored.
    void complete(const HostCompletion& completion);

    // While a host request is pending, runs the callback `name` `version`
    // (NVR CALB) with `arguments` until it returns, answering the host
    // requests it makes with `host`. Returns its result, or nothing when the
    // run ended during the callback (a trap, a process exit, or an
    // unknown callback); the next advance() then reports how it ended.
    using HostHandler = std::function<HostCompletion(const HostRequest&)>;
    std::optional<HostValue> invoke_callback(std::string_view name, std::uint32_t version,
                                             const std::vector<HostValue>& arguments, const HostHandler& host);

    std::uint64_t steps() const;
    // The largest live heap size reached so far, in the cells of
    // RunLimits::max_heap_cells.
    std::uint64_t peak_heap_cells() const;
    // Steps per image function, in function table order, when
    // RunLimits::profile is set; otherwise empty.
    const std::vector<std::uint64_t>& function_steps() const;

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
    std::uint64_t peak_heap_cells = 0;
    // Steps per image function when RunLimits::profile is set.
    std::vector<std::uint64_t> function_steps{};
};

// Runs `image` to completion, issuing each host request to its bound service
// and waiting for the response before continuing.
RunResult run(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
              RunLimits limits = {});

} // namespace neura::nvm

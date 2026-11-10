#pragma once

#include "host.hpp"
#include "machine.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace neura::nvm {

struct TraceCallback;

// One host interaction of a run: the request as the import's name and version
// with its arguments, the callbacks the service made while serving it, and
// the response the runner gave.
struct TraceEntry {
    std::string import;
    std::uint32_t version = 0;
    std::vector<HostValue> arguments;
    std::vector<TraceCallback> callbacks;
    HostCompletion completion;
    // The next four belong to the trace of an image with tasks (trace file
    // version 3) and stay at their defaults in every other trace. The task
    // that issued the request, the number of scheduling decisions made when
    // it did, how it was served (0 before the scheduler continued, 1
    // overlapping, 2 overlapping and cancellable), and, for an overlapping
    // request, whether the run ended before its completion was delivered.
    std::uint64_t task = 0;
    std::uint64_t tick = 0;
    std::uint8_t mode = 0;
    bool abandoned = false;

    bool operator==(const TraceEntry& other) const;
};

// A callback into the program during a host request (NVR CALB): its name,
// version, and arguments, the host requests it made, and its result, absent
// when the run ended during it.
struct TraceCallback {
    std::string name;
    std::uint32_t version = 0;
    std::vector<HostValue> arguments;
    std::vector<TraceEntry> entries;
    std::optional<HostValue> result;

    bool operator==(const TraceCallback&) const = default;
};

inline bool TraceEntry::operator==(const TraceEntry& other) const {
    return import == other.import && version == other.version && arguments == other.arguments &&
           callbacks == other.callbacks && completion.success == other.completion.success &&
           completion.value == other.completion.value && completion.exit_status == other.completion.exit_status &&
           task == other.task && tick == other.tick && mode == other.mode && abandoned == other.abandoned;
}

// How a run ended.
struct TraceOutcome {
    bool completed = false;
    std::int32_t status = 0;
    TrapKind trap = TrapKind::Program;
    std::uint32_t reason = 0;
    // A program failure may carry a user-visible diagnostic message.
    std::string message;

    bool operator==(const TraceOutcome&) const = default;
};

// The settings that fix a concurrent run's schedule. A replay uses them in
// place of its own command line.
struct TraceSettings {
    std::uint64_t quantum = 0;
    std::uint64_t max_tasks = 0;
    std::uint64_t max_queued_messages = 0;
    std::uint64_t max_pending_requests = 0;

    bool operator==(const TraceSettings&) const = default;
};

// What the driver did at a scheduling point of an image with tasks, beyond
// answering requests: it delivered the completion of an overlapping request,
// or read a clock that expired a timer or a deadline. `tick` is the number of
// scheduling decisions made when it happened.
struct TraceEvent {
    enum class Kind { Delivery, Clock };
    Kind kind = Kind::Delivery;
    std::uint64_t tick = 0;
    // Delivery: the position, among the trace's entries, of the request.
    std::uint64_t request = 0;
    // Delivery: whether a cancel request had reached the operation before the
    // answer was delivered, whichever answer it was.
    bool cancelled = false;
    // Clock: the reading, in nanoseconds.
    std::int64_t now = 0;

    bool operator==(const TraceEvent&) const = default;
};

struct Trace {
    std::vector<TraceEntry> entries;
    TraceOutcome outcome;
    // Present exactly for the trace of an image with tasks (trace file
    // version 3). `events` lists the deliveries and clock readings in the
    // order they happened.
    std::optional<TraceSettings> settings;
    std::vector<TraceEvent> events;
    // Details of the run that produced this trace, for diagnostics and cost
    // reports. They are not part of the comparison and a trace file does
    // not store them.
    Trap trap{};
    std::uint64_t steps = 0;
    std::uint64_t peak_heap_cells = 0;
};

// Runs `image` with its imports bound to `bindings`, recording every host
// interaction in order and the run's outcome. For an image with tasks the
// run goes through the driver of src/nvm/driver.hpp, with timers following
// `clock` (the steady clock when null), and the trace also records the
// settings, the issuing task, and the deliveries and clock readings.
Trace record(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
             RunLimits limits = {}, TaskClock* clock = nullptr);

// The result of replaying a recorded trace against an image.
struct Replay {
    // True when the image made the same requests, in the same order, by
    // import name and version with the same arguments, and ended the same
    // way. Function and block numbers may differ between the images.
    bool matches = false;
    // The first difference, when it does not match.
    std::string difference;
    Trace trace;
};

// Runs `image` answering each host request from `expected` instead of real
// services, so two images can be compared under identical external
// responses. For an image with tasks, the quantum and the scheduler limits
// in the trace's settings replace those of `limits`, and the deliveries and
// clock readings are applied at the scheduling points they were recorded at.
// `answered`, when given, sees each recorded entry as replay supplies it to
// the run, in request order.
Replay replay(const nvr::Image& image, const Trace& expected, std::span<const std::string> arguments,
              RunLimits limits = {}, const std::function<void(const TraceEntry&)>& answered = {});

} // namespace neura::nvm

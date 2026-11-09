#pragma once

#include "host.hpp"
#include "machine.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace neura::nvm {

// One host interaction of a run: the request as the import's name and version
// with its arguments, and the response the runner gave.
struct TraceEntry {
    std::string import;
    std::uint32_t version = 0;
    std::vector<HostValue> arguments;
    HostCompletion completion;

    bool operator==(const TraceEntry& other) const {
        return import == other.import && version == other.version && arguments == other.arguments &&
               completion.success == other.completion.success && completion.value == other.completion.value;
    }
};

// How a run ended.
struct TraceOutcome {
    bool completed = false;
    std::int32_t status = 0;
    TrapKind trap = TrapKind::Program;
    std::uint32_t reason = 0;

    bool operator==(const TraceOutcome&) const = default;
};

struct Trace {
    std::vector<TraceEntry> entries;
    TraceOutcome outcome;
};

// Runs `image` with its imports bound to `bindings`, recording every host
// interaction in order and the run's outcome.
Trace record(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
             RunLimits limits = {});

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
// responses.
Replay replay(const nvr::Image& image, const Trace& expected, std::span<const std::string> arguments,
              RunLimits limits = {});

} // namespace neura::nvm

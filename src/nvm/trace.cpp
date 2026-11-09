#include "trace.hpp"

namespace neura::nvm {
namespace {

TraceOutcome outcome_of(const Event& event) {
    TraceOutcome outcome;
    outcome.completed = event.kind == Event::Kind::Completed;
    outcome.status = event.status;
    if (!outcome.completed) {
        outcome.trap = event.trap.kind;
        outcome.reason = event.trap.reason;
    }
    return outcome;
}

std::string describe(const TraceOutcome& outcome) {
    if (outcome.completed) {
        return "completion with status " + std::to_string(outcome.status);
    }
    return std::string(describe(outcome.trap)) +
           (outcome.trap == TrapKind::Program ? " (reason " + std::to_string(outcome.reason) + ")" : "");
}

} // namespace

Trace record(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
             RunLimits limits) {
    Execution execution(image, arguments, limits);
    Trace trace;
    for (;;) {
        const auto event = execution.advance();
        if (event.kind != Event::Kind::HostRequest) {
            trace.outcome = outcome_of(event);
            return trace;
        }
        const auto& import = image.imports.at(event.request.import);
        TraceEntry entry;
        entry.import = import.name;
        entry.version = import.interface_version;
        entry.arguments = event.request.arguments;
        entry.completion = bindings.services.at(event.request.import)->invoke(event.request);
        execution.complete(entry.completion);
        trace.entries.push_back(std::move(entry));
    }
}

Replay replay(const nvr::Image& image, const Trace& expected, std::span<const std::string> arguments,
              RunLimits limits) {
    Execution execution(image, arguments, limits);
    Replay result;
    for (;;) {
        const auto event = execution.advance();
        if (event.kind != Event::Kind::HostRequest) {
            result.trace.outcome = outcome_of(event);
            break;
        }
        const auto& import = image.imports.at(event.request.import);
        const auto position = result.trace.entries.size();
        if (position >= expected.entries.size()) {
            result.difference = "request " + std::to_string(position) + " (" + import.name +
                                ") is beyond the recorded trace";
            return result;
        }
        const auto& recorded = expected.entries[position];
        if (recorded.import != import.name || recorded.version != import.interface_version ||
            !(recorded.arguments == event.request.arguments)) {
            result.difference = "request " + std::to_string(position) + " differs: " + import.name + " v" +
                                std::to_string(import.interface_version) + " instead of " + recorded.import +
                                " v" + std::to_string(recorded.version) + ", or different arguments";
            return result;
        }
        result.trace.entries.push_back(recorded);
        execution.complete(recorded.completion);
    }
    if (result.trace.entries.size() != expected.entries.size()) {
        result.difference = "the run made " + std::to_string(result.trace.entries.size()) + " of " +
                            std::to_string(expected.entries.size()) + " recorded requests";
        return result;
    }
    if (!(result.trace.outcome == expected.outcome)) {
        result.difference = "the run ended with " + describe(result.trace.outcome) + " instead of " +
                            describe(expected.outcome);
        return result;
    }
    result.matches = true;
    return result;
}

} // namespace neura::nvm

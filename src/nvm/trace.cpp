#include "trace.hpp"

#include "trace_file.hpp"

namespace neura::nvm {
namespace {

TraceOutcome outcome_of(const Event& event) {
    TraceOutcome outcome;
    outcome.completed = event.kind == Event::Kind::Completed;
    outcome.status = event.status;
    if (!outcome.completed) {
        outcome.trap = event.trap.kind;
        outcome.reason = event.trap.reason;
        if (outcome.trap == TrapKind::Program &&
            outcome.reason == static_cast<std::uint32_t>(nvr::TrapReason::ProgramFailure)) {
            outcome.message = event.trap.detail;
        }
    }
    return outcome;
}

std::string describe(const TraceOutcome& outcome) {
    if (outcome.completed) {
        return "completion with status " + std::to_string(outcome.status);
    }
    auto text = std::string(describe(outcome.trap));
    if (outcome.trap == TrapKind::Program) text += " (reason " + std::to_string(outcome.reason) + ")";
    if (!outcome.message.empty()) text += ": " + outcome.message;
    return text;
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
            trace.trap = event.trap;
            trace.steps = execution.steps();
            trace.peak_heap_cells = execution.peak_heap_cells();
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
              RunLimits limits, const std::function<void(const TraceEntry&)>& answered) {
    Execution execution(image, arguments, limits);
    Replay result;
    for (;;) {
        const auto event = execution.advance();
        if (event.kind != Event::Kind::HostRequest) {
            result.trace.outcome = outcome_of(event);
            result.trace.trap = event.trap;
            result.trace.steps = execution.steps();
            result.trace.peak_heap_cells = execution.peak_heap_cells();
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
        if (recorded.import != import.name || recorded.version != import.interface_version) {
            result.difference = "request " + std::to_string(position) + " differs: the run requested " +
                                import.name + " version " + std::to_string(import.interface_version) +
                                " where the trace recorded " + recorded.import + " version " +
                                std::to_string(recorded.version);
            return result;
        }
        if (!(recorded.arguments == event.request.arguments)) {
            std::size_t argument = 0;
            while (argument < recorded.arguments.size() && argument < event.request.arguments.size() &&
                   recorded.arguments[argument] == event.request.arguments[argument]) {
                ++argument;
            }
            const auto shown = [](const std::vector<HostValue>& values, std::size_t index) {
                if (index >= values.size()) return std::string("nothing");
                auto text = format_trace_value(values[index]);
                if (text.size() > 120) text = text.substr(0, 117) + "...";
                return text;
            };
            result.difference = "request " + std::to_string(position) + " (" + import.name + " version " +
                                std::to_string(import.interface_version) + ") differs in argument " +
                                std::to_string(argument) + ": the run passed " +
                                shown(event.request.arguments, argument) + " where the trace recorded " +
                                shown(recorded.arguments, argument);
            return result;
        }
        result.trace.entries.push_back(recorded);
        if (answered) answered(recorded);
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

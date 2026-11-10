#include "trace.hpp"

#include "trace_file.hpp"

#include <stdexcept>

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

namespace {

// Serves requests with bound services, recording each request and the
// callbacks made while serving it.
class Recorder final : public CallbackContext {
public:
    Recorder(Execution& execution, const Bindings& bindings) : execution_(execution), bindings_(bindings) {}

    TraceEntry serve(const nvr::Image& image, const HostRequest& request) {
        const auto& import = image.imports.at(request.import);
        TraceEntry entry;
        entry.import = import.name;
        entry.version = import.interface_version;
        entry.arguments = request.arguments;
        auto* outer = callbacks_;
        callbacks_ = &entry.callbacks;
        auto with_callbacks = request;
        with_callbacks.callbacks = this;
        entry.completion = bindings_.services.at(request.import)->invoke(with_callbacks);
        callbacks_ = outer;
        return entry;
    }

    void set_image(const nvr::Image& image) { image_ = &image; }

    std::optional<HostValue> call(std::string_view name, std::uint32_t version,
                                  const std::vector<HostValue>& arguments) override {
        TraceCallback callback;
        callback.name = std::string(name);
        callback.version = version;
        callback.arguments = arguments;
        auto* sink = callbacks_;
        callback.result = execution_.invoke_callback(name, version, arguments, [&](const HostRequest& nested) {
            callback.entries.push_back(serve(*image_, nested));
            return callback.entries.back().completion;
        });
        if (sink != nullptr) sink->push_back(callback);
        return callback.result;
    }

private:
    Execution& execution_;
    const Bindings& bindings_;
    const nvr::Image* image_ = nullptr;
    std::vector<TraceCallback>* callbacks_ = nullptr;
};

// Answers requests from recorded entries and replays the callbacks recorded
// with each one before its response.
class Replayer {
public:
    Replayer(const nvr::Image& image, Execution& execution, const std::function<void(const TraceEntry&)>& answered)
        : image_(image), execution_(execution), answered_(answered) {}

    std::string difference;

    // The response to `request` from `recorded`, the next entry at
    // `position` in `where`, or nothing after a difference.
    std::optional<HostCompletion> answer(const HostRequest& request, const std::vector<TraceEntry>& recorded,
                                         std::size_t& position, std::vector<TraceEntry>& replayed,
                                         const std::string& where) {
        const auto& import = image_.imports.at(request.import);
        const auto label = where + "request " + std::to_string(position);
        if (position >= recorded.size()) {
            difference = label + " (" + import.name + ") is beyond the recorded trace";
            return std::nullopt;
        }
        const auto& entry = recorded[position];
        if (entry.import != import.name || entry.version != import.interface_version) {
            difference = label + " differs: the run requested " + import.name + " version " +
                         std::to_string(import.interface_version) + " where the trace recorded " + entry.import +
                         " version " + std::to_string(entry.version);
            return std::nullopt;
        }
        if (!(entry.arguments == request.arguments)) {
            std::size_t argument = 0;
            while (argument < entry.arguments.size() && argument < request.arguments.size() &&
                   entry.arguments[argument] == request.arguments[argument]) {
                ++argument;
            }
            const auto shown = [](const std::vector<HostValue>& values, std::size_t index) {
                if (index >= values.size()) return std::string("nothing");
                auto text = format_trace_value(values[index]);
                if (text.size() > 120) text = text.substr(0, 117) + "...";
                return text;
            };
            difference = label + " (" + import.name + " version " + std::to_string(import.interface_version) +
                         ") differs in argument " + std::to_string(argument) + ": the run passed " +
                         shown(request.arguments, argument) + " where the trace recorded " +
                         shown(entry.arguments, argument);
            return std::nullopt;
        }
        ++position;
        replayed.push_back(entry);
        for (std::size_t c = 0; c < entry.callbacks.size(); ++c) {
            const auto& callback = entry.callbacks[c];
            const auto nested_where = label + " callback " + std::to_string(c) + " ";
            std::size_t nested_position = 0;
            std::vector<TraceEntry> nested_replayed;
            bool diverged = false;
            const auto result = execution_.invoke_callback(
                callback.name, callback.version, callback.arguments, [&](const HostRequest& nested) {
                    if (diverged) return HostCompletion::failed();
                    auto completion = answer(nested, callback.entries, nested_position, nested_replayed, nested_where);
                    if (!completion) {
                        diverged = true;
                        return HostCompletion::terminate(1);
                    }
                    return *completion;
                });
            if (diverged) return std::nullopt;
            if (nested_position != callback.entries.size()) {
                difference = nested_where + "made " + std::to_string(nested_position) + " of " +
                             std::to_string(callback.entries.size()) + " recorded requests";
                return std::nullopt;
            }
            if (result != callback.result) {
                difference = nested_where + "returned " + (result ? format_trace_value(*result) : "nothing") +
                             " where the trace recorded " +
                             (callback.result ? format_trace_value(*callback.result) : "nothing");
                return std::nullopt;
            }
        }
        if (answered_) answered_(entry);
        return entry.completion;
    }

private:
    const nvr::Image& image_;
    Execution& execution_;
    const std::function<void(const TraceEntry&)>& answered_;
};

} // namespace

Trace record(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
             RunLimits limits) {
    if (image.transferable_layouts) throw std::invalid_argument("recording tasks requires a concurrent trace version");
    Execution execution(image, arguments, limits);
    Recorder recorder(execution, bindings);
    recorder.set_image(image);
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
        auto entry = recorder.serve(image, event.request);
        execution.complete(entry.completion);
        trace.entries.push_back(std::move(entry));
    }
}

Replay replay(const nvr::Image& image, const Trace& expected, std::span<const std::string> arguments,
              RunLimits limits, const std::function<void(const TraceEntry&)>& answered) {
    if (image.transferable_layouts) throw std::invalid_argument("replaying tasks requires a concurrent trace version");
    Execution execution(image, arguments, limits);
    Replayer replayer(image, execution, answered);
    Replay result;
    std::size_t position = 0;
    for (;;) {
        const auto event = execution.advance();
        if (event.kind != Event::Kind::HostRequest) {
            result.trace.outcome = outcome_of(event);
            result.trace.trap = event.trap;
            result.trace.steps = execution.steps();
            result.trace.peak_heap_cells = execution.peak_heap_cells();
            break;
        }
        const auto completion = replayer.answer(event.request, expected.entries, position, result.trace.entries, "");
        if (!completion) {
            result.difference = replayer.difference;
            return result;
        }
        execution.complete(*completion);
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

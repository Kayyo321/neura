#include "trace.hpp"

#include "driver.hpp"
#include "trace_file.hpp"

#include <algorithm>
#include <map>
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
                                         const std::string& where, bool announce = true) {
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
        if (announce && answered_) answered_(entry);
        return entry.completion;
    }

private:
    const nvr::Image& image_;
    Execution& execution_;
    const std::function<void(const TraceEntry&)>& answered_;
};

} // namespace

namespace {

TraceSettings settings_of(const RunLimits& limits) {
    return {limits.task_quantum, limits.max_tasks, limits.max_queued_messages, limits.max_pending_requests};
}

// Records a run of an image with tasks: the same driver as an unrecorded run,
// logging what it did at each scheduling point.
Trace record_tasks(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
                   RunLimits limits, TaskClock* clock) {
    SteadyTaskClock steady;
    if (clock == nullptr) clock = &steady;
    const auto modes = host_modes_for(image, bindings);
    if (!limits.host_modes) limits.host_modes = &modes;
    Execution execution(image, arguments, limits);
    Recorder recorder(execution, bindings);
    recorder.set_image(image);
    Trace trace;
    trace.settings = settings_of(limits);
    // The entry of each request that is still waiting for its completion.
    std::map<std::uint64_t, std::size_t> overlapping;
    DriveHooks hooks;
    hooks.serve = [&](const Event& event) {
        auto entry = recorder.serve(image, event.request);
        entry.task = event.task;
        entry.tick = event.tick;
        auto completion = entry.completion;
        trace.entries.push_back(std::move(entry));
        return completion;
    };
    hooks.issued = [&](const Event& event, bool cancellable) {
        const auto& import = image.imports.at(event.request.import);
        TraceEntry entry;
        entry.import = import.name;
        entry.version = import.interface_version;
        entry.arguments = event.request.arguments;
        entry.task = event.task;
        entry.tick = event.tick;
        entry.mode = cancellable ? 2 : 1;
        // Until a delivery says otherwise, the run ends without an answer.
        entry.abandoned = true;
        overlapping[event.request.sequence] = trace.entries.size();
        trace.entries.push_back(std::move(entry));
    };
    hooks.delivered = [&](std::uint64_t tick, std::uint64_t sequence, bool cancelled, const HostCompletion& completion) {
        const auto index = overlapping.at(sequence);
        overlapping.erase(sequence);
        trace.entries[index].completion = completion;
        trace.entries[index].abandoned = false;
        trace.events.push_back({TraceEvent::Kind::Delivery, tick, index, cancelled, 0});
    };
    hooks.clock_read = [&](std::uint64_t tick, std::int64_t now) {
        trace.events.push_back({TraceEvent::Kind::Clock, tick, 0, false, now});
    };
    const auto last = drive(execution, bindings, *clock, hooks);
    trace.outcome = outcome_of(last);
    trace.trap = last.trap;
    trace.steps = execution.steps();
    trace.peak_heap_cells = execution.peak_heap_cells();
    return trace;
}

} // namespace

Trace record(const nvr::Image& image, const Bindings& bindings, std::span<const std::string> arguments,
             RunLimits limits, TaskClock* clock) {
    if (image.transferable_layouts) return record_tasks(image, bindings, arguments, limits, clock);
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

namespace {

// Replays a trace of an image with tasks (or an older trace of one whose
// requests all completed at once). Nothing but the trace answers.
Replay replay_tasks(const nvr::Image& image, const Trace& expected, std::span<const std::string> arguments,
                    RunLimits limits, const std::function<void(const TraceEntry&)>& answered) {
    Replay result;
    const bool recorded_with_tasks = expected.settings.has_value();
    if (recorded_with_tasks) {
        // The schedule depends on the quantum and the limits, so the trace's
        // replace the caller's.
        limits.task_quantum = expected.settings->quantum;
        limits.max_tasks = expected.settings->max_tasks;
        limits.max_queued_messages = expected.settings->max_queued_messages;
        limits.max_pending_requests = expected.settings->max_pending_requests;
        result.trace.settings = expected.settings;
    }
    // How each import was served in the recorded run.
    std::vector<std::uint8_t> modes(image.imports.size(), 0);
    for (std::size_t i = 0; i < image.imports.size(); ++i) {
        for (const auto& entry : expected.entries) {
            if (entry.import == image.imports[i].name && entry.version == image.imports[i].interface_version) {
                modes[i] = entry.mode;
                break;
            }
        }
    }
    limits.host_modes = &modes;
    Execution execution(image, arguments, limits);
    Replayer replayer(image, execution, answered);
    std::size_t position = 0;
    std::size_t cursor = 0;
    // Overlapping requests the run has made and the trace has not delivered:
    // by sequence number, the entry's position and whether the run cancelled it.
    struct Waiting {
        std::size_t index = 0;
        bool cancelled = false;
    };
    std::map<std::uint64_t, Waiting> waiting;
    const auto fail = [&](std::string text) {
        result.difference = std::move(text);
        return result;
    };
    const auto name_of = [&](std::size_t index) {
        return "request " + std::to_string(index) + " (" + expected.entries[index].import + ")";
    };
    for (;;) {
        const auto event = execution.advance();
        if (event.kind == Event::Kind::HostRequest) {
            const auto index = position;
            const auto completion = replayer.answer(event.request, expected.entries, position, result.trace.entries,
                                                    "", !event.overlapping);
            if (!completion) return fail(replayer.difference);
            const auto& entry = expected.entries[index];
            const auto label = "request " + std::to_string(index) + " (" + entry.import + ")";
            if (recorded_with_tasks && entry.task != event.task) {
                return fail(label + " was issued by task " + std::to_string(event.task) +
                            " where the trace recorded task " + std::to_string(entry.task));
            }
            if (recorded_with_tasks && entry.tick != event.tick) {
                return fail(label + " was issued at tick " + std::to_string(event.tick) +
                            " where the trace recorded tick " + std::to_string(entry.tick));
            }
            if (event.overlapping != (entry.mode != 0)) {
                return fail(label + (event.overlapping ? " overlaps" : " does not overlap") +
                            " where the trace recorded the opposite");
            }
            if (event.overlapping) {
                waiting[event.request.sequence] = {index, false};
            } else {
                execution.complete(*completion);
            }
            continue;
        }
        if (event.kind == Event::Kind::Cancel) {
            if (const auto found = waiting.find(event.sequence); found != waiting.end()) found->second.cancelled = true;
            continue;
        }
        if (event.kind == Event::Kind::Poll || event.kind == Event::Kind::Waiting) {
            const auto tick = event.tick;
            bool applied = false;
            while (cursor < expected.events.size() && expected.events[cursor].tick <= tick) {
                const auto& recorded = expected.events[cursor];
                const auto what = recorded.kind == TraceEvent::Kind::Delivery
                                      ? "the delivery of request " + std::to_string(recorded.request)
                                      : "the clock reading " + std::to_string(recorded.now);
                if (recorded.tick < tick) {
                    return fail(what + " recorded at tick " + std::to_string(recorded.tick) +
                                " was never reached: the run is at tick " + std::to_string(tick));
                }
                if (recorded.kind == TraceEvent::Kind::Delivery) {
                    const auto found = std::ranges::find_if(
                        waiting, [&](const auto& item) { return item.second.index == recorded.request; });
                    if (found == waiting.end()) {
                        return fail(what + " at tick " + std::to_string(tick) +
                                    " names a request that is not pending in the run");
                    }
                    if (found->second.cancelled != recorded.cancelled) {
                        return fail(name_of(recorded.request) +
                                    (found->second.cancelled
                                         ? " was cancelled by the run before its delivery at tick "
                                         : " was cancelled in the trace before its delivery at tick ") +
                                    std::to_string(tick) + (found->second.cancelled ? ", which the trace did not record"
                                                                                    : ", which the run did not do"));
                    }
                    const auto& entry = expected.entries[recorded.request];
                    if (entry.abandoned) {
                        return fail(what + " at tick " + std::to_string(tick) + " names an abandoned request");
                    }
                    const auto sequence = found->first;
                    waiting.erase(found);
                    if (answered) answered(entry);
                    execution.deliver(sequence, entry.completion);
                } else {
                    if (execution.advance_clock(recorded.now) == 0) {
                        return fail(what + " at tick " + std::to_string(tick) +
                                    " expires no timer or deadline in the run");
                    }
                }
                result.trace.events.push_back(recorded);
                ++cursor;
                applied = true;
                // A clock reading can cancel a scope, and the cancel request
                // is processed before the next scheduling point of this
                // tick. What the trace records after the reading belongs to
                // that later point.
                if (recorded.kind == TraceEvent::Kind::Clock) break;
            }
            if (event.kind == Event::Kind::Waiting && !applied) {
                return fail("the run waits at tick " + std::to_string(tick) +
                            " but the trace records no delivery or clock reading at that tick");
            }
            continue;
        }
        result.trace.outcome = outcome_of(event);
        result.trace.trap = event.trap;
        result.trace.steps = execution.steps();
        result.trace.peak_heap_cells = execution.peak_heap_cells();
        break;
    }
    if (result.trace.entries.size() != expected.entries.size()) {
        return fail("the run made " + std::to_string(result.trace.entries.size()) + " of " +
                    std::to_string(expected.entries.size()) + " recorded requests");
    }
    if (cursor != expected.events.size()) {
        const auto& next = expected.events[cursor];
        return fail("the run ended before " +
                    (next.kind == TraceEvent::Kind::Delivery ? "the delivery of request " + std::to_string(next.request)
                                                              : "the clock reading " + std::to_string(next.now)) +
                    " recorded at tick " + std::to_string(next.tick));
    }
    if (!(result.trace.outcome == expected.outcome)) {
        return fail("the run ended with " + describe(result.trace.outcome) + " instead of " +
                    describe(expected.outcome));
    }
    result.matches = true;
    return result;
}

} // namespace

Replay replay(const nvr::Image& image, const Trace& expected, std::span<const std::string> arguments,
              RunLimits limits, const std::function<void(const TraceEntry&)>& answered) {
    if (image.transferable_layouts) return replay_tasks(image, expected, arguments, limits, answered);
    Execution execution(image, arguments, limits);
    Replayer replayer(image, execution, answered);
    Replay result;
    if (expected.settings) {
        result.difference = "the trace was recorded from an image with tasks, and this image has none";
        return result;
    }
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

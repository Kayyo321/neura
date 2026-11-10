// Overlapping host requests and concurrent record and replay
// (docs/lang/Concurrency.md sections 10.4, 11, and 15). Every provider here
// is a fake the test controls: no real clock, socket, or file is used, so the
// order of completions, cancellations, and clock readings is exact.
//
// The scenarios are compiled from tests/backend/concurrency-io/scenarios.nra,
// which uses the library's own net bindings. The fakes serve them as follows:
// `neura.net.connect` (version 2, overlapping, cancellable) hands out socket
// number `port` and is ready at once; `neura.net.read` (version 2) of socket N
// is pending until N is released and then returns 10 * N bytes; closing socket
// N (`neura.net.close`, synchronous) releases it. Socket 0 is released from the
// start.

#include "fixtures.hpp"
#include "nvm/driver.hpp"
#include "nvm/providers.hpp"
#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace neura::testing {
namespace {

using nvm::HostCompletion;
using nvm::HostRequest;
using nvm::HostType;
using nvm::HostValue;
using nvm::PendingOperation;
using nvr::TypeKind;

namespace host_error = nvm::host_error;

HostValue scalar(std::uint64_t bits) { return HostValue{bits, {}, {}}; }
HostValue bytes_of(std::size_t count) { return HostValue{0, std::string(count, 'x'), {}}; }

template <class T>
std::string joined(const std::vector<T>& items) {
    std::string text;
    for (const auto& item : items) text += (text.empty() ? "" : ",") + std::to_string(item);
    return text;
}

HostType byte_list() { return HostType(TypeKind::List, std::vector<HostType>{HostType(TypeKind::Unsigned, 8)}); }

nvm::HostSignature make_signature(std::string name, std::uint32_t version, std::vector<HostType> parameters,
                             HostType success) {
    return {std::move(name), version, std::move(parameters), std::move(success), HostType(TypeKind::Unsigned, 32)};
}

// What the fake providers did, shared by all of them.
struct State {
    std::set<std::int64_t> released{0};
    // Reads that completed or were cancelled, by socket, in order.
    std::vector<std::int64_t> completed;
    std::vector<std::int64_t> cancelled;
    // Every socket the program closed, in order.
    std::vector<std::int64_t> closed;
    // How many times a pending read was asked to cancel.
    std::size_t cancel_attempts = 0;
    std::size_t connect_begins = 0;
    std::size_t read_begins = 0;
    std::size_t exit_begins = 0;
    bool reads_cancellable = true;
};

class FakeConnect final : public nvm::HostService {
public:
    explicit FakeConnect(State& state)
        : state_(state), signature_(make_signature("neura.net.connect", 2,
                                              {HostType(TypeKind::Text), HostType(TypeKind::Signed, 32),
                                               HostType(TypeKind::Signed, 64)},
                                              HostType(TypeKind::Signed, 64))) {}
    const nvm::HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override {
        // Port 13 refuses the connection.
        if (request.arguments.at(1).bits == 13) return HostCompletion::failed(scalar(host_error::connection_refused));
        return HostCompletion::ok(scalar(request.arguments.at(1).bits));
    }
    bool overlapping() const override { return true; }
    bool cancellable() const override { return true; }
    std::variant<HostCompletion, PendingOperation> begin(const HostRequest& request) override {
        ++state_.connect_begins;
        return invoke(request);
    }

private:
    State& state_;
    nvm::HostSignature signature_;
};

class FakeRead final : public nvm::HostService {
public:
    explicit FakeRead(State& state)
        : state_(state), signature_(make_signature("neura.net.read", 2,
                                              {HostType(TypeKind::Signed, 64), HostType(TypeKind::Signed, 32),
                                               HostType(TypeKind::Signed, 64)},
                                              byte_list())) {}
    const nvm::HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override {
        return HostCompletion::ok(bytes_of(static_cast<std::size_t>(request.arguments.at(0).bits) * 10));
    }
    bool overlapping() const override { return true; }
    bool cancellable() const override { return state_.reads_cancellable; }
    std::variant<HostCompletion, PendingOperation> begin(const HostRequest& request) override {
        ++state_.read_begins;
        const auto socket = static_cast<std::int64_t>(request.arguments.at(0).bits);
        if (state_.released.contains(socket)) {
            state_.completed.push_back(socket);
            return invoke(request);
        }
        PendingOperation operation;
        operation.attempt = [this, socket, request](bool cancelled) -> std::optional<HostCompletion> {
            if (state_.released.contains(socket)) {
                state_.completed.push_back(socket);
                return invoke(request);
            }
            if (cancelled) {
                ++state_.cancel_attempts;
                state_.cancelled.push_back(socket);
                return HostCompletion::failed(scalar(host_error::cancelled));
            }
            return std::nullopt;
        };
        return operation;
    }

private:
    State& state_;
    nvm::HostSignature signature_;
};

// Synchronous: closing a socket releases it.
class FakeClose final : public nvm::HostService {
public:
    explicit FakeClose(State& state)
        : state_(state), signature_(make_signature("neura.net.close", 1, {HostType(TypeKind::Signed, 64)},
                                              HostType(TypeKind::Void))) {}
    const nvm::HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override {
        const auto socket = static_cast<std::int64_t>(request.arguments.at(0).bits);
        state_.released.insert(socket);
        state_.closed.push_back(socket);
        // Socket 66 is released but reports that the connection was reset.
        if (socket == 66) return HostCompletion::failed(scalar(host_error::connection_reset));
        return HostCompletion::ok();
    }

private:
    State& state_;
    nvm::HostSignature signature_;
};

// A virtual clock: it moves only when the driver would otherwise sleep.
class FakeClock final : public nvm::TaskClock {
public:
    std::int64_t now() override { return time; }
    void wait_until(std::int64_t deadline) override { time = std::max(time, deadline); }
    bool virtual_time() const override { return true; }
    std::int64_t time = 1'000'000;
};

class FakeMonotonic final : public nvm::HostService {
public:
    explicit FakeMonotonic(FakeClock& clock)
        : clock_(clock), signature_(make_signature("neura.clock.monotonic", 1, {}, HostType(TypeKind::Signed, 64))) {}
    const nvm::HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest&) override {
        return HostCompletion::ok(scalar(static_cast<std::uint64_t>(clock_.time)));
    }

private:
    FakeClock& clock_;
    nvm::HostSignature signature_;
};

// neura.process.exit as an overlapping service: the exit takes effect when
// socket 99 is released.
class FakeExit final : public nvm::HostService {
public:
    explicit FakeExit(State& state)
        : state_(state), signature_(make_signature("neura.process.exit", 1, {HostType(TypeKind::Signed, 32)},
                                              HostType(TypeKind::Void))) {}
    const nvm::HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override {
        return HostCompletion::terminate(static_cast<std::int32_t>(request.arguments.at(0).bits));
    }
    bool overlapping() const override { return true; }
    std::variant<HostCompletion, PendingOperation> begin(const HostRequest& request) override {
        ++state_.exit_begins;
        PendingOperation operation;
        operation.attempt = [this, request](bool) -> std::optional<HostCompletion> {
            if (state_.released.contains(99)) return invoke(request);
            return std::nullopt;
        };
        return operation;
    }

private:
    State& state_;
    nvm::HostSignature signature_;
};

// Everything a scenario's providers need, bound to an image on demand.
struct World {
    State state;
    FakeClock clock;
    CapturingConsole console;
    FakeConnect connect{state};
    FakeRead read{state};
    FakeClose close{state};
    FakeMonotonic monotonic{clock};
    FakeExit exit{state};
    nvm::HostRegistry registry;

    World() {
        registry.add(console);
        registry.add(connect);
        registry.add(read);
        registry.add(close);
        registry.add(monotonic);
        registry.add(exit);
    }
    nvm::Bindings bind(const nvr::Image& image) { return nvm::bind(image, registry); }
};

const nvr::Image& scenarios(const Environment& environment) {
    static const nvr::Image image =
        build_program({environment.source_dir / "tests" / "backend" / "concurrency-io" / "scenarios.nra"});
    return image;
}

// Counts the scheduler's view of each overlapping request and completion.
struct Observer final : nvm::TaskObserver {
    void event(const nvm::TaskEvent& event) override {
        if (event.kind == nvm::TaskEvent::Kind::HostRequest) names[event.other] = event.detail;
        if (event.kind == nvm::TaskEvent::Kind::HostCompletion) {
            delivered.push_back(event.other);
            ++answers[event.other];
        }
        if (event.kind == nvm::TaskEvent::Kind::Cancel) ++scope_cancels;
    }
    // The service of each request, by sequence number (synchronous requests
    // included), the overlapping ones in the order they were answered, and how
    // many answers each got.
    std::map<std::uint64_t, std::string> names;
    std::vector<std::uint64_t> delivered;
    std::map<std::uint64_t, int> answers;
    // Scopes whose cancellation was requested (not requests sent to providers).
    int scope_cancels = 0;
};

struct Run {
    nvm::RunResult result;
    std::string output;
    std::vector<std::uint64_t> schedule;
};

Run run_scenario(World& world, const nvr::Image& image, const std::string& scenario, nvm::RunLimits limits = {},
                 Observer* observer = nullptr) {
    Run run;
    limits.schedule = &run.schedule;
    limits.observer = observer;
    const auto bindings = world.bind(image);
    run.result = nvm::run(image, bindings, std::vector<std::string>{scenario}, limits, &world.clock);
    run.output = world.console.output();
    return run;
}

// Every answer arrived exactly once.
void check_one_answer_each(const Observer& observer) {
    for (const auto& [sequence, count] : observer.answers) {
        check_equal(count, 1, "request " + std::to_string(sequence) + " was answered once");
    }
}

} // namespace

NEURA_TEST(concurrency, overlapping_completions_arrive_in_provider_order_while_other_tasks_run) {
    World world;
    Observer observer;
    const auto run = run_scenario(world, scenarios(environment), "order", {}, &observer);
    check(run.result.completed && run.result.status == 0, "the scenario completes");
    // Task 2 waits on socket 1 and task 3 on socket 2; the releaser frees 2
    // first. The completions come in the order the provider reported them,
    // and the releaser ran while both waited.
    check_equal(run.output, std::string("release 2\nwait 2 -> 20\nrelease 1\nwait 1 -> 10\n"), "output order");
    check_equal(joined(world.state.completed), std::string("2,1"), "reads finish in provider order");
    // The two reads were requested in the order socket 1, socket 2 and
    // delivered the other way round.
    std::vector<std::uint64_t> reads;
    for (const auto& [sequence, name] : observer.names) {
        if (name == "neura.net.read") reads.push_back(sequence);
    }
    check_equal(reads.size(), std::size_t{2}, "two reads");
    const auto position = [&](std::uint64_t sequence) {
        return std::find(observer.delivered.begin(), observer.delivered.end(), sequence) - observer.delivered.begin();
    };
    check(position(reads[1]) < position(reads[0]), "the later request is delivered first");
    check_one_answer_each(observer);
    // Every overlapping request was answered.
    for (const auto& [sequence, name] : observer.names) {
        if (name != "neura.console.write_line" && name != "neura.net.close") {
            check_equal(observer.answers.count(sequence), std::size_t{1},
                        "an answer for request " + std::to_string(sequence));
        }
    }
    check_equal(run.result.peak_pending_requests >= 2, true, "two requests were pending together");
}

NEURA_TEST(concurrency, a_busy_task_does_not_starve_a_ready_completion) {
    {
        World world;
        const auto run = run_scenario(world, scenarios(environment), "starve");
        check_equal(run.output, std::string("wait 0 -> 0\nbusy done 12497500\n"),
                    "a poll at each quantum delivers the completion while the busy task still runs");
    }
    {
        // With a quantum no loop reaches, the busy task never gives way: the
        // completion waits for the next scheduling decision, which is the end
        // of the loop. This is the contrast that shows the poll did the work.
        World world;
        nvm::RunLimits limits;
        limits.task_quantum = 1'000'000;
        const auto run = run_scenario(world, scenarios(environment), "starve", limits);
        check_equal(run.output, std::string("busy done 12497500\nwait 0 -> 0\n"), "no quantum, no poll");
    }
}

NEURA_TEST(concurrency, the_pending_request_limit_traps_before_the_request_reaches_the_provider) {
    World world;
    nvm::RunLimits limits;
    limits.max_pending_requests = 2;
    const auto run = run_scenario(world, scenarios(environment), "limit", limits);
    check(!run.result.completed && run.result.trap.kind == nvm::TrapKind::RequestLimit, "the limit traps");
    check_equal(nvm::exit_status(run.result.trap), 137, "exit status");
    // Each task connects, which is answered at the next scheduling point,
    // and then reads. Tasks 1 and 2 are waiting on reads when task 3 asks for
    // its read, the third pending request, which is never begun.
    check_equal(world.state.connect_begins, std::size_t{3}, "all three connects were started");
    check_equal(world.state.read_begins, std::size_t{2}, "the third read never reached the provider");
    check(run.output.empty(), "nothing was printed before the trap");
    check_equal(run.result.peak_pending_requests, std::uint64_t{2}, "the peak stays at the limit");
}

NEURA_TEST(concurrency, cancellation_racing_completion_in_both_orders) {
    // The read finished before the cancel request was processed: the
    // operation reports success and its effect stays.
    {
        World world;
        Observer observer;
        const auto run = run_scenario(world, scenarios(environment), "completion-first", {}, &observer);
        check_equal(run.output, std::string("wait 7 -> 70\n"), "the completion wins");
        check_equal(joined(world.state.completed), std::string("7"), "the effect stays");
        check(world.state.cancelled.empty(), "the provider was not asked to cancel a finished read");
        check_equal(world.state.cancel_attempts, std::size_t{0}, "no cancelling attempt");
        check_equal(observer.scope_cancels, 1, "the scope was cancelled");
        check_one_answer_each(observer);
    }
    // The cancel request came first: error 15, and a later release changes
    // nothing.
    {
        World world;
        Observer observer;
        const auto run = run_scenario(world, scenarios(environment), "cancel-first", {}, &observer);
        check_equal(run.output, std::string("wait 7 failed: Cancelled: stream read failed (fake:7)\n"),
                    "the cancellation wins");
        check_equal(joined(world.state.cancelled), std::string("7"), "the provider cancelled the read");
        check(world.state.completed.empty(), "the late release completes nothing");
        check(world.state.released.contains(7), "the release happened after the cancellation");
        check_one_answer_each(observer);
    }
    // A service that cannot be cancelled is never asked to, and its request
    // runs to its own end.
    {
        World world;
        world.state.reads_cancellable = false;
        Observer observer;
        const auto run = run_scenario(world, scenarios(environment), "cancel-first", {}, &observer);
        check_equal(run.output, std::string("wait 7 -> 70\n"), "the request finishes");
        check_equal(world.state.cancel_attempts, std::size_t{0}, "never cancelled");
        check_equal(observer.scope_cancels, 1, "the scope was cancelled all the same");
        check_one_answer_each(observer);
    }
}

NEURA_TEST(concurrency, with_connection_reports_both_failures_and_closes_exactly_once) {
    World world;
    const auto run = run_scenario(world, scenarios(environment), "with-connection");
    check(run.result.completed && run.result.status == 0, "the scenario completes");
    check_equal(
        run.output,
        std::string("ok body, close fails: Err ConnectionReset: stream close failed (fake:66)\n"
                    "err body, close fails: Err close failed after the body failed: ConnectionReset: stream close "
                    "failed (fake:66): the body failed\n"
                    "ok body, close works: Ok 6\n"
                    "err body, close works: Err the body failed\n"
                    "open fails: Err ConnectionRefused: connect failed (fake:13)\n"),
        "a body Ok with a failed close returns the close error; a body Err with a failed close returns a "
        "Context whose cause is the body's error; an open error skips the body");
    // Each opened stream was closed once; the failed open closed nothing.
    check_equal(joined(world.state.closed), std::string("66,66,7,7"), "every opened stream is closed exactly once");
}

NEURA_TEST(concurrency, an_overlapping_completion_can_end_the_process) {
    World world;
    const auto run = run_scenario(world, scenarios(environment), "exit");
    check(run.result.completed && run.result.status == 7, "the exit status of the delivered completion");
    check_equal(run.output, std::string("finisher runs\n"), "nothing runs after the exit is delivered");
    check_equal(world.state.exit_begins, std::size_t{1}, "the exit was started once");
}

NEURA_TEST(concurrency, a_run_waiting_on_a_request_is_not_a_deadlock) {
    const auto& image = scenarios(environment);
    World world;
    const auto bindings = world.bind(image);
    const auto modes = nvm::host_modes_for(image, bindings);
    nvm::RunLimits limits;
    limits.host_modes = &modes;
    nvm::Execution execution(image, std::vector<std::string>{"pending"}, limits);
    // Drive by hand: answer everything except the overlapping requests.
    std::vector<std::uint64_t> pending;
    int waits = 0;
    for (;;) {
        const auto event = execution.advance();
        if (event.kind == nvm::Event::Kind::HostRequest) {
            if (event.overlapping) {
                pending.push_back(event.request.sequence);
            } else {
                execution.complete(bindings.services.at(event.request.import)->invoke(event.request));
            }
            continue;
        }
        if (event.kind == nvm::Event::Kind::Waiting) {
            ++waits;
            check(!event.timer, "no timer is armed");
            check(!pending.empty(), "the run waits because a request is pending");
            // The first request is the connect, the second the read.
            const auto sequence = pending.front();
            pending.erase(pending.begin());
            execution.deliver(sequence, waits == 1 ? HostCompletion::ok(scalar(5)) : HostCompletion::ok(bytes_of(50)));
            continue;
        }
        check(event.kind == nvm::Event::Kind::Completed, "the run completes, it does not deadlock");
        break;
    }
    check_equal(waits, 2, "the run waited once for each of the two requests");
    check_equal(world.console.output(), std::string("got 50\n"), "the value arrived through the channel");

    // With nothing pending, the same kind of wait is a deadlock.
    World stuck;
    const auto run = run_scenario(stuck, image, "deadlock");
    check(!run.result.completed && run.result.trap.kind == nvm::TrapKind::Deadlock, "a real deadlock is reported");
    check_equal(nvm::exit_status(run.result.trap), 134, "deadlock exit status");
    check(run.output.empty(), "nothing ran");
}


// The recorded scenario, its trace, and what a replay of it must reproduce.
struct Recorded {
    nvm::Trace trace;
    std::string text;
    std::string output;
    std::vector<std::uint64_t> schedule;
};

Recorded record_scenario(const Environment& environment, const std::string& scenario, nvm::RunLimits limits = {}) {
    const auto& image = scenarios(environment);
    World world;
    Recorded recorded;
    limits.schedule = &recorded.schedule;
    const auto bindings = world.bind(image);
    recorded.trace = nvm::record(image, bindings, std::vector<std::string>{scenario}, limits, &world.clock);
    recorded.text = nvm::write_trace(recorded.trace);
    recorded.output = world.console.output();
    return recorded;
}

// What a replay of `trace` makes of the scenario: with no service bound.
struct Replayed {
    nvm::Replay replay;
    std::string output;
    std::vector<std::uint64_t> schedule;
};

Replayed replay_scenario(const Environment& environment, const nvm::Trace& trace, const std::string& scenario,
                         nvm::RunLimits limits = {}) {
    Replayed replayed;
    limits.schedule = &replayed.schedule;
    nvm::StringSink output;
    nvm::StringSink error;
    replayed.replay = nvm::replay(scenarios(environment), trace, std::vector<std::string>{scenario}, limits,
                                  [&](const nvm::TraceEntry& entry) {
                                      nvm::replay_console_output(entry.import, entry.version, entry.arguments,
                                                                 entry.completion, output, error);
                                  });
    replayed.output = output.contents();
    return replayed;
}

NEURA_TEST(concurrency, a_concurrent_run_replays_without_services_and_reproduces_the_run) {
    const auto recorded = record_scenario(environment, "mixed");
    check(recorded.text.starts_with("neura-trace 3\nsettings quantum 1024 max-tasks 65536 "
                                    "max-queued-messages 1048576 max-pending-requests 4096\n"),
          "the header and settings");
    check(recorded.text.find("\nrequest neura.net.connect 2 [\"fake\", 99, 0] task 3 tick 3 cancellable\nok 99\n") !=
              std::string::npos,
          "requests name their task, tick, and mode");
    check(recorded.text.find("\nrequest neura.net.close 1 [2] task 4 tick 8\nok 0\n") != std::string::npos,
          "synchronous requests name their task and tick, and no mode");
    check(recorded.text.find("\nclock 14 51000000\ndeliver 14 8 cancelled\noutcome completed 0\n") !=
              std::string::npos,
          "the deadline's clock reading, the cancelled delivery it caused, and the outcome");
    check(recorded.output.find("wait 99 failed: Cancelled") != std::string::npos, "the deadline cancelled the read");

    // The text form reads back to the same trace, with the same bytes.
    const auto parsed = nvm::read_trace(recorded.text);
    check_equal(nvm::write_trace(parsed), recorded.text, "the same bytes again");
    check(parsed.entries == recorded.trace.entries && parsed.events == recorded.trace.events &&
              parsed.settings == recorded.trace.settings,
          "the entries, events, and settings survive the round trip");

    // Replay: no registry, no capability, no service.
    const auto replayed = replay_scenario(environment, parsed, "mixed");
    check(replayed.replay.matches, "the replay matches: " + replayed.replay.difference);
    check_equal(replayed.output, recorded.output, "the replay prints what the recorded run printed");
    check(replayed.schedule == recorded.schedule, "the replay makes every scheduling decision again");
    check_equal(nvm::write_trace(replayed.replay.trace), recorded.text, "re-recording the replay gives the same trace");
    check_equal(replayed.replay.trace.steps, recorded.trace.steps, "the same steps");
    check_equal(replayed.replay.trace.outcome == recorded.trace.outcome, true, "the same outcome");

    // The quantum in the trace overrides the caller's: a replay asked for a
    // different quantum still reproduces the run.
    for (const std::uint64_t quantum : {1ULL, 3ULL, 1000000ULL}) {
        nvm::RunLimits limits;
        limits.task_quantum = quantum;
        limits.max_tasks = 2;
        limits.max_pending_requests = 1;
        const auto again = replay_scenario(environment, parsed, "mixed", limits);
        check(again.replay.matches, "a replay under quantum " + std::to_string(quantum) + ": " +
                                        again.replay.difference);
        check(again.schedule == recorded.schedule, "the schedule under quantum " + std::to_string(quantum));
        check_equal(again.output, recorded.output, "the output under quantum " + std::to_string(quantum));
    }

    // Recording under another quantum changes the schedule and the trace, and
    // each trace replays to its own run.
    nvm::RunLimits other;
    other.task_quantum = 2;
    const auto second = record_scenario(environment, "mixed", other);
    check(second.text.find("settings quantum 2 ") != std::string::npos, "the quantum is in the header");
    const auto second_replay = replay_scenario(environment, second.trace, "mixed");
    check(second_replay.replay.matches && second_replay.schedule == second.schedule, "the second trace replays");
}

NEURA_TEST(concurrency, a_perturbed_trace_reports_the_first_difference) {
    const auto recorded = record_scenario(environment, "mixed");
    const auto diverges = [&](const std::function<void(nvm::Trace&)>& change, std::string_view expected,
                              std::string_view what) {
        auto trace = nvm::read_trace(recorded.text);
        change(trace);
        const auto replayed = replay_scenario(environment, trace, "mixed");
        check(!replayed.replay.matches, std::string(what) + " must not replay");
        check(replayed.replay.difference.find(expected) != std::string::npos,
              std::string(what) + ": the difference '" + replayed.replay.difference + "' should mention '" +
                  std::string(expected) + "'");
    };
    using Kind = nvm::TraceEvent::Kind;
    const auto events_of = [](nvm::Trace& trace, Kind kind) {
        std::vector<nvm::TraceEvent*> found;
        for (auto& event : trace.events) {
            if (event.kind == kind) found.push_back(&event);
        }
        return found;
    };

    // The delivery that wakes the root's reader, which the run waits for.
    diverges([&](nvm::Trace& trace) { ++events_of(trace, Kind::Delivery)[7]->tick; },
             "the run waits at tick 12 but the trace records no delivery or clock reading at that tick",
             "a delivery one tick late");
    diverges([&](nvm::Trace& trace) { --events_of(trace, Kind::Delivery)[5]->tick; },
             "the delivery of request 10 recorded at tick 7 was never reached: the run is at tick 8",
             "a delivery one tick early");
    diverges([&](nvm::Trace& trace) { ++events_of(trace, Kind::Delivery)[5]->tick; },
             "request 11 (neura.console.write_line version 1) differs in argument 0",
             "a delivery that wakes a task one decision late");
    diverges(
        [&](nvm::Trace& trace) {
            // Two deliveries at the same scheduling point, in the other order.
            auto deliveries = events_of(trace, Kind::Delivery);
            for (std::size_t i = 0; i + 1 < deliveries.size(); ++i) {
                if (deliveries[i]->tick == deliveries[i + 1]->tick) {
                    std::swap(deliveries[i]->request, deliveries[i + 1]->request);
                    return;
                }
            }
            fail("the scenario has no two deliveries at one tick");
        },
        "request 11 (neura.console.write_line) was issued at tick 9 where the trace recorded tick 10",
        "swapped deliveries");
    diverges(
        [&](nvm::Trace& trace) {
            // Without the clock reading and the delivery it caused, the run
            // waits at a tick where the trace records nothing.
            trace.events.pop_back();
            trace.events.pop_back();
        },
        "the run waits at tick 14 but the trace records no delivery or clock reading", "a missing clock reading");
    diverges([&](nvm::Trace& trace) { events_of(trace, Kind::Clock).front()->tick = 15; },
             "the run waits at tick 14 but the trace records no delivery or clock reading at that tick",
             "a clock reading at a later tick");
    diverges([&](nvm::Trace& trace) { events_of(trace, Kind::Clock).front()->now = 5; },
             "expires no timer or deadline", "a clock reading before the deadline");
    diverges([&](nvm::Trace& trace) { trace.events.erase(trace.events.end() - 2); },
             "was cancelled in the trace before its delivery", "a deadline reading removed");
    diverges([&](nvm::Trace& trace) { events_of(trace, Kind::Delivery).back()->cancelled = false; },
             "cancelled by the run before its delivery at tick 14, which the trace did not record",
             "a cancel that was not recorded");
    diverges([&](nvm::Trace& trace) { events_of(trace, Kind::Delivery).front()->cancelled = true; },
             "cancelled in the trace before its delivery", "a cancel that is missing");
    diverges([&](nvm::Trace& trace) { trace.entries[7].completion.value.bytes.resize(5); },
             "differs in argument 0: the run passed \"first 5\" where the trace recorded \"first 20\"",
             "a changed completion value");
    diverges([&](nvm::Trace& trace) { trace.entries[1].arguments[1].bits = 3; },
             "request 1 (neura.net.connect version 2) differs in argument 1: the run passed 1 where the trace "
             "recorded 3",
             "a changed request argument");
    diverges([&](nvm::Trace& trace) { trace.entries[5].task = 3; },
             "request 5 (neura.net.connect) was issued by task 4 where the trace recorded task 3",
             "a request from another task");
    diverges([&](nvm::Trace& trace) { trace.outcome.status = 5; },
             "the run ended with completion with status 0 instead of completion with status 5",
             "a different outcome");
    diverges([&](nvm::Trace& trace) { trace.entries.pop_back(); }, "beyond the recorded trace",
             "a missing request");
    diverges([&](nvm::Trace& trace) { trace.events.insert(trace.events.begin() + 1, trace.events.front()); },
             "names a request that is not pending", "a delivery of a request twice");
}

NEURA_TEST(concurrency, a_task_image_with_one_task_records_and_replays) {
    const auto recorded = record_scenario(environment, "single");
    check_equal(recorded.output, std::string("single 0 -> 0\nsingle task done\n"), "the run");
    check(recorded.text.starts_with("neura-trace 3\n"), "an image with tasks writes version 3 even for one task");
    check(recorded.text.find("request neura.net.connect 2 [\"fake\", 0, 0] task 0 tick 0 cancellable\n") !=
              std::string::npos,
          "the one task issued the requests");
    for (const auto& entry : recorded.trace.entries) check_equal(entry.task, std::uint64_t{0}, "only task 0 exists");
    const auto replayed = replay_scenario(environment, nvm::read_trace(recorded.text), "single");
    check(replayed.replay.matches, "the replay matches: " + replayed.replay.difference);
    check_equal(replayed.output, recorded.output, "the replay's output");
    check_equal(nvm::write_trace(replayed.replay.trace), recorded.text, "re-recording gives the same bytes");
}

NEURA_TEST(concurrency, the_concurrent_trace_format_rejects_what_it_cannot_replay) {
    const auto rejects = [](std::string_view text, std::size_t line, std::string_view message) {
        try {
            nvm::read_trace(text);
        } catch (const nvm::TraceFormatError& error) {
            check_equal(error.line, line, "error line for " + std::string(message));
            check(std::string(error.what()).find(message) != std::string::npos,
                  "message '" + std::string(error.what()) + "' mentions '" + std::string(message) + "'");
            return;
        }
        fail("accepted malformed trace: " + std::string(text));
    };
    const std::string head = "neura-trace 3\nsettings quantum 1 max-tasks 2 max-queued-messages 3 max-pending-requests 4\n";
    rejects("neura-trace 3\noutcome completed 0\n", 2, "expected the 'settings' line");
    rejects("neura-trace 3\n", 1, "missing the 'settings' line");
    rejects(head + "request a.b 1 []\nok 0\noutcome completed 0\n", 3, "expected 'task'");
    rejects(head + "request a.b 1 [] task 0 tick 0 maybe\n", 3, "expected 'overlapping', 'cancellable'");
    rejects(head + "request a.b 1 [] task 0 tick 0\nok 0\ndeliver 1 0\noutcome completed 0\n", 5, "did not overlap");
    rejects(head + "request a.b 1 [] task 0 tick 0 overlapping\nok 0\ndeliver 1 1\n", 5, "but the trace has 1 requests");
    rejects(head + "request a.b 1 [] task 0 tick 0 overlapping\ndeliver 1 0\n", 4, "inside a request");
    rejects(head + "request a.b 1 [] task 0 tick 0 overlapping\nok 0\ndeliver 1 0 sideways\n", 5, "expected 'cancelled'");
    rejects(head + "request a.b 1 [] task 0 tick 0 overlapping\nok 0\nclock 1 soon\n", 5, "expected an int64 reading");
    rejects(head + "request a.b 1 [] task 0 tick 0 overlapping\nok 0\nclock 1 5\nrequest a.b 1 [] task 0 tick 0\n", 6,
            "after a delivery or clock reading");
    // A serial request cannot be abandoned: it always completes.
    rejects(head + "request a.b 1 [] task 0 tick 0\nabandoned\n", 4, "expected 'callback', 'ok', 'error', or 'exit'");

    // An abandoned request, a negative clock reading, and every deadlock kind
    // survive the round trip.
    const auto text = head + "request a.b 1 [7] task 2 tick 5 cancellable\nabandoned\nclock 3 -5\noutcome trap deadlock 0 \"\"\n";
    const auto trace = nvm::read_trace(text);
    check(trace.entries.at(0).abandoned && trace.entries.at(0).mode == 2 && trace.entries.at(0).task == 2,
          "the abandoned request");
    check(trace.events.at(0).kind == nvm::TraceEvent::Kind::Clock && trace.events.at(0).now == -5, "the clock reading");
    check(trace.outcome.trap == nvm::TrapKind::Deadlock, "a deadlock is its own outcome");
    check_equal(nvm::write_trace(trace), text, "the same bytes");
    for (const auto kind : {nvm::TrapKind::TaskLimit, nvm::TrapKind::InvalidTaskOperation, nvm::TrapKind::MessageLimit,
                            nvm::TrapKind::RequestLimit}) {
        check(nvm::trace_trap_kind_name(kind) != "unknown", "every trap kind of a task image has a name");
    }
}

NEURA_TEST(concurrency, a_process_exit_delivered_by_an_overlapping_completion_replays) {
    const auto recorded = record_scenario(environment, "exit");
    check(recorded.trace.outcome.completed && recorded.trace.outcome.status == 7, "the recorded exit status");
    check(recorded.text.find("request neura.process.exit 1 [7] task 1 tick 1 overlapping\nexit 7\n") !=
              std::string::npos,
          "the exit is an overlapping request with a terminal completion: " + recorded.text);
    check(recorded.text.ends_with("outcome completed 7\n"), "the outcome");
    const auto replayed = replay_scenario(environment, nvm::read_trace(recorded.text), "exit");
    check(replayed.replay.matches, "the exit replays: " + replayed.replay.difference);
    check_equal(replayed.output, std::string("finisher runs\n"), "the output");
    check_equal(replayed.replay.trace.outcome.status, 7, "the replayed status");
}

NEURA_TEST(concurrency, a_deadlock_and_a_pending_request_limit_trap_replay_as_outcomes) {
    {
        const auto recorded = record_scenario(environment, "deadlock");
        check(recorded.text.find("outcome trap deadlock 0 \"\"\n") != std::string::npos,
              "the deadlock is its own outcome: " + recorded.text);
        const auto replayed = replay_scenario(environment, nvm::read_trace(recorded.text), "deadlock");
        check(replayed.replay.matches, "the deadlock replays: " + replayed.replay.difference);
        check(replayed.replay.trace.trap.kind == nvm::TrapKind::Deadlock, "the replay deadlocks too");
    }
    {
        nvm::RunLimits limits;
        limits.max_pending_requests = 2;
        const auto recorded = record_scenario(environment, "limit", limits);
        check(recorded.text.find("settings quantum 1024 max-tasks 65536 max-queued-messages 1048576 "
                                 "max-pending-requests 2\n") != std::string::npos,
              "the limit is in the header");
        check(recorded.text.find("outcome trap request-limit 0 \"\"\n") != std::string::npos, "the limit trap");
        // The limit comes from the trace, not from the replay's command line.
        const auto replayed = replay_scenario(environment, nvm::read_trace(recorded.text), "limit");
        check(replayed.replay.matches, "the limit trap replays: " + replayed.replay.difference);
    }
}

} // namespace neura::testing

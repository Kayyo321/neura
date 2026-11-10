#include "graph_test_support.hpp"
#include "fleet_tests.hpp"

#include "backend/converter.hpp"
#include "ir-optimization/optimizer.hpp"
#include "nvm/fleet.hpp"
#include "nvm/layered.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace neura::testing {
namespace {

using nvm::Event;

template <typename Exception, typename Action>
void rejects(Action action, const char* message) {
    try {
        action();
        fail(message);
    } catch (const Exception&) {
    }
}

nvr::Block continuation(std::uint32_t target) {
    nvr::Block block;
    nvr::Exit exit;
    exit.kind = nvr::ExitKind::Continue;
    exit.resume = target;
    block.exits.push_back(std::move(exit));
    return block;
}

nvm::Event next_step_event(nvm::Execution& execution, const nvr::Image& image) {
    nvm::Execution::GraphStep step;
    std::vector<std::int64_t> scratch;
    std::vector<std::int64_t> outputs;
    for (;;) {
        if (auto event = execution.begin_step(step)) {
            return std::move(*event);
        }
        outputs.resize(step.output_count);
        if (const auto node = nvm::evaluate_outputs(image.graphs[step.graph], step.inputs, scratch, outputs)) {
            return execution.fail_step(*node);
        }
        if (auto event = execution.finish_step(outputs)) {
            return std::move(*event);
        }
    }
}

void check_step_split(const nvr::Image& image, nvm::RunLimits limits, const std::string& label) {
    const std::vector<std::string> arguments{"héllo", "two"};
    nvm::Execution direct(image, arguments, limits);
    nvm::Execution split(image, arguments, limits);
    nvm::StringSink sink;
    nvm::ConsoleWriteLine console(sink);
    nvm::HostRegistry registry;
    registry.add(console);
    const auto bindings = nvm::bind(image, registry);
    for (;;) {
        const auto expected = direct.advance();
        const auto actual = next_step_event(split, image);
        check(actual.kind == expected.kind, label + " event kind");
        check_equal(actual.status, expected.status, label + " event status");
        check_equal(actual.result, expected.result, label + " event result");
        check_same_trap(actual.trap, expected.trap, label);
        check_same_request(actual.request, expected.request, label);
        check_equal(split.steps(), direct.steps(), label + " step count at event");
        check_equal(split.live_heap_cells(), direct.live_heap_cells(), label + " live heap cells at event");
        check_equal(split.peak_heap_cells(), direct.peak_heap_cells(), label + " peak heap cells at event");
        check(split.function_steps() == direct.function_steps(), label + " profile at event");
        if (actual.kind != nvm::Event::Kind::HostRequest) {
            nvm::Execution::GraphStep step;
            const auto final = split.begin_step(step);
            check(final && final->kind == actual.kind && final->status == actual.status && final->result == actual.result,
                  label + " begin_step returns final event again");
            check_same_trap(final->trap, actual.trap, label);
            break;
        }
        const auto completion = bindings.services[actual.request.import]->invoke(actual.request);
        direct.complete(completion);
        split.complete(completion);
    }
}

} // namespace

NEURA_TEST(fleet, step_split_matches_advance) {
    for (const auto& item : image_corpus(environment)) {
        const auto image = nvr::load(item.bytes);
        check_step_split(image, graph_test_limits(item), item.label);
    }

    // The corpus includes language arithmetic traps. Add a graph arithmetic
    // overflow as well so the manual loop reaches fail_step explicitly.
    auto image = logic_model();
    image.graphs[1].nodes[0] = {nvr::Activation::Relu, std::numeric_limits<std::int64_t>::max(), {{0, 1}, {1, 1}}};
    image = nvr::load(nvr::encode(image));
    check_step_split(image, {}, "checked graph overflow");
}

NEURA_TEST(fleet, step_split_requires_an_active_step) {
    const auto image = nvr::load(nvr::encode(hello_model()));
    nvm::Execution execution(image, {});
    nvm::Execution::GraphStep step;
    rejects<std::logic_error>([&] { execution.finish_step({}); }, "finish_step accepted a call with no begun step");
    rejects<std::logic_error>([&] { execution.fail_step(0); }, "fail_step accepted a call with no begun step");
    check(!execution.begin_step(step), "begin_step starts the first step");
    rejects<std::logic_error>([&] { execution.begin_step(step); }, "begin_step accepted a second call before finish_step");
    const auto event = execution.finish_step({});
    check(event && event->kind == nvm::Event::Kind::HostRequest, "finish_step commits the host request");
    rejects<std::logic_error>([&] { execution.begin_step(step); }, "begin_step accepted a call while a request was pending");
    execution.complete(nvm::HostCompletion::ok());
    check(execution.advance().kind == nvm::Event::Kind::Completed, "advance resumes after a host completion");
}

const std::vector<std::vector<std::string>>& fleet_corpus_arguments() {
    static const std::vector<std::vector<std::string>> arguments{
        {}, {"one"}, {"héllo", "two"}, {""}, {"four", ""}, {"--flag", "five"}, {"two words"}, {"seven", "8", "9"},
    };
    return arguments;
}

nvm::RunLimits fleet_corpus_limits(const CorpusImage& item) {
    auto limits = graph_test_limits(item);
    if (item.label.starts_with("traps/endless-loop.nra") || item.label.starts_with("traps/empty-endless-loop.nra")) {
        limits.max_steps = 1'000; // Use the bounds declared by these CLI fixtures.
    }
    return limits;
}

std::vector<Captured> capture_serial_members(const nvr::Image& image, nvm::RunLimits limits) {
    std::vector<Captured> reference;
    for (const auto& member_arguments : fleet_corpus_arguments()) {
        reference.push_back(run_captured(image, limits, std::nullopt, member_arguments));
    }
    return reference;
}

void check_corpus_fleet(const nvr::Image& image, nvm::RunLimits limits, nvm::GraphDevice* device, std::uint32_t window,
                        const std::vector<Captured>& reference, const std::string& label) {
    const auto& arguments = fleet_corpus_arguments();
    nvm::Fleet fleet(image, {limits, device, window});
    for (std::size_t member = 0; member < arguments.size(); ++member) {
        check_equal(fleet.add(image.entry, arguments[member]), member, "member index");
    }
    CapturingConsole console;
    nvm::HostRegistry registry;
    registry.add(console);
    const auto bindings = nvm::bind(image, registry);
    std::size_t members = 0;
    std::size_t request = 0;
    std::string expected_output;
    nvm::run_fleet(fleet, bindings, [&](std::size_t member, const nvm::RunResult& actual) {
        check_equal(member, members, label + " completion order");
        const auto& expected = reference[member];
        check_same_run(actual, expected.result, label + " member " + std::to_string(member));
        check_equal(console.requests.size(), request + expected.requests.size(), label + " request count");
        for (const auto& expected_request : expected.requests) {
            check_same_request(console.requests[request++], expected_request, label);
        }
        expected_output += expected.output;
        check_equal(console.output(), expected_output, label + " console output");
        ++members;
    });
    check_equal(members, arguments.size(), label + " members reported");
    check(fleet.done(), label + " completed fleet");
}

NEURA_TEST(fleet, corpus_matches_serial_runs) {
    for (const auto& item : image_corpus(environment)) {
        const auto image = nvr::load(item.bytes);
        if (image.entry == nvr::absent) continue;
        const auto limits = fleet_corpus_limits(item);
        const auto reference = capture_serial_members(image, limits);
        for (const auto window : {1U, 3U, 4'096U}) {
            for (const bool use_device : {false, true}) {
                const auto label = item.label + " with window " + std::to_string(window) +
                                   (use_device ? " and ModelDevice" : " and the reference evaluator");
                nvm::ModelDevice model(image);
                nvm::VerifyingDevice verifying(model);
                check_corpus_fleet(image, limits, use_device ? &verifying : nullptr, window, reference, label);
            }
        }
    }
}

NEURA_TEST(fleet, early_host_requests_park_in_member_order) {
    auto image = hello_model();
    image.constants.push_back({3, {'s', 'e', 'c', 'o', 'n', 'd'}});
    image.graphs.push_back({0, {{nvr::Activation::Identity, 0, {}}}, {}});
    auto& first = image.functions[0];
    auto second = first;
    second.blocks[0].graph = 1;
    second.blocks[0].exits[0].arguments[0].index = 2;
    first.blocks.insert(first.blocks.begin(), {continuation(1), continuation(2)});
    first.blocks[2].exits[0].resume = 3;
    first.blocks[2].exits[0].error_resume = 4;
    image.functions.push_back(std::move(second));
    image = nvr::load(nvr::encode(image));

    nvm::ModelDevice device(image);
    nvm::Fleet fleet(image, {{}, &device, 2});
    fleet.add(0, {});
    fleet.add(1, {});
    CapturingConsole console;
    const auto first_request = fleet.advance();
    check(first_request.member == 0 && first_request.event.kind == Event::Kind::HostRequest,
          "the slow member's request is delivered first");
    check_equal(fleet.rounds(), std::uint64_t{3}, "the head reached its host exit on round three");
    check_equal(device.statistics().evaluated, std::uint64_t{1}, "the second member already evaluated its host block");
    rejects<std::logic_error>([&] { fleet.advance(); }, "advance accepted an outstanding host request");
    rejects<std::logic_error>([&] { fleet.add(1, {}); }, "add accepted a member after advance");
    rejects<std::logic_error>([&] { fleet.add_call(1, {}); }, "add_call accepted a member after advance");
    fleet.complete(console.invoke(first_request.event.request));
    const auto first_result = fleet.advance();
    check(first_result.member == 0 && first_result.result.completed, "the head completes before the parked request");
    const auto rounds_before_parked = fleet.rounds();
    const auto second_request = fleet.advance();
    check(second_request.member == 1 && second_request.event.kind == Event::Kind::HostRequest,
          "the parked member issues its request as the new head");
    check_equal(fleet.rounds(), rounds_before_parked, "the new head already had a parked request");
    fleet.complete(console.invoke(second_request.event.request));
    const auto second_result = fleet.advance();
    check(second_result.member == 1 && second_result.result.completed && fleet.done(), "both members complete");
    check_equal(console.output(), std::string("hi\nsecond\n"), "host output matches serial order");
    rejects<std::logic_error>([&] { fleet.advance(); }, "advance accepted a completed fleet");

    nvm::Fleet cpu_fleet(image, {{}, nullptr, 2});
    cpu_fleet.add(0, {});
    cpu_fleet.add(1, {});
    const std::uint64_t expected_rounds[]{3, rounds_before_parked, rounds_before_parked, fleet.rounds()};
    const nvm::FleetEvent* expected_events[]{&first_request, &first_result, &second_request, &second_result};
    CapturingConsole cpu_console;
    for (std::size_t i = 0; i < std::size(expected_rounds); ++i) {
        const auto event = cpu_fleet.advance();
        const auto& expected = *expected_events[i];
        check_equal(event.member, expected.member, "CPU member at event");
        check_equal(cpu_fleet.rounds(), expected_rounds[i], "CPU round count at event");
        check(event.event.kind == expected.event.kind, "CPU event kind");
        if (event.event.kind == nvm::Event::Kind::HostRequest) {
            check_same_request(event.event.request, expected.event.request, "CPU host request");
            cpu_fleet.complete(cpu_console.invoke(event.event.request));
        } else {
            check_same_run(event.result, expected.result, "CPU final result");
        }
    }
    check(cpu_fleet.done(), "CPU fleet reports both results");
    check_equal(cpu_console.output(), console.output(), "CPU console order");

    const auto other_image = image;
    rejects<std::invalid_argument>([&] { nvm::Fleet invalid(other_image, {{}, &device, 2}); },
                                   "a fleet accepted a device belonging to a different image object");
}

NEURA_TEST(fleet, traps_limits_and_terminal_host_completions) {
    auto image = hello_model();
    const auto entry = image.functions[0];
    image.constants.push_back({2, {37, 0, 0, 0}});
    image.imports.push_back({"neura.process.exit", 1, {2}, 0, 5});
    auto immediate_trap = entry;
    immediate_trap.blocks = {entry.blocks[2]};
    auto forever = entry;
    forever.blocks = {continuation(0)};
    auto exits = entry;
    auto& host_exit = exits.blocks[0].exits[0];
    host_exit.target = 1;
    host_exit.arguments = {{nvr::ValueKind::Constant, 2, 2}};
    image.functions = {std::move(immediate_trap), std::move(forever), std::move(exits)};
    image = nvr::load(nvr::encode(image));

    nvm::RunLimits limits;
    limits.max_steps = 3;
    nvm::Fleet fleet(image, {limits});
    for (std::uint32_t entry_index = 0; entry_index < image.functions.size(); ++entry_index) {
        fleet.add(entry_index, {});
    }
    const auto trapped = fleet.advance();
    check(trapped.member == 0 && trapped.event.kind == Event::Kind::Trapped &&
          trapped.result.trap.kind == nvm::TrapKind::Program && trapped.result.trap.reason == 3,
          "the first member reports its trap");
    check_equal(fleet.rounds(), std::uint64_t{1}, "a trapping step occupies a round");
    const auto limited = fleet.advance();
    check(limited.member == 1 && limited.event.kind == Event::Kind::Trapped &&
          limited.result.trap.kind == nvm::TrapKind::StepLimit && limited.result.steps == limits.max_steps,
          "the second member hits its own step limit");
    check_equal(fleet.rounds(), std::uint64_t{4}, "a pre-step limit trap occupies a round");
    const auto request = fleet.advance();
    check(request.member == 2 && request.event.kind == Event::Kind::HostRequest && request.event.request.import == 1 &&
          request.event.request.arguments.size() == 1 && request.event.request.arguments[0].bits == 37,
          "the third member's process exit request is still delivered");
    fleet.complete(nvm::HostCompletion::terminate(37));
    const auto exited = fleet.advance();
    check(exited.member == 2 && exited.event.kind == Event::Kind::Completed && exited.result.status == 37 &&
          exited.result.steps == 1 && fleet.done(), "terminal host completion skips the return block");
    check_equal(fleet.rounds(), std::uint64_t{5}, "a terminal host completion is settled in the next round");
}

NEURA_TEST(fleet, callbacks_run_only_for_the_head) {
    auto image = hello_model();
    image.constants.push_back({3, {'n', 'e', 's', 't', 'e', 'd'}});
    image.constants.push_back({2, {37, 0, 0, 0}});
    auto callback = image.functions[0];
    callback.parameter_count = 0;
    callback.blocks[0].exits[0].arguments[0].index = 2;
    callback.blocks[1].exits[0].arguments[0].index = 3;
    image.functions.push_back(std::move(callback));
    image.callbacks = {{"neura.test.callback", 1, 1}};
    image = nvr::load(nvr::encode(image));

    class CallbackConsole final : public nvm::HostService {
    public:
        explicit CallbackConsole(const std::size_t& completed) : completed_(completed) {}
        const nvm::HostSignature& signature() const override { return console.signature(); }
        nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
            console.invoke(request);
            const auto& message = request.arguments[0].bytes;
            if (message == "hi") {
                check(request.sequence == 0 && request.callbacks, "outer request has a callback context");
                check_equal(outer_requests++, completed_, "outer request issued only for the current member");
                const auto result = request.callbacks->call("neura.test.callback", 1, {});
                check(result && result->bits == 37, "nested request resumes the callback and returns its result");
            } else {
                check(message == "nested" && request.sequence == 1, "nested callback request uses the head's sequence");
            }
            return nvm::HostCompletion::ok();
        }

        CapturingConsole console;

    private:
        const std::size_t& completed_;
        std::size_t outer_requests = 0;
    };

    nvm::Fleet fleet(image, {});
    fleet.add(0, {});
    fleet.add(0, {});
    std::size_t completed = 0;
    CallbackConsole console(completed);
    nvm::HostRegistry registry;
    registry.add(console);
    const auto bindings = nvm::bind(image, registry);
    nvm::run_fleet(fleet, bindings, [&](std::size_t member, const nvm::RunResult& result) {
        check(member == completed++ && result.completed && result.status == 0 && result.host_requests == 2,
              "run_fleet attributes outer and nested requests to the head");
    });
    check_equal(completed, std::size_t{2}, "both callback runs complete");
    check_equal(console.console.output(), std::string("hi\nnested\nhi\nnested\n"), "callback output follows member order");
}

NEURA_TEST(fleet, live_heap_limits_admission_without_stopping_the_head) {
    auto image = hello_model();
    image.constants.resize(1);
    image.imports.clear();
    auto fast = image.functions[0];
    const auto done = fast.blocks[1];
    fast.blocks = {done};
    image.functions[0].blocks = {continuation(1), continuation(2), done};
    image.functions.push_back(std::move(fast));
    image = nvr::load(nvr::encode(image));

    for (const auto heap_cells : {8U, 9U}) {
        nvm::RunLimits limits;
        limits.max_heap_cells = heap_cells;
        nvm::Fleet fleet(image, {limits, nullptr, 2});
        fleet.add(0, {"12345"}); // Six cells for text and two for the argument array.
        fleet.add(1, {});
        const auto first = fleet.advance();
        check(first.member == 0 && first.result.completed && first.result.peak_heap_cells == 8,
              "the head uses eight cells and keeps running");
        check_equal(fleet.rounds(), std::uint64_t{3}, "the head takes three rounds");
        const auto second = fleet.advance();
        check(second.member == 1 && second.result.completed, "the next member eventually starts");
        check_equal(fleet.rounds(), heap_cells == 8 ? std::uint64_t{4} : std::uint64_t{3},
                    "only a full live heap blocks the second member's admission");
    }
    nvm::RunLimits limits;
    limits.max_heap_cells = 0;
    nvm::Fleet fleet(image, {limits, nullptr, 2});
    fleet.add(0, {});
    const auto first = fleet.advance();
    check(first.event.kind == Event::Kind::Trapped && first.result.trap.kind == nvm::TrapKind::HeapLimit && fleet.done(),
          "the head starts and hits its own heap limit even with zero capacity");
}

NEURA_TEST(fleet, exhaustive_two_byte_arguments) {
    auto program = lower_files({backend_dir(environment) / "fixtures" / "arithmetic.nra"});
    optimize(program);
    const auto symbol = find_symbol(program, "Arithmetic.add_i8");
    backend::ConversionOptions options;
    options.extra_functions = {symbol};
    const auto conversion = backend::convert(program, options);
    const auto origin = std::ranges::find_if(conversion.functions, [symbol](const backend::FunctionOrigin& entry) {
        return entry.role == backend::FunctionOrigin::Role::Function && entry.symbol == symbol;
    });
    check(origin != conversion.functions.end(), "Arithmetic.add_i8 was converted");
    const auto index = static_cast<std::uint32_t>(origin - conversion.functions.begin());
    const auto image = nvr::load(nvr::encode(conversion.image));
    nvm::ModelDevice model(image);
    nvm::VerifyingDevice verifying(model);
    nvm::Fleet fleet(image, {{}, &verifying});
    constexpr std::size_t pairs = 65'536;
    for (std::size_t bits = 0; bits < pairs; ++bits) {
        fleet.add_call(index, {bits & 255, bits >> 8});
    }
    for (std::size_t bits = 0; bits < pairs; ++bits) {
        const std::array<std::uint64_t, 2> arguments{bits & 255, bits >> 8};
        nvm::Execution execution(image, index, arguments);
        const auto expected = execution.advance();
        const auto actual = fleet.advance();
        if (actual.member != bits || actual.event.kind != expected.kind || actual.event.result != expected.result ||
            actual.event.status != expected.status || actual.result.steps != execution.steps()) {
            fail("Arithmetic.add_i8 disagrees for the operand bits " + std::to_string(bits));
        }
        if (expected.kind == Event::Kind::Trapped) {
            check_same_trap(actual.result.trap, expected.trap, "overflow in Arithmetic.add_i8");
        }
    }
    check(fleet.done(), "the sweep reported all 65,536 results");
}

} // namespace neura::testing

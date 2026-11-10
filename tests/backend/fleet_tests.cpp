#include "graph_test_support.hpp"

#include <limits>

namespace neura::testing {
namespace {

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
    const auto rejects = [](auto run, const char* message) {
        try {
            run();
            fail(message);
        } catch (const std::logic_error&) {
        }
    };
    rejects([&] { execution.finish_step({}); }, "finish_step accepted a call with no begun step");
    rejects([&] { execution.fail_step(0); }, "fail_step accepted a call with no begun step");
    check(!execution.begin_step(step), "begin_step starts the first step");
    rejects([&] { execution.begin_step(step); }, "begin_step accepted a second call before finish_step");
    const auto event = execution.finish_step({});
    check(event && event->kind == nvm::Event::Kind::HostRequest, "finish_step commits the host request");
    rejects([&] { execution.begin_step(step); }, "begin_step accepted a call while a host request was pending");
    execution.complete(nvm::HostCompletion::ok());
    check(execution.advance().kind == nvm::Event::Kind::Completed, "advance resumes after a host completion");
}

} // namespace neura::testing

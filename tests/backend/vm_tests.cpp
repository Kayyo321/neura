// Runtime behavior of NVM: graph exit selection, runtime faults, host
// imports and their continuations, resource limits, and determinism.

#include "fixtures.hpp"

#include <limits>

namespace neura::testing {
namespace {

using nvm::TrapKind;
using nvr::ValueKind;

void expect_trap(const nvr::Image& image, TrapKind kind, std::string_view detail = {}, nvm::RunLimits limits = {},
                 std::source_location where = std::source_location::current()) {
    const auto run = run_captured(nvr::load(nvr::encode(image)), limits);
    if (run.result.completed || run.result.trap.kind != kind ||
        run.result.trap.detail.find(detail) == std::string::npos) {
        fail("expected " + std::string(nvm::describe(kind)) + ", got " + describe_trap(run.result, image), where);
    }
}

// The logic fixture with block 1's selector graph replaced by `graph`.
nvr::Image with_selector_graph(nvr::Graph graph, std::size_t exits) {
    auto image = logic_model();
    image.graphs.push_back(std::move(graph));
    auto& block = image.functions[0].blocks[1];
    block.graph = 3;
    block.inputs.clear();
    while (block.exits.size() < exits) {
        block.exits.push_back(block.exits.back());
    }
    return image;
}

nvr::Graph constant_lanes(std::vector<std::int64_t> values) {
    nvr::Graph graph;
    for (std::size_t i = 0; i < values.size(); ++i) {
        graph.nodes.push_back({nvr::Activation::Identity, values[i], {}});
        graph.outputs.push_back(static_cast<std::uint32_t>(i));
    }
    return graph;
}

class Recorder final : public nvm::HostService {
public:
    Recorder() {
        signature_.name = "neura.console.write_line";
        signature_.version = 1;
        signature_.parameters = {{nvr::TypeKind::Text}};
        signature_.success = {nvr::TypeKind::Void};
        signature_.error = {nvr::TypeKind::Unsigned, 32};
    }
    const nvm::HostSignature& signature() const override { return signature_; }
    nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
        requests.push_back(request);
        return nvm::HostCompletion::ok();
    }

    nvm::HostSignature signature_;
    std::vector<nvm::HostRequest> requests;
};

} // namespace

NEURA_TEST(vm, selected_exit_follows_the_graph) {
    // Exit 0 traps and exit 1 continues; a selector of 1 lets the run finish.
    auto image = with_selector_graph(constant_lanes({1}), 2);
    auto run = run_captured(nvr::load(nvr::encode(image)));
    check(run.result.completed && run.result.status == 7, describe_trap(run.result, image));
    image = with_selector_graph(constant_lanes({0}), 2);
    expect_trap(image, TrapKind::Program);
    run = run_captured(nvr::load(nvr::encode(image)));
    check_equal(run.result.trap.reason, 3U, "trap reason of exit 0");
    check_equal(run.result.trap.function, 0U, "trap function");
    check_equal(run.result.trap.block, 1U, "trap block");
}

NEURA_TEST(vm, invalid_selectors_and_lanes_trap) {
    // Two selector lanes encode 3 for a block with three exits.
    expect_trap(with_selector_graph(constant_lanes({1, 1}), 3), TrapKind::InvalidSelector, "selector 3 for 3 exits");
    expect_trap(with_selector_graph(constant_lanes({2}), 2), TrapKind::NonBinaryLane, "holds 2");
    expect_trap(with_selector_graph(constant_lanes({-1}), 2), TrapKind::NonBinaryLane, "holds -1");
    // A returned Boolean decoded from a lane holding 2.
    auto image = logic_model();
    image.graphs[1].nodes[0] = {nvr::Activation::Identity, 1, {{0, 1}}};
    expect_trap(image, TrapKind::NonBinaryLane, "output lane 0 holds 2");
}

NEURA_TEST(vm, graph_overflow_traps_before_the_exit) {
    auto image = logic_model();
    image.graphs[1].nodes[0] = {nvr::Activation::Relu, std::numeric_limits<std::int64_t>::max(), {{0, 1}, {1, 1}}};
    expect_trap(image, TrapKind::GraphOverflow, "node 0");
}

NEURA_TEST(vm, uninitialized_state_traps) {
    auto image = logic_model();
    // Return slot 3 before anything writes it.
    image.functions[0].blocks[2].exits[0] = image.functions[0].blocks[6].exits[0];
    expect_trap(image, TrapKind::UninitializedSlot, "slot 3");
    image = logic_model();
    // Read the field before writing it.
    image.functions[0].blocks[3].exits[0].resume = 5;
    expect_trap(image, TrapKind::UninitializedField, "field 0");
    image = logic_model();
    image.globals[0].initializer = nvr::absent;
    expect_trap(image, TrapKind::UninitializedGlobal, "global 0");
}

NEURA_TEST(vm, freed_objects_cannot_be_used) {
    auto image = logic_model();
    // Free the object between writing and reading its field.
    nvr::Exit free_object;
    free_object.kind = nvr::ExitKind::State;
    free_object.operation = nvr::StateOp::FreeObject;
    free_object.arguments = {{ValueKind::Slot, 5, 2}};
    free_object.result_slot = nvr::absent;
    free_object.resume = 5;
    nvr::Block block;
    block.exits = {free_object};
    image.functions[0].blocks.push_back(block);
    image.functions[0].blocks[4].exits[0].resume = 7;
    expect_trap(image, TrapKind::InvalidHandle, "handle");
}

NEURA_TEST(vm, imports_bind_by_name_version_and_signature) {
    auto image = hello_model();
    nvm::HostRegistry empty;
    try {
        nvm::bind(image, empty);
        fail("binding without a service succeeded");
    } catch (const nvm::BindError& error) {
        check(std::string(error.what()).find("is not available") != std::string::npos, error.what());
    }
    Recorder recorder;
    nvm::HostRegistry registry;
    registry.add(recorder);
    nvm::bind(image, registry);
    image.imports[0].interface_version = 2;
    try {
        nvm::bind(image, registry);
        fail("binding a different interface version succeeded");
    } catch (const nvm::BindError&) {
    }
    image = hello_model();
    image.imports[0].error_type = 2;  // int32 instead of uint32
    try {
        nvm::bind(image, registry);
        fail("binding a mismatched signature succeeded");
    } catch (const nvm::BindError& error) {
        check(std::string(error.what()).find("does not match the signature") != std::string::npos, error.what());
    }
}

NEURA_TEST(vm, failed_console_writes_take_the_error_continuation) {
    // The sink accepts one byte: the partial write stays visible, and the
    // error continuation traps with reason 3.
    const auto run = run_captured(hello_model(), {}, std::size_t{1});
    check(!run.result.completed && run.result.trap.kind == TrapKind::Program && run.result.trap.reason == 3,
          describe_trap(run.result, hello_model()));
    check_equal(run.output, std::string("h"), "partial output");
    check_equal(run.result.trap.block, 2U, "error block");
}

NEURA_TEST(vm, host_requests_suspend_the_run) {
    const auto image = hello_model();
    nvm::Execution execution(image, std::vector<std::string>{});
    auto event = execution.advance();
    check(event.kind == nvm::Event::Kind::HostRequest, "the first event is a host request");
    check_equal(event.request.sequence, std::uint64_t{0}, "first sequence number");
    check_equal(event.request.import, 0U, "import index");
    check_equal(event.request.arguments.at(0).bytes, std::string("hi"), "argument bytes");
    execution.complete(nvm::HostCompletion::failed({7, {}}));
    event = execution.advance();
    check(event.kind == nvm::Event::Kind::Trapped && event.trap.reason == 3, "the error completion selects block 2");

    nvm::Execution invalid(image, std::vector<std::string>{});
    invalid.advance();
    invalid.complete(nvm::HostCompletion::failed({std::uint64_t{1} << 32, {}}));
    event = invalid.advance();
    check(event.kind == nvm::Event::Kind::Trapped && event.trap.kind == TrapKind::InvalidCompletion,
          "a uint32 error code wider than 32 bits is rejected");
}

NEURA_TEST(vm, host_request_order_follows_the_program) {
    const auto image = build_program({program_path(environment, "short-circuit")});
    Recorder recorder;
    nvm::HostRegistry registry;
    registry.add(recorder);
    const auto run = nvm::run(image, nvm::bind(image, registry), std::vector<std::string>{});
    check(run.completed, describe_trap(run, image));
    check_equal(run.host_requests, recorder.requests.size(), "requests issued");
    for (std::size_t i = 0; i < recorder.requests.size(); ++i) {
        check_equal(recorder.requests[i].sequence, std::uint64_t{i}, "sequence numbers count up from zero");
    }
    check_equal(recorder.requests.front().arguments[0].bytes, std::string("evaluated left of &&"), "first request");
}

NEURA_TEST(vm, resource_limits_trap) {
    const auto image = nvr::load(nvr::encode(logic_model()));
    nvm::RunLimits steps;
    steps.max_steps = 3;
    auto run = run_captured(image, steps);
    check(!run.result.completed && run.result.trap.kind == TrapKind::StepLimit, describe_trap(run.result, image));
    nvm::RunLimits frames;
    frames.max_frames = 1;
    run = run_captured(image, frames);
    check(!run.result.completed && run.result.trap.kind == TrapKind::FrameLimit, describe_trap(run.result, image));
    check_equal(run.result.trap.block, 0U, "the call exit that would exceed the frame limit");
    nvm::RunLimits heap;
    heap.max_heap_cells = 2;  // the argument array and the object do not both fit
    run = run_captured(image, heap);
    check(!run.result.completed && run.result.trap.kind == TrapKind::HeapLimit, describe_trap(run.result, image));
    check_equal(run.result.trap.block, 2U, "the allocation that would exceed the heap limit");
    nvm::RunLimits handles;
    handles.max_handles = 1;
    run = run_captured(image, handles);
    check(!run.result.completed && run.result.trap.kind == TrapKind::HandleLimit, describe_trap(run.result, image));
}

NEURA_TEST(vm, arguments_reach_the_entry_frame) {
    // Two arguments plus the array take three handles; the fixture's object
    // needs a fourth.
    const auto image = nvr::load(nvr::encode(logic_model()));
    nvm::RunLimits handles;
    handles.max_handles = 3;
    const auto run = run_captured(image, handles, std::nullopt, {"one", "two"});
    check(!run.result.completed && run.result.trap.kind == TrapKind::HandleLimit, describe_trap(run.result, image));
    try {
        nvm::Execution execution(image, std::vector<std::string>{std::string("\xc3\x28")});
        fail("an invalid UTF-8 argument was accepted");
    } catch (const std::invalid_argument&) {
    }
}

NEURA_TEST(vm, repeated_runs_are_identical) {
    for (const auto* name : {"computation", "objects", "short-circuit", "collections"}) {
        const auto image = build_program({program_path(environment, name)});
        const auto first = run_captured(image);
        const auto second = run_captured(image);
        check(first.result.completed, std::string(name) + ": " + describe_trap(first.result, image));
        check_equal(second.output, first.output, std::string(name) + ": output of a repeated run");
        check_equal(second.result.steps, first.result.steps, std::string(name) + ": steps of a repeated run");
    }
}

} // namespace neura::testing

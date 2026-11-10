// Runtime behavior of NVM: graph exit selection, runtime faults, host
// imports and their continuations, resource limits, and determinism.

#include "fixtures.hpp"
#include "nvm/trace.hpp"

#include <fstream>
#include <limits>
#include <utility>

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

nvr::Block one_exit(nvr::Exit exit) {
    nvr::Block block;
    block.exits.push_back(std::move(exit));
    return block;
}

nvr::Block state_block(nvr::StateOp operation, std::uint32_t resume, std::uint32_t result,
                       std::vector<nvr::Value> arguments = {}, std::uint32_t immediate = 0) {
    nvr::Exit exit;
    exit.kind = nvr::ExitKind::State;
    exit.operation = operation;
    exit.resume = resume;
    exit.result_slot = result;
    exit.arguments = std::move(arguments);
    exit.immediate = immediate;
    return one_exit(std::move(exit));
}

nvr::Image garbage_collection_image() {
    using Kind = nvr::TypeKind;
    using Op = nvr::StateOp;
    nvr::Image image;
    image.types = {{Kind::Void}, {Kind::Signed, 32}, {Kind::Text}, {Kind::Array, 0, 2},
                   {Kind::Object, 0, 0}, {Kind::List, 0, 4}, {Kind::Unsigned, 64}, {Kind::Unsigned, 32}};
    image.constants = {{1, {0, 0, 0, 0}}, {6, {0, 0, 0, 0, 0, 0, 0, 0}}};
    image.layouts = {{nvr::absent, {4}}};
    image.graphs = {nvr::Graph{}};

    nvr::Exit zero;
    zero.kind = nvr::ExitKind::Return;
    zero.arguments = {{ValueKind::Constant, 1, 0}};
    nvr::Exit call;
    call.kind = nvr::ExitKind::Call;
    call.target = 1;
    call.resume = 1;
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 1;
    entry.frame_types = {3};
    entry.blocks = {one_exit(call), one_exit(zero)};

    nvr::Function make_cycle;
    make_cycle.frame_types = {4, 4};
    const nvr::Value first{ValueKind::Slot, 4, 0};
    const nvr::Value second{ValueKind::Slot, 4, 1};
    nvr::Exit done;
    done.kind = nvr::ExitKind::Return;
    make_cycle.blocks = {
        state_block(Op::AllocateObject, 1, 0),
        state_block(Op::AllocateObject, 2, 1),
        state_block(Op::WriteField, 3, nvr::absent, {first, second}),
        state_block(Op::WriteField, 4, nvr::absent, {second, first}),
        one_exit(done),
    };
    image.functions = {std::move(entry), std::move(make_cycle)};
    return image;
}

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

NEURA_TEST(vm, traps_map_to_signal_exit_statuses) {
    const auto status = [](TrapKind kind, std::uint32_t reason = 0) {
        nvm::Trap trap;
        trap.kind = kind;
        trap.reason = reason;
        return nvm::exit_status(trap);
    };
    check(status(TrapKind::Program, 1) == 136, "arithmetic traps use ArithmeticError");
    check(status(TrapKind::Program, 2) == 139, "bounds traps use SegmentationFault");
    check(status(TrapKind::Program, 3) == 134, "message traps use Abort");
    check(status(TrapKind::Program, 4) == 132, "invalid dispatch uses IllegalInstruction");
    check(status(TrapKind::Program, 5) == 134, "modification during iteration uses Abort");
    check(status(TrapKind::Program, 99) == 132, "an undefined reason uses IllegalInstruction");
    check(status(TrapKind::InvalidCast) == 134, "a failed cast uses Abort");
    for (const auto kind : {TrapKind::UninitializedSlot, TrapKind::UninitializedField, TrapKind::UninitializedGlobal,
                            TrapKind::InvalidHandle, TrapKind::NilReference, TrapKind::IndexOutOfBounds,
                            TrapKind::FrameLimit})
        check(status(kind) == 139, std::string(nvm::describe(kind)) + " uses SegmentationFault");
    for (const auto kind : {TrapKind::GraphOverflow, TrapKind::NonBinaryLane, TrapKind::InvalidSelector,
                            TrapKind::ReferenceType, TrapKind::InvalidText, TrapKind::InvalidCompletion})
        check(status(kind) == 132, std::string(nvm::describe(kind)) + " uses IllegalInstruction");
    check(status(TrapKind::HeapLimit) == 137 && status(TrapKind::HandleLimit) == 137, "exhaustion uses OutOfMemory");
    check(status(TrapKind::StepLimit) == 152, "the step limit uses CpuTimeExceeded");
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

NEURA_TEST(vm, nil_is_a_value_and_faults_only_when_dereferenced) {
    using Kind = nvr::TypeKind;
    using Op = nvr::StateOp;
    auto image = garbage_collection_image();
    image.constants.push_back({4, {}});
    image.types.push_back({Kind::Bool});
    const nvr::Value nil{ValueKind::Constant, 4, 2};

    nvr::Function test;
    test.frame_types = {8};
    test.result_type = 8;
    nvr::Exit answer;
    answer.kind = nvr::ExitKind::Return;
    answer.arguments = {{ValueKind::Slot, 8, 0}};
    test.blocks = {state_block(Op::ClassIs, 1, 0, {nil}), one_exit(answer)};
    image.functions.push_back(std::move(test));

    nvr::Function cast;
    cast.frame_types = {4};
    cast.result_type = 4;
    answer.arguments = {{ValueKind::Slot, 4, 0}};
    cast.blocks = {state_block(Op::ClassCast, 1, 0, {nil}), one_exit(answer)};
    image.functions.push_back(std::move(cast));

    nvr::Function dereference;
    dereference.frame_types = {4};
    dereference.result_type = 4;
    dereference.blocks = {state_block(Op::ReadField, 1, 0, {nil}), one_exit(answer)};
    image.functions.push_back(std::move(dereference));
    image = nvr::load(nvr::encode(image));
    const std::vector<std::uint64_t> arguments;
    nvm::Execution testing(image, 2, arguments);
    const auto tested = testing.advance();
    check(tested.kind == nvm::Event::Kind::Completed && tested.result == 0, "nil is not an instance of a class");
    nvm::Execution casting(image, 3, arguments);
    const auto casted = casting.advance();
    check(casted.kind == nvm::Event::Kind::Completed && casted.result == 0, "casting preserves nil");
    nvm::Execution reading(image, 4, arguments);
    const auto invalid = reading.advance();
    check(invalid.kind == nvm::Event::Kind::Trapped && invalid.trap.kind == TrapKind::NilReference,
          "dereferencing nil traps");
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

NEURA_TEST(vm, host_compound_values_are_copied_in_both_directions) {
    using Kind = nvr::TypeKind;
    auto image = garbage_collection_image();
    image.types.push_back({Kind::Unsigned, 8});
    image.types.push_back({Kind::List, 0, 8});
    image.types.push_back({Kind::Object, 0, 1});
    image.layouts.push_back({nvr::absent, {2, 9}});
    image.imports = {{"neura.test.get_tuple", 1, {}, 10, 7},
                     {"neura.test.take_tuple", 1, {10}, 0, 7}};
    auto& function = image.functions.front();
    function.frame_types = {3, 10, 7};
    nvr::Exit get;
    get.kind = nvr::ExitKind::Host;
    get.result_slot = 1;
    get.resume = 1;
    get.error_slot = 2;
    get.error_resume = 2;
    nvr::Exit take;
    take.kind = nvr::ExitKind::Host;
    take.target = 1;
    take.arguments = {{ValueKind::Slot, 10, 1}};
    take.resume = 2;
    take.error_slot = 2;
    take.error_resume = 2;
    const auto done = function.blocks.back();
    function.blocks = {one_exit(get), one_exit(take), done};
    image = nvr::load(nvr::encode(image));

    class Stub final : public nvm::HostService {
    public:
        explicit Stub(nvm::HostSignature signature) : signature_(std::move(signature)) {}
        const nvm::HostSignature& signature() const override { return signature_; }
        nvm::HostCompletion invoke(const nvm::HostRequest&) override { return nvm::HostCompletion::ok(); }
    private:
        nvm::HostSignature signature_;
    };
    const nvm::HostType bytes(Kind::List, std::vector<nvm::HostType>{{Kind::Unsigned, 8}});
    const nvm::HostType tuple(Kind::Object, std::vector<nvm::HostType>{{Kind::Text}, bytes});
    Stub producer({"neura.test.get_tuple", 1, {}, tuple, {Kind::Unsigned, 32}});
    Stub consumer({"neura.test.take_tuple", 1, {tuple}, {Kind::Void}, {Kind::Unsigned, 32}});
    nvm::HostRegistry registry;
    registry.add(producer);
    registry.add(consumer);
    check_equal(nvm::bind(image, registry).services.size(), std::size_t{2}, "structural host bindings");

    const nvm::HostValue expected{0, {}, {{0, std::string(240, 'a')}, {0, std::string(32, '\xff')}}};
    nvm::Execution execution(image, std::vector<std::string>{});
    auto event = execution.advance();
    check(event.kind == nvm::Event::Kind::HostRequest && event.request.import == 0, "producer requested");
    execution.complete(nvm::HostCompletion::ok(expected));
    event = execution.advance();
    check(event.kind == nvm::Event::Kind::HostRequest && event.request.import == 1, "consumer requested");
    check(event.request.arguments == std::vector{expected}, "compound transfer survives allocation pressure");
    execution.complete(nvm::HostCompletion::ok());
    check(execution.advance().kind == nvm::Event::Kind::Completed, "completed");

    nvm::Execution bad(image, std::vector<std::string>{});
    bad.advance();
    bad.complete(nvm::HostCompletion::ok());
    const auto invalid = bad.advance();
    check(invalid.kind == nvm::Event::Kind::Trapped && invalid.trap.kind == TrapKind::InvalidCompletion,
          "a tuple completion must have all fields");
}

NEURA_TEST(vm, terminal_host_completions_record_and_replay) {
    using Kind = nvr::TypeKind;
    auto image = garbage_collection_image();
    image.constants.push_back({1, {37, 0, 0, 0}});
    image.imports = {{"neura.process.exit", 1, {1}, 0, 7}};
    auto& function = image.functions.front();
    function.frame_types = {3, 7};
    nvr::Exit stop;
    stop.kind = nvr::ExitKind::Host;
    stop.arguments = {{ValueKind::Constant, 1, 2}};
    stop.resume = 1;
    stop.error_slot = 1;
    stop.error_resume = 1;
    function.blocks.front() = one_exit(stop);
    image = nvr::load(nvr::encode(image));

    class ExitService final : public nvm::HostService {
    public:
        const nvm::HostSignature& signature() const override { return signature_; }
        nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
            return nvm::HostCompletion::terminate(static_cast<std::int32_t>(request.arguments.at(0).bits));
        }
    private:
        const nvm::HostSignature signature_{"neura.process.exit", 1, {{Kind::Signed, 32}}, {Kind::Void},
                                            {Kind::Unsigned, 32}};
    };
    ExitService service;
    nvm::HostRegistry registry;
    registry.add(service);
    const auto bindings = nvm::bind(image, registry);
    const auto trace = nvm::record(image, bindings, {});
    check(trace.outcome.completed && trace.outcome.status == 37 && trace.entries.size() == 1,
          "terminal completion skips the normal success continuation");
    check(nvm::replay(image, trace, {}).matches, "terminal completion replays without a registry");
    auto changed = trace;
    changed.entries[0].completion = nvm::HostCompletion::terminate(38);
    check(!nvm::replay(image, changed, {}).matches, "replay detects a different terminal result");
    image.imports[0].name = "neura.other.service";
    nvm::Execution unauthorized(image, std::vector<std::string>{});
    unauthorized.advance();
    unauthorized.complete(nvm::HostCompletion::terminate(37));
    const auto invalid = unauthorized.advance();
    check(invalid.kind == nvm::Event::Kind::Trapped && invalid.trap.kind == TrapKind::InvalidCompletion,
          "only the process exit service can return a terminal completion");
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

NEURA_TEST(vm, garbage_collection_reclaims_cycles_after_a_return) {
    auto image = garbage_collection_image();
    auto& blocks = image.functions[0].blocks;
    auto another_call = blocks.front();
    another_call.exits[0].resume = 2;
    blocks.insert(blocks.begin() + 1, std::move(another_call));
    image = nvr::load(nvr::encode(image));

    nvm::RunLimits limits;
    limits.max_heap_cells = 5; // entry arguments and exactly two two-cell objects
    const auto first = run_captured(image, limits);
    const auto second = run_captured(image, limits);
    check(first.result.completed && second.result.completed, describe_trap(first.result, image));
    check_equal(first.result.steps, second.result.steps, "collection does not change the step count");
    limits.max_handles = 4; // the second cycle still requires a fresh fifth handle
    expect_trap(image, TrapKind::HandleLimit, {}, limits);
}

NEURA_TEST(vm, garbage_collection_preserves_references_during_parallel_assignment) {
    using Op = nvr::StateOp;
    auto image = garbage_collection_image();
    auto& entry = image.functions[0];
    entry.frame_types = {3, 4, 4, 5, 4, 7};
    const nvr::Value first{ValueKind::Slot, 4, 1};
    const nvr::Value second{ValueKind::Slot, 4, 2};
    const nvr::Value list{ValueKind::Slot, 5, 3};
    const nvr::Value index{ValueKind::Constant, 6, 1};
    const nvr::Value fetched{ValueKind::Slot, 4, 4};
    const auto call = entry.blocks.front();
    const auto done = entry.blocks.back();
    auto append = state_block(Op::AppendElement, 5, nvr::absent, {list, first});
    // The argument is read before the assignment. At the safepoint the
    // pending append is its sole root.
    append.exits[0].assignments = {{1, second}};
    entry.blocks = {call,
                    state_block(Op::AllocateObject, 2, 1),
                    state_block(Op::AllocateObject, 3, 2),
                    state_block(Op::AllocateCollection, 4, 3, {}, 5),
                    std::move(append),
                    state_block(Op::ReadElement, 6, 4, {list, index}),
                    state_block(Op::ReadTag, 7, 5, {fetched}),
                    done};

    nvm::RunLimits limits;
    limits.max_heap_cells = 10; // appending exceeds the limit until the cycle is collected
    auto run = run_captured(nvr::load(nvr::encode(image)), limits);
    check(run.result.completed, describe_trap(run.result, image));

    // The same pending reference is the sole child of an unborn list.
    auto allocate = state_block(Op::AllocateCollection, 5, 3, {first}, 5);
    allocate.exits[0].assignments = {{1, second}};
    entry.blocks[3].exits[0].resume = 4;
    entry.blocks[4] = std::move(allocate);
    run = run_captured(nvr::load(nvr::encode(image)), limits);
    check(run.result.completed, describe_trap(run.result, image));
}

NEURA_TEST(vm, garbage_collection_traces_objects_from_globals) {
    using Op = nvr::StateOp;
    auto image = garbage_collection_image();
    image.globals = {{4, nvr::absent}};
    auto rooted_cycle = image.functions[1];
    rooted_cycle.blocks.insert(rooted_cycle.blocks.begin() + 4,
                               state_block(Op::WriteGlobal, 5, nvr::absent, {{ValueKind::Slot, 4, 0}}));
    image.functions.push_back(std::move(rooted_cycle));
    auto& entry = image.functions[0];
    entry.frame_types = {3, 4, 4, 7};
    auto keep_cycle = entry.blocks.front();
    keep_cycle.exits[0].target = 2;
    auto drop_cycle = entry.blocks.front();
    drop_cycle.exits[0].resume = 2;
    const auto done = entry.blocks.back();
    entry.blocks = {
        std::move(keep_cycle),
        std::move(drop_cycle),
        state_block(Op::AllocateObject, 3, 1),
        state_block(Op::ReadGlobal, 4, 1),
        state_block(Op::ReadField, 5, 2, {{ValueKind::Slot, 4, 1}}),
        state_block(Op::ReadTag, 6, 3, {{ValueKind::Slot, 4, 2}}),
        done,
    };
    nvm::RunLimits limits;
    limits.max_heap_cells = 9; // both cycles fit, but the next allocation forces collection
    const auto run = run_captured(nvr::load(nvr::encode(image)), limits);
    check(run.result.completed, describe_trap(run.result, image));
}

NEURA_TEST(vm, garbage_collection_preserves_startup_arguments) {
    auto image = garbage_collection_image();
    image.imports = {{"neura.console.write_line", 1, {2}, 0, 7}};
    auto& entry = image.functions[0];
    entry.frame_types = {3, 2, 7};
    nvr::Exit print;
    print.kind = nvr::ExitKind::Host;
    print.arguments = {{ValueKind::Slot, 2, 1}};
    print.resume = 2;
    print.error_slot = 2;
    print.error_resume = 2;
    const auto done = entry.blocks.back();
    entry.blocks = {
        state_block(nvr::StateOp::ReadElement, 1, 1, {{ValueKind::Slot, 3, 0}, {ValueKind::Constant, 6, 1}}),
        one_exit(print),
        done,
    };
    image = nvr::load(nvr::encode(image));
    // Preparing the second argument crosses the collector's initial growth
    // threshold while the first argument belongs to an unpublished array.
    const std::vector<std::string> arguments{std::string(240, 'a'), std::string(30, 'b')};
    const auto run = run_captured(image, {}, std::nullopt, arguments);
    check(run.result.completed, describe_trap(run.result, image));
    check_equal(run.output, arguments.front() + '\n', "first launch argument survives collection");
    nvm::RunLimits limits;
    limits.max_heap_cells = 274; // two strings plus the array require 275
    const auto exhausted = run_captured(image, limits, std::nullopt, arguments);
    check(!exhausted.result.completed && exhausted.result.trap.kind == TrapKind::HeapLimit,
          describe_trap(exhausted.result, image));
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

namespace neura::testing {

NEURA_TEST(vm, list_versions_count_structural_changes) {
    // The hand-assembled 1.1 fixture reads a fresh list's version and traps
    // with reason 5. A compiled program then shows that element writes keep
    // a list's version and that an append during for-each changes it.
    const auto image = nvr::load(version_bytes(1));
    nvm::Execution execution(image, std::vector<std::string>{});
    const auto event = execution.advance();
    check(event.kind == nvm::Event::Kind::Trapped && event.trap.kind == nvm::TrapKind::Program &&
              event.trap.reason == 5,
          "the version fixture traps with reason 5");
    const auto path = fs::temp_directory_path() / "neura-vm-tests" / "versions.nra";
    fs::create_directories(path.parent_path());
    std::ofstream(path) << R"(let console = import:Std.Io.Console;
let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let values: int32[list] = [1, 2];
        let seen: int32 = 0;
        for (let value in values) {
            values[1] = 5;
            seen = seen + value;
        }
        if (seen == 6) {
            console.print_line("element writes keep the version");
        }
        values.append(3);
        values.remove_at(0);
        for (let value in values) {
            values.append(value);
        }
        return program.ExitCode.Ok;
    }
);
)";
    const auto run = run_captured(build_program({path}));
    check_equal(run.output, std::string("element writes keep the version\n"), "output before the trap");
    check(!run.result.completed && run.result.trap.kind == nvm::TrapKind::Program && run.result.trap.reason == 5,
          "appending during for-each traps with reason 5");
}

} // namespace neura::testing

#include "fleet_tests.hpp"
#include "graph_test_support.hpp"

#include "nvm/layered.hpp"
#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"
#include "decompiler/assembly.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace neura::testing {
namespace {

using namespace nvr;

Block step(Exit exit) {
    Block result;
    result.graph = 1;
    result.exits = {std::move(exit)};
    return result;
}

Block scheduler(SchedulerOp op, std::uint32_t target, std::vector<Value> arguments,
                std::uint32_t result, std::uint32_t resume) {
    Exit exit;
    exit.kind = ExitKind::Scheduler;
    exit.scheduler = op;
    exit.target = target;
    exit.arguments = std::move(arguments);
    exit.result_slot = result;
    exit.resume = resume;
    return step(std::move(exit));
}

Block console(Value argument, std::uint32_t error_slot, std::uint32_t resume, std::uint32_t on_error) {
    Exit exit;
    exit.kind = ExitKind::Host;
    exit.arguments = {argument};
    exit.resume = resume;
    exit.error_slot = error_slot;
    exit.error_resume = on_error;
    return step(std::move(exit));
}

Block returning(Value value) {
    Exit exit;
    exit.kind = ExitKind::Return;
    exit.arguments = {value};
    return step(std::move(exit));
}

Block trapping() {
    Exit exit;
    exit.kind = ExitKind::Trap;
    exit.reason = 3;
    return step(std::move(exit));
}

Block poll(std::uint32_t resume) {
    Exit exit;
    exit.poll = true;
    exit.resume = resume;
    return step(std::move(exit));
}

Value slot(std::uint32_t type, std::uint32_t index) { return {ValueKind::Slot, type, index}; }
Value constant(std::uint32_t type, std::uint32_t index) { return {ValueKind::Constant, type, index}; }

Image tasks_model() {
    auto image = hello_model();
    image.transferable_layouts.emplace();
    image.types.push_back({nvr::TypeKind::Unsigned, 64});
    image.graphs.push_back({0, {{Activation::Identity, 0, {}}}, {}});
    image.constants = {
        {2, {0, 0, 0, 0}}, {2, {7, 0, 0, 0}}, {3, {'R', '1'}}, {3, {'R', '2'}},
        {3, {'A', '1'}}, {3, {'A', '2'}}, {3, {'B'}},
    };
    auto& root = image.functions.front();
    root.frame_types = {4, 5, 6, 6, 2, 2, 1};
    root.blocks = {
        scheduler(SchedulerOp::ScopeEnter, 0, {}, absent, 1),
        scheduler(SchedulerOp::Spawn, 1, {}, 2, 2),
        scheduler(SchedulerOp::Spawn, 2, {}, 3, 3),
        scheduler(SchedulerOp::IsFinished, 0, {slot(6, 2)}, 6, 4),
        console(constant(3, 2), 1, 5, 10),
        scheduler(SchedulerOp::Join, 0, {slot(6, 2)}, 4, 6),
        scheduler(SchedulerOp::Join, 0, {slot(6, 2)}, 5, 7),
        scheduler(SchedulerOp::ScopeExit, 0, {}, absent, 8),
        console(constant(3, 3), 1, 9, 10),
        returning(slot(2, 5)),
        trapping(),
    };
    Function a;
    a.result_type = 2;
    a.frame_types = {5};
    a.blocks = {
        console(constant(3, 4), 0, 1, 4), poll(2),
        console(constant(3, 5), 0, 3, 4), returning(constant(2, 1)), trapping(),
    };
    Function b;
    b.result_type = 2;
    b.frame_types = {5};
    b.blocks = {console(constant(3, 6), 0, 1, 2), returning(constant(2, 0)), trapping()};
    image.functions.push_back(std::move(a));
    image.functions.push_back(std::move(b));
    return image;
}

} // namespace

NEURA_TEST(concurrency, fifo_scope_join_and_limits) {
    auto fixture = tasks_model();
    const auto image = nvr::load(nvr::encode(fixture));
    nvm::RunLimits limits;
    limits.profile = true;
    limits.task_quantum = 1;
    const auto first = run_captured(image, limits);
    check_equal(first.output, std::string("R1\nA1\nB\nA2\nR2\n"), "quantum rotates tasks at the poll");
    check_equal(first.result.status, 7, "second join reads the same child result");
    const auto second = run_captured(image, limits);
    check_same_run(second.result, first.result, "repeat from clean state");
    check_equal(second.output, first.output, "repeat console output");

    check_equal(run_captured(image).output, std::string("R1\nA1\nA2\nB\nR2\n"), "default quantum");
    limits.max_tasks = 2;
    const auto limited = run_captured(image, limits);
    check(limited.result.trap.kind == nvm::TrapKind::TaskLimit, "task limit counts the root");
    check_equal(nvm::exit_status(limited.result.trap), 137, "task limit exit status");
    check(limited.output.empty(), "limit precedes the root console request");

    auto& blocks = fixture.functions.front().blocks;
    blocks[4].exits.front().resume = 7;
    blocks[9] = returning(constant(2, 0));
    const auto implicit = run_captured(nvr::load(nvr::encode(fixture)));
    check_equal(implicit.output, std::string("R1\nA1\nA2\nB\nR2\n"), "scope exit waits without joins");
    check_equal(implicit.result.status, 0, "scope wait completes the root");
}

NEURA_TEST(concurrency, join_slot_must_match_the_child_result) {
    const auto expect_mismatch = [](const Image& fixture, std::string_view what) {
        const auto run = run_captured(nvr::load(nvr::encode(fixture)));
        check(run.result.trap.kind == nvm::TrapKind::InvalidTaskOperation &&
              run.result.trap.detail.find("join result does not match the task") != std::string::npos,
              std::string(what) + " is an invalid task operation");
        check_equal(nvm::exit_status(run.result.trap), 132, std::string(what) + " exit status");
        check_equal(run.output, std::string("R1\n"), std::string(what) + " faults before the join parks");
    };
    auto absent_slot = tasks_model();
    absent_slot.functions[0].blocks[5].exits.front().result_slot = absent;
    expect_mismatch(absent_slot, "an absent slot for an int32 child");

    auto void_child = tasks_model();
    void_child.functions[1].result_type = 0;
    Exit bare;
    bare.kind = ExitKind::Return;
    void_child.functions[1].blocks[3] = step(std::move(bare));
    expect_mismatch(void_child, "a slot for a void child");
}

NEURA_TEST(concurrency, task_frames_count_as_heap_cells_only_in_task_images) {
    const auto peak = [](const Image& fixture) {
        return run_captured(nvr::load(nvr::encode(fixture))).result.peak_heap_cells;
    };
    auto tasks = tasks_model();
    const auto base = peak(tasks);
    tasks.functions[1].frame_types.insert(tasks.functions[1].frame_types.end(), {5, 5, 5});
    check_equal(peak(tasks), base + 3, "each slot of a live child frame is one heap cell");

    auto serial = hello_model();
    const auto serial_base = peak(serial);
    serial.functions[0].frame_types.insert(serial.functions[0].frame_types.end(), {5, 5, 5});
    check_equal(peak(serial), serial_base, "frames of an image without TRNS are not heap cells");

    nvm::RunLimits limits;
    limits.max_heap_cells = base - 1;
    const auto bounded = run_captured(nvr::load(nvr::encode(tasks_model())), limits);
    check(bounded.result.trap.kind == nvm::TrapKind::HeapLimit, "task frames, records, and scopes reach the heap limit");
    limits.max_heap_cells = base;
    check_equal(run_captured(nvr::load(nvr::encode(tasks_model())), limits).result.status, 7,
                "the peak itself fits");
}

NEURA_TEST(concurrency, spawn_checks_the_actual_layout_of_transferred_objects) {
    // Layout 0 (marked) has an int32 field and a field of its own type;
    // layout 1 derives from it and is not marked.
    const auto model = [](std::uint32_t allocated, bool child_reads) {
        auto image = tasks_model();
        image.types.push_back({nvr::TypeKind::Object, 0, 0});
        image.layouts = {{absent, {2, 7}}, {0, {2, 7}}};
        image.transferable_layouts = std::vector<std::uint32_t>{0};
        Exit allocate;
        allocate.kind = ExitKind::State;
        allocate.operation = StateOp::AllocateObject;
        allocate.immediate = allocated;
        allocate.result_slot = 7;
        allocate.resume = 2;
        auto& root = image.functions.front();
        root.frame_types.push_back(7);
        root.blocks = {
            scheduler(SchedulerOp::ScopeEnter, 0, {}, absent, 1),
            step(std::move(allocate)),
            scheduler(SchedulerOp::Spawn, 3, {slot(7, 7)}, 2, 3),
            scheduler(SchedulerOp::ScopeExit, 0, {}, absent, 4),
            returning(constant(2, 0)),
        };
        Function child;
        child.parameter_count = 1;
        child.result_type = 0;
        child.frame_types = {7, 2};
        Exit done;
        done.kind = ExitKind::Return;
        if (child_reads) {
            Exit read;
            read.kind = ExitKind::State;
            read.operation = StateOp::ReadField;
            read.arguments = {slot(7, 0)};
            read.result_slot = 1;
            read.resume = 1;
            child.blocks = {step(std::move(read)), step(std::move(done))};
        } else {
            child.blocks = {step(std::move(done))};
        }
        image.functions.push_back(std::move(child));
        return nvr::load(nvr::encode(image));
    };
    check_equal(run_captured(model(0, false)).result.status, 0, "unset fields of a marked object do not block transfer");
    const auto read = run_captured(model(0, true));
    check(read.result.trap.kind == nvm::TrapKind::UninitializedField &&
          read.result.trap.detail.find("task 1 spawned at function 0 block 2 by task 0") != std::string::npos,
          "the child traps when it reads the unset field, not the parent at the spawn");
    const auto unmarked = run_captured(model(1, false));
    check(unmarked.result.trap.kind == nvm::TrapKind::InvalidTaskOperation &&
          unmarked.result.trap.detail.find("unmarked layout") != std::string::npos,
          "an object whose actual layout is unmarked is rejected at the spawn");
}

NEURA_TEST(concurrency, nested_scope_chain_parks_until_the_task_limit) {
    std::ifstream file(backend_dir(environment) / "task-chain.nvra");
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto image = nvr::load(nvr::encode(decompiler::assemble(text)));
    nvm::RunLimits limits;
    limits.max_tasks = 1024;
    const auto first = run_captured(image, limits);
    check(first.result.trap.kind == nvm::TrapKind::TaskLimit, "the chain ends at the task limit");
    check_equal(nvm::exit_status(first.result.trap), 137, "task limit status");
    // Each of the 1,023 spawned tasks holds a 20-cell record, a 1-cell scope, and a one-slot frame
    // (2 cells): 23 cells. The root holds no spawned record; it is charged 4 cells like a task
    // under the first accounting plus one more slot: 5 cells.
    check_equal(first.result.peak_heap_cells, std::uint64_t{23 * 1023 + 5}, "parked tasks are charged as heap cells");
    check_equal(first.result.steps, std::uint64_t{3 * 1024 - 1}, "three scheduler steps per task");
    const auto& detail = first.result.trap.detail;
    check(detail.find("task 1023 spawned at function 1 block 1 by task 1022") != std::string::npos &&
          detail.find("by task 1019 ... 1015 more spawns ... spawned at function 1 block 1 by task 3") !=
              std::string::npos &&
          detail.find("spawned at function 0 block 1 by task 0)") != std::string::npos,
          "the trap names the trapping task and both ends of its spawn chain");
    check(detail.size() < 1024, "a deep spawn chain is abbreviated");
    check_same_run(run_captured(image, limits).result, first.result, "repeat from clean state");
}

NEURA_TEST(concurrency, scope_and_handle_rules_are_checked_at_run_time) {
    const auto expect_invalid = [](const Image& fixture, std::string_view text, std::string_view output) {
        const auto run = run_captured(nvr::load(nvr::encode(fixture)));
        check(run.result.trap.kind == nvm::TrapKind::InvalidTaskOperation &&
              run.result.trap.detail.find(text) != std::string::npos, std::string(text));
        check_equal(run.output, std::string(output), std::string(text) + ": output before the fault");
    };
    Exit skip;
    skip.resume = 1;
    auto unscoped = tasks_model();
    unscoped.functions[0].blocks[0] = step(skip);
    expect_invalid(unscoped, "no enclosing scope in the current function", "");

    auto open = tasks_model();
    skip.resume = 8;
    open.functions[0].blocks[7] = step(skip);
    expect_invalid(open, "return leaves an open scope", "R1\nA1\nA2\nB\nR2\n");

    auto forged = tasks_model();
    forged.constants.push_back({6, {9, 0, 0, 0, 0, 0, 0, 0}});
    forged.functions[0].blocks[5] = scheduler(SchedulerOp::Join, 0, {constant(6, 7)}, 4, 6);
    expect_invalid(forged, "task handle is outside its owning scope", "R1\n");

    auto stale = tasks_model();
    stale.functions[0].blocks[8] = scheduler(SchedulerOp::IsFinished, 0, {slot(6, 2)}, 6, 9);
    expect_invalid(stale, "task handle is outside its owning scope", "R1\nA1\nA2\nB\n");
}

NEURA_TEST(concurrency, zero_quantum_is_rejected_before_the_run_starts) {
    nvm::RunLimits limits;
    limits.task_quantum = 0;
    const auto image = nvr::load(nvr::encode(tasks_model()));
    try {
        nvm::Execution execution(image, {}, limits);
        fail("a concurrent execution accepted a zero quantum");
    } catch (const std::invalid_argument&) {
    }
    const auto serial = nvr::load(nvr::encode(hello_model()));
    check_equal(run_captured(serial, limits).result.status, 0, "a serial image ignores the quantum");
}

NEURA_TEST(concurrency, task_dump_and_run_wide_termination) {
    auto fixture = tasks_model();
    fixture.imports.push_back({"neura.process.exit", 1, {2}, 0, 5});
    auto& exit = fixture.functions[1].blocks[0].exits.front();
    exit.target = 1;
    exit.arguments = {constant(2, 1)};
    const auto image = nvr::load(nvr::encode(fixture));
    nvm::Execution execution(image, {});
    check(execution.advance().kind == nvm::Event::Kind::HostRequest, "root reaches its first request");
    const auto root_dump = execution.dump_tasks();
    check(root_dump.find("task 0  root  parked: host request 0\n") != std::string::npos &&
          root_dump.find("\n  task 2  spawned at function 0 block 2  runnable\n") != std::string::npos,
          "the dump lists runnable children under their parent");
    const auto json = execution.dump_tasks(nvm::TaskDumpFormat::Json);
    check(json.starts_with("{\"tasks\":[{\"id\":0,\"parent\":null,\"spawned_at\":null,") &&
          json.find("{\"id\":2,\"parent\":0,\"spawned_at\":\"function 0 block 2\",\"state\":\"runnable\"") !=
              std::string::npos && json.ends_with("]}\n"),
          "the JSON dump carries the same tasks");
    execution.complete(nvm::HostCompletion::ok());
    check(execution.advance().kind == nvm::Event::Kind::HostRequest, "child reaches its first request");
    check(execution.dump_tasks().find("task 0  root  parked: join of task 1\n") != std::string::npos,
          "the dump names the task a join waits for");
    execution.complete(nvm::HostCompletion::terminate(7));
    const auto end = execution.advance();
    check(end.kind == nvm::Event::Kind::Completed && end.status == 7, "child exit terminates the run");

    fixture.functions[1].blocks[0] = trapping();
    fixture.imports.pop_back();
    const auto trap = run_captured(nvr::load(nvr::encode(fixture)));
    check_equal(trap.output, std::string("R1\n"), "child trap ends the run before any sibling effect");
    check(trap.result.trap.kind == nvm::TrapKind::Program &&
          trap.result.trap.detail.find("task 1 spawned at function 0 block 1 by task 0") != std::string::npos,
          "child trap reports the spawn site");
}

NEURA_TEST(concurrency, collection_preserves_parked_frames_and_finished_results) {
    auto fixture = tasks_model();
    fixture.constants.push_back({6, {0, 0, 0, 0, 0, 0, 0, 0}});
    fixture.constants.push_back({6, {1, 0, 0, 0, 0, 0, 0, 0}});
    const auto slice = [](std::uint32_t result, std::uint32_t resume, std::uint32_t text) {
        Exit exit;
        exit.kind = ExitKind::State;
        exit.operation = StateOp::TextSlice;
        exit.arguments = {constant(3, text), constant(6, 7), constant(6, 8)};
        exit.result_slot = result;
        exit.resume = resume;
        return step(std::move(exit));
    };
    auto& root = fixture.functions[0];
    root.frame_types.insert(root.frame_types.end(), {3, 3});
    root.blocks[3] = slice(7, 5, 2);
    root.blocks[5] = scheduler(SchedulerOp::Join, 0, {slot(6, 3)}, 4, 6);
    root.blocks[6] = scheduler(SchedulerOp::Join, 0, {slot(6, 2)}, 8, 7);
    root.blocks[8] = console(slot(3, 7), 1, 11, 10);
    root.blocks[9] = returning(slot(2, 4));
    root.blocks.push_back(console(slot(3, 8), 1, 9, 10));
    auto& a = fixture.functions[1];
    a.frame_types = {3};
    a.result_type = 3;
    a.blocks = {slice(0, 1, 4), returning(slot(3, 0))};
    auto& b = fixture.functions[2];
    b.frame_types = {3};
    b.blocks.clear();
    for (std::uint32_t i = 0; i < 40; ++i) b.blocks.push_back(slice(0, i + 1, 6));
    b.blocks.push_back(returning(constant(2, 0)));
    const auto image = nvr::load(nvr::encode(fixture));

    nvm::RunLimits limits;
    // 45 cells of program data, plus 19 more for each of the two task records than one cell.
    limits.max_heap_cells = 45 + 2 * 19;
    const auto bounded = run_captured(image, limits);
    check_equal(bounded.result.status, 0, "collection allows more allocations than fit at once");
    check_equal(bounded.output, std::string("R\nA\n"), "parked frame and unjoined child result survive collection");
    check(run_captured(image).result.peak_heap_cells > limits.max_heap_cells, "fixture exceeds the bound without collection");
}

NEURA_TEST(concurrency, scheduler_exits_through_fleet) {
    const auto image = nvr::load(nvr::encode(tasks_model()));
    nvm::RunLimits limits;
    limits.task_quantum = 1;
    limits.profile = true;
    const auto serial = capture_serial_members(image, limits);
    for (const auto window : {1U, 3U, 4096U}) {
        nvm::ModelDevice model(image);
        nvm::VerifyingDevice verifying(model);
        const auto cpu = check_corpus_fleet(image, limits, nullptr, window, serial, "reference scheduler fleet");
        const auto gpu = check_corpus_fleet(image, limits, &verifying, window, serial, "model scheduler fleet");
        check(cpu == gpu, "scheduler fleet uses equal rounds with and without a device");
        check(model.statistics().evaluated > 0, "model evaluated scheduler blocks");
    }
}

NEURA_TEST(concurrency, task_images_use_the_concurrent_trace_version_and_serial_images_reject_it) {
    // An image with tasks records the concurrent trace (version 3) even when
    // it makes no request.
    auto fixture = tasks_model();
    const auto image = nvr::load(nvr::encode(fixture));
    CapturingConsole console;
    nvm::HostRegistry registry;
    registry.add(console);
    const auto recorded = nvm::record(image, nvm::bind(image, registry), {});
    check(recorded.settings.has_value(), "a task image records its settings");
    check(nvm::write_trace(recorded).starts_with("neura-trace 3\nsettings quantum 1024 "), "version 3");
    // A serial image refuses a trace of an image with tasks.
    const auto serial = nvr::load(nvr::encode(hello_model()));
    const auto replayed = nvm::replay(serial, recorded, {});
    check(!replayed.matches && replayed.difference.find("image with tasks") != std::string::npos,
          "a serial image does not replay a concurrent trace: " + replayed.difference);
}

} // namespace neura::testing

// The executor's use of the heap: the element width every allocating path
// declares, and what an allocation in flight keeps reachable when it
// collects (docs/runtime/NVM.md, "Garbage collection").

#include "fixtures.hpp"
#include "nvm/heap.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

namespace neura::testing {
namespace {

using nvr::ValueKind;
using Op = nvr::StateOp;
using Kind = nvr::TypeKind;

nvr::Block one_exit(nvr::Exit exit) {
    nvr::Block block;
    block.exits.push_back(std::move(exit));
    return block;
}

nvr::Block state_block(Op operation, std::uint32_t resume, std::uint32_t result,
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

nvr::Block return_block(std::vector<nvr::Value> arguments = {}) {
    nvr::Exit exit;
    exit.kind = nvr::ExitKind::Return;
    exit.arguments = std::move(arguments);
    return one_exit(std::move(exit));
}

nvm::HostValue scalar(std::uint64_t bits) {
    nvm::HostValue value;
    value.bits = bits;
    return value;
}

struct Finished {
    nvm::Event event;
    std::uint64_t peak = 0;
    std::uint64_t live = 0;
    std::vector<nvm::Execution::HeapEntryWidth> entries;
};

// Runs `image` to its end, answering host request i with completions[i]
// (console output with success), and reads the heap the run leaves.
Finished finish(const nvr::Image& image, nvm::RunLimits limits = {}, std::vector<std::string> arguments = {},
                std::vector<nvm::HostCompletion> completions = {}) {
    nvm::Execution execution(image, arguments, limits);
    std::size_t answered = 0;
    for (;;) {
        auto event = execution.advance();
        if (event.kind == nvm::Event::Kind::HostRequest) {
            execution.complete(answered < completions.size() ? completions[answered] : nvm::HostCompletion::ok());
            ++answered;
            continue;
        }
        if (event.kind == nvm::Event::Kind::Poll || event.kind == nvm::Event::Kind::Cancel) continue;
        return {event, execution.peak_heap_cells(), execution.live_heap_cells(), execution.heap_entry_widths()};
    }
}

std::string describe(const Finished& run, const nvr::Image& image) {
    if (run.event.kind == nvm::Event::Kind::Completed) return "completed";
    return run.event.kind == nvm::Event::Kind::Trapped ? nvm::describe(run.event.trap, image) : "did not finish";
}

// Every live entry declares the width of its element type: an array's or
// list's element type, one byte for text and bytes, eight for an object.
// Returns the widths the collections declared.
std::vector<unsigned> check_declared_widths(const Finished& run, const nvr::Image& image) {
    std::vector<unsigned> collections;
    for (const auto& entry : run.entries) {
        const auto kind = static_cast<nvm::EntryKind>(entry.kind);
        nvm::ElementWidth expected = nvm::ElementWidth::W8;
        if (kind == nvm::EntryKind::Array || kind == nvm::EntryKind::List) {
            expected = nvm::element_width(image.types[image.types[entry.type].reference]);
            collections.push_back(entry.width);
        } else if (kind == nvm::EntryKind::Text || kind == nvm::EntryKind::Bytes) {
            expected = nvm::ElementWidth::W1;
        }
        check_equal(static_cast<unsigned>(entry.width), static_cast<unsigned>(expected),
                    "declared width of handle " + std::to_string(entry.handle) + " (kind " +
                        std::to_string(entry.kind) + ", type " + std::to_string(entry.type) + ")");
    }
    return collections;
}

bool has(const std::vector<unsigned>& widths, unsigned width) {
    return std::find(widths.begin(), widths.end(), width) != widths.end();
}

// Types shared by the collection images below.
//   0 void, 1 int32, 2 text, 3 text[], 4 object (layout 0: one uint64 field),
//   5 uint64[list], 6 uint64, 7 object[list], 8 uint32
nvr::Image collection_image() {
    nvr::Image image;
    image.types = {{Kind::Void},      {Kind::Signed, 32}, {Kind::Text},         {Kind::Array, 0, 2}, {Kind::Object, 0, 0},
                   {Kind::List, 0, 6}, {Kind::Unsigned, 64}, {Kind::List, 0, 4}, {Kind::Unsigned, 32}};
    image.layouts = {{nvr::absent, {6}}};
    image.graphs = {nvr::Graph{}};
    //   c0 int32 0, c1 uint64 2, c2 uint64 3, c3 uint64 0
    image.constants = {{1, {0, 0, 0, 0}},
                       {6, {2, 0, 0, 0, 0, 0, 0, 0}},
                       {6, {3, 0, 0, 0, 0, 0, 0, 0}},
                       {6, {0, 0, 0, 0, 0, 0, 0, 0}}};
    return image;
}

// Calls a function that allocates two objects (handles 2 and 3, after the
// empty argument array) and returns, so both are unreachable; then runs
// `allocation`, whose elements are the scalars 2 and 3 or 2 and 2.
nvr::Image scalar_elements_image(nvr::Block allocation) {
    auto image = collection_image();
    nvr::Exit call;
    call.kind = nvr::ExitKind::Call;
    call.target = 1;
    call.resume = 1;
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 1;
    entry.frame_types = {3, 5};
    entry.blocks = {one_exit(call), std::move(allocation), return_block({{ValueKind::Constant, 1, 0}})};
    nvr::Function make;
    make.frame_types = {4, 4};
    make.blocks = {state_block(Op::AllocateObject, 1, 0), state_block(Op::AllocateObject, 2, 1), return_block()};
    image.functions = {std::move(entry), std::move(make)};
    return nvr::load(nvr::encode(image));
}

} // namespace

NEURA_TEST(heap, scalar_elements_do_not_keep_objects_alive) {
    // The argument array (1 cell) and two unreachable two-cell objects leave
    // 2 of 7 cells; the 3-cell list collects first. Rooting the scalars 2 and
    // 3 as references would keep both objects and trap with the heap limit.
    const nvr::Value two{ValueKind::Constant, 6, 1};
    const nvr::Value three{ValueKind::Constant, 6, 2};
    nvm::RunLimits limits;
    limits.max_heap_cells = 7;
    for (const auto& [label, allocation] :
         {std::pair{"AllocateCollection", state_block(Op::AllocateCollection, 2, 1, {two, three}, 5)},
          std::pair{"RepeatCollection", state_block(Op::RepeatCollection, 2, 1, {two, two}, 5)}}) {
        const auto image = scalar_elements_image(allocation);
        const auto run = finish(image, limits);
        check(run.event.kind == nvm::Event::Kind::Completed, std::string(label) + ": " + describe(run, image));
        check_equal(run.peak, std::uint64_t{5}, std::string(label) + ": peak_heap_cells");
        check_equal(run.live, std::uint64_t{4}, std::string(label) + ": live cells after the run");
        std::vector<std::uint64_t> handles;
        for (const auto& entry : run.entries) handles.push_back(entry.handle);
        check(handles == std::vector<std::uint64_t>{1, 4}, std::string(label) + ": the objects were collected");
    }
}

NEURA_TEST(heap, reference_elements_keep_objects_alive) {
    // Handle 2 is the list's only element and, after the exit's parallel
    // assignment overwrites its slot, reachable only through the list being
    // allocated. Handle 3 is garbage. 7 of 8 cells are live, so the 2-cell
    // list collects first; handle 2 must survive it.
    auto image = collection_image();
    const nvr::Value a{ValueKind::Slot, 4, 1};
    const nvr::Value b{ValueKind::Slot, 4, 2};
    const nvr::Value list{ValueKind::Slot, 7, 3};
    const nvr::Value fetched{ValueKind::Slot, 4, 4};
    auto allocate = state_block(Op::AllocateCollection, 4, 3, {a}, 7);
    allocate.exits[0].assignments = {{1, b}};
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 1;
    entry.frame_types = {3, 4, 4, 7, 4, 8};
    entry.blocks = {state_block(Op::AllocateObject, 1, 1),
                    state_block(Op::AllocateObject, 2, 2),
                    state_block(Op::AllocateObject, 3, 2),
                    std::move(allocate),
                    state_block(Op::ReadElement, 5, 4, {list, {ValueKind::Constant, 6, 3}}),
                    state_block(Op::ReadTag, 6, 5, {fetched}),
                    return_block({{ValueKind::Constant, 1, 0}})};
    image.functions = {std::move(entry)};
    image = nvr::load(nvr::encode(image));
    nvm::RunLimits limits;
    limits.max_heap_cells = 8;
    const auto run = finish(image, limits);
    check(run.event.kind == nvm::Event::Kind::Completed, describe(run, image));
    check_equal(run.peak, std::uint64_t{7}, "peak_heap_cells");
    check_equal(run.live, std::uint64_t{7}, "live cells after the run");
    std::vector<std::uint64_t> handles;
    for (const auto& entry : run.entries) handles.push_back(entry.handle);
    check(handles == std::vector<std::uint64_t>{1, 2, 4, 5}, "the list's element survived, the garbage did not");
}

NEURA_TEST(heap, state_operations_and_host_values_declare_element_widths) {
    // types: 0 void, 1 int32, 2 text, 3 text[], 4 uint8, 5 uint8[list],
    // 6 uint64, 7 int32[], 8 uint32, 9 int16, 10 int16[list], 11 bool,
    // 12 bool[], 13 object (layout 0: int16, text), 14 float32[list], 15 float32
    nvr::Image image;
    image.types = {{Kind::Void},          {Kind::Signed, 32},  {Kind::Text},          {Kind::Array, 0, 2},
                   {Kind::Unsigned, 8},   {Kind::List, 0, 4},  {Kind::Unsigned, 64},  {Kind::Array, 0, 1},
                   {Kind::Unsigned, 32},  {Kind::Signed, 16},  {Kind::List, 0, 9},    {Kind::Bool},
                   {Kind::Array, 0, 11},  {Kind::Object, 0, 0}, {Kind::List, 0, 15},  {Kind::Float, 32}};
    image.layouts = {{nvr::absent, {9, 2}}};
    image.graphs = {nvr::Graph{}};
    //   c0 int32 0, c1 uint64 3, c2 int16 0x7fff, c3 bool 1, c4 bool 0, c5 float32 1.0
    image.constants = {{1, {0, 0, 0, 0}}, {6, {3, 0, 0, 0, 0, 0, 0, 0}}, {9, {0xff, 0x7f}}, {11, {1}}, {11, {0}},
                       {15, {0, 0, 0x80, 0x3f}}};
    image.imports = {{"test.bytes", 1, {}, 5, 8}, {"test.integers", 1, {}, 7, 8}, {"test.tuple", 1, {}, 13, 8}};
    const auto host = [](std::uint32_t import, std::uint32_t slot, std::uint32_t resume) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::Host;
        exit.target = import;
        exit.result_slot = slot;
        exit.resume = resume;
        exit.error_slot = 8;
        exit.error_resume = resume;
        return one_exit(std::move(exit));
    };
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 1;
    entry.frame_types = {3, 5, 7, 13, 10, 12, 14, 10, 8};
    entry.blocks = {host(0, 1, 1),
                    host(1, 2, 2),
                    host(2, 3, 3),
                    state_block(Op::RepeatCollection, 4, 4, {{ValueKind::Constant, 6, 1}, {ValueKind::Constant, 9, 2}}, 10),
                    state_block(Op::AllocateCollection, 5, 5, {{ValueKind::Constant, 11, 3}, {ValueKind::Constant, 11, 4}}, 12),
                    state_block(Op::AllocateCollection, 6, 6, {{ValueKind::Constant, 15, 5}}, 14),
                    state_block(Op::AllocateCollection, 7, 7, {}, 10),
                    return_block({{ValueKind::Constant, 1, 0}})};
    image.functions = {std::move(entry)};
    image = nvr::load(nvr::encode(image));

    nvm::HostValue bytes;
    bytes.bytes = std::string("\x01\x02\xff", 3);
    nvm::HostValue integers;
    integers.elements = {scalar(5), scalar(0xffff'fff0)};
    nvm::HostValue tuple;
    nvm::HostValue word;
    word.bytes = "word";
    tuple.elements = {scalar(7), word};
    const auto run = finish(image, {}, {"launch", "arguments"},
                            {nvm::HostCompletion::ok(bytes), nvm::HostCompletion::ok(integers),
                             nvm::HostCompletion::ok(tuple)});
    check(run.event.kind == nvm::Event::Kind::Completed, describe(run, image));
    const auto widths = check_declared_widths(run, image);
    // text[] (launch arguments), uint8[list] (host), int32[] (host),
    // int16[list] (repeat, empty allocate), bool[], float32[list].
    check_equal(widths.size(), std::size_t{7}, "collections");
    for (const unsigned width : {1U, 2U, 4U, 8U}) check(has(widths, width), "a collection of width " + std::to_string(width));
}

NEURA_TEST(heap, compiled_paths_declare_element_widths) {
    // Kernel results (a digest's uint8[list]), parallel results and the
    // values a parallel item returns (int32[] and string[] of imported
    // text), collection literals, and the launch arguments.
    const auto path = fs::temp_directory_path() / "neura-heap-tests" / "widths.nra";
    fs::create_directories(path.parent_path());
    std::ofstream(path) << R"(let console = import:Std.Io.Console;
let sha256s = import:Std.Digest.Sha256;
let program = import:Std.Program;
let parallel = import:Std.Concurrent.Parallel;

let twice = func(x: int) -> int => x + x;
let echo = func(text: string) -> string => text + "!";

export let Main = module : program.Entry (
    export let.override main = func(arguments: string[]) -> program.ExitCode {
        let bytes: uint8[list] = [1, 2, 3];
        let flags: bool[] = [true, false];
        let reals: float64[list] = [1.5];
        let words: string[] = ["a", "b"];
        let digest = sha256s.of(bytes);
        let doubled = parallel.map<int, int>([1, 2, 3], twice, parallel.limits(100000, 100000));
        let echoed = parallel.map<string, string>(words, echo, parallel.limits(100000, 100000));
        console.print_line(digest.length.to_string() + " " + doubled[2].to_string() + " " + echoed[1] + " " +
                           arguments[0] + " " + flags.length.to_string() + " " + reals.length.to_string());
        return program.ExitCode.Ok;
    };
);
)";
    const auto image = build_program({path});
    const auto run = finish(image, {}, {"argument"});
    check(run.event.kind == nvm::Event::Kind::Completed, describe(run, image));
    const auto widths = check_declared_widths(run, image);
    check(has(widths, 1) && has(widths, 4) && has(widths, 8), "collections of widths 1, 4, and 8 are live");
}

} // namespace neura::testing

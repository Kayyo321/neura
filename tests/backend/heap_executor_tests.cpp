// The executor's use of the heap: the element width every allocating path
// declares, and what an allocation in flight keeps reachable when it
// collects (docs/runtime/NVM.md, "Garbage collection").

#include "fixtures.hpp"
#include "nvm/heap.hpp"

#include "neura_digest.h"
#include "neura_hash.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace neura::nvm {
// Kernel arguments passed in place (src/nvm/machine.cpp); for tests only.
extern std::atomic<std::uint64_t> kernel_views_in_place;
} // namespace neura::nvm

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

namespace {

// A function that allocates a collection of `type_index` from `elements`
// (constants), calls kernel 0 on it, and returns the kernel's result slot
// (`result_type`) or, for a void kernel, the collection.
nvr::Function kernel_function(std::uint32_t collection_type, std::uint32_t result_type,
                              const std::vector<nvr::Value>& elements, std::vector<nvr::Value> arguments) {
    const nvr::Value collection{ValueKind::Slot, collection_type, 0};
    for (auto& argument : arguments) {
        if (argument.kind == ValueKind::Slot) argument = collection;
    }
    nvr::Function function;
    function.result_type = result_type == 0 ? collection_type : result_type;
    function.frame_types = {collection_type, result_type == 0 ? collection_type : result_type};
    auto call = state_block(Op::Kernel, 2, result_type == 0 ? nvr::absent : 1, std::move(arguments), 0);
    function.blocks = {state_block(Op::AllocateCollection, 1, 0, elements, collection_type), std::move(call),
                       return_block({result_type == 0 ? collection : nvr::Value{ValueKind::Slot, result_type, 1}})};
    return function;
}

struct KernelRun {
    nvm::Event event;
    std::uint64_t in_place = 0;
    std::vector<std::uint64_t> collection;
};

// Runs function 0 of `image` from its start and reads the collection in
// slot 0 of the innermost frame (after a trap) or the returned one (when
// `returns_collection`).
KernelRun run_kernel_image(const nvr::Image& image, std::uint32_t collection_type, bool returns_collection) {
    const auto before = nvm::kernel_views_in_place.load();
    nvm::Execution execution(image, std::uint32_t{0}, std::span<const std::uint64_t>{});
    KernelRun run;
    run.event = execution.advance();
    run.in_place = nvm::kernel_views_in_place.load() - before;
    std::uint64_t handle = 0;
    if (run.event.kind == nvm::Event::Kind::Trapped) {
        const auto snapshot = execution.debug_snapshot();
        check(!snapshot.threads.empty() && !snapshot.threads[0].frames.empty(), "the trapped frame is kept");
        handle = snapshot.threads[0].frames[0].slots[0].bits;
    } else if (returns_collection) {
        handle = run.event.result;
    }
    if (handle != 0) {
        for (const auto& child : execution.debug_children(collection_type, handle, 0, 1000)) {
            run.collection.push_back(child.bits);
        }
    }
    return run;
}

// Gives `image` an entry function of the entry signature that returns 0,
// after its other functions; the tests start function 0 directly.
void add_entry(nvr::Image& image) {
    const auto text = static_cast<std::uint32_t>(image.types.size());
    image.types.push_back({Kind::Text});
    image.types.push_back({Kind::Array, 0, text});
    image.types.push_back({Kind::Signed, 32});
    image.constants.push_back({text + 2, {0, 0, 0, 0}});
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = text + 2;
    entry.frame_types = {text + 1};
    entry.blocks = {return_block({{ValueKind::Constant, text + 2, static_cast<std::uint32_t>(image.constants.size() - 1)}})};
    image.entry = static_cast<std::uint32_t>(image.functions.size());
    image.functions.push_back(std::move(entry));
}

std::string describe_event(const nvm::Event& event, const nvr::Image& image) {
    Finished run;
    run.event = event;
    return describe(run, image);
}

std::vector<std::uint8_t> le_bytes(std::uint64_t bits, unsigned bytes) {
    std::vector<std::uint8_t> out;
    for (unsigned i = 0; i < bytes; ++i) out.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    return out;
}

} // namespace

NEURA_TEST(heap, narrow_read_only_kernel_views_run_in_place) {
    // types: 0 void, 1 uint8, 2 uint8[list], 3 uint32, 4 uint8[], 5 int64,
    // 6 int64[list]
    nvr::Image base;
    base.types = {{Kind::Void},          {Kind::Unsigned, 8}, {Kind::List, 0, 1}, {Kind::Unsigned, 32},
                  {Kind::Array, 0, 1},   {Kind::Signed, 64},  {Kind::List, 0, 5}};
    base.graphs = {nvr::Graph{}};
    const std::vector<std::uint8_t> bytes{0xff, 0x80, 0x01, 0x00, 0x7f, 0x10, 0xfe};
    const std::vector<std::int64_t> integers{-5, 3, std::numeric_limits<std::int64_t>::min(), 0, 1};
    std::vector<nvr::Value> byte_elements;
    std::vector<nvr::Value> integer_elements;
    for (const auto b : bytes) {
        byte_elements.push_back({ValueKind::Constant, 1, static_cast<std::uint32_t>(base.constants.size())});
        base.constants.push_back({1, {b}});
    }
    for (const auto i : integers) {
        integer_elements.push_back({ValueKind::Constant, 5, static_cast<std::uint32_t>(base.constants.size())});
        base.constants.push_back({5, le_bytes(static_cast<std::uint64_t>(i), 8)});
    }
    const neura_view_u8 view{bytes.data(), bytes.size()};
    const neura_bytes byte_view{bytes.data(), bytes.size()};
    const neura_view_i64 integer_view{integers.data(), integers.size()};
    struct Case {
        const char* label;
        nvr::KernelEntry kernel;
        std::uint32_t collection;
        bool bytes;
        std::uint32_t expected;
    };
    for (const auto& c : {Case{"hash.list_u8 of a uint8 list", {"neura.hash.list_u8", 1, {2}, 3}, 2, true,
                               neura_k_hash_list_u8(view)},
                          Case{"hash.list_u8 of a uint8 array", {"neura.hash.list_u8", 1, {4}, 3}, 4, true,
                               neura_k_hash_list_u8(view)},
                          Case{"crc32 of a uint8 list", {"neura.digest.crc32", 1, {2}, 3}, 2, true,
                               neura_k_digest_crc32(byte_view)},
                          Case{"hash.list_i64 of an int64 list", {"neura.hash.list_i64", 1, {6}, 3}, 6, false,
                               neura_k_hash_list_i64(integer_view)}}) {
        auto image = base;
        image.kernels = {c.kernel};
        image.functions = {kernel_function(c.collection, 3, c.bytes ? byte_elements : integer_elements,
                                           {{ValueKind::Slot, c.collection, 0}})};
        add_entry(image);
        image = nvr::load(nvr::encode(image));
        const auto run = run_kernel_image(image, c.collection, false);
        check(run.event.kind == nvm::Event::Kind::Completed, std::string(c.label) + ": " + describe_event(run.event, image));
        check_equal(run.event.result, std::uint64_t{c.expected}, std::string(c.label) + ": the kernel's result");
        check_equal(run.in_place, std::uint64_t{1}, std::string(c.label) + ": the view is passed in place");
    }
}

NEURA_TEST(heap, writable_kernel_views_are_copied_and_written_back_on_success_only) {
    // types: 0 void, 1 int64, 2 int64[list], 3 text
    nvr::Image base;
    base.types = {{Kind::Void}, {Kind::Signed, 64}, {Kind::List, 0, 1}, {Kind::Text}};
    base.graphs = {nvr::Graph{}};
    std::vector<nvr::Value> elements;
    for (const std::int64_t v : {9, -1, 7, 0, 3}) {
        elements.push_back({ValueKind::Constant, 1, static_cast<std::uint32_t>(base.constants.size())});
        base.constants.push_back({1, le_bytes(static_cast<std::uint64_t>(v), 8)});
    }
    const auto text = static_cast<std::uint32_t>(base.constants.size());
    base.constants.push_back({3, {'a', ',', 'b'}});
    base.constants.push_back({3, {}});
    base.constants.push_back({1, le_bytes(10, 8)});

    auto sort = base;
    sort.kernels = {{"neura.sort.i64", 1, {2}, 0}};
    sort.functions = {kernel_function(2, 0, elements, {{ValueKind::Slot, 2, 0}})};
    add_entry(sort);
    sort = nvr::load(nvr::encode(sort));
    const auto sorted = run_kernel_image(sort, 2, true);
    check(sorted.event.kind == nvm::Event::Kind::Completed, "sort: " + describe_event(sorted.event, sort));
    const auto bits = [](std::int64_t v) { return static_cast<std::uint64_t>(v); };
    check(sorted.collection == std::vector<std::uint64_t>{bits(-1), 0, 3, 7, 9}, "sort writes the list back");
    check_equal(sorted.in_place, std::uint64_t{0}, "a writable view is copied");

    // An empty separator traps after the offsets were packed: the list
    // keeps its elements.
    auto split = base;
    split.kernels = {{"neura.text.split", 1, {3, 3, 1, 2}, 1}};
    split.functions = {kernel_function(2, 1, elements,
                                       {{ValueKind::Constant, 3, text}, {ValueKind::Constant, 3, text + 1},
                                        {ValueKind::Constant, 1, text + 2}, {ValueKind::Slot, 2, 0}})};
    add_entry(split);
    split = nvr::load(nvr::encode(split));
    const auto failed = run_kernel_image(split, 2, false);
    check(failed.event.kind == nvm::Event::Kind::Trapped && failed.event.trap.kind == nvm::TrapKind::Program,
          "split: " + describe_event(failed.event, split));
    check(failed.collection == std::vector<std::uint64_t>{9, bits(-1), 7, 0, 3}, "a failed kernel leaves the list unchanged");
    check_equal(failed.in_place, std::uint64_t{2}, "the two texts are passed in place, the offsets are copied");
}

NEURA_TEST(heap, narrow_collections_through_state_operations) {
    // For each narrow element type: allocate a list of boundary values,
    // append past several growth steps, write, remove, and read it back
    // through the debugger; and repeat a value into an array.
    struct Element {
        nvr::Type type;
        std::vector<std::uint64_t> values;
    };
    const auto max = ~std::uint64_t{0};
    for (const auto& element : {Element{{Kind::Signed, 8}, {0x80, 0x7f, 0xff, 0}},
                                Element{{Kind::Unsigned, 8}, {0xff, 0x80, 1}},
                                Element{{Kind::Signed, 16}, {0x8000, 0x7fff, 0xffff, 1}},
                                Element{{Kind::Signed, 32}, {0x8000'0000, 0x7fff'ffff, 0xffff'ffff, 1}},
                                Element{{Kind::Float, 32}, {0x3f80'0000, 0xffff'ffff, 0x8000'0000}},
                                Element{{Kind::Bool}, {1, 0}},
                                Element{{Kind::Unsigned, 64}, {max, 0x8000'0000'0000'0000ULL, 1}}}) {
        const auto width = element.type.kind == Kind::Bool ? 8U : element.type.width;
        const auto what = std::string("element of ") + std::to_string(width) + " bits, kind " +
                          std::to_string(static_cast<int>(element.type.kind));
        // types: 0 void, 1 element, 2 element[list], 3 uint64, 4 element[]
        nvr::Image image;
        image.types = {{Kind::Void}, element.type, {Kind::List, 0, 1}, {Kind::Unsigned, 64}, {Kind::Array, 0, 1}};
        image.graphs = {nvr::Graph{}};
        std::vector<nvr::Value> values;
        for (const auto v : element.values) {
            values.push_back({ValueKind::Constant, 1, static_cast<std::uint32_t>(image.constants.size())});
            image.constants.push_back({1, le_bytes(v, width / 8)});
        }
        const auto index = [&](std::uint64_t i) {
            image.constants.push_back({3, le_bytes(i, 8)});
            return nvr::Value{ValueKind::Constant, 3, static_cast<std::uint32_t>(image.constants.size() - 1)};
        };
        const nvr::Value list{ValueKind::Slot, 2, 0};
        std::vector<std::uint64_t> expected = element.values;
        nvr::Function function;
        function.result_type = 2;
        function.frame_types = {2, 4};
        function.blocks.push_back(state_block(Op::AllocateCollection, 1, 0, values, 2));
        for (std::uint32_t i = 0; i < 40; ++i) {
            const auto v = i % values.size();
            function.blocks.push_back(state_block(Op::AppendElement, i + 2, nvr::absent, {list, values[v]}));
            expected.push_back(element.values[v]);
        }
        auto next = static_cast<std::uint32_t>(function.blocks.size()) + 1;
        function.blocks.push_back(state_block(Op::WriteElement, next++, nvr::absent, {list, index(5), values[0]}));
        expected[5] = element.values[0];
        function.blocks.push_back(state_block(Op::RemoveElement, next++, nvr::absent, {list, index(0)}));
        expected.erase(expected.begin());
        function.blocks.push_back(
            state_block(Op::RepeatCollection, next++, 1, {index(3), values[values.size() - 1]}, 4));
        function.blocks.push_back(return_block({list}));
        image.functions = {std::move(function)};
        add_entry(image);
        image = nvr::load(nvr::encode(image));
        nvm::Execution execution(image, std::uint32_t{0}, std::span<const std::uint64_t>{});
        const auto event = execution.advance();
        check(event.kind == nvm::Event::Kind::Completed, what + ": " + describe_event(event, image));
        std::vector<std::uint64_t> read;
        for (const auto& child : execution.debug_children(2, event.result, 0, 1000)) read.push_back(child.bits);
        check(read == expected, what + ": every element reads back its bits");
        // The list holds 1 + n + 40 cells after the appends and one fewer
        // after the removal; the array of 3 then adds 4.
        check_equal(execution.peak_heap_cells(), std::uint64_t{element.values.size() + 44},
                    what + ": cells count one per element");
    }
}

} // namespace neura::testing

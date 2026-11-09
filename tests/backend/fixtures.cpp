#include "fixtures.hpp"

#include "assembler.hpp"
#include "backend/converter.hpp"

namespace neura::testing {

using namespace nvr;
using nvr::TypeKind;

fs::path backend_dir(const Environment& environment) {
    return environment.source_dir / "tests" / "backend";
}

fs::path program_path(const Environment& environment, std::string_view name) {
    return backend_dir(environment) / "programs" / (std::string(name) + ".nra");
}

namespace {

Exit return_exit(Value value) {
    Exit exit;
    exit.kind = ExitKind::Return;
    exit.arguments = {value};
    return exit;
}

Exit trap_exit(std::uint32_t reason) {
    Exit exit;
    exit.kind = ExitKind::Trap;
    exit.reason = reason;
    return exit;
}

Exit state_exit(StateOp operation, std::uint32_t immediate, std::vector<Value> arguments, std::uint32_t result,
                std::uint32_t resume) {
    Exit exit;
    exit.kind = ExitKind::State;
    exit.operation = operation;
    exit.immediate = immediate;
    exit.arguments = std::move(arguments);
    exit.result_slot = result;
    exit.resume = resume;
    return exit;
}

Block block(std::vector<Exit> exits, std::uint32_t graph = 0, std::vector<Value> inputs = {},
            std::uint32_t selector = 0) {
    Block result;
    result.graph = graph;
    result.inputs = std::move(inputs);
    result.selector_start = selector;
    result.exits = std::move(exits);
    return result;
}

} // namespace

Image hello_model() {
    Image image;
    image.types = {{TypeKind::Void}, {TypeKind::Bool}, {TypeKind::Signed, 32},
                   {TypeKind::Text}, {TypeKind::Array, 0, 3}, {TypeKind::Unsigned, 32}};
    image.constants = {{2, {0, 0, 0, 0}}, {3, {'h', 'i'}}};
    image.graphs = {Graph{}};
    image.imports = {{"neura.console.write_line", 1, {3}, 0, 5}};
    Exit host;
    host.kind = ExitKind::Host;
    host.target = 0;
    host.arguments = {{ValueKind::Constant, 3, 1}};
    host.result_slot = absent;
    host.resume = 1;
    host.error_slot = 1;
    host.error_resume = 2;
    Function function;
    function.parameter_count = 1;
    function.result_type = 2;
    function.frame_types = {4, 5};
    function.blocks = {block({host}), block({return_exit({ValueKind::Constant, 2, 0})}), block({trap_exit(3)})};
    image.functions = {function};
    image.entry = 0;
    return image;
}

// Types: 0 void, 1 bool, 2 int32, 3 text, 4 text[], 5 object of layout 0.
// Constants: 0 bool true, 1 int32 7. Global 0: int32 initialized to 7.
// Graph 0 is empty. Graph 1 is the ReLU conjunction of two lanes. Graph 2
// passes its single input lane through as the selector.
// Function 1 returns the conjunction of its two Boolean parameters.
// Function 0 calls it with true, true; traps with reason 3 when the result
// is false; then allocates an object, copies the global into its field,
// reads the field back, and returns it.
Image logic_model() {
    Image image;
    image.types = {{TypeKind::Void}, {TypeKind::Bool}, {TypeKind::Signed, 32},
                   {TypeKind::Text}, {TypeKind::Array, 0, 3}, {TypeKind::Object, 0, 0}};
    image.constants = {{1, {1}}, {2, {7, 0, 0, 0}}};
    image.layouts = {{absent, {2}}};
    image.globals = {{2, 1}};
    Graph conjunction;
    conjunction.input_lanes = 2;
    conjunction.nodes = {{Activation::Relu, -1, {{0, 1}, {1, 1}}}};
    conjunction.outputs = {2};
    Graph pass;
    pass.input_lanes = 1;
    pass.outputs = {0};
    image.graphs = {Graph{}, conjunction, pass};

    Function both;
    both.parameter_count = 2;
    both.result_type = 1;
    both.frame_types = {1, 1};
    both.blocks = {block({return_exit({ValueKind::Lanes, 1, 0})}, 1,
                         {{ValueKind::Slot, 1, 0}, {ValueKind::Slot, 1, 1}})};

    Function entry;
    entry.parameter_count = 1;
    entry.result_type = 2;
    entry.frame_types = {4, 1, 5, 2};
    Exit call;
    call.kind = ExitKind::Call;
    call.target = 1;
    call.arguments = {{ValueKind::Constant, 1, 0}, {ValueKind::Constant, 1, 0}};
    call.result_slot = 1;
    call.resume = 1;
    Exit go_on;
    go_on.kind = ExitKind::Continue;
    go_on.resume = 2;
    entry.blocks = {
        block({call}),
        block({trap_exit(3), go_on}, 2, {{ValueKind::Slot, 1, 1}}, 0),
        block({state_exit(StateOp::AllocateObject, 0, {}, 2, 3)}),
        block({state_exit(StateOp::ReadGlobal, 0, {}, 3, 4)}),
        block({state_exit(StateOp::WriteField, 0, {{ValueKind::Slot, 5, 2}, {ValueKind::Slot, 2, 3}}, absent, 5)}),
        block({state_exit(StateOp::ReadField, 0, {{ValueKind::Slot, 5, 2}}, 3, 6)}),
        block({return_exit({ValueKind::Slot, 2, 3})}),
    };
    image.functions = {entry, both};
    image.entry = 0;
    image.source_map = std::vector<SourceLocation>{{0, 0, "fixture.nra", 1, 1}, {0, 6, "fixture.nra", 7, 3}};
    return image;
}

// Types: 0 void, 1 bool, 2 int32, 3 text, 4 text[], 5 int32[list], 6 uint64.
Image version_model() {
    Image image;
    image.minor_version = 1;
    image.types = {{TypeKind::Void}, {TypeKind::Bool}, {TypeKind::Signed, 32}, {TypeKind::Text},
                   {TypeKind::Array, 0, 3}, {TypeKind::List, 0, 2}, {TypeKind::Unsigned, 64}};
    image.graphs = {Graph{}};
    Function entry;
    entry.parameter_count = 1;
    entry.result_type = 2;
    entry.frame_types = {4, 5, 6};
    entry.blocks = {
        block({state_exit(StateOp::AllocateCollection, 5, {}, 1, 1)}),
        block({state_exit(StateOp::ReadVersion, 0, {{ValueKind::Slot, 5, 1}}, 2, 2)}),
        block({trap_exit(5)}),
    };
    image.functions = {entry};
    return image;
}

std::vector<std::uint8_t> version_bytes(std::uint16_t minor) {
    using namespace nvr_bytes;
    Out functions;
    functions.u32(1);
    functions.u32(1).u32(2);
    functions.u32(3).u32(4).u32(5).u32(6);
    functions.u32(3);
    // Block 0: allocate an int32 list (type 5) into slot 1, resume 1.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(7).u32(5).u32(0).u32(1).u32(1);
    // Block 1: read the version of slot 1 into slot 2, resume 2.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(14).u32(0).raw(vector_of({nvr_bytes::value(0, 5, 1)})).u32(2).u32(2);
    // Block 2: trap with reason 5.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(5).u32(0).u32(5);

    Out graphs;
    graphs.u32(1).u32(0).u32(0).u32(0);
    Out types;
    types.u32(7).u8(0).u8(1).u8(3).u8(32).u8(4).u8(7).u32(3).u8(8).u32(2).u8(2).u8(64);
    Header header;
    header.minor = minor;
    return assemble({
        {"CONS", 1, Out().u32(0).bytes}, {"FUNC", 1, functions.bytes}, {"GLOB", 1, Out().u32(0).bytes},
        {"GRPH", 1, graphs.bytes},       {"IMPT", 1, Out().u32(0).bytes}, {"LAYO", 1, Out().u32(0).bytes},
        {"META", 1, Out().u32(0).bytes}, {"TYPE", 1, types.bytes},
    }, header);
}

std::vector<std::uint8_t> logic_bytes() {
    using namespace nvr_bytes;
    constexpr std::uint32_t none = 0xffffffffU;
    auto value = [](std::uint8_t kind, std::uint32_t type, std::uint32_t index) {
        return nvr_bytes::value(kind, type, index);
    };

    Out constants;
    constants.u32(2);
    constants.u32(1).u32(1).u8(1);
    constants.u32(2).u32(4).u8(7).u8(0).u8(0).u8(0);

    Out functions;
    functions.u32(2);
    // Function 0.
    functions.u32(1).u32(2);
    functions.u32(4).u32(4).u32(1).u32(5).u32(2);
    functions.u32(7);
    // Block 0: call function 1 with true, true into slot 1, resume 1.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(1).u32(0).u32(1).raw(vector_of({value(1, 1, 0), value(1, 1, 0)})).u32(1).u32(1);
    // Block 1: graph 2 reads slot 1; exit 0 traps, exit 1 continues to 2.
    functions.u32(2).raw(vector_of({value(0, 1, 1)})).u32(0).u32(2);
    functions.u8(5).u32(0).u32(3);
    functions.u8(0).u32(0).u32(2);
    // Block 2: allocate layout 0 into slot 2.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(0).u32(0).u32(0).u32(2).u32(3);
    // Block 3: read global 0 into slot 3.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(5).u32(0).u32(0).u32(3).u32(4);
    // Block 4: write field 0 of slot 2 from slot 3.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(3).u32(0).raw(vector_of({value(0, 5, 2), value(0, 2, 3)})).u32(none).u32(5);
    // Block 5: read field 0 of slot 2 into slot 3.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(3).u32(0).u8(2).u32(0).raw(vector_of({value(0, 5, 2)})).u32(3).u32(6);
    // Block 6: return slot 3.
    functions.u32(0).u32(0).u32(0).u32(1);
    functions.u8(2).u32(0).raw(value(0, 2, 3));
    // Function 1: both(bool, bool) -> bool.
    functions.u32(2).u32(1);
    functions.u32(2).u32(1).u32(1);
    functions.u32(1);
    functions.u32(1).raw(vector_of({value(0, 1, 0), value(0, 1, 1)})).u32(0).u32(1);
    functions.u8(2).u32(0).raw(value(2, 1, 0));

    Out globals;
    globals.u32(1).u32(2).u32(1);

    Out graphs;
    graphs.u32(3);
    graphs.u32(0).u32(0).u32(0);
    graphs.u32(2).u32(1).u8(1).i64(-1).u32(2).u32(0).i64(1).u32(1).i64(1).u32(1).u32(2);
    graphs.u32(1).u32(0).u32(1).u32(0);

    Out imports;
    imports.u32(0);

    Out layouts;
    layouts.u32(1).u32(none).u32(1).u32(2);

    Out meta;
    meta.u32(0);

    Out source_map;
    source_map.u32(2);
    source_map.u32(0).u32(0).text("fixture.nra").u32(1).u32(1);
    source_map.u32(0).u32(6).text("fixture.nra").u32(7).u32(3);

    Out types;
    types.u32(6).u8(0).u8(1).u8(3).u8(32).u8(4).u8(7).u32(3).u8(6).u32(0);

    return assemble({
        {"CONS", 1, constants.bytes}, {"FUNC", 1, functions.bytes}, {"GLOB", 1, globals.bytes},
        {"GRPH", 1, graphs.bytes},    {"IMPT", 1, imports.bytes},   {"LAYO", 1, layouts.bytes},
        {"META", 1, meta.bytes},      {"SMAP", 0, source_map.bytes}, {"TYPE", 1, types.bytes},
    });
}

Captured run_captured(const nvr::Image& image, nvm::RunLimits limits, std::optional<std::size_t> capacity,
                      const std::vector<std::string>& arguments) {
    nvm::StringSink sink = capacity ? nvm::StringSink(*capacity) : nvm::StringSink();
    nvm::ConsoleWriteLine console(sink);
    nvm::HostRegistry registry;
    registry.add(console);
    const auto bindings = nvm::bind(image, registry);
    Captured captured;
    captured.result = nvm::run(image, bindings, arguments, limits);
    captured.output = sink.contents();
    return captured;
}

nvr::Image build_program(const std::vector<fs::path>& files, const std::vector<fs::path>& roots, bool optimize) {
    backend::BuildOptions options;
    options.optimize = optimize;
    auto image = backend::build(lower_files(files, roots), options).image;
    // Every image a test runs goes through the encoder and the loader first.
    return nvr::load(nvr::encode(image));
}

std::string describe_trap(const nvm::RunResult& result, const nvr::Image& image) {
    return result.completed ? "completed with status " + std::to_string(result.status)
                            : nvm::describe(result.trap, image);
}

} // namespace neura::testing

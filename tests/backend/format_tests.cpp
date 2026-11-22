// Wire-format checks for NVR 1.0. Fixtures come from tests/backend/assembler,
// which encodes records from the specification tables independently of the
// production writer.

#include "assembler.hpp"
#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"
#include "fixtures.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <stdexcept>

namespace neura::testing {
namespace {

using namespace nvr_bytes;
using nvr::LoadError;
using Kind = LoadError::Kind;

std::string kind_name(Kind kind) { return std::string(nvr::describe(kind)); }

void expect_rejected(const Bytes& bytes, Kind kind, std::string_view fragment, const nvr::LoadLimits& limits = {},
                     std::source_location where = std::source_location::current()) {
    try {
        nvr::load(bytes, limits);
    } catch (const LoadError& error) {
        const std::string message = error.what();
        if (error.kind != kind || message.find(fragment) == std::string::npos) {
            fail("expected " + kind_name(kind) + " mentioning '" + std::string(fragment) + "', got " +
                     kind_name(error.kind) + ": " + message,
                 where);
        }
        return;
    }
    fail("expected the image to be rejected: " + std::string(fragment), where);
}

// Encodes a model with the production writer, for checks of the loader's
// semantic validation; the encoding itself is checked by the assembled
// fixtures.
void expect_invalid(const nvr::Image& image, std::string_view fragment,
                    std::source_location where = std::source_location::current()) {
    expect_rejected(nvr::encode(image), Kind::Invalid, fragment, {}, where);
}

void mutate_logic(const std::function<void(nvr::Image&)>& change, std::string_view fragment,
                  std::source_location where = std::source_location::current()) {
    auto image = logic_model();
    change(image);
    expect_invalid(image, fragment, where);
}

} // namespace

NEURA_TEST(format, version_1_1_features) {
    // A hand-assembled 1.1 image decodes to its model, and the writer emits
    // the same bytes: it raises the minor version only for 1.1 features.
    const auto bytes = version_bytes(1);
    check(nvr::load(bytes) == version_model(), "the 1.1 fixture decodes to its model");
    auto model = version_model();
    model.minor_version = 0;
    check(nvr::encode(model) == bytes, "a ReadVersion exit makes the writer emit 1.1");
    check_equal(nvr::encode(logic_model())[6], std::uint8_t{0}, "an image without 1.1 features is written as 1.0");
    auto recorded = logic_model();
    recorded.minor_version = 1;
    check_equal(nvr::encode(recorded)[6], std::uint8_t{1}, "a loaded 1.1 header is preserved");
    check(nvr::load(nvr::encode(recorded)).minor_version == 1, "the loader records the minor version");

    expect_rejected(version_bytes(0), Kind::Malformed, "uses NVR 1.1 features but declares 1.0");
    expect_rejected(version_bytes(nvr::minor_version + 1), Kind::Unsupported, "format version 1." + std::to_string(nvr::minor_version + 1) + " is not supported");
    auto reason_only = version_model();
    reason_only.functions[0].blocks[1] = reason_only.functions[0].blocks[2];
    reason_only.minor_version = 0;
    auto reason_bytes = nvr::encode(reason_only);
    check_equal(reason_bytes[6], std::uint8_t{1}, "trap reason 5 makes the writer emit 1.1");
    reason_bytes[6] = 0;
    expect_rejected(reason_bytes, Kind::Malformed, "uses NVR 1.1 features but declares 1.0");

    auto on_array = version_model();
    on_array.functions[0].blocks[1].exits[0].arguments = {{nvr::ValueKind::Slot, 4, 0}};
    expect_invalid(on_array, "argument 0 has the wrong type");
    auto wrong_slot = version_model();
    wrong_slot.functions[0].frame_types[2] = 2;
    expect_invalid(wrong_slot, "result slot must have type uint64");
}

NEURA_TEST(format, callbacks_and_native_libraries_require_version_1_4) {
    // CALB and NATV round-trip, raise the image to 1.4, and are required for
    // execution when present.
    auto image = logic_model();
    image.functions.push_back(image.functions.front());
    image.callbacks.push_back({"neura.ffi.upcall", 1, 1});
    image.native_library = nvr::NativeLibrary{"x86_64-linux-gnu", {0x7f, 'E', 'L', 'F'}, {"add", "scale"}};
    const auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{4}, "callbacks and a native library make the writer emit 1.4");
    auto decoded = nvr::load(bytes);
    image.minor_version = 4;
    check(decoded == image, "CALB and NATV round-trip");
    check(nvr::load(nvr::encode(logic_model())).callbacks.empty(), "an image without them omits both sections");

    mutate_logic([](nvr::Image& model) { model.callbacks.push_back({"neura.ffi.upcall", 1, 9}); },
                 "callback 0: function 9 is out of range");
    mutate_logic([](nvr::Image& model) { model.callbacks.push_back({"neura.ffi.upcall", 1, 0}); },
                 "the entry function cannot be a callback");
    mutate_logic(
        [](nvr::Image& model) {
            model.functions.push_back(model.functions.front());
            model.callbacks.push_back({"a.b", 1, 1});
            model.callbacks.push_back({"a.b", 1, 1});
        },
        "duplicate callback a.b version 1");
    mutate_logic([](nvr::Image& model) { model.native_library = nvr::NativeLibrary{"", {1}, {}}; },
                 "a target triple and an object are required");
    mutate_logic([](nvr::Image& model) { model.native_library = nvr::NativeLibrary{"t", {1}, {"b", "a"}}; },
                 "symbols are not unique and in ascending order");
}

NEURA_TEST(format, message_trap_requires_version_1_2_and_text) {
    auto image = hello_model();
    // Presence of an older feature must not keep a newer image at 1.1.
    image.types.push_back({nvr::TypeKind::Float, 32, 0});
    nvr::Exit trap;
    trap.kind = nvr::ExitKind::MessageTrap;
    trap.arguments = {{nvr::ValueKind::Constant, 3, 1}};
    image.functions.front().blocks.front().exits = {trap};
    auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{2}, "message trap requires NVR 1.2");
    check(nvr::load(bytes).functions.front().blocks.front().exits.front() == trap,
          "a message trap keeps its text operand");
    bytes[6] = 1;
    expect_rejected(bytes, Kind::Malformed, "uses NVR 1.2 features but declares 1.1");

    auto& exit = image.functions.front().blocks.front().exits.front();
    exit.arguments = {{nvr::ValueKind::Constant, 2, 0}};
    expect_invalid(image, "a message trap requires one text argument");
}

NEURA_TEST(format, kernel_exits_and_table) {
    // Types: 0 void, 1 bool, 2 int32, 3 text, 4 text[], 5 uint8, 6 uint8[list], 7 uint32, 8 uint64.
    // The entry allocates an empty byte list, calls neura.digest.crc32 on it, and returns 0.
    nvr::Image image;
    image.types = {{nvr::TypeKind::Void}, {nvr::TypeKind::Bool}, {nvr::TypeKind::Signed, 32},
                   {nvr::TypeKind::Text}, {nvr::TypeKind::Array, 0, 3}, {nvr::TypeKind::Unsigned, 8},
                   {nvr::TypeKind::List, 0, 5}, {nvr::TypeKind::Unsigned, 32}, {nvr::TypeKind::Unsigned, 64}};
    image.constants = {{2, {0, 0, 0, 0}}, {3, {'x'}}};
    image.graphs = {nvr::Graph{}};
    image.kernels = {{"neura.digest.crc32", 1, {6}, 7}};
    const auto exit_of = [](nvr::ExitKind kind) {
        nvr::Exit exit;
        exit.kind = kind;
        return exit;
    };
    auto allocate = exit_of(nvr::ExitKind::State);
    allocate.operation = nvr::StateOp::AllocateCollection;
    allocate.immediate = 6;
    allocate.result_slot = 1;
    allocate.resume = 1;
    auto kernel = exit_of(nvr::ExitKind::State);
    kernel.operation = nvr::StateOp::Kernel;
    kernel.immediate = 0;
    kernel.arguments = {{nvr::ValueKind::Slot, 6, 1}};
    kernel.result_slot = 2;
    kernel.resume = 2;
    auto done = exit_of(nvr::ExitKind::Return);
    done.arguments = {{nvr::ValueKind::Constant, 2, 0}};
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 2;
    entry.frame_types = {4, 6, 7};
    for (const auto& exit : {allocate, kernel, done}) {
        nvr::Block block;
        block.exits = {exit};
        entry.blocks.push_back(block);
    }
    image.functions = {entry};

    auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{6}, "a kernel exit makes the writer emit NVR 1.6");
    check(nvr::load(bytes) == [&] { auto model = image; model.minor_version = 6; return model; }(),
          "KERN and the kernel exit decode to their model");
    const auto assembly = decompiler::disassemble(nvr::load(bytes));
    check(assembly.find("kernel 0 \"neura.digest.crc32\" version 1 parameters [ 6 ] result 7") != std::string::npos &&
          assembly.find("exit state kernel immediate 0") != std::string::npos, "assembly names the kernel records");
    check(nvr::encode(decompiler::assemble(assembly)) == bytes, "kernel assembly reproduces the image bytes");
    check(decompiler::decompile(nvr::load(bytes)).text.find("kernel \"neura.digest.crc32\" v1(") != std::string::npos,
          "the decompiler names the kernel as an opaque operation");
    check(nvr::encode(hello_model())[6] == 0 && nvr::load(nvr::encode(hello_model())).kernels.empty(),
          "an image without kernels stays at its version and has no KERN section");

    bytes[6] = 5;
    expect_rejected(bytes, Kind::Malformed, "uses NVR 1.6 features but declares 1.5");
    const auto changed = [&](auto mutate, Kind kind, std::string_view reason) {
        auto other = image;
        mutate(other);
        expect_rejected(nvr::encode(other), kind, reason);
    };
    changed([](auto& i) { i.kernels[0].name = "neura.digest.md5"; }, Kind::Unsupported,
            "this runtime has no kernel neura.digest.md5 version 1");
    changed([](auto& i) { i.kernels[0].abi_version = 9; }, Kind::Unsupported, "no kernel neura.digest.crc32 version 9");
    changed([](auto& i) { i.kernels[0].parameter_types = {3}; }, Kind::Invalid, "the signature does not match");
    changed([](auto& i) { i.kernels[0].result_type = 8; }, Kind::Invalid, "the signature does not match");
    changed([](auto& i) { i.kernels.push_back(i.kernels[0]); }, Kind::Invalid, "duplicate kernel");
    changed([](auto& i) { i.functions[0].blocks[1].exits[0].immediate = 1; }, Kind::Invalid, "kernel 1 is out of range");
    changed([](auto& i) { i.functions[0].blocks[1].exits[0].arguments = {{nvr::ValueKind::Constant, 3, 1}}; },
            Kind::Invalid, "argument 0: type 3 is not assignable to type 6");
    changed([](auto& i) { i.functions[0].blocks[1].exits[0].result_slot = 1; }, Kind::Invalid, "");
    changed([](auto& i) { i.kernels.clear(); }, Kind::Malformed, "missing required section KERN");
}

NEURA_TEST(format, scheduler_exits_and_transferable_layouts) {
    auto sections = hello_sections();
    replace(sections, "TYPE", Out().u32(8).u8(0).u8(1).u8(3).u8(32).u8(4)
        .u8(7).u32(3).u8(2).u8(32).u8(2).u8(64).u8(6).u32(1).bytes);
    constexpr std::uint32_t none = 0xffffffffU;
    replace(sections, "LAYO", Out().u32(2)
        .u32(none).u32(1).u32(1).u32(0)
        .u32(0).u32(2).u32(1).u32(6).u32(0).bytes);
    replace(sections, "CONS", Out().u32(2).u32(2).u32(4).u32(0).u32(7).u32(0).bytes);
    sections.insert(sections.end() - 1, {"TRNS", 1, Out().u32(2).u32(0).u32(1).bytes});

    Out functions;
    functions.u32(2).u32(1).u32(2).u32(4).u32(4).u32(6).u32(1).u32(2).u32(8);
    const auto block = [&](const Out& exit) { functions.u32(0).u32(0).u32(0).u32(1).raw(exit.bytes); };
    const auto schedule = [&](std::uint8_t op, std::uint32_t target, const Bytes& args,
                              std::uint32_t result, std::uint32_t resume) {
        block(Out().u8(7).u32(0).u8(op).u32(target).raw(args).u32(result).u32(resume));
    };
    const auto nil = vector_of({value(1, 7, 1)});
    const auto task = vector_of({value(0, 6, 1)});
    schedule(0, 0, vector_of({}), none, 1); // Enter a scope.
    schedule(2, 1, nil, 1, 2);             // Spawn function 1, returning a task ID.
    schedule(4, 0, task, 2, 3);            // Query completion.
    schedule(3, 0, task, 3, 4);            // Join, returning the child's int32.
    schedule(1, 0, vector_of({}), none, 5); // Leave the scope.
    block(Out().u8(0x80).u32(0).u32(6));
    block(Out().u8(0x81).u32(0).u32(1).raw(nil).u32(3).u32(7));
    block(Out().u8(2).u32(0).raw(value(0, 2, 3)));
    functions.u32(1).u32(2).u32(1).u32(7).u32(1);
    block(Out().u8(2).u32(0).raw(value(1, 2, 0)));
    replace(sections, "FUNC", functions.bytes);

    const Header concurrent{.minor = 5};
    const auto bytes = assemble(sections, concurrent);
    const auto image = nvr::load(bytes);
    check(image.transferable_layouts == std::vector<std::uint32_t>{0, 1}, "TRNS decodes sorted layout indices");
    const auto& entry = image.functions.front();
    const auto exit = [&](std::size_t index) -> const nvr::Exit& { return entry.blocks[index].exits.front(); };
    check(exit(0).scheduler == nvr::SchedulerOp::ScopeEnter && exit(1).scheduler == nvr::SchedulerOp::Spawn &&
          exit(2).scheduler == nvr::SchedulerOp::IsFinished && exit(3).scheduler == nvr::SchedulerOp::Join &&
          exit(4).scheduler == nvr::SchedulerOp::ScopeExit, "scheduler opcodes decode to their declared operations");
    check(exit(1).target == 1 && exit(1).result_slot == 1 && exit(3).result_slot == 3, "spawn and join slots decode");
    check(exit(5).kind == nvr::ExitKind::Continue && exit(5).poll &&
          exit(6).kind == nvr::ExitKind::Call && exit(6).poll, "continue and call tags carry poll bits");
    check(nvr::is_transferable(image, 7) && nvr::is_transferable(image, 3) && !nvr::is_transferable(image, 4),
          "marked object and text types transfer while an array does not");
    auto unversioned = image;
    unversioned.minor_version = 0;
    check(nvr::encode(unversioned) == bytes, "the 1.5 writer reproduces independent wire bytes");

    const auto assembly = decompiler::disassemble(image);
    check(assembly.find("exit scheduler spawn target 1") != std::string::npos &&
          assembly.find("exit call poll") != std::string::npos &&
          assembly.find("transferable-layouts [ 0 1 ]") != std::string::npos, "assembly names scheduler records");
    check(decompiler::assemble(assembly) == image, "scheduler assembly preserves the complete image model");
    const auto recovered = decompiler::decompile(image).text;
    check(recovered.find("scope-enter [decoded] [reconstructed: scope b0.e0]") != std::string::npos &&
          recovered.find("scope-exit [decoded] [reconstructed: scope b0.e0]") != std::string::npos &&
          recovered.find("poll [decoded]") != std::string::npos, "decompiler pairs scopes without provenance");

    const auto changed = [&](auto mutate, Kind kind, std::string_view reason) {
        auto other = image;
        mutate(other);
        expect_rejected(nvr::encode(other), kind, reason);
    };
    expect_rejected(assemble(sections, {.minor = 4}), Kind::Malformed, "uses NVR 1.5 features but declares 1.4");
    // The writer refuses to encode tasks without the table, so the bytes
    // that lack it are assembled by hand.
    auto without_table = sections;
    std::erase_if(without_table, [](const Section& section) { return std::string_view(section.tag) == "TRNS"; });
    expect_rejected(assemble(without_table, concurrent), Kind::Malformed, "missing required section TRNS");
    {
        auto other = image;
        other.transferable_layouts.reset();
        try {
            nvr::encode(other);
            fail("the writer encoded scheduler exits without the transferable-layout table");
        } catch (const std::invalid_argument&) {
        }
        other = image;
        other.functions.front().blocks.back().exits.front().poll = true;
        try {
            nvr::encode(other);
            fail("the writer encoded a poll on a return exit");
        } catch (const std::invalid_argument&) {
        }
    }
    changed([](auto& i) { i.transferable_layouts = std::vector<std::uint32_t>{1, 0}; },
            Kind::Invalid, "strictly ascending order");
    changed([](auto& i) { i.transferable_layouts = std::vector<std::uint32_t>{0, 0}; },
            Kind::Invalid, "strictly ascending order");
    changed([](auto& i) { i.transferable_layouts = std::vector<std::uint32_t>{0, 2}; },
            Kind::Invalid, "layout 2 is out of range");
    changed([](auto& i) { i.transferable_layouts = std::vector<std::uint32_t>{1}; },
            Kind::Invalid, "parent layout 0 is not marked");
    changed([](auto& i) { i.layouts[1].fields[1] = 4; }, Kind::Invalid, "field 1 is not transferable");
    changed([](auto& i) { i.transferable_layouts = std::vector<std::uint32_t>{0}; },
            Kind::Invalid, "spawn argument is not transferable");
    changed([](auto& i) { i.functions[0].blocks[1].exits[0].result_slot = 2; }, Kind::Invalid,
            "result slot must have type uint64");
    changed([](auto& i) { i.functions[0].blocks[2].exits[0].result_slot = 1; }, Kind::Invalid,
            "result slot must have type bool");
    changed([](auto& i) { i.functions[0].blocks[3].exits[0].arguments[0] = {nvr::ValueKind::Constant, 2, 0}; },
            Kind::Invalid, "task operand must have type uint64");
    {
        // The writer refuses a poll on a scheduler exit, so set the bit in the assembled bytes: after
        // the 36-byte function header and the 16-byte block header, byte 52 of FUNC is the first exit's tag.
        auto polled = sections;
        for (auto& section : polled) {
            if (section.tag == "FUNC") section.data[52] |= 0x80;
        }
        expect_rejected(assemble(polled, concurrent), Kind::Malformed, "poll flag is valid only");
    }
    // The first code after the defined operations.
    changed([](auto& i) {
                i.functions[0].blocks[0].exits[0].scheduler = static_cast<nvr::SchedulerOp>(nvr::scheduler_op_count);
            },
            Kind::Malformed, "invalid scheduler operation 26");
    changed([](auto& i) { i.native_library = nvr::NativeLibrary{"target", {1}, {}}; }, Kind::Malformed,
            "concurrent images cannot contain CALB, NATV, or neura.ffi.* imports");
    changed([](auto& i) { i.imports[0].name = "neura.ffi.fake"; }, Kind::Malformed, "concurrent images cannot");
    sections.insert(sections.begin(), {"CALB", 1, Out().u32(0).bytes});
    expect_rejected(assemble(sections, concurrent), Kind::Malformed, "concurrent images cannot");

    auto legacy = hello_sections();
    legacy.insert(legacy.end() - 1, {"TRNS", 0, {0x01, 0x02}});
    const auto opaque_bytes = assemble(legacy);
    check(nvr::encode(nvr::load(opaque_bytes)) == opaque_bytes, "a pre-1.5 optional TRNS tag stays opaque");
    auto unresolved = image;
    unresolved.functions[0].blocks[0].exits[0].scheduler = nvr::SchedulerOp::ScopeExit;
    check(decompiler::decompile(unresolved).text.find("scope-exit [decoded] [scope pair unresolved]") != std::string::npos,
          "decompiler exposes unpaired operations without inferring a scope");
}

NEURA_TEST(format, float_types_and_constants) {
    // NVR 1.1 float types: kind 9 with width 16, 32, or 64, and kind 10 with
    // width 16, encoded like integer types; constants hold the encoding.
    const auto with_floats = [](std::uint8_t float_width, std::uint8_t bfloat_width, std::size_t payload) {
        auto sections = hello_sections();
        Out types;
        types.u32(8).u8(0).u8(1).u8(3).u8(32).u8(4).u8(7).u32(3).u8(2).u8(32).u8(9).u8(float_width).u8(10).u8(bfloat_width);
        replace(sections, "TYPE", types.bytes);
        Out constants;
        constants.u32(3);
        constants.u32(2).u32(4).u8(0).u8(0).u8(0).u8(0);
        constants.u32(3).text("hi");
        constants.u32(6).u32(static_cast<std::uint32_t>(payload));
        for (std::size_t i = 0; i < payload; ++i) {
            constants.u8(i == payload - 1 ? 0x3f : 0);
        }
        replace(sections, "CONS", constants.bytes);
        return sections;
    };
    Header newer;
    newer.minor = 1;
    const auto bytes = assemble(with_floats(32, 16, 4), newer);
    auto expected = hello_model();
    expected.minor_version = 1;
    expected.types.push_back({nvr::TypeKind::Float, 32, 0});
    expected.types.push_back({nvr::TypeKind::BFloat, 16, 0});
    expected.constants.push_back({6, {0, 0, 0, 0x3f}});
    check(nvr::load(bytes) == expected, "float types and a float constant decode to their model");
    auto unversioned = expected;
    unversioned.minor_version = 0;
    check(nvr::encode(unversioned) == bytes, "a float type makes the writer emit 1.1");
    expect_rejected(assemble(with_floats(32, 16, 4)), Kind::Malformed, "uses NVR 1.1 features but declares 1.0");
    expect_rejected(assemble(with_floats(8, 16, 1), newer), Kind::Malformed, "invalid float width 8");
    expect_rejected(assemble(with_floats(32, 32, 4), newer), Kind::Malformed, "invalid bfloat width 32");
    expect_rejected(assemble(with_floats(32, 16, 2), newer), Kind::Invalid, "a floating-point payload has 4 bytes");
    check_equal(nvr::lane_count({nvr::TypeKind::Float, 64, 0}), 64U, "float64 lanes");
    check_equal(nvr::lane_count({nvr::TypeKind::BFloat, 16, 0}), 16U, "bfloat16 lanes");
}

NEURA_TEST(format, assembled_fixtures_decode_to_their_models) {
    const auto hello = assemble(hello_sections());
    check(nvr::load(hello) == hello_model(), "the hello fixture decodes to its model");
    const auto logic = logic_bytes();
    check(nvr::load(logic) == logic_model(), "the logic fixture decodes to its model");
}

NEURA_TEST(format, static_object_constants_require_version_1_7_and_a_fieldless_layout) {
    auto image = logic_model();
    const auto layout = static_cast<std::uint32_t>(image.layouts.size());
    const auto type = static_cast<std::uint32_t>(image.types.size());
    image.layouts.push_back({nvr::absent, {}});
    image.types.push_back({nvr::TypeKind::Object, 0, layout});
    image.constants.push_back({type, {1}});
    const auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{7}, "a static object constant requires NVR 1.7");
    check(nvr::load(bytes).constants.back() == image.constants.back(), "the descriptor round trips");
    image.layouts[layout].fields = {1};
    expect_invalid(image, "a static object constant requires a fieldless layout");
}

NEURA_TEST(format, nil_and_interface_layouts_require_version_1_2) {
    auto image = logic_model();
    image.constants.push_back({5, {}});
    const auto nil_bytes = nvr::encode(image);
    check_equal(nil_bytes[6], std::uint8_t{2}, "nil constants require NVR 1.2");
    check(nvr::load(nil_bytes).constants.back() == image.constants.back(), "nil constant round trips");
    image.constants.back().payload = {0};
    expect_invalid(image, "a nil object constant has no payload");

    image = logic_model();
    image.layouts.push_back({nvr::absent, {}});
    image.layouts.push_back({nvr::absent, {}, {1}});
    image.layouts[0].interfaces = {2};
    const auto interfaces_bytes = nvr::encode(image);
    check_equal(interfaces_bytes[6], std::uint8_t{2}, "interface links require NVR 1.2");
    const auto decoded = nvr::load(interfaces_bytes);
    check(decoded.layouts == image.layouts, "interface lists round trip");
    const nvr::TypeSystem types(decoded);
    check(types.derives(0, 1) && types.derives(0, 2) && !types.derives(1, 0), "interfaces are transitive");

    image.layouts[0].interfaces.push_back(2);
    expect_invalid(image, "invalid or repeated interface layout");
    image.layouts[0].interfaces.pop_back();
    image.layouts[1].fields.push_back(2);
    expect_invalid(image, "invalid or repeated interface layout");
    image.layouts[1].fields.clear();
    image.layouts[1].interfaces = {2};
    expect_invalid(image, "links form a cycle");
}

NEURA_TEST(format, multi_word_results_need_version_1_7) {
    // tests/backend/tier/multi-result.nvra: function 2 returns (int64, float64, text),
    // function 4 (int64, int64), and function 0 is the entry. Types: 6 int64,
    // 7 float64, 0 text, 4 void; constant 4 is a float64, constant 6 a text.
    auto image = multi_result_image(environment);
    check(image.functions[2].more_results == std::vector<std::uint32_t>({7, 0}), "the assembled result words");
    check_equal(nvr::required_minor_version(image), std::uint16_t{7}, "result words declare 1.7");
    const auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{7}, "the writer emits 1.7");
    const auto loaded = nvr::load(bytes);
    check(loaded == image, "an image with result words round trips through the loader");
    check(nvr::encode(loaded) == bytes, "and re-encodes to the same bytes");

    // A re-encoded image keeps at least the version it declared, but never less than it needs.
    auto declared_old = image;
    declared_old.minor_version = 6;
    check(nvr::encode(declared_old) == bytes, "a declared 1.6 image with result words is written as 1.7");

    // Without result words the version and the bytes are what they were.
    check_equal(nvr::required_minor_version(hello_model()), std::uint16_t{0}, "no result words, no 1.7");
    check_equal(nvr::encode(hello_model())[6], std::uint8_t{0}, "an image without result words keeps its version");
    check(nvr::encode(logic_model()) == logic_bytes(), "and its bytes");
    auto empty_list = logic_model();
    empty_list.minor_version = 7;
    const auto seven = nvr::encode(empty_list);
    check_equal(seven[6], std::uint8_t{7}, "a declared 1.7 image keeps 1.7");
    check(nvr::load(seven) == empty_list, "and carries an empty result word list for every function");

    // The same bytes under a 1.6 header are not a 1.6 image: the function
    // record has no result word list there.
    auto old_header = bytes;
    old_header[6] = 6;
    bool rejected = false;
    try {
        nvr::load(old_header);
    } catch (const LoadError& error) {
        rejected = error.kind == Kind::Malformed || error.kind == Kind::Invalid;
    }
    check(rejected, "a 1.7 function table under a 1.6 header is rejected as malformed or invalid");
    auto future = bytes;
    future[6] = static_cast<std::uint8_t>(nvr::minor_version + 1);
    expect_rejected(future, Kind::Unsupported, "format version 1." + std::to_string(nvr::minor_version + 1) + " is not supported");

    // Writer checks: a return carries one value per word, and no word is void.
    auto short_return = image;
    short_return.functions[2].blocks[0].exits[0].arguments.pop_back();
    bool threw = false;
    try {
        nvr::encode(short_return);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "the writer rejects a return with too few values");
    auto void_word = image;
    void_word.functions[2].more_results = {4};
    threw = false;
    try {
        nvr::encode(void_word);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "the writer rejects a void result word");
    auto void_first = image;
    void_first.functions[2].result_type = 4;
    threw = false;
    try {
        nvr::encode(void_first);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "the writer rejects result words after a void first word");
}

NEURA_TEST(format, multi_word_results_wire_encoding) {
    // Built byte by byte from the NVR 1.7 function record in docs/runtime/NVR.md, not by the writer.
    Header seven;
    seven.minor = 7;
    const auto bytes = assemble(multi_result_sections(), seven);
    const auto image = nvr::load(bytes);
    check_equal(image.minor_version, std::uint16_t{7}, "the declared version");
    check(image.functions[1].more_results == std::vector<std::uint32_t>({5}), "the result word list follows the result type");
    check(image.functions[0].more_results.empty(), "a single-word function has an empty list");
    check_equal(image.functions[1].blocks[0].exits[0].arguments.size(), std::size_t{2}, "a return carries one value per word");
    check(nvr::encode(image) == bytes, "the writer reproduces the bytes");
    const auto run = run_captured(image);
    check(run.result.completed, "the image runs");
    check_equal(run.result.status, 7, "the call writes the first word to its result slot");

    // One value fewer than the words: the reader takes two values and runs out of record.
    expect_rejected(assemble(multi_result_sections(1), seven), Kind::Malformed, "FUNC");

    // The same function table under a 1.6 header has no list: the loader reads a frame
    // list where the list is, and what follows is not a function table.
    Header six;
    six.minor = 6;
    expect_rejected(assemble(multi_result_sections(), six), Kind::Malformed, "FUNC");
    // Under 1.6 the writer's output for the same model is the 1.7 bytes, never a 1.6 image with a list.
    auto declared_six = image;
    declared_six.minor_version = 6;
    check(nvr::encode(declared_six) == bytes, "a model with result words is encoded as 1.7");
}

NEURA_TEST(format, multi_word_results_are_validated) {
    const auto image = multi_result_image(environment);
    const auto mutated = [&](const std::function<void(nvr::Image&)>& change, std::string_view fragment,
                             std::source_location where = std::source_location::current()) {
        auto copy = image;
        change(copy);
        expect_invalid(copy, fragment, where);
    };
    // Each returned word is assignable to its word's type.
    mutated([](nvr::Image& m) { m.functions[2].blocks[0].exits[0].arguments[1] = {nvr::ValueKind::Constant, 0, 6}; },
            "value 1: type 0 is not assignable to type 7");
    mutated([](nvr::Image& m) { m.functions[2].blocks[0].exits[0].arguments[2] = {nvr::ValueKind::Constant, 7, 4}; },
            "value 2: type 7 is not assignable to type 0");
    mutated([](nvr::Image& m) { m.functions[2].blocks[0].exits[0].arguments[0] = {nvr::ValueKind::Constant, 7, 4}; },
            "value: type 7 is not assignable to type 6");
    // A call's words land in consecutive slots from the result slot, each of the word's type.
    mutated([](nvr::Image& m) { m.functions[1].blocks[2].exits[0].result_slot = 6; }, "do not fit the frame");
    mutated([](nvr::Image& m) { m.functions[1].blocks[2].exits[0].result_slot = 2; }, "result word 1: type 7 is not assignable to type 6");
    mutated([](nvr::Image& m) { m.functions[1].blocks[2].exits[0].result_slot = 7; }, "do not fit the frame");
    mutated([](nvr::Image& m) { m.functions[1].blocks[2].exits[0].result_slot = 99; }, "do not fit the frame");
    // An absent slot discards every word, which the image already does in function 0.
    check(image.functions[0].blocks[8].exits[0].result_slot == nvr::absent, "the fixture discards a two-word result");
    nvr::load(nvr::encode(image));

    // Not the entry, a spawned task, a parallel item, or a callback.
    mutated([](nvr::Image& m) { m.entry = 4; }, "the entry function cannot return several words");
    mutated([](nvr::Image& m) { m.callbacks.push_back({"app.callback", 1, 4}); }, "a callback cannot return several words");
    const auto scheduled = [&](nvr::SchedulerOp operation, std::vector<nvr::Value> arguments, std::uint32_t target) {
        auto copy = image;
        copy.transferable_layouts = std::vector<std::uint32_t>{};
        auto& exit = copy.functions[0].blocks[8].exits[0];
        exit = {};
        exit.kind = nvr::ExitKind::Scheduler;
        exit.scheduler = operation;
        exit.target = target;
        exit.arguments = std::move(arguments);
        exit.resume = 10;
        return copy;
    };
    const nvr::Value zero{nvr::ValueKind::Constant, 6, 2};
    // The same exits to a function with one result word load, so the rejection below is the result words'.
    nvr::load(nvr::encode(scheduled(nvr::SchedulerOp::Spawn, {zero, zero}, 3)));
    expect_invalid(scheduled(nvr::SchedulerOp::Spawn, {zero, zero}, 2), "a spawned task cannot return several words");
    expect_invalid(scheduled(nvr::SchedulerOp::ParallelStart, {zero, zero, zero}, 4),
                   "a parallel item cannot return several words");
}

NEURA_TEST(format, multi_word_results_round_trip_through_the_assembly_text) {
    const auto image = multi_result_image(environment);
    const auto text = decompiler::disassemble(image);
    check(text.find("nvr 1.7 profile") != std::string::npos, "the assembly text declares 1.7");
    check(text.find("result 6 more [ 7 0 ] frame") != std::string::npos, "the three-word signature is listed");
    check(text.find("result 6 more [ 6 ] frame") != std::string::npos, "the two-word signature is listed");
    check(decompiler::assemble(text) == image, "assemble(disassemble(image)) is the image");
    check(nvr::encode(decompiler::assemble(text)) == nvr::encode(image), "and encodes to the same bytes");
    // An image without result words has no `more` keyword anywhere.
    check(decompiler::disassemble(logic_model()).find(" more ") == std::string::npos, "single-word text is unchanged");
    // The text of a 1.6 image cannot list result words.
    auto old_text = text;
    old_text.replace(old_text.find("nvr 1.7"), 7, "nvr 1.6");
    bool rejected = false;
    try {
        decompiler::assemble(old_text);
    } catch (const decompiler::AssemblyError& error) {
        rejected = std::string(error.what()).find("require NVR 1.7") != std::string::npos;
    }
    check(rejected, "the assembler rejects result words under a 1.6 header");
    // The decompiled source shows each word and the call's and return's values.
    const auto source = decompiler::decompile(image).text;
    check(source.find("-> (int64, float64, text)") != std::string::npos, "the signature lists every word: " + source);
    check(source.find("return ") != std::string::npos, "returns are listed");
}

NEURA_TEST(format, aggregate_elements_need_version_1_7) {
    // tests/backend/tier/aggregate-cells.nvra: types 9 and 11 are aggregates of
    // (int64, float64) and (int64, float64, int64); 10 is an array of 9, 12 a list of 11.
    auto image = aggregate_cells_image(environment);
    check_equal(image.minor_version, std::uint16_t{7}, "the assembly text declares 1.7");
    check(image.types[9].kind == nvr::TypeKind::Aggregate && image.types[9].reference == 0, "the aggregate type");
    check(image.layouts[1].fields == std::vector<std::uint32_t>({6, 7, 6}), "the aggregate layout lists its components");
    check_equal(static_cast<unsigned>(nvr::TypeKind::Aggregate), 11U, "the type kind number");
    check_equal(static_cast<unsigned>(nvr::StateOp::ReadComponent), 25U, "the read-component number");
    check_equal(static_cast<unsigned>(nvr::StateOp::WriteComponent), 26U, "the write-component number");
    check_equal(nvr::required_minor_version(image), std::uint16_t{7}, "an aggregate type declares 1.7");
    const auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{7}, "the writer emits 1.7");
    const auto loaded = nvr::load(bytes);
    check(loaded == image, "an image with aggregates round trips through the loader");
    check(nvr::encode(loaded) == bytes, "and re-encodes to the same bytes");
    auto declared_old = image;
    declared_old.minor_version = 6;
    check(nvr::encode(declared_old) == bytes, "a declared 1.6 image with aggregates is written as 1.7");

    // A component operation alone also needs 1.7, even when a model declares less.
    auto only_ops = image;
    check_equal(nvr::required_minor_version(only_ops), std::uint16_t{7}, "components of an image");
    only_ops.types.resize(8);
    only_ops.functions[0].blocks.resize(1);
    only_ops.functions[0].blocks[0].exits[0].operation = nvr::StateOp::ReadComponent;
    check_equal(nvr::required_minor_version(only_ops), std::uint16_t{7}, "a component operation declares 1.7");

    // Without aggregates the version and the bytes are what they were.
    check_equal(nvr::required_minor_version(hello_model()), std::uint16_t{0}, "no aggregate, no 1.7");
    check_equal(nvr::encode(hello_model())[6], std::uint8_t{0}, "an image without aggregates keeps its version");
    check(nvr::encode(logic_model()) == logic_bytes(), "and its bytes");
    check(nvr::encode(multi_result_image(environment))[6] == 7, "the multi-word image still declares 1.7 alone");

    // Wire encoding, byte by byte from docs/runtime/NVR.md: kind 11 and a layout index.
    Header seven;
    seven.minor = 7;
    const auto wire = nvr::load(assemble(aggregate_sections(7), seven));
    check(wire.types[7].kind == nvr::TypeKind::Aggregate && wire.types[7].reference == 0, "the wire type record");
    check(wire.types[8].kind == nvr::TypeKind::Array && wire.types[8].reference == 7, "an array of the aggregate");
    check(nvr::encode(wire) == assemble(aggregate_sections(7), seven), "the writer reproduces the bytes");
    const auto run = run_captured(wire);
    check(run.result.completed && run.result.status == 0, "the image runs");
    check_equal(run.result.peak_heap_cells, std::uint64_t{4}, "arguments array 1, array 1 + 2 words");

    // The same function table under a 1.6 header is a well-formed 1.6 table; the aggregate type
    // is what needs 1.7.
    Header six;
    six.minor = 6;
    expect_rejected(assemble(aggregate_sections(6), six), Kind::Malformed, "uses NVR 1.7 features but declares 1.6");
    auto future = assemble(aggregate_sections(7), seven);
    future[6] = static_cast<std::uint8_t>(nvr::minor_version + 1);
    expect_rejected(future, Kind::Unsupported, "format version 1." + std::to_string(nvr::minor_version + 1) + " is not supported");
}

NEURA_TEST(format, aggregate_elements_are_validated) {
    const auto image = aggregate_cells_image(environment);
    // Blocks of the entry: 0 allocate the array, 1 repeat the list, 2 append, 3 remove, 4 read
    // component 2 of l[0] into slot 3, 5 write component 1 of a[2], 6 return. Slots: 0 text[],
    // 1 array of 9, 2 list of 11, 3 int64. Constants: 0 int64 1, 1 float64 0.5, 6 uint64 2.
    const auto mutated = [&](const std::function<void(nvr::Image&)>& change, std::string_view fragment,
                             std::source_location where = std::source_location::current()) {
        auto copy = image;
        change(copy);
        expect_invalid(copy, fragment, where);
    };
    const auto exit_of = [](nvr::Image& m, std::size_t block) -> nvr::Exit& {
        return m.functions[0].blocks[block].exits[0];
    };
    nvr::load(nvr::encode(image));

    // An aggregate is the element of an array or list and nothing else.
    const auto only = "an aggregate is valid only as the element of an array or list";
    mutated([](nvr::Image& m) { m.functions[0].frame_types.push_back(9); }, only);
    mutated([](nvr::Image& m) { m.globals.push_back({9, nvr::absent}); }, only);
    mutated([](nvr::Image& m) { m.layouts.push_back({nvr::absent, {6, 9}, {}}); }, only);
    mutated([](nvr::Image& m) { m.imports.push_back({"app.service", 1, {9}, 4, 3}); }, only);
    mutated([](nvr::Image& m) { m.imports.push_back({"app.service", 1, {}, 9, 3}); }, only);
    mutated([](nvr::Image& m) { m.functions[0].result_type = 9; }, "an aggregate is not a result type");
    mutated([&](nvr::Image& m) {
        m.functions[0].more_results = {9};
        exit_of(m, 6).arguments.push_back(exit_of(m, 6).arguments[0]);
    }, "an aggregate is not a result type");
    mutated([](nvr::Image& m) { m.constants[0].type = 9; }, "constants cannot have this type");
    mutated([](nvr::Image& m) { m.kernels.push_back({"neura.test.kernel", 1, {9}, 4}); }, only);
    // Arrays of arrays of aggregates are fine, and a list of aggregates is a slot type.
    {
        auto copy = image;
        copy.types.push_back({nvr::TypeKind::Array, 0, 10});
        copy.functions[0].frame_types.push_back(13);
        nvr::load(nvr::encode(copy));
    }
    // The layout of an aggregate: two to 64 components, no parent, no interface, no object.
    mutated([](nvr::Image& m) { m.layouts[0].fields = {6}; }, "an aggregate has 2 to 64 components, not 1");
    mutated([](nvr::Image& m) { m.layouts[0].fields.assign(65, 6); }, "an aggregate has 2 to 64 components, not 65");
    {
        // 64 components are allowed: the load gets as far as the operations, which build
        // elements of two words.
        auto copy = image;
        copy.layouts[0].fields.assign(64, 6);
        expect_invalid(copy, "words are not a whole number of 64-component elements");
    }
    mutated([](nvr::Image& m) { m.layouts[0].parent = 1; }, "the layout has a parent or interfaces");
    mutated([](nvr::Image& m) { m.layouts.push_back({0, {6, 7}, {}}); },
            "an aggregate layout cannot be a parent or interface");
    mutated([](nvr::Image& m) { m.types.push_back({nvr::TypeKind::Object, 0, 0}); },
            "an object cannot use an aggregate's layout");
    mutated([](nvr::Image& m) { m.types[9].reference = 7; }, "layout 7 is out of range");

    // ReadElement and WriteElement do not address an aggregate element.
    mutated([&](nvr::Image& m) {
        auto& exit = exit_of(m, 4);
        exit.operation = nvr::StateOp::ReadElement;
        exit.immediate = 0;
    }, "read-element cannot address an aggregate element");
    mutated([&](nvr::Image& m) {
        auto& exit = exit_of(m, 5);
        exit.operation = nvr::StateOp::WriteElement;
        exit.immediate = 0;
    }, "write-element cannot address an aggregate element");
    // Component operations: the component index, the collection and the types.
    mutated([&](nvr::Image& m) { exit_of(m, 4).immediate = 3; }, "component 3 is out of range");
    mutated([&](nvr::Image& m) { exit_of(m, 5).immediate = 2; }, "component 2 is out of range");
    mutated([&](nvr::Image& m) { exit_of(m, 4).result_slot = 1; }, "result: type 6 is not assignable to type 10");
    mutated([&](nvr::Image& m) { exit_of(m, 5).arguments[2] = {nvr::ValueKind::Constant, 6, 0}; },
            "component value: type 6 is not assignable to type 7");
    mutated([&](nvr::Image& m) { exit_of(m, 5).arguments.pop_back(); }, "expects 3 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 4).arguments[1] = {nvr::ValueKind::Constant, 7, 1}; },
            "argument 1 must be an unsigned 64-bit index");
    mutated([&](nvr::Image& m) { exit_of(m, 5).result_slot = 3; }, "a void result requires an absent slot");
    mutated([](nvr::Image& m) {
        m.types.push_back({nvr::TypeKind::Array, 0, 6});
        m.functions[0].frame_types.push_back(13);
        auto& exit = m.functions[0].blocks[4].exits[0];
        exit.arguments[0] = {nvr::ValueKind::Slot, 13, 4};
        exit.immediate = 0;
    }, "a component operation needs a collection of aggregates");
    // Building a collection: whole elements, each word of its component's type.
    mutated([&](nvr::Image& m) { exit_of(m, 0).arguments.pop_back(); },
            "5 words are not a whole number of 2-component elements");
    mutated([&](nvr::Image& m) { exit_of(m, 0).arguments.push_back(exit_of(m, 0).arguments[0]); },
            "7 words are not a whole number of 2-component elements");
    mutated([&](nvr::Image& m) { std::swap(exit_of(m, 0).arguments[0], exit_of(m, 0).arguments[1]); },
            "element 0: type 7 is not assignable to type 6");
    mutated([&](nvr::Image& m) { std::swap(exit_of(m, 0).arguments[4], exit_of(m, 0).arguments[5]); },
            "element 4: type 7 is not assignable to type 6");
    mutated([&](nvr::Image& m) { exit_of(m, 1).arguments.pop_back(); }, "expects 4 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 1).arguments[2] = exit_of(m, 1).arguments[1]; },
            "element 2: type 6 is not assignable to type 7");
    mutated([&](nvr::Image& m) { exit_of(m, 2).arguments.pop_back(); }, "expects 4 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 2).arguments[3] = exit_of(m, 2).arguments[2]; },
            "component value 2: type 7 is not assignable to type 6");
    mutated([&](nvr::Image& m) { exit_of(m, 2).arguments[0] = {nvr::ValueKind::Slot, 10, 1}; },
            "argument 0 has the wrong type");
    // Removal and length are unchanged: a list and an index.
    {
        auto copy = image;
        nvr::load(nvr::encode(copy));
        auto& exit = exit_of(copy, 3);
        exit.arguments.push_back(exit.arguments[1]);
        expect_invalid(copy, "expects 2 argument(s)");
    }
    // A kernel's collection arguments are scalars: an array of aggregates is not one.
    // (Kernel signatures are checked against a table, in kernel_tests.cpp.)
}

NEURA_TEST(format, storage_regions_need_version_1_7) {
    // tests/backend/tier/region-storage.nvra: enter-region (27), release-region (28), region-object
    // (29), region-collection (30), region-repeat (31) and move-to-region (32).
    check_equal(static_cast<unsigned>(nvr::StateOp::EnterRegion), 27U, "the enter-region number");
    check_equal(static_cast<unsigned>(nvr::StateOp::ReleaseRegion), 28U, "the release-region number");
    check_equal(static_cast<unsigned>(nvr::StateOp::RegionObject), 29U, "the region-object number");
    check_equal(static_cast<unsigned>(nvr::StateOp::RegionCollection), 30U, "the region-collection number");
    check_equal(static_cast<unsigned>(nvr::StateOp::RegionRepeat), 31U, "the region-repeat number");
    check_equal(static_cast<unsigned>(nvr::StateOp::MoveToRegion), 32U, "the move-to-region number");
    check_equal(static_cast<unsigned>(nvr::StateOp::HookEnter), 33U, "the hook-enter number");
    check_equal(static_cast<unsigned>(nvr::StateOp::HookLeave), 34U, "the hook-leave number");
    check_equal(static_cast<unsigned>(nvr::StateOp::ConcatText), 35U, "the concat-text number");
    check_equal(static_cast<unsigned>(nvr::StateOp::AppendRun), 36U, "the append-run number");
    check_equal(static_cast<unsigned>(nvr::StateOp::RetainCellStorage), 37U, "the retain-cell-storage number");
    check_equal(static_cast<unsigned>(nvr::state_op_count), 38U, "the operation count");
    const auto image = region_image(environment);
    check_equal(image.minor_version, std::uint16_t{7}, "the assembly text declares 1.7");
    check(nvr::uses_regions(image), "the image uses regions");
    check_equal(nvr::required_minor_version(image), std::uint16_t{7}, "a region operation declares 1.7");
    const auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{7}, "the writer emits 1.7");
    const auto loaded = nvr::load(bytes);
    check(loaded == image, "an image with regions round trips through the loader");
    check(nvr::encode(loaded) == bytes, "and re-encodes to the same bytes");
    auto declared_old = image;
    declared_old.minor_version = 6;
    check(nvr::encode(declared_old) == bytes, "a declared 1.6 image with regions is written as 1.7");
    // Without a region operation the version and the bytes are what they were.
    check(!nvr::uses_regions(hello_model()) && !nvr::uses_regions(aggregate_cells_image(environment)),
          "an image without region operations does not use regions");
    check_equal(nvr::required_minor_version(hello_model()), std::uint16_t{0}, "no region, no 1.7");
    check(nvr::encode(logic_model()) == logic_bytes(), "an image that uses none keeps its bytes");
    // Wire encoding, byte by byte: operation numbers 27 and 28.
    Header seven;
    seven.minor = 7;
    const auto wire = nvr::load(assemble(region_sections(7), seven));
    check(wire.functions[0].blocks[0].exits[0].operation == nvr::StateOp::EnterRegion, "the wire enter-region");
    check(wire.functions[0].blocks[1].exits[0].operation == nvr::StateOp::ReleaseRegion, "the wire release-region");
    check(nvr::encode(wire) == assemble(region_sections(7), seven), "the writer reproduces the bytes");
    const auto run = run_captured(wire);
    check(run.result.completed && run.result.status == 0, "the image runs");
    check_equal(run.result.peak_stack_cells, std::uint64_t{0}, "an empty region costs no cell");
    // The same function table under a 1.6 header is a well-formed 1.6 table; the operations are
    // what needs 1.7.
    Header six;
    six.minor = 6;
    expect_rejected(assemble(region_sections(6), six), Kind::Malformed, "uses NVR 1.7 features but declares 1.6");
}

NEURA_TEST(format, storage_region_operations_are_validated) {
    const auto image = region_image(environment);
    nvr::load(nvr::encode(image));
    const auto mutated = [&](const std::function<void(nvr::Image&)>& change, std::string_view fragment,
                             std::source_location where = std::source_location::current()) {
        auto copy = image;
        change(copy);
        expect_invalid(copy, fragment, where);
    };
    // F6 (nested): 0 enter, 1 region-repeat, 2 enter, 3 region-collection, 8 release. F8 (make):
    // 3 move-to-region. F9 (make_holder): 1 region-object. Slots of F6: 0, 1 uint64, 2, 3
    // int64[], 4 int64.
    const auto exit_of = [](nvr::Image& m, std::size_t function, std::size_t block) -> nvr::Exit& {
        return m.functions[function].blocks[block].exits[0];
    };
    const nvr::Value token{nvr::ValueKind::Slot, 8, 0};
    const nvr::Value not_a_token{nvr::ValueKind::Slot, 6, 4};
    // enter-region: no operand, no immediate and a uint64 result.
    mutated([&](nvr::Image& m) { exit_of(m, 6, 0).arguments.push_back(token); }, "expects 0 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 0).result_slot = 4; }, "result slot must have type uint64");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 0).result_slot = nvr::absent; }, "result slot must have type uint64");
    // release-region: a uint64 token, no result.
    mutated([&](nvr::Image& m) { exit_of(m, 6, 8).arguments[0] = not_a_token; },
            "argument 0 must be a uint64 region token");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 8).arguments[0] = {nvr::ValueKind::Slot, 11, 3}; },
            "argument 0 must be a uint64 region token");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 8).result_slot = 0; }, "a void result requires an absent slot");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 8).immediate = 1; }, "immediate must be zero");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 8).arguments.clear(); }, "expects 1 argument(s)");
    // region-repeat and region-collection: the token first, then as the allocation without one.
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).arguments[0] = not_a_token; },
            "argument 0 must be a uint64 region token");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).arguments.pop_back(); }, "expects 3 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).arguments[1] = not_a_token; },
            "argument 1 must be an unsigned 64-bit index");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).arguments[2] = token; }, "is not assignable");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).immediate = 9; }, "immediate must name an array or list type");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).immediate = 99; }, "type 99 is out of range");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 1).result_slot = 4; }, "is not assignable");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 3).arguments.clear(); }, "expects a region token");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 3).arguments[1] = token; }, "is not assignable");
    mutated([&](nvr::Image& m) { exit_of(m, 6, 3).arguments[0] = not_a_token; },
            "argument 0 must be a uint64 region token");
    // region-object: the token, an existing layout, a result of that layout.
    mutated([&](nvr::Image& m) { exit_of(m, 9, 1).immediate = 7; }, "layout 7 is out of range");
    mutated([&](nvr::Image& m) { exit_of(m, 9, 1).arguments.clear(); }, "expects 1 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 9, 1).arguments[0] = {nvr::ValueKind::Slot, 9, 3}; },
            "argument 0 must be a uint64 region token");
    mutated([&](nvr::Image& m) { exit_of(m, 9, 1).immediate = 1; }, "result slot must have type a reference to the allocated layout");
    // move-to-region: a reference, then a token, and nothing back.
    mutated([&](nvr::Image& m) { exit_of(m, 8, 3).arguments[0] = {nvr::ValueKind::Slot, 8, 0}; },
            "argument 0 has the wrong type");
    mutated([&](nvr::Image& m) { exit_of(m, 8, 3).arguments[1] = {nvr::ValueKind::Slot, 11, 3}; },
            "argument 1 must be a uint64 region token");
    mutated([&](nvr::Image& m) { exit_of(m, 8, 3).arguments.pop_back(); }, "expects 2 argument(s)");
    mutated([&](nvr::Image& m) { exit_of(m, 8, 3).result_slot = 0; }, "a void result requires an absent slot");
    mutated([&](nvr::Image& m) { exit_of(m, 8, 3).immediate = 2; }, "unknown move-to-region mode");
}

NEURA_TEST(format, a_region_token_is_not_a_task_or_channel_operand) {
    // A token belongs to the task that entered the region. It is a uint64 and has no type of its
    // own, so the loader follows the slots EnterRegion writes, and the slots they are assigned to,
    // and refuses them as the operands of a scheduler exit; the runtime refuses a token from
    // another task (tier.storage_regions_of_tasks_are_their_own).
    const auto image = region_tasks_image(environment);
    nvr::load(nvr::encode(image));
    // F0 of the tasks image: slot 1 is the token, blocks 4 and 5 spawn the children.
    const auto spawn_with = [&](const nvr::Value& argument, const std::vector<nvr::Assignment>& assignments) {
        auto copy = image;
        auto& block = copy.functions[0].blocks[4];
        block.exits[0].arguments = {argument};
        copy.functions[0].blocks[3].exits[0].assignments = assignments;
        return copy;
    };
    expect_invalid(spawn_with({nvr::ValueKind::Slot, 5, 1}, {}), "a region token cannot be a task or channel operand");
    // Through a copy: slot 3 receives the token on the way into the scope.
    expect_invalid(spawn_with({nvr::ValueKind::Slot, 5, 3}, {{3, {nvr::ValueKind::Slot, 5, 1}}}),
                   "a region token cannot be a task or channel operand");
    // A uint64 that is no token is not refused for being one (the callee takes no argument).
    expect_invalid(spawn_with({nvr::ValueKind::Constant, 5, 1}, {}), "expects 0 argument(s)");
}

NEURA_TEST(format, region_images_round_trip_through_the_assembler) {
    for (const auto& image : {region_image(environment), region_tasks_image(environment),
                              region_tasks_image(environment, true)}) {
        const auto text = decompiler::disassemble(image);
        const auto assembled = decompiler::assemble(text);
        check(assembled == image, "the assembled model is the loaded one");
        check(nvr::encode(assembled) == nvr::encode(image), "assembling the text keeps the bytes");
        check_equal(decompiler::disassemble(assembled), text, "disassembly is stable");
        check(text.find("enter-region") != std::string::npos && text.find("release-region") != std::string::npos,
              "the operations are named");
        const auto source = decompiler::decompile(image).text;
        check(source.find("enter_region()") != std::string::npos, "the decompiler names enter_region");
    }
    const auto text = decompiler::disassemble(region_image(environment));
    for (const auto* name : {"region-object", "region-collection", "region-repeat", "move-to-region"}) {
        check(text.find(name) != std::string::npos, std::string("the text names ") + name);
    }
    const auto source = decompiler::decompile(region_image(environment)).text;
    for (const auto* form : {"release_region(", " in ", "move "}) {
        check(source.find(form) != std::string::npos, std::string("the decompiled text shows ") + form);
    }
    // A text that declares 1.6 and uses a region operation is refused by the assembler.
    auto old = text;
    old.replace(old.find("nvr 1.7"), 7, "nvr 1.6");
    bool refused = false;
    try {
        decompiler::assemble(old);
    } catch (const decompiler::AssemblyError& error) {
        refused = std::string(error.what()).find("require NVR 1.7") != std::string::npos;
    }
    check(refused, "the assembler refuses region operations under a 1.6 header");
}

NEURA_TEST(format, region_entries_show_as_heap_entries_marked_with_their_region_in_the_debugger) {
    // F0 calls F6: three blocks in (the call, enter-region, region-repeat) the array of three
    // int64 words is in slot 2 of F6's frame, the innermost, an entry of the region whose token is in slot 0. The
    // step limit stops the run; the debugger's launch keeps the regions of a trap in place.
    const auto image = region_image(environment);
    const auto frame_of = [&](bool keep) {
        nvm::RunLimits limits;
        limits.max_steps = 3;
        limits.keep_regions_at_end = keep;
        auto execution = std::make_unique<nvm::Execution>(image, std::span<const std::string>{}, limits);
        const auto event = execution->advance();
        check(event.kind == nvm::Event::Kind::Trapped, "the step limit stops the run");
        const auto snapshot = execution->debug_snapshot();
        check(!snapshot.threads.empty() && snapshot.threads[0].frames.size() == 2, "two frames");
        return std::tuple{snapshot.threads[0].frames[0].slots, execution->live_stack_cells(), std::move(execution)};
    };
    const auto [kept, kept_cells, kept_run] = frame_of(true);
    check_equal(kept[2].text, std::string{"array[3] [region " + std::to_string(kept[0].bits) + "]"},
                "the entry shows its length and its region's token");
    check_equal(kept[2].children, std::size_t{3}, "and its elements");
    check_equal(kept_cells, std::uint64_t{4}, "the entry's cells are stack cells");
    const auto [released, released_cells, released_run] = frame_of(false);
    check_equal(released[2].text, std::string{"<released reference>"}, "a trap released the region");
    check_equal(released_cells, std::uint64_t{0}, "no cell is left");
}

NEURA_TEST(format, an_aggregate_is_not_a_channel_element_type) {
    const auto image = aggregate_cells_image(environment);
    // channel-open takes its element type as a type index: an aggregate is not one.
    {
        auto copy = image;
        copy.layouts.push_back({nvr::absent, {6}, {}});
        copy.types.push_back({nvr::TypeKind::Object, 0, 2});
        copy.functions[0].frame_types.push_back(13);
        copy.transferable_layouts = std::vector<std::uint32_t>{};
        auto& exit = copy.functions[0].blocks[4].exits[0];
        exit = {};
        exit.kind = nvr::ExitKind::Scheduler;
        exit.scheduler = nvr::SchedulerOp::ChannelOpen;
        exit.target = 6;
        exit.arguments = {{nvr::ValueKind::Slot, 13, 4}, {nvr::ValueKind::Constant, 6, 0}};
        exit.resume = 5;
        nvr::load(nvr::encode(copy));
        exit.target = 9;
        expect_invalid(copy, "the channel's element type must be a transferable type");
        exit.target = 10;
        expect_invalid(copy, "the channel's element type must be a transferable type");
    }
}

NEURA_TEST(format, aggregate_collections_show_their_elements_as_groups_in_the_debugger) {
    // Stopped by the step limit after the six blocks that build, change and read the collections:
    // slot 1 is the array of three (int64, float64) elements, slot 2 the list of two
    // (int64, float64, int64) elements.
    const auto image = aggregate_cells_image(environment);
    nvm::RunLimits limits;
    limits.max_steps = 6;
    nvm::Execution execution(image, std::span<const std::string>{}, limits);
    const auto event = execution.advance();
    check(event.kind == nvm::Event::Kind::Trapped, "the step limit stops the run");
    const auto snapshot = execution.debug_snapshot();
    check(!snapshot.threads.empty() && !snapshot.threads[0].frames.empty(), "the frame is kept");
    const auto& slots = snapshot.threads[0].frames[0].slots;
    check_equal(slots[1].text, std::string{"array[3]"}, "the array's length counts elements");
    check_equal(slots[1].children, std::size_t{3}, "one child per element");
    check_equal(slots[2].text, std::string{"list[2]"}, "the list's length counts elements");
    const auto array = execution.debug_children(slots[1].type, slots[1].bits, 0, 100);
    check_equal(array.size(), std::size_t{3}, "three groups");
    check_equal(array[0].text, std::string{"(1, 0.5)"}, "an element is a group of its components");
    check_equal(array[2].text, std::string{"(3, 9.5)"}, "a written component shows");
    const auto list = execution.debug_children(slots[2].type, slots[2].bits, 0, 100);
    check_equal(list.size(), std::size_t{2}, "two groups");
    check_equal(list[1].text, std::string{"(4, 0.75, 5)"}, "three components");
    check_equal(execution.debug_children(slots[2].type, slots[2].bits, 1, 100).size(), std::size_t{1}, "a window");
    check_equal(slots[3].text, std::string{"2"}, "the component read by block 4 is int64 2");
}

NEURA_TEST(format, aggregate_elements_round_trip_through_the_assembly_text) {
    const auto image = aggregate_cells_image(environment);
    const auto text = decompiler::disassemble(image);
    check(text.find("nvr 1.7 profile") != std::string::npos, "the assembly text declares 1.7");
    check(text.find("type 9 aggregate 0") != std::string::npos, "the aggregate type is listed");
    check(text.find("read-component immediate 2") != std::string::npos, "the component read is listed");
    check(text.find("write-component immediate 1") != std::string::npos, "the component write is listed");
    check(decompiler::assemble(text) == image, "assemble(disassemble(image)) is the image");
    check(nvr::encode(decompiler::assemble(text)) == nvr::encode(image), "and encodes to the same bytes");
    const auto large = aggregate_image(environment);
    check(decompiler::assemble(decompiler::disassemble(large)) == large, "the larger image round trips too");
    // The text of a 1.6 image names no aggregate and no component operation.
    auto old_text = text;
    old_text.replace(old_text.find("nvr 1.7"), 7, "nvr 1.6");
    bool rejected = false;
    try {
        decompiler::assemble(old_text);
    } catch (const decompiler::AssemblyError& error) {
        rejected = std::string(error.what()).find("aggregate types require NVR 1.7") != std::string::npos;
    }
    check(rejected, "the assembler rejects an aggregate type under a 1.6 header");
    auto no_type = decompiler::disassemble(aggregate_cells_image(environment));
    no_type.replace(no_type.find("nvr 1.7"), 7, "nvr 1.6");
    no_type.replace(no_type.find("type 9 aggregate 0"), 18, "type 9 void");
    no_type.replace(no_type.find("type 11 aggregate 1"), 19, "type 11 void");
    rejected = false;
    try {
        decompiler::assemble(no_type);
    } catch (const decompiler::AssemblyError& error) {
        rejected = std::string(error.what()).find("component operations require NVR 1.7") != std::string::npos;
    }
    check(rejected, "and a component operation under a 1.6 header");
    // The decompiled source shows the element types and the components.
    const auto source = decompiler::decompile(image).text;
    check(source.find("(int64, float64)[]") != std::string::npos, "the array's element type is listed: " + source);
    check(source.find("(int64, float64, int64)[list]") != std::string::npos, "the list's element type is listed");
    check(source.find("[uint64 0].c2") != std::string::npos || source.find("].c2") != std::string::npos,
          "the component read is listed: " + source);
    check(source.find("].c1 = ") != std::string::npos, "the component write is listed");
    check(source.find("repeat(") != std::string::npos && source.find("append(") != std::string::npos,
          "repeat and append list every word");
}

NEURA_TEST(format, writer_reproduces_assembled_fixtures) {
    check(nvr::encode(hello_model()) == assemble(hello_sections()), "writer output matches the hello fixture");
    check(nvr::encode(logic_model()) == logic_bytes(), "writer output matches the logic fixture");
}

NEURA_TEST(format, assembled_fixtures_run) {
    const auto hello = run_captured(nvr::load(assemble(hello_sections())));
    check(hello.result.completed && hello.result.status == 0, "hello fixture completes with status 0");
    check_equal(hello.output, std::string("hi\n"), "hello fixture output");
    const auto image = nvr::load(logic_bytes());
    const auto logic = run_captured(image);
    check(logic.result.completed, describe_trap(logic.result, image));
    check_equal(logic.result.status, 7, "logic fixture status");
}

NEURA_TEST(format, void_return_is_the_shortest_exit) {
    // A void function's return exit is five bytes: its kind and an empty
    // assignment list. Placing one last in FUNC checks that the loader's
    // size bound before reading the exit list admits it.
    auto sections = hello_sections();
    auto& functions = sections[1].data;
    functions[0] = 2;  // two functions
    Out callee;
    callee.u32(0).u32(0).u32(0);              // no parameters, void result, empty frame
    callee.u32(1).u32(0).u32(0).u32(0);       // one block: graph 0, no inputs, selector 0
    callee.u32(1).u8(2).u32(0);               // one exit: return, no assignments
    functions.insert(functions.end(), callee.bytes.begin(), callee.bytes.end());
    auto expected = hello_model();
    nvr::Function function;
    function.result_type = 0;
    nvr::Exit exit;
    exit.kind = nvr::ExitKind::Return;
    nvr::Block block;
    block.exits = {exit};
    function.blocks = {block};
    expected.functions.push_back(function);
    const auto bytes = assemble(sections);
    check(nvr::load(bytes) == expected, "the image with a trailing void return decodes");
    check(nvr::encode(expected) == bytes, "the writer emits five bytes for a void return");
}

NEURA_TEST(format, every_truncation_is_rejected) {
    for (const auto& bytes : {assemble(hello_sections()), logic_bytes()}) {
        for (std::size_t size = 0; size < bytes.size(); ++size) {
            const Bytes prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
            try {
                nvr::load(prefix);
                fail("a " + std::to_string(size) + "-byte prefix was accepted");
            } catch (const LoadError& error) {
                check(error.kind == Kind::Malformed, "prefix " + std::to_string(size) + ": " + error.what());
            }
        }
    }
}

NEURA_TEST(format, truncated_section_records_are_rejected) {
    // Shorten one section at a time and lay the image out again, so that the
    // header and directory stay consistent and only the records are cut.
    const auto sections = hello_sections();
    for (std::size_t s = 0; s < sections.size(); ++s) {
        for (std::size_t size = 0; size < sections[s].data.size(); ++size) {
            auto cut = sections;
            cut[s].data.resize(size);
            try {
                nvr::load(assemble(cut));
                fail(sections[s].tag + " cut to " + std::to_string(size) + " bytes was accepted");
            } catch (const LoadError& error) {
                check(error.kind == Kind::Malformed,
                      sections[s].tag + " cut to " + std::to_string(size) + ": " + error.what());
            }
        }
    }
}

NEURA_TEST(format, header_errors) {
    const auto sections = hello_sections();
    Header bad_magic;
    bad_magic.magic = "NEUX";
    expect_rejected(assemble(sections, bad_magic), Kind::Malformed, "not an NVR image");
    Header newer_minor;
    newer_minor.minor = nvr::minor_version + 1;
    expect_rejected(assemble(sections, newer_minor), Kind::Unsupported,
                    "format version 1." + std::to_string(nvr::minor_version + 1) + " is not supported" + "; this loader accepts 1.0 through 1." + std::to_string(nvr::minor_version));
    Header newer_major;
    newer_major.major = 2;
    expect_rejected(assemble(sections, newer_major), Kind::Unsupported, "format version 2.0");
    Header profile;
    profile.profile = 2;
    expect_rejected(assemble(sections, profile), Kind::Unsupported, "graph profile 2");
    const std::uint64_t wrong_size = 12;
    expect_rejected(assemble(sections, {}, &wrong_size), Kind::Malformed, "file size field");

    auto bytes = assemble(sections);
    bytes[12] = 0xff;  // section count far beyond the file
    expect_rejected(bytes, Kind::Malformed, "truncated section directory");
}

NEURA_TEST(format, directory_errors) {
    const auto base = hello_sections();
    {
        auto bytes = assemble(base);
        bytes[directory_offset + 4] = 3;  // reserved flag bit on CONS
        expect_rejected(bytes, Kind::Malformed, "reserved flag bits");
    }
    {
        auto bytes = assemble(base);
        bytes[directory_offset] = 0x01;  // non-printable tag byte
        expect_rejected(bytes, Kind::Malformed, "printable ASCII");
    }
    {
        auto sections = base;
        sections.insert(sections.begin() + 1, sections[0]);
        expect_rejected(assemble(sections), Kind::Malformed, "duplicate section CONS");
    }
    {
        auto sections = base;
        std::swap(sections[0], sections[1]);
        expect_rejected(assemble(sections), Kind::Malformed, "out of ascending tag order");
    }
    {
        auto sections = base;
        sections.erase(sections.begin() + 2);  // GLOB
        expect_rejected(assemble(sections), Kind::Malformed, "missing required section GLOB");
    }
    {
        // Move CONS one byte later: a gap before it and the file one byte
        // longer than the directory accounts for.
        auto bytes = assemble(base);
        const auto at = directory_offset + 8;
        bytes[at] = static_cast<std::uint8_t>(bytes[at] + 1);
        expect_rejected(bytes, Kind::Malformed, "(gap or overlap)");
    }
    {
        // Point FUNC one byte into CONS.
        auto bytes = assemble(base);
        const auto at = directory_offset + directory_record + 8;
        bytes[at] = static_cast<std::uint8_t>(bytes[at] - 1);
        expect_rejected(bytes, Kind::Malformed, "(gap or overlap)");
    }
    {
        auto bytes = assemble(base);
        bytes.push_back(0);
        const std::uint64_t size = bytes.size();
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[16 + i] = static_cast<std::uint8_t>(size >> (8 * i));
        }
        expect_rejected(bytes, Kind::Malformed, "follow the last section");
    }
    {
        auto bytes = assemble(base);
        const auto at = directory_offset + 8 + 8;  // CONS size
        bytes[at] = 0xff;
        bytes[at + 1] = 0xff;
        expect_rejected(bytes, Kind::Malformed, "extends past the end");
    }
}

NEURA_TEST(format, unknown_and_misflagged_sections) {
    auto sections = hello_sections();
    sections.push_back({"XTRA", 0, {1, 2, 3}});
    {
        // An unknown optional section does not affect execution. The loader
        // keeps its bytes so that re-encoding reproduces the image.
        const auto bytes = assemble(sections);
        auto expected = hello_model();
        expected.other_sections.push_back({{'X', 'T', 'R', 'A'}, {1, 2, 3}});
        const auto loaded = nvr::load(bytes);
        check(loaded == expected, "an unknown optional section is kept raw");
        check(nvr::encode(loaded) == bytes, "re-encoding keeps the unknown optional section in tag order");
        auto colliding = expected;
        colliding.other_sections.push_back({{'T', 'Y', 'P', 'E'}, {}});
        bool rejected = false;
        try {
            nvr::encode(colliding);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "the writer rejects a raw section that reuses a known tag");
    }
    sections.back() = {"ZZZZ", 1, {}};
    expect_rejected(assemble(sections), Kind::Unsupported, "unknown required section ZZZZ");

    auto required_map = hello_sections();
    required_map.insert(required_map.end() - 1, {"SMAP", 1, Out().u32(0).bytes});
    expect_rejected(assemble(required_map), Kind::Malformed, "SMAP has the wrong required flag");
    auto optional_constants = hello_sections();
    optional_constants[0].flags = 0;
    expect_rejected(assemble(optional_constants), Kind::Malformed, "CONS has the wrong required flag");
}

NEURA_TEST(format, record_encoding_errors) {
    {
        auto sections = hello_sections();
        sections[0].data.push_back(0);
        expect_rejected(assemble(sections), Kind::Malformed, "CONS: 1 unread bytes");
    }
    {
        auto sections = hello_sections();
        replace(sections, "TYPE", Out().u32(1).u8(12).bytes);
        expect_rejected(assemble(sections), Kind::Malformed, "invalid type kind 12");
    }
    {
        auto sections = hello_sections();
        replace(sections, "TYPE", Out().u32(1).u8(3).u8(12).bytes);
        expect_rejected(assemble(sections), Kind::Malformed, "invalid integer width 12");
    }
    {
        auto sections = hello_sections();
        replace(sections, "IMPT", Out().u32(1).u32(2).u8(0xc3).u8(0x28).u32(1).u32(0).u32(0).u32(0).bytes);
        expect_rejected(assemble(sections), Kind::Malformed, "import name is not valid UTF-8");
    }
    {
        auto sections = hello_sections();
        replace(sections, "GRPH", Out().u32(1).u32(0).u32(1).u8(2).i64(0).u32(0).u32(0).bytes);
        expect_rejected(assemble(sections), Kind::Malformed, "invalid activation 2");
    }
    {
        auto sections = hello_sections();
        replace(sections, "GLOB", Out().u32(0xffffffffU).bytes);
        expect_rejected(assemble(sections), Kind::Malformed, "reserved index");
    }
    {
        // An invalid kind in the first exit of the first block.
        auto sections = hello_sections();
        auto& data = sections[1].data;
        // Function count, parameter count, result type, frame (count and two
        // slots), block count, graph, input count, selector start, exit count.
        const std::size_t exit_kind = 4 * 11;
        check_equal(static_cast<int>(data[exit_kind]), 4, "fixture layout: host exit kind");
        data[exit_kind] = 255;
        expect_rejected(assemble(sections), Kind::Malformed, "invalid exit kind 255");
        data[exit_kind] = 3;  // a state operation needs more bytes than remain valid
        data[exit_kind + 5] = 255;
        expect_rejected(assemble(sections), Kind::Malformed, "invalid state operation 255");
    }
}

NEURA_TEST(format, load_limits) {
    const auto bytes = assemble(hello_sections());
    nvr::LoadLimits small_image;
    small_image.max_image_bytes = bytes.size() - 1;
    expect_rejected(bytes, Kind::LimitExceeded, "exceeds the limit", small_image);
    nvr::LoadLimits short_vectors;
    short_vectors.max_vector_length = 5;
    expect_rejected(bytes, Kind::LimitExceeded, "count 6 exceeds the limit", short_vectors);
    nvr::LoadLimits few_wires;
    few_wires.max_graph_wires = 2;
    expect_rejected(logic_bytes(), Kind::LimitExceeded, "wire limit", few_wires);
}

NEURA_TEST(format, type_and_layout_validation) {
    mutate_logic([](nvr::Image& i) { i.types.push_back({nvr::TypeKind::Object, 0, 9}); }, "layout 9 is out of range");
    mutate_logic([](nvr::Image& i) { i.types.push_back({nvr::TypeKind::List, 0, 0}); }, "void is not a value type");
    mutate_logic([](nvr::Image& i) {
        i.types.push_back({nvr::TypeKind::Array, 0, 7});
        i.types.push_back({nvr::TypeKind::List, 0, 6});
    }, "element types form a cycle");
    mutate_logic([](nvr::Image& i) { i.layouts[0].parent = 4; }, "parent 4 is out of range");
    mutate_logic([](nvr::Image& i) { i.layouts[0].parent = 0; }, "parent links form a cycle");
    mutate_logic([](nvr::Image& i) { i.layouts.push_back({0, {1}}); }, "inherited field 0 changes type");
    mutate_logic([](nvr::Image& i) { i.layouts.push_back({0, {}}); }, "does not contain its parent's fields");
}

NEURA_TEST(format, constant_and_global_validation) {
    mutate_logic([](nvr::Image& i) { i.constants[0].payload = {2}; }, "Boolean payload");
    mutate_logic([](nvr::Image& i) { i.constants[1].payload = {7, 0}; }, "integer payload has 4 bytes");
    mutate_logic([](nvr::Image& i) { i.constants.push_back({3, {0xc3, 0x28}}); }, "not valid UTF-8");
    mutate_logic([](nvr::Image& i) { i.constants.push_back({0, {}}); }, "constants cannot have this type");
    // NVR 1.7: an array or list constant is the nil collection, with no payload.
    mutate_logic([](nvr::Image& i) { i.constants.push_back({4, {1}}); }, "a nil collection constant has no payload");
    auto nil_collection = logic_model();
    nil_collection.constants.push_back({4, {}});
    const auto bytes = nvr::encode(nil_collection);
    check_equal(bytes[6], std::uint8_t{7}, "a nil collection constant makes the writer emit 1.7");
    check(nvr::load(bytes) == [&] { auto loaded = nil_collection; loaded.minor_version = 7; return loaded; }(),
          "a nil collection constant round-trips");
    mutate_logic([](nvr::Image& i) { i.globals[0].initializer = 0; }, "initializer type does not match");
    mutate_logic([](nvr::Image& i) { i.globals[0].initializer = 5; }, "initializer 5 is out of range");
    mutate_logic([](nvr::Image& i) { i.globals[0].type = 0; }, "void is not a value type");
}

NEURA_TEST(format, graph_validation) {
    mutate_logic([](nvr::Image& i) { i.graphs[1].nodes[0].edges[1].source = 2; }, "does not refer to an earlier wire");
    mutate_logic([](nvr::Image& i) { std::swap(i.graphs[1].nodes[0].edges[0], i.graphs[1].nodes[0].edges[1]); },
                 "edges are not sorted by unique source");
    mutate_logic([](nvr::Image& i) { i.graphs[1].nodes[0].edges[1].source = 0; },
                 "edges are not sorted by unique source");
    mutate_logic([](nvr::Image& i) { i.graphs[1].outputs[0] = 3; }, "output wire 3 is out of range");
}

NEURA_TEST(format, block_validation) {
    mutate_logic([](nvr::Image& i) { i.functions[1].blocks[0].graph = 3; }, "graph 3 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[1].blocks[0].inputs.pop_back(); },
                 "inputs supply 1 lanes but the graph takes 2");
    mutate_logic([](nvr::Image& i) { i.functions[1].blocks[0].inputs[1].kind = nvr::ValueKind::Lanes; },
                 "only frame slots and constants may supply graph inputs");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].exits.clear(); }, "at least one exit");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].selector_start = 1; },
                 "one exit has selector start zero");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].selector_start = 1; },
                 "selector lanes exceed the graph outputs");
}

NEURA_TEST(format, exit_validation) {
    using nvr::ValueKind;
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].exits[1].resume = 9; }, "block 9 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].exits[0].target = 5; }, "function 5 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].exits[0].arguments.pop_back(); },
                 "expects 2 argument(s)");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].exits[0].arguments[1] = {ValueKind::Constant, 2, 1}; },
                 "is not assignable");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].exits[0].result_slot = nvr::absent; },
                 "a nonvoid result requires a slot");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[0].exits[0].result_slot = 3; }, "is not assignable");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[4].exits[0].result_slot = 3; },
                 "a void result requires an absent slot");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[6].exits[0].arguments[0] = {ValueKind::Slot, 1, 1}; },
                 "is not assignable");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].exits[0].assignments.push_back({1, {ValueKind::Constant, 1, 0}}); },
                 "a trap exit has no assignments");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].exits[0].reason = 0; }, "trap reason 0 is not defined");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].exits[0].reason = 7; }, "trap reason 7 is not defined");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[1].exits[1];
        exit.assignments = {{1, {ValueKind::Constant, 1, 0}}, {1, {ValueKind::Slot, 1, 1}}};
    }, "slot 1 is assigned twice");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].exits[1].assignments = {{8, {ValueKind::Constant, 1, 0}}}; },
                 "destination slot 8 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[6].exits[0].arguments[0] = {ValueKind::Slot, 1, 3}; },
                 "descriptor type does not match slot 3");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[6].exits[0].arguments[0] = {ValueKind::Constant, 1, 1}; },
                 "descriptor type does not match constant 1");
    mutate_logic([](nvr::Image& i) { i.functions[1].blocks[0].exits[0].arguments[0].index = 1; },
                 "exceed the graph's 1 outputs");
}

NEURA_TEST(format, state_operation_validation) {
    using nvr::ValueKind;
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[2].exits[0].immediate = 1; }, "layout 1 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[3].exits[0].immediate = 1; }, "global 1 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[5].exits[0].immediate = 1; }, "field 1 is out of range");
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[4].exits[0].arguments.pop_back(); }, "expects 2 argument(s)");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[5].exits[0];
        exit.operation = nvr::StateOp::ReadTag;
    }, "result slot must have type uint32");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[5].exits[0];
        exit.operation = nvr::StateOp::FreeObject;
        exit.immediate = 0;
        exit.result_slot = nvr::absent;
        exit.arguments = {{ValueKind::Slot, 1, 1}};
    }, "argument 0 has the wrong type");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[5].exits[0];
        exit.operation = nvr::StateOp::FreeObject;
        exit.immediate = 2;
        exit.result_slot = nvr::absent;
    }, "immediate must be zero");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[5].exits[0];
        exit.operation = nvr::StateOp::ReadLength;
        exit.immediate = 0;
    }, "argument 0 has the wrong type");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[5].exits[0];
        exit.operation = nvr::StateOp::ReadElement;
        exit.immediate = 0;
        exit.arguments = {{ValueKind::Slot, 4, 0}, {ValueKind::Slot, 2, 3}};
    }, "must be an unsigned 64-bit index");
    mutate_logic([](nvr::Image& i) {
        auto& exit = i.functions[0].blocks[5].exits[0];
        exit.operation = nvr::StateOp::ConstructText;
        exit.immediate = 3;
        exit.arguments = {{ValueKind::Slot, 4, 0}};
    }, "argument must hold uint8 elements");
}

NEURA_TEST(format, import_function_entry_and_map_validation) {
    mutate_logic([](nvr::Image& i) {
        i.imports = {{"x", 1, {}, 0, 0}, {"x", 1, {}, 0, 0}};
    }, "duplicate import x version 1");
    mutate_logic([](nvr::Image& i) { i.imports = {{"x", 1, {0}, 0, 0}}; }, "void is not a value type");
    mutate_logic([](nvr::Image& i) { i.functions[1].parameter_count = 3; }, "more parameters than frame slots");
    mutate_logic([](nvr::Image& i) { i.functions[1].frame_types.push_back(0); }, "void is not a value type");
    mutate_logic([](nvr::Image& i) { i.functions[1].blocks.clear(); }, "needs an entry block");
    mutate_logic([](nvr::Image& i) { i.entry = 2; }, "entry function 2 is out of range");
    mutate_logic([](nvr::Image& i) { i.entry = 1; }, "must take one UTF-8 text array and return int32");
    mutate_logic([](nvr::Image& i) { std::swap((*i.source_map)[0], (*i.source_map)[1]); },
                 "not sorted and unique");
    mutate_logic([](nvr::Image& i) { (*i.source_map)[0].line = 0; }, "lines and columns start at one");
    mutate_logic([](nvr::Image& i) { (*i.source_map)[1].block = 7; }, "block reference is out of range");
}

NEURA_TEST(format, long_type_chains_load_without_recursion) {
    // 200,000 nested array types, deeper than a resolver that recursed once
    // per level could go on a default stack.
    auto image = logic_model();
    for (std::uint32_t i = 0; i < 200'000; ++i) {
        image.types.push_back({nvr::TypeKind::Array, 0, static_cast<std::uint32_t>(image.types.size() - 1)});
    }
    const auto loaded = nvr::load(nvr::encode(image));
    check_equal(loaded.types.size(), image.types.size(), "types loaded");
    nvr::TypeSystem system(loaded);
    check(!system.same(6, 7), "different nesting depths are different types");
}

NEURA_TEST(format, structural_types_are_equal) {
    // Equal types under different indices are interchangeable.
    auto image = logic_model();
    image.types.push_back({nvr::TypeKind::Signed, 32});
    image.functions[0].frame_types[3] = 6;
    image.functions[0].blocks[6].exits[0].arguments[0].type = 6;
    image.functions[0].blocks[5].exits[0].result_slot = 3;
    const auto loaded = nvr::load(nvr::encode(image));
    const auto run = run_captured(loaded);
    check(run.result.completed && run.result.status == 7, describe_trap(run.result, loaded));
}

// docs/runtime/NVR.md, Text concatenation (1.9): the writer selects 1.9 only
// for concat-text, and the loader checks its arguments and result.
NEURA_TEST(format, concat_text_needs_version_1_9) {
    const auto path = fs::temp_directory_path() / "neura-format-tests" / "concat.nra";
    fs::create_directories(path.parent_path());
    std::ofstream(path) << R"(let program = import:Std.Program;

let join = func(a: string, b: string, c: string) -> string => a .. b .. c;

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        if (join("a", "b", "c") != "abc") { return program.ExitCode.Failure; }
        return program.ExitCode.Ok;
    };
);
)";
    const auto image = build_program({path});
    check_equal(image.minor_version, std::uint16_t{9}, "a chain declares 1.9");
    std::size_t function = 0, block = 0, exit = 0;
    bool found = false;
    for (std::size_t f = 0; f < image.functions.size() && !found; ++f)
        for (std::size_t b = 0; b < image.functions[f].blocks.size() && !found; ++b)
            for (std::size_t e = 0; e < image.functions[f].blocks[b].exits.size() && !found; ++e) {
                const auto& candidate = image.functions[f].blocks[b].exits[e];
                if (candidate.kind == nvr::ExitKind::State && candidate.operation == nvr::StateOp::ConcatText) {
                    function = f, block = b, exit = e, found = true;
                }
            }
    check(found, "the image has a concat-text exit");
    if (!found) return;
    const auto bytes = nvr::encode(image);
    check_equal(bytes[6], std::uint8_t{9}, "the writer emits 1.9");
    check(nvr::encode(nvr::load(bytes)) == bytes, "an image with concat-text round trips");
    auto declared_old = image;
    declared_old.minor_version = 8;
    check(nvr::encode(declared_old) == bytes, "a declared 1.8 image with concat-text is written as 1.9");
    auto old_header = bytes;
    old_header[6] = 8;
    expect_rejected(old_header, Kind::Malformed, "uses NVR 1.9 features but declares 1.8");
    const auto mutated = [&](const std::function<void(nvr::Exit&, nvr::Function&)>& change) {
        auto copy = image;
        auto& target = copy.functions[function];
        change(target.blocks[block].exits[exit], target);
        return nvr::encode(copy);
    };
    expect_rejected(mutated([](nvr::Exit& e, nvr::Function&) { e.arguments.resize(1); }), Kind::Invalid,
                    "at least two arguments");
    expect_rejected(mutated([](nvr::Exit& e, nvr::Function&) { e.immediate = 1; }), Kind::Invalid,
                    "immediate must be zero");
    // Argument 1 read from a new frame slot of type bool.
    expect_rejected(mutated([&](nvr::Exit& e, nvr::Function& f) {
                        for (std::uint32_t t = 0; t < image.types.size(); ++t) {
                            if (image.types[t].kind != nvr::TypeKind::Bool) continue;
                            f.frame_types.push_back(t);
                            e.arguments[1] = {nvr::ValueKind::Slot, t, static_cast<std::uint32_t>(f.frame_types.size() - 1)};
                            break;
                        }
                    }),
                    Kind::Invalid, "argument 1 has the wrong type");
    expect_rejected(mutated([](nvr::Exit& e, nvr::Function&) { e.result_slot = nvr::absent; }), Kind::Invalid,
                    "result slot must have type text");
}

NEURA_TEST(format, append_run_needs_version_1_10_and_keeps_loop_accounting) {
    const auto path = fs::temp_directory_path() / "neura-format-tests" / "append-run.nra";
    fs::create_directories(path.parent_path());
    std::ofstream(path) << R"(let buffers = import:Std.Io.ByteBuffer;
let builders = import:Std.Core.StringBuilder;
let lists = import:Std.Collections.Lists;
let program = import:Std.Program;
let strings = import:Std.Text.Strings;

export let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        last bytes = lists.filled<uint8>(513, 97);
        last buffer = buffers.create();
        buffer.write(bytes);
        last builder = builders.create();
        last joined = strings.join(new(["first", "second", "third"]), ":");
        builder.append(joined);
        if (bytes.version != 513 || buffer.size != 513 || builder.to_string() != "first:second:third") {
            return program.ExitCode.Failure;
        }
        return program.ExitCode.Ok;
    };
);
)";
    const auto image = build_program({path});
    check_equal(image.minor_version, std::uint16_t{10}, "a list run declares 1.10");
    const auto binary = nvr::encode(image);
    check_equal(binary[6], std::uint8_t{10}, "the writer emits 1.10");
    check(nvr::encode(nvr::load(binary)) == binary, "a list run round trips through the loader");
    check(nvr::encode(decompiler::assemble(decompiler::disassemble(image))) == binary,
          "assembly round trips every list run byte");
    auto old_header = binary;
    old_header[6] = 9;
    expect_rejected(old_header, Kind::Malformed, "uses NVR 1.10 features but declares 1.9");

    auto fallback = image;
    std::array<bool, 5> seen{};
    std::optional<nvr::Image> invalid;
    for (std::size_t f = 0; f < fallback.functions.size(); ++f) {
        for (std::size_t b = 0; b < fallback.functions[f].blocks.size(); ++b) {
            for (auto& exit : fallback.functions[f].blocks[b].exits) {
                if (exit.kind != nvr::ExitKind::State || exit.operation != nvr::StateOp::AppendRun) continue;
                if (!invalid) {
                    invalid = image;
                    invalid->functions[f].blocks[b].exits.front().immediate = 99;
                }
                seen.at(exit.immediate) = true;
                exit.arguments.erase(exit.arguments.begin() + 1, exit.arguments.begin() + 3);
                if (exit.immediate != static_cast<std::uint32_t>(nvr::AppendRunKind::Repeat)) exit.arguments.pop_back();
                exit.operation = nvr::StateOp::AppendElement;
                exit.result_slot = nvr::absent;
                exit.immediate = 0;
            }
        }
    }
    if (!invalid) return;
    expect_invalid(*invalid, "unknown append-run kind");
    check(std::ranges::all_of(seen, [](bool present) { return present; }), "all five list-run forms are emitted");
    fallback.minor_version = nvr::required_minor_version(fallback);
    const auto elementwise = nvr::load(nvr::encode(fallback));

    for (std::uint8_t tier = 0; tier <= 2; ++tier) {
        nvm::RunLimits limits;
        limits.tier = tier;
        limits.profile = true;
        const auto ample_budget = limits.max_steps;
        for (const auto budget : {std::uint64_t{100}, std::uint64_t{800}, ample_budget}) {
            limits.max_steps = budget;
            const auto expected = run_captured(elementwise, limits);
            const auto actual = run_captured(image, limits);
            check(actual.result.completed == expected.result.completed, "list-run completion matches the loop");
            check_equal(actual.result.status, expected.result.status, "status and version checks match");
            check(actual.result.trap.kind == expected.result.trap.kind &&
                  actual.result.trap.detail == expected.result.trap.detail &&
                  actual.result.trap.block == expected.result.trap.block, "list-run failure matches the loop");
            check_equal(actual.result.steps, expected.result.steps, "logical steps match");
            check_equal(actual.result.peak_heap_cells, expected.result.peak_heap_cells, "peak heap matches");
            check(actual.result.function_steps == expected.result.function_steps, "profiled steps match per function");
            if (budget == ample_budget) check(actual.result.completed && actual.result.status == 0, "contents and version match");
        }
    }
}

} // namespace neura::testing

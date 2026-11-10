// Wire-format checks for NVR 1.0. Fixtures come from tests/backend/assembler,
// which encodes records from the specification tables independently of the
// production writer.

#include "assembler.hpp"
#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"
#include "fixtures.hpp"

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
    expect_rejected(version_bytes(6), Kind::Unsupported, "format version 1.6 is not supported");
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
    changed([](auto& i) { i.functions[0].blocks[0].exits[0].scheduler = static_cast<nvr::SchedulerOp>(5); },
            Kind::Malformed, "invalid scheduler operation 5");
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
    newer_minor.minor = 6;
    expect_rejected(assemble(sections, newer_minor), Kind::Unsupported,
                    "format version 1.6 is not supported; this loader accepts 1.0 through 1.5");
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
        replace(sections, "TYPE", Out().u32(1).u8(11).bytes);
        expect_rejected(assemble(sections), Kind::Malformed, "invalid type kind 11");
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
    mutate_logic([](nvr::Image& i) { i.constants.push_back({4, {}}); }, "constants cannot have this type");
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
    mutate_logic([](nvr::Image& i) { i.functions[0].blocks[1].exits[0].reason = 6; }, "trap reason 6 is not defined");
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

} // namespace neura::testing

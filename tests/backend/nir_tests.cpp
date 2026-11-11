// The versioned .nir file: exact round trips and rejection of corrupt files.

#include "fixtures.hpp"

#include "ir-optimization/concurrency_support.hpp"
#include "lexer/lexer.hpp"
#include "neura-ir/attributes.hpp"
#include "neura-ir/linker.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "nvr/format.hpp"
#include "parser/syntax.hpp"

#include "backend/converter.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>

namespace neura::testing {
namespace {

std::string mismatch;

bool same(const IrType& a, const IrType& b, const std::string& where) {
    if (!(a == b)) {
        mismatch = where + ": type differs";
        return false;
    }
    return true;
}

bool same(const Span& a, const Span& b, const std::string& where) {
    if (a.file != b.file || a.line != b.line || a.column != b.column || a.offset != b.offset ||
        a.length != b.length) {
        mismatch = where + ": span differs";
        return false;
    }
    return true;
}

bool same(const std::vector<IrNode>& a, const std::vector<IrNode>& b, const std::string& where);

bool same(const IrNode& a, const IrNode& b, const std::string& where) {
    if (a.op != b.op || a.symbol != b.symbol || a.text != b.text || a.type_arguments != b.type_arguments ||
        a.poll != b.poll) {
        mismatch = where + ": node differs";
        return false;
    }
    return same(a.type, b.type, where) && same(a.span, b.span, where) &&
           same(a.operands, b.operands, where + "/operands") && same(a.body, b.body, where + "/body") &&
           same(a.alternative, b.alternative, where + "/alternative");
}

bool same(const std::vector<IrNode>& a, const std::vector<IrNode>& b, const std::string& where) {
    if (a.size() != b.size()) {
        mismatch = where + ": region sizes differ";
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same(a[i], b[i], where + "[" + std::to_string(i) + "]")) {
            return false;
        }
    }
    return true;
}

// Every field of every symbol and node, including spans.
bool same_program(const IrProgram& a, const IrProgram& b) {
    if (a.symbols.size() != b.symbols.size() || a.bodies.size() != b.bodies.size() ||
        a.static_constants.size() != b.static_constants.size()) {
        mismatch = "table sizes differ";
        return false;
    }
    for (std::size_t i = 0; i < a.symbols.size(); ++i) {
        const auto& x = a.symbols[i];
        const auto& y = b.symbols[i];
        const auto where = "symbol " + std::to_string(i);
        if (x.kind != y.kind || x.name != y.name || x.owner != y.owner || x.base != y.base ||
            x.overrides != y.overrides || x.parameters != y.parameters || x.generics != y.generics ||
            x.bound != y.bound || x.payload_types != y.payload_types ||
            x.exported != y.exported || x.constant != y.constant ||
            x.external != y.external || x.has_body != y.has_body ||
            x.library_path != y.library_path || x.interfaces != y.interfaces ||
            x.additional_bounds != y.additional_bounds || x.overload_group != y.overload_group ||
            x.variadic != y.variadic || x.user_cast != y.user_cast || x.singleton_instance != y.singleton_instance ||
            x.attributes != y.attributes ||
            x.host_binding.has_value() != y.host_binding.has_value() ||
            (x.host_binding && (x.host_binding->name != y.host_binding->name ||
                                x.host_binding->version != y.host_binding->version))) {
            mismatch = where + " differs";
            return false;
        }
        if (!same(x.type, y.type, where) || !same(x.span, y.span, where)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.static_constants.size(); ++i) {
        if (a.static_constants[i].field != b.static_constants[i].field ||
            !same(a.static_constants[i].value, b.static_constants[i].value, "static constant")) return false;
    }
    for (std::size_t i = 0; i < a.bodies.size(); ++i) {
        if (a.bodies[i].owner != b.bodies[i].owner ||
            !same(a.bodies[i].instructions, b.bodies[i].instructions, "body " + std::to_string(i))) {
            if (mismatch.empty()) {
                mismatch = "body " + std::to_string(i) + " owner differs";
            }
            return false;
        }
    }
    return true;
}

struct Source {
    std::string label;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
};

std::vector<Source> sources(const Environment& environment) {
    std::vector<Source> result;
    const auto examples = example_files(environment);
    result.push_back({"all examples", examples,
                      {environment.examples() / "guide", environment.examples() / "modules" / "multi-file"}});
    for (const auto& fixture : fixture_files(environment)) {
        result.push_back({fixture.filename().string(), {fixture}, {}});
    }
    for (const auto& entry : fs::directory_iterator(backend_dir(environment) / "programs")) {
        if (entry.path().extension() == ".nra") {
            result.push_back({entry.path().filename().string(), {entry.path()}, {}});
        }
    }
    // Programs that spawn tasks carry polls, which the round trip must keep.
    for (const auto& fixture : concurrency_fixtures(environment)) {
        result.push_back({"concurrency " + fixture.name, {fixture.path}, {}});
    }
    return result;
}

// Points every span and file symbol of `program` that names `from` at `to`.
void rename_file(IrProgram& program, const std::string& from, const std::string& to) {
    const auto rename = [&](Span& span) {
        if (span.file == from) span.file = to;
    };
    const std::function<void(IrNode&)> node = [&](IrNode& item) {
        rename(item.span);
        for (auto* region : {&item.operands, &item.body, &item.alternative}) {
            for (auto& child : *region) node(child);
        }
    };
    for (auto& symbol : program.symbols) {
        rename(symbol.span);
        if (symbol.kind == SymbolKind::File && symbol.name == from) symbol.name = to;
    }
    for (auto& body : program.bodies) {
        for (auto& instruction : body.instructions) node(instruction);
    }
    for (auto& constant : program.static_constants) node(constant.value);
}

void for_each_node(const IrProgram& program, const std::function<void(const IrNode&)>& visit) {
    const std::function<void(const IrNode&)> walk = [&](const IrNode& item) {
        visit(item);
        for (const auto* region : {&item.operands, &item.body, &item.alternative}) {
            for (const auto& child : *region) walk(child);
        }
    };
    for (const auto& body : program.bodies) {
        for (const auto& instruction : body.instructions) walk(instruction);
    }
}

std::vector<std::uint8_t> read_bytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    check(file.good(), "cannot read " + path.string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void expect_nir_error(const std::vector<std::uint8_t>& bytes, std::string_view fragment,
                      std::source_location where = std::source_location::current()) {
    try {
        read_nir(bytes, "test.nir");
    } catch (const NirError& error) {
        check(std::string(error.what()).find(fragment) != std::string::npos,
              "expected '" + std::string(fragment) + "', got: " + error.what(), where);
        return;
    }
    fail("expected the .nir file to be rejected: " + std::string(fragment), where);
}

} // namespace

NEURA_TEST(nir, round_trip_preserves_every_field) {
    for (const auto& source : sources(environment)) {
        const auto program = lower_files(source.files, source.roots);
        const auto bytes = write_nir(program);
        const auto read = read_nir(bytes, source.label);
        mismatch.clear();
        check(same_program(program, read), source.label + ": " + mismatch);
        check(write_nir(read) == bytes, source.label + ": rewriting the read program changes its bytes");
        check(printed(read) == printed(program), source.label + ": listing differs after the round trip");
    }
}

NEURA_TEST(nir, encoding_is_deterministic) {
    for (const auto& source : sources(environment)) {
        check(write_nir(lower_files(source.files, source.roots)) == write_nir(lower_files(source.files, source.roots)),
              source.label + ": two lowerings encode differently");
    }
}

NEURA_TEST(nir, both_build_routes_emit_identical_images) {
    // `neura build x.nra` and `neura build x.nir` share one pipeline after
    // loading, so equal IR must give equal image bytes.
    for (const auto& source : sources(environment)) {
        auto program = lower_files(source.files, source.roots);
        auto from_nir = read_nir(write_nir(program), source.label);
        std::vector<std::uint8_t> direct;
        std::string direct_error;
        try {
            direct = nvr::encode(backend::build(std::move(program)).image);
        } catch (const Diagnostic& error) {
            direct_error = error.what();
        }
        std::vector<std::uint8_t> routed;
        std::string routed_error;
        try {
            routed = nvr::encode(backend::build(std::move(from_nir)).image);
        } catch (const Diagnostic& error) {
            routed_error = error.what();
        }
        check_equal(routed_error, direct_error, source.label + ": diagnostics of the two routes");
        check(routed == direct, source.label + ": the two routes emit different images");
    }
}

NEURA_TEST(nir, corrupt_files_are_rejected) {
    // Keep the exhaustive truncation check independent of the amount of
    // standard library source imported by Hello World.
    const auto bytes = write_nir(lower_files({backend_dir(environment) / "nir-versions" / "loops.nra"}));
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        try {
            read_nir(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)), "x");
            fail("a " + std::to_string(size) + "-byte prefix was accepted");
        } catch (const NirError&) {
        }
    }
    auto changed = bytes;
    changed[0] = 'X';
    expect_nir_error(changed, "not a NeuraIR file");
    changed = bytes;
    changed[4] = 2;
    expect_nir_error(changed, "NeuraIR format 2." + std::to_string(bytes[6]) + " is not supported");
    changed = bytes;
    changed[6] = static_cast<std::uint8_t>(nir_minor_version + 1);
    expect_nir_error(changed, "NeuraIR format 1." + std::to_string(changed[6]) + " is not supported");
    changed = bytes;
    changed.push_back(0);
    expect_nir_error(changed, "1 unread bytes");

    IrProgram dangling;
    dangling.bodies.push_back({5, {}});
    expect_nir_error(write_nir(dangling), "symbol reference 5 is out of range");

    IrProgram bad_op;
    bad_op.symbols.push_back(IrSymbol{SymbolKind::Function, "f", {}, no_symbol, no_symbol, no_symbol, {}, {}, {}, {},
                                      false, false, false, true});
    IrNode node{};
    node.op = static_cast<IrOp>(200);
    bad_op.bodies.push_back({0, {node}});
    expect_nir_error(write_nir(bad_op), "invalid operation tag 200");

    IrProgram deep = bad_op;
    IrNode nested{};
    nested.op = IrOp::Block;
    for (int i = 0; i < 1100; ++i) {
        IrNode outer{};
        outer.op = IrOp::Block;
        outer.body.push_back(std::move(nested));
        nested = std::move(outer);
    }
    deep.bodies[0].instructions = {std::move(nested)};
    expect_nir_error(write_nir(deep), "nesting exceeds");
}

NEURA_TEST(nir, minor_version_follows_the_features_used) {
    // Programs without 1.1 operations or loop latches are written as 1.0,
    // so their files are unchanged; for loops and conversions need 1.1.
    // Hello World imports the standard library, whose matches need 1.2, so
    // the 1.0 check uses a library module of the multi-file example.
    const auto plain = write_nir(lower_files(
        {environment.examples() / "modules" / "multi-file" / "acme" / "greetings" / "punctuation.nra"}));
    check_equal(static_cast<int>(plain[6]), 0, "punctuation.nir is NeuraIR 1.0");
    // Programs that import the standard library use its 1.3 operations, so
    // these checks use import-free modules.
    const auto versions = backend_dir(environment) / "nir-versions";
    const auto loops = write_nir(lower_files({versions / "loops.nra"}));
    check_equal(static_cast<int>(loops[6]), 1, "loops.nir is NeuraIR 1.1");
    auto downgraded = loops;
    downgraded[6] = 0;
    try {
        read_nir(downgraded, "loops.nir");
        fail("a 1.0 header on a file with 1.1 features should be rejected");
    } catch (const NirError& error) {
        const std::string message = error.what();
        check(message.find("invalid operation tag") != std::string::npos ||
                  message.find("a loop latch requires NeuraIR format 1.1") != std::string::npos,
              message);
    }
    for (const auto* fixture : {"aggregates", "operators"}) {
        auto current = write_nir(lower_files({versions / (std::string(fixture) + ".nra")}));
        check_equal(static_cast<int>(current[6]), 2, std::string(fixture) + " uses NeuraIR 1.2");
        current[6] = 1;
        expect_nir_error(current, std::string_view(fixture) == "operators"
                          ? "bitwise operations require NeuraIR format 1.2" : "invalid");
    }
    auto range = write_nir(lower_files({versions / "range.nra"}));
    check_equal(static_cast<int>(range[6]), 4, "range.nir is NeuraIR 1.4");
    range[6] = 3;
    expect_nir_error(range, "invalid operation tag 59");
    auto statics = write_nir(lower_files({versions / "statics.nra"}));
    check_equal(static_cast<int>(statics[6]), 5, "lazy statics require NeuraIR 1.5");
    statics[6] = 4;
    expect_nir_error(statics, "class initializers require NeuraIR format 1.5");
}

// NeuraIR 1.9 (Concurrency.md section 13): tasks and polls are the only reason
// to write it. The files in nir-versions/before-tasks were written by the
// compiler as it stood before the task operations existed (the parent of the
// commit that added them), from the programs of the same names, with the
// source path in each file replaced by nir-versions/<name>.nra. The current
// writer must reproduce them byte for byte, and the current reader must
// accept them.
NEURA_TEST(nir, programs_without_tasks_keep_the_bytes_of_the_previous_writer) {
    const auto versions = backend_dir(environment) / "nir-versions";
    struct Golden {
        const char* name;
        fs::path source;
        int minor;
    };
    const std::vector<Golden> goldens{
        {"punctuation", environment.examples() / "modules" / "multi-file" / "acme" / "greetings" / "punctuation.nra", 0},
        {"loops", versions / "loops.nra", 1},
        {"aggregates", versions / "aggregates.nra", 2},
        {"operators", versions / "operators.nra", 2},
        {"range", versions / "range.nra", 4},
        {"statics", versions / "statics.nra", 5},
    };
    for (const auto& golden : goldens) {
        const std::string name = golden.name;
        const auto expected = read_bytes(versions / "before-tasks" / (name + ".nir"));
        check_equal(static_cast<int>(expected[6]), golden.minor, name + ": golden minor version");
        auto program = lower_files({golden.source});
        bool polled = false;
        for_each_node(program, [&](const IrNode& item) { polled = polled || item.poll; });
        check(!polled && !has_spawn(program), name + ": a program without tasks has no poll");
        rename_file(program, fs::canonical(golden.source).string(), "nir-versions/" + name + ".nra");
        const auto written = write_nir(program);
        check(written == expected, name + ": the current writer's bytes differ from the previous writer's (" +
                                       std::to_string(written.size()) + " and " + std::to_string(expected.size()) +
                                       " bytes)");
        // Running the poll pass on it changes nothing, since there is no spawn.
        insert_polls(program);
        check(write_nir(program) == expected, name + ": insert_polls changed a program without tasks");
        std::uint16_t minor = 99;
        const auto read = read_nir(expected, name, &minor);
        check_equal(static_cast<int>(minor), golden.minor, name + ": minor version read");
        check(write_nir(read) == expected, name + ": the old file does not rewrite to itself");
    }
}

// The standard library's declarations carry tags, and a tag is a NeuraIR 1.10
// attribute, so a program that imports the library is written as 1.10. These
// tests are about what tasks alone require, so they drop the attributes.
IrProgram without_attributes(IrProgram program) {
    for (auto& symbol : program.symbols) symbol.attributes.clear();
    return program;
}

NEURA_TEST(nir, programs_with_tasks_are_written_as_1_9_and_keep_their_polls) {
    std::size_t polls = 0;
    std::size_t spawns = 0;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        const auto program = without_attributes(lower_files({fixture.path}));
        check(has_spawn(program), fixture.name + ": the fixture spawns");
        const auto bytes = write_nir(program);
        check_equal(static_cast<int>(bytes[4]) + 256 * static_cast<int>(bytes[5]), 1, fixture.name + " major version");
        check_equal(static_cast<int>(bytes[6]), 9, fixture.name + " is NeuraIR 1.9");
        std::uint16_t minor = 0;
        const auto read = read_nir(bytes, fixture.name, &minor);
        check_equal(static_cast<int>(minor), 9, fixture.name + " minor version read");
        std::size_t before = 0;
        std::size_t after = 0;
        for_each_node(program, [&](const IrNode& item) {
            before += item.poll ? 1 : 0;
            spawns += item.op == IrOp::Spawn ? 1 : 0;
        });
        for_each_node(read, [&](const IrNode& item) { after += item.poll ? 1 : 0; });
        check(before > 0, fixture.name + ": lowering placed no poll");
        check_equal(after, before, fixture.name + " polls after the round trip");
        mismatch.clear();
        check(same_program(program, read), fixture.name + ": " + mismatch);
        polls += before;

        // The poll bit is part of the bytes: clearing every poll of a program
        // with a spawn leaves it 1.9 but changes the file.
        auto unpolled = program;
        const std::function<void(IrNode&)> clear = [&](IrNode& item) {
            item.poll = false;
            for (auto* region : {&item.operands, &item.body, &item.alternative}) {
                for (auto& child : *region) clear(child);
            }
        };
        for (auto& body : unpolled.bodies) {
            for (auto& instruction : body.instructions) clear(instruction);
        }
        const auto without = write_nir(unpolled);
        check_equal(static_cast<int>(without[6]), 9, fixture.name + " without polls is still 1.9");
        check(without != bytes, fixture.name + ": polls are not part of the bytes");
    }
    std::cout << "  " << polls << " polls and " << spawns << " spawns survived the round trip in 1.9 files\n";
}

NEURA_TEST(nir, task_files_are_rejected_when_malformed_or_newer) {
    const auto program = without_attributes(lower_files({environment.fixtures() / "concurrency" / "discard.nra"}));
    const auto bytes = write_nir(program);
    check_equal(static_cast<int>(bytes[6]), 9, "discard.nir is NeuraIR 1.9");

    // A poll bit on an operation that is neither a loop nor a call.
    IrProgram misplaced;
    misplaced.symbols.push_back(IrSymbol{SymbolKind::Function, "f", {}, no_symbol, no_symbol, no_symbol, {}, {}, {}, {},
                                         false, false, false, true});
    IrNode literal{};
    literal.op = IrOp::Boolean;
    literal.poll = true;
    misplaced.bodies.push_back({0, {literal}});
    const auto misplaced_bytes = write_nir(misplaced);
    check_equal(static_cast<int>(misplaced_bytes[6]), 9, "a poll makes the file 1.9");
    expect_nir_error(misplaced_bytes, "a poll marks only a loop or a call");
    // A poll bit on a spawn, a scope, or a scheduler call is misplaced as well.
    for (const auto op : {IrOp::Spawn, IrOp::TaskScope, IrOp::SchedulerCall}) {
        auto other = misplaced;
        other.bodies.front().instructions.front().op = op;
        expect_nir_error(write_nir(other), "a poll marks only a loop or a call");
    }
    // The same node without the poll bit is readable.
    auto clean = misplaced;
    clean.bodies.front().instructions.front().poll = false;
    check_equal(read_nir(write_nir(clean), "clean").bodies.size(), std::size_t{1}, "a plain node reads");

    // Minor 11 is newer than this reader.
    auto newer = bytes;
    newer[6] = 11;
    expect_nir_error(newer, "NeuraIR format 1.11 is not supported; this reader accepts 1.0 through 1.10");
    newer[6] = 200;
    expect_nir_error(newer, "this reader accepts 1.0 through 1.10");

    // An operation tag above the newest one, in a 1.9 file.
    IrProgram above;
    above.symbols = misplaced.symbols;
    IrNode tagged{};
    tagged.op = static_cast<IrOp>(static_cast<std::uint8_t>(IrOp::SchedulerCall) + 1);
    above.bodies.push_back({0, {tagged}});
    const auto above_bytes = write_nir(above);
    check_equal(static_cast<int>(above_bytes[6]), 9, "an operation above the 1.7 set is written as 1.9");
    expect_nir_error(above_bytes, "invalid operation tag " + std::to_string(static_cast<int>(tagged.op)));
    // The newest operation itself is accepted.
    IrProgram newest = above;
    newest.bodies.front().instructions.front().op = IrOp::SchedulerCall;
    check_equal(read_nir(write_nir(newest), "newest").bodies.size(), std::size_t{1}, "SchedulerCall reads");

    // A file that claims an older minor cannot use the task operations: 1.8
    // has neither the tags nor the poll bit.
    auto older = bytes;
    older[6] = 8;
    try {
        read_nir(older, "older.nir");
        fail("a 1.8 header on a file with task operations was accepted");
    } catch (const NirError&) {
    }
}

NEURA_TEST(nir, linking_coalesces_shared_definitions_and_preserves_metadata) {
    const auto shared = R"(
        export let Shared = module (
            export let Numeric = class (
                export static const Width: int = 16;
                export let.as int = func -> int => 19;
                export let init = func {}
                    or func(value: int) {};
            );
            export let echo = func(x: int) -> int => x
                or func(x: string) -> string => x;
            export let count = func(values: ...int) -> int => values.length;
            export let Counter = singleton ();
        );
    )";
    const auto make_unit = [&](std::string file, std::string_view source) {
        std::vector<ParsedFile> parsed;
        auto main = parse(lex(source, file));
        main.imports.push_back({"Shared", "shared.nra"});
        parsed.push_back(std::move(main));
        parsed.push_back(parse(lex(shared, "shared.nra")));
        return lower(parsed);
    };
    auto first = make_unit("left.nra", "let shared = import:Shared; let x = new shared.Numeric();");
    auto second = make_unit("right.nra", R"(
        let shared = import:Shared;
        let Alpha = class ();
        let Beta = class ();
        let y = shared.echo(8);
    )");
    IrProgram merged;
    const Span site{"consumer.nra"};
    const auto left = link_ir(merged, read_nir(write_nir(first), "left.nir"), site);
    const auto right = link_ir(merged, read_nir(write_nir(second), "right.nir"), site);
    require_valid_ir(merged, "after merging test modules");
    const auto find = [](const IrProgram& program, std::string_view name) {
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].name == name) return id;
        }
        return no_symbol;
    };
    for (const auto* name : {"shared.nra", "Shared", "Numeric", "Width", "$class", "Counter", "echo", "count"}) {
        check_equal(left.at(find(first, name)), right.at(find(second, name)), std::string("shared ") + name);
    }
    for (SymbolId id = 0; id < second.symbols.size(); ++id) {
        const auto& before = second.symbols[id];
        const auto& after = merged.symbols[right[id]];
        if (before.overload_group != no_symbol)
            check_equal(after.overload_group, right[before.overload_group], "overload group link");
        if (before.singleton_instance != no_symbol)
            check_equal(after.singleton_instance, right[before.singleton_instance], "singleton instance link");
        check(before.variadic == after.variadic && before.user_cast == after.user_cast,
              "linking changes function signature metadata");
    }
    check_equal(merged.static_constants.size(), std::size_t{1}, "the shared constant is stored once");
    check_equal(merged.static_constants.front().field, left[find(first, "Width")], "static constant field link");
    check_equal(merged.static_constants.front().value.text, std::string{"16"}, "static constant value");
    check(write_nir(read_nir(write_nir(merged), "merged.nir")) == write_nir(merged),
          "linked metadata must survive a NeuraIR round trip");
}

NEURA_TEST(nir, linking_rejects_conflicting_embedded_code) {
    const auto unit = [](std::string_view text, std::string path) {
        std::vector<ParsedFile> files;
        auto source = parse(lex(text, path));
        source.library_path = "std/core/sample.nra";
        files.push_back(std::move(source));
        return lower(files);
    };
    auto trusted = unit("export let Sample = module (export let first = func(values: ...int) -> int => 1;);",
                        "trusted/stdlib/std/core/sample.nra");
    auto embedded = unit("export let Sample = module (export let first = func(values: ...int) -> int => 1;);",
                         "old/stdlib/std/core/sample.nra");
    for (auto& symbol : embedded.symbols) symbol.variadic = VariadicKind::None;
    const Span site{"consumer.nra", 3, 16};
    const auto before = write_nir(trusted);
    link_ir(trusted, std::move(embedded), site, true);
    check(before == write_nir(trusted), "embedded library files must use the installed definitions");
    auto conflict = unit("export let Sample = module (export let first = func(values: ...int) -> int => 2;);",
                         "old/stdlib/std/core/sample.nra");
    try {
        link_ir(trusted, std::move(conflict), site);
        fail("the conflicting embedded function was accepted");
    } catch (const Diagnostic& error) {
        check(same(error.span, site, "import site"), "diagnostic should point to the source import");
        check(std::string(error.what()).find("incompatible definitions") != std::string::npos, error.what());
        check(before == write_nir(trusted), "failed linkage changed the destination");
    }
    auto cyclic = unit("export let Sample = module (export let first = func(values: ...int) -> int => 1;);",
                       "old/stdlib/std/core/sample.nra");
    for (SymbolId id = 0; id < cyclic.symbols.size(); ++id) {
        if (cyclic.symbols[id].name == "first") cyclic.symbols[id].overrides = id;
    }
    try {
        link_ir(trusted, std::move(cyclic), site);
        fail("a cyclic override was accepted");
    } catch (const Diagnostic& error) {
        check(std::string(error.what()).find("cyclic owner, base, or override chain") != std::string::npos,
              error.what());
    }
    auto spoofed = unit("let NotStandard = module ();", "trusted/stdlib/std/core/sample.nra");
    spoofed.symbols.front().library_path.clear();
    try {
        link_ir(trusted, std::move(spoofed), site);
        fail("an untrusted file spoofed the installed library path");
    } catch (const Diagnostic& error) {
        check(std::string(error.what()).find("file identity") != std::string::npos, error.what());
    }
}

// ---- NeuraIR 1.10: symbol attributes and deferred initialization ----------
//
// No source construct produces either yet, so these tests attach every
// pre-registered attribute to the declarations of nir-versions/attributes.nra
// and turn two of its locals into deferred `last` bindings, as lowering of
// `last name: T; name = value;` will.

namespace {

SymbolId find_local(const IrProgram& program, SymbolId owner, std::string_view name) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if ((item.kind == SymbolKind::Local || item.kind == SymbolKind::Parameter) && item.owner == owner &&
            item.name == name)
            return id;
    }
    fail("no local or parameter named " + std::string(name));
}

IrNode load_of(const IrProgram& program, SymbolId id, const Span& span) {
    IrNode node{};
    node.op = IrOp::Load;
    node.type = program.symbols[id].type;
    node.span = span;
    node.symbol = id;
    return node;
}

// Rewrites `let name = value;` in `region` to a deferred declaration followed
// by `name = value;`.
bool defer_in(const IrProgram& program, std::vector<IrNode>& region, SymbolId local) {
    for (std::size_t i = 0; i < region.size(); ++i) {
        auto& node = region[i];
        if (node.op == IrOp::Initialize && node.symbol == local && node.operands.size() == 1) {
            IrNode store{};
            store.op = IrOp::Store;
            store.type = node.type;
            store.span = node.span;
            store.operands.push_back(load_of(program, local, node.span));
            store.operands.push_back(std::move(node.operands.front()));
            node.operands.clear();
            region.insert(region.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(store));
            return true;
        }
        for (auto* nested : {&node.body, &node.alternative}) {
            if (defer_in(program, *nested, local)) return true;
        }
    }
    return false;
}

// Removes the first store to `local` in `region`, at any depth.
bool remove_store(std::vector<IrNode>& region, SymbolId local) {
    for (auto it = region.begin(); it != region.end(); ++it) {
        if (it->op == IrOp::Store && it->operands.front().op == IrOp::Load && it->operands.front().symbol == local) {
            region.erase(it);
            return true;
        }
        for (auto* nested : {&it->body, &it->alternative}) {
            if (remove_store(*nested, local)) return true;
        }
    }
    return false;
}

struct Attributed {
    IrProgram program;
    SymbolId point, x, get_x, set_x, box, value, peek, checksum, sum_to, main, step, point_local, parameter;
};

const std::string attributed_output = "x = 4\nbox = 7\nsum_to(10) = 55\n";

Attributed attributed_fixture(const Environment& environment, bool attributes = true, bool deferred = true) {
    Attributed a;
    a.program = lower_files({backend_dir(environment) / "nir-versions" / "attributes.nra"});
    auto& p = a.program;
    a.point = find_symbol(p, "Attributes.Point");
    a.x = find_symbol(p, "Attributes.Point.x");
    a.get_x = find_symbol(p, "Attributes.Point.get_x");
    a.set_x = find_symbol(p, "Attributes.Point.set_x");
    a.box = find_symbol(p, "Attributes.Box");
    a.value = find_symbol(p, "Attributes.Box.value");
    a.peek = find_symbol(p, "Attributes.Box.peek");
    a.checksum = find_symbol(p, "Attributes.checksum");
    a.sum_to = find_symbol(p, "Attributes.sum_to");
    a.main = find_symbol(p, "Attributes.main");
    a.step = find_local(p, a.sum_to, "step");
    a.point_local = find_local(p, a.main, "point");
    a.parameter = find_local(p, a.checksum, "value");
    auto& s = p.symbols;
    if (attributes) {
        const Span tag_span{s[a.point].span.file, 7, 5, 120, 11};
        add_attribute(s[a.point], AttributeId::Tag,
                      tag_payload(1, "Deprecated", {{"as_of", attribute_text("0.2.0")}, {"", attribute_bool(true)}},
                                  tag_span));
        add_attribute(s[a.point], AttributeId::Tag,
                      tag_payload(12, "Since", {{"", attribute_unsigned(2)}, {"delta", attribute_signed(-5)}}, tag_span));
        set_attribute(s[a.point], AttributeId::ClassAnonymous,
                      anonymous_class_payload(true, "class(x: int32)", s[a.point].span));
        set_attribute(s[a.point], AttributeId::ClassShareable, attribute_bool(true));
        set_attribute(s[a.x], AttributeId::AccessorGet, attribute_symbol(a.get_x));
        set_attribute(s[a.x], AttributeId::AccessorSet, attribute_symbol(a.set_x));
        set_attribute(s[a.x], AttributeId::BindingLast, attribute_bool(false));
        set_attribute(s[a.get_x], AttributeId::AccessorOf, accessor_of_payload(a.x, AccessorKind::Get));
        set_attribute(s[a.get_x], AttributeId::InlineDirective,
                      attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Always)));
        set_attribute(s[a.set_x], AttributeId::AccessorOf, accessor_of_payload(a.x, AccessorKind::Set));
        set_attribute(s[a.set_x], AttributeId::Synchronized, attribute_bool(true));
        // peek is never called, so specialization copies it only for the link.
        set_attribute(s[a.value], AttributeId::AccessorGet, attribute_symbol(a.peek));
        set_attribute(s[a.peek], AttributeId::AccessorOf, accessor_of_payload(a.value, AccessorKind::Get));
        set_attribute(s[a.checksum], AttributeId::KernelBinding, kernel_binding_payload("neura.test.checksum", 1));
        set_attribute(s[a.checksum], AttributeId::InlineDirective,
                      attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Never)));
        add_attribute(s[a.parameter], AttributeId::Tag, tag_payload(3, "Hidden", {}, s[a.parameter].span));
    }
    if (deferred) {
        for (const auto local : {a.step, a.point_local}) {
            set_attribute(s[local], AttributeId::BindingLast, attribute_bool(true));
            s[local].constant = true;
        }
        check(defer_in(p, body_of(p, "Attributes.sum_to"), a.step), "step is declared in sum_to");
        check(defer_in(p, body_of(p, "Attributes.main"), a.point_local), "point is declared in main");
    }
    return a;
}

std::vector<std::uint8_t> image_bytes(IrProgram program) {
    return nvr::encode(backend::build(std::move(program)).image);
}

Captured run_program(IrProgram program, bool optimize = true) {
    backend::BuildOptions options;
    options.optimize = optimize;
    return run_captured(nvr::load(nvr::encode(backend::build(std::move(program), options).image)));
}

void expect_issue(const IrProgram& program, std::string_view fragment,
                  std::source_location where = std::source_location::current()) {
    const auto issues = verify_ir(program);
    std::string found;
    for (const auto& issue : issues) {
        if (issue.message.find(fragment) != std::string::npos) return;
        found += "\n  " + issue.message;
    }
    fail("expected a verifier issue containing '" + std::string(fragment) + "', found:" + found, where);
}

// One function symbol and nothing else: the smallest file that can carry
// attributes, for byte-level corruption checks.
IrProgram attributed_function(std::vector<IrAttribute> attributes) {
    IrProgram program;
    program.symbols.push_back(IrSymbol{SymbolKind::Function, "f", {}, no_symbol, no_symbol, no_symbol, {}, {}, {}, {},
                                       false, false, false, false});
    program.symbols.front().attributes = std::move(attributes);
    return program;
}

} // namespace

NEURA_TEST(nir, attribute_registry_matches_its_documentation) {
    struct Row {
        AttributeId id;
        std::uint16_t number;
        std::string_view name;
        std::uint32_t kinds;
        bool repeats;
        std::string_view schema;
    };
    // The table of docs/NirFormat.md, Revision 1.10.
    const std::vector<Row> rows{
        {AttributeId::Tag, 1, "tag", on_declarations, true, "[u65535 t *[t a] p]"},
        {AttributeId::AccessorGet, 2, "accessor.get", on_field, false, "s"},
        {AttributeId::AccessorSet, 3, "accessor.set", on_field, false, "s"},
        {AttributeId::AccessorOf, 4, "accessor.of", on_function, false, "[s u1]"},
        {AttributeId::BindingLast, 5, "binding.last", on_local | on_field, false, "b"},
        {AttributeId::ClassAnonymous, 6, "class.anonymous", on_class, false, "[b t p]"},
        {AttributeId::KernelBinding, 7, "kernel.binding", on_function, false, "[t u4294967295]"},
        {AttributeId::InlineDirective, 8, "inline.directive", on_function, false, "u2"},
        {AttributeId::Synchronized, 9, "synchronized", on_function, false, "b"},
        {AttributeId::ClassShareable, 10, "class.shareable", on_class, false, "b"},
    };
    check_equal(attribute_registry().size(), rows.size(), "registered attributes");
    for (const auto& row : rows) {
        const auto* info = find_attribute_info(row.number);
        check(info != nullptr && info->id == row.id && info->name == row.name && info->kinds == row.kinds &&
                  info->repeats == row.repeats && info->schema == row.schema,
              std::string(row.name) + " differs from the documented registry");
    }
    check(find_attribute_info(0) == nullptr && find_attribute_info(11) == nullptr, "ids 0 and 11 are unregistered");
    check((on_declarations & symbol_kind_bit(SymbolKind::File)) == 0, "files carry no tags");

    // Schemas accept the payloads of the helpers and reject other shapes.
    check(!schema_problem("[u65535 t *[t a] p]", tag_payload(65535, "T", {{"", attribute_signed(-1)}}, {})),
          "a tag payload matches its schema");
    check(!schema_problem("[b t p]", anonymous_class_payload(false, "", {})), "an anonymous class payload matches");
    check(!schema_problem("[t u4294967295]", kernel_binding_payload("k", 4294967295U)), "a kernel payload matches");
    const auto problem = [](std::string_view schema, const IrAttributeValue& value) {
        return schema_problem(schema, value).value_or("");
    };
    check_equal(problem("u2", attribute_unsigned(3)), std::string("the payload is 3, above its maximum 2"), "range");
    check_equal(problem("b", attribute_text("x")), std::string("the payload is a text, expected bool"), "kind");
    check_equal(problem("[s u1]", attribute_list({attribute_symbol(1)})),
                std::string("the payload has 1 item(s), expected more"), "record length");
    check_equal(problem("*[t a]", attribute_list({attribute_list({attribute_text(""), attribute_type({})})})),
                std::string("the payload item 1 item 2 is a type, expected literal"), "nested literal");
    check_equal(problem("s", attribute_symbol(no_symbol)), std::string("the payload is an absent symbol reference"),
                "absent symbol");
    check(problem("q", attribute_bool(true)).starts_with("invalid attribute schema"), "unknown schema letter");
}

NEURA_TEST(nir, symbol_attributes_and_deferred_initialization_round_trip_in_1_10) {
    const auto fixture = attributed_fixture(environment);
    const auto& program = fixture.program;
    const auto issues = verify_ir(program);
    check(issues.empty(), issues.empty() ? "" : issues.front().message);
    // Every pre-registered attribute is exercised.
    for (const auto& info : attribute_registry()) {
        check(std::ranges::any_of(program.symbols, [&](const IrSymbol& item) { return has_attribute(item, info.id); }),
              std::string(info.name) + " is not attached by the fixture");
    }
    const auto bytes = write_nir(program);
    check_equal(static_cast<int>(bytes[6]), 10, "an attributed program is NeuraIR 1.10");
    std::uint16_t minor = 0;
    const auto read = read_nir(bytes, "attributes.nir", &minor);
    check_equal(static_cast<int>(minor), 10, "minor version read");
    mismatch.clear();
    check(same_program(program, read), mismatch);
    check(write_nir(read) == bytes, "rewriting the read program changes its bytes");
    check(printed(read) == printed(program), "listing differs after the round trip");
    check_equal(find_attributes(read.symbols[fixture.point], AttributeId::Tag).size(), std::size_t{2},
                "repeated tags keep their count");
    check(find_attributes(read.symbols[fixture.point], AttributeId::Tag)[1]->value.items[1].text == "Since",
          "tags keep their order");
    check(deferred_binding(read.symbols[fixture.step]) && last_binding(read.symbols[fixture.x]) == false,
          "binding.last keeps its deferred flag");
    check(inline_directive(read.symbols[fixture.checksum]) == InlineDirective::Never &&
              inline_directive(read.symbols[fixture.sum_to]) == InlineDirective::Auto,
          "inline directives read back");

    // The listing names each attribute and marks the deferred declarations.
    const auto listing = printed(read);
    const auto id = [](SymbolId symbol) { return "%" + std::to_string(symbol); };
    for (const auto& fragment : {
             " @accessor.get(" + id(fixture.get_x) + ") @accessor.set(" + id(fixture.set_x) + ") @binding.last(false)",
             " @accessor.of([" + id(fixture.x) + ", 0u]) @inline.directive(1u)",
             std::string(" @tag([1u, \"Deprecated\", [[\"as_of\", \"0.2.0\"], [\"\", true]], span("),
             std::string("[[\"\", 2u], [\"delta\", -5i]]"),
             std::string(" @kernel.binding([\"neura.test.checksum\", 1u]) @inline.directive(2u)"),
             std::string(" @synchronized(true)"), std::string(" @class.shareable(true)"),
             std::string(" @class.anonymous([true, \"class(x: int32)\", span("),
             "initialize " + id(fixture.step) + " deferred : void"}) {
        check(listing.find(fragment) != std::string::npos, "the listing lacks: " + fragment);
    }

    // Each half alone also needs 1.10.
    check_equal(static_cast<int>(write_nir(attributed_fixture(environment, true, false).program)[6]), 10,
                "attributes alone make a file 1.10");
    check_equal(static_cast<int>(write_nir(attributed_fixture(environment, false, true).program)[6]), 10,
                "deferred initialization alone makes a file 1.10");
    check(static_cast<int>(write_nir(without_attributes(attributed_fixture(environment, false, false).program))[6]) < 10,
          "the fixture without either keeps an earlier version");
}

NEURA_TEST(nir, attributed_programs_build_the_same_image_from_nir) {
    auto fixture = attributed_fixture(environment);
    const auto direct = image_bytes(fixture.program);
    const auto routed = image_bytes(read_nir(write_nir(fixture.program), "attributes.nir"));
    check(direct == routed, "the source and .nir routes emit different images");
    // Attributes describe declarations; none of the current ones reaches the
    // image, so attaching them changes no byte.
    check(image_bytes(attributed_fixture(environment, true, false).program) ==
              image_bytes(attributed_fixture(environment, false, false).program),
          "attributes changed the image");
    // Deferred declarations run like the declarations they replace, with and
    // without optimization.
    for (const bool optimize : {true, false}) {
        const auto run = run_program(attributed_fixture(environment).program, optimize);
        check(run.result.completed, "the deferred program trapped: " + run.result.trap.detail);
        check_equal(run.output, attributed_output, "output with deferred bindings");
    }
}

NEURA_TEST(nir, deferred_bindings_trap_when_read_before_their_store) {
    // The verifier checks structure only. Definite assignment is the
    // frontend's job, and the runtime traps on a read that slips through.
    auto fixture = attributed_fixture(environment, false, true);
    check(remove_store(body_of(fixture.program, "Attributes.sum_to"), fixture.step), "step has a store");
    check(verify_ir(fixture.program).empty(), "the verifier does not track definite assignment");
    for (const bool optimize : {true, false}) {
        const auto run = run_program(fixture.program, optimize);
        check(!run.result.completed && run.result.trap.kind == nvm::TrapKind::UninitializedSlot,
              "expected an uninitialized slot trap, got: " + run.output);
        check_equal(run.output, std::string("x = 4\nbox = 7\n"), "output before the trap");
    }
}

NEURA_TEST(nir, verifier_checks_attributes_and_deferred_initialization) {
    const auto base = attributed_fixture(environment);
    check(verify_ir(base.program).empty(), "the fixture verifies");
    auto program = base.program;
    // A deferred declaration needs binding.last with deferred set.
    remove_attributes(program.symbols[base.step], AttributeId::BindingLast);
    expect_issue(program, "needs binding.last with deferred set");
    program = base.program;
    set_attribute(program.symbols[base.step], AttributeId::BindingLast, attribute_bool(false));
    expect_issue(program, "needs binding.last with deferred set");
    // A deferred binding has no value at its declaration.
    program = attributed_fixture(environment, false, false).program;
    set_attribute(program.symbols[base.step], AttributeId::BindingLast, attribute_bool(true));
    expect_issue(program, "is deferred but initialized with a value");
    // Only a local is declared without a value; a deferred field has no
    // initialization at all.
    program = base.program;
    for (auto& body : program.bodies) {
        if (body.owner != base.point) continue;
        for (auto& instruction : body.instructions)
            if (instruction.op == IrOp::Initialize && instruction.symbol == base.x) instruction.operands.clear();
    }
    set_attribute(program.symbols[base.x], AttributeId::BindingLast, attribute_bool(true));
    expect_issue(program, "a deferred initialization declares only a local");
    // A constant binding without the deferred flag still rejects stores.
    program = attributed_fixture(environment, false, false).program;
    program.symbols[base.step].constant = true;
    check(defer_in(program, body_of(program, "Attributes.sum_to"), base.step), "step is declared in sum_to");
    program.symbols[base.step].attributes = {{AttributeId::BindingLast, attribute_bool(false)}};
    expect_issue(program, "store writes to const");

    // Registry rules.
    program = base.program;
    set_attribute(program.symbols[base.checksum], AttributeId::BindingLast, attribute_bool(true));
    expect_issue(program, "attribute binding.last is not allowed on this kind of symbol");
    program = base.program;
    add_attribute(program.symbols[base.checksum], AttributeId::InlineDirective, attribute_unsigned(0));
    expect_issue(program, "attribute inline.directive is repeated");
    program = base.program;
    set_attribute(program.symbols[base.checksum], AttributeId::InlineDirective, attribute_text("always"));
    expect_issue(program, "attribute inline.directive: the payload is a text, expected unsigned integer");
    program = base.program;
    program.symbols[base.checksum].attributes.push_back({static_cast<AttributeId>(999), attribute_bool(true)});
    expect_issue(program, "unknown symbol attribute 999");
    program = base.program;
    set_attribute(program.symbols[base.x], AttributeId::AccessorGet, attribute_symbol(program.symbols.size() + 4));
    expect_issue(program, "refers to missing symbol");
    // Links between a field and its accessors.
    program = base.program;
    set_attribute(program.symbols[base.x], AttributeId::AccessorGet, attribute_symbol(base.set_x));
    expect_issue(program, "as an accessor that does not serve it");
    program = base.program;
    remove_attributes(program.symbols[base.x], AttributeId::AccessorSet);
    expect_issue(program, "which does not name it");
    program = base.program;
    set_attribute(program.symbols[base.checksum], AttributeId::Synchronized, attribute_bool(true));
    expect_issue(program, "synchronized marks a method of a class");
    program = base.program;
    set_attribute(program.symbols[base.checksum], AttributeId::KernelBinding, kernel_binding_payload("", 1));
    expect_issue(program, "needs a kernel name");
}

NEURA_TEST(nir, specialization_links_the_accessors_of_generic_instances) {
    auto fixture = attributed_fixture(environment);
    auto& program = fixture.program;
    materialize_reference_storage(program);
    specialize(program);
    const auto issues = verify_ir(program);
    check(issues.empty(), issues.empty() ? "" : issues.front().message);
    const auto instance = std::ranges::find_if(program.symbols, [](const IrSymbol& item) {
        return item.kind == SymbolKind::Class && item.name == "Box<int32>";
    });
    check(instance != program.symbols.end(), "Box<int32> is instantiated");
    const auto instance_id = static_cast<SymbolId>(instance - program.symbols.begin());
    const auto member = [&](std::string_view name) {
        for (SymbolId id = 0; id < program.symbols.size(); ++id)
            if (program.symbols[id].owner == instance_id && program.symbols[id].name == name) return id;
        fail("Box<int32> has no member " + std::string(name));
    };
    const auto value = member("value");
    const auto peek = member("peek");
    const auto* get = find_attribute(program.symbols[value], AttributeId::AccessorGet);
    check(get && get->value.symbol == peek, "the instance field names the instance's accessor");
    const auto* of = find_attribute(program.symbols[peek], AttributeId::AccessorOf);
    check(of && of->value.items[0].symbol == value, "the instance accessor names the instance field");
    check(deferred_binding(program.symbols[fixture.step]), "the local keeps binding.last");
}

NEURA_TEST(nir, linking_remaps_attribute_references) {
    const auto fixture = attributed_fixture(environment);
    // Something first, so that the module's own symbols move.
    IrProgram merged = lower_files({backend_dir(environment) / "nir-versions" / "loops.nra"});
    const Span site{"consumer.nra"};
    const auto mapping = link_ir(merged, read_nir(write_nir(fixture.program), "attributes.nir"), site);
    require_valid_ir(merged, "after linking an attributed module");
    check(mapping[fixture.x] != fixture.x, "the linked module's symbols moved");
    const auto& x = merged.symbols[mapping[fixture.x]];
    check(find_attribute(x, AttributeId::AccessorGet)->value.symbol == mapping[fixture.get_x],
          "accessor.get follows the getter");
    check(find_attribute(merged.symbols[mapping[fixture.set_x]], AttributeId::AccessorOf)->value.items[0].symbol ==
              mapping[fixture.x],
          "accessor.of follows the field");
    // The same module again coalesces, since its attributes are equal.
    const auto before = write_nir(merged);
    const auto again = link_ir(merged, read_nir(write_nir(fixture.program), "attributes.nir"), site);
    check(again == mapping && write_nir(merged) == before, "linking the module twice adds nothing");
    // A copy whose attributes differ is a conflicting definition.
    auto changed = fixture.program;
    set_attribute(changed.symbols[fixture.checksum], AttributeId::InlineDirective, attribute_unsigned(1));
    try {
        link_ir(merged, std::move(changed), site);
        fail("a definition with different attributes was coalesced");
    } catch (const Diagnostic& error) {
        check(std::string(error.what()).find("incompatible definitions") != std::string::npos, error.what());
    }
}

NEURA_TEST(nir, attribute_files_are_rejected_when_malformed) {
    const auto valid = attributed_function(
        {{AttributeId::KernelBinding, kernel_binding_payload("neura.test", 2)},
         {AttributeId::Tag, tag_payload(4, "Pure", {{"", attribute_signed(-3)}}, {})},
         {AttributeId::InlineDirective, attribute_unsigned(1)}});
    const auto bytes = write_nir(valid);
    check_equal(static_cast<int>(bytes[6]), 10, "an attributed function is NeuraIR 1.10");
    mismatch.clear();
    check(same_program(read_nir(bytes, "valid"), valid), mismatch);
    // A truncated attribute list.
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        try {
            read_nir(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size)), "x");
            fail("a " + std::to_string(size) + "-byte prefix was accepted");
        } catch (const NirError&) {
        }
    }
    // A 1.9 header on a file with attributes.
    auto older = bytes;
    older[6] = 9;
    expect_nir_error(older, "symbol attributes require NeuraIR format 1.10");

    // Registry rules, with the bytes the writer produces.
    expect_nir_error(write_nir(attributed_function({{static_cast<AttributeId>(999), attribute_bool(true)}})),
                     "unknown symbol attribute 999");
    expect_nir_error(write_nir(attributed_function({{AttributeId::InlineDirective, attribute_text("never")}})),
                     "attribute inline.directive: the payload is a text, expected unsigned integer");
    expect_nir_error(write_nir(attributed_function({{AttributeId::InlineDirective, attribute_unsigned(3)}})),
                     "the payload is 3, above its maximum 2");
    expect_nir_error(write_nir(attributed_function({{AttributeId::BindingLast, attribute_bool(true)}})),
                     "attribute binding.last is not allowed on this kind of symbol");
    expect_nir_error(write_nir(attributed_function({{AttributeId::Synchronized, attribute_bool(true)},
                                                    {AttributeId::Synchronized, attribute_bool(true)}})),
                     "attribute synchronized is repeated");
    // A type decodes, then fails the tag's literal schema.
    expect_nir_error(
        write_nir(attributed_function(
            {{AttributeId::Tag, tag_payload(4, "Pure", {{"", attribute_type({TypeKind::Bool, "bool", no_symbol, {}})}}, {})}})),
        "item 3 item 1 item 2 is a type, expected literal");
    // Symbol payloads are references into the table.
    auto field = attributed_function({});
    field.symbols.front().kind = SymbolKind::Field;
    field.symbols.front().attributes = {{AttributeId::AccessorGet, attribute_symbol(no_symbol)}};
    expect_nir_error(write_nir(field), "missing symbol reference");
    field.symbols.front().attributes = {{AttributeId::AccessorGet, attribute_symbol(7)}};
    expect_nir_error(write_nir(field), "symbol reference 7 is out of range");

    // Value bytes: one local with binding.last(true) ends in its value kind
    // and bool, then the empty body and constant vectors and the native flag.
    auto local = attributed_function({{AttributeId::BindingLast, attribute_bool(true)}});
    local.symbols.front().kind = SymbolKind::Local;
    auto value_bytes = write_nir(local);
    const auto bool_byte = value_bytes.size() - 10;
    check_equal(static_cast<int>(value_bytes[bool_byte]), 1, "the bool payload byte");
    check_equal(static_cast<int>(value_bytes[bool_byte - 1]), static_cast<int>(AttributeValueKind::Bool),
                "the value kind byte");
    value_bytes[bool_byte] = 2;
    expect_nir_error(value_bytes, "invalid attribute bool 2");
    value_bytes[bool_byte] = 1;
    value_bytes[bool_byte - 1] = 8;
    expect_nir_error(value_bytes, "invalid attribute value tag 8");
    value_bytes[bool_byte - 1] = static_cast<std::uint8_t>(AttributeValueKind::Unsigned);
    expect_nir_error(value_bytes, "attribute binding.last: the payload is an unsigned integer, expected bool");

    // Deferred initialization is a 1.10 feature too.
    IrProgram deferred;
    deferred.symbols.push_back(IrSymbol{SymbolKind::Function, "f", {}, no_symbol, no_symbol, no_symbol, {}, {}, {},
                                        {}, false, false, false, true});
    IrNode declaration{};
    declaration.op = IrOp::Initialize;
    deferred.bodies.push_back({0, {declaration}});
    auto deferred_bytes = write_nir(deferred);
    check_equal(static_cast<int>(deferred_bytes[6]), 10, "a deferred initialization is NeuraIR 1.10");
    check(is_deferred_initialize(read_nir(deferred_bytes, "deferred").bodies.front().instructions.front()),
          "a deferred initialization reads back");
    deferred_bytes[6] = 9;
    expect_nir_error(deferred_bytes, "deferred initialization requires NeuraIR format 1.10");
}

} // namespace neura::testing

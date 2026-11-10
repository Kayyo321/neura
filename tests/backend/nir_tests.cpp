// The versioned .nir file: exact round trips and rejection of corrupt files.

#include "fixtures.hpp"

#include "ir-optimization/concurrency_support.hpp"
#include "lexer/lexer.hpp"
#include "neura-ir/linker.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/verifier.hpp"
#include "nvr/format.hpp"
#include "parser/syntax.hpp"

#include "backend/converter.hpp"

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

NEURA_TEST(nir, programs_with_tasks_are_written_as_1_9_and_keep_their_polls) {
    std::size_t polls = 0;
    std::size_t spawns = 0;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        const auto program = lower_files({fixture.path});
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
    const auto program = lower_files({environment.fixtures() / "concurrency" / "discard.nra"});
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

    // Minor 10 is newer than this reader.
    auto newer = bytes;
    newer[6] = 10;
    expect_nir_error(newer, "NeuraIR format 1.10 is not supported; this reader accepts 1.0 through 1.9");
    newer[6] = 200;
    expect_nir_error(newer, "this reader accepts 1.0 through 1.9");

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
                    or func(value: int) {}
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

} // namespace neura::testing

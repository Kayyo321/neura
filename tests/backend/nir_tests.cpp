// The versioned .nir file: exact round trips and rejection of corrupt files.

#include "fixtures.hpp"

#include "neura-ir/nir_format.hpp"
#include "nvr/format.hpp"

#include "backend/converter.hpp"

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
    if (a.op != b.op || a.symbol != b.symbol || a.text != b.text || a.type_arguments != b.type_arguments) {
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
    if (a.symbols.size() != b.symbols.size() || a.bodies.size() != b.bodies.size()) {
        mismatch = "table sizes differ";
        return false;
    }
    for (std::size_t i = 0; i < a.symbols.size(); ++i) {
        const auto& x = a.symbols[i];
        const auto& y = b.symbols[i];
        const auto where = "symbol " + std::to_string(i);
        if (x.kind != y.kind || x.name != y.name || x.owner != y.owner || x.base != y.base ||
            x.overrides != y.overrides || x.parameters != y.parameters || x.generics != y.generics ||
            x.bound != y.bound || x.exported != y.exported || x.constant != y.constant ||
            x.external != y.external || x.has_body != y.has_body) {
            mismatch = where + " differs";
            return false;
        }
        if (!same(x.type, y.type, where) || !same(x.span, y.span, where)) {
            return false;
        }
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
    return result;
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
    const auto bytes = write_nir(lower_files({environment.examples() / "hello-world" / "hello-world.nra"}));
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
    expect_nir_error(changed, "NeuraIR format 2.0 is not supported");
    changed = bytes;
    changed[6] = 2;
    expect_nir_error(changed, "NeuraIR format 1.2 is not supported");
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
    const auto hello = write_nir(lower_files({environment.examples() / "hello-world" / "hello-world.nra"}));
    check_equal(static_cast<int>(hello[6]), 0, "hello-world.nir is NeuraIR 1.0");
    const auto loops = write_nir(lower_files({program_path(environment, "loops")}));
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
}

} // namespace neura::testing

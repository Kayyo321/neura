// The decompiler. `lossless`: the text form represents every valid image
// exactly, and assembling it reproduces the image's bytes (an exact,
// structural check over the corpus). The lifting and behavior suites follow.

#include "fixtures.hpp"

#include "backend/converter.hpp"
#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"
#include "nvm/trace.hpp"

#include <algorithm>
#include <cstdio>

namespace neura::testing {
namespace {

void expect_assembly_error(std::string_view text, std::string_view fragment, std::size_t line,
                           std::source_location where = std::source_location::current()) {
    try {
        decompiler::assemble(text);
    } catch (const decompiler::AssemblyError& error) {
        check(std::string(error.what()).find(fragment) != std::string::npos,
              "expected '" + std::string(fragment) + "', got: " + error.what(), where);
        check_equal(error.line, line, "error line", where);
        return;
    }
    fail("expected an assembly error: " + std::string(fragment), where);
}

} // namespace

NEURA_TEST(lossless, every_corpus_image_round_trips) {
    std::size_t images = 0;
    for (const auto& item : image_corpus(environment)) {
        const auto image = nvr::load(item.bytes);
        const auto text = decompiler::disassemble(image);
        const auto assembled = decompiler::assemble(text);
        check(assembled == image, item.label + ": the assembled model differs from the loaded one");
        check(nvr::encode(assembled) == item.bytes, item.label + ": assembling the text changes the bytes");
        check_equal(decompiler::disassemble(assembled), text, item.label + ": disassembly is not stable");
        ++images;
    }
    check(images > 150, "the corpus covers the test programs in four variants");
}

NEURA_TEST(lossless, edited_images_round_trip) {
    // An image whose weights, biases, outputs, and exits were edited is still
    // valid, and the text form keeps every edit.
    for (const auto& item : image_corpus(environment)) {
        if (item.label != "programs/computation.nra") {
            continue;
        }
        auto image = nvr::load(item.bytes);
        std::size_t edited = 0;
        for (auto& graph : image.graphs) {
            for (auto& node : graph.nodes) {
                if (!node.edges.empty() && edited < 50) {
                    node.edges.front().weight += 3;
                    node.bias -= 1;
                    ++edited;
                }
            }
        }
        check(edited == 50, "the program has enough neurons to edit");
        const auto bytes = nvr::encode(image);
        const auto loaded = nvr::load(bytes);
        check(decompiler::assemble(decompiler::disassemble(loaded)) == loaded, "edited weights survive");
        check(nvr::encode(decompiler::assemble(decompiler::disassemble(loaded))) == bytes, "edited bytes survive");
        return;
    }
    fail("the corpus has no computation program");
}

NEURA_TEST(lossless, assembly_errors_name_their_line) {
    expect_assembly_error("type 0 void\n", "must start with an 'nvr' header", 1);
    expect_assembly_error("nvr 1.6 profile 1\n", "NVR 1.6 is not supported", 1);
    expect_assembly_error("nvr 1.0 profile 2\n", "only graph profile 1", 1);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\ntype 1 void\n", "type 1 appears where type 0 belongs", 3);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\ntype 0 complex\n", "unknown type kind 'complex'", 3);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\nnode 0 relu 0 [ ]\n", "a node before any graph", 3);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\ngraph 0 inputs 1 outputs [ 0 ]\n  node 0 relu 0 [ 0x1 ]\n",
                          "expected an edge of the form source*weight, found '0x1'", 4);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\nconstant 0 type 0 bytes abc\n", "odd number of digits", 3);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\nimport 0 name version 1\n", "expected a quoted import name", 3);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\nfunction 0 parameters 0 result 0 frame [ ]\n  block 0 graph 0 "
                          "selector 0 inputs [ ]\n    exit jump resume 1 assign [ ]\n",
                          "unknown exit kind 'jump'", 5);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\nwhatever\n", "unknown record 'whatever'", 3);
    expect_assembly_error("nvr 1.0 profile 1\n", "no entry", 1);
    expect_assembly_error("nvr 1.0 profile 1\nentry 0\nglobal 0 type 0 initializer - extra\n", "unexpected 'extra'", 3);
    // Syntax is the assembler's job; the image contract is the loader's.
    const auto text = decompiler::disassemble(hello_model());
    auto broken = text;
    const auto at = broken.find("exit trap reason 3");
    broken.replace(at, std::string("exit trap reason 3").size(), "exit trap reason 9");
    const auto image = decompiler::assemble(broken);
    try {
        nvr::load(nvr::encode(image));
        fail("trap reason 9 should not load");
    } catch (const nvr::LoadError& error) {
        check(std::string(error.what()).find("trap reason 9 is not defined") != std::string::npos, error.what());
    }
}

} // namespace neura::testing

// ---------------------------------------------------------------------------
// Lifting: every claim the decompiler makes is re-checked, coverage is
// measured, edited graphs are not lifted on the strength of stale structure,
// and provenance never changes the lifted content.

namespace neura::testing {
namespace {

struct Totals {
    std::size_t graph_blocks = 0;
    std::size_t lifted = 0;
    std::size_t exhaustive = 0;
    std::size_t circuits = 0;
};

Totals check_claims(const nvr::Image& image, const decompiler::Decompilation& decompilation,
                    const std::string& label) {
    Totals totals;
    for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
        for (std::uint32_t b = 0; b < image.functions[f].blocks.size(); ++b) {
            const auto& lifted = decompilation.blocks[f][b];
            const auto& block = image.functions[f].blocks[b];
            if (lifted.evidence == decompiler::Evidence::None) {
                continue;
            }
            ++totals.graph_blocks;
            if (lifted.evidence == decompiler::Evidence::Circuit) {
                ++totals.circuits;
                continue;
            }
            if (lifted.evidence == decompiler::Evidence::Exhaustive) {
                ++totals.exhaustive;
                continue;
            }
            ++totals.lifted;
            if (!lifted.dispatch.empty()) {
                continue;
            }
            nvr::Graph graph;
            const auto regenerated = decompiler::regenerate(image, f, b, lifted, graph);
            check(regenerated && *regenerated == block && graph == image.graphs[block.graph],
                  label + ": function " + std::to_string(f) + " block " + std::to_string(b) +
                      " does not regenerate from its lifted operations");
        }
    }
    return totals;
}

nvr::Image without_provenance(nvr::Image image) {
    image.source_map.reset();
    image.other_sections.clear();
    return image;
}

} // namespace

NEURA_TEST(lift, claims_hold_and_coverage_is_reported) {
    Totals all;
    for (const auto& item : image_corpus(environment)) {
        if (!item.compiled) {
            continue;
        }
        const auto image = nvr::load(item.bytes);
        const auto decompilation = decompiler::decompile(image);
        const auto totals = check_claims(image, decompilation, item.label);
        all.graph_blocks += totals.graph_blocks;
        all.lifted += totals.lifted;
        all.exhaustive += totals.exhaustive;
        all.circuits += totals.circuits;
    }
    std::printf("  lifted %zu structurally and %zu exhaustively of %zu blocks with graphs; %zu circuits\n",
                all.lifted, all.exhaustive, all.graph_blocks, all.circuits);
    check(all.lifted * 10 >= all.graph_blocks * 9, "at least 90% of the corpus's graph blocks lift structurally");
}

NEURA_TEST(lift, lifted_operations_match_the_source) {
    // Lifted text for known programs names the operations the source wrote.
    const auto image = build_program({backend_dir(environment) / "fixtures" / "shared-multiplier.nra"});
    const auto text = decompiler::decompile(image).text;
    check(text.find(" * ") != std::string::npos, "the shared multiplication is lifted:\n" + text);
    // Built as written: the optimizer inlines `both(true, true)` and folds it,
    // which leaves no conjunction in the image to lift.
    const auto both = build_program({backend_dir(environment) / "fixtures" / "both.nra"}, {}, false);
    const auto both_text = decompiler::decompile(both).text;
    check(both_text.find("&&") != std::string::npos, "the conjunction is lifted:\n" + both_text);
    const auto loops = build_program({program_path(environment, "loops")});
    const auto loops_text = decompiler::decompile(loops).text;
    check(loops_text.find("[reconstructed: loop header") != std::string::npos, "loop headers are reconstructed");
    check(loops_text.find("version(") != std::string::npos, "for-each version reads are decoded");
}

NEURA_TEST(lift, edited_weights_are_not_lifted_from_stale_structure) {
    // Changing one weight in a lifted comparison's graph changes the graph's
    // function. The decompiler must not show the original comparison for it.
    auto image = build_program({program_path(environment, "computation")});
    const auto before = decompiler::decompile(image);
    std::optional<std::pair<std::uint32_t, std::uint32_t>> target;
    for (std::uint32_t f = 0; f < image.functions.size() && !target; ++f) {
        for (std::uint32_t b = 0; b < image.functions[f].blocks.size() && !target; ++b) {
            const auto& lifted = before.blocks[f][b];
            if (lifted.evidence == decompiler::Evidence::Structural && lifted.condition &&
                image.graphs[image.functions[f].blocks[b].graph].nodes.size() == 1) {
                target = std::make_pair(f, b);
            }
        }
    }
    check(target.has_value(), "computation has a lifted single-neuron comparison");
    // Give the block a graph of its own and change the comparison's bias.
    auto& block = image.functions[target->first].blocks[target->second];
    auto graph = image.graphs[block.graph];
    graph.nodes[0].bias += 1;
    block.graph = static_cast<std::uint32_t>(image.graphs.size());
    image.graphs.push_back(graph);
    image = nvr::load(nvr::encode(image));
    const auto after = decompiler::decompile(image);
    const auto& edited = after.blocks[target->first][target->second];
    check(edited.evidence != decompiler::Evidence::Structural ||
              !(edited.condition && before.blocks[target->first][target->second].condition &&
                decompiler::render(image, *edited.condition, {}) ==
                    decompiler::render(image, *before.blocks[target->first][target->second].condition, {})),
          "the edited block is not lifted as the original comparison");
    if (edited.evidence == decompiler::Evidence::Structural) {
        nvr::Graph regenerated;
        const auto rebuilt = decompiler::regenerate(image, target->first, target->second, edited, regenerated);
        check(rebuilt && regenerated == graph, "an edited block lifted structurally regenerates its edited graph");
    }
    // Every other block keeps its evidence.
    std::size_t same = 0;
    for (std::uint32_t f = 0; f < image.functions.size(); ++f) {
        for (std::uint32_t b = 0; b < image.functions[f].blocks.size(); ++b) {
            if (std::make_pair(f, b) != *target &&
                after.blocks[f][b].evidence == before.blocks[f][b].evidence) {
                ++same;
            }
        }
    }
    std::size_t blocks = 0;
    for (const auto& function : image.functions) {
        blocks += function.blocks.size();
    }
    check_equal(same, blocks - 1, "blocks other than the edited one keep their evidence");
}

NEURA_TEST(lift, provenance_is_optional) {
    // With the source map and NAME section removed, the lifted content is
    // unchanged: only provenance comments disappear.
    for (const auto* name : {"objects", "loops", "generics", "floats"}) {
        const auto image = build_program({program_path(environment, name)});
        decompiler::DecompileOptions plain;
        plain.provenance = false;
        const auto without = decompiler::decompile(image, plain).text;
        const auto stripped = decompiler::decompile(without_provenance(image)).text;
        check_equal(stripped, without, std::string(name) + ": decompiling without provenance");
        check(without.find("provenance:") == std::string::npos, std::string(name) + ": no provenance comments");
        check(decompiler::decompile(image).text.find("provenance:") != std::string::npos,
              std::string(name) + ": provenance comments when metadata is present");
    }
}

// ---------------------------------------------------------------------------
// Behavior: the image regenerated from the lifted operations runs like the
// original under the same inputs, limits, and host responses.

NEURA_TEST(behavior, resynthesized_images_replay_the_original_trace) {
    std::size_t compared = 0;
    for (const auto& item : image_corpus(environment)) {
        if (!item.compiled) {
            continue;
        }
        const auto image = nvr::load(item.bytes);
        const auto decompilation = decompiler::decompile(image);
        const auto rebuilt = nvr::load(nvr::encode(decompiler::resynthesize(image, decompilation)));
        nvm::StringSink sink;
        nvm::ConsoleWriteLine console(sink);
        nvm::HostRegistry registry;
        registry.add(console);
        const auto bindings = nvm::bind(image, registry);
        nvm::RunLimits limits;
        limits.max_steps = 2'000'000;
        for (const auto& arguments : {std::vector<std::string>{}, std::vector<std::string>{"héllo", "two"}}) {
            const auto trace = nvm::record(image, bindings, arguments, limits);
            const auto replayed = nvm::replay(rebuilt, trace, arguments, limits);
            check(replayed.matches, item.label + ": " + replayed.difference);
            ++compared;
        }
    }
    check(compared > 150, "every compiled corpus image was compared");
}

NEURA_TEST(behavior, resynthesized_functions_agree_exhaustively) {
    // Scalar functions of the arithmetic and structures fixtures, called on
    // every 8-bit input in the original and the regenerated image.
    for (const auto* fixture : {"arithmetic", "structures"}) {
        auto program = lower_files({backend_dir(environment) / "fixtures" / (std::string(fixture) + ".nra")});
        backend::ConversionOptions options;
        const auto module = find_symbol(program, fixture == std::string("arithmetic") ? "Arithmetic" : "Structures");
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto& symbol = program.symbols[id];
            if (symbol.owner == module && symbol.kind == SymbolKind::Function && symbol.exported &&
                symbol.parameters.size() <= 2) {
                options.extra_functions.push_back(id);
            }
        }
        const auto conversion = backend::convert(program, options);
        const auto image = nvr::load(nvr::encode(conversion.image));
        const auto rebuilt = nvr::load(nvr::encode(decompiler::resynthesize(image, decompiler::decompile(image))));
        for (std::uint32_t f = 0; f < conversion.functions.size(); ++f) {
            const auto& origin = conversion.functions[f];
            if (std::find(options.extra_functions.begin(), options.extra_functions.end(), origin.symbol) ==
                options.extra_functions.end()) {
                continue;
            }
            const auto& function = image.functions[f];
            std::vector<std::uint64_t> domain;
            bool small = true;
            for (std::uint32_t p = 0; p < function.parameter_count; ++p) {
                const auto& t = image.types[function.frame_types[p]];
                small = small && (t.kind == nvr::TypeKind::Bool || t.width <= 8);
            }
            if (!small) {
                continue;
            }
            const auto count = function.parameter_count;
            const std::uint64_t limit = count == 0 ? 1 : (count == 1 ? 256 : 65536);
            for (std::uint64_t bits = 0; bits < limit; ++bits) {
                std::vector<std::uint64_t> raw;
                for (std::uint32_t p = 0; p < count; ++p) {
                    const auto& t = image.types[function.frame_types[p]];
                    const auto value = (bits >> (8 * p)) & 0xff;
                    raw.push_back(t.kind == nvr::TypeKind::Bool ? value & 1 : value);
                }
                nvm::Execution original(image, f, raw);
                nvm::Execution regenerated(rebuilt, f, raw);
                const auto a = original.advance();
                const auto b = regenerated.advance();
                check(a.kind == b.kind && a.result == b.result &&
                          (a.kind != nvm::Event::Kind::Trapped || (a.trap.kind == b.trap.kind && a.trap.reason == b.trap.reason)),
                      std::string(fixture) + " function " + std::to_string(f) + " differs on input " + std::to_string(bits));
            }
        }
    }
}

} // namespace neura::testing

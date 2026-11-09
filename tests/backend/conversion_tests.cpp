// IRtoNeuronConversion: the compiled graphs compute what the source says.
// Results are compared with the NeuraIR reference evaluator from the
// optimization tests, which shares no code with the converter or NVM.

#include "fixtures.hpp"

#include "backend/converter.hpp"
#include "ir-optimization/evaluator.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/float.hpp"
#include "nvr/names.hpp"
#include "neura-ir/integer.hpp"

#include <fstream>
#include <map>
#include <sstream>

namespace neura::testing {
namespace {

using backend::FunctionOrigin;

fs::path fixture(const Environment& environment, std::string_view name) {
    return backend_dir(environment) / "fixtures" / (std::string(name) + ".nra");
}

struct Compiled {
    IrProgram program;
    backend::Conversion conversion;
    nvr::Image image;
};

// Builds `file` with every function of `module` converted as well, so that
// tests can call those functions directly.
Compiled compile_with_functions(const fs::path& file, std::string_view module) {
    Compiled result;
    result.program = lower_files({file});
    auto optimized = result.program;
    optimize(optimized);
    const auto owner = find_symbol(optimized, module);
    backend::ConversionOptions options;
    for (SymbolId id = 0; id < optimized.symbols.size(); ++id) {
        const auto& symbol = optimized.symbols[id];
        if (symbol.owner == owner && symbol.kind == SymbolKind::Function) {
            options.extra_functions.push_back(id);
        }
    }
    result.conversion = backend::convert(optimized, options);
    result.image = nvr::load(nvr::encode(result.conversion.image));
    return result;
}

std::uint32_t function_index(const Compiled& compiled, SymbolId symbol) {
    for (std::uint32_t i = 0; i < compiled.conversion.functions.size(); ++i) {
        const auto& origin = compiled.conversion.functions[i];
        if (origin.role == FunctionOrigin::Role::Function && origin.symbol == symbol) {
            return i;
        }
    }
    fail("function " + qualified_name(compiled.program, symbol) + " was not converted");
}

std::uint64_t encode(const Value& value, unsigned width) {
    if (value.kind == Value::Kind::Bool) {
        return value.boolean ? 1 : 0;
    }
    const auto mask = width == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
    return (value.negative ? std::uint64_t{0} - value.magnitude : value.magnitude) & mask;
}

Value decode(std::uint64_t bits, const IrType& type) {
    if (type.kind == TypeKind::Bool) {
        return bool_value(bits != 0);
    }
    if (type.kind == TypeKind::Float) {
        Value value;
        value.kind = Value::Kind::Float;
        value.type = type;
        value.magnitude = bits;
        return value;
    }
    const auto format = *specified_integer_format(type);
    if (format.is_signed && format.bits < 64 && ((bits >> (format.bits - 1)) & 1U) != 0) {
        const auto magnitude = (std::uint64_t{1} << format.bits) - bits;
        return integer_value(type, true, magnitude);
    }
    if (format.is_signed && format.bits == 64 && static_cast<std::int64_t>(bits) < 0) {
        return integer_value(type, true, std::uint64_t{0} - bits);
    }
    return integer_value(type, false, bits);
}

std::vector<std::uint64_t> operands(const IrType& type, bool exhaustive) {
    if (type.kind == TypeKind::Bool) {
        return {0, 1};
    }
    if (type.kind == TypeKind::Float) {
        // Zeros, ones, halves, extremes, subnormals, infinities, a NaN, and
        // pseudo-random encodings.
        const auto format = *float_format(type);
        const auto w = format.width();
        const auto f = format.fraction_bits;
        const auto sign = std::uint64_t{1} << (w - 1);
        const auto ones = ((std::uint64_t{1} << format.exponent_bits) - 1) << f;
        const auto one = format.bias() << f;
        std::vector<std::uint64_t> values{0, sign, one, one | sign, one - (std::uint64_t{1} << f), one + 1,
                                          1, sign | 1, ones - 1, ones, ones | sign, ones | (std::uint64_t{1} << (f - 1)),
                                          (format.bias() + 3) << f, (std::uint64_t{1} << f) - 1};
        std::uint64_t state = w * 7919;
        for (int i = 0; i < (exhaustive ? 24 : 10); ++i) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            values.push_back((state >> 11) & (w == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << w) - 1));
        }
        return values;
    }
    const auto format = *specified_integer_format(type);
    const auto mask = format.bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << format.bits) - 1;
    std::vector<std::uint64_t> values;
    if (format.bits == 8) {
        for (std::uint64_t v = 0; v < 256; v += exhaustive ? 1 : 3) {
            values.push_back(v);
        }
        if (!exhaustive) {
            values.push_back(255);
            values.push_back(128);
            values.push_back(127);
        }
        return values;
    }
    values = {0, 1, 2, 3, 5, 10, 49, 50, 100, mask, mask - 1, mask >> 1, (mask >> 1) + 1, (mask >> 1) - 1};
    std::uint64_t state = format.bits;
    for (int i = 0; i < 24; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        values.push_back((state >> (i % 2 == 0 ? 40 : 3)) & mask);
    }
    return values;
}

// The NVM fault that corresponds to a fault the evaluator reports.
nvm::TrapKind fault_kind(const std::string& reason) {
    if (reason == "index out of bounds") return nvm::TrapKind::IndexOutOfBounds;
    if (reason == "read of an uninitialized field") return nvm::TrapKind::UninitializedField;
    if (reason == "use of a destroyed object") return nvm::TrapKind::InvalidHandle;
    return nvm::TrapKind::Program;
}

struct Comparison {
    std::size_t returned = 0;
    std::size_t trapped = 0;
};

// Calls `name` on every combination of operands in the evaluator and in the
// compiled image and requires the same result or the same failure.
Comparison compare_function(const Compiled& compiled, Evaluator& evaluator, std::string_view name, bool exhaustive) {
    const auto symbol = find_symbol(compiled.program, name);
    const auto& function = compiled.program.symbols[symbol];
    const auto index = function_index(compiled, symbol);
    std::vector<std::vector<std::uint64_t>> domains;
    std::vector<IrType> types;
    for (const auto parameter : function.parameters) {
        types.push_back(compiled.program.symbols[parameter].type);
        domains.push_back(operands(types.back(), exhaustive && function.parameters.size() <= 2));
    }
    Comparison comparison;
    std::vector<std::size_t> position(domains.size(), 0);
    for (;;) {
        std::vector<Value> values;
        std::vector<std::uint64_t> raw;
        for (std::size_t i = 0; i < domains.size(); ++i) {
            raw.push_back(domains[i][position[i]]);
            values.push_back(decode(raw.back(), types[i]));
        }
        const auto expected = evaluator.call(symbol, values);
        nvm::Execution execution(compiled.image, index, raw);
        const auto event = execution.advance();
        std::ostringstream label;
        label << name << "(";
        for (std::size_t i = 0; i < values.size(); ++i) {
            label << (i == 0 ? "" : ", ") << describe(values[i]);
        }
        label << ")";
        switch (expected.status) {
        case Status::Returned: {
            check(event.kind == nvm::Event::Kind::Completed,
                  label.str() + ": evaluator returned " + describe(*expected.result) + ", image " +
                      (event.kind == nvm::Event::Kind::Trapped ? nvm::describe(event.trap, compiled.image) : "paused"));
            const auto width = expected.result->kind == Value::Kind::Bool    ? 1U
                               : expected.result->kind == Value::Kind::Float ? float_format(function.type)->width()
                                                                              : specified_integer_format(function.type)->bits;
            check_equal(event.result, encode(*expected.result, width), label.str() + " result bits");
            ++comparison.returned;
            break;
        }
        case Status::Faulted:
            check(event.kind == nvm::Event::Kind::Trapped && event.trap.kind == fault_kind(expected.reason),
                  label.str() + ": evaluator faulted (" + expected.reason + "), image " +
                      (event.kind == nvm::Event::Kind::Completed ? "returned " + std::to_string(event.result)
                                                                 : nvm::describe(event.trap, compiled.image)));
            ++comparison.trapped;
            break;
        case Status::Trapped:
            check(event.kind == nvm::Event::Kind::Trapped && event.trap.kind == nvm::TrapKind::Program &&
                      event.trap.reason == expected.trap_reason,
                  label.str() + ": evaluator stopped (" + expected.reason + "), image " +
                      (event.kind == nvm::Event::Kind::Completed ? "returned " + std::to_string(event.result)
                                                                 : nvm::describe(event.trap, compiled.image)));
            ++comparison.trapped;
            break;
        case Status::Unsupported:
            fail(label.str() + ": the evaluator does not model this call: " + expected.reason);
        case Status::LimitReached:
            break;
        }
        std::size_t i = 0;
        for (; i < position.size(); ++i) {
            if (++position[i] < domains[i].size()) {
                break;
            }
            position[i] = 0;
        }
        if (i == position.size()) {
            break;
        }
    }
    return comparison;
}

// Lines the program printed, according to the evaluator's effect list.
std::string expected_output(const Outcome& outcome) {
    std::string output;
    for (const auto& effect : outcome.effects) {
        output += effect.arguments.at(0).text + "\n";
    }
    return output;
}

} // namespace

NEURA_TEST(conversion, conjunction_is_one_relu_neuron) {
    // docs/Project.md: `left && right` on two Boolean parameters becomes a
    // single ReLU neuron with weights 1, 1 and bias -1.
    const auto compiled = compile_with_functions(fixture(environment, "both"), "Logic");
    const auto index = function_index(compiled, find_symbol(compiled.program, "Logic.both"));
    const auto& function = compiled.image.functions[index];
    check_equal(function.blocks.size(), std::size_t{1}, "blocks in both()");
    const auto& graph = compiled.image.graphs[function.blocks[0].graph];
    check_equal(graph.input_lanes, 2U, "input lanes");
    check_equal(graph.nodes.size(), std::size_t{1}, "neurons");
    const auto& node = graph.nodes[0];
    check(node.activation == nvr::Activation::Relu && node.bias == -1 && node.edges.size() == 2 &&
              node.edges[0].weight == 1 && node.edges[1].weight == 1,
          "the neuron is relu(left + right - 1)");
    check(graph.outputs == std::vector<std::uint32_t>{2}, "the neuron is the only output");
    std::string table;
    for (std::uint64_t left = 0; left < 2; ++left) {
        for (std::uint64_t right = 0; right < 2; ++right) {
            nvm::Execution execution(compiled.image, index, std::vector<std::uint64_t>{left, right});
            const auto event = execution.advance();
            check(event.kind == nvm::Event::Kind::Completed, "both() completes");
            table += std::to_string(event.result);
        }
    }
    check_equal(table, std::string("0001"), "truth table in the order 00, 01, 10, 11");
}

NEURA_TEST(conversion, functions_agree_with_the_reference_evaluator) {
    const auto compiled = compile_with_functions(fixture(environment, "arithmetic"), "Arithmetic");
    Evaluator evaluator(compiled.program);
    evaluator.initialize();
    const std::map<std::string, bool> functions{
        {"Arithmetic.add_i8", true},       {"Arithmetic.sub_u8", true},
        {"Arithmetic.mul_i8", true},       {"Arithmetic.div_i8", true},
        {"Arithmetic.div_u8", false},      {"Arithmetic.negate_i8", true},
        {"Arithmetic.mixed_i8", false},    {"Arithmetic.guarded_i8", true},
        {"Arithmetic.compare_i16", false}, {"Arithmetic.poly_i32", false},
        {"Arithmetic.clamp_i64", false},   {"Arithmetic.triangle_u64", false},
        {"Arithmetic.sum_to_u16", false},  {"Arithmetic.first_root_above_i32", false},
        {"Arithmetic.recursive_sum_u8", false}, {"Arithmetic.swap_difference_u32", false},
        {"Arithmetic.rem_i8", true},       {"Arithmetic.rem_u8", true},
        {"Arithmetic.to_unsigned_i8", true}, {"Arithmetic.to_signed_u8", true},
        {"Arithmetic.narrow_i16", false},  {"Arithmetic.widen_u8", true},
        {"Arithmetic.digit_sum_u8", true},
    };
    Comparison total;
    for (const auto& [name, exhaustive] : functions) {
        const auto comparison = compare_function(compiled, evaluator, name, exhaustive);
        check(comparison.returned > 0, name + " returned for some inputs");
        total.returned += comparison.returned;
        total.trapped += comparison.trapped;
    }
    check(total.trapped > 0, "some inputs reach an undefined result");
}

NEURA_TEST(conversion, floats_agree_with_the_reference_evaluator) {
    // Float arithmetic, comparisons, and conversions in compiled functions,
    // including literal operands, a loop, and traps on out-of-range
    // conversions.
    const auto compiled = compile_with_functions(fixture(environment, "floats"), "Floats");
    Evaluator evaluator(compiled.program);
    evaluator.initialize();
    Comparison total;
    for (const auto* name : {"Floats.add16", "Floats.multiply_bf16", "Floats.divide32", "Floats.subtract64",
                             "Floats.ordered32", "Floats.scaled32", "Floats.truncate32", "Floats.from_int16",
                             "Floats.narrow64", "Floats.polynomial64"}) {
        const auto comparison = compare_function(compiled, evaluator, name, true);
        check(comparison.returned > 0, std::string(name) + " returned for some inputs");
        total.returned += comparison.returned;
        total.trapped += comparison.trapped;
    }
    check(total.trapped > 0, "out-of-range conversions trap");
}

NEURA_TEST(conversion, structures_agree_with_the_reference_evaluator) {
    // Objects, collections, and loops: results, program traps, and runtime
    // faults must match the evaluator on every input of each 8-bit domain.
    const auto compiled = compile_with_functions(fixture(environment, "structures"), "Structures");
    Evaluator evaluator(compiled.program);
    evaluator.initialize();
    Comparison total;
    for (const auto* name : {"Structures.two_counters", "Structures.construction_order", "Structures.list_sum",
                             "Structures.element", "Structures.remove", "Structures.grow_while_iterating",
                             "Structures.skip_and_stop"}) {
        const auto comparison = compare_function(compiled, evaluator, name, true);
        check(comparison.returned > 0, std::string(name) + " returned for some inputs");
        total.returned += comparison.returned;
        total.trapped += comparison.trapped;
    }
    check(total.trapped > 0, "some inputs trap or fault");
}

NEURA_TEST(conversion, programs_print_what_the_evaluator_records) {
    // Whole programs within the evaluator's model: the evaluator's effect
    // list and NVM's captured stdout must agree byte for byte.
    const std::map<std::string, std::string> programs{
        {"computation", "Computation"}, {"short-circuit", "ShortCircuit"}, {"integers", "Integers"},
        {"loops", "Loops"}, {"objects", "Objects"}, {"collections", "Collections"},
    };
    for (const auto& [name, module] : programs) {
        const auto path = program_path(environment, name);
        const auto program = lower_files({path});
        Evaluator evaluator(program);
        for (const auto& outcome : evaluator.initialize()) {
            check(outcome.status == Status::Returned, std::string(name) + ": initializer " + describe(outcome));
        }
        const auto main = find_symbol(program, module + ".main");
        const auto expected = evaluator.call(main, {Value{}});
        check(expected.status == Status::Returned, std::string(name) + ": " + describe(expected));
        const auto image = build_program({path});
        const auto run = run_captured(image);
        check(run.result.completed, std::string(name) + ": " + describe_trap(run.result, image));
        check_equal(run.output, expected_output(expected), std::string(name) + ": stdout");
    }
}

NEURA_TEST(conversion, initialization_order_matches_the_evaluator) {
    // Two files, top-level statements, literal and computed module globals,
    // and a top-level object: the start function and the evaluator must
    // produce the same output in the same order.
    const auto path = backend_dir(environment) / "initialization" / "main.nra";
    const auto program = lower_files({path, backend_dir(environment) / "initialization" / "settings.nra"});
    Evaluator evaluator(program);
    std::string expected;
    for (const auto& outcome : evaluator.initialize()) {
        check(outcome.status == Status::Returned, "initialization: " + describe(outcome));
        expected += expected_output(outcome);
    }
    const auto run = evaluator.call(find_symbol(program, "App.main"), {Value{}});
    check(run.status == Status::Returned, "main: " + describe(run));
    expected += expected_output(run);
    const auto image = build_program({path});
    const auto captured = run_captured(image);
    check(captured.result.completed, describe_trap(captured.result, image));
    check_equal(captured.output, expected, "stdout of the initialization program");
    std::ifstream file(backend_dir(environment) / "initialization" / "main.out");
    const std::string recorded((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check_equal(captured.output, recorded, "stdout matches main.out");
}

NEURA_TEST(conversion, optimized_and_unoptimized_images_agree) {
    for (const auto& entry : fs::directory_iterator(backend_dir(environment) / "programs")) {
        if (entry.path().extension() != ".nra") {
            continue;
        }
        const auto optimized = build_program({entry.path()}, {}, true);
        const auto unoptimized = build_program({entry.path()}, {}, false);
        const auto a = run_captured(optimized);
        const auto b = run_captured(unoptimized);
        const auto label = entry.path().filename().string();
        check(a.result.completed == b.result.completed && a.result.status == b.result.status,
              label + ": " + describe_trap(a.result, optimized) + " vs " + describe_trap(b.result, unoptimized));
        check_equal(a.output, b.output, label + ": stdout");
    }
}

NEURA_TEST(conversion, unsupported_operations_are_diagnosed) {
    for (const auto& entry : fs::directory_iterator(backend_dir(environment) / "unsupported")) {
        std::ifstream input(entry.path());
        std::string header;
        std::getline(input, header);
        const std::string prefix = "// expect: ";
        check(header.starts_with(prefix), entry.path().string() + " starts with an expectation");
        std::istringstream expectation(header.substr(prefix.size()));
        std::size_t line = 0;
        std::size_t column = 0;
        char colon = 0;
        expectation >> line >> colon >> column;
        std::string message;
        std::getline(expectation >> std::ws, message);
        const auto label = entry.path().filename().string();
        try {
            backend::build(lower_files({entry.path()}));
            fail(label + ": the build succeeded");
        } catch (const Diagnostic& error) {
            check(std::string(error.what()).find(message) != std::string::npos,
                  label + ": expected '" + message + "', got '" + error.what() + "'");
            check(error.span.line == line && error.span.column == column,
                  label + ": expected " + std::to_string(line) + ":" + std::to_string(column) + ", got " +
                      describe(error.span));
        }
    }
}

NEURA_TEST(conversion, identical_operations_share_one_graph) {
    const auto image = build_program({fixture(environment, "shared-multiplier")});
    std::size_t multipliers = 0;
    std::size_t sites = 0;
    for (std::uint32_t g = 0; g < image.graphs.size(); ++g) {
        if (image.graphs[g].input_lanes == 64 && image.graphs[g].nodes.size() > 300) {
            ++multipliers;
            for (const auto& function : image.functions) {
                for (const auto& block : function.blocks) {
                    sites += block.graph == g ? 1 : 0;
                }
            }
        }
    }
    check_equal(multipliers, std::size_t{1}, "int32 multiplier graphs");
    check_equal(sites, std::size_t{2}, "blocks using the multiplier");
    check_equal(run_captured(image).output, std::string("24\n"), "output");
}

NEURA_TEST(conversion, conversion_is_deterministic) {
    for (const auto& entry : fs::directory_iterator(backend_dir(environment) / "programs")) {
        if (entry.path().extension() == ".nra") {
            check(nvr::encode(backend::build(lower_files({entry.path()})).image) ==
                      nvr::encode(backend::build(lower_files({entry.path()})).image),
                  entry.path().filename().string() + ": two builds differ");
        }
    }
}

NEURA_TEST(conversion, source_map_is_optional) {
    auto with_map = backend::build(lower_files({program_path(environment, "computation")})).image;
    backend::BuildOptions options;
    options.source_map = false;
    auto without_map = backend::build(lower_files({program_path(environment, "computation")}), options).image;
    check(with_map.source_map.has_value() && !with_map.source_map->empty(), "the default build has a source map");
    check(!without_map.source_map.has_value(), "--no-source-map omits SMAP");
    check(without_map.other_sections.empty(), "--no-source-map omits NAME");
    check(with_map.other_sections.size() == 1 && with_map.other_sections.front().tag == nvr::names_tag,
          "the default build has a NAME section");
    with_map.source_map.reset();
    with_map.other_sections.clear();
    check(with_map == without_map, "the source map and names are the only differences");
    const auto rebuilt = backend::build(lower_files({program_path(environment, "computation")}));
    for (const auto& location : *rebuilt.image.source_map) {
        check_equal(location.path, std::string("computation.nra"), "source map paths are relative");
    }
}

} // namespace neura::testing

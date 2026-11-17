// IRtoNeuronConversion: the compiled graphs compute what the source says.
// Results are compared with the NeuraIR reference evaluator from the
// optimization tests, which shares no code with the converter or NVM.

#include "fixtures.hpp"

#include "backend/converter.hpp"
#include "cli/source_loader.hpp"
#include "ir-optimization/evaluator.hpp"
#include "lift/hints.hpp"
#include "lift/proof.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/float.hpp"
#include "nvr/names.hpp"
#include "neura-ir/integer.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/inline_limit.hpp"
#include "neura-ir/containment.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#ifndef _WIN32
#include <unistd.h>
#endif

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
        case Status::Exited:
            // These fixtures call no host service, so none can end the process.
            fail(label.str() + ": the evaluator reports a process exit in a function without host calls");
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

NEURA_TEST(conversion, inspection_preserves_hook_execution) {
    const auto image = build_program({backend_dir(environment) / "hooks" / "references.nra"});
    const auto oracle = run_captured(image);
    check(oracle.result.completed, describe_trap(oracle.result, image));
    nvm::Execution execution(image, std::span<const std::string>{});
    CapturingConsole console;
    std::vector<std::int64_t> scratch, outputs;
    auto inspected = std::size_t{0};
    const auto inspect = [&](const std::vector<nvm::DebugValue>& values) {
        for (const auto& value : values) {
            if (value.children > 0) inspected += execution.debug_children(value.type, value.bits).size();
        }
    };
    for (;;) {
        nvm::Execution::GraphStep step;
        auto event = execution.begin_step(step);
        if (!event) {
            const auto snapshot = execution.debug_snapshot();
            inspect(snapshot.globals);
            for (const auto& thread : snapshot.threads) {
                for (const auto& frame : thread.frames) inspect(frame.slots);
            }
            outputs.resize(step.output_count);
            const auto overflow = nvm::evaluate_outputs(image.graphs[step.graph], step.inputs, scratch, outputs);
            check(!overflow, "the fixture graphs evaluate successfully");
            event = execution.finish_step(outputs);
        }
        if (!event) continue;
        if (event->kind == nvm::Event::Kind::HostRequest) {
            execution.complete(console.invoke(event->request));
            continue;
        }
        check(event->kind == nvm::Event::Kind::Completed, "the inspected hook program completes");
        check_equal(event->status, oracle.result.status, "status");
        break;
    }
    check(inspected > 0, "inspected object or collection children");
    check_equal(console.output(), oracle.output, "hook trace while inspecting every block");
    check_equal(console.requests.size(), oracle.requests.size(), "host requests");
    check_equal(execution.steps(), oracle.result.steps, "steps");
    check_equal(execution.peak_heap_cells(), oracle.result.peak_heap_cells, "peak heap cells");
    check_equal(execution.peak_stack_cells(), oracle.result.peak_stack_cells, "peak stack cells");
}

// The source range producer only repeats numbers. NeuraIR also permits a
// class reference as a repeat fill. Evaluate its getter once even for length
// zero, then admit the saved value once for each element.
NEURA_TEST(conversion, repeated_class_reference_admission) {
    for (const auto length : {0, 3}) {
        auto program = lower_files({backend_dir(environment) / "hooks" / "repeat.nra"});
        bool replaced = false;
        for (auto& body : program.bodies) {
            for (auto& node : body.instructions) {
                if (node.op != IrOp::Initialize || program.symbols[node.symbol].name != "batch") continue;
                auto& collection = node.operands.front();
                check(collection.op == IrOp::Collection && collection.operands.size() == 1, "the single fill");
                IrNode count{};
                count.op = IrOp::Integer;
                count.span = collection.span;
                count.type = {TypeKind::Integer, "uint64", no_symbol, {}};
                count.text = std::to_string(length);
                collection.op = IrOp::RepeatCollection;
                collection.operands.insert(collection.operands.begin(), std::move(count));
                replaced = true;
            }
        }
        check(replaced, "the fixture includes the batch initializer");
        const auto image = nvr::load(nvr::encode(backend::build(std::move(program)).image));
        const auto run = run_captured(image);
        check(run.result.completed, describe_trap(run.result, image));
        check_equal(run.result.status, 1 + length * 10, "a getter once and a setter per repeated element");
    }
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

// Jump threading: no exit resumes at a block that evaluates nothing and only
// continues elsewhere (a poll is an effect, so a block that only polls stays), except inside a cycle of such blocks (an empty endless
// loop, kept so that it still runs) or the entry block.
NEURA_TEST(conversion, exits_skip_jump_only_blocks) {
    std::size_t checked = 0;
    for (const auto& entry : fs::directory_iterator(backend_dir(environment) / "programs")) {
        if (entry.path().extension() != ".nra") continue;
        const auto image = build_program({entry.path()});
        for (const auto& function : image.functions) {
            const auto jump_only = [&](std::uint32_t index) {
                const auto& block = function.blocks.at(index);
                return block.inputs.empty() && image.graphs[block.graph].nodes.empty() && block.exits.size() == 1 &&
                       block.exits.front().kind == nvr::ExitKind::Continue && block.exits.front().assignments.empty() &&
                       !block.exits.front().poll;
            };
            for (const auto& block : function.blocks) {
                for (const auto& exit : block.exits) {
                    if (exit.kind != nvr::ExitKind::Continue && exit.kind != nvr::ExitKind::Call &&
                        exit.kind != nvr::ExitKind::State) {
                        continue;
                    }
                    ++checked;
                    if (!jump_only(exit.resume)) continue;
                    // Allowed only when the jump-only chain returns to itself.
                    auto target = exit.resume;
                    bool cycle = false;
                    for (std::size_t hops = 0; hops <= function.blocks.size() && jump_only(target); ++hops) {
                        target = function.blocks[target].exits.front().resume;
                        if (target == exit.resume) {
                            cycle = true;
                            break;
                        }
                    }
                    check(cycle, entry.path().filename().string() + ": an exit resumes at a jump-only block");
                }
            }
        }
    }
    check(checked > 1000, "too few exits were checked");
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

NEURA_TEST(conversion, lift_hints_are_optional) {
    // The LIFT section is optional metadata (docs/runtime/NVR.md, "Lift
    // hints"): leaving it out changes nothing else, raises no format
    // version, and every hint the converter writes passes the structural
    // proof against its block.
    const auto program = program_path(environment, "computation");
    auto with_hints = backend::build(lower_files({program})).image;
    backend::BuildOptions options;
    options.lift_hints = false;
    const auto without_hints = backend::build(lower_files({program}), options).image;
    const auto hints = lift::decode_hints(with_hints);
    check(!hints.empty(), "the default build records lift hints");
    check(lift::decode_hints(without_hints).empty(), "--no-lift-hints omits LIFT");
    for (const auto& [key, form] : hints) {
        const auto& block = with_hints.functions[key.first].blocks[key.second];
        check(lift::prove_structure(with_hints, block, with_hints.graphs[block.graph], *form),
              "hint for function " + std::to_string(key.first) + " block " + std::to_string(key.second) +
                  " regenerates its block");
    }
    check(nvr::encode(with_hints).size() > nvr::encode(without_hints).size(), "the section takes bytes");
    check(nvr::load(nvr::encode(with_hints)).minor_version == nvr::load(nvr::encode(without_hints)).minor_version,
          "the section raises no format version");
    check(lift::strip_hints(with_hints), "the default build has a LIFT section");
    check(with_hints == without_hints, "the hints are the only difference");
}

NEURA_TEST(conversion, source_map_is_optional) {
    auto with_map = backend::build(lower_files({program_path(environment, "computation")})).image;
    backend::BuildOptions options;
    options.source_map = false;
    auto without_map = backend::build(lower_files({program_path(environment, "computation")}), options).image;
    // The LIFT section is optional metadata of its own (lift_hints_are_optional).
    check(lift::strip_hints(with_map) && lift::strip_hints(without_map), "both builds carry lift hints");
    check(with_map.source_map.has_value() && !with_map.source_map->empty(), "the default build has a source map");
    check(!without_map.source_map.has_value(), "--no-source-map omits SMAP");
    check(without_map.other_sections.empty(), "--no-source-map omits NAME");
    check(with_map.other_sections.size() == 1 && with_map.other_sections.front().tag == nvr::names_tag,
          "the default build has a NAME section");
    with_map.source_map.reset();
    with_map.other_sections.clear();
    check(with_map == without_map, "the source map and names are the only differences");
    const auto rebuilt = backend::build(lower_files({program_path(environment, "computation")}));
    // Program files are labeled relative to their common directory and
    // standard library files by their path below the library root.
    bool program_label = false;
    for (const auto& location : *rebuilt.image.source_map) {
        if (location.path == "computation.nra") {
            program_label = true;
            continue;
        }
        check(location.path.starts_with("std/") && location.path.ends_with(".nra"),
              "source map label " + location.path + " is relative");
    }
    check(program_label, "the program's own blocks are labeled computation.nra");
}

// A payload enum is a run of value words: a uint32 tag, then its payload words pooled by type. Optional and Result
// over a payload that needs no allocation allocate nothing, a function returns an enum's words, a word that the
// variant does not use is zero, empty text or the nil collection, and the words carry the binding's name.
NEURA_TEST(conversion, payload_enums_are_value_words) {
    using Kind = nvr::TypeKind;
    // The functions use generic library enums, so the program is specialized as `neura build` does.
    Compiled compiled;
    compiled.program = lower_files({fixture(environment, "enum-words")});
    auto specialized = compiled.program;
    materialize_reference_storage(specialized);
    specialize(specialized);
    check_containment(specialized);
    check_inline_limit(specialized);
    optimize(specialized);
    backend::ConversionOptions options;
    const auto module = find_symbol(specialized, "Words");
    for (SymbolId id = 0; id < specialized.symbols.size(); ++id) {
        const auto& symbol = specialized.symbols[id];
        if (symbol.owner == module && symbol.kind == SymbolKind::Function) options.extra_functions.push_back(id);
    }
    compiled.conversion = backend::convert(specialized, options);
    compiled.image = nvr::load(nvr::encode(compiled.conversion.image));
    const auto names = nvr::decode_names(compiled.image).value();
    const auto index_of = [&](std::string_view name) {
        return function_index(compiled, find_symbol(compiled.program, "Words." + std::string(name)));
    };
    const auto words_of = [&](std::string_view name) {
        return compiled.image.functions[index_of(name)].more_results.size() + 1;
    };
    check_equal(words_of("some"), std::size_t{2}, "Optional<int> is a tag and an int32");
    // Ok(int) and Err(Error) share the int32 word: the tag, then int32, then Error's words but its int32.
    check_equal(words_of("ok"), std::size_t{8}, "Result<int> is 1 + 7 words");
    check_equal(words_of("count"), std::size_t{4}, "Token is a tag, an int32, a text and a list");
    for (const auto* name : {"some", "unwrap", "ok", "count", "tag"}) {
        for (const auto& block : compiled.image.functions[index_of(name)].blocks) {
            for (const auto& exit : block.exits) {
                check(exit.kind != nvr::ExitKind::State || (exit.operation != nvr::StateOp::AllocateObject &&
                                                            exit.operation != nvr::StateOp::ReadField),
                      std::string(name) + " allocates no object and reads no field");
            }
        }
    }
    // Token.Count leaves its text word empty and its list word nil.
    const auto count = index_of("count");
    bool empty_text = false;
    bool nil_list = false;
    for (const auto& block : compiled.image.functions[count].blocks) {
        for (const auto& exit : block.exits) {
            if (exit.kind != nvr::ExitKind::Return) continue;
            for (const auto& value : exit.arguments) {
                if (value.kind != nvr::ValueKind::Constant) continue;
                const auto& constant = compiled.image.constants[value.index];
                const auto kind = compiled.image.types[constant.type].kind;
                empty_text = empty_text || (kind == Kind::Text && constant.payload.empty());
                nil_list = nil_list || (kind == Kind::List && constant.payload.empty());
            }
        }
    }
    check(empty_text, "an unused text word is the empty text");
    check(nil_list, "an unused list word is the nil collection constant");
    check_equal(compiled.image.minor_version, std::uint16_t{7}, "enum results make the image NVR 1.7");
    const auto& slots = names.slots[index_of("tag")];
    check(slots.size() >= 4 && slots[0] == "token.tag" && slots[1] == "token.Count.0" && slots[2] == "token.Name.0" &&
              slots[3] == "token.Items.0",
          "an enum parameter's words are named by tag and payload");
}

#ifndef _WIN32

// A tuple crosses a host call, and a host callback, as one word in its heap layout. The program's tuples are
// words inline: the call boxes its argument, unboxes the payload of its result, and the callback is a boundary
// function that unboxes its parameter and boxes its result.
NEURA_TEST(conversion, tuples_cross_host_calls_and_callbacks_boxed) {
    using Kind = nvr::TypeKind;
    const auto fixtures = backend_dir(environment) / "fixtures" / "tuple-host";
    // The trusted library tree: the standard library with one more module.
    const auto library = fs::temp_directory_path() / ("neura-tuple-host-" + std::to_string(::getpid()));
    fs::remove_all(library);
    // A copy, not links: the loader trusts a file by its real path below the root.
    fs::create_directories(library);
    fs::copy(library_root() / "std", library / "std", fs::copy_options::recursive);
    fs::copy(fixtures / "probe", library / "std" / "probe", fs::copy_options::recursive);
    const auto main = fs::canonical(fixtures / "main.nra");
    auto program = lower_sources({main}, {main.parent_path()}, library);
    const auto converted = backend::build(std::move(program));
    fs::remove_all(library);
    const auto image = nvr::load(nvr::encode(converted.image));

    // The callback and the import carry boxed tuples; no function of either has result words.
    check_equal(image.callbacks.size(), std::size_t{1}, "one callback");
    const auto& callback = image.functions[image.callbacks.front().function];
    check_equal(callback.parameter_count, std::uint32_t{1}, "the callback takes one word");
    check(callback.more_results.empty() && image.types[callback.result_type].kind == Kind::Object,
          "the callback returns one boxed tuple");
    check(image.types[callback.frame_types[0]].kind == Kind::Object, "the callback takes a boxed tuple");
    check_equal(image.imports.size() >= 1, true, "the host call is imported");

    const nvm::HostType word{Kind::Signed, 32};
    const nvm::HostType flag{Kind::Bool};
    const nvm::HostType text{Kind::Text};
    const nvm::HostType inner(Kind::Object, std::vector<nvm::HostType>{text, flag});
    const nvm::HostType pair(Kind::Object, std::vector<nvm::HostType>{word, inner});
    const nvm::HostType visited(Kind::Object, std::vector<nvm::HostType>{word, text});

    class Echo final : public nvm::HostService {
    public:
        explicit Echo(nvm::HostSignature signature) : signature_(std::move(signature)) {}
        const nvm::HostSignature& signature() const override { return signature_; }
        nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
            arguments = request.arguments;
            const nvm::HostValue sent{0, {}, {{7, {}, {}}, {0, "seven", {}}}};
            if (request.callbacks != nullptr) visited = request.callbacks->call("neura.test.visit", 1, {sent});
            return nvm::HostCompletion::ok(request.arguments.front());
        }
        std::vector<nvm::HostValue> arguments;
        std::optional<nvm::HostValue> visited;

    private:
        nvm::HostSignature signature_;
    };
    Echo echo({"neura.test.echo", 1, {pair}, pair, {Kind::Unsigned, 32}});
    CapturingConsole console;
    nvm::HostRegistry registry;
    registry.add(console);
    registry.add(echo);
    const auto run = nvm::run(image, nvm::bind(image, registry), std::vector<std::string>{});
    check(run.completed, describe_trap(run, image));
    check_equal(console.output(), std::string("seven7true\n"), "the program sees the tuple the host returned");
    const nvm::HostValue expected{0, {}, {{7, {}, {}}, {0, {}, {{0, "seven", {}}, {1, {}, {}}}}}};
    check(echo.arguments == std::vector{expected}, "the host receives the argument as one tuple");
    const nvm::HostValue visit_result{0, {}, {{8, {}, {}}, {0, "seven?", {}}}};
    check(echo.visited.has_value() && *echo.visited == visit_result, "the callback takes and gives one tuple");
}

#endif


} // namespace neura::testing

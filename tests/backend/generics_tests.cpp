// Generic specialization: instances are cached per declaration and concrete
// type arguments, no type parameter survives, the result is deterministic,
// the limits stop specialization that does not terminate, and specialized
// code computes what the reference evaluator computes.

#include "fixtures.hpp"

#include "backend/converter.hpp"
#include "ir-optimization/evaluator.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"

#include <fstream>
#include <sstream>

namespace neura::testing {
namespace {

std::size_t count_named(const IrProgram& program, std::string_view name) {
    std::size_t count = 0;
    for (const auto& symbol : program.symbols) {
        count += symbol.name == name ? 1 : 0;
    }
    return count;
}

IrProgram lower_source(std::string_view name, const std::string& source) {
    const auto path = fs::temp_directory_path() / "neura-generics-tests" / (std::string(name) + ".nra");
    fs::create_directories(path.parent_path());
    std::ofstream(path) << source;
    return lower_files({path});
}

} // namespace

NEURA_TEST(generics, instances_are_cached_and_complete) {
    auto program = lower_files({program_path(environment, "generics")});
    specialize(program);
    check(verify_ir(program).empty(), "the specialized program verifies");
    // box_of<int32> is instantiated from three sites, and Box<int> names the
    // same instance as Box<int32>.
    check_equal(count_named(program, "Box<int32>"), std::size_t{1}, "Box<int32> classes");
    check_equal(count_named(program, "box_of<int32>"), std::size_t{1}, "box_of<int32> functions");
    check_equal(count_named(program, "Box<Box<int32>>"), std::size_t{1}, "Box<Box<int32>> classes");
    check_equal(count_named(program, "pair<bool>"), std::size_t{1}, "Box<int64>.pair<bool> methods");
    check_equal(count_named(program, "Pair<int64, bool>"), std::size_t{1}, "Pair<int64, bool> classes");
    check_equal(count_named(program, "twice<int16>"), std::size_t{1}, "Tools.twice<int16> methods");
    check_equal(count_named(program, "identity<int32>"), std::size_t{1}, "identity<int32> functions");
}

NEURA_TEST(generics, specialization_is_deterministic) {
    auto first = lower_files({program_path(environment, "generics")});
    auto second = lower_files({program_path(environment, "generics")});
    specialize(first);
    specialize(second);
    check_equal(printed(first), printed(second), "two specializations of the same program");
    const auto image = build_program({program_path(environment, "generics")});
    check(nvr::encode(image) == nvr::encode(build_program({program_path(environment, "generics")})),
          "two builds of the generics program");
}

NEURA_TEST(generics, limits_stop_unbounded_specialization) {
    const std::string nested = R"(
let Box = class<T> (
    export let value: T;
);
let use = func() -> int32 {
    let deep = new Box<Box<Box<Box<int32>>>>();
    return 0;
};
)";
    auto program = lower_source("nested", nested);
    auto copy = program;
    specialize(copy);
    check(count_named(copy, "Box<Box<Box<Box<int32>>>>") == 1, "four levels specialize under the default limit");
    SpecializationLimits limits;
    limits.max_depth = 3;
    try {
        specialize(program, limits);
        fail("a nesting limit of 3 should stop Box<Box<Box<Box<int32>>>>");
    } catch (const Diagnostic& error) {
        check(std::string(error.what()).find("nest deeper than 3 levels") != std::string::npos, error.what());
        check_equal(error.span.line, std::size_t{6}, "the diagnostic points at the instantiating line");
    }
    auto many = lower_files({program_path(environment, "generics")});
    limits = {};
    limits.max_instances = 4;
    try {
        specialize(many, limits);
        fail("an instance limit of 4 should stop the generics program");
    } catch (const Diagnostic& error) {
        check(std::string(error.what()).find("more than 4 instances") != std::string::npos, error.what());
    }
}

NEURA_TEST(generics, specialized_functions_agree_with_the_reference_evaluator) {
    auto program = lower_files({backend_dir(environment) / "fixtures" / "generics.nra"});
    specialize(program);
    auto optimized = program;
    optimize(optimized);
    const auto module = find_symbol(optimized, "Generic");
    backend::ConversionOptions options;
    std::vector<std::string> names;
    for (SymbolId id = 0; id < optimized.symbols.size(); ++id) {
        const auto& symbol = optimized.symbols[id];
        if (symbol.owner == module && symbol.kind == SymbolKind::Function && symbol.generics.empty() &&
            symbol.exported && symbol.name.find('<') == std::string::npos) {
            options.extra_functions.push_back(id);
            names.push_back("Generic." + symbol.name);
        }
    }
    const auto conversion = backend::convert(optimized, options);
    const auto image = nvr::load(nvr::encode(conversion.image));
    Evaluator evaluator(program);
    evaluator.initialize();
    std::size_t compared = 0;
    for (std::size_t f = 0; f < options.extra_functions.size(); ++f) {
        const auto symbol = options.extra_functions[f];
        std::uint32_t index = 0;
        while (conversion.functions[index].symbol != symbol) {
            ++index;
        }
        const auto& function = optimized.symbols[symbol];
        const auto parameter_type = optimized.symbols[function.parameters.front()].type;
        const bool boolean = parameter_type.kind == TypeKind::Bool;
        const std::uint64_t limit = boolean ? 2 : 256;
        for (std::uint64_t a = 0; a < limit; ++a) {
            for (std::uint64_t b = 0; b < (function.parameters.size() > 1 ? limit : 1); ++b) {
                std::vector<Value> values;
                std::vector<std::uint64_t> raw;
                for (const auto bits : {a, b}) {
                    if (values.size() == function.parameters.size()) {
                        break;
                    }
                    const auto& type = optimized.symbols[function.parameters[values.size()]].type;
                    raw.push_back(bits);
                    if (type.kind == TypeKind::Bool) {
                        values.push_back(bool_value(bits != 0));
                    } else if (type.name == "int8" && bits >= 128) {
                        values.push_back(integer_value(type, true, 256 - bits));
                    } else {
                        values.push_back(integer_value(type, false, bits));
                    }
                }
                const auto expected = evaluator.call(symbol, values);
                nvm::Execution execution(image, index, raw);
                const auto event = execution.advance();
                const auto label = names[f] + "(" + std::to_string(a) + ", " + std::to_string(b) + ")";
                if (expected.status == Status::Returned) {
                    check(event.kind == nvm::Event::Kind::Completed, label + " should complete");
                    const auto& result = *expected.result;
                    const std::uint64_t bits = result.kind == Value::Kind::Bool
                        ? (result.boolean ? 1 : 0)
                        : ((result.negative ? std::uint64_t{0} - result.magnitude : result.magnitude) & 0xff);
                    check_equal(event.result & 0xff, bits, label);
                } else {
                    check(expected.status == Status::Trapped && event.kind == nvm::Event::Kind::Trapped &&
                              event.trap.reason == expected.trap_reason,
                          label + ": " + describe(expected));
                }
                ++compared;
            }
        }
    }
    check(compared > 100'000, "the generic wrappers were compared exhaustively");
}

} // namespace neura::testing

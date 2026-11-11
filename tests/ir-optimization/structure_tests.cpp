// Structural assertions about optimized NeuraIR: which rewrites happen, which
// are declined, and what the pass manager reports. These tests say nothing
// about runtime behavior; see behavior_tests.cpp for that evidence.
#include "ir-optimization/optimizer.hpp"
#include "support.hpp"

#include <map>
#include <stdexcept>

namespace neura::testing {
namespace {

struct Expectation {
    std::string_view function;
    std::string_view shape;
};

// Optimizes `file` and compares each listed body with its expected shape.
void expect_shapes(const Environment& environment, std::string_view file,
                   std::initializer_list<Expectation> expectations) {
    auto program = lower_fixture(environment, file);
    optimize(program);
    for (const auto& expectation : expectations) {
        check_equal(shape(program, body_of(program, expectation.function)), expectation.shape,
                    std::string(expectation.function));
    }
}

// Requires the optimizer to leave each listed body exactly as lowered.
void expect_unchanged(const Environment& environment, std::string_view file,
                      std::initializer_list<std::string_view> functions) {
    const auto original = lower_fixture(environment, file);
    auto optimized = original;
    optimize(optimized);
    for (const auto function : functions) {
        check_equal(shape(optimized, body_of(optimized, function)), shape(original, body_of(original, function)),
                    std::string(function) + " must not be rewritten");
    }
}

// Optimization keeps every symbol. Only inlining adds symbols: locals named
// "$inline.<callee>.<name>", owned by a function, after the original table.
bool keeps_symbols(const IrProgram& original, const IrProgram& optimized) {
    if (optimized.symbols.size() < original.symbols.size()) return false;
    IrProgram prefix = optimized;
    prefix.symbols.resize(original.symbols.size());
    if (!same_symbols(original, prefix)) return false;
    for (auto id = original.symbols.size(); id < optimized.symbols.size(); ++id) {
        const auto& added = optimized.symbols[id];
        if (added.kind != SymbolKind::Local || !added.name.starts_with("$inline.") ||
            added.owner >= original.symbols.size() || original.symbols[added.owner].kind != SymbolKind::Function) {
            return false;
        }
    }
    return true;
}

constexpr std::string_view observe_call ="(virtual_call observe (receiver ShortCircuit) (load value))";

std::string with_call(std::string_view format) {
    std::string result(format);
    const auto at = result.find("CALL");
    return at == std::string::npos ? result : result.replace(at, 4, observe_call);
}

} // namespace

NEURA_TEST(structure, folds_defined_integer_and_boolean_constants) {
    expect_shapes(environment, "folding.nra",
                  {
                      {"Folding.sum_literals", "(return (integer 5))"},
                      {"Folding.nested", "(return (integer 17))"},
                      {"Folding.double_negation", "(return (integer 5))"},
                      {"Folding.exact_division", "(return (integer -64))"},
                      {"Folding.int8_upper", "(return (integer 127))"},
                      {"Folding.int8_lower", "(return (integer -128))"},
                      {"Folding.uint8_upper", "(return (integer 255))"},
                      {"Folding.uint8_zero", "(return (integer 0))"},
                      {"Folding.int64_upper", "(return (integer 9223372036854775807))"},
                      {"Folding.int64_lower", "(return (integer -9223372036854775808))"},
                      {"Folding.uint64_upper", "(return (integer 18446744073709551615))"},
                      {"Folding.uint64_exact_division", "(return (integer 3689348814741910323))"},
                      {"Folding.ordering", "(return (boolean true))"},
                      {"Folding.boolean_equality", "(return (boolean false))"},
                      {"Folding.boolean_negation", "(return (boolean true))"},
                      {"Folding.scaled", "(return (binary - (binary * (load value) (integer 3)) (integer 2)))"},
                      {"Folding.condition", "(block {(return (load value))})"},
                      {"Folding.truncated_division", "(return (integer 3))"},
                      {"Folding.negative_truncated_division", "(return (integer -3))"},
                      {"Folding.remainder", "(return (integer -1))"},
                      {"Folding.remainder_of_negative_divisor", "(return (integer 1))"},
                      {"Folding.minimum_remainder", "(return (integer 0))"},
                      {"Folding.unsigned_remainder", "(return (integer 15))"},
                      {"Folding.int_alias", "(return (integer 3))"},
                      {"Folding.byte_alias", "(return (integer 255))"},
                      {"Folding.widening", "(return (integer -5))"},
                      {"Folding.narrowing", "(return (integer 200))"},
                  });
}

NEURA_TEST(structure, declines_folds_that_trap) {
    expect_unchanged(environment, "fold-declines.nra",
                     {"FoldDeclines.int8_overflow", "FoldDeclines.int8_underflow",
                      "FoldDeclines.int8_negated_minimum", "FoldDeclines.int8_division_overflow",
                      "FoldDeclines.uint8_negative", "FoldDeclines.int32_overflow", "FoldDeclines.int64_overflow",
                      "FoldDeclines.int64_product_overflow", "FoldDeclines.int64_division_overflow",
                      "FoldDeclines.uint64_overflow", "FoldDeclines.division_by_zero",
                      "FoldDeclines.remainder_by_zero", "FoldDeclines.conversion_out_of_range",
                      "FoldDeclines.negative_to_unsigned", "FoldDeclines.floating_point",
                      "FoldDeclines.concatenation"});
    expect_shapes(environment, "fold-declines.nra",
                  {
                      {"FoldDeclines.int8_overflow", "(return (binary + (integer 127) (integer 1)))"},
                      {"FoldDeclines.int8_negated_minimum", "(return (unary - (integer -128)))"},
                      {"FoldDeclines.int64_division_overflow",
                       "(return (binary / (integer -9223372036854775808) (integer -1)))"},
                      {"FoldDeclines.partially_folded", "(return (binary + (integer 127) (integer 1)))"},
                      {"FoldDeclines.parameter_overflow", "(return (binary + (load value) (integer 127)))"},
                  });
}

NEURA_TEST(structure, simplifies_short_circuits_without_losing_effects) {
    expect_unchanged(environment, "short-circuit.nra", {"ShortCircuit.observe", "ShortCircuit.keep_lazy"});
    expect_shapes(environment, "short-circuit.nra",
                  {
                      {"ShortCircuit.skip_right", "(return (boolean false))"},
                      {"ShortCircuit.either_skips", "(return (boolean true))"},
                      {"ShortCircuit.pure_absorbed", "(return (boolean false))"},
                      {"ShortCircuit.skip_failure", "(return (boolean false))"},
                  });
    auto program = lower_fixture(environment, "short-circuit.nra");
    optimize(program);
    const std::map<std::string_view, std::string> expected{
        {"ShortCircuit.always_right", with_call("(return CALL)")},
        {"ShortCircuit.either_right", with_call("(return CALL)")},
        {"ShortCircuit.identity_right", with_call("(return CALL)")},
        {"ShortCircuit.keep_effect", with_call("(return (short_circuit && CALL {(boolean false)}))")},
        {"ShortCircuit.keep_effect_or", with_call("(return (short_circuit || CALL {(boolean true)}))")},
        {"ShortCircuit.keep_failure",
         "(return (short_circuit && (binary == (binary / (load value) (integer 0)) (integer 1)) "
         "{(boolean false)}))"},
        {"ShortCircuit.nested",
         with_call("(return (short_circuit || (load value) {(short_circuit && CALL {(boolean false)})}))")},
    };
    for (const auto& [function, form] : expected) {
        check_equal(shape(program, body_of(program, function)), form, std::string(function));
    }
}

NEURA_TEST(structure, simplifies_branches_loops_and_unreachable_statements) {
    expect_unchanged(environment, "control-flow.nra", {"ControlFlow.keep_loop_exit"});
    expect_shapes(
        environment, "control-flow.nra",
        {
            {"ControlFlow.select_then",
             "(block {(initialize doubled (binary * (load value) (integer 2))) (return (load doubled))})"},
            {"ControlFlow.select_else", "(return (load value))"},
            {"ControlFlow.skip_loop", "(return (load value))"},
            {"ControlFlow.after_return", "(return (load value))"},
            {"ControlFlow.both_arms_return", "(branch (load flag) {(return (integer 1))} else {(return (integer 2))})"},
            {"ControlFlow.loop_control",
             "(initialize total (integer 0)) (initialize index (integer 0)) "
             "(loop (binary < (load index) (load limit)) {"
             "(store (load index) (binary + (load index) (integer 1))) "
             "(branch (binary == (load index) (integer 2)) {(continue)}) "
             "(branch (binary > (load index) (integer 5)) {(break)}) "
             "(store (load total) (binary + (load total) (load index)))}) "
             "(return (load total))"},
            {"ControlFlow.exposed_infinite_loop", "(loop (boolean true) {})"},
            {"ControlFlow.nested_loops",
             "(initialize count (integer 0)) (loop (boolean true) {(loop (boolean true) {(break)}) "
             "(store (load count) (binary + (load count) (integer 1))) "
             "(branch (binary >= (load count) (load limit)) {(return (load count))})})"},
        });
}

NEURA_TEST(structure, removes_only_pure_discards) {
    expect_unchanged(environment, "state.nra", {"State.bump"});
    expect_shapes(environment, "state.nra",
                  {
                      // A global initializer keeps its root form: written as
                      // 2 + 3, it runs at program start rather than at load.
                      {"State", "(initialize counter (binary + (integer 2) (integer 3)))"},
                      {"State.discards",
                       "(discard (virtual_call bump (receiver State) (integer 1))) "
                       "(discard (virtual_call assert (reference test) (boolean true))) "
                       "(discard (binary + (load value) (integer 1))) (return (load value))"},
                      {"State.failing_discard", "(discard (binary / (load value) (integer 0))) (return (load value))"},
                      {"State.reset_then_read",
                       "(branch (load flag) {(store (load counter (receiver State)) (integer 0))}) "
                       "(return (load counter (receiver State)))"},
                  });
}

NEURA_TEST(structure, rewrites_keep_source_spans) {
    const auto original = lower_fixture(environment, "folding.nra");
    auto optimized = original;
    optimize(optimized);
    // A folded literal takes the span of the expression it replaces.
    const auto& sum = body_of(original, "Folding.sum_literals").front().operands.front();
    const auto& folded = body_of(optimized, "Folding.sum_literals").front().operands.front();
    check(same_span(folded.span, sum.span), "folded literal at " + describe(folded.span) + ", expected " +
                                                describe(sum.span));
    check_equal(folded.span.line, std::size_t{4}, "line of the folded literal");
    // A selected branch keeps the branch's span as a block.
    const auto& branch = body_of(original, "Folding.condition").front();
    const auto& block = body_of(optimized, "Folding.condition").front();
    check(same_span(block.span, branch.span), "selected branch block lost the branch span");

    // An operand that replaces a short-circuit operation keeps its own span.
    const auto lazy = lower_fixture(environment, "short-circuit.nra");
    auto simplified = lazy;
    optimize(simplified);
    const auto& call = body_of(lazy, "ShortCircuit.always_right").front().operands.front().body.front();
    const auto& kept = body_of(simplified, "ShortCircuit.always_right").front().operands.front();
    check(same_span(kept.span, call.span), "forwarded operand lost its span");
}

NEURA_TEST(structure, enabled_passes_run_in_pipeline_order) {
    std::vector<std::string> names;
    for (const auto& pass : optimization_passes()) {
        names.emplace_back(pass.name);
    }
    const std::vector<std::string> order{"fold-constants",
                                         "simplify-short-circuit",
                                         "propagate-copies",
                                         "eliminate-common-subexpressions",
                                         "hoist-loop-invariants",
                                         "simplify-control-flow",
                                         "remove-unreachable",
                                         "remove-dead-stores",
                                         "remove-pure-discards",
                                         "inline-calls"};
    check(names == order, "the pipeline order");
    check(names == OptimizationOptions{}.passes, "all passes are enabled by default");

    auto program = lower_fixture(environment, "control-flow.nra");
    OptimizationOptions options;
    options.passes = {"simplify-control-flow", "fold-constants"};
    const auto report = optimize(program, options);
    check_equal(report.passes.size(), std::size_t{2}, "reported passes");
    check_equal(report.passes[0].name, std::string("fold-constants"), "first reported pass");
    check_equal(report.passes[1].name, std::string("simplify-control-flow"), "second reported pass");
}

NEURA_TEST(structure, single_pass_leaves_other_rewrites_undone) {
    auto program = lower_fixture(environment, "control-flow.nra");
    OptimizationOptions options;
    options.passes = {"fold-constants"};
    const auto report = optimize(program, options);
    check(report.rewrites("fold-constants") > 0, "fold-constants made no rewrites");
    check_equal(shape(program, body_of(program, "ControlFlow.select_else")),
                std::string("(branch (boolean false) {(return (integer 0))}) (return (load value))"),
                "ControlFlow.select_else after fold-constants alone");
    check_equal(report.rounds, std::size_t{2}, "rounds for a single pass");
}

NEURA_TEST(structure, reports_rewrites_and_reaches_a_fixed_point) {
    auto program = lower_fixture(environment, "control-flow.nra");
    const auto report = optimize(program);
    for (const auto* pass : {"fold-constants", "simplify-control-flow", "remove-unreachable"}) {
        check(report.rewrites(pass) > 0, std::string(pass) + " reported no rewrites");
    }
    check(report.rounds >= 2, "the last round must confirm the fixed point");
    const auto again = optimize(program);
    check_equal(again.total_rewrites(), std::size_t{0}, "rewrites when optimizing optimized IR");
    check_equal(again.rounds, std::size_t{1}, "rounds when optimizing optimized IR");
}

NEURA_TEST(structure, rejects_unknown_and_repeated_passes) {
    IrProgram program;
    for (const auto& passes : {std::vector<std::string>{"fold-everything"},
                               std::vector<std::string>{"fold-constants", "fold-constants"}}) {
        OptimizationOptions options;
        options.passes = passes;
        bool rejected = false;
        try {
            optimize(program, options);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "pass list '" + passes.front() + "' was accepted");
    }
}

NEURA_TEST(structure, optimization_is_deterministic_and_preserves_symbols) {
    std::vector<std::vector<fs::path>> inputs;
    for (const auto& file : fixture_files(environment)) {
        inputs.push_back({file});
    }
    inputs.push_back(example_files(environment));
    for (const auto& files : inputs) {
        const auto original = lower_files(files);
        auto first = original;
        auto second = lower_files(files);
        optimize(first);
        optimize(second);
        const auto label = files.size() == 1 ? files.front().filename().string() : "all examples";
        check(printed(first) == printed(second), label + ": repeated optimization printed different IR");
        check(keeps_symbols(original, first), label + ": optimization changed the symbol table");
        check_equal(first.bodies.size(), original.bodies.size(), label + ": body count");
        for (std::size_t i = 0; i < first.bodies.size(); ++i) {
            check_equal(first.bodies[i].owner, original.bodies[i].owner, label + ": body order");
        }
    }
}

// The examples contain no constant expressions or literal conditions. If one
// gains them, update this expectation to the intended optimized form.
// The examples' own code is already in the form the five structural passes
// leave alone; the passes over the values of locals do rewrite it, for
// example by propagating a range's literal step. Standard library bodies
// that the examples import are not part of this check.
NEURA_TEST(structure, examples_are_unchanged) {
    const auto original = lower_files(example_files(environment));
    auto optimized = original;
    OptimizationOptions options;
    options.passes = {"fold-constants", "simplify-short-circuit", "simplify-control-flow", "remove-unreachable",
                      "remove-pure-discards"};
    optimize(optimized, options);
    const auto examples = environment.examples().generic_string() + "/";
    check_equal(optimized.bodies.size(), original.bodies.size(), "body count");
    std::size_t compared = 0;
    for (std::size_t i = 0; i < original.bodies.size(); ++i) {
        const auto& body = original.bodies[i];
        if (!original.symbols[body.owner].span.file.starts_with(examples)) continue;
        ++compared;
        check(shape(original, body.instructions) == shape(optimized, optimized.bodies[i].instructions),
              "the optimizer rewrote " + qualified_name(original, body.owner));
    }
    check(compared > 0, "no example bodies were compared");
}

} // namespace neura::testing

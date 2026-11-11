// Structural assertions for the passes over the values of locals and for
// inlining: where each rewrite applies, where it must not, and what inlining
// records. Behavior is compared in behavior_tests.cpp.
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/attributes.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "support.hpp"

#include "concurrency_support.hpp"

namespace neura::testing {
namespace {

struct Expectation {
    std::string_view function;
    std::string_view shape;
};

IrProgram prepared(const Environment& environment, std::string_view file) {
    auto program = lower_fixture(environment, file);
    materialize_reference_storage(program);
    specialize(program);
    return program;
}

OptimizationReport only(IrProgram& program, std::string_view pass) {
    OptimizationOptions options;
    options.passes = {std::string(pass)};
    return optimize(program, options);
}

// Runs `pass` alone on the lowered `file` and compares the listed bodies.
void expect_pass(const Environment& environment, std::string_view file, std::string_view pass,
                 std::initializer_list<Expectation> rewritten, std::initializer_list<std::string_view> unchanged) {
    const auto original = lower_fixture(environment, file);
    auto program = original;
    only(program, pass);
    for (const auto& expectation : rewritten) {
        check_equal(shape(program, body_of(program, expectation.function)), expectation.shape,
                    std::string(pass) + " on " + std::string(expectation.function));
    }
    for (const auto function : unchanged) {
        check_equal(shape(program, body_of(program, function)), shape(original, body_of(original, function)),
                    std::string(pass) + " must leave " + std::string(function));
    }
}

} // namespace

NEURA_TEST(structure, propagates_copies_of_unchanged_values) {
    expect_pass(environment, "dataflow.nra", "propagate-copies",
                {{"Dataflow.propagate_constant", "(return (binary + (load value) (integer 3)))"},
                 {"Dataflow.propagate_chain", "(return (binary * (load value) (integer 2)))"},
                 {"Dataflow.propagate_into_loop",
                  "(initialize total (integer 0)) (initialize index (integer 0)) (loop (binary < (load index) (load "
                  "limit)) {(initialize current (load index)) (store (load total) (binary + (load total) (load "
                  "current))) (store (load index) (binary + (load index) (integer 1)))}) (return (load total))"},
                 {"Dataflow.propagate_wide", "(return (binary + (load value) (integer 18446744073709551615)))"}},
                {"Dataflow.keep_stored_local", "Dataflow.keep_copy_of_changed_source",
                 "Dataflow.keep_copy_of_changed_parameter"});
}

NEURA_TEST(structure, reuses_values_whose_operands_are_unchanged) {
    expect_pass(environment, "dataflow.nra", "eliminate-common-subexpressions",
                {{"Dataflow.reuse_across_call",
                  "(initialize product (binary * (load left) (load right))) (discard (virtual_call mark (receiver "
                  "Dataflow) (integer 1))) (initialize again (load product)) (return (binary - (load again) (load "
                  "product)))"},
                 {"Dataflow.reuse_nested",
                  "(initialize sum (binary + (load left) (load right))) (branch (binary < (load left) (load right)) "
                  "{(return (binary / (load sum) (integer 2)))}) (return (binary - (binary + (convert (virtual_call "
                  "mark (receiver Dataflow) (integer 1))) (load sum)) (load sum)))"},
                 {"Dataflow.reuse_in_loop",
                  "(initialize difference (binary - (load left) (load right))) (initialize total (integer 0)) "
                  "(initialize index (integer 0)) (loop (binary < (load index) (integer 3)) {(store (load total) "
                  "(binary + (load total) (load difference))) (store (load index) (binary + (load index) (integer "
                  "1)))}) (return (binary - (load total) (load difference)))"},
                 {"Dataflow.reuse_unsigned_difference",
                  "(initialize difference (binary - (load left) (load right))) (return (binary + (binary / (load "
                  "difference) (integer 2)) (binary / (load difference) (integer 2))))"}},
                {"Dataflow.keep_after_operand_write", "Dataflow.keep_after_local_write",
                 "Dataflow.keep_after_global_write", "Dataflow.keep_after_element_write"});
}

NEURA_TEST(structure, hoists_only_pure_and_total_invariants) {
    expect_pass(environment, "dataflow.nra", "hoist-loop-invariants",
                {{"Dataflow.hoist_invariant",
                  "(initialize total (integer 0)) (initialize index (integer 0)) (initialize small (binary < (load "
                  "base) (integer 10))) (loop (binary < (load index) (load limit)) {(branch (load small) {(store "
                  "(load total) (binary + (load total) (integer 1)))}) (store (load index) (binary + (load index) "
                  "(integer 1)))}) (return (load total))"}},
                {"Dataflow.keep_trapping_invariant", "Dataflow.keep_variant", "Dataflow.keep_zero_trip_trap"});
}

NEURA_TEST(structure, removes_dead_stores_to_value_locals) {
    expect_pass(environment, "dataflow.nra", "remove-dead-stores",
                {{"Dataflow.drop_overwritten",
                  "(initialize result (integer 0)) (discard (binary + (load value) (integer 1))) (store (load "
                  "result) (load value)) (return (load result))"},
                 {"Dataflow.drop_unread",
                  "(discard (virtual_call mark (receiver Dataflow) (load value))) (return (load value))"},
                 {"Dataflow.keep_dead_wide_product",
                  "(discard (binary * (load left) (load right))) (return (load left))"}},
                {"Dataflow.keep_read_between", "Dataflow.keep_store_before_break", "Dataflow.keep_reference_store",
                 "Dataflow.keep_text_store"});
}

NEURA_TEST(structure, inlines_calls_that_cannot_trap_call_or_allocate) {
    const auto original = prepared(environment, "inlining.nra");
    auto program = original;
    const auto report = only(program, "inline-calls");
    check(report.rewrites("inline-calls") > 0, "nothing was inlined");
    const std::initializer_list<Expectation> rewritten{
        // A returned call: the callee's returns return from the caller.
        {"Inlining.maximum",
         "(block {(initialize $inline.larger.left (load left)) (initialize $inline.larger.right (load right)) "
         "(branch (binary > (load $inline.larger.left) (load $inline.larger.right)) {(return (load "
         "$inline.larger.left))}) (return (load $inline.larger.right))})"},
        // Early returns in an expression: the body runs once in a loop.
        {"Inlining.maximum_in_expression",
         "(return (binary == (scoped_value {(initialize $inline.larger.left (load left)) (initialize "
         "$inline.larger.right (load right)) (initialize $inline.larger.result (integer 0)) (loop (boolean true) "
         "{(branch (binary > (load $inline.larger.left) (load $inline.larger.right)) {(store (load "
         "$inline.larger.result) (load $inline.larger.left)) (break)}) (store (load $inline.larger.result) (load "
         "$inline.larger.right)) (break)}) (load $inline.larger.result)}) (load left)))"},
        // Arguments initialize the parameters once, left to right.
        {"Inlining.argument_order",
         "(block {(initialize $inline.pick.flag (binary > (load left) (load right))) (initialize "
         "$inline.pick.left (virtual_call mark (receiver Inlining) (load left))) (initialize $inline.pick.right "
         "(virtual_call mark (receiver Inlining) (load right))) (branch (load $inline.pick.flag) {(return (load "
         "$inline.pick.left))} else {(return (load $inline.pick.right))})})"},
        // A discarded void call whose body names the module's receiver.
        {"Inlining.remember_discarded",
         "(block {(initialize $inline.remember.flag (load flag)) (branch (load $inline.remember.flag) {(store "
         "(load seen (reference Inlining)) (boolean true))})}) (block {(initialize $inline.pick.flag (load flag)) "
         "(initialize $inline.pick.left (integer 1)) (initialize $inline.pick.right (integer 2)) (loop (boolean "
         "true) {(branch (load $inline.pick.flag) {(discard (load $inline.pick.left)) (break)} else {(discard "
         "(load $inline.pick.right)) (break)})})}) (return (load seen (receiver Inlining)))"},
    };
    for (const auto& expectation : rewritten) {
        check_equal(shape(program, body_of(program, expectation.function)), expectation.shape,
                    std::string(expectation.function));
    }
    // A callee that may trap, is recursive, requests an effect, returns a
    // reference, or exceeds the size budget stays a call.
    for (const auto* function : {"Inlining.keep_trapping_callee", "Inlining.keep_recursive_callee",
                                 "Inlining.keep_effectful_callee", "Inlining.keep_reference_result",
                                 "Inlining.ranges"}) {
        check_equal(shape(program, body_of(program, function)), shape(original, body_of(original, function)),
                    std::string(function) + " must keep its call");
    }
    // Only calls are inlined; callees keep their bodies.
    check_equal(shape(program, body_of(program, "Inlining.larger")),
                shape(original, body_of(original, "Inlining.larger")), "the callee's own body");
}

NEURA_TEST(structure, inlining_records_sites_and_keeps_spans) {
    const auto original = prepared(environment, "inlining.nra");
    auto program = original;
    only(program, "inline-calls");
    const auto caller = find_symbol(program, "Inlining.maximum");
    const auto callee = find_symbol(program, "Inlining.larger");
    const auto& before = body_of(original, "Inlining.maximum").front().operands.front();
    const auto& block = body_of(program, "Inlining.maximum").front();
    check(block.inline_site != 0 && block.inline_site <= program.inline_sites.size(), "the block has no site");
    const auto& site = program.inline_sites[block.inline_site - 1];
    check(site.callee == callee && site.caller == caller && site.parent == 0, "the site names the call");
    check(same_span(site.call, before.span), "the site keeps the call's span");
    check(same_span(block.span, before.span), "the block takes the call's span");
    // The inlined branch keeps the callee's span and records the site.
    const auto& branch = block.body.at(2);
    const auto& original_branch = body_of(original, "Inlining.larger").front();
    check(branch.op == IrOp::Branch && same_span(branch.span, original_branch.span),
          "the inlined branch keeps the callee's span: " + describe(branch.span));
    check(branch.inline_site == block.inline_site, "the inlined branch records the site");
    // Each inlined local is a new local of the caller.
    const auto local = branch.operands.front().operands.front().symbol;
    check(local >= original.symbols.size() && program.symbols[local].owner == caller &&
              program.symbols[local].kind == SymbolKind::Local,
          "the inlined parameter is a new local of the caller");
    // Nothing is recorded without inlining.
    check(original.inline_sites.empty(), "sites before optimization");
}

NEURA_TEST(structure, inline_directives_override_the_budget) {
    const auto original = prepared(environment, "inlining.nra");
    {
        auto program = original;
        set_attribute(program.symbols[find_symbol(program, "Inlining.is_digit")], AttributeId::InlineDirective,
                      attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Never)));
        only(program, "inline-calls");
        check_equal(shape(program, body_of(program, "Inlining.digits")),
                    shape(original, body_of(original, "Inlining.digits")), "Never keeps the calls");
    }
    {
        auto program = original;
        set_attribute(program.symbols[find_symbol(program, "Inlining.in_ranges")], AttributeId::InlineDirective,
                      attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Always)));
        only(program, "inline-calls");
        check(body_of(program, "Inlining.ranges").front().op == IrOp::Block, "Always inlines past the budget");
    }
    {
        // Always does not lift the other conditions.
        auto program = original;
        set_attribute(program.symbols[find_symbol(program, "Inlining.add")], AttributeId::InlineDirective,
                      attribute_unsigned(static_cast<std::uint64_t>(InlineDirective::Always)));
        only(program, "inline-calls");
        check_equal(shape(program, body_of(program, "Inlining.keep_trapping_callee")),
                    shape(original, body_of(original, "Inlining.keep_trapping_callee")),
                    "Always keeps a callee that may trap");
    }
}

// Every call in a program with tasks is a poll, which inlining would remove.
NEURA_TEST(structure, programs_with_tasks_are_not_inlined) {
    std::size_t programs = 0;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        auto program = prepare_concurrent(fixture.path);
        if (!uses_tasks(program)) continue;
        const auto report = only(program, "inline-calls");
        check_equal(report.rewrites("inline-calls"), std::size_t{0}, fixture.name);
        ++programs;
    }
    check(programs > 0, "no concurrent fixtures");
}

} // namespace neura::testing

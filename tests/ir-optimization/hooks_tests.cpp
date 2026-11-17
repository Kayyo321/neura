// Class hook events (docs/design/Hooks030.md): the derivation of each row of
// its table on the linked, specialized program, the verifier's rule that
// every event is wrapped exactly once, the effect summaries, and the
// optimizer passes that must leave every event in place.
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/class_hooks.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/purity.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "support.hpp"

#include <map>
#include <utility>

namespace neura::testing {
namespace {

IrProgram elaborated(const Environment& environment, std::string_view file) {
    auto program = lower_files({environment.source_dir / "tests" / "ir-optimization" / "hooks" / std::string(file)});
    materialize_reference_storage(program);
    specialize(program);
    elaborate_class_hooks(program);
    return program;
}

struct Count {
    std::size_t reads = 0;
    std::size_t admissions = 0;
    bool operator==(const Count&) const = default;
};

std::ostream& operator<<(std::ostream& output, const Count& count) {
    return output << count.reads << " reads, " << count.admissions << " admissions";
}

void count(const std::vector<IrNode>& nodes, Count& into) {
    for (const auto& node : nodes) {
        if (node.op == IrOp::HookRead) ++into.reads;
        if (node.op == IrOp::HookAdmit) ++into.admissions;
        count(node.operands, into);
        count(node.body, into);
        count(node.alternative, into);
    }
}

Count events(const IrProgram& program, SymbolId function) {
    Count result;
    for (const auto& body : program.bodies)
        if (body.owner == function) count(body.instructions, result);
    return result;
}

Count events(const IrProgram& program, std::string_view name) { return events(program, find_symbol(program, name)); }

// The events inside loops: a hoisted event would leave its loop.
void count_in_loops(const std::vector<IrNode>& nodes, bool inside, Count& into) {
    for (const auto& node : nodes) {
        if (inside && node.op == IrOp::HookRead) ++into.reads;
        if (inside && node.op == IrOp::HookAdmit) ++into.admissions;
        const bool loop = inside || node.op == IrOp::Loop;
        count_in_loops(node.operands, loop, into);
        count_in_loops(node.body, loop, into);
        count_in_loops(node.alternative, loop, into);
    }
}

Count loop_events(const IrProgram& program, std::string_view name) {
    Count result;
    const auto function = find_symbol(program, name);
    for (const auto& body : program.bodies)
        if (body.owner == function) count_in_loops(body.instructions, false, result);
    return result;
}

Count total(const IrProgram& program) {
    Count result;
    for (const auto& body : program.bodies) count(body.instructions, result);
    return result;
}

} // namespace

NEURA_TEST(hooks, each_row_of_the_derivation_table_has_its_events) {
    const auto program = elaborated(environment, "derivation.nra");
    check(program.hooks_elaborated, "the program is elaborated");
    const auto issues = verify_ir(program);
    check(issues.empty(), issues.empty() ? "" : issues.front().message);
    const std::vector<std::pair<std::string_view, Count>> rows{
        {"Rows.load_local", {1, 1}},      // read s, initialize t
        {"Rows.load_global", {1, 1}},     // read global, admit the result
        {"Rows.field", {1, 1}},           // read h.held (h is not policy-carrying)
        {"Rows.accessor_read", {1, 1}},   // the getter adapter's result, the result
        {"Rows.accessor_write", {1, 1}},  // read s, one admission to the field
        {"Rows.index", {1, 1}},
        {"Rows.project", {1, 1}},
        {"Rows.enum_project", {1, 1}},    // the payload, the binding `found`
        {"Rows.remove_at", {0, 0}},       // remove_at yields no element
        {"Rows.store", {1, 1}},
        {"Rows.append", {1, 1}},
        {"Rows.collection", {2, 2}},
        {"Rows.closure", {1, 1}},         // read s, admit the capture
        {"Rows.call", {2, 2}},            // in written order, each before the next
        {"Rows.receiver", {2, 1}},        // the receiver is read but not admitted
        {"Rows.closure_call", {1, 1}},
        {"Rows.allocate", {1, 1}},        // a constructor argument is admitted
        {"Rows.tuple", {1, 0}},           // building a tuple is no event
        {"Rows.equality", {2, 0}},
        {"Rows.for_each", {1, 1}},        // the element read and the binding; $items is a temporary
        {"Rows.discard", {1, 0}},
        {"Rows.plain", {0, 0}},           // Holder is not policy-carrying
        {"Rows.take", {0, 0}},
        {"Holder.keep", {1, 1}},
        {"Derived.init", {1, 1}},         // the argument to the base constructor
    };
    for (const auto& [name, expected] : rows) check_equal(events(program, name), expected, name);
    // The accessor adapters and the hook functions have no events of their
    // own: their parameters, results and own storage are part of the event
    // that reaches them.
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind == SymbolKind::Function &&
            (item.name == "$get_owner" || item.name == "$set_owner" || item.name.starts_with("$class_")))
            check_equal(events(program, id), Count{}, item.name);
    }
    // The module's global is admitted where it is initialized.
    check_equal(events(program, find_symbol(program, "Rows")), Count{0, 1}, "the module body");
}

NEURA_TEST(hooks, a_program_without_clauses_is_unchanged) {
    auto program = lower_files({environment.source_dir / "tests" / "ir-optimization" / "fixtures" / "control-flow.nra"});
    materialize_reference_storage(program);
    specialize(program);
    const auto before = write_nir(program);
    elaborate_class_hooks(program);
    check(!program.hooks_elaborated, "nothing to elaborate");
    check(write_nir(program) == before, "the program keeps its bytes");
}

NEURA_TEST(hooks, the_verifier_requires_each_event_wrapped_exactly_once) {
    auto program = elaborated(environment, "derivation.nra");
    const auto function = find_symbol(program, "Rows.load_local");
    auto* body = &program.bodies.front();
    for (auto& candidate : program.bodies)
        if (candidate.owner == function) body = &candidate;
    // Unwrapping one event, or wrapping one twice, is reported.
    auto missing = program;
    for (auto& candidate : missing.bodies) {
        if (candidate.owner != function) continue;
        candidate.instructions.front() = strip_class_hooks(candidate.instructions.front());
    }
    check(!verify_ir(missing).empty(), "an unwrapped event is reported");
    auto doubled = program;
    for (auto& candidate : doubled.bodies) {
        if (candidate.owner != function) continue;
        auto& operand = candidate.instructions.front().operands.front();
        IrNode extra{};
        extra.op = IrOp::HookAdmit;
        extra.type = operand.type;
        extra.span = operand.span;
        extra.operands.push_back(operand);
        operand = std::move(extra);
    }
    check(!verify_ir(doubled).empty(), "an event wrapped twice is reported");
    // A hook node in a program that is not elaborated is reported, and the
    // .nir writer refuses one.
    auto unmarked = program;
    unmarked.hooks_elaborated = false;
    check(!verify_ir(unmarked).empty(), "a hook node outside an elaborated program is reported");
    try {
        (void)write_nir(program);
        fail("the .nir writer accepted an event");
    } catch (const NirError& error) {
        check(std::string(error.what()).find("not written to .nir") != std::string::npos, error.what());
    }
    check(body->owner == function, "the body was found");
}

NEURA_TEST(hooks, summaries_record_the_hook_fact_and_the_clause_effects) {
    const auto program = elaborated(environment, "derivation.nra");
    const PurityAnalysis purity(program);
    const auto* summary = purity.summary(find_symbol(program, "Rows.load_local"));
    check(summary && summary->hooks.has_value(), "a function with an event has the hook fact");
    check(summary->hooks->operation.find("hook of 'Session'") != std::string::npos, summary->hooks->operation);
    const auto* plain = purity.summary(find_symbol(program, "Rows.plain"));
    check(plain && !plain->hooks, "a function without an event has none");
}

NEURA_TEST(hooks, every_pass_leaves_every_event_in_place) {
    const auto reference = elaborated(environment, "passes.nra");
    const auto before = total(reference);
    check(before.reads > 0 && before.admissions > 0, "the fixture has events");
    for (const auto& pass : all_pass_names()) {
        auto program = reference;
        OptimizationOptions options;
        options.passes = {pass};
        optimize(program, options);
        check_equal(total(program), before, "after " + pass);
        const auto issues = verify_ir(program);
        check(issues.empty(), pass + ": " + (issues.empty() ? "" : issues.front().message));
    }
    // Case 55: the read of an unchanging local stays in its loop, one
    // event per iteration.
    const auto in_loop = loop_events(reference, "Passes.loop");
    check(in_loop.reads > 0, "the loop of Passes.loop has a read event");
    for (const auto& pass : all_pass_names()) {
        auto program = reference;
        OptimizationOptions options;
        options.passes = {pass};
        optimize(program, options);
        check_equal(loop_events(program, "Passes.loop"), in_loop, "events inside the loop after " + pass);
    }
    auto program = reference;
    optimize(program);
    check_equal(total(program), before, "after every pass");
    check_equal(loop_events(program, "Passes.loop"), in_loop, "events inside the loop after every pass");
    for (const auto* name : {"Passes.twice", "Passes.loop", "Passes.discard", "Passes.dead", "Passes.caller",
                             "Passes.copy", "Passes.constant"}) {
        check_equal(events(program, name), events(reference, name), name);
    }
}

NEURA_TEST(hooks, a_policy_carrying_reference_cannot_cross_a_host_binding) {
    // No bodyless function of the standard library takes a class, so the
    // test makes `Rows.take` a host binding (E0546, case 49).
    auto program = lower_files({environment.source_dir / "tests" / "ir-optimization" / "hooks" / "derivation.nra"});
    materialize_reference_storage(program);
    specialize(program);
    const auto take = find_symbol(program, "Rows.take");
    program.symbols[take].external = true;
    program.symbols[take].has_body = false;
    program.symbols[take].host_binding = IrHostBinding{"test.take", 1};
    std::erase_if(program.bodies, [&](const IrBody& body) { return body.owner == take; });
    try {
        elaborate_class_hooks(program);
        fail("a host binding took a policy-carrying reference");
    } catch (const Diagnostic& error) {
        check_equal(std::string(error.code()), std::string("E0546"), "the code");
        check(std::string(error.what()).find("'Session' references carry a class policy") != std::string::npos,
              error.what());
    }
}

} // namespace neura::testing

// NeuraIR verifier checks: lowered and optimized programs are accepted, and
// each deliberately malformed program is rejected with a specific issue.
#include "concurrency_support.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "support.hpp"

#include <algorithm>
#include <functional>

namespace neura::testing {
namespace {

std::string issues_text(const std::vector<VerificationIssue>& issues) {
    std::string text;
    for (const auto& issue : issues) {
        text += "\n  " + describe(issue.span) + ": " + issue.message;
    }
    return text;
}

// Lowers `file`, applies `mutate`, and requires an issue containing `message`.
void expect_issue(const Environment& environment, std::string_view file, std::string_view message,
                  const std::function<void(IrProgram&)>& mutate) {
    auto program = lower_fixture(environment, file);
    check(verify_ir(program).empty(), "fixture must verify before mutation");
    mutate(program);
    const auto issues = verify_ir(program);
    const bool found = std::ranges::any_of(issues, [&](const VerificationIssue& issue) {
        return issue.message.find(message) != std::string::npos;
    });
    check(found, "expected an issue containing '" + std::string(message) + "', found:" + issues_text(issues));
}

IrNode* find_first(std::vector<IrNode>& nodes, IrOp op) {
    for (auto& node : nodes) {
        if (node.op == op) {
            return &node;
        }
        for (auto* region : {&node.operands, &node.body, &node.alternative}) {
            if (auto* found = find_first(*region, op)) {
                return found;
            }
        }
    }
    return nullptr;
}

// The first node with `op` in depth-first, pre-order traversal.
IrNode& first(std::vector<IrNode>& nodes, IrOp op) {
    auto* found = find_first(nodes, op);
    check(found != nullptr, "no node with the requested operation");
    return *found;
}

// The first scheduler call with the given operation name, wherever it is
// written (the library's Task methods hold them).
IrNode* find_scheduler_call(std::vector<IrNode>& nodes, std::string_view operation) {
    for (auto& node : nodes) {
        if (node.op == IrOp::SchedulerCall && node.text == operation) return &node;
        for (auto* region : {&node.operands, &node.body, &node.alternative}) {
            if (auto* found = find_scheduler_call(*region, operation)) return found;
        }
    }
    return nullptr;
}

IrNode& scheduler_call(IrProgram& program, std::string_view operation) {
    for (auto& body : program.bodies) {
        if (auto* found = find_scheduler_call(body.instructions, operation)) return *found;
    }
    fail("no scheduler call named " + std::string(operation));
}

constexpr std::string_view task_fixture = "concurrency/discard.nra";

IrType type_named(TypeKind kind, std::string name) { return {kind, std::move(name), no_symbol, {}}; }

} // namespace

NEURA_TEST(verifier, rejects_unsatisfied_bounds_and_type_argument_counts) {
    // polymorphism.nra calls render_twice<Alert>, whose T is bounded by
    // Renderable. A .nir file could name any type there.
    auto program = lower_files({environment.examples() / "types" / "polymorphism.nra"});
    check(verify_ir(program).empty(), "the example verifies");
    auto bad_bound = program;
    auto& body = bad_bound.bodies.back().instructions;
    std::function<IrNode*(std::vector<IrNode>&)> generic_call = [&](std::vector<IrNode>& nodes) -> IrNode* {
        for (auto& node : nodes) {
            if ((node.op == IrOp::Call || node.op == IrOp::VirtualCall) && !node.type_arguments.empty() &&
                bad_bound.symbols[node.symbol].name == "render_twice") {
                return &node;
            }
            for (auto* region : {&node.operands, &node.body, &node.alternative}) {
                if (auto* found = generic_call(*region)) {
                    return found;
                }
            }
        }
        return nullptr;
    };
    auto* call = generic_call(body);
    check(call != nullptr, "the example calls render_twice");
    auto missing = bad_bound;
    call->type_arguments.front() = {TypeKind::Integer, "int32", no_symbol, {}};
    auto issues = verify_ir(bad_bound);
    check(std::ranges::any_of(issues, [](const VerificationIssue& issue) {
              return issue.message.find("type argument int32 does not satisfy the bound Renderable") != std::string::npos;
          }),
          "an unsatisfied bound is reported:" + issues_text(issues));
    auto* missing_call = generic_call(missing.bodies.back().instructions);
    missing_call->type_arguments.clear();
    issues = verify_ir(missing);
    check(std::ranges::any_of(issues, [](const VerificationIssue& issue) {
              return issue.message.find("passes 0 type argument(s)") != std::string::npos;
          }),
          "a missing type argument is reported:" + issues_text(issues));
}

NEURA_TEST(verifier, accepts_lowered_and_optimized_programs) {
    std::vector<std::vector<fs::path>> inputs;
    for (const auto& file : fixture_files(environment)) {
        inputs.push_back({file});
    }
    inputs.push_back(example_files(environment));
    for (const auto& files : inputs) {
        auto program = lower_files(files);
        const auto label = files.size() == 1 ? files.front().filename().string() : "all examples";
        auto issues = verify_ir(program);
        check(issues.empty(), label + " before optimization:" + issues_text(issues));
        optimize(program);
        issues = verify_ir(program);
        check(issues.empty(), label + " after optimization:" + issues_text(issues));
    }
}

NEURA_TEST(verifier, rejects_mismatched_binary_operands) {
    expect_issue(environment, "fold-declines.nra", "binary operands have different types", [](IrProgram& program) {
        first(body_of(program, "FoldDeclines.int8_overflow"), IrOp::Binary).operands[1].type.name = "int32";
    });
}

NEURA_TEST(verifier, rejects_out_of_range_literal) {
    expect_issue(environment, "folding.nra", "outside the range of int8", [](IrProgram& program) {
        first(body_of(program, "Folding.int8_upper"), IrOp::Integer).text = "128";
    });
}

NEURA_TEST(verifier, rejects_non_bool_condition) {
    expect_issue(environment, "control-flow.nra", "branch condition has type", [](IrProgram& program) {
        auto& branch = first(body_of(program, "ControlFlow.both_arms_return"), IrOp::Branch);
        branch.operands.front() = first(body_of(program, "ControlFlow.both_arms_return"), IrOp::Integer);
    });
}

NEURA_TEST(verifier, rejects_loop_control_outside_a_loop) {
    expect_issue(environment, "control-flow.nra", "loop control appears outside a loop", [](IrProgram& program) {
        auto& body = body_of(program, "ControlFlow.select_else");
        auto control = body.back();
        control.op = IrOp::Break;
        control.operands.clear();
        body.insert(body.begin(), control);
    });
}

NEURA_TEST(verifier, rejects_local_read_before_initialization) {
    expect_issue(environment, "control-flow.nra", "is not preceded by its initialization", [](IrProgram& program) {
        auto& body = body_of(program, "ControlFlow.loop_control");
        body.erase(body.begin());
    });
}

NEURA_TEST(verifier, rejects_repeated_initialization) {
    expect_issue(environment, "control-flow.nra", "initialized more than once", [](IrProgram& program) {
        auto& body = body_of(program, "ControlFlow.loop_control");
        body.insert(body.begin() + 1, body.front());
    });
}

NEURA_TEST(verifier, rejects_parameter_of_another_function) {
    expect_issue(environment, "control-flow.nra", "outside its function", [](IrProgram& program) {
        const auto other = program.symbols[find_symbol(program, "ControlFlow.select_else")].parameters.front();
        first(body_of(program, "ControlFlow.skip_loop"), IrOp::Return).operands.front().symbol = other;
    });
}

NEURA_TEST(verifier, rejects_dangling_symbol) {
    expect_issue(environment, "control-flow.nra", "missing symbol", [](IrProgram& program) {
        first(body_of(program, "ControlFlow.skip_loop"), IrOp::Return).operands.front().symbol = 1'000'000;
    });
}

NEURA_TEST(verifier, rejects_malformed_short_circuit) {
    expect_issue(environment, "short-circuit.nra", "exactly one lazily evaluated operand", [](IrProgram& program) {
        first(body_of(program, "ShortCircuit.keep_lazy"), IrOp::ShortCircuit).body.clear();
    });
}

NEURA_TEST(verifier, rejects_missing_return_value) {
    expect_issue(environment, "folding.nra", "return expects 1 operand(s)", [](IrProgram& program) {
        first(body_of(program, "Folding.sum_literals"), IrOp::Return).operands.clear();
    });
}

NEURA_TEST(verifier, rejects_function_that_can_finish_without_returning) {
    expect_issue(environment, "folding.nra", "can finish without returning", [](IrProgram& program) {
        body_of(program, "Folding.sum_literals").clear();
    });
}

NEURA_TEST(verifier, rejects_expression_in_statement_region) {
    expect_issue(environment, "folding.nra", "expression operation appears in a statement region",
                 [](IrProgram& program) {
                     auto& body = body_of(program, "Folding.sum_literals");
                     body.insert(body.begin(), first(body, IrOp::Integer));
                 });
}

NEURA_TEST(verifier, rejects_missing_span) {
    expect_issue(environment, "folding.nra", "no source span", [](IrProgram& program) {
        first(body_of(program, "Folding.sum_literals"), IrOp::Integer).span.file.clear();
    });
}

NEURA_TEST(verifier, failures_name_the_boundary_and_location) {
    auto program = lower_fixture(environment, "folding.nra");
    auto& literal = first(body_of(program, "Folding.int8_upper"), IrOp::Integer);
    literal.text = "128";
    const auto expected = literal.span;
    for (const bool through_optimizer : {false, true}) {
        bool thrown = false;
        try {
            if (through_optimizer) {
                optimize(program);
            } else {
                require_valid_ir(program, "before optimization");
            }
        } catch (const Diagnostic& error) {
            thrown = true;
            const std::string message = error.what();
            check(message.starts_with("NeuraIR verification failed before optimization: "), message);
            check(same_span(error.span, expected), "diagnostic at " + describe(error.span));
        }
        check(thrown, "invalid IR was accepted");
    }
}

// ---- Scoped tasks (NeuraIR 1.9) ----------------------------------------------

NEURA_TEST(verifier, accepts_concurrent_fixtures_before_and_after_specialization) {
    std::size_t spawns = 0;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        auto program = lower_files({fixture.path});
        auto issues = verify_ir(program);
        check(issues.empty(), fixture.name + " as lowered:" + issues_text(issues));
        // Before specialization a spawn starts a closure value; afterward it
        // starts the allocation of the closure's capture layout.
        bool closures = false;
        const std::function<void(const IrNode&)> look = [&](const IrNode& node) {
            if (node.op == IrOp::Spawn) {
                ++spawns;
                closures = closures || node.operands.front().op == IrOp::MakeClosure;
            }
            for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
                for (const auto& child : *region) look(child);
            }
        };
        for (const auto& body : program.bodies) {
            for (const auto& instruction : body.instructions) look(instruction);
        }
        check(closures, fixture.name + ": lowering should leave the spawned values as closures");
        materialize_reference_storage(program);
        specialize(program);
        issues = verify_ir(program);
        check(issues.empty(), fixture.name + " after specialization:" + issues_text(issues));
        optimize(program);
        issues = verify_ir(program);
        check(issues.empty(), fixture.name + " after optimization:" + issues_text(issues));
    }
    check(spawns > 0, "the fixtures hold no spawn");
}

NEURA_TEST(verifier, rejects_spawn_outside_a_task_scope) {
    expect_issue(environment, task_fixture, "spawn appears outside a task scope of its function body",
                 [](IrProgram& program) {
                     auto& body = body_of(program, "Main.main");
                     const auto scope = std::ranges::find(body, IrOp::TaskScope, &IrNode::op);
                     check(scope != body.end(), "main has a scope");
                     auto inner = scope->body;
                     const auto at = body.erase(scope) - body.begin();
                     body.insert(body.begin() + at, inner.begin(), inner.end());
                 });
}

NEURA_TEST(verifier, rejects_spawn_whose_operand_is_not_a_function_value_without_arguments) {
    expect_issue(environment, task_fixture, "spawn requires the function value it starts as its operand",
                 [](IrProgram& program) {
                     auto& spawn = first(body_of(program, "Main.main"), IrOp::Spawn);
                     spawn.operands.front() = IrNode{IrOp::Integer, type_named(TypeKind::Integer, "uint64"),
                                                     spawn.span, no_symbol, "1", {}, {}, {}, {}};
                 });
    expect_issue(environment, task_fixture, "spawn requires a function value that takes no arguments",
                 [](IrProgram& program) {
                     auto& closure = first(body_of(program, "Main.main"), IrOp::Spawn).operands.front();
                     check(closure.op == IrOp::MakeClosure, "the spawned value is a closure");
                     // A function value type lists its parameter types and then its result.
                     closure.type.arguments.insert(closure.type.arguments.begin(),
                                                   type_named(TypeKind::Integer, "int32"));
                 });
    expect_issue(environment, task_fixture, "a spawned function value returns a value", [](IrProgram& program) {
        auto& closure = first(body_of(program, "Main.main"), IrOp::Spawn).operands.front();
        closure.type.arguments.front() = type_named(TypeKind::Void, "void");
    });
    expect_issue(environment, task_fixture, "spawn expects 1 operand(s)", [](IrProgram& program) {
        first(body_of(program, "Main.main"), IrOp::Spawn).operands.clear();
    });
}

NEURA_TEST(verifier, rejects_spawn_whose_type_is_not_uint64) {
    for (const auto* wrong : {"int32", "uint32", "int64"}) {
        expect_issue(environment, task_fixture, "spawn result must be uint64", [&](IrProgram& program) {
            first(body_of(program, "Main.main"), IrOp::Spawn).type = type_named(TypeKind::Integer, wrong);
        });
    }
    expect_issue(environment, task_fixture, "spawn result must be uint64", [](IrProgram& program) {
        first(body_of(program, "Main.main"), IrOp::Spawn).type = type_named(TypeKind::Bool, "bool");
    });
}

NEURA_TEST(verifier, rejects_malformed_task_scopes) {
    expect_issue(environment, task_fixture, "task scope expects 0 operand(s)", [](IrProgram& program) {
        auto& scope = first(body_of(program, "Main.main"), IrOp::TaskScope);
        scope.operands.push_back(IrNode{IrOp::Boolean, type_named(TypeKind::Bool, "bool"), scope.span, no_symbol,
                                        "true", {}, {}, {}, {}});
    });
    expect_issue(environment, task_fixture, "a task scope has an else region", [](IrProgram& program) {
        auto& scope = first(body_of(program, "Main.main"), IrOp::TaskScope);
        scope.alternative = scope.body;
    });
    expect_issue(environment, task_fixture, "a task scope appears outside a function body", [](IrProgram& program) {
        // A statement of a file body runs outside every function.
        auto scope = first(body_of(program, "Main.main"), IrOp::TaskScope);
        scope.body.clear();
        SymbolId file = no_symbol;
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].kind == SymbolKind::File && program.symbols[id].library_path.empty()) {
                file = id;
            }
        }
        check(file != no_symbol, "the fixture has a file symbol");
        program.bodies.push_back({file, {scope}});
    });
    expect_issue(environment, task_fixture, "a task scope names a handle", [](IrProgram& program) {
        auto& scope = first(body_of(program, "Main.main"), IrOp::TaskScope);
        scope.symbol = static_cast<SymbolId>(program.symbols.size() - 1);
    });
}

NEURA_TEST(verifier, rejects_malformed_scheduler_calls) {
    expect_issue(environment, task_fixture, "scheduler call names unknown operation 'cancel'", [](IrProgram& program) {
        scheduler_call(program, "join").text = "cancel";
    });
    expect_issue(environment, task_fixture, "scheduler call names unknown operation ''", [](IrProgram& program) {
        scheduler_call(program, "is_finished").text.clear();
    });
    // Arity: join and is_finished take the task number alone.
    expect_issue(environment, task_fixture, "scheduler call expects 1 operand(s), found 0", [](IrProgram& program) {
        scheduler_call(program, "join").operands.clear();
    });
    expect_issue(environment, task_fixture, "scheduler call expects 1 operand(s), found 2", [](IrProgram& program) {
        auto& call = scheduler_call(program, "is_finished");
        call.operands.push_back(call.operands.front());
    });
    // Operand type: the task number is a uint64.
    for (const auto* operation : {"join", "is_finished"}) {
        expect_issue(environment, task_fixture, std::string("scheduler operation '") + operation + "' argument 1 has type",
                     [&](IrProgram& program) {
                         scheduler_call(program, operation).operands.front().type =
                             type_named(TypeKind::Integer, "int32");
                     });
    }
    // Result type: is_finished answers a bool, and join answers a value.
    expect_issue(environment, task_fixture, "scheduler operation 'is_finished' has result type", [](IrProgram& program) {
        scheduler_call(program, "is_finished").type = type_named(TypeKind::Integer, "int32");
    });
    expect_issue(environment, task_fixture, "scheduler operation 'join' has result type", [](IrProgram& program) {
        scheduler_call(program, "join").type = type_named(TypeKind::Void, "void");
    });
    // A scheduler call names its operation in text only.
    expect_issue(environment, task_fixture, "a scheduler call names its operation in text only", [](IrProgram& program) {
        scheduler_call(program, "join").symbol = 0;
    });
    expect_issue(environment, task_fixture, "a scheduler call names its operation in text only", [](IrProgram& program) {
        scheduler_call(program, "join").type_arguments.push_back(type_named(TypeKind::Integer, "int32"));
    });
}

NEURA_TEST(verifier, rejects_a_poll_on_an_operation_that_is_neither_a_loop_nor_a_call) {
    for (const auto op : {IrOp::String, IrOp::Spawn, IrOp::Discard, IrOp::Return, IrOp::Initialize, IrOp::Reference, IrOp::TaskScope}) {
        expect_issue(environment, task_fixture, "a poll marks an operation that is neither a loop nor a call",
                     [&](IrProgram& program) { first(body_of(program, "Main.main"), op).poll = true; });
    }
    // The same flag on a loop or a call is what lowering writes.
    auto program = lower_fixture(environment, "concurrency/fold-and-dead.nra");
    bool loop = false;
    bool call = false;
    const std::function<void(const IrNode&)> look = [&](const IrNode& node) {
        loop = loop || (node.op == IrOp::Loop && node.poll);
        call = call || ((node.op == IrOp::Call || node.op == IrOp::VirtualCall) && node.poll);
        for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
            for (const auto& child : *region) look(child);
        }
    };
    for (const auto& body : program.bodies) {
        for (const auto& instruction : body.instructions) look(instruction);
    }
    check(loop && call, "lowering marks polls on loops and on calls");
    check(verify_ir(program).empty(), "polls on loops and calls verify");
}

} // namespace neura::testing

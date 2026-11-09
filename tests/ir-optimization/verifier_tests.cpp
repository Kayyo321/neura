// NeuraIR verifier checks: lowered and optimized programs are accepted, and
// each deliberately malformed program is rejected with a specific issue.
#include "ir-optimization/optimizer.hpp"
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

} // namespace neura::testing

// Behavioral evidence: optimized and unoptimized NeuraIR agree when run by the
// independent reference evaluator, and the integer folder agrees with an
// oracle that uses plain C++ arithmetic. These tests make no claims about IR
// structure; see structure_tests.cpp for those.
#include "evaluator.hpp"
#include "ir-optimization/optimizer.hpp"
#include "support.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <map>
#include <set>

namespace neura::testing {
namespace {

using U64 = std::uint64_t;

struct Sample {
    bool negative;
    U64 magnitude;
};

std::vector<Sample> boundary_samples(unsigned bits, bool is_signed) {
    const U64 upper = is_signed ? (U64{1} << (bits - 1)) - 1
                                : (bits == 64 ? std::numeric_limits<U64>::max() : (U64{1} << bits) - 1);
    std::set<std::pair<bool, U64>> values;
    for (const U64 magnitude : {U64{0}, U64{1}, U64{2}, U64{3}, U64{5}, U64{7}, U64{100}, U64{127}, U64{128},
                                U64{255}, U64{256}, upper / 2, upper / 2 + 1, upper - 1, upper}) {
        if (magnitude <= upper) {
            values.insert({false, magnitude});
        }
        if (is_signed && magnitude != 0 && magnitude <= upper + 1) {
            values.insert({true, magnitude});
        }
    }
    if (is_signed) {
        values.insert({true, upper + 1});
        values.insert({true, upper});
    }
    std::vector<Sample> result;
    for (const auto& [negative, magnitude] : values) {
        result.push_back({negative, magnitude});
    }
    return result;
}

// Every value for 8-bit types and boundary values for wider ones. `int` and
// `byte` get small values because their width is unspecified.
std::vector<Value> samples(const IrType& type, bool exhaustive_small) {
    if (type.kind == TypeKind::Bool) {
        return {bool_value(false), bool_value(true)};
    }
    if (type.kind != TypeKind::Integer) {
        return {};
    }
    std::vector<Value> values;
    if (type.name == "int" || type.name == "byte") {
        for (const int value : {0, 1, 2, 100, 127}) {
            values.push_back(integer_value(type, false, static_cast<U64>(value)));
        }
        if (type.name == "int") {
            values.push_back(integer_value(type, true, 1));
        }
        return values;
    }
    const bool is_signed = type.name.starts_with("int");
    const auto bits = static_cast<unsigned>(std::stoul(type.name.substr(is_signed ? 3 : 4)));
    if (bits == 8 && exhaustive_small) {
        for (int value = is_signed ? -128 : 0; value <= (is_signed ? 127 : 255); ++value) {
            values.push_back(integer_value(type, value < 0, static_cast<U64>(value < 0 ? -value : value)));
        }
        return values;
    }
    for (const auto& sample : boundary_samples(bits, is_signed)) {
        values.push_back(integer_value(type, sample.negative, sample.magnitude));
    }
    return values;
}

struct Tally {
    std::size_t functions = 0;
    std::size_t runs = 0;
    std::size_t skipped = 0;
    std::map<Status, std::size_t> statuses;
};

std::string join(const std::vector<Value>& values) {
    std::string text;
    for (const auto& value : values) {
        text += (text.empty() ? "" : ", ") + describe(value);
    }
    return text;
}

// Runs every initializer body and every function in both programs over the
// sampled argument domain and requires identical outcomes.
Tally compare_programs(const IrProgram& original, const IrProgram& optimized, const std::string& label) {
    constexpr std::size_t max_inputs = 70'000;
    Tally tally;
    Evaluator before(original);
    Evaluator after(optimized);
    const auto initialized_before = before.initialize();
    const auto initialized_after = after.initialize();
    check(initialized_before.size() == initialized_after.size(), label + ": initializer counts differ");
    for (std::size_t i = 0; i < initialized_before.size(); ++i) {
        const auto diff = difference(initialized_before[i], initialized_after[i]);
        check(diff.empty(), label + ": initializer " + std::to_string(i) + " " + diff + "\n  before: " +
                                describe(initialized_before[i]) + "\n  after:  " + describe(initialized_after[i]));
    }
    for (const auto& body : original.bodies) {
        const auto& function = original.symbols[body.owner];
        if (function.kind != SymbolKind::Function) {
            continue;
        }
        std::vector<std::vector<Value>> domains;
        std::size_t inputs = 1;
        for (const bool exhaustive : {true, false}) {
            domains.clear();
            inputs = 1;
            for (const auto parameter : function.parameters) {
                domains.push_back(samples(original.symbols[parameter].type, exhaustive));
                inputs *= domains.back().size();
            }
            if (inputs <= max_inputs) {
                break;
            }
        }
        if (inputs == 0 || inputs > max_inputs) {
            ++tally.skipped;
            continue;
        }
        ++tally.functions;
        std::vector<std::size_t> index(domains.size(), 0);
        for (std::size_t run = 0; run < inputs; ++run) {
            std::vector<Value> arguments;
            for (std::size_t p = 0; p < domains.size(); ++p) {
                arguments.push_back(domains[p][index[p]]);
            }
            const auto expected = before.call(body.owner, arguments);
            const auto actual = after.call(body.owner, arguments);
            const auto diff = difference(expected, actual);
            check(diff.empty(), label + ": " + qualified_name(original, body.owner) + "(" + join(arguments) +
                                    ") " + diff + "\n  unoptimized: " + describe(expected) +
                                    "\n  optimized:   " + describe(actual));
            ++tally.runs;
            ++tally.statuses[expected.status];
            for (std::size_t p = domains.size(); p-- > 0;) {
                if (++index[p] < domains[p].size()) {
                    break;
                }
                index[p] = 0;
            }
        }
    }
    return tally;
}

void report(const std::string& label, const Tally& tally) {
    std::cout << "  " << label << ": " << tally.functions << " function(s), " << tally.runs << " run(s)";
    for (const auto& [status, count] : tally.statuses) {
        std::cout << ", " << count << ' ' << describe(status);
    }
    if (tally.skipped != 0) {
        std::cout << ", " << tally.skipped << " skipped for unsupported parameter types";
    }
    std::cout << '\n';
}

Tally merge(Tally total, const Tally& part) {
    total.functions += part.functions;
    total.runs += part.runs;
    total.skipped += part.skipped;
    for (const auto& [status, count] : part.statuses) {
        total.statuses[status] += count;
    }
    return total;
}

Tally differential(const std::vector<fs::path>& files, const std::vector<fs::path>& roots, const std::string& label) {
    auto original = lower_files(files, roots);
    auto optimized = original;
    optimize(optimized);
    auto tally = compare_programs(original, optimized, label);
    report(label, tally);
    return tally;
}

} // namespace

NEURA_TEST(behavior, fixtures_match_unoptimized) {
    Tally total;
    for (const auto& file : fixture_files(environment)) {
        total = merge(total, differential({file}, {}, file.filename().string()));
    }
    check_equal(total.skipped, std::size_t{0}, "fixture functions skipped by the evaluator");
    for (const auto status : {Status::Returned, Status::Unspecified, Status::LimitReached}) {
        check(total.statuses[status] > 0, "the fixture domain never produced a " + describe(status) + " outcome");
    }
}

NEURA_TEST(behavior, examples_match_unoptimized) {
    const auto files = example_files(environment);
    const std::vector<fs::path> roots{environment.examples() / "guide",
                                      environment.examples() / "modules" / "multi-file"};
    differential(files, {}, "all examples");
    for (const auto& file : files) {
        differential({file}, roots, fs::relative(file, environment.examples()).generic_string());
    }

    // The evaluator must actually execute the example with effects.
    const auto program = lower_files(files);
    Evaluator evaluator(program);
    evaluator.initialize();
    const auto outcome = evaluator.call(find_symbol(program, "MathTests.test_sum_to"), {});
    check(outcome.status == Status::Returned, "MathTests.test_sum_to: " + describe(outcome));
    check_equal(outcome.effects.size(), std::size_t{2}, "assertions recorded by MathTests.test_sum_to");
}

// Folds `left op right` for literal operands and compares the folder, the
// reference evaluator, and (for int8 to int32, uint8, and uint16) a long
// long oracle.
// The folder must fold exactly when the result is defined and must produce
// the defined value.
NEURA_TEST(behavior, integer_folding_agrees_with_oracles) {
    struct Case {
        const char* type;
        bool exhaustive;
    };
    const std::array cases{Case{"int8", true},    Case{"uint8", true},    Case{"int16", false},
                           Case{"uint16", false}, Case{"int32", false},   Case{"uint32", false},
                           Case{"int64", false},  Case{"uint64", false}};
    const std::array<std::string, 11> operators{"+", "-", "*", "/", "==", "!=", "<", "<=", ">", ">=", "neg"};
    const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};
    std::size_t checked = 0;
    std::size_t folded = 0;
    for (const auto& test_case : cases) {
        const IrType type{TypeKind::Integer, test_case.type, no_symbol, {}};
        const auto values = samples(type, test_case.exhaustive);
        const bool is_signed = type.name.starts_with("int");
        const auto bits = std::stoul(type.name.substr(is_signed ? 3 : 4));
        for (const auto& op : operators) {
            IrProgram program;
            Span span{"exhaustive.nra", 1, 1, 0, 1};
            program.symbols.push_back({SymbolKind::File, "exhaustive.nra", span, no_symbol, no_symbol, no_symbol,
                                       {TypeKind::Void, "void", no_symbol, {}}, {}, {}, {}, false, false, false,
                                       false});
            program.symbols.push_back({SymbolKind::Function, "probe", span, 0, no_symbol, no_symbol,
                                       {TypeKind::Void, "void", no_symbol, {}}, {}, {}, {}, false, false, false,
                                       true});
            program.bodies.push_back({1, {}});
            auto& body = program.bodies.front().instructions;
            auto literal = [&](const Value& value, const Span& where) {
                return IrNode{IrOp::Integer, type, where, no_symbol,
                              (value.negative ? "-" : "") + std::to_string(value.magnitude), {}, {}, {}, {}};
            };
            const bool comparison = op != "+" && op != "-" && op != "*" && op != "/" && op != "neg";
            std::vector<std::pair<Value, Value>> inputs;
            for (const auto& left : values) {
                if (op == "neg") {
                    inputs.push_back({left, left});
                    continue;
                }
                for (const auto& right : values) {
                    inputs.push_back({left, right});
                }
            }
            body.reserve(inputs.size());
            for (const auto& [left, right] : inputs) {
                span.line = body.size() + 1;
                IrNode expression{op == "neg" ? IrOp::Unary : IrOp::Binary, comparison ? bool_type : type, span,
                                  no_symbol, op == "neg" ? "-" : op, {}, {}, {}, {}};
                expression.operands.push_back(literal(left, span));
                if (op != "neg") {
                    expression.operands.push_back(literal(right, span));
                }
                auto& discard = body.emplace_back();
                discard.op = IrOp::Discard;
                discard.type = {TypeKind::Void, "void", no_symbol, {}};
                discard.span = span;
                discard.operands.push_back(std::move(expression));
            }
            auto optimized = program;
            OptimizationOptions options;
            options.passes = {"fold-constants"};
            optimize(optimized, options);
            Evaluator evaluator(program);
            const auto& result_body = optimized.bodies.front().instructions;
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                const auto& [left, right] = inputs[i];
                const auto& original = body[i].operands.front();
                const auto& rewritten = result_body[i].operands.front();
                const auto reference = evaluator.evaluate(original);
                const bool defined = reference.status == Status::Returned;
                // Messages are built only on failure; there are over a million checks.
                const auto expect = [&](bool condition, const auto& message) {
                    if (!condition) {
                        fail(std::string(test_case.type) + " " + describe(left) + " " + op + " " +
                             describe(right) + ": " + message());
                    }
                };
                expect(defined || reference.status == Status::Unspecified, [&] { return describe(reference); });
                const bool was_folded = rewritten.op == IrOp::Integer || rewritten.op == IrOp::Boolean;
                expect(was_folded == defined, [&] {
                    return "folded=" + std::to_string(was_folded) + " but evaluator says " + describe(reference);
                });
                if (was_folded) {
                    ++folded;
                    expect(same_span(rewritten.span, original.span), [] { return "folded literal lost its span"; });
                    expect(rewritten.type == original.type, [] { return "folded literal changed type"; });
                    const auto& value = *reference.result;
                    const auto spelled = value.kind == Value::Kind::Bool
                        ? std::string(value.boolean ? "true" : "false")
                        : (value.negative ? "-" : "") + std::to_string(value.magnitude);
                    expect(rewritten.text == spelled, [&] {
                        return "folded to " + rewritten.text + ", evaluator computed " + describe(reference);
                    });
                } else {
                    expect(rewritten.op == original.op && rewritten.operands.size() == original.operands.size() &&
                               rewritten.operands.front().text == original.operands.front().text &&
                               rewritten.operands.back().text == original.operands.back().text,
                           [] { return "declined fold changed IR"; });
                }
                if (bits < 32 || (bits == 32 && is_signed)) {
                    // Independent oracle: for these formats no sum, difference,
                    // product, or negation of two values overflows long long.
                    const long long a = left.negative ? -static_cast<long long>(left.magnitude)
                                                      : static_cast<long long>(left.magnitude);
                    const long long b = right.negative ? -static_cast<long long>(right.magnitude)
                                                       : static_cast<long long>(right.magnitude);
                    const long long lower = is_signed ? -(1LL << (bits - 1)) : 0;
                    const long long upper = is_signed ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
                    std::optional<long long> exact;
                    std::optional<bool> truth;
                    if (op == "+") exact = a + b;
                    if (op == "-") exact = a - b;
                    if (op == "*") exact = a * b;
                    if (op == "neg") exact = -a;
                    if (op == "/" && b != 0 && a % b == 0) exact = a / b;
                    if (op == "==") truth = a == b;
                    if (op == "!=") truth = a != b;
                    if (op == "<") truth = a < b;
                    if (op == "<=") truth = a <= b;
                    if (op == ">") truth = a > b;
                    if (op == ">=") truth = a >= b;
                    const bool oracle_defined = truth || (exact && *exact >= lower && *exact <= upper);
                    expect(oracle_defined == defined, [] { return "oracle and evaluator disagree on definedness"; });
                    if (defined && truth) {
                        expect(reference.result == bool_value(*truth), [] { return "comparison result"; });
                    } else if (defined) {
                        const auto magnitude = static_cast<U64>(*exact < 0 ? -*exact : *exact);
                        expect(reference.result == integer_value(type, *exact < 0, magnitude),
                               [&] { return "oracle computed " + std::to_string(*exact); });
                    }
                }
                ++checked;
            }
        }
    }
    std::cout << "  " << checked << " integer operations checked, " << folded << " folded\n";
}

// Expectations derived by hand from the fixture source, not from the
// unoptimized IR, run against the optimized program.
NEURA_TEST(behavior, hand_derived_expectations) {
    auto check_call = [&](std::string_view file, std::string_view function, const std::vector<Value>& arguments,
                          const Outcome& expected) {
        auto program = lower_fixture(environment, file);
        optimize(program);
        Evaluator evaluator(program);
        evaluator.initialize();
        const auto actual = evaluator.call(find_symbol(program, function), arguments);
        const auto diff = difference(expected, actual);
        check(diff.empty(), std::string(function) + "(" + join(arguments) + ") " + diff + "\n  expected: " +
                                describe(expected) + "\n  actual:   " + describe(actual));
    };
    auto returned = [](Value value, std::vector<Effect> effects = {}) {
        Outcome outcome;
        outcome.result = std::move(value);
        outcome.effects = std::move(effects);
        return outcome;
    };
    const auto assertion = [](bool value) { return Effect{"Test.assert", {bool_value(value)}}; };
    const IrType int8{TypeKind::Integer, "int8", no_symbol, {}};

    // The effectful left operand still runs even though the result is known.
    check_call("short-circuit.nra", "ShortCircuit.keep_effect", {bool_value(true)},
               returned(bool_value(false), {assertion(true)}));
    // The right operand runs only when the left one does not decide.
    check_call("short-circuit.nra", "ShortCircuit.keep_lazy", {bool_value(false)}, returned(bool_value(false)));
    check_call("short-circuit.nra", "ShortCircuit.keep_lazy", {bool_value(true)},
               returned(bool_value(true), {assertion(true)}));
    check_call("short-circuit.nra", "ShortCircuit.skip_right", {bool_value(true)}, returned(bool_value(false)));
    check_call("short-circuit.nra", "ShortCircuit.nested", {bool_value(false)},
               returned(bool_value(false), {assertion(false)}));

    // index 1 adds 1, index 2 continues, indexes 3 to 5 add 12, index 6 breaks.
    check_call("control-flow.nra", "ControlFlow.loop_control", {integer_value(int8, false, 10)},
               returned(integer_value(int8, false, 13)));

    Outcome failure;
    failure.status = Status::Unspecified;
    failure.reason = "division by zero";
    auto program = lower_fixture(environment, "short-circuit.nra");
    const auto& keep_failure = body_of(program, "ShortCircuit.keep_failure");
    failure.location = keep_failure.front().operands.front().operands.front().operands.front().span;
    const IrType int32{TypeKind::Integer, "int32", no_symbol, {}};
    check_call("short-circuit.nra", "ShortCircuit.keep_failure", {integer_value(int32, false, 0)}, failure);
}

} // namespace neura::testing

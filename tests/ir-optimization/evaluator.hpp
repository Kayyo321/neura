#pragma once

#include "neura-ir/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace neura::testing {

// A reference evaluator for a subset of NeuraIR, written independently of the
// optimizer so that both can be compared on the same inputs.
//
// Modeled: bool; integer literals, comparisons, and `!`; arithmetic and unary
// minus on int8..int64 and uint8..uint64; string literals and concatenation;
// enum variant values; locals, parameters, and module globals; branches,
// loops, break, continue, and return; direct calls and calls on module
// receivers, including module-interface dispatch; and external calls, which
// are recorded as effects whose response is `void`.
//
// Stops with Status::Unspecified at an operation whose result the language
// leaves open: integer overflow, division by zero, a division with a nonzero
// remainder (rounding is unspecified), and arithmetic on `int` or `byte`.
// Stops with Status::Unsupported at constructs the evaluator does not model,
// such as objects, collections, and floating point. Stops with
// Status::LimitReached when the loop-iteration and call budget or the call
// depth is exhausted. Loop iterations and calls are what the budget counts;
// the passes under test never add or remove an executed iteration or call.
struct Value {
    enum class Kind { Void, Bool, Integer, String, Variant, Module };

    Kind kind = Kind::Void;
    IrType type;
    bool boolean = false;
    // Integers are held as sign and magnitude; zero is never negative.
    bool negative = false;
    std::uint64_t magnitude = 0;
    std::string text;
    SymbolId symbol = no_symbol;

    bool operator==(const Value&) const = default;
};

Value bool_value(bool value);
Value integer_value(const IrType& type, bool negative, std::uint64_t magnitude);
std::string describe(const Value& value);

struct Effect {
    std::string callee;
    std::vector<Value> arguments;

    bool operator==(const Effect&) const = default;
};

enum class Status { Returned, Unspecified, Unsupported, LimitReached };
std::string describe(Status status);

struct Outcome {
    Status status = Status::Returned;
    std::optional<Value> result;
    std::string reason;
    Span location;
    std::vector<Effect> effects;
    std::map<SymbolId, Value> globals;
};

// Empty when `left` and `right` agree on status, result, stop reason and
// location, effects, and final global state; otherwise a description of the
// first difference.
std::string difference(const Outcome& left, const Outcome& right);
std::string describe(const Outcome& outcome);

struct Limits {
    std::size_t budget = 100'000;
    std::size_t depth = 64;
};

class Evaluator {
public:
    explicit Evaluator(const IrProgram& program, Limits limits = {});
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    // Runs file and module initializer bodies in program order and keeps the
    // resulting globals as the starting state for later calls. Class field
    // initializers belong to objects, which are not modeled.
    std::vector<Outcome> initialize();

    // Calls `function` from the state left by initialize(). Calls do not
    // affect one another.
    Outcome call(SymbolId function, const std::vector<Value>& arguments);

    // Evaluates an expression that refers to no locals or parameters.
    Outcome evaluate(const IrNode& expression);

private:
    class Machine;

    const IrProgram& program_;
    Limits limits_;
    std::map<SymbolId, Value> initial_globals_;
    std::unique_ptr<Machine> closed_;
};

} // namespace neura::testing

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

// A reference evaluator for NeuraIR, written independently of the optimizer
// and the converter so that each can be compared with it on the same inputs.
//
// Modeled: bool; integer literals, arithmetic, comparisons, remainders,
// unary minus, and checked conversions on int8..int64 and uint8..uint64;
// enum values with payloads (construction, tags, and projections) and the
// scoped and selected value regions that matches lower to; message traps;
// float16, bfloat16, float32, and float64 arithmetic, comparisons, negation,
// and conversions, through float_reference.hpp (literals go through strtod
// or strtof, so a 16-bit literal is rounded twice);
// string literals and concatenation; enum variants and their ordinals;
// locals, parameters, and module globals; branches, loops with latches,
// break, continue, and return; direct calls, calls on module receivers
// including module-interface dispatch, and virtual calls on objects; object
// construction in the documented order, fields, and destruction; arrays and
// lists with lengths, indexing, append, remove_at, and structural versions;
// and external calls, which are recorded as effects whose response is
// `void`, or for a standard library host binding, the success variant `Ok`
// when it carries no payload.
//
// Stops with Status::Trapped at a program trap, with the NVR reason code the
// backend must use: 1 for arithmetic (overflow, a zero divisor, MIN / -1,
// an out-of-range conversion), 2 for a negative index, and 5 for a list
// modified during for-each. Stops with Status::Faulted at a runtime fault
// that NVM detects: an index at or past the length, a read of an
// uninitialized field or global, or use of a destroyed object. Stops with
// Status::Unsupported at constructs the evaluator does not model. Stops with
// Status::LimitReached when the loop-iteration
// and call budget or the call depth is exhausted. Loop iterations and calls
// are what the budget counts; the passes under test never add or remove an
// executed iteration or call.
struct Value {
    // A Float holds its encoding in `magnitude`.
    enum class Kind { Void, Bool, Integer, Float, String, Variant, Module, Reference };

    Kind kind = Kind::Void;
    IrType type;
    bool boolean = false;
    // Integers are held as sign and magnitude; zero is never negative.
    bool negative = false;
    std::uint64_t magnitude = 0;
    std::string text;
    SymbolId symbol = no_symbol;
    // Heap entry of an object, array, or list.
    std::size_t handle = 0;
    // The payload fields of a Variant of an enum with payloads.
    std::vector<Value> payload{};

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

enum class Status { Returned, Trapped, Faulted, Unsupported, LimitReached };
std::string describe(Status status);

struct Outcome {
    Status status = Status::Returned;
    std::optional<Value> result;
    std::string reason;
    // NVR trap reason of a Trapped outcome.
    std::uint32_t trap_reason = 0;
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

struct Heap;

class Evaluator {
public:
    explicit Evaluator(const IrProgram& program, Limits limits = {});
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    // Runs file and module initializer bodies in program order and keeps the
    // resulting globals and heap as the starting state for later calls.
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
    std::shared_ptr<const Heap> initial_heap_;
    std::unique_ptr<Machine> closed_;
};

} // namespace neura::testing

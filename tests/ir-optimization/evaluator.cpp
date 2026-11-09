#include "evaluator.hpp"

#include "support.hpp"

#include <limits>
#include <sstream>
#include <utility>

namespace neura::testing {
namespace {

using U64 = std::uint64_t;

struct Format {
    unsigned bits;
    bool is_signed;
};

// The evaluator keeps its own table rather than sharing the compiler's.
std::optional<Format> format_of(const IrType& type) {
    static const std::map<std::string, Format, std::less<>> formats{
        {"int8", {8, true}},   {"int16", {16, true}},   {"int32", {32, true}},   {"int64", {64, true}},
        {"uint8", {8, false}}, {"uint16", {16, false}}, {"uint32", {32, false}}, {"uint64", {64, false}},
    };
    if (type.kind != TypeKind::Integer) {
        return {};
    }
    const auto found = formats.find(type.name);
    return found == formats.end() ? std::nullopt : std::optional{found->second};
}

// An exact integer as sign and magnitude. `wide` marks a magnitude of at least
// 2^64, which no supported type can represent.
struct Exact {
    bool negative = false;
    U64 magnitude = 0;
    bool wide = false;
};

Exact normalized(Exact value) {
    if (value.magnitude == 0 && !value.wide) {
        value.negative = false;
    }
    return value;
}

Exact add(Exact a, Exact b) {
    if (a.wide || b.wide) {
        return {false, 0, true};
    }
    if (a.negative == b.negative) {
        const U64 sum = a.magnitude + b.magnitude; // Unsigned addition wraps.
        return normalized({a.negative, sum, sum < a.magnitude});
    }
    if (a.magnitude >= b.magnitude) {
        return normalized({a.negative, a.magnitude - b.magnitude, false});
    }
    return normalized({b.negative, b.magnitude - a.magnitude, false});
}

Exact negated(Exact value) {
    value.negative = !value.negative;
    return normalized(value);
}

// Forms the full 128-bit product of the magnitudes from 32-bit limbs.
Exact multiply(Exact a, Exact b) {
    constexpr U64 mask = 0xffffffffU;
    const U64 a_low = a.magnitude & mask;
    const U64 a_high = a.magnitude >> 32;
    const U64 b_low = b.magnitude & mask;
    const U64 b_high = b.magnitude >> 32;
    const U64 low_low = a_low * b_low;
    const U64 high_low = a_high * b_low;
    const U64 low_high = a_low * b_high;
    const U64 high_high = a_high * b_high;
    const U64 middle = (low_low >> 32) + (high_low & mask) + low_high;
    const U64 high = high_high + (high_low >> 32) + (middle >> 32);
    const U64 low = (middle << 32) | (low_low & mask);
    return normalized({a.negative != b.negative, low, high != 0 || a.wide || b.wide});
}

bool fits(const Exact& value, Format format) {
    if (value.wide) {
        return false;
    }
    if (!format.is_signed) {
        return !value.negative &&
               (format.bits == 64 || value.magnitude <= (U64{1} << format.bits) - 1);
    }
    const U64 limit = U64{1} << (format.bits - 1);
    return value.negative ? value.magnitude <= limit : value.magnitude < limit;
}

int compare(const Exact& a, const Exact& b) {
    if (a.negative != b.negative) {
        return a.negative ? -1 : 1;
    }
    const int order = a.magnitude < b.magnitude ? -1 : (a.magnitude > b.magnitude ? 1 : 0);
    return a.negative ? -order : order;
}

Exact exact(const Value& value) {
    return {value.negative, value.magnitude, false};
}

struct Stop {
    Status status;
    std::string reason;
    Span location;
};

enum class Flow { Next, Return, Break, Continue };

struct Frame {
    SymbolId function = no_symbol;
    SymbolId receiver = no_symbol;
    std::map<SymbolId, Value> locals;
    std::optional<Value> result;
};

} // namespace

Value bool_value(bool value) {
    Value result;
    result.kind = Value::Kind::Bool;
    result.type = {TypeKind::Bool, "bool", no_symbol, {}};
    result.boolean = value;
    return result;
}

Value integer_value(const IrType& type, bool negative, std::uint64_t magnitude) {
    Value result;
    result.kind = Value::Kind::Integer;
    result.type = type;
    result.negative = negative && magnitude != 0;
    result.magnitude = magnitude;
    return result;
}

std::string describe(const Value& value) {
    switch (value.kind) {
    case Value::Kind::Void: return "void";
    case Value::Kind::Bool: return value.boolean ? "true" : "false";
    case Value::Kind::Integer:
        return (value.negative ? "-" : "") + std::to_string(value.magnitude) + ":" + value.type.name;
    case Value::Kind::String: return '"' + value.text + '"';
    case Value::Kind::Variant: return "variant %" + std::to_string(value.symbol);
    case Value::Kind::Module: return "module %" + std::to_string(value.symbol);
    }
    return "<invalid>";
}

std::string describe(Status status) {
    switch (status) {
    case Status::Returned: return "returned";
    case Status::Unspecified: return "unspecified";
    case Status::Unsupported: return "unsupported";
    case Status::LimitReached: return "limit reached";
    }
    return "<invalid>";
}

std::string describe(const Outcome& outcome) {
    std::ostringstream out;
    out << describe(outcome.status);
    if (outcome.result) {
        out << ' ' << describe(*outcome.result);
    }
    if (outcome.status != Status::Returned) {
        out << " (" << outcome.reason << " at " << testing::describe(outcome.location) << ')';
    }
    out << ", effects [";
    for (std::size_t i = 0; i < outcome.effects.size(); ++i) {
        out << (i == 0 ? "" : ", ") << outcome.effects[i].callee << '(';
        for (std::size_t j = 0; j < outcome.effects[i].arguments.size(); ++j) {
            out << (j == 0 ? "" : ", ") << describe(outcome.effects[i].arguments[j]);
        }
        out << ')';
    }
    out << "], " << outcome.globals.size() << " global(s)";
    return out.str();
}

std::string difference(const Outcome& left, const Outcome& right) {
    if (left.status != right.status) return "status differs";
    if (left.result != right.result) return "result differs";
    if (left.status != Status::Returned &&
        (left.reason != right.reason || !same_span(left.location, right.location))) {
        return "stop reason or location differs";
    }
    if (left.effects != right.effects) return "effects differ";
    if (left.globals != right.globals) return "final global state differs";
    return {};
}

class Evaluator::Machine {
public:
    Machine(const IrProgram& program, Limits limits, std::map<SymbolId, Value> globals)
        : program_(program), limits_(limits), globals_(std::move(globals)) {
        for (const auto& body : program.bodies) {
            bodies_.emplace(body.owner, &body);
        }
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].kind == SymbolKind::Function) {
                functions_by_owner_[program.symbols[id].owner].push_back(id);
            }
        }
    }

    const std::map<SymbolId, Value>& globals() const { return globals_; }

    Outcome run_body(const IrBody& body, SymbolId receiver) {
        return guarded([&]() -> std::optional<Value> {
            Frame frame;
            frame.receiver = receiver;
            run_region(body.instructions, frame);
            return std::nullopt;
        });
    }

    Outcome call(SymbolId function, const std::vector<Value>& arguments) {
        return guarded([&]() -> std::optional<Value> {
            return invoke(function, no_symbol, arguments, symbol(function).span);
        });
    }

    Outcome evaluate_closed(const IrNode& expression) {
        return guarded([&]() -> std::optional<Value> {
            Frame frame;
            return evaluate(expression, frame);
        });
    }

private:
    const IrProgram& program_;
    Limits limits_;
    std::map<SymbolId, Value> globals_;
    std::map<SymbolId, const IrBody*> bodies_;
    std::map<SymbolId, std::vector<SymbolId>> functions_by_owner_;
    std::vector<Effect> effects_;
    std::size_t spent_ = 0;
    std::size_t depth_ = 0;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols.at(id); }

    template <typename Run>
    Outcome guarded(Run run) {
        effects_.clear();
        spent_ = 0;
        depth_ = 0;
        Outcome outcome;
        try {
            outcome.result = run();
        } catch (const Stop& stop) {
            outcome.status = stop.status;
            outcome.reason = stop.reason;
            outcome.location = stop.location;
        }
        outcome.effects = effects_;
        outcome.globals = globals_;
        return outcome;
    }

    [[noreturn]] static void unsupported(const IrNode& node, std::string reason) {
        throw Stop{Status::Unsupported, std::move(reason), node.span};
    }

    [[noreturn]] static void unspecified(const IrNode& node, std::string reason) {
        throw Stop{Status::Unspecified, std::move(reason), node.span};
    }

    void spend(const Span& where) {
        if (++spent_ > limits_.budget) {
            throw Stop{Status::LimitReached, "loop and call budget exhausted", where};
        }
    }

    Value invoke(SymbolId function, SymbolId receiver, const std::vector<Value>& arguments, const Span& site) {
        spend(site);
        if (depth_ >= limits_.depth) {
            throw Stop{Status::LimitReached, "call depth limit", site};
        }
        const auto body = bodies_.find(function);
        if (body == bodies_.end()) {
            throw Stop{Status::Unsupported, "call to " + qualified_name(program_, function) + " without a body", site};
        }
        Frame frame;
        frame.function = function;
        frame.receiver = receiver;
        const auto& parameters = symbol(function).parameters;
        if (parameters.size() != arguments.size()) {
            throw Stop{Status::Unsupported, "argument count mismatch", site};
        }
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            frame.locals[parameters[i]] = arguments[i];
        }
        ++depth_;
        run_region(body->second->instructions, frame);
        --depth_;
        return frame.result.value_or(Value{});
    }

    // The implementation a call on `module` selects for `target`: the first
    // function in the module or its bases that is `target` or overrides it.
    SymbolId dispatch(SymbolId target, SymbolId module) const {
        std::size_t steps = 0;
        for (auto scope = module; scope < program_.symbols.size() && steps++ < program_.symbols.size();
             scope = symbol(scope).base) {
            const auto found = functions_by_owner_.find(scope);
            if (found == functions_by_owner_.end()) {
                continue;
            }
            for (const auto candidate : found->second) {
                for (auto current = candidate; current < program_.symbols.size(); current = symbol(current).overrides) {
                    if (current == target) {
                        return candidate;
                    }
                }
            }
        }
        return target;
    }

    Flow run_region(const std::vector<IrNode>& region, Frame& frame) {
        for (const auto& statement : region) {
            const auto flow = run_statement(statement, frame);
            if (flow != Flow::Next) {
                return flow;
            }
        }
        return Flow::Next;
    }

    Flow run_statement(const IrNode& node, Frame& frame) {
        switch (node.op) {
        case IrOp::Initialize: {
            auto value = evaluate(node.operands.at(0), frame);
            const auto kind = symbol(node.symbol).kind;
            if (kind == SymbolKind::Local) {
                frame.locals[node.symbol] = std::move(value);
            } else if (kind == SymbolKind::Global) {
                globals_[node.symbol] = std::move(value);
            } else {
                unsupported(node, "field initialization");
            }
            return Flow::Next;
        }
        case IrOp::Store: {
            const auto& destination = node.operands.at(0);
            if (destination.op != IrOp::Load) {
                unsupported(destination, "store to a field or element");
            }
            const bool global = symbol(destination.symbol).kind == SymbolKind::Global;
            if (global) {
                check_module_receiver(destination, frame);
            }
            auto value = evaluate(node.operands.at(1), frame);
            (global ? globals_ : frame.locals)[destination.symbol] = std::move(value);
            return Flow::Next;
        }
        case IrOp::Discard:
            evaluate(node.operands.at(0), frame);
            return Flow::Next;
        case IrOp::Block:
            return run_region(node.body, frame);
        case IrOp::Branch:
            return run_region(condition(node.operands.at(0), frame) ? node.body : node.alternative, frame);
        case IrOp::Loop:
            while (condition(node.operands.at(0), frame)) {
                spend(node.span);
                const auto flow = run_region(node.body, frame);
                if (flow == Flow::Return) {
                    return flow;
                }
                if (flow == Flow::Break) {
                    break;
                }
            }
            return Flow::Next;
        case IrOp::Return:
            if (!node.operands.empty()) {
                frame.result = evaluate(node.operands.front(), frame);
            }
            return Flow::Return;
        case IrOp::Break:
            return Flow::Break;
        case IrOp::Continue:
            return Flow::Continue;
        default:
            unsupported(node, "statement operation");
        }
    }

    bool condition(const IrNode& node, Frame& frame) {
        const auto value = evaluate(node, frame);
        if (value.kind != Value::Kind::Bool) {
            unsupported(node, "non-bool condition");
        }
        return value.boolean;
    }

    Value module_value(SymbolId module) const {
        Value result;
        result.kind = Value::Kind::Module;
        result.type = symbol(module).type;
        result.symbol = module;
        return result;
    }

    void check_module_receiver(const IrNode& load, Frame& frame) {
        if (load.operands.empty()) {
            return;
        }
        const auto receiver = evaluate(load.operands.front(), frame);
        if (receiver.kind != Value::Kind::Module || receiver.symbol != symbol(load.symbol).owner) {
            unsupported(load, "global accessed through another module");
        }
    }

    Value literal(const IrNode& node) const {
        std::string_view text = node.text;
        const bool negative = text.starts_with('-');
        if (negative) {
            text.remove_prefix(1);
        }
        U64 magnitude = 0;
        for (const char digit : text) {
            const auto d = static_cast<U64>(digit - '0');
            if (digit < '0' || digit > '9' || magnitude > (std::numeric_limits<U64>::max() - d) / 10) {
                unsupported(node, "malformed integer literal");
            }
            magnitude = magnitude * 10 + d;
        }
        return integer_value(node.type, negative, magnitude);
    }

    std::vector<Value> arguments(const IrNode& node, std::size_t first, Frame& frame) {
        std::vector<Value> values;
        for (std::size_t i = first; i < node.operands.size(); ++i) {
            values.push_back(evaluate(node.operands[i], frame));
        }
        return values;
    }

    Value evaluate(const IrNode& node, Frame& frame) {
        switch (node.op) {
        case IrOp::Boolean:
            return bool_value(node.text == "true");
        case IrOp::Integer:
            return literal(node);
        case IrOp::String: {
            Value result;
            result.kind = Value::Kind::String;
            result.type = node.type;
            result.text = node.text;
            return result;
        }
        case IrOp::Load: {
            const auto kind = symbol(node.symbol).kind;
            if (kind == SymbolKind::Global) {
                check_module_receiver(node, frame);
                const auto found = globals_.find(node.symbol);
                if (found == globals_.end()) {
                    unsupported(node, "read of an uninitialized global");
                }
                return found->second;
            }
            const auto found = frame.locals.find(node.symbol);
            if (found == frame.locals.end()) {
                unsupported(node, "read of an unset local");
            }
            return found->second;
        }
        case IrOp::Reference: {
            const auto& target = symbol(node.symbol);
            if (target.kind == SymbolKind::Variant) {
                Value result;
                result.kind = Value::Kind::Variant;
                result.type = node.type;
                result.symbol = node.symbol;
                return result;
            }
            if (target.kind == SymbolKind::Module) {
                return module_value(node.symbol);
            }
            if (target.kind == SymbolKind::Import && target.type.kind == TypeKind::Module) {
                return module_value(target.type.symbol);
            }
            unsupported(node, "reference to a non-value symbol");
        }
        case IrOp::Receiver:
            if (symbol(node.symbol).kind != SymbolKind::Module) {
                unsupported(node, "object receiver");
            }
            return module_value(frame.receiver != no_symbol ? frame.receiver : node.symbol);
        case IrOp::Unary:
            return unary(node, evaluate(node.operands.at(0), frame));
        case IrOp::Binary: {
            auto left = evaluate(node.operands.at(0), frame);
            auto right = evaluate(node.operands.at(1), frame);
            return binary(node, left, right);
        }
        case IrOp::ShortCircuit: {
            const bool left = condition(node.operands.at(0), frame);
            if (node.text == "&&" ? !left : left) {
                return bool_value(left);
            }
            return bool_value(condition(node.body.at(0), frame));
        }
        case IrOp::Call:
            return invoke(node.symbol, no_symbol, arguments(node, 0, frame), node.span);
        case IrOp::VirtualCall: {
            const auto receiver = evaluate(node.operands.at(0), frame);
            if (receiver.kind != Value::Kind::Module) {
                unsupported(node, "method call on an object");
            }
            auto values = arguments(node, 1, frame);
            return invoke(dispatch(node.symbol, receiver.symbol), receiver.symbol, values, node.span);
        }
        case IrOp::ExternalCall: {
            auto values = arguments(node, 0, frame);
            effects_.push_back({qualified_name(program_, node.symbol), std::move(values)});
            if (symbol(node.symbol).type.kind != TypeKind::Void) {
                unsupported(node, "external call with a result");
            }
            return Value{};
        }
        case IrOp::Float:
            unsupported(node, "floating point");
        default:
            unsupported(node, "expression operation");
        }
    }

    Value unary(const IrNode& node, const Value& operand) {
        if (node.text == "!" && operand.kind == Value::Kind::Bool) {
            return bool_value(!operand.boolean);
        }
        if (node.text != "-" || operand.kind != Value::Kind::Integer) {
            unsupported(node, "unary operation");
        }
        return arithmetic_result(node, negated(exact(operand)));
    }

    Value arithmetic_result(const IrNode& node, const Exact& result) {
        const auto format = format_of(node.type);
        if (!format) {
            unspecified(node, "arithmetic on " + node.type.name + ", whose width is unspecified");
        }
        if (!fits(result, *format)) {
            unspecified(node, "overflow");
        }
        return integer_value(node.type, result.negative, result.magnitude);
    }

    Value binary(const IrNode& node, const Value& left, const Value& right) {
        const auto& op = node.text;
        if (left.kind == Value::Kind::Bool && right.kind == Value::Kind::Bool && (op == "==" || op == "!=")) {
            return bool_value((left.boolean == right.boolean) == (op == "=="));
        }
        if (left.kind == Value::Kind::String && right.kind == Value::Kind::String && op == "+") {
            Value result = left;
            result.text += right.text;
            return result;
        }
        if (left.kind != Value::Kind::Integer || right.kind != Value::Kind::Integer) {
            unsupported(node, "binary operation on these operands");
        }
        const auto a = exact(left);
        const auto b = exact(right);
        const int order = compare(a, b);
        if (op == "==") return bool_value(order == 0);
        if (op == "!=") return bool_value(order != 0);
        if (op == "<") return bool_value(order < 0);
        if (op == "<=") return bool_value(order <= 0);
        if (op == ">") return bool_value(order > 0);
        if (op == ">=") return bool_value(order >= 0);
        if (!format_of(node.type)) {
            unspecified(node, "arithmetic on " + node.type.name + ", whose width is unspecified");
        }
        if (op == "+") return arithmetic_result(node, add(a, b));
        if (op == "-") return arithmetic_result(node, add(a, negated(b)));
        if (op == "*") return arithmetic_result(node, multiply(a, b));
        if (op == "/") {
            if (b.magnitude == 0) {
                unspecified(node, "division by zero");
            }
            if (a.magnitude % b.magnitude != 0) {
                unspecified(node, "division with a remainder, whose rounding is unspecified");
            }
            return arithmetic_result(node, normalized({a.negative != b.negative, a.magnitude / b.magnitude, false}));
        }
        unsupported(node, "binary operator " + op);
    }
};

Evaluator::Evaluator(const IrProgram& program, Limits limits) : program_(program), limits_(limits) {}

Evaluator::~Evaluator() = default;

std::vector<Outcome> Evaluator::initialize() {
    Machine machine(program_, limits_, {});
    std::vector<Outcome> outcomes;
    for (const auto& body : program_.bodies) {
        const auto kind = program_.symbols.at(body.owner).kind;
        if (kind == SymbolKind::File || kind == SymbolKind::Module) {
            outcomes.push_back(machine.run_body(body, kind == SymbolKind::Module ? body.owner : no_symbol));
        }
    }
    initial_globals_ = machine.globals();
    return outcomes;
}

Outcome Evaluator::call(SymbolId function, const std::vector<Value>& arguments) {
    Machine machine(program_, limits_, initial_globals_);
    return machine.call(function, arguments);
}

Outcome Evaluator::evaluate(const IrNode& expression) {
    // A closed expression cannot write globals, so one machine is reused.
    if (!closed_) {
        closed_ = std::make_unique<Machine>(program_, limits_, std::map<SymbolId, Value>{});
    }
    return closed_->evaluate_closed(expression);
}

} // namespace neura::testing

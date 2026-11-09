#include "evaluator.hpp"

#include "float_reference.hpp"
#include "support.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include <algorithm>
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
        {"int", {32, true}},   {"byte", {8, false}},
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
    std::uint32_t trap_reason = 0;
};

enum class Flow { Next, Return, Break, Continue };

struct Frame {
    SymbolId function = no_symbol;
    SymbolId receiver = no_symbol;
    // The object a method, constructor, or destructor runs on.
    std::optional<Value> self;
    std::map<SymbolId, Value> locals;
    std::optional<Value> result;
};

} // namespace

struct HeapEntry {
    enum class Kind { Object, Array, List };
    Kind kind = Kind::Object;
    // Runtime class of an object.
    SymbolId cls = no_symbol;
    std::map<SymbolId, Value> fields;
    std::vector<Value> elements;
    std::uint64_t version = 0;
    bool freed = false;
};

struct Heap {
    std::vector<HeapEntry> entries;
};

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
    case Value::Kind::Float: {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value.magnitude));
        return std::string(buffer) + ":" + value.type.name;
    }
    case Value::Kind::String: return '"' + value.text + '"';
    case Value::Kind::Variant: return "variant %" + std::to_string(value.symbol);
    case Value::Kind::Module: return "module %" + std::to_string(value.symbol);
    case Value::Kind::Reference: return "ref#" + std::to_string(value.handle);
    }
    return "<invalid>";
}

std::string describe(Status status) {
    switch (status) {
    case Status::Returned: return "returned";
    case Status::Trapped: return "trapped";
    case Status::Faulted: return "faulted";
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
        out << " (" << outcome.reason;
        if (outcome.status == Status::Trapped) {
            out << ", reason " << outcome.trap_reason;
        }
        out << " at " << testing::describe(outcome.location) << ')';
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
        (left.reason != right.reason || left.trap_reason != right.trap_reason ||
         !same_span(left.location, right.location))) {
        return "stop reason or location differs";
    }
    if (left.effects != right.effects) return "effects differ";
    if (left.globals != right.globals) return "final global state differs";
    return {};
}

class Evaluator::Machine {
public:
    Machine(const IrProgram& program, Limits limits, std::map<SymbolId, Value> globals, Heap heap)
        : program_(program), limits_(limits), globals_(std::move(globals)), heap_(std::move(heap)) {
        for (const auto& body : program.bodies) {
            bodies_.emplace(body.owner, &body);
            if (program.symbols.at(body.owner).kind == SymbolKind::Class) {
                for (const auto& instruction : body.instructions) {
                    if (instruction.op == IrOp::Initialize) {
                        field_initializers_[body.owner].push_back(&instruction);
                    }
                }
            }
        }
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].kind == SymbolKind::Function) {
                functions_by_owner_[program.symbols[id].owner].push_back(id);
            }
        }
    }

    const std::map<SymbolId, Value>& globals() const { return globals_; }
    const Heap& heap() const { return heap_; }

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
    Heap heap_;
    std::map<SymbolId, const IrBody*> bodies_;
    std::map<SymbolId, std::vector<const IrNode*>> field_initializers_;
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
            outcome.trap_reason = stop.trap_reason;
            outcome.location = stop.location;
        }
        outcome.effects = effects_;
        outcome.globals = globals_;
        return outcome;
    }

    [[noreturn]] static void unsupported(const IrNode& node, std::string reason) {
        throw Stop{Status::Unsupported, std::move(reason), node.span};
    }

    [[noreturn]] static void trap(const IrNode& node, std::uint32_t reason, std::string description) {
        throw Stop{Status::Trapped, std::move(description), node.span, reason};
    }

    [[noreturn]] static void fault(const IrNode& node, std::string description) {
        throw Stop{Status::Faulted, std::move(description), node.span};
    }

    void spend(const Span& where) {
        if (++spent_ > limits_.budget) {
            throw Stop{Status::LimitReached, "loop and call budget exhausted", where};
        }
    }

    Value invoke(SymbolId function, SymbolId receiver, const std::vector<Value>& arguments, const Span& site,
                 std::optional<Value> self = std::nullopt) {
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
        frame.self = std::move(self);
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
                object(*frame.self, node).fields[node.symbol] = std::move(value);
            }
            return Flow::Next;
        }
        case IrOp::Store: {
            const auto& destination = node.operands.at(0);
            if (destination.op == IrOp::Field) {
                // The receiver is evaluated before the stored value.
                const auto receiver = evaluate(destination.operands.at(0), frame);
                auto value = evaluate(node.operands.at(1), frame);
                object(receiver, node).fields[destination.symbol] = std::move(value);
                return Flow::Next;
            }
            if (destination.op == IrOp::Index) {
                const auto collection = evaluate(destination.operands.at(0), frame);
                const auto index = evaluate(destination.operands.at(1), frame);
                auto value = evaluate(node.operands.at(1), frame);
                auto& entry = collection_entry(collection, node);
                entry.elements[element_index(entry, index, destination)] = std::move(value);
                return Flow::Next;
            }
            if (destination.op != IrOp::Load) {
                unsupported(destination, "store destination");
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
                // The latch runs after the body completes or continues.
                run_region(node.alternative, frame);
            }
            return Flow::Next;
        case IrOp::Fail:
            trap(node, 5, "collection modified during iteration");
        case IrOp::Destroy: {
            const auto target = evaluate(node.operands.at(0), frame);
            const auto cls = object(target, node).cls;
            destruct(cls, target, node.span);
            object(target, node).freed = true;
            return Flow::Next;
        }
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
                if (!frame.self) {
                    unsupported(node, "object receiver outside a method");
                }
                return *frame.self;
            }
            return module_value(frame.receiver != no_symbol ? frame.receiver : node.symbol);
        case IrOp::Field: {
            const auto receiver = evaluate(node.operands.at(0), frame);
            auto& entry = object(receiver, node);
            const auto found = entry.fields.find(node.symbol);
            if (found == entry.fields.end()) {
                fault(node, "read of an uninitialized field");
            }
            return found->second;
        }
        case IrOp::Allocate: {
            auto values = arguments(node, 0, frame);
            heap_.entries.push_back({HeapEntry::Kind::Object, node.type.symbol, {}, {}, 0, false});
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = heap_.entries.size() - 1;
            construct(node.type.symbol, result, values, node.span);
            return result;
        }
        case IrOp::Collection: {
            auto values = arguments(node, 0, frame);
            const auto kind = node.type.kind == TypeKind::List ? HeapEntry::Kind::List : HeapEntry::Kind::Array;
            heap_.entries.push_back({kind, no_symbol, {}, std::move(values), 0, false});
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = heap_.entries.size() - 1;
            return result;
        }
        case IrOp::Index: {
            const auto collection = evaluate(node.operands.at(0), frame);
            const auto index = evaluate(node.operands.at(1), frame);
            auto& entry = collection_entry(collection, node);
            return entry.elements[element_index(entry, index, node)];
        }
        case IrOp::Length: {
            const auto collection = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, collection_entry(collection, node).elements.size());
        }
        case IrOp::CollectionVersion: {
            const auto collection = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, collection_entry(collection, node).version);
        }
        case IrOp::Append: {
            const auto collection = evaluate(node.operands.at(0), frame);
            auto value = evaluate(node.operands.at(1), frame);
            auto& entry = collection_entry(collection, node);
            entry.elements.push_back(std::move(value));
            ++entry.version;
            return Value{};
        }
        case IrOp::RemoveAt: {
            const auto collection = evaluate(node.operands.at(0), frame);
            const auto index = evaluate(node.operands.at(1), frame);
            auto& entry = collection_entry(collection, node);
            const auto position = element_index(entry, index, node);
            entry.elements.erase(entry.elements.begin() + static_cast<std::ptrdiff_t>(position));
            ++entry.version;
            return Value{};
        }
        case IrOp::Convert:
            return convert(node, evaluate(node.operands.at(0), frame));
        case IrOp::BaseInitialize: {
            auto values = arguments(node, 1, frame);
            const auto self = evaluate(node.operands.at(0), frame);
            construct(symbol(owner_class(frame)).base, self, values, node.span);
            return Value{};
        }
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
            auto values = arguments(node, 1, frame);
            if (receiver.kind == Value::Kind::Reference) {
                // Dispatch on the runtime class, by name, as overrides match.
                const auto cls = object(receiver, node).cls;
                return invoke(implementation(cls, node.symbol, node), no_symbol, values, node.span, receiver);
            }
            if (receiver.kind != Value::Kind::Module) {
                unsupported(node, "method call on this receiver");
            }
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
        case IrOp::Float: {
            const auto format = float_of(node.type, node);
            // strtod and strtof round correctly; for the 16-bit formats the
            // double is rounded again.
            const auto text = node.text;
            if (format.width() == 32) {
                const float value = std::strtof(text.c_str(), nullptr);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &value, 4);
                return float_value(node.type, bits);
            }
            return float_value(node.type, floats::encode(std::strtod(text.c_str(), nullptr), format));
        }
        default:
            unsupported(node, "expression operation");
        }
    }

    static floats::Format float_of(const IrType& type, const IrNode& node) {
        if (type.name == "float16") return {5, 10};
        if (type.name == "bfloat16") return {8, 7};
        if (type.name == "float32") return {8, 23};
        if (type.name == "float64") return {11, 52};
        unsupported(node, "float type " + type.name);
    }

    static Value float_value(const IrType& type, std::uint64_t bits) {
        Value result;
        result.kind = Value::Kind::Float;
        result.type = type;
        result.magnitude = bits;
        return result;
    }

    Value unary(const IrNode& node, const Value& operand) {
        if (node.text == "!" && operand.kind == Value::Kind::Bool) {
            return bool_value(!operand.boolean);
        }
        if (node.text == "-" && operand.kind == Value::Kind::Float) {
            const auto format = float_of(operand.type, node);
            return float_value(operand.type, operand.magnitude ^ (std::uint64_t{1} << (format.width() - 1)));
        }
        if (node.text != "-" || operand.kind != Value::Kind::Integer) {
            unsupported(node, "unary operation");
        }
        return arithmetic_result(node, negated(exact(operand)));
    }

    Value arithmetic_result(const IrNode& node, const Exact& result) {
        const auto format = format_of(node.type);
        if (!format) {
            unsupported(node, "arithmetic on " + node.type.name);
        }
        if (!fits(result, *format)) {
            trap(node, 1, "overflow");
        }
        return integer_value(node.type, result.negative, result.magnitude);
    }

    Value convert(const IrNode& node, const Value& operand) {
        if (node.type.kind == TypeKind::Float) {
            const auto to = float_of(node.type, node);
            if (operand.kind == Value::Kind::Float) {
                return float_value(node.type, floats::convert(operand.magnitude, float_of(operand.type, node), to));
            }
            // The conversion to double rounds correctly; a second rounding
            // to a narrower format is innocuous (float_reference.hpp).
            const double magnitude = static_cast<double>(operand.magnitude);
            return float_value(node.type, floats::encode(operand.negative ? -magnitude : magnitude, to));
        }
        if (operand.kind == Value::Kind::Float) {
            const double value = floats::decode(operand.magnitude, float_of(operand.type, node));
            if (std::isnan(value) || std::isinf(value)) {
                trap(node, 1, "conversion of a NaN or an infinity");
            }
            const double truncated = std::trunc(value);
            if (std::fabs(truncated) >= 18446744073709551616.0) {
                trap(node, 1, "conversion out of range");
            }
            const Exact exact_value{truncated < 0, static_cast<U64>(std::fabs(truncated)), false};
            const auto format = format_of(node.type);
            if (!format) {
                unsupported(node, "conversion to " + node.type.name);
            }
            if (!fits(normalized(exact_value), *format)) {
                trap(node, 1, "conversion out of range");
            }
            return integer_value(node.type, exact_value.negative && exact_value.magnitude != 0, exact_value.magnitude);
        }
        Exact value;
        if (operand.kind == Value::Kind::Integer) {
            value = exact(operand);
        } else if (operand.kind == Value::Kind::Variant) {
            value.magnitude = ordinal(operand.symbol);
        } else {
            unsupported(node, "conversion of this operand");
        }
        const auto format = format_of(node.type);
        if (!format) {
            unsupported(node, "conversion to " + node.type.name);
        }
        if (!fits(value, *format)) {
            trap(node, 1, "conversion out of range");
        }
        return integer_value(node.type, value.negative, value.magnitude);
    }

    // Position of a variant among its enum's variants, in declaration order.
    U64 ordinal(SymbolId variant) const {
        U64 position = 0;
        for (SymbolId id = 0; id < variant; ++id) {
            if (symbol(id).kind == SymbolKind::Variant && symbol(id).owner == symbol(variant).owner) {
                ++position;
            }
        }
        return position;
    }

    HeapEntry& entry(const Value& value, const IrNode& node) {
        if (value.kind != Value::Kind::Reference || value.handle >= heap_.entries.size()) {
            unsupported(node, "a reference operand that is not a reference");
        }
        auto& item = heap_.entries[value.handle];
        if (item.freed) {
            fault(node, "use of a destroyed object");
        }
        return item;
    }

    HeapEntry& object(const Value& value, const IrNode& node) {
        auto& item = entry(value, node);
        if (item.kind != HeapEntry::Kind::Object) {
            unsupported(node, "an object operand that is a collection");
        }
        return item;
    }

    HeapEntry& collection_entry(const Value& value, const IrNode& node) {
        auto& item = entry(value, node);
        if (item.kind == HeapEntry::Kind::Object) {
            unsupported(node, "a collection operand that is an object");
        }
        return item;
    }

    // A negative index is a program trap; one at or past the length is a
    // runtime bounds fault.
    std::size_t element_index(const HeapEntry& item, const Value& index, const IrNode& node) {
        if (index.kind != Value::Kind::Integer) {
            unsupported(node, "a non-integer index");
        }
        if (index.negative) {
            trap(node, 2, "negative index");
        }
        if (index.magnitude >= item.elements.size()) {
            fault(node, "index out of bounds");
        }
        return static_cast<std::size_t>(index.magnitude);
    }

    SymbolId own_member(SymbolId owner, std::string_view name) const {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.owner == owner && item.name == name &&
                (item.kind == SymbolKind::Function || item.kind == SymbolKind::Field)) {
                return id;
            }
        }
        return no_symbol;
    }

    // The method with `method`'s name that the runtime class or its nearest
    // base defines with a body.
    SymbolId implementation(SymbolId cls, SymbolId method, const IrNode& node) const {
        for (auto scope = cls; scope != no_symbol; scope = symbol(scope).base) {
            const auto found = own_member(scope, symbol(method).name);
            if (found != no_symbol && symbol(found).kind == SymbolKind::Function && symbol(found).has_body) {
                return found;
            }
        }
        unsupported(node, "no implementation of " + symbol(method).name);
    }

    SymbolId owner_class(const Frame& frame) const {
        return frame.function == no_symbol ? no_symbol : symbol(frame.function).owner;
    }

    // Construction order (Object lifecycle): the base, then this class's
    // field initializers, then the rest of its declared constructor.
    void construct(SymbolId cls, const Value& self, const std::vector<Value>& arguments, const Span& site) {
        spend(site);
        if (depth_ >= limits_.depth) {
            throw Stop{Status::LimitReached, "call depth limit", site};
        }
        ++depth_;
        const auto init = own_member(cls, "init");
        Frame frame;
        frame.self = self;
        frame.function = init;
        if (init != no_symbol) {
            const auto& parameters = symbol(init).parameters;
            for (std::size_t i = 0; i < parameters.size() && i < arguments.size(); ++i) {
                frame.locals[parameters[i]] = arguments[i];
            }
            const auto& body = bodies_.at(init)->instructions;
            std::size_t start = 0;
            if (symbol(cls).base != no_symbol && !body.empty()) {
                run_statement(body.front(), frame);
                start = 1;
            }
            initialize_fields(cls, self);
            for (std::size_t i = start; i < body.size(); ++i) {
                if (run_statement(body[i], frame) == Flow::Return) {
                    break;
                }
            }
        } else {
            if (symbol(cls).base != no_symbol) {
                construct(symbol(cls).base, self, {}, site);
            }
            initialize_fields(cls, self);
        }
        --depth_;
    }

    void initialize_fields(SymbolId cls, const Value& self) {
        const auto found = field_initializers_.find(cls);
        if (found == field_initializers_.end()) {
            return;
        }
        Frame frame;
        frame.self = self;
        for (const auto* initializer : found->second) {
            run_statement(*initializer, frame);
        }
    }

    // Cleanup runs from the most derived class toward its bases.
    void destruct(SymbolId cls, const Value& self, const Span& site) {
        const auto cleanup = own_member(cls, "deinit");
        if (cleanup != no_symbol && symbol(cleanup).kind == SymbolKind::Function && symbol(cleanup).has_body) {
            invoke(cleanup, no_symbol, {}, site, self);
        }
        if (symbol(cls).base != no_symbol) {
            destruct(symbol(cls).base, self, site);
        }
    }

    Value binary(const IrNode& node, const Value& left, const Value& right) {
        const auto& op = node.text;
        if (left.kind == Value::Kind::Bool && right.kind == Value::Kind::Bool && (op == "==" || op == "!=")) {
            return bool_value((left.boolean == right.boolean) == (op == "=="));
        }
        if (left.kind == Value::Kind::Variant && right.kind == Value::Kind::Variant && (op == "==" || op == "!=")) {
            return bool_value((left.symbol == right.symbol) == (op == "=="));
        }
        if (left.kind == Value::Kind::String && right.kind == Value::Kind::String && op == "+") {
            Value result = left;
            result.text += right.text;
            return result;
        }
        if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Float) {
            const auto format = float_of(left.type, node);
            const auto a = left.magnitude;
            const auto b = right.magnitude;
            if (op == "==") return bool_value(floats::compare(floats::Compare::Equal, a, b, format));
            if (op == "!=") return bool_value(floats::compare(floats::Compare::NotEqual, a, b, format));
            if (op == "<") return bool_value(floats::compare(floats::Compare::Less, a, b, format));
            if (op == "<=") return bool_value(floats::compare(floats::Compare::LessEqual, a, b, format));
            if (op == ">") return bool_value(floats::compare(floats::Compare::Greater, a, b, format));
            if (op == ">=") return bool_value(floats::compare(floats::Compare::GreaterEqual, a, b, format));
            const auto operation = op == "+"   ? floats::Op::Add
                                   : op == "-" ? floats::Op::Subtract
                                   : op == "*" ? floats::Op::Multiply
                                               : floats::Op::Divide;
            return float_value(node.type, floats::binary(operation, a, b, format));
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
        if (op == "+") return arithmetic_result(node, add(a, b));
        if (op == "-") return arithmetic_result(node, add(a, negated(b)));
        if (op == "*") return arithmetic_result(node, multiply(a, b));
        if (op == "/" || op == "%") {
            if (b.magnitude == 0) {
                trap(node, 1, "division by zero");
            }
            // Truncating division on magnitudes; the remainder takes the
            // dividend's sign.
            if (op == "/") {
                return arithmetic_result(node, normalized({a.negative != b.negative, a.magnitude / b.magnitude, false}));
            }
            return arithmetic_result(node, normalized({a.negative, a.magnitude % b.magnitude, false}));
        }
        unsupported(node, "binary operator " + op);
    }
};

Evaluator::Evaluator(const IrProgram& program, Limits limits) : program_(program), limits_(limits) {}

Evaluator::~Evaluator() = default;

std::vector<Outcome> Evaluator::initialize() {
    // Modules.md, Initialization: literal globals hold their values first.
    // Then the files run, each after the files it imports (ties in program
    // order), and each file's initializers and statements run in source
    // order.
    const auto& symbols = program_.symbols;
    const auto file_of = [&](SymbolId id) {
        while (id != no_symbol && symbols.at(id).kind != SymbolKind::File) {
            id = symbols.at(id).owner;
        }
        return id;
    };
    const auto literal = [&](const IrNode& node) {
        return node.op == IrOp::Integer || node.op == IrOp::Boolean || node.op == IrOp::String ||
               (node.op == IrOp::Reference && symbols.at(node.symbol).kind == SymbolKind::Variant);
    };
    std::vector<SymbolId> files;
    std::map<SymbolId, std::vector<SymbolId>> imports;
    for (SymbolId id = 0; id < symbols.size(); ++id) {
        if (symbols[id].kind == SymbolKind::File) {
            files.push_back(id);
        } else if (symbols[id].kind == SymbolKind::Import && symbols[id].type.symbol != no_symbol) {
            const auto target = file_of(symbols[id].type.symbol);
            if (target != no_symbol && target != file_of(id)) {
                imports[file_of(id)].push_back(target);
            }
        }
    }
    std::vector<SymbolId> order;
    while (order.size() < files.size()) {
        SymbolId chosen = no_symbol;
        for (const auto file : files) {
            const bool done = std::find(order.begin(), order.end(), file) != order.end();
            const bool ready = std::all_of(imports[file].begin(), imports[file].end(), [&](SymbolId dependency) {
                return std::find(order.begin(), order.end(), dependency) != order.end();
            });
            if (!done && ready) {
                chosen = file;
                break;
            }
        }
        for (const auto file : files) {
            if (chosen == no_symbol && std::find(order.begin(), order.end(), file) == order.end()) {
                chosen = file;
            }
        }
        order.push_back(chosen);
    }

    Machine machine(program_, limits_, {}, {});
    std::vector<Outcome> outcomes;
    struct Item {
        const IrNode* node;
        SymbolId owner;
    };
    std::vector<Item> literals;
    std::vector<std::pair<SymbolId, std::vector<Item>>> steps;
    for (const auto file : order) {
        std::vector<Item> items;
        for (const auto& body : program_.bodies) {
            const auto kind = symbols.at(body.owner).kind;
            if ((kind != SymbolKind::File && kind != SymbolKind::Module) || file_of(body.owner) != file) {
                continue;
            }
            for (const auto& instruction : body.instructions) {
                auto& target = instruction.op == IrOp::Initialize && literal(instruction.operands.front()) ? literals
                                                                                                          : items;
                target.push_back({&instruction, kind == SymbolKind::Module ? body.owner : no_symbol});
            }
        }
        std::stable_sort(items.begin(), items.end(),
                         [](const Item& a, const Item& b) { return a.node->span.offset < b.node->span.offset; });
        steps.emplace_back(file, std::move(items));
    }
    for (const auto& item : literals) {
        outcomes.push_back(machine.run_body({item.owner, {*item.node}}, item.owner));
    }
    for (const auto& [file, items] : steps) {
        for (const auto& item : items) {
            outcomes.push_back(machine.run_body({item.owner, {*item.node}}, item.owner));
            if (outcomes.back().status != Status::Returned) {
                break;
            }
        }
    }
    initial_globals_ = machine.globals();
    initial_heap_ = std::make_shared<const Heap>(machine.heap());
    return outcomes;
}

Outcome Evaluator::call(SymbolId function, const std::vector<Value>& arguments) {
    Machine machine(program_, limits_, initial_globals_, initial_heap_ ? *initial_heap_ : Heap{});
    return machine.call(function, arguments);
}

Outcome Evaluator::evaluate(const IrNode& expression) {
    // A closed expression cannot write globals, so one machine is reused.
    if (!closed_) {
        closed_ = std::make_unique<Machine>(program_, limits_, std::map<SymbolId, Value>{}, Heap{});
    }
    return closed_->evaluate_closed(expression);
}

} // namespace neura::testing

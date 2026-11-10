#include "static_initialization.hpp"

#include "ir-optimization/integer_folding.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace neura {
namespace {

const IrType void_type{TypeKind::Void, "void", no_symbol, {}};
const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};

IrNode operation(IrOp op, const Span& span, const IrType& type = void_type, SymbolId id = no_symbol) {
    return {op, type, span, id, {}, {}, {}, {}, {}};
}

IrNode discard(IrNode expression) {
    auto result = operation(IrOp::Discard, expression.span);
    result.operands.push_back(std::move(expression));
    return result;
}

IrNode assignment(const Span& span, IrNode place, IrNode value) {
    auto result = operation(IrOp::Store, span);
    result.operands.push_back(std::move(place));
    result.operands.push_back(std::move(value));
    return result;
}

IrNode initialize(SymbolId target, IrNode value) {
    auto result = operation(IrOp::Initialize, value.span, void_type, target);
    result.operands.push_back(std::move(value));
    return result;
}

IrNode boolean(const Span& span, bool value) {
    auto result = operation(IrOp::Boolean, span, bool_type);
    result.text = value ? "true" : "false";
    return result;
}

bool constant_type(TypeKind kind) {
    return kind == TypeKind::Bool || kind == TypeKind::Integer ||
           kind == TypeKind::Float || kind == TypeKind::String;
}

std::optional<IrNode> default_value(const IrSymbol& field) {
    switch (field.type.kind) {
    case TypeKind::Bool: return boolean(field.span, false);
    case TypeKind::Object: return operation(IrOp::Nil, field.span, field.type);
    case TypeKind::Integer:
    case TypeKind::Float: {
        const bool floating = field.type.kind == TypeKind::Float;
        auto result = operation(floating ? IrOp::Float : IrOp::Integer, field.span, field.type);
        result.text = "0";
        return result;
    }
    default: return std::nullopt;
    }
}

bool comparison(std::string_view op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

class Initialization {
public:
    Initialization(IrProgram& program, const std::set<SymbolId>& singletons)
        : program_(program), singletons_(singletons), scopes_(program.symbols.size(), no_symbol),
          guards_(scopes_.size(), no_symbol), pending_(scopes_.size()) {}

    void run();

private:
    IrProgram& program_;
    const std::set<SymbolId>& singletons_;
    std::vector<SymbolId> scopes_;
    std::vector<SymbolId> guards_;
    std::vector<std::vector<IrNode>> pending_;
    std::map<SymbolId, std::size_t> scope_bodies_;
    std::set<SymbolId> defaults_;
    std::set<SymbolId> planned_;
    std::map<SymbolId, IrNode> candidates_;
    std::map<SymbolId, std::optional<IrNode>> constants_;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    SymbolId add(SymbolKind kind, std::string name, const Span& span, SymbolId owner, const IrType& type);
    SymbolId static_owner(SymbolId field) const;
    const std::optional<IrNode>& constant(SymbolId field);
    std::optional<IrNode> constant_expression(IrNode value);
    void prepare_fields();
    void collect_defaults(SymbolId interface, std::set<SymbolId>& seen, std::vector<SymbolId>& ordered);
    SymbolId plan(SymbolId type);
    std::vector<IrNode>& prepared_body(SymbolId scope);
    IrNode load(SymbolId id, const Span& span) const;
    void rewrite(IrNode& node, SymbolId owner, SymbolId former_owner = no_symbol);
    IrNode guarded_read(IrNode value, SymbolId guard);
    IrNode guarded_store(IrNode store, SymbolId guard, SymbolId owner);
};

SymbolId Initialization::add(SymbolKind kind, std::string name, const Span& span,
                             SymbolId owner, const IrType& type) {
    const auto id = program_.symbols.size();
    IrSymbol value{};
    value.kind = kind;
    value.name = std::move(name);
    value.span = span;
    value.owner = owner;
    value.type = type;
    program_.symbols.push_back(std::move(value));
    return id;
}

SymbolId Initialization::static_owner(SymbolId field) const {
    if (field == no_symbol) return no_symbol;
    const auto& item = symbol(field);
    if (item.owner == no_symbol || !is_static_scope(symbol(item.owner))) return no_symbol;
    return symbol(item.owner).owner;
}

const std::optional<IrNode>& Initialization::constant(SymbolId field) {
    // An empty entry also breaks cyclic constant definitions.
    auto [found, added] = constants_.try_emplace(field);
    if (added) {
        if (const auto candidate = candidates_.find(field); candidate != candidates_.end())
            found->second = constant_expression(candidate->second);
    }
    return found->second;
}

std::optional<IrNode> Initialization::constant_expression(IrNode value) {
    switch (value.op) {
    case IrOp::Boolean:
    case IrOp::Integer:
    case IrOp::Float:
    case IrOp::String:
        return value;
    case IrOp::Load:
        if (value.operands.empty()) return constant(value.symbol);
        return std::nullopt;
    case IrOp::Unary:
    case IrOp::Binary:
    case IrOp::Convert:
    case IrOp::TextEqual:
    case IrOp::ShortCircuit:
    case IrOp::SelectValue:
        break;
    default:
        return std::nullopt;
    }
    for (auto* region : {&value.operands, &value.body, &value.alternative}) {
        for (auto& child : *region) {
            auto constant = constant_expression(std::move(child));
            if (!constant) return std::nullopt;
            child = std::move(*constant);
        }
    }
    if (optimization::fold_scalar_literals(value)) return value;

    const auto& first = value.operands.front();
    if (first.op == IrOp::Boolean) {
        const bool truth = first.text == "true";
        if (value.op == IrOp::ShortCircuit)
            return truth == (value.text == "||") ? first : value.body.front();
        if (value.op == IrOp::SelectValue)
            return truth ? value.body.front() : value.alternative.front();
    }
    if (value.op == IrOp::Binary && value.type.kind == TypeKind::String &&
        first.op == IrOp::String && value.operands[1].op == IrOp::String) {
        auto text = operation(IrOp::String, value.span, value.type);
        text.text = first.text + value.operands[1].text;
        return text;
    }
    if (value.op == IrOp::TextEqual && first.op == IrOp::String && value.operands[1].op == IrOp::String)
        return boolean(value.span, (first.text == value.operands[1].text) == (value.text == "=="));

    // These expressions are total even if the value is computed by a circuit.
    // Checked integer arithmetic/conversions above must fold successfully.
    if (value.op == IrOp::ShortCircuit || value.op == IrOp::SelectValue ||
        (value.op == IrOp::Unary && (value.text == "!" || value.text == "~" ||
                                   first.type.kind == TypeKind::Float)) ||
        (value.op == IrOp::Binary && (comparison(value.text) || first.type.kind == TypeKind::Float)) ||
        (value.op == IrOp::Convert && value.type.kind == TypeKind::Float))
        return value;
    return std::nullopt;
}

void Initialization::prepare_fields() {
    for (const auto& [scope, index] : scope_bodies_) {
        for (const auto& entry : program_.bodies[index].instructions) {
            const auto& field = symbol(entry.symbol);
            if (field.constant && constant_type(field.type.kind))
                candidates_.emplace(entry.symbol, entry.operands.front());
        }
    }
    for (const auto& [scope, index] : scope_bodies_) {
        auto& body = program_.bodies[index].instructions;
        std::vector<IrNode> prepared;
        for (auto& entry : body) {
            const auto& field = symbol(entry.symbol);
            if (singletons_.contains(entry.symbol)) {
                prepared.push_back(std::move(entry));
            } else if (!constant(entry.symbol)) {
                if (auto initial = default_value(field))
                    prepared.push_back(initialize(entry.symbol, std::move(*initial)));
                pending_[symbol(scope).owner].push_back(std::move(entry));
            }
        }
        body = std::move(prepared);
    }
}

void Initialization::collect_defaults(SymbolId interface, std::set<SymbolId>& seen,
                                      std::vector<SymbolId>& ordered) {
    if (!seen.insert(interface).second) return;
    for (const auto& base : symbol(interface).interfaces)
        collect_defaults(base.symbol, seen, ordered);
    if (defaults_.contains(interface)) {
        if (const auto guard = plan(interface); guard != no_symbol)
            ordered.push_back(guard);
    }
}

std::vector<IrNode>& Initialization::prepared_body(SymbolId scope) {
    const auto [it, added] = scope_bodies_.try_emplace(scope, program_.bodies.size());
    if (added) program_.bodies.push_back({scope, {}});
    return program_.bodies[it->second].instructions;
}

SymbolId Initialization::plan(SymbolId type) {
    if (!planned_.insert(type).second) return guards_[type];
    std::vector<SymbolId> dependencies;
    if (symbol(type).kind == SymbolKind::Class) {
        if (const auto base = symbol(type).base; base != no_symbol) {
            if (const auto guard = plan(base); guard != no_symbol)
                dependencies.push_back(guard);
        }
        std::set<SymbolId> seen;
        for (const auto& interface : symbol(type).interfaces)
            collect_defaults(interface.symbol, seen, dependencies);
    }
    if (pending_[type].empty() && dependencies.empty()) return no_symbol;

    const auto span = symbol(type).span;
    auto& scope = scopes_[type];
    if (scope == no_symbol) {
        scope = add(SymbolKind::Module, static_scope_name, span, type, void_type);
        program_.symbols[scope].type = {TypeKind::Module, static_scope_name, scope, {}};
    }
    const auto flag = add(SymbolKind::Global, "$static_started", span, scope, bool_type);
    const auto guard = add(SymbolKind::Function, "$static_init", span, scope, void_type);
    program_.symbols[guard].has_body = true;
    guards_[type] = guard;
    prepared_body(scope).push_back(initialize(flag, boolean(span, false)));

    auto already_started = operation(IrOp::Branch, span);
    already_started.operands.push_back(load(flag, span));
    already_started.body.push_back(operation(IrOp::Return, span));
    std::vector<IrNode> body;
    body.push_back(std::move(already_started));
    body.push_back(assignment(span, load(flag, span), boolean(span, true)));
    for (const auto dependency : dependencies)
        body.push_back(discard(operation(IrOp::Call, span, void_type, dependency)));
    program_.bodies.push_back({guard, std::move(body)});
    return guard;
}

IrNode Initialization::load(SymbolId id, const Span& span) const {
    return operation(IrOp::Load, span, symbol(id).type, id);
}

IrNode Initialization::guarded_read(IrNode value, SymbolId guard) {
    auto result = operation(IrOp::ScopedValue, value.span, value.type);
    auto qualifier = std::move(value.operands);
    value.operands.clear();
    if (!qualifier.empty()) result.body.push_back(discard(std::move(qualifier.front())));
    if (const auto& literal = constant(value.symbol)) {
        const auto span = value.span;
        value = *literal;
        value.span = span;
        if (result.body.empty()) return value;
    } else {
        result.body.push_back(discard(operation(IrOp::Call, value.span, void_type, guard)));
    }
    result.body.push_back(std::move(value));
    return result;
}

IrNode Initialization::guarded_store(IrNode store, SymbolId guard, SymbolId owner) {
    auto result = operation(IrOp::Block, store.span);
    auto& destination = store.operands.front();
    if (!destination.operands.empty()) {
        result.body.push_back(discard(std::move(destination.operands.front())));
        destination.operands.clear();
    }
    auto& input = store.operands[1];
    const auto saved = add(SymbolKind::Local, "$static_rhs", input.span, owner, input.type);
    result.body.push_back(initialize(saved, std::move(input)));
    input = load(saved, store.span);
    result.body.push_back(discard(operation(IrOp::Call, store.span, void_type, guard)));
    result.body.push_back(std::move(store));
    return result;
}

void Initialization::rewrite(IrNode& node, SymbolId owner, SymbolId former_owner) {
    if (node.op == IrOp::Initialize && former_owner != no_symbol &&
        symbol(node.symbol).kind == SymbolKind::Local && symbol(node.symbol).owner == former_owner)
        program_.symbols[node.symbol].owner = owner;
    if (node.op == IrOp::Store) {
        auto& destination = node.operands[0];
        for (auto& input : destination.operands) rewrite(input, owner, former_owner);
        rewrite(node.operands[1], owner, former_owner);
        if (destination.op == IrOp::Load && symbol(destination.symbol).kind == SymbolKind::Global) {
            const auto type = static_owner(destination.symbol);
            if (type != no_symbol && guards_[type] != no_symbol)
                node = guarded_store(std::move(node), guards_[type], owner);
        }
        return;
    }
    for (auto* region : {&node.operands, &node.body, &node.alternative}) {
        for (auto& child : *region) rewrite(child, owner, former_owner);
    }
    if (node.op == IrOp::Load && symbol(node.symbol).kind == SymbolKind::Global) {
        const auto type = static_owner(node.symbol);
        if (type != no_symbol && !singletons_.contains(node.symbol) &&
            (guards_[type] != no_symbol || constant(node.symbol)))
            node = guarded_read(std::move(node), guards_[type]);
    } else if (node.op == IrOp::Allocate) {
        if (const auto guard = guards_[node.type.symbol]; guard != no_symbol) {
            auto result = operation(IrOp::ScopedValue, node.span, node.type);
            result.body.push_back(discard(operation(IrOp::Call, node.span, void_type, guard)));
            result.body.push_back(std::move(node));
            node = std::move(result);
        }
    }
}

void Initialization::run() {
    for (SymbolId id = 0; id < scopes_.size(); ++id) {
        const auto& item = symbol(id);
        if (is_static_scope(item)) scopes_[item.owner] = id;
        if (item.kind == SymbolKind::Function && item.has_body && item.owner != no_symbol &&
            symbol(item.owner).kind == SymbolKind::Interface)
            defaults_.insert(item.owner);
    }
    if (std::ranges::all_of(scopes_, [](SymbolId scope) { return scope == no_symbol; })) return;
    const auto source_bodies = program_.bodies.size();
    for (std::size_t i = 0; i < source_bodies; ++i) {
        const auto owner = program_.bodies[i].owner;
        if (is_static_scope(symbol(owner))) scope_bodies_.emplace(owner, i);
    }
    if (scope_bodies_.empty()) return;
    prepare_fields();
    for (SymbolId id = 0; id < scopes_.size(); ++id) {
        if (symbol(id).kind == SymbolKind::Class || symbol(id).kind == SymbolKind::Interface)
            plan(id);
    }
    for (auto& body : program_.bodies) {
        if (is_static_initializer(symbol(body.owner))) continue;
        for (auto& instruction : body.instructions) rewrite(instruction, body.owner);
        if (symbol(body.owner).kind == SymbolKind::Function) {
            const auto type = static_owner(body.owner);
            if (type != no_symbol && guards_[type] != no_symbol)
                body.instructions.insert(body.instructions.begin(),
                    discard(operation(IrOp::Call, symbol(body.owner).span, void_type, guards_[type])));
        }
    }
    for (auto& body : program_.bodies) {
        if (!is_static_initializer(symbol(body.owner))) continue;
        const auto type = static_owner(body.owner);
        for (auto& entry : pending_[type]) {
            auto value = std::move(entry.operands.front());
            rewrite(value, body.owner, scopes_[type]);
            body.instructions.push_back(assignment(entry.span, load(entry.symbol, entry.span), std::move(value)));
        }
    }
}

} // namespace

void lower_static_initializers(IrProgram& program, const std::set<SymbolId>& singletons) {
    Initialization(program, singletons).run();
}

} // namespace neura

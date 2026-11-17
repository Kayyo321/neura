#include "hook_arguments.hpp"

#include "attributes.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace neura {
namespace {

template<typename Visitor>
void walk(IrNode& node, const Visitor& visit) {
    for (auto& operand : node.operands) walk(operand, visit);
    for (auto& child : node.body) walk(child, visit);
    for (auto& child : node.alternative) walk(child, visit);
    visit(node);
}

template<typename Visitor>
void walk(IrProgram& program, const Visitor& visit) {
    for (auto& body : program.bodies)
        for (auto& instruction : body.instructions) walk(instruction, [&](IrNode& node) { visit(body.owner, node); });
}

bool call(const IrNode& node) {
    return node.op == IrOp::Call || node.op == IrOp::VirtualCall ||
           node.op == IrOp::Allocate || node.op == IrOp::BaseInitialize;
}

std::size_t first_argument(const IrNode& node, std::size_t parameters) {
    return node.op == IrOp::VirtualCall || node.op == IrOp::BaseInitialize ||
           (node.op == IrOp::Call && node.operands.size() == parameters + 1) ? 1 : 0;
}

bool default_span(const Span& value, const Span& call) {
    return value.length != 0 && value.file == call.file && value.offset == call.offset && value.length == call.length;
}

bool nil_default(const IrSymbol& parameter) {
    const auto* value = find_attribute(parameter, AttributeId::ParameterDefault);
    return value && value->value.items.size() == 2 && value->value.items.front().integer == 5;
}

// Captures of a spawn thunk move values admitted in the parent into the
// child's call. User-written lambda captures have ordinary binding events.
struct SpawnArguments {
    std::set<SymbolId> receivers;
    std::set<SymbolId> omitted_nil;
    std::map<SymbolId, IrType> types;
};

SpawnArguments spawn_arguments(IrProgram& program, const HookIndex& hooks) {
    std::map<SymbolId, SymbolId> constructors;
    walk(program, [&](SymbolId, const IrNode& node) {
        if (node.op == IrOp::Allocate && node.text == task_closure && node.symbol < program.symbols.size())
            constructors.emplace(program.symbols[node.symbol].owner, node.symbol);
    });
    SpawnArguments captures;
    if (constructors.empty()) return captures;
    walk(program, [&](SymbolId owner, IrNode& node) {
        if (owner >= program.symbols.size()) return;
        const auto& thunk = program.symbols[owner];
        const auto found = constructors.find(thunk.owner);
        if (found == constructors.end() || thunk.name != "$invoke") return;
        if (node.op == IrOp::Return) node.text = argument_transport;
        if (!call(node) || node.symbol >= program.symbols.size()) return;
        const auto& formals = program.symbols[node.symbol].parameters;
        const auto first = first_argument(node, formals.size());
        const auto& captured = program.symbols[found->second].parameters;
        for (std::size_t i = 0; i < node.operands.size(); ++i) {
            auto& operand = node.operands[i];
            if (operand.op != IrOp::Field || operand.symbol >= program.symbols.size()) continue;
            const auto& field = program.symbols[operand.symbol];
            if (field.owner != thunk.owner) continue;
            const auto parameter = std::ranges::find_if(captured, [&](SymbolId id) {
                return program.symbols[id].name == field.name;
            });
            if (parameter == captured.end()) continue;
            operand.text = argument_transport;
            if (i < first) {
                captures.receivers.insert(*parameter);
            } else if (i - first < formals.size()) {
                const auto& formal = program.symbols[formals[i - first]];
                const auto& type = formal.type;
                if (nil_default(formal) && default_span(operand.span, node.span)) captures.omitted_nil.insert(*parameter);
                if (hooks.carrying(type)) {
                    captures.types[operand.symbol] = type;
                    captures.types[*parameter] = type;
                }
            }
        }
    });
    return captures;
}

IrNode save(IrProgram& program, IrNode value, SymbolId owner, const IrType& type,
            bool argument, std::vector<IrNode>& prelude) {
    IrSymbol local{};
    local.kind = SymbolKind::Local;
    local.owner = owner;
    local.span = value.span;
    local.type = type;
    local.name = argument ? "$argument" : "$receiver";
    local.constant = true;
    const auto id = program.symbols.size();
    program.symbols.push_back(std::move(local));
    IrNode load{};
    load.op = IrOp::Load;
    load.type = type;
    load.span = value.span;
    load.symbol = id;
    IrNode initialize{};
    initialize.op = IrOp::Initialize;
    initialize.span = value.span;
    initialize.symbol = id;
    initialize.operands.push_back(std::move(value));
    prelude.push_back(std::move(initialize));
    return load;
}

} // namespace

void prepare_class_hook_arguments(IrProgram& program, const HookIndex& hooks) {
    auto captures = spawn_arguments(program, hooks);
    for (const auto& [id, type] : captures.types) program.symbols[id].type = type;
    walk(program, [&](SymbolId owner, IrNode& node) {
        // Spawn takes the constructed task function directly. A prelude
        // for its saved arguments runs outside Spawn, in the parent.
        if (node.op == IrOp::Spawn && node.operands.size() == 1 && node.operands.front().op == IrOp::ScopedValue) {
            auto ordered = std::move(node.operands.front());
            node.operands.front() = std::move(ordered.body.back());
            ordered.body.back() = std::move(node);
            ordered.type = ordered.body.back().type;
            node = std::move(ordered);
            return;
        }
        if (!call(node) || node.symbol >= program.symbols.size()) return;
        const auto parameters = program.symbols[node.symbol].parameters;
        const auto first = first_argument(node, parameters.size());
        if (node.operands.size() != first + parameters.size()) return;
        const auto carried = [&](const IrNode& operand) {
            return operand.op == IrOp::Load && operand.symbol < program.symbols.size() &&
                   program.symbols[operand.symbol].name == "$argument";
        };
        // Older prebuilt calls have the parameter's nil-default attribute
        // and the whole call span on the omitted literal. A written nil
        // has its own source span. New IR marks an omitted nil explicitly.
        const auto omitted = [&](std::size_t i) {
            const auto& value = node.operands[i];
            if (i < first || value.op != IrOp::Nil) return false;
            const auto formal = parameters[i - first];
            return value.text == default_argument_nil || captures.omitted_nil.contains(formal) ||
                   (nil_default(program.symbols[formal]) && default_span(value.span, node.span));
        };
        // A default before a written expression needs a prelude only if it
        // can run an admission. Reordered calls already have such a prelude.
        bool default_seen = false, needs_prelude = false;
        for (std::size_t i = first; i < node.operands.size(); ++i) {
            const auto& operand = node.operands[i];
            if (omitted(i)) {
                default_seen |= hooks.carrying(program.symbols[parameters[i - first]].type);
            } else if (default_seen && !carried(operand) && !transports_argument(operand)) {
                needs_prelude = true;
            }
        }
        std::vector<IrNode> prelude;
        for (std::size_t i = 0; i < node.operands.size(); ++i) {
            auto& operand = node.operands[i];
            if (operand.type.kind == TypeKind::Module) continue;
            const bool receiver = i < first || captures.receivers.contains(parameters[i - first]);
            const auto type = receiver ? operand.type : program.symbols[parameters[i - first]].type;
            if (needs_prelude && !omitted(i) && !carried(operand) &&
                operand.op != IrOp::Receiver) {
                operand = save(program, std::move(operand), owner, type, !receiver, prelude);
            }
            if (!receiver && carried(operand) && hooks.carrying(type)) {
                program.symbols[operand.symbol].type = type;
                captures.types[operand.symbol] = type;
                operand.text = argument_transport;
            } else if (receiver && i >= first && hooks.carrying(type)) {
                // A spawn saves its evaluated method receiver. The hidden
                // closure constructor moves it without a parameter setter.
                IrNode move{};
                move.op = IrOp::ScopedValue;
                move.type = type;
                move.span = operand.span;
                move.text = argument_transport;
                move.body.push_back(std::move(operand));
                operand = std::move(move);
            }
        }
        if (prelude.empty()) return;
        IrNode ordered{};
        ordered.op = IrOp::ScopedValue;
        ordered.type = node.type;
        ordered.span = node.span;
        ordered.body = std::move(prelude);
        ordered.body.push_back(std::move(node));
        node = std::move(ordered);
    });
    walk(program, [&](SymbolId, IrNode& node) {
        const auto found = captures.types.find(node.symbol);
        if (found == captures.types.end()) return;
        if (node.op == IrOp::Initialize) node.text = parameter_admission;
        if (node.op == IrOp::Load || node.op == IrOp::Field) node.type = found->second;
    });
}

} // namespace neura

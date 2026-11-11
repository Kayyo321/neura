#include "purity.hpp"

#include "host_binding.hpp"
#include "integer.hpp"
#include "kernel_signature.hpp"
#include "tags.hpp"

#include <algorithm>

namespace neura {
namespace {

template <typename Visit>
void each_node(const std::vector<IrNode>& nodes, const Visit& visit) {
    for (const auto& node : nodes) {
        visit(node);
        each_node(node.operands, visit);
        each_node(node.body, visit);
        each_node(node.alternative, visit);
    }
}

bool comparison(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

std::string display(const IrType& type) {
    if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
        return (type.arguments.empty() ? std::string("?") : display(type.arguments.front())) +
               (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    return type.name.empty() ? std::string("value") : type.name;
}

bool is_local(const IrProgram& program, SymbolId id) {
    return id < program.symbols.size() &&
           (program.symbols[id].kind == SymbolKind::Local || program.symbols[id].kind == SymbolKind::Parameter);
}

bool is_global(const IrProgram& program, SymbolId id) {
    return id < program.symbols.size() && program.symbols[id].kind == SymbolKind::Global;
}

} // namespace

bool immutable_type(const IrProgram& program, const IrType& type) {
    // Enums and classes that reach themselves are immutable when their other
    // parts are.
    std::set<SymbolId> visiting;
    const std::function<bool(const IrType&)> check = [&](const IrType& item) -> bool {
        switch (item.kind) {
        case TypeKind::Bool:
        case TypeKind::Integer:
        case TypeKind::Float:
        case TypeKind::String:
        case TypeKind::TypeParameter:
            return true;
        case TypeKind::Tuple:
            return std::ranges::all_of(item.arguments, check);
        case TypeKind::Enum: {
            if (item.symbol >= program.symbols.size()) return false;
            if (!visiting.insert(item.symbol).second) return true;
            for (const auto& variant : program.symbols) {
                if (variant.kind != SymbolKind::Variant || variant.owner != item.symbol) continue;
                if (!std::ranges::all_of(variant.payload_types, check)) return false;
            }
            return true;
        }
        case TypeKind::Object: {
            // A class marked #Immutable, with immutable type arguments; a
            // specialization's fields have the concrete types.
            if (item.symbol >= program.symbols.size() || program.symbols[item.symbol].kind != SymbolKind::Class ||
                !has_tag(program.symbols[item.symbol], TagId::Immutable) || !std::ranges::all_of(item.arguments, check)) {
                return false;
            }
            if (!visiting.insert(item.symbol).second) return true;
            std::set<SymbolId> chain;
            for (auto cls = item.symbol; cls < program.symbols.size() && chain.insert(cls).second;
                 cls = program.symbols[cls].base) {}
            for (const auto& field : program.symbols) {
                if (field.kind == SymbolKind::Field && chain.contains(field.owner) && !check(field.type)) return false;
            }
            return true;
        }
        default:
            return false;
        }
    };
    return check(type);
}

PurityAnalysis::PurityAnalysis(const IrProgram& program, std::function<bool(SymbolId)> opaque)
    : program_(program), opaque_(std::move(opaque)) {
    for (const auto& body : program.bodies) bodies_.emplace(body.owner, &body);
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind != SymbolKind::Function) continue;
        std::set<SymbolId> seen;
        for (auto base = item.overrides; valid(base) && seen.insert(base).second; base = symbol(base).overrides) {
            overriders_[base].push_back(id);
        }
    }
    std::map<SymbolId, std::vector<std::pair<SymbolId, Span>>> edges;
    std::map<SymbolId, std::vector<SymbolId>> callers;
    const auto is_opaque = [&](SymbolId id) {
        return opaque_ && valid(id) && symbol(id).kind == SymbolKind::Function && opaque_(id);
    };
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        if (!is_opaque(id) || has_tag(symbol(id), TagId::Pure)) continue;
        reasons_.emplace(id, Reason{"is declared in a prebuilt module without #Pure", symbol(id).span, no_symbol});
    }
    for (const auto& body : program.bodies) {
        if (is_opaque(body.owner)) continue;
        const auto fresh = fresh_locals(body);
        auto& out = edges[body.owner];
        each_node(body.instructions, [&](const IrNode& node) {
            if (!reasons_.contains(body.owner)) {
                if (auto reason = direct(body.owner, node, fresh)) reasons_.emplace(body.owner, std::move(*reason));
            }
            callees(node, out);
        });
        for (const auto& [callee, span] : out) callers[callee].push_back(body.owner);
    }
    // A function that can run one with a reason has that reason too.
    std::vector<SymbolId> pending;
    for (const auto& [owner, reason] : reasons_) pending.push_back(owner);
    while (!pending.empty()) {
        const auto callee = pending.back();
        pending.pop_back();
        const auto found = callers.find(callee);
        if (found == callers.end()) continue;
        for (const auto caller : found->second) {
            if (reasons_.contains(caller)) continue;
            const auto& out = edges[caller];
            const auto edge = std::ranges::find(out, callee, &std::pair<SymbolId, Span>::first);
            reasons_.emplace(caller, Reason{{}, edge->second, callee});
            pending.push_back(caller);
        }
    }
}

bool PurityAnalysis::function_value_call(const IrNode& node) const {
    if (node.op != IrOp::VirtualCall || !valid(node.symbol)) return false;
    const auto& item = symbol(node.symbol);
    return item.name == "$invoke" && valid(item.owner) && symbol(item.owner).name.starts_with("$signature");
}

void PurityAnalysis::callees(const IrNode& node, std::vector<std::pair<SymbolId, Span>>& out) const {
    const auto add = [&](SymbolId id) {
        if (valid(id)) out.emplace_back(id, node.span);
    };
    // The class whose constructor chain and field initializers run.
    const auto construct = [&](SymbolId cls) {
        std::set<SymbolId> seen;
        for (auto scope = cls; valid(scope) && seen.insert(scope).second; scope = symbol(scope).base) add(scope);
    };
    switch (node.op) {
    case IrOp::Call:
        add(node.symbol);
        break;
    case IrOp::VirtualCall:
        if (function_value_call(node)) break;
        // The union over every implementation the call can reach.
        add(node.symbol);
        if (const auto found = overriders_.find(node.symbol); found != overriders_.end()) {
            for (const auto id : found->second) add(id);
        }
        break;
    case IrOp::BaseInitialize:
        add(node.symbol);
        if (!node.operands.empty() && node.operands.front().type.kind == TypeKind::Object) {
            construct(node.operands.front().type.symbol);
        }
        break;
    case IrOp::Allocate:
        add(node.symbol);
        construct(valid(node.symbol) ? symbol(node.symbol).owner : node.type.symbol);
        break;
    default:
        break;
    }
}

// The locals of a body that only ever hold objects the body allocated: each
// assignment stores a new allocation or another such local, and no reference
// is taken to the local.
std::set<SymbolId> PurityAnalysis::fresh_locals(const IrBody& body) const {
    std::set<SymbolId> candidates;
    std::vector<std::pair<SymbolId, const IrNode*>> assignments;
    std::set<SymbolId> excluded;
    each_node(body.instructions, [&](const IrNode& node) {
        if (node.op == IrOp::Initialize && valid(node.symbol) && symbol(node.symbol).kind == SymbolKind::Local) {
            candidates.insert(node.symbol);
            if (!node.operands.empty()) assignments.emplace_back(node.symbol, &node.operands.front());
        } else if (node.op == IrOp::Store && node.operands.size() == 2 && node.operands.front().op == IrOp::Load &&
                   valid(node.operands.front().symbol)) {
            assignments.emplace_back(node.operands.front().symbol, &node.operands.back());
        } else if (node.op == IrOp::ReferencePlace) {
            excluded.insert(node.symbol);
        }
    });
    for (const auto id : excluded) candidates.erase(id);
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [local, value] : assignments) {
            if (!candidates.contains(local)) continue;
            const bool allocation = value->op == IrOp::Allocate || value->op == IrOp::Collection ||
                                    value->op == IrOp::RepeatCollection;
            const bool copy = value->op == IrOp::Load && candidates.contains(value->symbol);
            if (!allocation && !copy) {
                candidates.erase(local);
                changed = true;
            }
        }
    }
    return candidates;
}

// The object of a field or element write: a local that holds only objects the
// call allocated, or the receiver of a constructor or field initializer, which
// is the object being built.
bool PurityAnalysis::fresh_object(SymbolId owner, const IrNode& object, const std::set<SymbolId>& fresh) const {
    if (object.op == IrOp::Load) return fresh.contains(object.symbol);
    if (object.op != IrOp::Receiver || !valid(owner)) return false;
    const auto& item = symbol(owner);
    if (item.kind == SymbolKind::Class) return true;
    return item.kind == SymbolKind::Function && item.name == "init" && valid(item.owner) &&
           symbol(item.owner).kind == SymbolKind::Class;
}

std::optional<PurityAnalysis::Reason> PurityAnalysis::direct(SymbolId owner, const IrNode& node,
                                                             const std::set<SymbolId>& fresh) const {
    const auto global_name = [&](SymbolId id) {
        const auto& item = symbol(id);
        return "'" + item.name + "'";
    };
    // The started flag of first-use static initialization.
    const auto static_start = [&](SymbolId id) -> std::optional<std::string> {
        const auto& item = symbol(id);
        if (item.name != "$static_started" || !valid(item.owner) || !valid(symbol(item.owner).owner))
            return std::nullopt;
        return "runs the static initialization of '" + symbol(symbol(item.owner).owner).name + "'";
    };
    switch (node.op) {
    case IrOp::Load:
        if (!is_global(program_, node.symbol)) return std::nullopt;
        if (auto start = static_start(node.symbol)) return Reason{*start, node.span};
        if (!symbol(node.symbol).constant) {
            return Reason{"reads the reassignable binding " + global_name(node.symbol), node.span};
        }
        if (!immutable_type(program_, symbol(node.symbol).type)) {
            return Reason{"reads the binding " + global_name(node.symbol) + ", whose type " +
                              display(symbol(node.symbol).type) + " is mutable", node.span};
        }
        return std::nullopt;
    case IrOp::Initialize:
        if (is_global(program_, node.symbol)) {
            return Reason{"initializes the binding " + global_name(node.symbol), node.span};
        }
        return std::nullopt;
    case IrOp::Store: {
        if (node.operands.size() != 2) return Reason{"stores to an unknown place", node.span};
        const auto& destination = node.operands.front();
        if (destination.op == IrOp::Load) {
            if (!is_global(program_, destination.symbol)) return std::nullopt;
            if (auto start = static_start(destination.symbol)) return Reason{*start, destination.span};
            return Reason{"assigns the binding " + global_name(destination.symbol), destination.span};
        }
        if (destination.op == IrOp::Field && !destination.operands.empty()) {
            if (fresh_object(owner, destination.operands.front(), fresh)) return std::nullopt;
            return Reason{"assigns the field '" + (valid(destination.symbol) ? symbol(destination.symbol).name
                                                                                : std::string("?")) +
                              "' of an object it did not allocate", destination.span};
        }
        if (destination.op == IrOp::Index && !destination.operands.empty()) {
            if (fresh_object(owner, destination.operands.front(), fresh)) return std::nullopt;
            return Reason{"assigns an element of a collection it did not allocate", destination.span};
        }
        return Reason{"assigns through a reference", destination.span};
    }
    case IrOp::Append:
    case IrOp::RemoveAt:
        if (!node.operands.empty() && fresh_object(owner, node.operands.front(), fresh)) return std::nullopt;
        return Reason{"changes a list it did not allocate", node.span};
    case IrOp::ExternalCall:
        // A kernel reads its arguments and writes only the storage it is
        // given, inside the NVM (docs/runtime/Kernels.md).
        if (valid(node.symbol) && kernel_bound(symbol(node.symbol))) return std::nullopt;
        if (valid(node.symbol) && symbol(node.symbol).host_binding) {
            return Reason{"makes the host request " + symbol(node.symbol).host_binding->name, node.span};
        }
        return Reason{"makes a host request", node.span};
    case IrOp::Spawn:
    case IrOp::TaskScope:
        return Reason{"starts or scopes tasks", node.span};
    case IrOp::SchedulerCall:
        if (node.text == "transfer.check") return std::nullopt;
        return Reason{"uses the scheduler operation '" + node.text + "'", node.span};
    case IrOp::CallClosure:
        return Reason{"calls a function value", node.span};
    case IrOp::VirtualCall:
        if (function_value_call(node)) return Reason{"calls a function value", node.span};
        return std::nullopt;
    case IrOp::ClassHash:
    case IrOp::ClassText:
        return Reason{"uses an object's identity", node.span};
    case IrOp::Destroy:
        return Reason{"destroys an object", node.span};
    default:
        return std::nullopt;
    }
}

std::optional<PurityAnalysis::Impure> PurityAnalysis::impure(SymbolId function) const {
    auto found = reasons_.find(function);
    if (found == reasons_.end()) return std::nullopt;
    Impure result;
    std::set<SymbolId> seen{function};
    while (found->second.callee != no_symbol) {
        result.calls.emplace_back(found->second.callee, found->second.span);
        const auto next = reasons_.find(found->second.callee);
        if (next == reasons_.end() || !seen.insert(found->second.callee).second) break;
        found = next;
    }
    result.operation = found->second.operation;
    result.span = found->second.span;
    return result;
}

bool PurityAnalysis::cannot_trap(const IrNode& node) const {
    switch (node.op) {
    case IrOp::Boolean:
    case IrOp::Integer:
    case IrOp::Float:
    case IrOp::Nil:
    case IrOp::Reference:
    case IrOp::Receiver:
    case IrOp::Block:
    case IrOp::Branch:
    case IrOp::Return:
    case IrOp::Discard:
    case IrOp::ScopedValue:
    case IrOp::ShortCircuit:
    case IrOp::Call:
    case IrOp::VirtualCall:
        return true;
    case IrOp::Load:
        return is_local(program_, node.symbol);
    case IrOp::Initialize:
        return is_local(program_, node.symbol);
    case IrOp::Store:
        return node.operands.size() == 2 && node.operands.front().op == IrOp::Load &&
               is_local(program_, node.operands.front().symbol);
    case IrOp::Unary:
        return node.text == "!" || node.text == "~";
    case IrOp::Binary: {
        const auto operand = node.operands.empty() ? TypeKind::Void : node.operands.front().type.kind;
        const bool scalar = operand == TypeKind::Bool || operand == TypeKind::Integer;
        return scalar && (comparison(node.text) || (is_bitwise_binary(node.text) && !is_shift(node.text)));
    }
    default:
        return false;
    }
}

bool PurityAnalysis::total(SymbolId function) const {
    if (const auto known = total_.find(function); known != total_.end()) return known->second;
    // A call that reaches itself may not return.
    total_[function] = false;
    if (!valid(function) || !pure(function) || symbol(function).kind != SymbolKind::Function ||
        (opaque_ && opaque_(function))) {
        return false;
    }
    const auto body = bodies_.find(function);
    if (body == bodies_.end()) return false;
    bool result = true;
    each_node(body->second->instructions, [&](const IrNode& node) {
        if (!result) return;
        if (!cannot_trap(node)) {
            result = false;
        } else if (node.op == IrOp::Call) {
            result = total(node.symbol);
        } else if (node.op == IrOp::VirtualCall) {
            // Only a module function that nothing overrides has a fixed target.
            const bool module = !node.operands.empty() && node.operands.front().type.kind == TypeKind::Module &&
                                (node.operands.front().op == IrOp::Reference ||
                                 node.operands.front().op == IrOp::Receiver);
            result = module && !overriders_.contains(node.symbol) && valid(node.symbol) &&
                     valid(symbol(node.symbol).owner) && symbol(symbol(node.symbol).owner).kind == SymbolKind::Module &&
                     symbol(symbol(node.symbol).owner).base == no_symbol && total(node.symbol);
        }
    });
    total_[function] = result;
    return result;
}

void check_pure_tags(const IrProgram& program, const std::function<bool(SymbolId)>& opaque) {
    bool any = false;
    for (const auto& item : program.symbols) {
        if (item.kind == SymbolKind::Function && has_tag(item, TagId::Pure)) any = true;
    }
    if (!any) return;
    const PurityAnalysis analysis(program, opaque);
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != SymbolKind::Function || !has_tag(item, TagId::Pure)) continue;
        // A prebuilt module's tag was checked when the module was compiled.
        if (opaque && opaque(id)) continue;
        const auto reason = analysis.impure(id);
        if (!reason) continue;
        const bool method = item.owner < program.symbols.size() &&
                            (program.symbols[item.owner].kind == SymbolKind::Class ||
                             program.symbols[item.owner].kind == SymbolKind::Interface);
        std::vector<std::pair<SymbolId, Span>> hops;
        for (const auto& [callee, span] : reason->calls) {
            const auto& called = program.symbols[callee];
            if (called.kind != SymbolKind::Function || called.name == "$invoke") continue;
            hops.emplace_back(callee, span);
        }
        auto text = "#Pure " + std::string(method ? "method" : "function") + " '" + item.name + "' is not pure: ";
        text += hops.empty() ? "it " + reason->operation
                             : "it reaches '" + program.symbols[hops.back().first].name + "', which " +
                                   reason->operation;
        const auto at = tag_span(item, TagId::Pure).value_or(item.span);
        auto error = Diagnostic(at, std::move(text)).with_code("E0917").label("the function is marked #Pure here");
        for (const auto& [callee, span] : hops) error.label(span, "calls '" + program.symbols[callee].name + "'");
        error.label(reason->span, reason->operation + " here");
        error.note("a #Pure function may allocate and trap, but it writes only objects it allocates, reads no "
                   "reassignable state, and calls only pure functions");
        error.help("remove #Pure, or move the effect to the caller");
        throw error;
    }
}

} // namespace neura

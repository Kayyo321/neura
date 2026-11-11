#include "purity.hpp"

#include "host_binding.hpp"
#include "integer.hpp"
#include "kernel_signature.hpp"
#include "tags.hpp"

#include <algorithm>
#include <string>

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

std::string type_key(const IrType& type) {
    auto key = std::to_string(static_cast<int>(type.kind)) + ":" + std::to_string(type.symbol) + ":" + type.name;
    if (!type.arguments.empty()) {
        key += "<";
        for (const auto& argument : type.arguments) key += type_key(argument) + ",";
        key += ">";
    }
    return key;
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

bool PurityAnalysis::Roots::join(const Roots& other) {
    bool changed = false;
    if (other.fresh && !fresh) changed = fresh = true;
    if (other.unknown && !unknown) changed = unknown = true;
    for (const auto slot : other.slots) changed = slots.insert(slot).second || changed;
    for (const auto slot : other.inside) changed = inside.insert(slot).second || changed;
    return changed;
}

bool PurityAnalysis::Summary::same(const Summary& other) const {
    if (reason.has_value() != other.reason.has_value() || links != other.links || !(result == other.result) ||
        !(fresh_graph == other.fresh_graph) || deep != other.deep || writes.size() != other.writes.size()) {
        return false;
    }
    for (const auto& [slot, why] : writes) {
        if (!other.writes.contains(slot)) return false;
    }
    return true;
}

// One pass over a body, flow-insensitive: the roots of each local are the
// union over every value stored to it, and `fresh_graph_` holds the roots of
// whatever was stored into an object the call allocated. The pass repeats
// until neither grows.
class PurityWalker {
public:
    using Roots = PurityAnalysis::Roots;
    using Reason = PurityAnalysis::Reason;
    using Summary = PurityAnalysis::Summary;

    PurityWalker(const PurityAnalysis& analysis, SymbolId owner) : a_(analysis), owner_(owner) {
        if (a_.valid(owner) && a_.symbol(owner).kind == SymbolKind::Function) {
            const auto& parameters = a_.symbol(owner).parameters;
            for (std::size_t i = 0; i < parameters.size(); ++i) locals_[parameters[i]].slots.insert(i + 1);
        }
    }

    Summary run(const std::vector<const IrBody*>& bodies) {
        for (;;) {
            grew_ = false;
            out_ = Summary{};
            for (const auto* body : bodies) {
                for (const auto& node : body->instructions) eval(node);
            }
            if (!grew_) break;
        }
        out_.fresh_graph = fresh_graph_;
        out_.fresh_graph.fresh = false;
        return std::move(out_);
    }

private:
    const PurityAnalysis& a_;
    SymbolId owner_;
    std::map<SymbolId, Roots> locals_;
    Roots fresh_graph_;
    Summary out_;
    bool grew_ = false;

    static Roots unknown() {
        Roots roots;
        roots.unknown = true;
        return roots;
    }
    static Roots fresh() {
        Roots roots;
        roots.fresh = true;
        return roots;
    }
    // The objects that the objects of `roots` refer to directly.
    Roots step(const Roots& roots) const {
        Roots next;
        next.unknown = roots.unknown;
        next.inside = roots.inside;
        next.inside.insert(roots.slots.begin(), roots.slots.end());
        if (roots.fresh) {
            next.fresh = true;
            next.join(fresh_graph_);
        }
        return next;
    }
    // Every object reachable from those of `roots`, themselves included.
    Roots graph(Roots roots) const {
        for (bool grew = true; grew;) grew = roots.join(step(roots));
        return roots;
    }
    Roots typed(const IrType& type, Roots roots) const {
        return type.kind == TypeKind::Void || a_.immutable(type) ? Roots{} : roots;
    }
    void impure(Reason reason) {
        if (!out_.reason) out_.reason = std::move(reason);
    }
    void assign(SymbolId local, const Roots& value) {
        if (locals_[local].join(value)) grew_ = true;
    }
    // A write to the objects `target` names: the object of a slot itself, or
    // one reachable from it.
    void write(const Roots& target, const Reason& reason) {
        if (target.unknown) impure(reason);
        for (const auto slot : target.slots) out_.writes.emplace(slot, reason);
        for (const auto slot : target.inside) {
            out_.writes.emplace(slot, reason);
            out_.deep.insert(slot);
        }
    }
    // `value` becomes reachable from the objects `target` names.
    void link(const Roots& target, const Roots& value) {
        if (value.empty()) return;
        if (target.fresh && fresh_graph_.join(value)) grew_ = true;
        for (const auto slot : target.slots) out_.links[slot].join(value);
        for (const auto slot : target.inside) out_.links[slot].join(value);
    }
    std::string name(SymbolId id) const { return a_.valid(id) ? a_.symbol(id).name : std::string("?"); }

    Roots receiver(const IrNode& node) const {
        if (node.type.kind == TypeKind::Module) return unknown();
        Roots roots;
        roots.slots.insert(0);
        return roots;
    }

    // Applies the summaries of `targets` to a call whose slot k holds
    // `slots[k]`.
    Roots apply(const std::vector<SymbolId>& targets, const std::vector<Roots>& slots, const Span& span) {
        const auto slot = [&](std::size_t k) { return k < slots.size() ? slots[k] : Roots{}; };
        // A callee's roots in the caller's terms.
        const auto map = [&](const Roots& roots) {
            Roots mapped;
            mapped.fresh = roots.fresh;
            mapped.unknown = roots.unknown;
            for (const auto k : roots.slots) mapped.join(slot(k));
            for (const auto k : roots.inside) mapped.join(step(graph(slot(k))));
            return mapped;
        };
        Roots result;
        for (const auto target : targets) {
            if (!a_.valid(target)) continue;
            const auto* summary = a_.summary(target);
            if (!summary) {
                // A function without a body runs nothing, unless it is bound
                // to something outside the program.
                if (a_.symbol(target).external) {
                    impure(Reason{"calls the external function '" + name(target) + "'", span});
                    result.join(unknown());
                }
                continue;
            }
            if (summary->reason) impure(Reason{{}, span, target, std::nullopt});
            for (const auto& [k, why] : summary->writes) {
                write(summary->deep.contains(k) ? graph(slot(k)) : slot(k), Reason{{}, span, target, k});
            }
            // Objects the callee allocated and lets escape bring along what
            // it stored into them.
            bool escapes = summary->result.fresh;
            for (const auto& [k, roots] : summary->links) {
                link(graph(slot(k)), map(roots));
                escapes = escapes || roots.fresh;
            }
            if (escapes) link(fresh(), map(summary->fresh_graph));
            result.join(map(summary->result));
        }
        return result;
    }

    std::vector<Roots> operands(const IrNode& node, std::size_t first_slot, std::size_t skip = 0) {
        std::vector<Roots> slots(first_slot);
        for (std::size_t i = skip; i < node.operands.size(); ++i) slots.push_back(eval(node.operands[i]));
        return slots;
    }

    void children(const IrNode& node) {
        for (const auto& child : node.operands) eval(child);
        for (const auto& child : node.body) eval(child);
        for (const auto& child : node.alternative) eval(child);
    }

    std::optional<std::string> static_start(SymbolId id) const {
        const auto& item = a_.symbol(id);
        if (item.name != "$static_started" || !a_.valid(item.owner) || !a_.valid(a_.symbol(item.owner).owner))
            return std::nullopt;
        return "runs the static initialization of '" + a_.symbol(a_.symbol(item.owner).owner).name + "'";
    }

    Roots load(const IrNode& node) {
        for (const auto& qualifier : node.operands) eval(qualifier);
        if (!a_.valid(node.symbol)) return unknown();
        const auto& item = a_.symbol(node.symbol);
        if (item.kind == SymbolKind::Local || item.kind == SymbolKind::Parameter) {
            return typed(node.type, locals_[node.symbol]);
        }
        if (auto start = static_start(node.symbol)) {
            impure(Reason{*start, node.span});
        } else if (!item.constant) {
            impure(Reason{"reads the reassignable binding '" + item.name + "'", node.span});
        } else if (!a_.immutable(item.type)) {
            impure(Reason{"reads the binding '" + item.name + "', whose type " + display(item.type) + " is mutable",
                          node.span});
        }
        return typed(node.type, unknown());
    }

    // The destination's own reason comes first: it is what the check
    // reports for a store whose value is impure too.
    void store(const IrNode& node) {
        if (node.operands.size() != 2) {
            impure(Reason{"stores to an unknown place", node.span});
            children(node);
            return;
        }
        const auto& destination = node.operands.front();
        if (destination.op == IrOp::Load) {
            const bool local = a_.valid(destination.symbol) &&
                               (a_.symbol(destination.symbol).kind == SymbolKind::Local ||
                                a_.symbol(destination.symbol).kind == SymbolKind::Parameter);
            if (!a_.valid(destination.symbol)) {
                impure(Reason{"stores to an unknown place", destination.span});
            } else if (!local) {
                if (auto start = static_start(destination.symbol)) {
                    impure(Reason{*start, destination.span});
                } else {
                    impure(Reason{"assigns the binding '" + name(destination.symbol) + "'", destination.span});
                }
            }
            for (const auto& qualifier : destination.operands) eval(qualifier);
            const auto value = eval(node.operands.back());
            if (local) assign(destination.symbol, value);
            return;
        }
        if ((destination.op == IrOp::Field || destination.op == IrOp::Index) && !destination.operands.empty()) {
            const auto object = eval(destination.operands.front());
            const auto text = destination.op == IrOp::Field
                                  ? "assigns the field '" + name(destination.symbol) + "' of an object it did not allocate"
                                  : std::string("assigns an element of a collection it did not allocate");
            write(object, Reason{text, destination.span});
            for (std::size_t i = 1; i < destination.operands.size(); ++i) eval(destination.operands[i]);
            link(object, eval(node.operands.back()));
            return;
        }
        impure(Reason{"assigns through a reference", destination.span});
        children(node);
    }

    Roots kernel_call(const IrNode& node) {
        const auto& function = a_.symbol(node.symbol);
        const auto writes = kernel_writes(function);
        const auto binding = kernel_binding(function);
        const auto kernel = binding ? binding->first : function.name;
        for (std::size_t i = 0; i < node.operands.size(); ++i) {
            const auto value = eval(node.operands[i]);
            const bool written = writes ? i < writes->size() && (*writes)[i] : !value.empty();
            if (written) {
                write(value, Reason{"passes a list it did not allocate to the kernel " + kernel + ", which writes it",
                                    node.operands[i].span});
            }
        }
        return typed(node.type, fresh());
    }

public:
    Roots eval(const IrNode& node) {
        switch (node.op) {
        case IrOp::Load:
            return load(node);
        case IrOp::Initialize: {
            const auto value = node.operands.empty() ? Roots{} : eval(node.operands.front());
            if (!a_.valid(node.symbol)) return {};
            const auto kind = a_.symbol(node.symbol).kind;
            if (kind == SymbolKind::Local || kind == SymbolKind::Parameter) {
                assign(node.symbol, value);
            } else if (kind == SymbolKind::Global) {
                impure(Reason{"initializes the binding '" + name(node.symbol) + "'", node.span});
            } else {
                // A field initializer sets a field of the object being built.
                Roots object;
                object.slots.insert(0);
                write(object, Reason{"assigns the field '" + name(node.symbol) + "' of an object it did not allocate",
                                     node.span});
                link(object, value);
            }
            return {};
        }
        case IrOp::Store:
            store(node);
            return {};
        case IrOp::Append:
        case IrOp::RemoveAt: {
            if (node.operands.empty()) return {};
            const auto list = eval(node.operands.front());
            Roots value;
            for (std::size_t i = 1; i < node.operands.size(); ++i) value.join(eval(node.operands[i]));
            write(list, Reason{"changes a list it did not allocate", node.span});
            if (node.op == IrOp::Append) link(list, value);
            return {};
        }
        case IrOp::Receiver:
            return typed(node.type, receiver(node));
        case IrOp::Field:
        case IrOp::Index: {
            Roots from;
            for (std::size_t i = 0; i < node.operands.size(); ++i) {
                auto value = eval(node.operands[i]);
                if (i == 0) from = std::move(value);
            }
            return typed(node.type, step(from));
        }
        case IrOp::Project:
        case IrOp::EnumProject: {
            // A part of a tuple or enum value, which is not an object.
            Roots from;
            for (std::size_t i = 0; i < node.operands.size(); ++i) {
                auto value = eval(node.operands[i]);
                if (i == 0) from = std::move(value);
            }
            return typed(node.type, from);
        }
        case IrOp::ClassCast: {
            Roots value;
            for (const auto& child : node.operands) value.join(eval(child));
            return typed(node.type, value);
        }
        case IrOp::Tuple:
        case IrOp::EnumConstruct: {
            Roots value;
            for (const auto& child : node.operands) value.join(eval(child));
            return typed(node.type, value);
        }
        case IrOp::Collection:
        case IrOp::RepeatCollection:
        case IrOp::MakeClosure: {
            Roots parts;
            for (const auto& child : node.operands) parts.join(eval(child));
            link(fresh(), parts);
            return fresh();
        }
        case IrOp::Allocate: {
            auto slots = operands(node, 1);
            slots[0] = fresh();
            const auto cls = a_.valid(node.symbol) ? a_.symbol(node.symbol).owner : node.type.symbol;
            apply(a_.construction(cls, node.symbol), slots, node.span);
            return fresh();
        }
        case IrOp::BaseInitialize: {
            const auto slots = operands(node, 0);
            const auto cls = !node.operands.empty() && node.operands.front().type.kind == TypeKind::Object
                                 ? node.operands.front().type.symbol
                                 : no_symbol;
            apply(a_.construction(cls, node.symbol), slots, node.span);
            return {};
        }
        case IrOp::Call: {
            const auto slots = operands(node, 1);
            return typed(node.type, apply({node.symbol}, slots, node.span));
        }
        case IrOp::VirtualCall: {
            const auto slots = operands(node, 0);
            if (a_.function_value_call(node)) {
                impure(Reason{"calls a function value", node.span});
                return typed(node.type, unknown());
            }
            if (!node.operands.empty() && node.operands.front().type.kind == TypeKind::TypeParameter) {
                // Known only in a specialization, where it is checked.
                Roots result = fresh();
                for (const auto& value : slots) result.join(graph(value));
                return typed(node.type, result);
            }
            return typed(node.type, apply(a_.targets(node), slots, node.span));
        }
        case IrOp::ExternalCall:
            if (a_.valid(node.symbol) && kernel_bound(a_.symbol(node.symbol))) return kernel_call(node);
            children(node);
            if (a_.valid(node.symbol) && a_.symbol(node.symbol).host_binding) {
                impure(Reason{"makes the host request " + a_.symbol(node.symbol).host_binding->name, node.span});
            } else {
                impure(Reason{"makes a host request", node.span});
            }
            return typed(node.type, unknown());
        case IrOp::ValueEquals:
        case IrOp::ValueHash:
        case IrOp::ValueText: {
            const auto slots = operands(node, 0);
            if (node.operands.empty()) return {};
            bool deferred = false;
            const auto members = a_.dispatch_.value_members(node.operands.front().type, node.op, deferred);
            // The members run on the objects inside the value, so each slot
            // is everything the operand reaches.
            std::vector<Roots> inside;
            for (const auto& value : slots) inside.push_back(graph(value));
            apply(members, inside, node.span);
            return {};
        }
        case IrOp::Spawn:
        case IrOp::TaskScope:
            children(node);
            impure(Reason{"starts or scopes tasks", node.span});
            return typed(node.type, unknown());
        case IrOp::SchedulerCall: {
            Roots value;
            for (const auto& child : node.operands) value.join(eval(child));
            if (node.text == "transfer.check") return typed(node.type, value);
            impure(Reason{"uses the scheduler operation '" + node.text + "'", node.span});
            return typed(node.type, unknown());
        }
        case IrOp::CallClosure:
            children(node);
            impure(Reason{"calls a function value", node.span});
            return typed(node.type, unknown());
        case IrOp::ClassHash:
        case IrOp::ClassText:
            children(node);
            impure(Reason{"uses an object's identity", node.span});
            return {};
        case IrOp::Destroy:
            children(node);
            impure(Reason{"destroys an object", node.span});
            return {};
        case IrOp::ReferencePlace:
            // The local escapes as a mutable reference.
            children(node);
            if (a_.valid(node.symbol)) assign(node.symbol, unknown());
            return unknown();
        case IrOp::ScopedValue: {
            Roots value;
            for (std::size_t i = 0; i < node.body.size(); ++i) {
                auto result = eval(node.body[i]);
                if (i + 1 == node.body.size()) value = std::move(result);
            }
            return typed(node.type, value);
        }
        case IrOp::SelectValue: {
            for (const auto& child : node.operands) eval(child);
            Roots value;
            for (const auto& child : node.body) value.join(eval(child));
            for (const auto& child : node.alternative) value.join(eval(child));
            return typed(node.type, value);
        }
        case IrOp::Return:
            for (const auto& child : node.operands) out_.result.join(eval(child));
            return {};
        case IrOp::Nil:
        case IrOp::Reference:
            return node.type.kind == TypeKind::Module ? unknown() : Roots{};
        default:
            children(node);
            return typed(node.type, unknown());
        }
    }
};

PurityAnalysis::PurityAnalysis(const IrProgram& program, std::function<bool(SymbolId)> opaque)
    : program_(program), opaque_(std::move(opaque)), dispatch_(program) {
    for (const auto& body : program.bodies) bodies_[body.owner].push_back(&body);
    const auto is_opaque = [&](SymbolId id) {
        return opaque_ && valid(id) && symbol(id).kind == SymbolKind::Function && opaque_(id);
    };
    // A function of a prebuilt module has the summary its tag gives: pure,
    // with a result that may reach its arguments, or impure.
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        if (!is_opaque(id)) continue;
        Summary summary;
        if (has_tag(symbol(id), TagId::Pure)) {
            summary.result.fresh = true;
            for (std::size_t k = 0; k <= symbol(id).parameters.size(); ++k) {
                summary.result.slots.insert(k);
                summary.result.inside.insert(k);
            }
        } else {
            summary.reason = Reason{"is declared in a prebuilt module without #Pure", symbol(id).span};
        }
        summaries_.emplace(id, std::move(summary));
    }
    // A function bound to a kernel writes the lists its writable-view and
    // state parameters receive; one bound to a host service is impure.
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind != SymbolKind::Function || item.has_body || !item.external || summaries_.contains(id)) continue;
        Summary summary;
        if (kernel_bound(item)) {
            const auto writes = kernel_writes(item);
            const auto binding = kernel_binding(item);
            const auto kernel = binding ? binding->first : item.name;
            for (std::size_t i = 0; i < item.parameters.size(); ++i) {
                if (writes && !(i < writes->size() && (*writes)[i])) continue;
                const auto parameter = item.parameters[i];
                summary.writes.emplace(i + 1, Reason{"is bound to the kernel " + kernel + ", which writes the list " +
                                                         "passed for '" + symbol(parameter).name + "'",
                                                     symbol(parameter).span});
            }
            summary.result.fresh = true;
        } else if (item.host_binding) {
            summary.reason = Reason{"makes the host request " + item.host_binding->name, item.span};
        } else {
            summary.reason = Reason{"is an external function", item.span};
        }
        summaries_.emplace(id, std::move(summary));
    }
    // Callers, to revisit a body when a summary it applies changes.
    std::map<SymbolId, std::set<SymbolId>> callers;
    std::vector<SymbolId> order;
    for (const auto& [owner, bodies] : bodies_) {
        if (is_opaque(owner)) continue;
        order.push_back(owner);
        summaries_.emplace(owner, Summary{});
        std::vector<SymbolId> out;
        for (const auto* body : bodies) {
            each_node(body->instructions, [&](const IrNode& node) { callees(node, out); });
        }
        for (const auto callee : out) callers[callee].insert(owner);
    }
    std::set<SymbolId> queued(order.begin(), order.end());
    std::vector<SymbolId> pending(order.rbegin(), order.rend());
    while (!pending.empty()) {
        const auto owner = pending.back();
        pending.pop_back();
        queued.erase(owner);
        auto summary = PurityWalker(*this, owner).run(bodies_.at(owner));
        auto& current = summaries_.at(owner);
        const bool changed = !summary.same(current);
        // Summaries only grow, so this reaches a fixed point; the reasons of
        // the last pass name the first operation in body order.
        current = std::move(summary);
        if (!changed) continue;
        if (const auto found = callers.find(owner); found != callers.end()) {
            for (const auto caller : found->second) {
                if (queued.insert(caller).second) pending.push_back(caller);
            }
        }
    }
}

const PurityAnalysis::Summary* PurityAnalysis::summary(SymbolId function) const {
    const auto found = summaries_.find(function);
    return found == summaries_.end() ? nullptr : &found->second;
}

bool PurityAnalysis::immutable(const IrType& type) const {
    switch (type.kind) {
    case TypeKind::Bool:
    case TypeKind::Integer:
    case TypeKind::Float:
    case TypeKind::String:
    case TypeKind::TypeParameter:
        return true;
    case TypeKind::Void:
    case TypeKind::Array:
    case TypeKind::List:
    case TypeKind::Module:
        return false;
    default:
        break;
    }
    const auto key = type_key(type);
    if (const auto found = immutable_.find(key); found != immutable_.end()) return found->second;
    const bool result = immutable_type(program_, type);
    immutable_.emplace(key, result);
    return result;
}

bool PurityAnalysis::pure(SymbolId function) const {
    const auto* found = summary(function);
    return found && !found->reason && found->writes.empty();
}

std::vector<SymbolId> PurityAnalysis::targets(const IrNode& call) const {
    if (call.op != IrOp::VirtualCall || function_value_call(call)) return {};
    if (!call.operands.empty() && call.operands.front().type.kind == TypeKind::TypeParameter) return {};
    return dispatch_.of(call);
}

bool PurityAnalysis::function_value_call(const IrNode& node) const {
    if (node.op != IrOp::VirtualCall || !valid(node.symbol)) return false;
    const auto& item = symbol(node.symbol);
    return item.name == "$invoke" && valid(item.owner) && symbol(item.owner).name.starts_with("$signature");
}

std::vector<SymbolId> PurityAnalysis::construction(SymbolId cls, SymbolId init) const {
    // The constructor, and for the class and each base its field
    // initializers and, run implicitly, a constructor without parameters.
    std::vector<SymbolId> result;
    if (valid(init)) result.push_back(init);
    std::set<SymbolId> seen;
    for (auto scope = cls; valid(scope) && seen.insert(scope).second; scope = symbol(scope).base) {
        result.push_back(scope);
        for (const auto candidate : dispatch_.declared(scope, "init")) {
            if (candidate != init && symbol(candidate).parameters.empty()) result.push_back(candidate);
        }
    }
    return result;
}

void PurityAnalysis::callees(const IrNode& node, std::vector<SymbolId>& out) const {
    switch (node.op) {
    case IrOp::Call:
    case IrOp::ExternalCall:
        out.push_back(node.symbol);
        break;
    case IrOp::VirtualCall:
        for (const auto id : targets(node)) out.push_back(id);
        break;
    case IrOp::BaseInitialize:
        for (const auto id : construction(!node.operands.empty() && node.operands.front().type.kind == TypeKind::Object
                                              ? node.operands.front().type.symbol
                                              : no_symbol,
                                          node.symbol)) {
            out.push_back(id);
        }
        break;
    case IrOp::Allocate:
        for (const auto id : construction(valid(node.symbol) ? symbol(node.symbol).owner : node.type.symbol,
                                          node.symbol)) {
            out.push_back(id);
        }
        break;
    case IrOp::ValueEquals:
    case IrOp::ValueHash:
    case IrOp::ValueText:
        if (!node.operands.empty()) {
            bool deferred = false;
            for (const auto id : dispatch_.value_members(node.operands.front().type, node.op, deferred)) {
                out.push_back(id);
            }
        }
        break;
    default:
        break;
    }
}

std::optional<PurityAnalysis::Impure> PurityAnalysis::impure(SymbolId function) const {
    const auto first = [](const Summary& summary) -> const Reason* {
        if (summary.reason) return &*summary.reason;
        return summary.writes.empty() ? nullptr : &summary.writes.begin()->second;
    };
    const auto* start = summary(function);
    if (!start) {
        if (valid(function)) return Impure{{}, "has no body to check", symbol(function).span};
        return Impure{{}, "is not a function", {}};
    }
    const auto* reason = first(*start);
    if (!reason) return std::nullopt;
    Impure result;
    std::set<SymbolId> seen{function};
    while (reason->callee != no_symbol) {
        result.calls.emplace_back(reason->callee, reason->span);
        const auto* next_summary = summary(reason->callee);
        if (!next_summary || !seen.insert(reason->callee).second) break;
        const Reason* next = nullptr;
        if (reason->slot) {
            const auto found = next_summary->writes.find(*reason->slot);
            if (found != next_summary->writes.end()) next = &found->second;
        } else {
            next = first(*next_summary);
        }
        if (!next) break;
        reason = next;
    }
    result.operation = reason->operation.empty() ? "calls a function that is not pure" : reason->operation;
    result.span = reason->span;
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
    for (const auto* part : body->second) {
        each_node(part->instructions, [&](const IrNode& node) {
            if (!result) return;
            if (!cannot_trap(node)) {
                result = false;
            } else if (node.op == IrOp::Call) {
                result = total(node.symbol);
            } else if (node.op == IrOp::VirtualCall) {
                // A call on a module cannot meet a nil receiver; every
                // function it can run must be total.
                const bool module = !node.operands.empty() && node.operands.front().type.kind == TypeKind::Module &&
                                    (node.operands.front().op == IrOp::Reference ||
                                     node.operands.front().op == IrOp::Receiver);
                const auto reached = targets(node);
                result = module && !reached.empty() &&
                         std::ranges::all_of(reached, [&](SymbolId id) { return total(id); });
            }
        });
    }
    total_[function] = result;
    return result;
}

void check_pure_tags(const IrProgram& program, const std::function<bool(SymbolId)>& opaque, bool specialized) {
    bool any = false;
    for (const auto& item : program.symbols) {
        if (item.kind == SymbolKind::Function && has_tag(item, TagId::Pure)) any = true;
    }
    if (!any) return;
    const auto valid = [&](SymbolId id) { return id < program.symbols.size(); };
    // A generic declaration, or a member of one: after specialization only
    // its specializations run.
    const auto in_template = [&](SymbolId id) {
        std::set<SymbolId> seen;
        for (auto scope = id; valid(scope) && seen.insert(scope).second; scope = program.symbols[scope].owner) {
            if (!program.symbols[scope].generics.empty()) return true;
        }
        return false;
    };
    // The specialization a function belongs to, by its spelled name.
    const auto specialization = [&](SymbolId id) -> std::optional<std::string> {
        std::set<SymbolId> seen;
        for (auto scope = id; valid(scope) && seen.insert(scope).second; scope = program.symbols[scope].owner) {
            const auto& item = program.symbols[scope];
            if (item.name.find('<') != std::string::npos) {
                return scope == id ? item.name : item.name + "." + program.symbols[id].name;
            }
        }
        return std::nullopt;
    };
    const PurityAnalysis analysis(program, specialized ? std::function<bool(SymbolId)>{} : opaque);
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != SymbolKind::Function || !has_tag(item, TagId::Pure)) continue;
        // A prebuilt module's tag was checked when the module was compiled.
        if (!specialized && opaque && opaque(id)) continue;
        if (specialized && in_template(id)) continue;
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
        if (const auto name = specialized ? specialization(id) : std::nullopt) {
            error.note("in the specialization '" + *name + "': a generic function is checked with the type "
                       "arguments of each specialization, whose members it calls");
        }
        error.note("a #Pure function may allocate and trap, but it writes only objects it allocates, reads no "
                   "reassignable state, and calls only pure functions");
        error.help("remove #Pure, or move the effect to the caller");
        throw error;
    }
}

} // namespace neura

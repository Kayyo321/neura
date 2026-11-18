#include "purity.hpp"
#include "frozen.hpp"

#include "host_binding.hpp"
#include "integer.hpp"
#include "kernel_signature.hpp"
#include "attributes.hpp"
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
                if (field.kind != SymbolKind::Field || !chain.contains(field.owner)) continue;
                // #Frozen list storage is immutable when its elements are.
                const auto& stored = frozen_field(field) && field.type.kind == TypeKind::List &&
                                             field.type.arguments.size() == 1
                                         ? field.type.arguments.front()
                                         : field.type;
                if (!check(stored)) return false;
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
    if (other.opaque && !opaque) changed = opaque = true;
    for (const auto slot : other.slots) changed = slots.insert(slot).second || changed;
    for (const auto slot : other.inside) changed = inside.insert(slot).second || changed;
    return changed;
}

bool PurityAnalysis::Summary::same(const Summary& other) const {
    if (reason.has_value() != other.reason.has_value() || render.has_value() != other.render.has_value() ||
        links != other.links || !(result == other.result) ||
        !(fresh_graph == other.fresh_graph) || deep != other.deep || writes.size() != other.writes.size() ||
        allocates.has_value() != other.allocates.has_value() || allocation_unknown != other.allocation_unknown ||
        traps.has_value() != other.traps.has_value() || host.has_value() != other.host.has_value() ||
        scheduler.has_value() != other.scheduler.has_value() ||
        class_init.has_value() != other.class_init.has_value() || hooks.has_value() != other.hooks.has_value() ||
        !(optimizer == other.optimizer) || !(escapes == other.escapes) || !(leaks == other.leaks) ||
        !(shared_graph == other.shared_graph)) {
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
    using Cause = PurityAnalysis::Cause;

    PurityWalker(const PurityAnalysis& analysis, SymbolId owner) : a_(analysis) {
        if (a_.valid(owner) && a_.symbol(owner).kind == SymbolKind::Function) {
            const auto& parameters = a_.symbol(owner).parameters;
            for (std::size_t i = 0; i < parameters.size(); ++i) locals_[parameters[i]].slots.insert(i + 1);
        }
    }

    Summary run(const std::vector<const IrBody*>& bodies) {
        for (;;) {
            grew_ = false;
            out_ = Summary{};
            fresh_cause_.reset();
            result_cause_.reset();
            for (const auto* body : bodies) {
                for (const auto& node : body->instructions) eval(node);
            }
            if (!grew_) break;
        }
        out_.fresh_graph = fresh_graph_;
        out_.fresh_graph.fresh = false;
        out_.shared_graph = shared_graph_;
        out_.shared_graph.fresh = false;
        // What the objects that escape hold is retained with them.
        if (fresh_cause_) {
            for (const auto* graph : {&out_.fresh_graph, &out_.shared_graph}) {
                for (const auto slot : graph->slots) out_.retained_at.emplace(slot, *fresh_cause_);
                for (const auto slot : graph->inside) out_.retained_at.emplace(slot, *fresh_cause_);
            }
        }
        out_.ownership_cause = result_cause_;
        return std::move(out_);
    }

private:
    const PurityAnalysis& a_;
    std::map<SymbolId, Roots> locals_;
    Roots fresh_graph_;
    // Like fresh_graph_, for the objects of a callee that escape by a route
    // that purity does not follow; only retention reads it.
    Roots shared_graph_;
    Summary out_;
    bool grew_ = false;
    std::optional<Cause> fresh_cause_;
    std::optional<Cause> result_cause_;

    static Roots unknown() {
        Roots roots;
        roots.unknown = true;
        roots.opaque = true;
        return roots;
    }
    // A program global: unknown to purity, but a known place to ownership.
    static Roots global() {
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
        next.opaque = roots.opaque;
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
    // `renders` is true for the reasons that rendering allows (Html030.md,
    // "Typing and ownership"): reads of reassignable or mutable bindings, the
    // static initialization a read runs, and object identity.
    void impure(Reason reason, bool renders = false) {
        if (!renders && !out_.render) out_.render = reason;
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
    void link(const Roots& target, const Roots& value, const Span& at = {}, const std::string& what = {}) {
        if (value.empty()) return;
        // A place that no slot names outlives the call.
        if (target.unknown) retain(out_.escapes, value, what.empty() ? "stores it where the caller cannot see" : what, at);
        if (target.fresh && fresh_graph_.join(value)) grew_ = true;
        for (const auto slot : target.slots) out_.links[slot].join(value);
        for (const auto slot : target.inside) out_.links[slot].join(value);
    }
    std::string name(SymbolId id) const { return a_.valid(id) ? a_.symbol(id).name : std::string("?"); }

    // `roots` is kept past the call by `what`, into the escapes or the leaks.
    void retain(Roots& into, const Roots& roots, std::string what, const Span& at, SymbolId callee = no_symbol) {
        if (roots.empty()) return;
        into.join(roots);
        Cause cause{std::move(what), at, callee};
        for (const auto slot : roots.slots) out_.retained_at.emplace(slot, cause);
        for (const auto slot : roots.inside) out_.retained_at.emplace(slot, cause);
        if (roots.fresh && !fresh_cause_) fresh_cause_ = std::move(cause);
    }
    // Handed to code that may keep it anywhere.
    void leak(const Roots& roots, const std::string& what, const Span& at, SymbolId callee = no_symbol) {
        retain(out_.leaks, graph(roots), what, at, callee);
    }

    // The facts. `may` records the first operation that causes one.
    // The first operation that causes a fact, a direct one in preference to a
    // call: a chain of calls through a cycle then ends at an operation.
    static void may(std::optional<Cause>& fact, const std::string& what, const Span& at, SymbolId callee = no_symbol) {
        if (!fact || (fact->callee != no_symbol && callee == no_symbol)) fact = Cause{what, at, callee};
    }
    void allocate(const IrNode& node, const std::string& what) {
        may(out_.allocates, what, node.span);
        out_.optimizer.allocates = true;
    }
    void trap(const IrNode& node, const std::string& what) {
        may(out_.traps, what, node.span);
        out_.optimizer.traps = true;
    }
    void precise_unknown(const std::string& what, const Span& at, SymbolId callee = no_symbol) {
        out_.allocation_unknown = true;
        may(out_.allocates, what, at, callee);
        may(out_.traps, what, at, callee);
        may(out_.host, what, at, callee);
        may(out_.scheduler, what, at, callee);
        may(out_.class_init, what, at, callee);
    }
    void legacy_unknown() {
        out_.optimizer = {true, true, true, true};
    }
    void unknown_operation(const IrNode& node, const std::string& what) {
        precise_unknown(what, node.span);
        legacy_unknown();
    }
    // What a call of `target` may do, as its summary says; a function with no
    // summary is unknown.
    void merge_facts(SymbolId target, const Span& at) {
        const auto* callee = a_.valid(target) ? a_.summary(target) : nullptr;
        if (!callee) {
            precise_unknown("calls '" + name(target) + "', which has no body or contract", at, target);
            return;
        }
        const auto take = [&](std::optional<Cause>& mine, const std::optional<Cause>& theirs) {
            if (theirs) may(mine, "", at, target);
        };
        take(out_.allocates, callee->allocates);
        out_.allocation_unknown = out_.allocation_unknown || callee->allocation_unknown;
        take(out_.traps, callee->traps);
        take(out_.host, callee->host);
        take(out_.scheduler, callee->scheduler);
        take(out_.class_init, callee->class_init);
        take(out_.hooks, callee->hooks);
    }
    // The optimizer's view of a call: only a function with a body is known.
    void merge_optimizer(SymbolId target) {
        const auto* callee = a_.valid(target) ? a_.summary(target) : nullptr;
        const bool body = callee && a_.symbol(target).kind == SymbolKind::Function && a_.bodies_.contains(target) &&
                          !(a_.opaque_ && a_.opaque_(target));
        if (!body) {
            legacy_unknown();
            return;
        }
        out_.optimizer.allocates = out_.optimizer.allocates || callee->optimizer.allocates;
        out_.optimizer.traps = out_.optimizer.traps || callee->optimizer.traps;
        out_.optimizer.host = out_.optimizer.host || callee->optimizer.host;
        out_.optimizer.scheduler = out_.optimizer.scheduler || callee->optimizer.scheduler;
    }

    // A callee's roots in the caller's terms, for a call whose slot k holds
    // `slots[k]`.
    Roots mapped(const Roots& roots, const std::vector<Roots>& slots) const {
        Roots result;
        result.fresh = roots.fresh;
        result.unknown = roots.unknown;
        result.opaque = roots.opaque;
        for (const auto k : roots.slots) result.join(k < slots.size() ? slots[k] : Roots{});
        for (const auto k : roots.inside) result.join(step(graph(k < slots.size() ? slots[k] : Roots{})));
        return result;
    }
    // What a callee keeps of its arguments, as the caller keeps it.
    void flow(const Summary& callee, SymbolId target, const std::vector<Roots>& slots, const Span& at) {
        retain(out_.escapes, mapped(callee.escapes, slots), "passes it to '" + name(target) + "', which keeps it", at,
               target);
        retain(out_.leaks, mapped(callee.leaks, slots), "passes it to '" + name(target) + "', which hands it on", at,
               target);
        if (callee.escapes.fresh || callee.leaks.fresh) {
            Roots held = callee.fresh_graph;
            held.join(callee.shared_graph);
            shared_graph_.join(mapped(held, slots));
        }
    }
    void leak_arguments(const std::vector<Roots>& slots, const std::string& what, const Span& at,
                        SymbolId callee = no_symbol, std::size_t first = 0) {
        for (std::size_t i = first; i < slots.size(); ++i) leak(slots[i], what, at, callee);
    }

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
                leak_arguments(slots, "passes it to '" + name(target) + "', which has no body or contract", span, target);
                // A function without a body runs nothing, unless it is bound
                // to something outside the program.
                if (a_.symbol(target).external) {
                    impure(Reason{"calls the external function '" + name(target) + "'", span});
                    result.join(unknown());
                }
                continue;
            }
            flow(*summary, target, slots, span);
            // A host binding or other external function that a call reaches
            // returns something no one here knows.
            if (!a_.bodies_.contains(target) && !kernel_bound(a_.symbol(target)) && a_.symbol(target).external)
                result.join(unknown());
            if (summary->reason) impure(Reason{{}, span, target, std::nullopt}, true);
            if (summary->render && !out_.render) out_.render = Reason{{}, span, target, std::nullopt};
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
            if (escapes) {
                link(fresh(), map(summary->fresh_graph));
                shared_graph_.join(map(summary->shared_graph));
            }
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
            impure(Reason{*start, node.span}, true);
        } else if (!item.constant) {
            impure(Reason{"reads the reassignable binding '" + item.name + "'", node.span}, true);
        } else if (!a_.immutable(item.type)) {
            impure(Reason{"reads the binding '" + item.name + "', whose type " + display(item.type) + " is mutable",
                          node.span}, true);
        }
        return typed(node.type, global());
    }

    // The destination's own reason comes first: it is what the check
    // reports for a store whose value is impure too.
    void store(const IrNode& node) {
        if (node.operands.size() != 2) {
            impure(Reason{"stores to an unknown place", node.span});
            leak_arguments(operands(node, 0), "stores it to an unknown place", node.span);
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
            if (local) {
                assign(destination.symbol, value);
            } else {
                retain(out_.escapes, value, "stores it in the binding '" + name(destination.symbol) + "'", node.span);
            }
            return;
        }
        if ((destination.op == IrOp::Field || destination.op == IrOp::Index) && !destination.operands.empty()) {
            const auto object = eval(destination.operands.front());
            const auto text = destination.op == IrOp::Field
                                  ? "assigns the field '" + name(destination.symbol) + "' of an object it did not allocate"
                                  : std::string("assigns an element of a collection it did not allocate");
            write(object, Reason{text, destination.span});
            for (std::size_t i = 1; i < destination.operands.size(); ++i) eval(destination.operands[i]);
            link(object, eval(node.operands.back()), node.span,
                 destination.op == IrOp::Field ? "stores it in the field '" + name(destination.symbol) + "'"
                                               : std::string("stores it in a collection"));
            return;
        }
        impure(Reason{"assigns through a reference", destination.span});
        leak_arguments(operands(node, 0), "assigns it through a reference", destination.span);
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

    // What the operation itself may do, for the facts and for the optimizer's
    // flags. The table follows EffectAnalysis::add, which the optimizer's
    // images depend on; the differential test of summaries_tests.cpp holds the
    // two together. The effects of what a call runs are merged where the call
    // is evaluated.
    void effects(const IrNode& node) {
        const auto local = [&](SymbolId id) { return is_local(a_.program_, id); };
        switch (node.op) {
        case IrOp::Boolean:
        case IrOp::Integer:
        case IrOp::Float:
        case IrOp::Nil:
        case IrOp::Reference:
        case IrOp::Receiver:
        case IrOp::ReinterpretFloat:
        case IrOp::ShortCircuit:
        case IrOp::ComparisonChain:
        case IrOp::ScopedValue:
        case IrOp::SelectValue:
        case IrOp::Block:
        case IrOp::Branch:
        case IrOp::Loop:
        case IrOp::Break:
        case IrOp::Continue:
        case IrOp::Return:
        case IrOp::Discard:
            break;
        case IrOp::Load:
            // A global that has not been initialized faults when read.
            if (!local(node.symbol)) trap(node, "reads a binding that may be uninitialized");
            break;
        case IrOp::Store:
            if (node.operands.size() != 2) {
                unknown_operation(node, "stores to an unknown place");
            } else if (node.operands.front().op != IrOp::Load) {
                trap(node, "stores to a field or an element");
            }
            break;
        case IrOp::Initialize:
            if (!local(node.symbol) && (!a_.valid(node.symbol) || a_.symbol(node.symbol).kind != SymbolKind::Global)) {
                trap(node, "sets a field");
            }
            break;
        case IrOp::Unary:
            if (node.text != "!" && node.text != "~") trap(node, "applies '" + node.text + "'");
            break;
        case IrOp::Binary: {
            const auto operand = node.operands.empty() ? TypeKind::Void : node.operands.front().type.kind;
            const bool scalar = operand == TypeKind::Bool || operand == TypeKind::Integer;
            const bool safe = scalar && (comparison(node.text) || (is_bitwise_binary(node.text) && !is_shift(node.text)));
            if (!safe) trap(node, "applies '" + node.text + "'");
            if (operand == TypeKind::String) allocate(node, "builds a string");
            break;
        }
        case IrOp::Convert:
        case IrOp::Fail:
        case IrOp::TrapMessage:
            trap(node, "converts a number or fails");
            break;
        case IrOp::Field:
        case IrOp::Index:
        case IrOp::Length:
        case IrOp::CollectionVersion:
        case IrOp::TextLength:
        case IrOp::TextByteAt:
        case IrOp::TextEqual:
        case IrOp::Project:
        case IrOp::EnumTag:
        case IrOp::EnumProject:
            trap(node, "reads a field or an element");
            break;
        case IrOp::TextSlice:
        case IrOp::TextFromUtf8:
            allocate(node, "builds a string");
            trap(node, "builds a string");
            break;
        case IrOp::Append:
        case IrOp::RemoveAt:
            allocate(node, "changes a list");
            trap(node, "changes a list");
            break;
        case IrOp::String:
        case IrOp::Allocate:
        case IrOp::Collection:
        case IrOp::RepeatCollection:
        case IrOp::Tuple:
        case IrOp::EnumConstruct:
        case IrOp::MakeClosure:
            allocate(node, "allocates");
            trap(node, "allocates");
            break;
        case IrOp::Call:
            // Every call can reach the frame-depth limit.
            trap(node, "calls a function, which may reach the frame-depth limit");
            if (a_.valid(node.symbol) && is_static_initializer(a_.symbol(node.symbol))) {
                const auto& guard = a_.symbol(node.symbol);
                const auto cls = a_.valid(guard.owner) ? a_.symbol(guard.owner).owner : no_symbol;
                may(out_.class_init, "runs the static initialization of '" + name(cls) + "'", node.span);
            }
            break;
        case IrOp::VirtualCall:
            if (const auto fixed = a_.modules_.target(node); fixed != no_symbol) {
                trap(node, "calls a function, which may reach the frame-depth limit");
                merge_optimizer(fixed);
            } else {
                legacy_unknown();
            }
            break;
        case IrOp::CallClosure:
            unknown_operation(node, "calls a function value");
            break;
        case IrOp::ExternalCall:
            if (a_.valid(node.symbol) && kernel_bound(a_.symbol(node.symbol))) {
                out_.optimizer.allocates = out_.optimizer.traps = true;
            } else {
                legacy_unknown();
                may(out_.host, "makes a host request", node.span);
            }
            break;
        case IrOp::TaskScope:
            may(out_.scheduler, "scopes tasks", node.span);
            out_.optimizer.scheduler = true;
            trap(node, "scopes tasks");
            break;
        case IrOp::Spawn:
        case IrOp::SchedulerCall:
            unknown_operation(node, node.op == IrOp::Spawn ? "starts a task" : "uses the scheduler");
            break;
        case IrOp::ReferencePlace:
            unknown_operation(node, "lets a local escape as a reference");
            break;
        default:
            // Destroy and BaseInitialize run class code; class and value
            // members may dispatch.
            unknown_operation(node, "runs code the analysis does not follow");
            break;
        }
    }

public:
    Roots eval(const IrNode& node) {
        effects(node);
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
                retain(out_.escapes, value, "stores it in the binding '" + name(node.symbol) + "'", node.span);
            } else {
                // A field initializer sets a field of the object being built.
                Roots object;
                object.slots.insert(0);
                write(object, Reason{"assigns the field '" + name(node.symbol) + "' of an object it did not allocate",
                                     node.span});
                link(object, value, node.span, "stores it in the field '" + name(node.symbol) + "'");
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
            if (node.op == IrOp::Append) link(list, value, node.span, "appends it to a list");
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
            const auto constructors = a_.construction(cls, node.symbol);
            apply(constructors, slots, node.span);
            // The constructors run too, which the optimizer has never counted.
            for (const auto target : constructors) {
                if (a_.valid(target) && (a_.summary(target) || a_.symbol(target).kind == SymbolKind::Function)) {
                    merge_facts(target, node.span);
                }
            }
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
            const auto result = typed(node.type, apply({node.symbol}, slots, node.span));
            merge_facts(node.symbol, node.span);
            merge_optimizer(node.symbol);
            return result;
        }
        case IrOp::VirtualCall: {
            const auto slots = operands(node, 0);
            if (a_.function_value_call(node)) {
                precise_unknown("calls a function value", node.span);
                leak_arguments(slots, "passes it to a function value", node.span, no_symbol, 1);
                impure(Reason{"calls a function value", node.span});
                return typed(node.type, unknown());
            }
            if (!node.operands.empty() && node.operands.front().type.kind == TypeKind::TypeParameter) {
                // Known only in a specialization, where it is checked.
                precise_unknown("calls through a type parameter", node.span);
                leak_arguments(slots, "passes it through a type parameter", node.span);
                Roots result = fresh();
                for (const auto& value : slots) result.join(graph(value));
                return typed(node.type, result);
            }
            const auto targets = a_.targets(node);
            if (targets.empty()) {
                precise_unknown("calls a method with no known target", node.span);
                leak_arguments(slots, "passes it to a call with no known target", node.span);
            } else {
                may(out_.traps, "calls a method, which may reach the frame-depth limit", node.span);
            }
            const auto result = typed(node.type, apply(targets, slots, node.span));
            for (const auto target : targets) merge_facts(target, node.span);
            return result;
        }
        case IrOp::ExternalCall:
            if (a_.valid(node.symbol) && kernel_bound(a_.symbol(node.symbol))) {
                const auto result = kernel_call(node);
                merge_facts(node.symbol, node.span);
                return result;
            }
            {
                const auto slots = operands(node, 1);
                // The binding's contract, or what an absent one leaves unknown.
                if (const auto* contract = a_.valid(node.symbol) ? a_.summary(node.symbol) : nullptr) {
                    flow(*contract, node.symbol, slots, node.span);
                    merge_facts(node.symbol, node.span);
                } else {
                    precise_unknown("makes a host request that has no contract", node.span);
                    leak_arguments(slots, "passes it to a host request that has no contract", node.span);
                }
            }
            may(out_.host, "makes a host request", node.span);
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
        case IrOp::TaskScope: {
            // A task keeps the function value it starts.
            leak_arguments(operands(node, 0), "starts a task with it", node.span);
            for (const auto& child : node.body) eval(child);
            for (const auto& child : node.alternative) eval(child);
            impure(Reason{"starts or scopes tasks", node.span});
            return typed(node.type, unknown());
        }
        case IrOp::SchedulerCall: {
            Roots value;
            for (const auto& child : node.operands) value.join(eval(child));
            if (node.text == "transfer.check") return typed(node.type, value);
            leak(value, "hands it to the scheduler operation '" + node.text + "'", node.span);
            impure(Reason{"uses the scheduler operation '" + node.text + "'", node.span});
            return typed(node.type, unknown());
        }
        case IrOp::CallClosure:
            leak_arguments(operands(node, 0), "passes it to a function value", node.span, no_symbol, 1);
            for (const auto& child : node.body) eval(child);
            for (const auto& child : node.alternative) eval(child);
            impure(Reason{"calls a function value", node.span});
            return typed(node.type, unknown());
        case IrOp::ClassHash:
        case IrOp::ClassText:
            children(node);
            impure(Reason{"uses an object's identity", node.span}, true);
            return {};
        case IrOp::Destroy:
            // A destructor may keep its receiver.
            leak_arguments(operands(node, 0), "runs a destructor on it", node.span);
            for (const auto& child : node.body) eval(child);
            for (const auto& child : node.alternative) eval(child);
            impure(Reason{"destroys an object", node.span});
            return {};
        case IrOp::ReferencePlace:
            // The local escapes as a mutable reference.
            children(node);
            if (a_.valid(node.symbol)) {
                leak(locals_[node.symbol], "takes a reference to the local that holds it", node.span);
                assign(node.symbol, unknown());
            }
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
            for (const auto& child : node.operands) {
                const auto value = eval(child);
                retain(out_.result, value, "returns it", node.span);
                if (!value.empty() && !result_cause_) result_cause_ = Cause{"returns it", node.span, no_symbol};
            }
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
    : program_(program), opaque_(std::move(opaque)), dispatch_(program), modules_(program) {
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
        // Whatever a prebuilt module's tag says, its effects are not recorded.
        const Cause why{"is declared in a prebuilt module", symbol(id).span, no_symbol};
        summary.allocation_unknown = true;
        for (auto* fact : {&summary.allocates, &summary.traps, &summary.host, &summary.scheduler, &summary.class_init})
            *fact = why;
        summary.optimizer = {true, true, true, true};
        for (std::size_t k = 1; k <= symbol(id).parameters.size(); ++k) {
            summary.leaks.slots.insert(k);
            summary.leaks.inside.insert(k);
        }
        summaries_.emplace(id, std::move(summary));
    }
    // A function bound to a kernel writes the lists its writable-view and
    // state parameters receive; one bound to a host service is impure.
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind != SymbolKind::Function || item.has_body || !item.external || summaries_.contains(id)) continue;
        Summary summary;
        const auto all_slots = [&](Roots& roots) {
            for (std::size_t k = 1; k <= item.parameters.size(); ++k) {
                roots.slots.insert(k);
                roots.inside.insert(k);
            }
        };
        if (kernel_bound(item)) {
            // Nothing in a call but its own work: it reads its arguments,
            // allocates its result and may trap, and it calls nothing back
            // (the rule EffectAnalysis has always applied to a kernel).
            summary.allocates = Cause{"is bound to a kernel, which allocates its result", item.span, no_symbol};
            summary.traps = Cause{"is bound to a kernel, which may trap", item.span, no_symbol};
            summary.optimizer = {true, true, true, true};
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
            const Cause request{"makes the host request " + item.host_binding->name, item.span, no_symbol};
            summary.reason = Reason{request.operation, item.span};
            summary.host = request;
            summary.optimizer = {true, true, true, true};
            if (const auto contract = host_effects(item)) {
                if (contract->allocates) summary.allocates = request;
                if (contract->traps) summary.traps = request;
                if (contract->scheduler) summary.scheduler = request;
                if (contract->retains) all_slots(summary.escapes);
            } else {
                summary.allocation_unknown = true;
                for (auto* fact : {&summary.allocates, &summary.traps, &summary.scheduler, &summary.class_init})
                    *fact = Cause{"has no #HostEffects contract", item.span, no_symbol};
                all_slots(summary.leaks);
            }
        } else {
            summary.reason = Reason{"is an external function", item.span};
            summary.allocation_unknown = true;
            for (auto* fact : {&summary.allocates, &summary.traps, &summary.host, &summary.scheduler, &summary.class_init})
                *fact = Cause{"is an external function with no contract", item.span, no_symbol};
            summary.optimizer = {true, true, true, true};
            all_slots(summary.leaks);
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
        if (is_static_initializer(symbol(owner)) && !summary.class_init) {
            const auto cls = valid(symbol(owner).owner) ? symbol(symbol(owner).owner).owner : no_symbol;
            summary.class_init =
                Cause{"is the static initializer of '" + (valid(cls) ? symbol(cls).name : std::string("?")) + "'",
                      symbol(owner).span, no_symbol};
        }
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
    for (auto& [id, summary] : summaries_) finish(id, summary);
}

void PurityAnalysis::finish(SymbolId function, Summary& summary) const {
    const auto& item = symbol(function);
    const bool function_kind = item.kind == SymbolKind::Function;
    const std::size_t slots = function_kind ? item.parameters.size() + 1 : 0;
    const auto scalar = [](const IrType& type) {
        return type.kind == TypeKind::Bool || type.kind == TypeKind::Integer || type.kind == TypeKind::Float ||
               type.kind == TypeKind::Void;
    };
    Roots held = summary.fresh_graph;
    held.join(summary.shared_graph);
    const auto within = [&](const Roots& roots, std::size_t k) {
        const auto has = [&](const Roots& from) { return from.slots.contains(k) || from.inside.contains(k); };
        return has(roots) || (roots.fresh && has(held));
    };
    summary.retention.assign(slots, Retention::NotRetained);
    for (std::size_t k = 0; k < slots; ++k) {
        auto& kind = summary.retention[k];
        if (k == 0 && function_kind && valid(item.owner) && symbol(item.owner).kind == SymbolKind::Class &&
            immutable(IrType{TypeKind::Object, symbol(item.owner).name, item.owner, {}})) {
            // The receiver of an #Immutable class is not tracked either.
            kind = Retention::Unknown;
            summary.retained_at.emplace(0, Cause{"is the receiver of an immutable class, which is not tracked",
                                                 item.span, no_symbol});
            continue;
        }
        if (k > 0) {
            const auto& type = symbol(item.parameters[k - 1]).type;
            if (scalar(type)) continue;
            // The tracking leaves out immutable values, which may still be
            // kept: they are not described.
            if (immutable(type)) {
                kind = Retention::Unknown;
                summary.retained_at.emplace(k, Cause{"is a value of an immutable reference type, which is not tracked",
                                                     symbol(item.parameters[k - 1]).span, no_symbol});
                continue;
            }
        }
        bool elsewhere = within(summary.escapes, k);
        for (const auto& [slot, linked] : summary.links) elsewhere = elsewhere || within(linked, k);
        if (within(summary.leaks, k)) {
            kind = Retention::Unknown;
        } else if (elsewhere) {
            kind = Retention::Elsewhere;
        } else if (within(summary.result, k)) {
            kind = Retention::ByResult;
        }
    }
    // The result.
    const bool bodyless = !bodies_.contains(function);
    if (!function_kind || (bodyless && !kernel_bound(item)) || (opaque_ && opaque_(function))) {
        summary.ownership = Ownership::Unknown;
        summary.ownership_cause = Cause{"has no body to read the result from", item.span, no_symbol};
        return;
    }
    if (scalar(item.type)) {
        summary.ownership = Ownership::Fresh;
        return;
    }
    if (immutable(item.type)) {
        summary.ownership = Ownership::Unknown;
        summary.ownership_cause = Cause{"returns an immutable reference type, which is not tracked", item.span, no_symbol};
        return;
    }
    const auto& result = summary.result;
    bool stored = summary.escapes.fresh || summary.leaks.fresh;
    for (const auto& [slot, linked] : summary.links) stored = stored || linked.fresh;
    std::set<std::size_t> parameters = result.slots;
    parameters.insert(result.inside.begin(), result.inside.end());
    const bool global = result.unknown && !result.opaque;
    const int sources = (result.fresh ? 1 : 0) + static_cast<int>(parameters.size()) + (global ? 1 : 0);
    if (result.opaque || sources > 1 || (result.fresh && stored)) {
        summary.ownership = Ownership::Unknown;
    } else if (parameters.size() == 1) {
        summary.ownership = Ownership::Borrowed;
        summary.borrowed_slot = *parameters.begin();
    } else if (global) {
        summary.ownership = Ownership::Static;
    } else {
        summary.ownership = Ownership::Fresh;
    }
}

std::optional<PurityAnalysis::Cause> PurityAnalysis::origin(SymbolId function, Fact fact) const {
    std::set<SymbolId> seen;
    const auto* current = summary(function);
    const Cause* last = nullptr;
    while (current && seen.insert(function).second) {
        const std::optional<Cause>* found = nullptr;
        switch (fact) {
        case Fact::Allocation: found = &current->allocates; break;
        case Fact::Traps: found = &current->traps; break;
        case Fact::Host: found = &current->host; break;
        case Fact::Scheduler: found = &current->scheduler; break;
        case Fact::ClassInit: found = &current->class_init; break;
        case Fact::Hooks: found = &current->hooks; break;
        }
        if (!found || !*found) break;
        last = &**found;
        if (last->callee == no_symbol) break;
        function = last->callee;
        current = summary(function);
    }
    return last ? std::optional<Cause>(*last) : std::nullopt;
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
    return chain(function, false, {});
}

std::optional<PurityAnalysis::Impure> PurityAnalysis::render_effect(SymbolId function,
                                                                    const std::set<std::size_t>& ignored) const {
    return chain(function, true, ignored);
}

std::optional<PurityAnalysis::Impure> PurityAnalysis::chain(SymbolId function, bool rendering,
                                                            const std::set<std::size_t>& ignored) const {
    const auto first = [rendering](const Summary& summary, const std::set<std::size_t>& skip) -> const Reason* {
        const auto& own = rendering ? summary.render : summary.reason;
        if (own) return &*own;
        for (const auto& [slot, why] : summary.writes) {
            if (!skip.contains(slot)) return &why;
        }
        return nullptr;
    };
    const auto* start = summary(function);
    if (!start) {
        if (valid(function)) return Impure{{}, "has no body to check", symbol(function).span};
        return Impure{{}, "is not a function", {}};
    }
    const auto* reason = first(*start, ignored);
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
            next = first(*next_summary, {});
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

void check_render_factories(const IrProgram& program, const std::function<bool(SymbolId)>& opaque,
                            bool specialized) {
    const auto valid = [&](SymbolId id) { return id < program.symbols.size(); };
    const auto factory = [&](const IrSymbol& item) {
        return item.kind == SymbolKind::Function && find_attribute(item, AttributeId::UiFactory) != nullptr;
    };
    if (std::ranges::none_of(program.symbols, factory)) return;
    const auto in_template = [&](SymbolId id) {
        std::set<SymbolId> seen;
        for (auto scope = id; valid(scope) && seen.insert(scope).second; scope = program.symbols[scope].owner) {
            if (!program.symbols[scope].generics.empty()) return true;
        }
        return false;
    };
    const PurityAnalysis analysis(program, specialized ? std::function<bool(SymbolId)>{} : opaque);
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (!factory(item) || !item.has_body) continue;
        if (!specialized && opaque && opaque(id)) continue;
        if (specialized && in_template(id)) continue;
        // A function value cannot be written: its captures cannot be reassigned, and what they refer to is
        // reached only by calling it, which is an effect of its own. A write the analysis attributes to such a
        // parameter comes from tracking the objects the factory allocated together, and is not one.
        std::set<std::size_t> ignored;
        // After specialization a function type is the interface of its signature.
        const auto function_type = [&](const IrType& type) {
            return type.kind == TypeKind::Callable ||
                   (valid(type.symbol) && program.symbols[type.symbol].name.starts_with("$signature"));
        };
        for (std::size_t i = 0; i < item.parameters.size(); ++i) {
            const auto parameter = item.parameters[i];
            if (valid(parameter) && function_type(program.symbols[parameter].type)) ignored.insert(i + 1);
        }
        const auto effect = analysis.render_effect(id, ignored);
        if (!effect) continue;
        std::vector<std::pair<SymbolId, Span>> hops;
        for (const auto& [callee, span] : effect->calls) {
            const auto& called = program.symbols[callee];
            if (called.kind != SymbolKind::Function || called.name == "$invoke") continue;
            hops.emplace_back(callee, span);
        }
        // The expression in the template that leads to the effect.
        const auto at = hops.empty() ? effect->span : hops.front().second;
        auto text = std::string("effect not allowed while rendering: ");
        text += hops.empty() ? "the template " + effect->operation
                             : "it reaches '" + program.symbols[hops.back().first].name + "', which " +
                                   effect->operation;
        auto error = Diagnostic(at, std::move(text)).with_code("E1010");
        for (std::size_t i = 0; i < hops.size(); ++i) {
            const auto& [callee, span] = hops[i];
            if (i == 0) error.label("calls '" + program.symbols[callee].name + "' while the view is built");
            else error.label(span, "calls '" + program.symbols[callee].name + "'");
        }
        if (!hops.empty()) error.label(effect->span, effect->operation + " here");
        error.note("a template may allocate and read, but it may not write what it did not create, assign a "
                   "binding, make a host request, start or wait for tasks, or call a function value");
        error.help("do it in a handler or a command, and pass the result to the factory");
        throw error;
    }
}

} // namespace neura

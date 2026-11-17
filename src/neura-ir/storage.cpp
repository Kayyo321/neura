#include "storage.hpp"

#include "reference_storage.hpp"
#include "scheduler_ops.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace neura {

namespace {

constexpr std::size_t no_slot = static_cast<std::size_t>(-1);

// True for a declaration inside a generic class or function, including the
// generic declaration itself. Specialization keeps these, unreachable, and
// gives each instance its own symbols with no generics.
bool in_template(const IrProgram& program, SymbolId id) {
    for (auto scope = id; scope != no_symbol && scope < program.symbols.size(); scope = program.symbols[scope].owner) {
        const auto& item = program.symbols[scope];
        if (!item.generics.empty() || item.kind == SymbolKind::TypeParameter) return true;
        if (is_static_scope(item)) return false;
    }
    return false;
}

bool valid(const IrProgram& program, SymbolId id) { return id != no_symbol && id < program.symbols.size(); }

// Closures and primitive references are classes that lowering synthesizes:
// their constructors store captures and values into fields, which the
// operation that builds them already reports.
bool synthetic_class(const IrProgram& program, SymbolId id) {
    return valid(program, id) && program.symbols[id].kind == SymbolKind::Class &&
           (program.symbols[id].name.starts_with("$closure") || program.symbols[id].name == "$reference");
}

bool synthetic_body(const IrProgram& program, SymbolId owner) {
    if (!valid(program, owner)) return true;
    const auto& item = program.symbols[owner];
    return item.kind == SymbolKind::Function && synthetic_class(program, item.owner);
}

bool closure_allocation(const IrProgram& program, const IrNode& node) {
    if (node.op != IrOp::Allocate) return false;
    const auto cls = valid(program, node.symbol) ? program.symbols[node.symbol].owner : node.type.symbol;
    return valid(program, cls) && program.symbols[cls].kind == SymbolKind::Class &&
           program.symbols[cls].name.starts_with("$closure");
}

bool static_field(const IrProgram& program, SymbolId id) {
    if (!valid(program, id)) return false;
    const auto& item = program.symbols[id];
    return item.kind == SymbolKind::Field && valid(program, item.owner) && is_static_scope(program.symbols[item.owner]);
}

// Whether a type is, or contains, an array. A tuple holds its elements and an
// enum its payloads; a class, list or function value holds a reference.
class TypeFacts {
public:
    explicit TypeFacts(const IrProgram& program) : program_(program) {
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].kind == SymbolKind::Variant) variants_[program.symbols[id].owner].push_back(id);
        }
    }

    bool holds_array(const IrType& type) const {
        switch (type.kind) {
        case TypeKind::Array: return true;
        case TypeKind::Tuple:
            return std::ranges::any_of(type.arguments, [&](const IrType& item) { return holds_array(item); });
        case TypeKind::Enum: {
            // An unspecialized instance names its arguments, not its payloads.
            if (std::ranges::any_of(type.arguments, [&](const IrType& item) { return holds_array(item); })) return true;
            return enum_holds_array(type.symbol);
        }
        default: return false;
        }
    }

    // What the analysis follows: an array, or a primitive reference.
    bool tracked(const IrType& type) const { return holds_array(type) || is_primitive_reference(type); }

private:
    bool enum_holds_array(SymbolId id) const {
        if (!valid(program_, id)) return false;
        const auto found = memo_.find(id);
        if (found != memo_.end()) return found->second;
        // A recursive enum is under way here: it adds nothing of its own.
        memo_[id] = false;
        bool result = false;
        const auto variants = variants_.find(id);
        if (variants != variants_.end()) {
            for (const auto variant : variants->second) {
                for (const auto& payload : program_.symbols[variant].payload_types) {
                    result = result || holds_array(payload);
                }
            }
        }
        memo_[id] = result;
        return result;
    }

    const IrProgram& program_;
    std::map<SymbolId, std::vector<SymbolId>> variants_;
    mutable std::map<SymbolId, bool> memo_;
};

// -- Origins -----------------------------------------------------------------

struct Origin {
    enum class Kind : std::uint8_t {
        Owned,     // an array that `scope` owns
        Parameter, // reached through the parameter `symbol`, which is `slot`
        Global,
        Field,
        Static, // storage a callee's summary knows only to be static
        Local,  // a primitive reference to frame storage `symbol`
        Unknown,
    };
    Kind kind = Kind::Unknown;
    SymbolId symbol = no_symbol;
    std::size_t slot = no_slot;
    StorageOwner scope;
    Span span;

    bool operator<(const Origin& other) const {
        const auto left = std::tie(kind, symbol, slot, span.line, span.column, span.offset);
        const auto right = std::tie(other.kind, other.symbol, other.slot, other.span.line, other.span.column,
                                    other.span.offset);
        if (left != right) return left < right;
        return scope < other.scope;
    }
    bool operator==(const Origin& other) const { return !(*this < other) && !(other < *this); }
};

using OriginSet = std::set<Origin>;

struct ReturnFact {
    Span span;
    OriginSet origins;
};

struct BodyFacts {
    std::vector<ArrayCreation> creations;
    std::vector<StorageEscape> escapes;
    std::vector<ReturnFact> returns;
};

bool opens_scope(IrOp op, Region region) {
    switch (op) {
    case IrOp::Block: return region == Region::Body;
    case IrOp::Branch:
    case IrOp::Loop:
    case IrOp::TaskScope: return region == Region::Body || region == Region::Alternative;
    default: return false;
    }
}

class Analyzer {
public:
    Analyzer(const IrProgram& program, const PurityAnalysis& purity, const TypeFacts& facts, const IrBody& body)
        : program_(program), purity_(purity), facts_(facts), body_(body) {
        root_.function = body.owner;
        root_.arm = Region::Function;
    }

    BodyFacts run() {
        traverse_all(Phase::Declare);
        for (int round = 0; round < 12; ++round) {
            const auto before = table_;
            traverse_all(Phase::Flow);
            if (before == table_) break;
        }
        traverse_all(Phase::Emit);
        return std::move(out_);
    }

private:
    enum class Phase { Declare, Flow, Emit };

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool known(SymbolId id) const { return valid(program_, id); }

    void traverse_all(Phase phase) {
        std::vector<OwnerStep> path;
        for (std::size_t i = 0; i < body_.instructions.size(); ++i) {
            path.push_back({Region::Function, i});
            traverse(body_.instructions[i], path, root_, phase);
            path.pop_back();
        }
    }

    void traverse(const IrNode& node, std::vector<OwnerStep>& path, const StorageOwner& scope, Phase phase) {
        visit(node, scope, phase);
        const std::pair<Region, const std::vector<IrNode>*> regions[] = {
            {Region::Operands, &node.operands}, {Region::Body, &node.body}, {Region::Alternative, &node.alternative}};
        for (const auto& [region, children] : regions) {
            StorageOwner inner;
            const bool opens = opens_scope(node.op, region);
            if (opens) {
                inner.function = body_.owner;
                inner.path = path;
                inner.arm = region;
                inner.iteration = node.op == IrOp::Loop;
            }
            for (std::size_t i = 0; i < children->size(); ++i) {
                path.push_back({region, i});
                traverse((*children)[i], path, opens ? inner : scope, phase);
                path.pop_back();
            }
        }
    }

    // -- The operand of a call that a slot names ------------------------------

    const IrNode* slot_operand(const IrNode& call, std::size_t slot) const {
        std::size_t index = slot;
        if (call.op != IrOp::VirtualCall) {
            if (slot == 0) return nullptr;
            index = slot - 1;
        }
        return index < call.operands.size() ? &call.operands[index] : nullptr;
    }

    std::size_t parameter_slot(SymbolId parameter) const {
        const auto owner = symbol(parameter).owner;
        if (!known(owner)) return no_slot;
        const auto& list = symbol(owner).parameters;
        const auto found = std::ranges::find(list, parameter);
        return found == list.end() ? no_slot : static_cast<std::size_t>(found - list.begin()) + 1;
    }

    // -- Origins ----------------------------------------------------------------

    static Origin make(Origin::Kind kind, SymbolId id = no_symbol, std::size_t slot = no_slot) {
        Origin origin;
        origin.kind = kind;
        origin.symbol = id;
        origin.slot = slot;
        return origin;
    }

    static OriginSet unknown() { return {make(Origin::Kind::Unknown)}; }

    // The slot of the object that a field operand reads, when it is a slot.
    std::size_t owner_slot(const IrNode& field) const {
        if (field.operands.empty()) return no_slot;
        const auto& receiver = field.operands.front();
        if (receiver.op == IrOp::Receiver) return 0;
        if (receiver.op == IrOp::Load && known(receiver.symbol) && symbol(receiver.symbol).kind == SymbolKind::Parameter) {
            return parameter_slot(receiver.symbol);
        }
        return no_slot;
    }

    Origin field_origin(const IrNode& field) const {
        if (static_field(program_, field.symbol)) return make(Origin::Kind::Global, field.symbol);
        return make(Origin::Kind::Field, field.symbol, owner_slot(field));
    }

    OriginSet origins(const IrNode& node, const StorageOwner& scope) const {
        OriginSet out;
        switch (node.op) {
        case IrOp::Collection:
        case IrOp::RepeatCollection:
            if (node.type.kind == TypeKind::Array) {
                auto origin = make(Origin::Kind::Owned);
                origin.scope = scope;
                origin.span = node.span;
                out.insert(std::move(origin));
            }
            break;
        case IrOp::Load:
            if (!known(node.symbol)) break;
            if (symbol(node.symbol).kind == SymbolKind::Parameter) {
                if (facts_.tracked(node.type)) out.insert(make(Origin::Kind::Parameter, node.symbol, parameter_slot(node.symbol)));
                const auto found = table_.find(node.symbol);
                if (found != table_.end()) out.insert(found->second.begin(), found->second.end());
            } else if (symbol(node.symbol).kind == SymbolKind::Local) {
                const auto found = table_.find(node.symbol);
                if (found != table_.end()) out = found->second;
            } else if (symbol(node.symbol).kind == SymbolKind::Global && facts_.tracked(node.type)) {
                out.insert(make(Origin::Kind::Global, node.symbol));
            }
            break;
        case IrOp::Field:
            if (facts_.tracked(node.type)) out.insert(field_origin(node));
            break;
        case IrOp::Index:
            // An element of an array lives and dies with the array.
            if (!node.operands.empty() && node.operands.front().type.kind == TypeKind::Array) {
                out = origins(node.operands.front(), scope);
            }
            break;
        case IrOp::Project:
        case IrOp::EnumProject:
        case IrOp::Convert:
        case IrOp::ClassCast:
        case IrOp::AnyPack:
        case IrOp::AnyUnpack:
            if (!node.operands.empty()) out = origins(node.operands.front(), scope);
            break;
        case IrOp::Tuple:
        case IrOp::EnumConstruct:
            for (const auto& child : node.operands) {
                const auto part = origins(child, scope);
                out.insert(part.begin(), part.end());
            }
            break;
        case IrOp::ScopedValue:
            if (!node.body.empty()) out = origins(node.body.back(), scope);
            break;
        case IrOp::SelectValue:
            for (const auto* arm : {&node.body, &node.alternative}) {
                if (arm->empty()) continue;
                const auto part = origins(arm->back(), scope);
                out.insert(part.begin(), part.end());
            }
            break;
        case IrOp::ReferencePlace: {
            if (!known(node.symbol)) {
                out = unknown();
                break;
            }
            const auto kind = symbol(node.symbol).kind;
            if (kind == SymbolKind::Local || kind == SymbolKind::Parameter) {
                out.insert(make(Origin::Kind::Local, node.symbol, kind == SymbolKind::Parameter ? parameter_slot(node.symbol) : no_slot));
            } else if (kind == SymbolKind::Global || static_field(program_, node.symbol)) {
                out.insert(make(Origin::Kind::Global, node.symbol));
            } else if (kind == SymbolKind::Field && !node.operands.empty() && node.operands.front().op == IrOp::Field) {
                out.insert(field_origin(node.operands.front()));
            } else {
                out = unknown();
            }
            break;
        }
        case IrOp::Call:
        case IrOp::VirtualCall:
        case IrOp::ExternalCall:
            out = call_origins(node, scope);
            break;
        case IrOp::CallClosure: out = unknown(); break;
        default: break;
        }
        if (!facts_.tracked(node.type)) return {};
        return out;
    }

    OriginSet call_origins(const IrNode& call, const StorageOwner& scope) const {
        if (!facts_.tracked(call.type)) return {};
        std::vector<SymbolId> targets;
        if (call.op == IrOp::VirtualCall) {
            targets = purity_.targets(call);
        } else {
            targets.push_back(call.symbol);
        }
        if (targets.empty()) return unknown();
        OriginSet out;
        for (const auto target : targets) {
            const auto* summary = known(target) ? purity_.summary(target) : nullptr;
            if (!summary) return unknown();
            switch (summary->ownership) {
            case PurityAnalysis::Ownership::Fresh: {
                auto origin = make(Origin::Kind::Owned);
                origin.scope = scope;
                origin.span = call.span;
                out.insert(std::move(origin));
                break;
            }
            case PurityAnalysis::Ownership::Borrowed: {
                const auto* argument = slot_operand(call, summary->borrowed_slot);
                if (!argument) return unknown();
                const auto part = origins(*argument, scope);
                out.insert(part.begin(), part.end());
                break;
            }
            case PurityAnalysis::Ownership::Static: out.insert(make(Origin::Kind::Static)); break;
            case PurityAnalysis::Ownership::Unknown: out.insert(make(Origin::Kind::Unknown)); break;
            }
        }
        return out;
    }

    // A binding keeps what it is given. An array created in a scope inside the
    // binding's own is treated as moved out to the binding's scope, so one
    // escape is reported once, where it is assigned.
    OriginSet moved(SymbolId binding, OriginSet from) const {
        const auto declared = declared_.find(binding);
        const StorageOwner& home = declared == declared_.end() ? root_ : declared->second;
        OriginSet out;
        for (auto origin : from) {
            if (origin.kind == Origin::Kind::Owned && home.encloses(origin.scope)) origin.scope = home;
            out.insert(std::move(origin));
        }
        return out;
    }

    void bind(SymbolId binding, const IrNode& value, const StorageOwner& scope) {
        const auto part = moved(binding, origins(value, scope));
        auto& entry = table_[binding];
        entry.insert(part.begin(), part.end());
    }

    // -- Visiting a node ----------------------------------------------------------

    bool local_or_parameter(SymbolId id) const {
        return known(id) && (symbol(id).kind == SymbolKind::Local || symbol(id).kind == SymbolKind::Parameter);
    }

    void visit(const IrNode& node, const StorageOwner& scope, Phase phase) {
        switch (phase) {
        case Phase::Declare:
            if (node.op == IrOp::Initialize && local_or_parameter(node.symbol)) declared_.emplace(node.symbol, scope);
            break;
        case Phase::Flow:
            if (node.op == IrOp::Initialize && local_or_parameter(node.symbol) && !node.operands.empty()) {
                bind(node.symbol, node.operands.front(), scope);
            } else if (node.op == IrOp::Store && node.operands.size() == 2 && node.operands[0].op == IrOp::Load &&
                       local_or_parameter(node.operands[0].symbol) && !is_primitive_reference(node.operands[0].type)) {
                bind(node.operands[0].symbol, node.operands[1], scope);
            }
            break;
        case Phase::Emit: emit(node, scope); break;
        }
    }

    // The declaration that names a value: the local or parameter that a load
    // or reference reads.
    SymbolId named(const IrNode& value) const {
        const IrNode* at = &value;
        while ((at->op == IrOp::Convert || at->op == IrOp::ClassCast) && !at->operands.empty()) at = &at->operands.front();
        if ((at->op == IrOp::Load || at->op == IrOp::ReferencePlace) && local_or_parameter(at->symbol)) {
            // A temporary that lowering introduces names no declaration.
            const auto& name = symbol(at->symbol).name;
            if (name.empty() || name.front() == '$' || name.front() == '#') return no_symbol;
            return at->symbol;
        }
        return no_symbol;
    }

    void report(EscapeKind kind, const IrNode& at, const IrNode& value, const Origin& origin, SymbolId target = no_symbol) {
        StorageEscape escape;
        escape.kind = kind;
        escape.function = body_.owner;
        escape.operation = at.span;
        const auto binding = named(value);
        if (binding != no_symbol) {
            escape.symbol = binding;
            escape.storage = symbol(binding).span;
        } else if (known(origin.symbol)) {
            escape.symbol = origin.symbol;
            escape.storage = symbol(origin.symbol).span;
        } else {
            escape.storage = origin.span;
        }
        escape.target = target;
        out_.escapes.push_back(std::move(escape));
    }

    // An array stored where the region rules do not allow one, or a reference
    // to frame storage where it would outlive the frame.
    void arrays_into(EscapeKind kind, const IrNode& at, const IrNode& value, const StorageOwner& scope,
                     SymbolId target = no_symbol) {
        for (const auto& origin : origins(value, scope)) {
            if (origin.kind == Origin::Kind::Unknown) continue;
            if (is_primitive_reference(value.type)) continue;
            report(kind, at, value, origin, target);
        }
    }

    void references_into(EscapeKind kind, const IrNode& at, const IrNode& value, const StorageOwner& scope,
                         SymbolId target = no_symbol) {
        if (!is_primitive_reference(value.type)) return;
        for (const auto& origin : origins(value, scope)) {
            if (origin.kind == Origin::Kind::Local) report(kind, at, value, origin, target);
        }
    }

    // A static initializer creates the storage of the class it belongs to, as
    // a global's initializer does: neither is a store into storage that exists.
    bool initializer() const { return known(body_.owner) && is_static_initializer(symbol(body_.owner)); }

    void stored(const IrNode& at, const IrNode& destination, const IrNode& value, const StorageOwner& scope) {
        if (!facts_.tracked(value.type)) return;
        if (initializer() && (static_field(program_, destination.symbol) ||
                              (destination.op == IrOp::Load && known(destination.symbol) &&
                               symbol(destination.symbol).kind == SymbolKind::Global))) {
            return;
        }
        if (destination.op == IrOp::Field) {
            const auto kind = static_field(program_, destination.symbol) ? EscapeKind::ArrayInGlobal : EscapeKind::ArrayInField;
            arrays_into(kind, at, value, scope, destination.symbol);
            references_into(EscapeKind::ReferenceInField, at, value, scope, destination.symbol);
        } else if (destination.op == IrOp::Load && known(destination.symbol) &&
                   symbol(destination.symbol).kind == SymbolKind::Global) {
            arrays_into(EscapeKind::ArrayInGlobal, at, value, scope, destination.symbol);
            references_into(EscapeKind::ReferenceInField, at, value, scope, destination.symbol);
        } else if (destination.op == IrOp::Load && local_or_parameter(destination.symbol)) {
            if (is_primitive_reference(destination.type)) return;
            const auto declared = declared_.find(destination.symbol);
            const StorageOwner& home = declared == declared_.end() ? root_ : declared->second;
            for (const auto& origin : origins(value, scope)) {
                if (origin.kind == Origin::Kind::Owned && home.encloses(origin.scope)) {
                    report(EscapeKind::ArrayAssignedToOuterBinding, at, value, origin, destination.symbol);
                }
            }
        } else if (destination.op == IrOp::Index && !destination.operands.empty() &&
                   destination.operands.front().type.kind == TypeKind::List) {
            arrays_into(EscapeKind::ArrayInListElement, at, value, scope);
        }
    }

    void captured(const IrNode& closure, bool task, const StorageOwner& scope) {
        for (const auto& value : closure.operands) {
            arrays_into(task ? EscapeKind::ArrayInTaskArgument : EscapeKind::ArrayCapturedByClosure, closure, value, scope);
            references_into(EscapeKind::ReferenceCapturedByClosure, closure, value, scope);
        }
    }

    void retained(const IrNode& call, const StorageOwner& scope) {
        std::vector<SymbolId> targets;
        if (call.op == IrOp::VirtualCall) {
            targets = purity_.targets(call);
        } else {
            targets.push_back(call.symbol);
        }
        for (const auto target : targets) {
            const auto* summary = known(target) ? purity_.summary(target) : nullptr;
            if (!summary) continue;
            for (std::size_t slot = 0; slot < summary->retention.size(); ++slot) {
                if (summary->retention[slot] != PurityAnalysis::Retention::Elsewhere) continue;
                const auto* argument = slot_operand(call, slot);
                if (argument) references_into(EscapeKind::ReferenceRetainedByCall, call, *argument, scope, target);
            }
        }
    }

    void emit(const IrNode& node, const StorageOwner& scope) {
        switch (node.op) {
        case IrOp::Collection:
        case IrOp::RepeatCollection: {
            if (node.type.kind == TypeKind::Array) {
                out_.creations.push_back({body_.owner, node.op, no_symbol, node.span, scope});
            } else if (node.type.kind == TypeKind::List) {
                // The elements of a list literal, or the value a list repeats.
                const std::size_t first = node.op == IrOp::RepeatCollection ? 1 : 0;
                for (std::size_t i = first; i < node.operands.size(); ++i) {
                    arrays_into(EscapeKind::ArrayInListElement, node.operands[i], node.operands[i], scope);
                }
            }
            break;
        }
        case IrOp::Call:
        case IrOp::VirtualCall:
        case IrOp::ExternalCall: {
            if (node.type.kind == TypeKind::Array) fresh_call(node, scope);
            retained(node, scope);
            break;
        }
        case IrOp::Allocate:
            if (closure_allocation(program_, node)) {
                captured(node, tasks_.contains(&node), scope);
            } else {
                retained(node, scope);
            }
            break;
        case IrOp::MakeClosure:
            captured(node, tasks_.contains(&node), scope);
            break;
        case IrOp::Initialize:
            // A global's initializer creates its storage; only a field's is a
            // store into an object.
            if (known(node.symbol) && symbol(node.symbol).kind == SymbolKind::Field && !node.operands.empty() &&
                !(initializer() && static_field(program_, node.symbol)) && facts_.tracked(node.operands.front().type)) {
                const auto kind = static_field(program_, node.symbol) ? EscapeKind::ArrayInGlobal : EscapeKind::ArrayInField;
                arrays_into(kind, node, node.operands.front(), scope, node.symbol);
            }
            break;
        case IrOp::Store:
            if (node.operands.size() == 2) stored(node, node.operands[0], node.operands[1], scope);
            break;
        case IrOp::Append:
            for (std::size_t i = 1; i < node.operands.size(); ++i) {
                arrays_into(EscapeKind::ArrayInListElement, node, node.operands[i], scope);
            }
            break;
        case IrOp::Spawn:
            if (!node.operands.empty()) task_closure(node.operands.front());
            break;
        case IrOp::SchedulerCall: scheduler(node, scope); break;
        case IrOp::Return: returned(node, scope); break;
        default: break;
        }
    }

    // Marks the closure that a task starts, which the traversal reaches next.
    void task_closure(const IrNode& value) { tasks_.insert(&value); }

    void fresh_call(const IrNode& call, const StorageOwner& scope) {
        std::vector<SymbolId> targets;
        if (call.op == IrOp::VirtualCall) {
            targets = purity_.targets(call);
        } else {
            targets.push_back(call.symbol);
        }
        if (targets.empty()) return;
        for (const auto target : targets) {
            const auto* summary = known(target) ? purity_.summary(target) : nullptr;
            if (!summary || summary->ownership != PurityAnalysis::Ownership::Fresh) return;
        }
        out_.creations.push_back({body_.owner, call.op, targets.front(), call.span, scope});
    }

    void scheduler(const IrNode& node, const StorageOwner& scope) {
        const auto& name = node.text;
        if ((name == "channel.send" || name == "channel.try_send") && node.operands.size() > 1) {
            arrays_into(EscapeKind::ArrayInChannelMessage, node, node.operands[1], scope);
        } else if ((name == select_operation || name == select_with_default)) {
            // Arms in source order: a kind, then that kind's operands.
            for (std::size_t i = 0; i < node.operands.size();) {
                const auto& kind = node.operands[i];
                if (kind.op != IrOp::Integer) break;
                const auto arm = std::strtoul(kind.text.c_str(), nullptr, 10);
                if (arm == select_send && i + 2 < node.operands.size()) {
                    arrays_into(EscapeKind::ArrayInChannelMessage, node, node.operands[i + 2], scope);
                    i += 3;
                } else {
                    i += 2;
                }
            }
        } else if (name == "parallel.start" && !node.operands.empty()) {
            task_closure(node.operands.front());
        }
    }

    void returned(const IrNode& node, const StorageOwner& scope) {
        if (node.operands.empty() || !facts_.tracked(node.operands.front().type)) return;
        const auto& value = node.operands.front();
        const auto set = origins(value, scope);
        out_.returns.push_back({node.span, set});
        for (const auto& origin : set) {
            const bool borrowed = origin.kind == Origin::Kind::Parameter || origin.kind == Origin::Kind::Global ||
                                  origin.kind == Origin::Kind::Field || origin.kind == Origin::Kind::Static;
            if (origin.kind == Origin::Kind::Local) {
                if (is_primitive_reference(value.type)) report(EscapeKind::ReferenceReturned, node, value, origin);
            } else if (borrowed && !is_primitive_reference(value.type)) {
                report(EscapeKind::ArrayReturnedBorrowed, node, value, origin);
            }
        }
    }

    const IrProgram& program_;
    const PurityAnalysis& purity_;
    const TypeFacts& facts_;
    const IrBody& body_;
    StorageOwner root_;
    std::map<SymbolId, StorageOwner> declared_;
    std::map<SymbolId, OriginSet> table_;
    std::set<const IrNode*> tasks_;
    BodyFacts out_;
};

// The bodies a program analyzes at `stage`.
bool analyzed(const IrProgram& program, const IrBody& body, StorageStage stage) {
    if (synthetic_body(program, body.owner)) return false;
    if (stage == StorageStage::Specialized && in_template(program, body.owner)) return false;
    return true;
}

} // namespace

// -- StorageOwner ---------------------------------------------------------------

bool StorageOwner::encloses(const StorageOwner& inner) const {
    if (function != inner.function || inner.path.size() <= path.size()) return false;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (!(path[i] == inner.path[i])) return false;
    }
    // The first step below this scope's own node must enter its arm.
    return inner.path[path.size()].region == arm;
}

bool StorageOwner::operator<(const StorageOwner& other) const {
    if (function != other.function) return function < other.function;
    if (arm != other.arm) return arm < other.arm;
    if (iteration != other.iteration) return iteration < other.iteration;
    return std::lexicographical_compare(path.begin(), path.end(), other.path.begin(), other.path.end(),
                                        [](const OwnerStep& a, const OwnerStep& b) {
                                            return std::tie(a.region, a.index) < std::tie(b.region, b.index);
                                        });
}

// -- Storage classes ------------------------------------------------------------

std::string_view describe_storage_class(StorageClass storage) {
    switch (storage) {
    case StorageClass::Frame: return "frame";
    case StorageClass::Static: return "static";
    case StorageClass::InObject: return "inside a heap object";
    case StorageClass::Borrow: return "borrow";
    }
    return "frame";
}

std::optional<StorageClass> storage_class(const IrProgram& program, SymbolId id) {
    if (!valid(program, id)) return std::nullopt;
    const TypeFacts facts(program);
    const auto& item = program.symbols[id];
    const bool reference = is_primitive_reference(item.type);
    switch (item.kind) {
    case SymbolKind::Local: return reference ? StorageClass::Borrow : StorageClass::Frame;
    case SymbolKind::Parameter:
        return reference || facts.holds_array(item.type) ? StorageClass::Borrow : StorageClass::Frame;
    case SymbolKind::Field:
        if (static_field(program, id)) return StorageClass::Static;
        return reference ? StorageClass::Borrow : StorageClass::InObject;
    case SymbolKind::Global: return reference ? StorageClass::Borrow : StorageClass::Static;
    default: return std::nullopt;
    }
}

// -- Creations ------------------------------------------------------------------

std::vector<ArrayCreation> find_array_creations(const IrProgram& program, const PurityAnalysis& purity,
                                                StorageStage stage) {
    const TypeFacts facts(program);
    std::vector<ArrayCreation> out;
    for (const auto& body : program.bodies) {
        if (!analyzed(program, body, stage)) continue;
        auto found = Analyzer(program, purity, facts, body).run();
        out.insert(out.end(), std::make_move_iterator(found.creations.begin()),
                   std::make_move_iterator(found.creations.end()));
    }
    return out;
}

std::vector<ArrayCreation> find_array_creations(const IrProgram& program, StorageStage stage) {
    return find_array_creations(program, PurityAnalysis(program), stage);
}

// -- Result transfer --------------------------------------------------------------

namespace {

using Transfer = ResultTransfer;

bool function_result_tracked(const IrProgram& program, SymbolId function, const TypeFacts& facts) {
    return valid(program, function) && program.symbols[function].kind == SymbolKind::Function &&
           facts.tracked(program.symbols[function].type);
}

Transfer unknown_transfer() {
    Transfer transfer;
    transfer.kind = Transfer::Kind::Unknown;
    transfer.origin = Transfer::Origin::Unknown;
    return transfer;
}

Transfer transfer_of(const Origin& origin) {
    Transfer transfer;
    transfer.kind = Transfer::Kind::Borrowed;
    transfer.symbol = origin.symbol;
    transfer.slot = origin.slot;
    switch (origin.kind) {
    case Origin::Kind::Owned:
        transfer.kind = Transfer::Kind::Fresh;
        transfer.symbol = no_symbol;
        transfer.slot = no_slot;
        break;
    case Origin::Kind::Parameter: transfer.origin = Transfer::Origin::Parameter; break;
    case Origin::Kind::Global:
    case Origin::Kind::Static: transfer.origin = Transfer::Origin::Global; break;
    case Origin::Kind::Field: transfer.origin = Transfer::Origin::Field; break;
    case Origin::Kind::Local: transfer.origin = Transfer::Origin::Local; break;
    case Origin::Kind::Unknown: return unknown_transfer();
    }
    return transfer;
}

} // namespace

std::string describe_result_transfer(const IrProgram& program, const ResultTransfer& transfer) {
    const auto name = [&](SymbolId id) { return valid(program, id) ? program.symbols[id].name : std::string("?"); };
    switch (transfer.kind) {
    case Transfer::Kind::None: return "none";
    case Transfer::Kind::Fresh: return "fresh";
    case Transfer::Kind::Unknown: return "unknown";
    case Transfer::Kind::Borrowed: break;
    }
    switch (transfer.origin) {
    case Transfer::Origin::Parameter:
        return transfer.slot == 0 ? "borrowed from the receiver" : "borrowed from parameter " + std::to_string(transfer.slot);
    case Transfer::Origin::Global: return "borrowed from global " + name(transfer.symbol);
    case Transfer::Origin::Field:
        return "borrowed from field " + name(transfer.symbol) +
               (transfer.slot == no_slot ? "" : " of slot " + std::to_string(transfer.slot));
    case Transfer::Origin::Local: return "borrowed from frame storage " + name(transfer.symbol);
    default: return "borrowed from an unknown origin";
    }
}

ResultTransfer recorded_result_transfer(const IrProgram& program, const PurityAnalysis& purity, SymbolId function) {
    const TypeFacts facts(program);
    if (!function_result_tracked(program, function, facts)) return {};
    const auto* summary = purity.summary(function);
    if (!summary) return unknown_transfer();
    Transfer transfer;
    switch (summary->ownership) {
    case PurityAnalysis::Ownership::Fresh: transfer.kind = Transfer::Kind::Fresh; break;
    case PurityAnalysis::Ownership::Borrowed:
        transfer.kind = Transfer::Kind::Borrowed;
        transfer.origin = Transfer::Origin::Parameter;
        transfer.slot = summary->borrowed_slot;
        break;
    case PurityAnalysis::Ownership::Static:
        transfer.kind = Transfer::Kind::Borrowed;
        transfer.origin = Transfer::Origin::Global;
        break;
    case PurityAnalysis::Ownership::Unknown: return unknown_transfer();
    }
    return transfer;
}

namespace {

struct Derived {
    ResultTransfer transfer;
    Span span;
};

Derived derive(const IrProgram& program, const PurityAnalysis& purity, const TypeFacts& facts, SymbolId function) {
    Derived result;
    result.transfer = unknown_transfer();
    result.span = valid(program, function) ? program.symbols[function].span : Span{};
    if (!function_result_tracked(program, function, facts)) {
        result.transfer = {};
        return result;
    }
    std::optional<Transfer> joined;
    bool conflict = false;
    bool first = true;
    for (const auto& body : program.bodies) {
        if (body.owner != function) continue;
        auto found = Analyzer(program, purity, facts, body).run();
        for (const auto& item : found.returns) {
            if (first) result.span = item.span;
            first = false;
            // A value that holds no tracked storage, such as `nil`, says nothing.
            if (item.origins.empty()) {
                conflict = true;
                continue;
            }
            for (const auto& origin : item.origins) {
                const auto one = transfer_of(origin);
                if (!joined) joined = one;
                else if (!(*joined == one)) conflict = true;
            }
        }
    }
    result.transfer = !joined || conflict ? unknown_transfer() : *joined;
    return result;
}

// Whether what a summary says contradicts what the body does.
bool agrees(const ResultTransfer& recorded, const ResultTransfer& derived) {
    using Kind = ResultTransfer::Kind;
    using Origin = ResultTransfer::Origin;
    if (recorded.kind == Kind::Unknown || derived.kind == Kind::Unknown) return true;
    if (recorded.kind != derived.kind) {
        // A reference to a local is a box until references stop being promoted.
        return false;
    }
    if (recorded.kind != Kind::Borrowed) return true;
    switch (recorded.origin) {
    case Origin::Parameter:
        if (derived.origin == Origin::Parameter) return derived.slot == recorded.slot;
        if (derived.origin == Origin::Field) return derived.slot == no_slot || derived.slot == recorded.slot;
        return false;
    case Origin::Global:
        // The summary knows a static place, not which: a field of an object
        // the analysis cannot name counts as one.
        return derived.origin == Origin::Global || derived.origin == Origin::Field;
    default: return true;
    }
}

} // namespace

ResultTransfer derived_result_transfer(const IrProgram& program, const PurityAnalysis& purity, SymbolId function) {
    const TypeFacts facts(program);
    return derive(program, purity, facts, function).transfer;
}

std::optional<TransferMismatch> check_result_transfer(const IrProgram& program, const PurityAnalysis& purity,
                                                      SymbolId function, const ResultTransfer& recorded) {
    const TypeFacts facts(program);
    const auto derived = derive(program, purity, facts, function);
    // A reference to frame storage is a fresh box once references are
    // materialized, and an unknown result before: both agree with it.
    if (derived.transfer.kind == ResultTransfer::Kind::Borrowed && derived.transfer.origin == ResultTransfer::Origin::Local &&
        (recorded.kind == ResultTransfer::Kind::Fresh || recorded.kind == ResultTransfer::Kind::Unknown)) {
        return std::nullopt;
    }
    if (agrees(recorded, derived.transfer)) return std::nullopt;
    return TransferMismatch{function, derived.span, recorded, derived.transfer};
}

std::vector<TransferMismatch> check_result_transfer(const IrProgram& program, const PurityAnalysis& purity,
                                                    StorageStage stage) {
    const TypeFacts facts(program);
    std::vector<TransferMismatch> out;
    std::set<SymbolId> seen;
    for (const auto& body : program.bodies) {
        if (!analyzed(program, body, stage) || !seen.insert(body.owner).second) continue;
        if (!function_result_tracked(program, body.owner, facts)) continue;
        if (auto mismatch = check_result_transfer(program, purity, body.owner,
                                                  recorded_result_transfer(program, purity, body.owner))) {
            out.push_back(std::move(*mismatch));
        }
    }
    return out;
}

// -- Escapes ----------------------------------------------------------------------

std::string_view describe_escape_kind(EscapeKind kind) {
    switch (kind) {
    case EscapeKind::ArrayInField: return "array stored in a field";
    case EscapeKind::ArrayInGlobal: return "array stored in a global";
    case EscapeKind::ArrayInListElement: return "array stored in a list element";
    case EscapeKind::ArrayInChannelMessage: return "array sent on a channel";
    case EscapeKind::ArrayInTaskArgument: return "array passed to a task";
    case EscapeKind::ArrayCapturedByClosure: return "array captured by a closure";
    case EscapeKind::ArrayAssignedToOuterBinding: return "array assigned to a binding of an outer block";
    case EscapeKind::ArrayReturnedBorrowed: return "array returned but not created";
    case EscapeKind::ReferenceReturned: return "reference to a local returned";
    case EscapeKind::ReferenceInField: return "reference to a local stored in a field or global";
    case EscapeKind::ReferenceCapturedByClosure: return "reference to a local captured by a closure";
    case EscapeKind::ReferenceRetainedByCall: return "reference to a local retained by a call";
    }
    return "";
}

std::vector<StorageEscape> find_storage_escapes(const IrProgram& program, const PurityAnalysis& purity,
                                                StorageStage stage) {
    const TypeFacts facts(program);
    std::vector<StorageEscape> out;
    for (const auto& body : program.bodies) {
        if (!analyzed(program, body, stage)) continue;
        auto found = Analyzer(program, purity, facts, body).run();
        out.insert(out.end(), std::make_move_iterator(found.escapes.begin()),
                   std::make_move_iterator(found.escapes.end()));
    }
    return out;
}

std::vector<StorageEscape> find_storage_escapes(const IrProgram& program, StorageStage stage) {
    return find_storage_escapes(program, PurityAnalysis(program), stage);
}

} // namespace neura

#include "class_hooks.hpp"
#include "hook_arguments.hpp"

#include "attributes.hpp"
#include "kernel_signature.hpp"
#include "nominal.hpp"
#include "purity.hpp"
#include "task_analysis.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace neura {

SymbolId declared_hook(const IrSymbol& cls, HookKind kind) {
    if (cls.kind != SymbolKind::Class) return no_symbol;
    const auto* link = find_attribute(cls, kind == HookKind::Get ? AttributeId::HookGet : AttributeId::HookSet);
    return link ? link->value.symbol : no_symbol;
}

SymbolId effective_hook(const IrProgram& program, SymbolId cls, HookKind kind) {
    // A base chain is finite and acyclic in a verified program; the bound
    // keeps a malformed one from looping.
    for (std::size_t hops = 0; cls != no_symbol && cls < program.symbols.size() && hops <= program.symbols.size();
         ++hops) {
        const auto& item = program.symbols[cls];
        if (const auto function = declared_hook(item, kind); function != no_symbol) return function;
        cls = item.base;
    }
    return no_symbol;
}

SymbolId hook_owner(const IrSymbol& function) {
    const auto* of = find_attribute(function, AttributeId::HookOf);
    return of && !of->value.items.empty() ? of->value.items.front().symbol : no_symbol;
}

bool has_class_hooks(const IrProgram& program) {
    return std::ranges::any_of(program.symbols, [](const IrSymbol& item) {
        return has_attribute(item, AttributeId::HookGet) || has_attribute(item, AttributeId::HookSet);
    });
}

} // namespace neura

// ---- Events (docs/design/Hooks030.md, Derivation) -------------------------

namespace neura {
namespace {

// A generator's state names a moved local `$local<M>:<name>`
// (docs/design/Lifetime030.md): a binding with the events of that local.
bool moved_local(const std::string& name) {
    return name.starts_with("$local") && name.find(':') != std::string::npos;
}

bool hidden_name(const std::string& name) {
    return name.starts_with('$') && !name.starts_with("$capture") && !moved_local(name);
}

// A generator's state class (docs/design/Lifetime030.md, Generators).
bool generator_class(const IrSymbol& item) {
    return item.kind == SymbolKind::Class && item.name.starts_with("$generator");
}

// A class the compiler writes: the root class and closure layouts.
bool compiler_class(const IrSymbol& item) {
    return item.kind == SymbolKind::Class && item.name.starts_with('$');
}

} // namespace

HookIndex::HookIndex(const IrProgram& program) {
    if (!has_class_hooks(program)) return;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != SymbolKind::Class || !item.generics.empty()) continue;
        const auto get = effective_hook(program, id, HookKind::Get);
        const auto set = effective_hook(program, id, HookKind::Set);
        if (get == no_symbol && set == no_symbol) continue;
        std::set<SymbolId> seen;
        for (const auto& view : nominal_views(program, item.type)) {
            if (view.symbol == no_symbol || !seen.insert(view.symbol).second) continue;
            auto& entry = views_[view.symbol];
            if (get != no_symbol) entry.get.push_back(get);
            if (set != no_symbol) entry.set.push_back(set);
        }
        // Every class is assignable to the umbrella `class`, whose symbol is
        // the root of every base chain.
        auto root = id;
        while (root < program.symbols.size() && program.symbols[root].base != no_symbol) root = program.symbols[root].base;
        if (root != id && seen.insert(root).second) {
            auto& entry = views_[root];
            if (get != no_symbol) entry.get.push_back(get);
            if (set != no_symbol) entry.set.push_back(set);
        }
    }
    for (auto& [symbol, view] : views_) {
        for (auto* list : {&view.get, &view.set}) {
            std::ranges::sort(*list);
            list->erase(std::unique(list->begin(), list->end()), list->end());
        }
    }
}

bool HookIndex::carrying(const IrType& type) const {
    return type.kind == TypeKind::Object && views_.contains(type.symbol);
}

std::vector<SymbolId> HookIndex::targets(const IrType& type, HookKind kind) const {
    if (type.kind != TypeKind::Object) return {};
    const auto found = views_.find(type.symbol);
    if (found == views_.end()) return {};
    return kind == HookKind::Get ? found->second.get : found->second.set;
}

namespace {

class Elaborator {
public:
    Elaborator(const IrProgram& program, const HookIndex& index) : program_(program), index_(index) {
        for (const auto& item : program.symbols)
            if (item.singleton_instance != no_symbol) singletons_.insert(item.singleton_instance);
    }

    void body(IrBody& body) {
        function_ = body.owner;
        exempt_body_ = false;
        exempt_signature_ = false;
        if (function_ < program_.symbols.size()) {
            const auto& function = program_.symbols[function_];
            exempt_signature_ = has_attribute(function, AttributeId::AccessorOf) ||
                                has_attribute(function, AttributeId::HookOf);
            // An accessor adapter reads and writes its field's storage as
            // part of the one event at its call.
            accessor_field_ = no_symbol;
            if (const auto* of = find_attribute(function, AttributeId::AccessorOf); of && !of->value.items.empty())
                accessor_field_ = of->value.items.front().symbol;
            const auto owner = function.owner;
            // A closure layout's constructor stores the captures that the
            // MakeClosure admitted; the root class's members are built in.
            exempt_body_ = owner < program_.symbols.size() && compiler_class(program_.symbols[owner]) &&
                           (function.name == "init" || program_.symbols[owner].name == "$class");
            // A generator's generated has_next and next add no event: the
            // body of `$resume` keeps every event of the source body.
            if (owner < program_.symbols.size() && generator_class(program_.symbols[owner]) &&
                (function.name == "has_next" || function.name == "next"))
                exempt_body_ = true;
        }
        if (exempt_body_) return;
        for (auto& node : body.instructions) value(node);
    }

private:
    const IrProgram& program_;
    const HookIndex& index_;
    std::set<SymbolId> singletons_;
    SymbolId function_ = no_symbol;
    bool exempt_body_ = false;
    bool exempt_signature_ = false;
    SymbolId accessor_field_ = no_symbol;

    const IrSymbol* symbol(SymbolId id) const { return id < program_.symbols.size() ? &program_.symbols[id] : nullptr; }

    // A member of a generator's state class.
    bool generator_member(SymbolId id) const {
        const auto* item = symbol(id);
        const auto* owner = item ? symbol(item->owner) : nullptr;
        return owner && generator_class(*owner);
    }

    // A local, parameter, global or field that is not a compiler temporary,
    // and not a parameter of an accessor or hook function.
    bool source_binding(SymbolId id) const {
        const auto* item = symbol(id);
        if (!item || hidden_name(item->name) || id == accessor_field_) return false;
        if (item->kind == SymbolKind::Parameter && exempt_signature_ && item->owner == function_) return false;
        return item->kind == SymbolKind::Local || item->kind == SymbolKind::Parameter ||
               item->kind == SymbolKind::Global || item->kind == SymbolKind::Field;
    }

    SymbolId accessor_of(const IrNode& node, std::uint64_t kind) const {
        if (node.op != IrOp::Call && node.op != IrOp::VirtualCall) return no_symbol;
        const auto* target = symbol(node.symbol);
        if (!target) return no_symbol;
        const auto* of = find_attribute(*target, AttributeId::AccessorOf);
        if (!of || of->value.items.size() != 2 || of->value.items[1].integer != kind) return no_symbol;
        return of->value.items[0].symbol;
    }

    bool read_site(const IrNode& node) const {
        if (!index_.carrying(node.type) || transports_argument(node)) return false;
        switch (node.op) {
        case IrOp::Load:
        case IrOp::Field:
            return source_binding(node.symbol);
        case IrOp::Index:
        case IrOp::Project:
        case IrOp::EnumProject:
        case IrOp::RemoveAt:
            return true;
        case IrOp::ClassCast:
            // A match.is binding refines the selected reference from an any
            // snapshot. The ClassIs and nil checks inspect that snapshot raw.
            return node.operands.size() == 1 && node.operands.front().op == IrOp::AnyUnpack;
        case IrOp::Call:
        case IrOp::VirtualCall:
            return accessor_of(node, 0) != no_symbol;
        default:
            return false;
        }
    }

    static IrNode wrap(IrOp op, IrNode inner, const IrType& type, SymbolId target = no_symbol) {
        IrNode result{};
        result.op = op;
        result.type = type;
        result.span = inner.span;
        result.symbol = target;
        result.operands.push_back(std::move(inner));
        return result;
    }

    // Elaborates `slot` and wraps it when it is a read.
    void value(IrNode& slot) {
        visit(slot);
        if (read_site(slot)) {
            const auto type = slot.type;
            slot = wrap(IrOp::HookRead, std::move(slot), type);
        }
    }

    // Wraps an elaborated operand admitted to a destination of `type`.
    void admit(IrNode& slot, const IrType& type, SymbolId target = no_symbol) {
        if (index_.carrying(type)) slot = wrap(IrOp::HookAdmit, std::move(slot), type, target);
    }

    // A Store's destination: its own operands are read, the place is not.
    void place(IrNode& destination) {
        for (auto& operand : destination.operands) value(operand);
        for (auto& child : destination.body) value(child);
        for (auto& child : destination.alternative) value(child);
    }

    // The declared types of a call's written arguments, from `first` on.
    void arguments(IrNode& node, std::size_t first, const std::vector<IrType>& parameters, bool variadic) {
        for (std::size_t i = first; i < node.operands.size(); ++i) {
            const auto k = i - first;
            if (parameters.empty()) break;
            IrType declared = k < parameters.size() ? parameters[k] : parameters.back();
            if (variadic && k + 1 >= parameters.size() && declared.kind == TypeKind::Array &&
                !declared.arguments.empty() && node.operands[i].type != declared) {
                declared = declared.arguments.front();
            }
            if (!transports_argument(node.operands[i])) admit(node.operands[i], declared);
        }
    }

    std::vector<IrType> parameter_types(SymbolId function) const {
        std::vector<IrType> result;
        if (const auto* item = symbol(function)) {
            for (const auto parameter : item->parameters)
                if (const auto* p = symbol(parameter)) result.push_back(p->type);
        }
        return result;
    }

    bool variadic(SymbolId function) const {
        const auto* item = symbol(function);
        return item && item->variadic != VariadicKind::None;
    }

    void call(IrNode& node) {
        for (auto& operand : node.operands) value(operand);
        for (auto& child : node.body) value(child);
        for (auto& child : node.alternative) value(child);
        if (node.op == IrOp::CallClosure) {
            if (node.operands.empty()) return;
            const auto& callee = node.operands.front().type;
            if (!is_function_value(callee)) return;
            std::vector<IrType> parameters(callee.arguments.begin(), callee.arguments.end() - 1);
            arguments(node, 1, parameters, false);
            return;
        }
        if (const auto field = accessor_of(node, 1); field != no_symbol) {
            // `object.field = x` through a setter adapter: one admission to
            // the field before the adapter runs, with the field's stored
            // reference as the previous value.
            if (node.operands.size() >= 2) admit(node.operands.back(), symbol(field)->type, field);
            return;
        }
        const auto parameters = parameter_types(node.symbol);
        std::size_t first = 0;
        if (node.op == IrOp::VirtualCall || node.op == IrOp::BaseInitialize) first = 1;
        else if (node.op == IrOp::Call && node.operands.size() == parameters.size() + 1) first = 1;
        arguments(node, first, parameters, variadic(node.symbol));
    }

    void visit(IrNode& node) {
        switch (node.op) {
        case IrOp::HookRead:
        case IrOp::HookAdmit:
            throw std::logic_error("class hook events are already elaborated");
        case IrOp::Store: {
            if (node.operands.size() != 2) break;
            place(node.operands[0]);
            value(node.operands[1]);
            const auto& destination = node.operands[0];
            // A store to the carried return value is no event; the store to
            // a generator's buffer before a Yield is the result admission.
            if (destination.op == IrOp::Pending) return;
            const bool buffer = destination.op == IrOp::Field && generator_member(destination.symbol) &&
                                symbol(destination.symbol)->name == "$buffer";
            const bool hidden = (destination.op == IrOp::Load || destination.op == IrOp::Field) &&
                                !source_binding(destination.symbol) && !buffer;
            if (!hidden) admit(node.operands[1], destination.type);
            return;
        }
        case IrOp::Initialize: {
            const auto* target = symbol(node.symbol);
            // A `using` token copies the admitted binding without a getter.
            if (target && target->name.starts_with("$using")) {
                for (auto& operand : node.operands) visit(operand);
                return;
            }
            for (auto& operand : node.operands) value(operand);
            if (node.operands.size() != 1 || !target ||
                (!source_binding(node.symbol) && node.text != parameter_admission)) return;
            // A singleton's instance is created by the compiler, not stored
            // by the program; reading it is an event.
            if (singletons_.contains(node.symbol)) return;
            admit(node.operands.front(), target->type);
            return;
        }
        case IrOp::Return: {
            for (auto& operand : node.operands) value(operand);
            // The generated forwarding of a generator's has_next and next
            // adds no event.
            if (node.operands.size() == 1 && node.operands.front().op == IrOp::VirtualCall &&
                generator_member(node.operands.front().symbol))
                return;
            if (node.operands.size() == 1 && !exempt_signature_ && node.text != argument_transport)
                if (const auto* function = symbol(function_)) admit(node.operands.front(), function->type);
            return;
        }
        case IrOp::Call:
        case IrOp::VirtualCall:
        case IrOp::CallClosure:
        case IrOp::BaseInitialize:
        case IrOp::Allocate:
            // A generator's call copies its admitted arguments and captures
            // into the state: not a second read or admission.
            if (node.op == IrOp::Allocate && generator_member(node.symbol)) {
                for (auto& operand : node.operands) visit(operand);
                return;
            }
            call(node);
            return;
        case IrOp::MakeClosure: {
            for (auto& operand : node.operands) value(operand);
            arguments(node, 0, parameter_types(node.symbol), false);
            return;
        }
        case IrOp::Collection: {
            for (auto& operand : node.operands) value(operand);
            if (!node.type.arguments.empty())
                for (auto& operand : node.operands) admit(operand, node.type.arguments.front());
            return;
        }
        case IrOp::RepeatCollection: {
            for (auto& operand : node.operands) value(operand);
            if (node.operands.size() == 2 && !node.type.arguments.empty())
                admit(node.operands[1], node.type.arguments.front());
            return;
        }
        case IrOp::Append: {
            for (auto& operand : node.operands) value(operand);
            if (node.operands.size() == 2 && node.operands[0].type.kind == TypeKind::List &&
                !node.operands[0].type.arguments.empty())
                admit(node.operands[1], node.operands[0].type.arguments.front());
            return;
        }
        // Every other operation has no event of its own; its operands and
        // regions keep theirs. The list is the derivation table's: a new
        // operation does not compile until it has a row.
        case IrOp::Load: case IrOp::Reference: case IrOp::Receiver: case IrOp::Field: case IrOp::Index:
        case IrOp::Integer: case IrOp::Float: case IrOp::String: case IrOp::Boolean: case IrOp::Unary:
        case IrOp::Binary: case IrOp::ShortCircuit: case IrOp::ExternalCall: case IrOp::Length:
        case IrOp::RemoveAt: case IrOp::Block: case IrOp::Branch: case IrOp::Loop: case IrOp::Break:
        case IrOp::Continue: case IrOp::Destroy: case IrOp::Discard: case IrOp::Convert:
        case IrOp::CollectionVersion: case IrOp::Fail: case IrOp::Tuple: case IrOp::Project:
        case IrOp::EnumConstruct: case IrOp::EnumTag: case IrOp::EnumProject: case IrOp::ComparisonChain:
        case IrOp::ScopedValue: case IrOp::SelectValue: case IrOp::TextLength: case IrOp::TextByteAt:
        case IrOp::TextSlice: case IrOp::TextFromUtf8: case IrOp::TextEqual: case IrOp::ReinterpretFloat:
        case IrOp::TrapMessage: case IrOp::Nil: case IrOp::ClassHash: case IrOp::ClassText:
        case IrOp::ClassIs: case IrOp::ClassCast: case IrOp::ValueEquals: case IrOp::ValueHash:
        case IrOp::ValueText: case IrOp::ReferencePlace: case IrOp::TaskScope: case IrOp::Spawn:
        case IrOp::ConformanceCall: case IrOp::SchedulerCall: case IrOp::AnyPack: case IrOp::AnyTag: case IrOp::AnyUnpack:
        // docs/design/Lifetime030.md: a Cleanup's lists keep their events,
        // which happen when they run; reading Pending and a Yield are none.
        case IrOp::Cleanup: case IrOp::Pending: case IrOp::Yield:
        case IrOp::ConcatText: case IrOp::TypeOf:
            break;
        }
        for (auto& operand : node.operands) value(operand);
        for (auto& child : node.body) value(child);
        for (auto& child : node.alternative) value(child);
    }
};

std::string where(const Span& span) {
    return span.file + ":" + std::to_string(span.line) + ":" + std::to_string(span.column);
}

void strip(std::vector<IrNode>& nodes) {
    for (auto& node : nodes) {
        while ((node.op == IrOp::HookRead || node.op == IrOp::HookAdmit) && node.operands.size() == 1) {
            auto inner = std::move(node.operands.front());
            node = std::move(inner);
        }
        strip(node.operands);
        strip(node.body);
        strip(node.alternative);
    }
}

std::optional<std::string> first_difference(const IrNode& expected, const IrNode& actual) {
    if (expected.op != actual.op) {
        if (expected.op == IrOp::HookRead || expected.op == IrOp::HookAdmit)
            return "an event at " + where(expected.span) + " is not wrapped";
        return "a class hook node at " + where(actual.span) + " wraps no event";
    }
    if (expected.type != actual.type || expected.symbol != actual.symbol)
        return "the class hook node at " + where(actual.span) + " has the wrong destination";
    for (const auto pair : {&IrNode::operands, &IrNode::body, &IrNode::alternative}) {
        const auto& a = expected.*pair;
        const auto& b = actual.*pair;
        if (a.size() != b.size()) return "the tree at " + where(actual.span) + " changed shape";
        for (std::size_t i = 0; i < a.size(); ++i)
            if (auto found = first_difference(a[i], b[i])) return found;
    }
    return std::nullopt;
}

} // namespace

namespace {

bool contains_reference(const IrProgram& program, const HookIndex& index, const IrType& type,
                        bool collections, std::set<SymbolId>& visited) {
    if (index.carrying(type) || (type.kind == TypeKind::Any && index.any())) return true;
    if (type.kind == TypeKind::Tuple || (collections && (type.kind == TypeKind::Array || type.kind == TypeKind::List))) {
        return std::ranges::any_of(type.arguments, [&](const auto& part) {
            return contains_reference(program, index, part, collections, visited);
        });
    }
    if (type.kind == TypeKind::Enum && visited.insert(type.symbol).second) {
        for (const auto& variant : program.symbols) {
            if (variant.kind != SymbolKind::Variant || variant.owner != type.symbol) continue;
            for (const auto& payload : variant.payload_types)
                if (contains_reference(program, index, payload, collections, visited)) return true;
        }
    }
    return false;
}

} // namespace

bool HookIndex::contains_reference(const IrProgram& program, const IrType& type, bool collections) const {
    if (!any()) return false;
    std::set<SymbolId> visited;
    return neura::contains_reference(program, *this, type, collections, visited);
}

namespace {

// E0546: the other side of a host binding, a kernel or a foreign function
// has no policy word and could not keep one.
void check_hook_boundaries(const IrProgram& program, const HookIndex& index) {
    for (const auto& item : program.symbols) {
        if (item.kind != SymbolKind::Function || !item.generics.empty() || (!item.host_binding && !item.external && !kernel_bound(item)))
            continue;
        const auto reject = [&](const IrType& type, const Span& at, const std::string& what) {
            const auto cls = type.kind == TypeKind::Object && type.symbol < program.symbols.size()
                                 ? program.symbols[type.symbol].name : std::string("this type");
            throw Diagnostic(at, "'" + cls + "' references carry a class policy that a host binding cannot keep")
                .with_code("E0546")
                .label(what + " of '" + item.name + "', which has no body in the program")
                .help("pass an identifier or a plain data class across the boundary");
        };
        for (const auto parameter : item.parameters) {
            const auto& p = program.symbols[parameter];
            if (index.contains_reference(program, p.type, true)) reject(p.type, p.span, "a parameter");
        }
        if (index.contains_reference(program, item.type, true)) reject(item.type, item.span, "the result");
    }
}

// E0544: a hook body that may suspend, directly or through what it calls.
void check_hook_scheduling(const IrProgram& program) {
    const PurityAnalysis purity(program);
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (!has_attribute(item, AttributeId::HookOf)) continue;
        const auto* summary = purity.summary(id);
        if (!summary || !summary->scheduler) continue;
        const auto cause = purity.origin(id, PurityAnalysis::Fact::Scheduler).value_or(*summary->scheduler);
        auto error = Diagnostic(item.span, "a class hook cannot use the scheduler: it " + cause.operation +
                                               ", which may suspend").with_code("E0544")
            .label("this hook runs while its event is open");
        if (!cause.span.file.empty()) error.label(cause.span, "it " + cause.operation + " here");
        throw error.note("an event starts and ends without its task being suspended")
            .help("do the scheduling work outside the hook, and keep the hook to checks and existing objects");
    }
}

} // namespace

IrNode strip_class_hooks(const IrNode& node) {
    std::vector<IrNode> nodes{node};
    strip(nodes);
    return std::move(nodes.front());
}

std::optional<std::string> check_class_hook_events(const IrProgram& program) {
    const HookIndex index(program);
    Elaborator elaborator(program, index);
    for (const auto& body : program.bodies) {
        IrBody expected{body.owner, body.instructions};
        strip(expected.instructions);
        if (index.any()) elaborator.body(expected);
        if (expected.instructions.size() != body.instructions.size()) return "a body changed shape";
        for (std::size_t i = 0; i < expected.instructions.size(); ++i)
            if (auto found = first_difference(expected.instructions[i], body.instructions[i])) return found;
    }
    return std::nullopt;
}

void elaborate_class_hooks(IrProgram& program) {
    if (!has_class_hooks(program)) return;
    const HookIndex index(program);
    check_hook_boundaries(program, index);
    prepare_class_hook_arguments(program, index);
    Elaborator elaborator(program, index);
    for (auto& body : program.bodies) elaborator.body(body);
    program.hooks_elaborated = true;
    check_hook_scheduling(program);
    // A clause's own effects join every function with an event, so #Pure is
    // checked again on the elaborated program (case 52).
    check_pure_tags(program, {}, true);
    check_task_safety(program);
}

} // namespace neura

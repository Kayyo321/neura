#include "monitors.hpp"

#include "attributes.hpp"
#include "kernel_signature.hpp"
#include "purity.hpp"
#include "tags.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace neura {
namespace {

bool valid(const IrProgram& program, SymbolId id) { return id < program.symbols.size(); }

std::string quoted(const std::string& name) { return "'" + name + "'"; }

std::string display(const IrType& type) {
    if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
        return (type.arguments.empty() ? std::string("?") : display(type.arguments.front())) +
               (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    return type.name.empty() ? std::string("value") : type.name;
}

// A symbol inside a generic declaration that specialization copies before
// conversion; the copies are what run.
bool template_symbol(const IrProgram& program, SymbolId id) {
    std::set<SymbolId> seen;
    for (auto scope = id; valid(program, scope) && seen.insert(scope).second; scope = program.symbols[scope].owner) {
        if (!program.symbols[scope].generics.empty()) return true;
    }
    return false;
}

// The `export` keyword that starts an exported member's declaration, which
// is what a monitor's rules are about; a whole declaration would be a long label.
Span export_keyword(const Span& span) {
    auto result = span;
    result.length = std::min<std::size_t>(span.length, 6);
    return result;
}

bool constructor_or_destructor(const IrSymbol& item) { return item.name == "init" || item.name == "deinit"; }

bool accessor(const IrSymbol& item) { return has_attribute(item, AttributeId::AccessorOf); }

template <typename Visit>
void each_node(std::vector<IrNode>& nodes, const Visit& visit) {
    for (auto& node : nodes) {
        visit(node);
        each_node(node.operands, visit);
        each_node(node.body, visit);
        each_node(node.alternative, visit);
    }
}

IrNode make(IrOp op, IrType type, const Span& span) {
    IrNode node;
    node.op = op;
    node.type = std::move(type);
    node.span = span;
    return node;
}

IrType void_type() { return IrType{TypeKind::Void, "void", no_symbol, {}}; }

} // namespace

bool synchronized_method(const IrSymbol& symbol) {
    const auto* found = find_attribute(symbol, AttributeId::Synchronized);
    return found && found->value.kind == AttributeValueKind::Bool && found->value.integer != 0;
}

bool umbrella_class(const IrProgram& program, SymbolId cls) {
    return valid(program, cls) && program.symbols[cls].kind == SymbolKind::Class &&
           program.symbols[cls].owner == no_symbol && program.symbols[cls].name == "$class";
}

bool has_synchronized_methods(const IrProgram& program, SymbolId cls) {
    std::set<SymbolId> chain;
    for (auto scope = cls; valid(program, scope) && chain.insert(scope).second; scope = program.symbols[scope].base) {}
    for (const auto& item : program.symbols) {
        if (item.kind == SymbolKind::Function && chain.contains(item.owner) && synchronized_method(item)) return true;
    }
    return false;
}

std::optional<MemberProblem> monitor_problem(const IrProgram& program, SymbolId cls) {
    if (!valid(program, cls) || program.symbols[cls].kind != SymbolKind::Class) {
        return MemberProblem{"it is not a class", {}};
    }
    const auto& item = program.symbols[cls];
    if (!has_synchronized_methods(program, cls)) {
        return MemberProblem{"it has no #Synchronized method", item.span};
    }
    if (valid(program, item.base) && !umbrella_class(program, item.base)) {
        if (const auto problem = monitor_problem(program, item.base)) {
            return MemberProblem{"its base class " + quoted(program.symbols[item.base].name) + " is not a monitor: " +
                                     problem->text, problem->span};
        }
    }
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& member = program.symbols[id];
        if (member.owner != cls) continue;
        // A bodyless requirement runs nothing; its implementations are
        // methods of the subclasses, which must be monitors too.
        if (member.kind == SymbolKind::Function && member.exported && member.has_body &&
            !constructor_or_destructor(member) && !accessor(member) && !synchronized_method(member)) {
            return MemberProblem{"method " + quoted(member.name) + " is exported and not synchronized",
                                 export_keyword(member.span)};
        }
        if (member.kind != SymbolKind::Field || !member.exported) continue;
        // Code outside the class reaches a field with accessors only through
        // them, and they are synchronized.
        if (has_attribute(member, AttributeId::AccessorGet) || has_attribute(member, AttributeId::AccessorSet)) continue;
        if (!member.constant) {
            return MemberProblem{"field " + quoted(member.name) + " is exported and can be assigned without the guard",
                                 export_keyword(member.span)};
        }
        if (!immutable_type(program, member.type)) {
            return MemberProblem{"field " + quoted(member.name) + " is exported and its type " +
                                     display(member.type) + " is mutable", export_keyword(member.span)};
        }
    }
    return std::nullopt;
}

void declare_checked_tags(IrProgram& program, SymbolId first, const std::function<Span(SymbolId)>& name_span) {
    const auto count = program.symbols.size();
    const auto tag_or = [&](SymbolId id, TagId tag) { return tag_span(program.symbols[id], tag).value_or(name_span(id)); };
    // #Synchronized on a method.
    for (SymbolId id = first; id < count; ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != SymbolKind::Function || !has_tag(item, TagId::Synchronized)) continue;
        const auto at = tag_or(id, TagId::Synchronized);
        const auto owner = item.owner;
        const auto reject = [&](const std::string& why, const std::string& help) {
            throw Diagnostic(at, "#Synchronized method " + quoted(item.name) + " " + why).with_code("E0921")
                .label("#Synchronized given here").label(name_span(id), "declared here").help(help);
        };
        if (!valid(program, owner) || program.symbols[owner].kind == SymbolKind::Interface) {
            reject("has no body to synchronize: it is an interface method",
                   "mark the implementations #Synchronized instead");
        }
        if (is_static_scope(program.symbols[owner]) || program.symbols[owner].kind != SymbolKind::Class) {
            reject("is static: it has no receiver whose guard it could take",
                   "remove #Synchronized, or make it an instance method");
        }
        if (item.name == "init") reject("is a constructor", "a constructor runs before any other task can reach the object; remove #Synchronized");
        if (item.name == "deinit") reject("is a destructor", "remove #Synchronized");
        if (item.host_binding || kernel_bound(item)) {
            reject("is bound to a host service or a kernel and has no body to synchronize", "remove #Synchronized");
        }
        if (!item.has_body) reject("has no body to synchronize", "remove #Synchronized");
        set_attribute(program.symbols[id], AttributeId::Synchronized, attribute_bool(true));
    }
    // #Synchronized on a class: every exported method with a body.
    for (SymbolId id = first; id < count; ++id) {
        const auto& cls = program.symbols[id];
        if (cls.kind != SymbolKind::Class || !has_tag(cls, TagId::Synchronized)) continue;
        for (SymbolId member = 0; member < count; ++member) {
            const auto& function = program.symbols[member];
            if (function.kind != SymbolKind::Function || function.owner != id || !function.exported ||
                !function.has_body || constructor_or_destructor(function) || accessor(function))
                continue;
            set_attribute(program.symbols[member], AttributeId::Synchronized, attribute_bool(true));
        }
    }
    // The classes that declare a synchronized method.
    std::set<SymbolId> synchronized_owners;
    for (const auto& item : program.symbols) {
        if (item.kind == SymbolKind::Function && synchronized_method(item)) synchronized_owners.insert(item.owner);
    }
    // The accessors of a class with synchronized methods take the guard too.
    for (SymbolId id = first; id < count && !synchronized_owners.empty(); ++id) {
        const auto& function = program.symbols[id];
        if (function.kind != SymbolKind::Function || !accessor(function) || !function.has_body) continue;
        if (synchronized_owners.contains(function.owner)) {
            set_attribute(program.symbols[id], AttributeId::Synchronized, attribute_bool(true));
        }
    }
    for (SymbolId id = first; id < count; ++id) {
        const auto& item = program.symbols[id];
        // An override keeps its base's guard (E0922).
        if (item.kind == SymbolKind::Function && !synchronized_method(item)) {
            std::set<SymbolId> seen;
            for (auto base = item.overrides; valid(program, base) && seen.insert(base).second;
                 base = program.symbols[base].overrides) {
                if (!synchronized_method(program.symbols[base])) continue;
                auto error = Diagnostic(name_span(id), "method " + quoted(item.name) +
                                                           " overrides a synchronized method and is not #Synchronized")
                    .with_code("E0922").label("not synchronized");
                if (const auto tag = tag_span(program.symbols[base], TagId::Synchronized)) {
                    error.label(*tag, quoted(program.symbols[base].name) + " is synchronized here");
                } else {
                    error.label(program.symbols[base].span, quoted(program.symbols[base].name) + " is synchronized");
                }
                error.help("mark the override #Synchronized, so that every implementation runs under the guard");
                throw error;
            }
        }
        if (item.kind != SymbolKind::Class) continue;
        // #NotThreadSafe on a class, or on a base: its instances stay in one
        // task, so the class cannot promise that tasks may share them
        // (E0610). The same declaration's #Immutable or #Synchronized is
        // E0907.
        {
            SymbolId marked = no_symbol;
            std::set<SymbolId> chain;
            for (auto scope = id; valid(program, scope) && chain.insert(scope).second;
                 scope = program.symbols[scope].base) {
                if (marked == no_symbol && has_tag(program.symbols[scope], TagId::NotThreadSafe)) marked = scope;
            }
            if (marked != no_symbol) {
                const auto declined = tag_or(marked, TagId::NotThreadSafe);
                const auto here = marked == id ? std::string("here") : "on " + quoted(program.symbols[marked].name);
                const auto conflict = [&](const Span& at, const std::string& what, const std::string& label) {
                    return Diagnostic(at, "class " + quoted(item.name) + " is #NotThreadSafe and " + what)
                        .with_code("E0610").label(label).label(declined, "#NotThreadSafe given " + here)
                        .note("the instances of a #NotThreadSafe class stay in the task that holds them, so tasks "
                              "never share one")
                        .help("remove #NotThreadSafe, or the promise that tasks may share the instances");
                };
                if (const auto tag = tag_span(item, TagId::ThreadSafe); tag && marked != id) {
                    throw conflict(*tag, "cannot be #ThreadSafe", "#ThreadSafe given here");
                }
                if (const auto tag = tag_span(item, TagId::Immutable); tag && marked != id) {
                    throw conflict(*tag, "cannot be #Immutable", "#Immutable given here");
                }
                for (SymbolId member = 0; member < count; ++member) {
                    const auto& function = program.symbols[member];
                    if (function.kind != SymbolKind::Function || !chain.contains(function.owner) ||
                        !synchronized_method(function)) {
                        continue;
                    }
                    // The method's tag, or the class's, which marks every
                    // exported method.
                    auto at = tag_span(function, TagId::Synchronized);
                    if (!at) at = tag_span(program.symbols[function.owner], TagId::Synchronized);
                    throw conflict(at.value_or(function.span),
                                   "cannot be a monitor: method " + quoted(function.name) + " is #Synchronized",
                                   "#Synchronized given here");
                }
            }
        }
        // #Immutable: no field changes after construction (E0918-E0920).
        const bool immutable = has_tag(item, TagId::Immutable);
        if (valid(program, item.base) && !umbrella_class(program, item.base)) {
            const auto& base = program.symbols[item.base];
            if (immutable && !has_tag(base, TagId::Immutable)) {
                throw Diagnostic(tag_or(id, TagId::Immutable), "#Immutable class " + quoted(item.name) +
                                                                   " extends " + quoted(base.name) +
                                                                   ", which is not #Immutable")
                    .with_code("E0920").label("marked #Immutable here")
                    .label(base.span, quoted(base.name) + " is declared here")
                    .note("an inherited field could change after construction")
                    .help("mark " + quoted(base.name) + " #Immutable, or do not extend it");
            }
            if (!immutable && has_tag(base, TagId::Immutable)) {
                auto error = Diagnostic(name_span(id), "class " + quoted(item.name) + " extends the #Immutable class " +
                                                           quoted(base.name) + " and is not #Immutable")
                    .with_code("E0920").label("not #Immutable");
                if (const auto tag = tag_span(base, TagId::Immutable))
                    error.label(*tag, quoted(base.name) + " is marked #Immutable here");
                error.note("a value of type " + quoted(base.name) + " may be an instance of any subclass");
                error.help("mark " + quoted(item.name) + " #Immutable");
                throw error;
            }
        }
        if (immutable) {
            for (SymbolId field = 0; field < count; ++field) {
                const auto& member = program.symbols[field];
                if (member.kind != SymbolKind::Field || member.owner != id) continue;
                if (!member.constant) {
                    throw Diagnostic(name_span(field), "field " + quoted(member.name) + " of #Immutable class " +
                                                           quoted(item.name) + " can be assigned after construction")
                        .with_code("E0918").label("declared with let, so it can be assigned again")
                        .label(tag_or(id, TagId::Immutable), quoted(item.name) + " is marked #Immutable here")
                        .help("declare the field with last or const");
                }
                if (!immutable_type(program, member.type)) {
                    throw Diagnostic(name_span(field), "field " + quoted(member.name) + " of #Immutable class " +
                                                           quoted(item.name) + " has the mutable type " +
                                                      display(member.type))
                        .with_code("E0919").label("its value can change after construction")
                        .label(tag_or(id, TagId::Immutable), quoted(item.name) + " is marked #Immutable here")
                        .note("immutable types are bool, numbers, string, tuples and enums of immutable types, and "
                              "classes marked #Immutable")
                        .help("store an immutable value, or mark its class #Immutable");
                }
            }
        }
    }
    // A monitor by its declaration; the linked program decides whether its
    // instances may be shared.
    for (SymbolId id = first; id < count && !synchronized_owners.empty(); ++id) {
        if (program.symbols[id].kind != SymbolKind::Class) continue;
        bool candidate = false;
        std::set<SymbolId> chain;
        for (auto scope = id; valid(program, scope) && chain.insert(scope).second; scope = program.symbols[scope].base) {
            candidate = candidate || synchronized_owners.contains(scope);
        }
        if (candidate && !monitor_problem(program, id)) {
            set_attribute(program.symbols[id], AttributeId::ClassShareable, attribute_bool(true));
        }
    }
}

namespace {

class SynchronizedLowering {
public:
    explicit SynchronizedLowering(IrProgram& program) : program_(program) {}

    void run() {
        std::vector<SymbolId> synchronized;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.kind != SymbolKind::Function || !synchronized_method(item) || !item.has_body ||
                !valid(program_, item.owner) || program_.symbols[item.owner].kind != SymbolKind::Class ||
                template_symbol(program_, id) || !body_index(id))
                continue;
            synchronized.push_back(id);
        }
        if (synchronized.empty()) return;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.kind != SymbolKind::Function) continue;
            std::set<SymbolId> seen;
            for (auto base = item.overrides; valid(program_, base) && seen.insert(base).second;
                 base = program_.symbols[base].overrides) {
                overriders_[base].push_back(id);
            }
        }
        for (const auto id : synchronized) request(id);
        while (!pending_.empty()) {
            const auto original = pending_.front();
            pending_.pop_front();
            clone(original);
        }
        // The locked methods dispatch like the methods they copy.
        for (const auto& [original, copy] : locked_) {
            const auto base = program_.symbols[original].overrides;
            const auto found = locked_.find(base);
            program_.symbols[copy].overrides = found == locked_.end() ? no_symbol : found->second;
        }
        for (const auto& [original, copy] : locked_) retarget(copy);
        for (const auto id : synchronized) wrap(id);
        open_guards(synchronized);
    }

private:
    std::optional<std::size_t> body_index(SymbolId owner) const {
        for (std::size_t i = 0; i < program_.bodies.size(); ++i) {
            if (program_.bodies[i].owner == owner) return i;
        }
        return std::nullopt;
    }

    // A call on the receiver from a locked body may reach `target`: the
    // locked copy of it, and of every method its call can dispatch to, is
    // needed when one of them takes the guard or is a hidden helper.
    bool needs_locked_copy(SymbolId target) const {
        if (!valid(program_, target)) return false;
        const auto& item = program_.symbols[target];
        if (item.kind != SymbolKind::Function || !valid(program_, item.owner) ||
            program_.symbols[item.owner].kind != SymbolKind::Class || constructor_or_destructor(item) ||
            template_symbol(program_, target))
            return false;
        if (synchronized_method(item) || (!item.exported && item.has_body)) return true;
        if (const auto found = overriders_.find(target); found != overriders_.end()) {
            for (const auto id : found->second) {
                if (synchronized_method(program_.symbols[id])) return true;
            }
        }
        return false;
    }

    // Requests a locked copy of `function` and of every method in its
    // override family, so that dispatch among the copies mirrors dispatch
    // among the originals.
    void request(SymbolId function) {
        std::vector<SymbolId> family{function};
        std::set<SymbolId> seen;
        for (auto base = program_.symbols[function].overrides;
             valid(program_, base) && program_.symbols[base].kind == SymbolKind::Function &&
             valid(program_, program_.symbols[base].owner) &&
             program_.symbols[program_.symbols[base].owner].kind == SymbolKind::Class && seen.insert(base).second;
             base = program_.symbols[base].overrides) {
            family.push_back(base);
        }
        const auto count = family.size();
        for (std::size_t i = 0; i < count; ++i) {
            if (const auto found = overriders_.find(family[i]); found != overriders_.end()) {
                family.insert(family.end(), found->second.begin(), found->second.end());
            }
        }
        for (const auto id : family) {
            if (requested_.insert(id).second) pending_.push_back(id);
        }
    }

    void clone(SymbolId original) {
        const auto index = body_index(original);
        const auto copy = program_.symbols.size();
        {
            auto item = program_.symbols[original];
            item.name = "$locked." + item.name;
            item.exported = false;
            item.attributes.clear();
            item.overload_group = no_symbol;
            item.overrides = no_symbol;
            item.has_body = item.has_body && index.has_value();
            item.parameters.clear();
            program_.symbols.push_back(std::move(item));
        }
        locked_[original] = copy;
        std::map<SymbolId, SymbolId> remap;
        for (SymbolId id = 0; id < copy; ++id) {
            const auto& item = program_.symbols[id];
            if (item.owner != original || (item.kind != SymbolKind::Parameter && item.kind != SymbolKind::Local)) {
                continue;
            }
            auto local = item;
            local.owner = copy;
            remap[id] = program_.symbols.size();
            program_.symbols.push_back(std::move(local));
        }
        for (const auto parameter : program_.symbols[original].parameters) {
            program_.symbols[copy].parameters.push_back(remap.at(parameter));
        }
        if (!index) return;
        auto instructions = program_.bodies[*index].instructions;
        each_node(instructions, [&](IrNode& node) {
            if (const auto found = remap.find(node.symbol); found != remap.end()) node.symbol = found->second;
            node.inline_site = 0;
        });
        // Calls on the receiver that need a locked copy of their target.
        each_node(instructions, [&](IrNode& node) {
            if ((node.op == IrOp::VirtualCall || node.op == IrOp::Call) && !node.operands.empty() &&
                node.operands.front().op == IrOp::Receiver && needs_locked_copy(node.symbol)) {
                request(node.symbol);
            }
        });
        program_.bodies.push_back({copy, std::move(instructions)});
    }

    void retarget(SymbolId copy) {
        const auto index = body_index(copy);
        if (!index) return;
        each_node(program_.bodies[*index].instructions, [&](IrNode& node) {
            if ((node.op != IrOp::VirtualCall && node.op != IrOp::Call) || node.operands.empty() ||
                node.operands.front().op != IrOp::Receiver)
                return;
            if (const auto found = locked_.find(node.symbol); found != locked_.end()) node.symbol = found->second;
        });
    }

    IrNode receiver(SymbolId cls, const Span& span) const {
        auto node = make(IrOp::Receiver, program_.symbols[cls].type, span);
        node.symbol = cls;
        return node;
    }

    IrNode guard(const std::string& operation, SymbolId cls, const Span& span, IrType type) const {
        auto call = make(IrOp::SchedulerCall, std::move(type), span);
        call.text = operation;
        call.operands.push_back(receiver(cls, span));
        auto statement = make(IrOp::Discard, void_type(), span);
        statement.operands.push_back(std::move(call));
        return statement;
    }

    // The method takes the guard, runs its locked copy, and leaves the
    // guard. A trap ends the run, and cancellation is cooperative, so the
    // copy's return is the only way out of it.
    void wrap(SymbolId method) {
        const auto copy = locked_.at(method);
        const auto cls = program_.symbols[method].owner;
        const auto span = program_.symbols[method].span;
        const auto result = program_.symbols[method].type;
        auto call = make(IrOp::VirtualCall, result, span);
        call.symbol = copy;
        call.operands.push_back(receiver(cls, span));
        for (const auto parameter : program_.symbols[method].parameters) {
            auto load = make(IrOp::Load, program_.symbols[parameter].type, span);
            load.symbol = parameter;
            call.operands.push_back(std::move(load));
        }
        std::vector<IrNode> body;
        body.push_back(guard("guard.enter", cls, span, IrType{TypeKind::Bool, "bool", no_symbol, {}}));
        std::optional<SymbolId> value;
        if (result.kind == TypeKind::Void) {
            auto statement = make(IrOp::Discard, void_type(), span);
            statement.operands.push_back(std::move(call));
            body.push_back(std::move(statement));
        } else {
            IrSymbol local;
            local.kind = SymbolKind::Local;
            local.name = "$result";
            local.span = span;
            local.owner = method;
            local.type = result;
            value = program_.symbols.size();
            program_.symbols.push_back(std::move(local));
            auto initialize = make(IrOp::Initialize, void_type(), span);
            initialize.symbol = *value;
            initialize.operands.push_back(std::move(call));
            body.push_back(std::move(initialize));
        }
        body.push_back(guard("guard.exit", cls, span, void_type()));
        auto exit = make(IrOp::Return, void_type(), span);
        if (value) {
            auto load = make(IrOp::Load, result, span);
            load.symbol = *value;
            exit.operands.push_back(std::move(load));
        }
        body.push_back(std::move(exit));
        program_.bodies[*body_index(method)].instructions = std::move(body);
    }

    // The base-most class with synchronized methods opens the guard before
    // its field initializers, which run before its constructor's body and
    // before any subclass's.
    void open_guards(const std::vector<SymbolId>& synchronized) {
        std::set<SymbolId> owners;
        for (const auto id : synchronized) owners.insert(program_.symbols[id].owner);
        for (const auto cls : owners) {
            bool inherited = false;
            std::set<SymbolId> seen{cls};
            for (auto base = program_.symbols[cls].base; valid(program_, base) && seen.insert(base).second;
                 base = program_.symbols[base].base) {
                inherited = inherited || owners.contains(base);
            }
            if (inherited) continue;
            const auto span = program_.symbols[cls].span;
            auto call = make(IrOp::SchedulerCall, void_type(), span);
            call.text = "guard.open";
            call.operands.push_back(receiver(cls, span));
            auto value = make(IrOp::Boolean, IrType{TypeKind::Bool, "bool", no_symbol, {}}, span);
            value.text = "true";
            call.operands.push_back(std::move(value));
            auto statement = make(IrOp::Discard, void_type(), span);
            statement.operands.push_back(std::move(call));
            // First, so that a field initializer may call a synchronized method.
            if (const auto index = body_index(cls)) {
                auto& instructions = program_.bodies[*index].instructions;
                instructions.insert(instructions.begin(), std::move(statement));
            } else {
                program_.bodies.push_back({cls, {}});
                program_.bodies.back().instructions.push_back(std::move(statement));
            }
        }
    }

    IrProgram& program_;
    std::map<SymbolId, std::vector<SymbolId>> overriders_;
    std::map<SymbolId, SymbolId> locked_;
    std::set<SymbolId> requested_;
    std::deque<SymbolId> pending_;
};

} // namespace

void lower_synchronized(IrProgram& program) {
    if (!has_spawn(program)) return;
    SynchronizedLowering(program).run();
    // The calls the lowering added poll like every other call.
    insert_polls(program);
}

} // namespace neura

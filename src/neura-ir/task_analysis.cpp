#include "task_analysis.hpp"

#include "attributes.hpp"
#include "host_binding.hpp"
#include "kernel_signature.hpp"
#include "monitors.hpp"
#include "tags.hpp"

#include <algorithm>
#include <functional>

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

template <typename Visit>
void each_node(std::vector<IrNode>& nodes, const Visit& visit) {
    for (auto& node : nodes) {
        visit(node);
        each_node(node.operands, visit);
        each_node(node.body, visit);
        each_node(node.alternative, visit);
    }
}

bool polled(IrOp op) {
    return op == IrOp::Loop || op == IrOp::Call || op == IrOp::VirtualCall || op == IrOp::CallClosure;
}

std::string display(const IrType& type) {
    if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
        return (type.arguments.empty() ? std::string("?") : display(type.arguments.front())) +
               (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    if (type.kind == TypeKind::Tuple) {
        std::string text = "[";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            text += (i == 0 ? "" : ", ") + display(type.arguments[i]);
        }
        return text + "]";
    }
    return type.name.empty() ? std::string("value") : type.name;
}

} // namespace

bool has_spawn(const IrProgram& program) {
    bool found = false;
    for (const auto& body : program.bodies) {
        each_node(body.instructions, [&](const IrNode& node) { found = found || node.op == IrOp::Spawn; });
        if (found) return true;
    }
    return false;
}

bool uses_tasks(const IrProgram& program) {
    bool found = false;
    for (const auto& body : program.bodies) {
        each_node(body.instructions, [&](const IrNode& node) {
            found = found || node.op == IrOp::Spawn || node.op == IrOp::TaskScope || node.op == IrOp::SchedulerCall;
        });
        if (found) return true;
    }
    return false;
}

void insert_polls(IrProgram& program) {
    // The flag is a function of the whole program: a module compiled alone
    // carries none, and gains them when it is linked into a program that
    // spawns (Concurrency.md section 10.2).
    const bool concurrent = has_spawn(program);
    for (auto& body : program.bodies) {
        each_node(body.instructions, [&](IrNode& node) { node.poll = concurrent && polled(node.op); });
    }
}

TaskAnalysis::TaskAnalysis(const IrProgram& program) : program_(program), dispatch_(program) {
    for (const auto& body : program.bodies) bodies_.emplace(body.owner, &body);
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind == SymbolKind::Field && valid(item.owner)) fields_[item.owner].push_back(id);
        if (item.kind == SymbolKind::Variant && valid(item.owner)) variants_[item.owner].push_back(id);
        if (item.kind == SymbolKind::Class && valid(item.base)) derived_[item.base].push_back(id);
        if (item.kind == SymbolKind::Function && item.name == "deinit" && valid(item.owner)) {
            destructors_[item.owner].push_back(id);
        }
        if (item.kind == SymbolKind::Function) {
            // Every function this one overrides, directly or through a chain.
            std::set<SymbolId> seen;
            for (auto base = item.overrides; valid(base) && seen.insert(base).second; base = symbol(base).overrides) {
                overriders_[base].push_back(id);
            }
        }
    }
    find_created_closures();
    find_stray_accesses();
    summarize();
}

SymbolId TaskAnalysis::closure_of(SymbolId function) const {
    if (!valid(function) || symbol(function).kind != SymbolKind::Function) return no_symbol;
    const auto owner = symbol(function).owner;
    return capture_layout(owner) ? owner : no_symbol;
}

// Execution starts in the program's own code and in initializers. From
// there, a body can run what it calls, and a closure's body can run once code
// that can run has allocated the closure. Library functions that nothing
// reaches allocate nothing.
void TaskAnalysis::find_created_closures() {
    std::set<SymbolId> reached;
    std::vector<SymbolId> pending;
    const auto reach = [&](SymbolId owner) {
        if (valid(owner) && reached.insert(owner).second) pending.push_back(owner);
    };
    for (const auto& body : program_.bodies) {
        if (!valid(body.owner)) continue;
        // Any of the program's own functions may be an entry, and an
        // initializer runs without being called.
        if (!library_code(body.owner) || symbol(body.owner).kind != SymbolKind::Function) reach(body.owner);
    }
    while (!pending.empty()) {
        const auto owner = pending.back();
        pending.pop_back();
        const auto body = bodies_.find(owner);
        if (body == bodies_.end()) continue;
        each_node(body->second->instructions, [&](const IrNode& node) {
            if (node.op == IrOp::Allocate && valid(node.symbol) && capture_layout(symbol(node.symbol).owner)) {
                // The closure exists from here on; any call through its type
                // may run its body.
                const auto layout = symbol(node.symbol).owner;
                if (created_.insert(layout).second) {
                    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
                        if (closure_of(id) == layout) reach(id);
                    }
                }
            }
            std::vector<std::pair<SymbolId, Span>> out;
            callees(node, out);
            for (const auto& [callee, span] : out) {
                // A closure's body is reached through its creation above.
                if (closure_of(callee) == no_symbol) reach(callee);
            }
        });
    }
}

bool TaskAnalysis::signature_layout(SymbolId cls) const {
    return valid(cls) && symbol(cls).kind == SymbolKind::Class && symbol(cls).name.starts_with("$signature");
}

bool TaskAnalysis::capture_layout(SymbolId cls) const {
    return valid(cls) && symbol(cls).kind == SymbolKind::Class && symbol(cls).name.starts_with("$closure");
}

// The handles of Std.Concurrent are identities of scheduler state. Task and
// Scope are scope-bound instead (section 4.3).
bool TaskAnalysis::concurrent_handle(SymbolId cls) const {
    if (!valid(cls) || symbol(cls).kind != SymbolKind::Class) return false;
    const auto file = source_file(program_, cls);
    if (file == no_symbol) return false;
    const auto& path = symbol(file).library_path;
    return path.starts_with("std/concurrent/") && path != "std/concurrent/task.nra" &&
           path != "std/concurrent/scope.nra";
}

std::vector<SymbolId> TaskAnalysis::all_fields(SymbolId cls) const {
    std::vector<SymbolId> result;
    std::set<SymbolId> seen;
    for (auto scope = cls; valid(scope) && seen.insert(scope).second; scope = symbol(scope).base) {
        if (const auto found = fields_.find(scope); found != fields_.end()) {
            result.insert(result.end(), found->second.begin(), found->second.end());
        }
    }
    return result;
}

bool TaskAnalysis::fields_transferable(SymbolId cls) const {
    const auto fields = all_fields(cls);
    return std::ranges::all_of(fields, [&](SymbolId field) { return transferable(symbol(field).type); });
}

// Section 8.1: the state of a monitor is reached only by its own methods,
// through their receiver, so holding the receiver's guard covers it. A field
// read or written through any other object, and a call of a hidden method on
// one, is a stray access.
void TaskAnalysis::find_stray_accesses() {
    const auto declared_monitor = [&](SymbolId cls) {
        return valid(cls) && symbol(cls).kind == SymbolKind::Class &&
               has_attribute(symbol(cls), AttributeId::ClassShareable);
    };
    const auto describe = [&](SymbolId owner) {
        if (!valid(owner)) return std::string("code");
        if (symbol(owner).name == "$invoke") return std::string("a function value");
        // The body of a synchronized method runs as its locked copy.
        auto name = symbol(owner).name;
        if (name.starts_with("$locked.")) name = name.substr(8);
        return "'" + name + "'";
    };
    for (const auto& body : program_.bodies) {
        const auto owner = body.owner;
        // A method or a field initializer, whose receiver is the instance.
        const bool method = valid(owner) && (symbol(owner).kind == SymbolKind::Class ||
                                             (symbol(owner).kind == SymbolKind::Function && valid(symbol(owner).owner) &&
                                              symbol(symbol(owner).owner).kind == SymbolKind::Class &&
                                              !capture_layout(symbol(owner).owner)));
        each_node(body.instructions, [&](const IrNode& node) {
            const bool through_receiver = method && !node.operands.empty() && node.operands.front().op == IrOp::Receiver;
            if (node.op == IrOp::Field && valid(node.symbol) && symbol(node.symbol).kind == SymbolKind::Field) {
                const auto cls = symbol(node.symbol).owner;
                if (!declared_monitor(cls) || through_receiver || stray_access_.contains(cls)) return;
                stray_access_.emplace(cls, Problem{describe(owner) + " uses the field '" + symbol(node.symbol).name +
                                                       "' of an instance other than its receiver, without that "
                                                       "instance's guard",
                                                   node.span});
            }
            if ((node.op == IrOp::Call || node.op == IrOp::VirtualCall) && valid(node.symbol)) {
                const auto& target = symbol(node.symbol);
                const auto cls = target.owner;
                if (target.kind != SymbolKind::Function || !declared_monitor(cls) || target.exported ||
                    synchronized_method(target) || target.name == "init" || target.name == "deinit" ||
                    through_receiver || stray_access_.contains(cls))
                    return;
                stray_access_.emplace(cls, Problem{describe(owner) + " calls the hidden method '" + target.name +
                                                       "' of an instance other than its receiver, without that "
                                                       "instance's guard",
                                                   node.span});
            }
        });
    }
}

std::optional<TaskAnalysis::Problem> TaskAnalysis::shareable_problem(SymbolId cls) const {
    if (!valid(cls) || symbol(cls).kind != SymbolKind::Class) return Problem{"it is not a class", {}};
    const auto& item = symbol(cls);
    if (const auto marked = not_thread_safe_class(cls); marked != no_symbol) {
        return Problem{marked == cls ? "it is marked #NotThreadSafe"
                                     : "it extends '" + symbol(marked).name + "', which is marked #NotThreadSafe",
                       tag_span(symbol(marked), TagId::NotThreadSafe).value_or(symbol(marked).span)};
    }
    if (!has_attribute(item, AttributeId::ClassShareable)) {
        if (const auto problem = monitor_problem(program_, cls)) return Problem{problem->text, problem->span};
        return Problem{"it is not a monitor", item.span};
    }
    for (const auto field : all_fields(cls)) {
        if (transferable(symbol(field).type)) continue;
        return Problem{"its field '" + symbol(field).name + "' has the type " + display(symbol(field).type) +
                           ", which is not transferable",
                       symbol(field).span};
    }
    std::set<SymbolId> seen;
    for (auto scope = cls; valid(scope) && seen.insert(scope).second; scope = symbol(scope).base) {
        if (const auto found = stray_access_.find(scope); found != stray_access_.end()) return found->second;
    }
    if (const auto found = derived_.find(cls); found != derived_.end()) {
        for (const auto derived : found->second) {
            if (const auto problem = shareable_problem(derived)) {
                return Problem{"its subclass '" + symbol(derived).name + "' is not shareable: " + problem->text,
                               problem->span};
            }
        }
    }
    return std::nullopt;
}

bool TaskAnalysis::class_transferable(SymbolId cls, bool item) const {
    if (!valid(cls) || symbol(cls).kind != SymbolKind::Class) return false;
    auto& cache = class_cache_[item ? 1 : 0];
    if (const auto known = cache.find(cls); known != cache.end()) return known->second;
    // A type that reaches itself is transferable when its other parts are.
    cache[cls] = true;
    const auto parts = [&](SymbolId layout) {
        return std::ranges::all_of(all_fields(layout), [&](SymbolId field) {
            return transferable_in(symbol(field).type, item);
        });
    };
    bool result = false;
    if (signature_layout(cls)) {
        // Section 5.2's union rule, applied to values: a function value is
        // transferable when every closure of its type that the program can
        // create is.
        result = true;
        if (const auto found = derived_.find(cls); found != derived_.end()) {
            result = std::ranges::all_of(found->second, [&](SymbolId layout) {
                return !created_.contains(layout) || parts(layout);
            });
        }
    } else if (capture_layout(cls) || concurrent_handle(cls)) {
        result = parts(cls);
    } else if (not_thread_safe_class(cls) != no_symbol) {
        result = false;
    } else if (has_tag(symbol(cls), TagId::Immutable)) {
        // No field changes after construction, so a task that receives the
        // instance shares nothing that can change, and an item receives a
        // copy. A specialization of a generic class is transferable when its
        // fields are. Every subclass is #Immutable too.
        result = parts(cls);
        if (const auto found = derived_.find(cls); result && found != derived_.end()) {
            result = std::ranges::all_of(found->second, [&](SymbolId derived) {
                return class_transferable(derived, item);
            });
        }
    } else if (!item && has_attribute(symbol(cls), AttributeId::ClassShareable)) {
        // A monitor is shared, not copied: an isolated item, which has its
        // own heap and no scheduler, cannot receive one.
        result = !shareable_problem(cls);
    }
    cache[cls] = result;
    return result;
}

bool TaskAnalysis::transferable_in(const IrType& type, bool item) const {
    switch (type.kind) {
    case TypeKind::Bool:
    case TypeKind::Integer:
    case TypeKind::Float:
    case TypeKind::String:
        return true;
    case TypeKind::Tuple:
        return std::ranges::all_of(type.arguments, [&](const IrType& part) { return transferable_in(part, item); });
    case TypeKind::Enum: {
        if (!valid(type.symbol)) return false;
        auto& cache = enum_cache_[item ? 1 : 0];
        if (const auto known = cache.find(type.symbol); known != cache.end()) return known->second;
        cache[type.symbol] = true;
        bool result = true;
        if (const auto found = variants_.find(type.symbol); found != variants_.end()) {
            for (const auto variant : found->second) {
                result = result && std::ranges::all_of(symbol(variant).payload_types, [&](const IrType& payload) {
                    return transferable_in(payload, item);
                });
            }
        }
        cache[type.symbol] = result;
        return result;
    }
    case TypeKind::Object:
        return class_transferable(type.symbol, item);
    default:
        // Arrays and lists can be assigned through; a module, a type
        // parameter, and an unspecialized callable are not values here.
        return false;
    }
}

void TaskAnalysis::explain_class(Diagnostic& error, const IrType& type) const {
    if (type.kind != TypeKind::Object || !valid(type.symbol)) return;
    const auto cls = type.symbol;
    const auto& item = symbol(cls);
    if (item.kind != SymbolKind::Class) return;
    if (const auto marked = not_thread_safe_class(cls); marked != no_symbol) {
        error.label(tag_span(symbol(marked), TagId::NotThreadSafe).value_or(symbol(marked).span),
                    "'" + symbol(marked).name + "' is marked #NotThreadSafe here");
        error.note("the instances of a #NotThreadSafe class, and of a class that extends one, stay in the task that "
                   "holds them");
        return;
    }
    if (has_tag(item, TagId::Immutable)) {
        for (const auto field : all_fields(cls)) {
            if (transferable(symbol(field).type)) continue;
            error.label(symbol(field).span, "'" + item.name + "' holds a value of type " +
                                                display(symbol(field).type) + " here");
            return;
        }
        return;
    }
    if (!has_synchronized_methods(program_, cls)) {
        // A class of the program's own, which its author could make shareable.
        if (!library_code(cls) && !signature_layout(cls) && !capture_layout(cls) && !concurrent_handle(cls)) {
            error.note("tasks may share an instance of a class marked #Immutable, or of a monitor, whose exported "
                       "methods are #Synchronized");
        }
        return;
    }
    if (const auto problem = shareable_problem(cls)) {
        error.label(problem->span, "'" + item.name + "' is not shareable: " + problem->text);
        error.note("tasks may share an instance of a monitor: a class whose exported methods are all #Synchronized, "
                   "whose fields are transferable, and whose state only its own methods reach through their "
                   "receiver");
    }
}

std::optional<TaskAnalysis::Reason> TaskAnalysis::direct(const IrNode& node) const {
    if (node.op == IrOp::Load && valid(node.symbol) && symbol(node.symbol).kind == SymbolKind::Global) {
        const auto& global = symbol(node.symbol);
        if (!global.constant) {
            return Reason{"uses the reassignable binding '" + global.name + "'", node.span, no_symbol};
        }
        if (!transferable(global.type)) {
            return Reason{"uses the binding '" + global.name + "', whose type " + display(global.type) +
                              " is not transferable", node.span, no_symbol};
        }
    }
    if (node.op == IrOp::ExternalCall && valid(node.symbol) && symbol(node.symbol).host_binding &&
        symbol(node.symbol).host_binding->name.starts_with("neura.ffi.")) {
        return Reason{"issues the native request " + symbol(node.symbol).host_binding->name, node.span, no_symbol};
    }
    return std::nullopt;
}

// What only a task of the run may do, and an isolated item may not.
std::optional<TaskAnalysis::Reason> TaskAnalysis::isolated_reason(const IrNode& node) const {
    switch (node.op) {
    case IrOp::ExternalCall:
        // A kernel runs inside the NVM like a heap operation: it reads its
        // arguments and writes only the storage it is given, so a task or an
        // isolated item may call it (docs/runtime/Kernels.md).
        if (valid(node.symbol) && kernel_bound(symbol(node.symbol))) return std::nullopt;
        return Reason{"issues a host request", node.span, no_symbol};
    case IrOp::Spawn:
    case IrOp::TaskScope:
        return Reason{"starts or scopes tasks", node.span, no_symbol};
    case IrOp::SchedulerCall:
        if (node.text == "transfer.check") return std::nullopt;
        return Reason{"uses the scheduler operation '" + node.text + "'", node.span, no_symbol};
    case IrOp::ClassHash:
    case IrOp::ClassText:
        return Reason{"uses an object's identity", node.span, no_symbol};
    default:
        return std::nullopt;
    }
}

void TaskAnalysis::callees(const IrNode& node, std::vector<std::pair<SymbolId, Span>>& out) const {
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
    case IrOp::VirtualCall: {
        // The union over every implementation the call can reach: every
        // override, and every implementation of an interface method, which
        // needs no `.override` (dispatch.hpp).
        std::set<SymbolId> reached{node.symbol};
        if (const auto found = overriders_.find(node.symbol); found != overriders_.end()) {
            reached.insert(found->second.begin(), found->second.end());
        }
        for (const auto id : dispatch_.of(node)) reached.insert(id);
        for (const auto id : reached) {
            // Only a closure that some reachable code creates can be what
            // the call runs.
            if (const auto layout = closure_of(id); layout != no_symbol && !created_.contains(layout)) continue;
            add(id);
        }
        break;
    }
    case IrOp::ValueEquals:
    case IrOp::ValueHash:
    case IrOp::ValueText:
        // The members that equality, hashing, and text of the value run.
        if (!node.operands.empty()) {
            bool deferred = false;
            for (const auto id : dispatch_.value_members(node.operands.front().type, node.op, deferred)) add(id);
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
    case IrOp::Destroy: {
        // Cleanup of the runtime class: the named class, its bases, and
        // every class that derives from it.
        std::vector<SymbolId> pending{node.symbol};
        std::set<SymbolId> seen;
        for (auto base = node.symbol; valid(base) && seen.insert(base).second; base = symbol(base).base) {
            pending.push_back(base);
        }
        for (std::size_t i = 0; i < pending.size(); ++i) {
            const auto cls = pending[i];
            if (!valid(cls)) continue;
            if (const auto found = destructors_.find(cls); found != destructors_.end()) {
                for (const auto id : found->second) add(id);
            }
            if (const auto found = derived_.find(cls); found != derived_.end()) {
                for (const auto id : found->second) {
                    if (seen.insert(id).second) pending.push_back(id);
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

void TaskAnalysis::summarize() {
    // Edges from each body's owner to what it can run, and the direct reason
    // of each owner. A class owner stands for its field initializers.
    std::map<SymbolId, std::vector<std::pair<SymbolId, Span>>> edges;
    std::map<SymbolId, std::vector<SymbolId>> callers;
    // #NotThreadSafe on a function or method: the author declines to promise
    // task safety for it. On a class it keeps the instances in one task
    // (class_transferable), and a task may use an instance of its own.
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind != SymbolKind::Function) continue;
        if (const auto tag = tag_span(item, TagId::NotThreadSafe)) {
            unsafe_.emplace(id, Reason{"is marked #NotThreadSafe", *tag, no_symbol});
        }
    }
    for (const auto& body : program_.bodies) {
        auto& out = edges[body.owner];
        each_node(body.instructions, [&](const IrNode& node) {
            if (!unsafe_.contains(body.owner)) {
                if (auto reason = direct(node)) unsafe_.emplace(body.owner, std::move(*reason));
            }
            if (!impure_.contains(body.owner)) {
                if (auto reason = isolated_reason(node)) impure_.emplace(body.owner, std::move(*reason));
            }
            callees(node, out);
        });
        for (const auto& [callee, span] : out) {
            callers[callee].push_back(body.owner);
            callers_[callee].emplace_back(body.owner, span);
        }
    }
    // A function that calls one with a reason has that reason too.
    const auto propagate = [&](std::map<SymbolId, Reason>& reasons) {
        std::vector<SymbolId> pending;
        for (const auto& [owner, reason] : reasons) pending.push_back(owner);
        while (!pending.empty()) {
            const auto callee = pending.back();
            pending.pop_back();
            const auto found = callers.find(callee);
            if (found == callers.end()) continue;
            for (const auto caller : found->second) {
                if (reasons.contains(caller)) continue;
                const auto& out = edges[caller];
                const auto edge = std::ranges::find(out, callee, &std::pair<SymbolId, Span>::first);
                reasons.emplace(caller, Reason{{}, edge->second, callee});
                pending.push_back(caller);
            }
        }
    };
    propagate(unsafe_);
    propagate(impure_);
}

SymbolId TaskAnalysis::not_thread_safe_class(SymbolId cls) const {
    std::set<SymbolId> seen;
    for (auto scope = cls; valid(scope) && seen.insert(scope).second; scope = symbol(scope).base) {
        if (symbol(scope).kind == SymbolKind::Class && has_tag(symbol(scope), TagId::NotThreadSafe)) return scope;
    }
    return no_symbol;
}

bool TaskAnalysis::library_code(SymbolId owner) const {
    const auto file = source_file(program_, owner);
    return file != no_symbol && !symbol(file).library_path.empty();
}

// Where a program's own code reaches `owner`: the nearest call outside the
// standard library, so that a rule a library operation enforces is reported
// where the program used it. `fallback` when no such call exists.
Span TaskAnalysis::user_site(SymbolId owner, const Span& fallback) const {
    if (!library_code(owner)) return fallback;
    std::vector<SymbolId> pending{owner};
    std::set<SymbolId> seen{owner};
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto found = callers_.find(pending[i]);
        if (found == callers_.end()) continue;
        for (const auto& [caller, span] : found->second) {
            if (!library_code(caller)) return span;
            if (seen.insert(caller).second) pending.push_back(caller);
        }
    }
    return fallback;
}

std::optional<TaskAnalysis::Unsafe> TaskAnalysis::unsafe(SymbolId function) const {
    return chain(unsafe_, function);
}

std::optional<TaskAnalysis::Unsafe> TaskAnalysis::not_isolated(SymbolId function) const {
    return chain(impure_, function);
}

std::optional<TaskAnalysis::Unsafe> TaskAnalysis::chain(const std::map<SymbolId, Reason>& reasons,
                                                        SymbolId function) const {
    const auto& unsafe_ = reasons;
    auto found = unsafe_.find(function);
    if (found == unsafe_.end()) return std::nullopt;
    Unsafe result;
    std::set<SymbolId> seen{function};
    while (found->second.callee != no_symbol) {
        result.calls.emplace_back(found->second.callee, found->second.span);
        const auto next = unsafe_.find(found->second.callee);
        if (next == unsafe_.end() || !seen.insert(found->second.callee).second) break;
        found = next;
    }
    result.operation = found->second.operation;
    result.span = found->second.span;
    return result;
}

void TaskAnalysis::check() const {
    // The first use of each kind, for decision C15 below.
    const IrNode* first_spawn = nullptr;
    const IrNode* first_operation = nullptr;
    SymbolId first_operation_owner = no_symbol;
    // For a function type that is not transferable, the capture that makes it
    // so: under the union rule the reason is a closure somewhere else in the
    // program, which the type alone does not show. Labelled at the capture.
    const auto capture_note = [&](Diagnostic& error, const IrType& type) {
        if (type.kind != TypeKind::Object || !valid(type.symbol)) return;
        std::vector<SymbolId> layouts;
        if (signature_layout(type.symbol)) {
            if (const auto found = derived_.find(type.symbol); found != derived_.end()) layouts = found->second;
        } else if (capture_layout(type.symbol)) {
            layouts.push_back(type.symbol);
        }
        for (const auto layout : layouts) {
            if (!created_.contains(layout)) continue;
            for (const auto field : all_fields(layout)) {
                if (transferable(symbol(field).type)) continue;
                error.label(symbol(field).span, "a function value of this type captures a value of type " +
                                                    display(symbol(field).type) + " here");
                return;
            }
        }
    };
    // A check that fails inside a library helper is reported where the
    // user's code calls into the library, and labels where the task starts.
    const auto started_by_library = [&](Diagnostic& error, SymbolId owner, const Span& span) {
        if (library_code(owner)) error.label(span, "the library starts the task here");
    };
    // A task-safety error: the rule's first clause and, in the message, the
    // function that breaks it and what it does; each call on the way from the
    // checked call is a label, as is the operation itself.
    const auto unsafe_error = [&](const Span& at, std::string rule, const Unsafe& reason) {
        std::vector<std::pair<SymbolId, Span>> hops;
        for (const auto& [callee, span] : reason.calls) {
            if (symbol(callee).name == "$invoke" || symbol(callee).kind != SymbolKind::Function) continue;
            hops.emplace_back(callee, span);
        }
        auto text = std::move(rule) + ": ";
        text += hops.empty() ? "it " + reason.operation
                             : "it reaches '" + symbol(hops.back().first).name + "', which " + reason.operation;
        auto error = Diagnostic(at, std::move(text)).with_code("E0601").label("this call must be task-safe");
        for (const auto& [callee, span] : hops) error.label(span, "calls '" + symbol(callee).name + "'");
        error.label(reason.span, reason.operation + " here");
        error.note("`neura check --task-safety` reports for every function whether it is task-safe, and why not");
        return error;
    };
    for (const auto& body : program_.bodies) {
        each_node(body.instructions, [&](const IrNode& node) {
            if ((node.op == IrOp::TaskScope || (node.op == IrOp::SchedulerCall && node.text != "transfer.check")) &&
                !first_operation) {
                first_operation = &node;
                first_operation_owner = body.owner;
            }
            if (node.op == IrOp::SchedulerCall) {
                // What a channel carries is given to another task (5.1).
                const auto require = [&](const IrType& type, const char* role, bool item = false) {
                    if (transferable_in(type, item)) return;
                    auto error = Diagnostic(user_site(body.owner, node.span),
                                            "a value of type " + display(type) + " is not transferable, so it cannot be " +
                                            role).with_code("E0602").label("not transferable");
                    capture_note(error, type);
                    explain_class(error, type);
                    if (item && transferable(type)) {
                        error.note("tasks share an instance of a monitor, but a parallel item runs in a heap of its "
                                   "own and receives copies, so a monitor cannot enter one");
                    }
                    throw error;
                };
                if (node.text == "transfer.check" && node.operands.size() == 1) {
                    require(node.operands.front().type, "used where a transferable value is required");
                } else if (node.text == "parallel.start" && !node.operands.empty() &&
                           node.operands.front().op == IrOp::Allocate && valid(node.operands.front().symbol)) {
                    // An isolated item: its captures enter another heap, its
                    // result returns from one, and it may touch nothing else.
                    const auto layout = symbol(node.operands.front().symbol).owner;
                    for (const auto field : all_fields(layout)) require(symbol(field).type, "given to a parallel item", true);
                    SymbolId invoke = no_symbol;
                    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
                        const auto& item = symbol(id);
                        if (item.kind == SymbolKind::Function && item.owner == layout && item.name == "$invoke") invoke = id;
                    }
                    if (invoke == no_symbol) throw Diagnostic(node.span, "an isolated function value has no body")
                        .with_code("E0899");
                    require(symbol(invoke).type, "the result of a parallel item", true);
                    if (const auto reason = chain(unsafe_, invoke)) {
                        throw unsafe_error(user_site(body.owner, node.span),
                                           "the worker of a parallel map is not task-safe", *reason);
                    }
                    if (const auto reason = chain(impure_, invoke)) {
                        throw unsafe_error(user_site(body.owner, node.span),
                                           "the worker of a parallel map must compute in isolation", *reason);
                    }
                } else if (node.text == "channel.open" && !node.type_arguments.empty()) {
                    require(node.type_arguments.front(), "a channel's element type");
                } else if ((node.text == "channel.send" || node.text == "channel.try_send") && node.operands.size() == 2) {
                    require(node.operands[1].type, "sent on a channel");
                } else if (node.text.starts_with("select")) {
                    for (std::size_t i = 0; i + 1 < node.operands.size();) {
                        const bool send = node.operands[i].text == "1";
                        if (send && i + 2 < node.operands.size()) require(node.operands[i + 2].type, "sent on a channel");
                        i += send ? 3 : 2;
                    }
                }
                return;
            }
            if (node.op != IrOp::Spawn) return;
            if (!first_spawn) first_spawn = &node;
            if (node.operands.size() != 1 || node.operands.front().op != IrOp::Allocate ||
                !valid(node.operands.front().symbol)) {
                throw Diagnostic(node.span, "spawn must be checked after specialization").with_code("E0899");
            }
            const auto layout = symbol(node.operands.front().symbol).owner;
            // The captures are the callee value, a receiver, and the
            // arguments, each at the span of the expression it came from.
            for (const auto field : all_fields(layout)) {
                if (!transferable(symbol(field).type)) {
                    auto error = Diagnostic(user_site(body.owner, symbol(field).span),
                                            "a value of type " + display(symbol(field).type) +
                                            " is not transferable, so it cannot be given to a spawned task")
                        .with_code("E0602").label("not transferable");
                    capture_note(error, symbol(field).type);
                    explain_class(error, symbol(field).type);
                    started_by_library(error, body.owner, node.span);
                    throw error;
                }
            }
            SymbolId invoke = no_symbol;
            for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
                const auto& item = symbol(id);
                if (item.kind == SymbolKind::Function && item.owner == layout && item.name == "$invoke") invoke = id;
            }
            if (invoke == no_symbol) throw Diagnostic(node.span, "a spawned function value has no body")
                .with_code("E0899");
            if (const auto reason = unsafe(invoke)) {
                auto error = unsafe_error(user_site(body.owner, node.span), "the spawned call is not task-safe", *reason);
                started_by_library(error, body.owner, node.span);
                throw error;
            }
        });
    }
    if (!first_spawn && !first_operation) return;
    // C15: the first concurrent image profile has no native bindings. The
    // loader refuses any image with a scheduler operation beside them, so a
    // program that only opens a scope or a channel is held to the same rule
    // here, where the reason can be given.
    bool native = program_.native_library.has_value() || !program_.native_units.empty();
    for (const auto& item : program_.symbols) {
        native = native || (item.host_binding && item.host_binding->name.starts_with("neura.ffi."));
    }
    if (native && first_spawn) {
        throw Diagnostic(first_spawn->span,
                         "a program that spawns a task cannot use Std.DynamicForeign or import:c(...): "
                         "tasks and native calls cannot share an image yet").with_code("E0604");
    }
    if (native) {
        throw Diagnostic(user_site(first_operation_owner, first_operation->span),
                         "a program that uses a scope, a channel, or another scheduler operation cannot use "
                         "Std.DynamicForeign or import:c(...): tasks and native calls cannot share an image yet")
            .with_code("E0604");
    }
}

void TaskAnalysis::check_tags() const {
    // A generic declaration is checked in each of its specializations.
    const auto in_template = [&](SymbolId id) {
        std::set<SymbolId> seen;
        for (auto scope = id; valid(scope) && seen.insert(scope).second; scope = symbol(scope).owner) {
            if (!symbol(scope).generics.empty()) return true;
        }
        return false;
    };
    const auto not_task_safe = [&](const Span& at, std::string subject, const Unsafe& reason) {
        std::vector<std::pair<SymbolId, Span>> hops;
        for (const auto& [callee, span] : reason.calls) {
            if (symbol(callee).name == "$invoke" || symbol(callee).kind != SymbolKind::Function) continue;
            hops.emplace_back(callee, span);
        }
        auto text = std::move(subject) + " is not task-safe: ";
        text += hops.empty() ? "it " + reason.operation
                             : "it reaches '" + symbol(hops.back().first).name + "', which " + reason.operation;
        auto error = Diagnostic(at, std::move(text)).with_code("E0608").label("#ThreadSafe promises task safety");
        for (const auto& [callee, span] : hops) error.label(span, "calls '" + symbol(callee).name + "'");
        error.label(reason.span, reason.operation + " here");
        error.note("`neura check --task-safety` reports for every function whether it is task-safe, and why not");
        error.help("remove #ThreadSafe, or remove the use of shared state");
        return error;
    };
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        const auto tag = tag_span(item, TagId::ThreadSafe);
        if (!tag || in_template(id)) continue;
        if (item.kind == SymbolKind::Function) {
            if (const auto reason = unsafe(id)) {
                throw not_task_safe(*tag, "#ThreadSafe function '" + item.name + "'", *reason);
            }
            continue;
        }
        if (item.kind != SymbolKind::Class) continue;
        if (!transferable_class(id)) {
            auto error = Diagnostic(*tag, "#ThreadSafe class '" + item.name + "' cannot be shared between tasks")
                .with_code("E0609").label("#ThreadSafe promises that tasks may share its instances");
            if (has_tag(item, TagId::Immutable)) {
                for (const auto field : all_fields(id)) {
                    if (transferable(symbol(field).type)) continue;
                    error.label(symbol(field).span, "this field has the type " + display(symbol(field).type) +
                                                        ", which is not transferable");
                    break;
                }
            } else if (const auto problem = shareable_problem(id)) {
                error.label(problem->span, problem->text);
            }
            error.note("tasks may share an instance of an #Immutable class, or of a monitor: a class whose exported "
                       "methods are all #Synchronized, whose fields are transferable, and whose state only its own "
                       "methods reach through their receiver");
            error.help("mark the class #Immutable or #Synchronized, or remove #ThreadSafe");
            throw error;
        }
        // Every exported method, on a shared instance, from any task.
        for (SymbolId member = 0; member < program_.symbols.size(); ++member) {
            const auto& function = symbol(member);
            if (function.kind != SymbolKind::Function || function.owner != id || !function.exported) continue;
            if (const auto reason = unsafe(member)) {
                throw not_task_safe(*tag, "method '" + function.name + "' of #ThreadSafe class '" + item.name + "'",
                                    *reason);
            }
        }
    }
}

} // namespace neura

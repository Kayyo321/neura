#include "task_analysis.hpp"

#include "host_binding.hpp"

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

TaskAnalysis::TaskAnalysis(const IrProgram& program) : program_(program) {
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

bool TaskAnalysis::transferable_class(SymbolId cls) const {
    if (!valid(cls) || symbol(cls).kind != SymbolKind::Class) return false;
    if (const auto known = class_cache_.find(cls); known != class_cache_.end()) return known->second;
    // A type that reaches itself is transferable when its other parts are.
    class_cache_[cls] = true;
    bool result = false;
    if (signature_layout(cls)) {
        // Section 5.2's union rule, applied to values: a function value is
        // transferable when every closure of its type that the program can
        // create is.
        result = true;
        if (const auto found = derived_.find(cls); found != derived_.end()) {
            result = std::ranges::all_of(found->second, [&](SymbolId layout) {
                return !created_.contains(layout) || fields_transferable(layout);
            });
        }
    } else if (capture_layout(cls) || concurrent_handle(cls)) {
        result = fields_transferable(cls);
    }
    class_cache_[cls] = result;
    return result;
}

bool TaskAnalysis::transferable(const IrType& type) const {
    switch (type.kind) {
    case TypeKind::Bool:
    case TypeKind::Integer:
    case TypeKind::Float:
    case TypeKind::String:
        return true;
    case TypeKind::Tuple:
        return std::ranges::all_of(type.arguments, [&](const IrType& part) { return transferable(part); });
    case TypeKind::Enum: {
        if (!valid(type.symbol)) return false;
        if (const auto known = enum_cache_.find(type.symbol); known != enum_cache_.end()) return known->second;
        enum_cache_[type.symbol] = true;
        bool result = true;
        if (const auto found = variants_.find(type.symbol); found != variants_.end()) {
            for (const auto variant : found->second) {
                result = result && std::ranges::all_of(symbol(variant).payload_types, [&](const IrType& payload) {
                    return transferable(payload);
                });
            }
        }
        enum_cache_[type.symbol] = result;
        return result;
    }
    case TypeKind::Object:
        return transferable_class(type.symbol);
    default:
        // Arrays and lists can be assigned through; a module, a type
        // parameter, and an unspecialized callable are not values here.
        return false;
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
    case IrOp::VirtualCall:
        // The union over every implementation the call can reach.
        add(node.symbol);
        if (const auto found = overriders_.find(node.symbol); found != overriders_.end()) {
            for (const auto id : found->second) {
                // Only a closure that some reachable code creates can be
                // what the call runs.
                if (const auto layout = closure_of(id); layout != no_symbol && !created_.contains(layout)) continue;
                add(id);
            }
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
                const auto require = [&](const IrType& type, const char* role) {
                    if (transferable(type)) return;
                    auto error = Diagnostic(user_site(body.owner, node.span),
                                            "a value of type " + display(type) + " is not transferable, so it cannot be " +
                                            role).with_code("E0602").label("not transferable");
                    capture_note(error, type);
                    throw error;
                };
                if (node.text == "transfer.check" && node.operands.size() == 1) {
                    require(node.operands.front().type, "used where a transferable value is required");
                } else if (node.text == "parallel.start" && !node.operands.empty() &&
                           node.operands.front().op == IrOp::Allocate && valid(node.operands.front().symbol)) {
                    // An isolated item: its captures enter another heap, its
                    // result returns from one, and it may touch nothing else.
                    const auto layout = symbol(node.operands.front().symbol).owner;
                    for (const auto field : all_fields(layout)) require(symbol(field).type, "given to a parallel item");
                    SymbolId invoke = no_symbol;
                    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
                        const auto& item = symbol(id);
                        if (item.kind == SymbolKind::Function && item.owner == layout && item.name == "$invoke") invoke = id;
                    }
                    if (invoke == no_symbol) throw Diagnostic(node.span, "an isolated function value has no body")
                        .with_code("E0899");
                    require(symbol(invoke).type, "the result of a parallel item");
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

} // namespace neura

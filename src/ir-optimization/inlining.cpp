// Inlining of direct calls (docs/IrOptimization.md, inline-calls).
#include "passes.hpp"

#include "effects.hpp"
#include "locals.hpp"

#include "neura-ir/control_flow.hpp"

#include <algorithm>
#include <map>
#include <optional>

namespace neura::optimization {
namespace {

const IrType void_type{TypeKind::Void, "void", no_symbol, {}};
const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};

// How a callee's returns sit in its body, which decides where a call of it
// can be replaced.
enum class Shape {
    // No return except one as the last statement of the body: the body can
    // stand anywhere the call's value is used.
    Tail,
    // Returns elsewhere, none inside a loop, and a bool or integer result:
    // the body runs once inside a loop that each return breaks out of.
    Early,
    // Returns inside loops, or early returns of a float: only a call that is
    // itself returned is inlined.
    Nested,
};

struct Callee {
    const IrBody* body;
    Shape shape;
    // The body names its module's receiver, for example to qualify a global.
    bool receiver;
};

bool contains_op(const std::vector<IrNode>& nodes, std::initializer_list<IrOp> ops) {
    return std::ranges::any_of(nodes, [&](const IrNode& node) {
        return std::ranges::find(ops, node.op) != ops.end() || contains_op(node.operands, ops) ||
               contains_op(node.body, ops) || contains_op(node.alternative, ops);
    });
}

std::size_t count(const std::vector<IrNode>& nodes) {
    std::size_t total = 0;
    for (const auto& node : nodes) total += node_count(node);
    return total;
}

std::size_t returns(const std::vector<IrNode>& nodes) {
    std::size_t total = 0;
    for (const auto& node : nodes) {
        total += node.op == IrOp::Return ? 1 : 0;
        total += returns(node.operands) + returns(node.body) + returns(node.alternative);
    }
    return total;
}

bool return_in_loop(const std::vector<IrNode>& nodes, bool in_loop) {
    return std::ranges::any_of(nodes, [&](const IrNode& node) {
        const bool inside = in_loop || node.op == IrOp::Loop;
        return (in_loop && node.op == IrOp::Return) || return_in_loop(node.operands, inside) ||
               return_in_loop(node.body, inside) || return_in_loop(node.alternative, inside);
    });
}

class Inliner {
public:
    explicit Inliner(IrProgram& program) : program_(program), analysis_(program) {}

    std::size_t run();

private:
    IrProgram& program_;
    EffectAnalysis analysis_;
    std::map<SymbolId, Callee> callees_;
    SymbolId caller_ = no_symbol;
    std::size_t count_ = 0;

    // Only functions whose inlined body cannot trap, call, allocate, or
    // touch the heap, so that inlining changes no trap's function or caller
    // chain and no frame or slot holds a reference longer.
    std::optional<Shape> inlinable(const IrBody& body) const;
    // The module a call of `callee` runs on: the receiver of a module call,
    // or the callee's own module for a direct call.
    SymbolId receiver_module(const IrNode& call, SymbolId callee) const {
        if (call.op == IrOp::VirtualCall) return analysis_.dispatch().module(call);
        const auto owner = program_.symbols[callee].owner;
        return owner < program_.symbols.size() && program_.symbols[owner].kind == SymbolKind::Module ? owner
                                                                                                    : no_symbol;
    }
    // The function a call inlines, or no_symbol: a direct call, or a module
    // call whose target is the same in every run, of an inlinable callee,
    // with no poll, and whose module is known when the body names it.
    SymbolId target(const IrNode& node) const {
        if (node.poll || (node.op != IrOp::Call && node.op != IrOp::VirtualCall)) return no_symbol;
        const auto callee = node.op == IrOp::Call ? node.symbol : analysis_.dispatch().target(node);
        const auto found = callees_.find(callee);
        if (callee == no_symbol || callee == caller_ || found == callees_.end()) return no_symbol;
        const std::size_t first = node.op == IrOp::VirtualCall ? 1 : 0;
        if (node.operands.size() != first + program_.symbols[callee].parameters.size()) return no_symbol;
        if (found->second.receiver && receiver_module(node, callee) == no_symbol) return no_symbol;
        return callee;
    }

    void region(std::vector<IrNode>& statements);
    void expressions(IrNode& node);

    struct Instance {
        std::uint32_t site;
        std::vector<IrNode> arguments;
        std::vector<IrNode> statements;
    };
    Instance instantiate(IrNode& call);
    SymbolId fresh(SymbolId callee, const IrSymbol& original, const std::string& name);
    IrNode make(IrOp op, const IrType& type, const Span& span, std::uint32_t site) const {
        IrNode result{op, type, span, no_symbol, {}, {}, {}, {}, {}};
        result.inline_site = site;
        return result;
    }
    IrNode load(SymbolId local, const Span& span, std::uint32_t site) const {
        auto result = make(IrOp::Load, program_.symbols[local].type, span, site);
        result.symbol = local;
        return result;
    }
    // Replaces every return of an Early body: with a store of its value to
    // `result` when there is one, or a discard of it, and then a break.
    void returns_to_breaks(std::vector<IrNode>& statements, SymbolId result, std::uint32_t site) const;
    IrNode run_once(Instance& instance, SymbolId result, const Span& span) const;
    IrNode result_local(SymbolId callee, const IrType& type, const Span& span, std::uint32_t site, SymbolId& result);
};

std::optional<Shape> Inliner::inlinable(const IrBody& body) const {
    const auto& function = program_.symbols[body.owner];
    if (!function.has_body || function.external || function.host_binding ||
        function.inline_directive == InlineDirective::Never || function.name.starts_with("$") ||
        analysis_.recursive(body.owner)) {
        return std::nullopt;
    }
    const auto& effects = analysis_.function(body.owner);
    if (effects.may_trap || effects.calls() || effects.allocates || effects.destroys || effects.any_reads ||
        effects.any_writes || !effects.field_writes.empty() || !effects.field_reads.empty() ||
        effects.element_reads || effects.element_writes) {
        return std::nullopt;
    }
    if (function.type.kind != TypeKind::Void && !value_type(function.type)) return std::nullopt;
    if (contains_op(body.instructions, {IrOp::BaseInitialize, IrOp::ReferencePlace, IrOp::TaskScope, IrOp::Spawn,
                                        IrOp::SchedulerCall, IrOp::MakeClosure})) {
        return std::nullopt;
    }
    // A module's receiver can be replaced by a reference to the module the
    // call runs on; an object's receiver cannot.
    bool object_receiver = false;
    const auto visit = [&](const auto& self, const std::vector<IrNode>& nodes) -> void {
        for (const auto& node : nodes) {
            if (node.op == IrOp::Receiver && (node.symbol >= program_.symbols.size() ||
                                              program_.symbols[node.symbol].kind != SymbolKind::Module)) {
                object_receiver = true;
            }
            self(self, node.operands);
            self(self, node.body);
            self(self, node.alternative);
        }
    };
    visit(visit, body.instructions);
    if (object_receiver) return std::nullopt;
    for (const auto parameter : function.parameters) {
        if (!value_type(program_.symbols[parameter].type)) return std::nullopt;
    }
    for (const auto& [local, use] : scan_locals(program_, body.instructions)) {
        if (!value_type(program_.symbols[local].type) || use.other) return std::nullopt;
        if (program_.symbols[local].kind == SymbolKind::Local && program_.symbols[local].owner != body.owner) {
            return std::nullopt;
        }
    }
    constexpr std::size_t budget = 40;
    if (function.inline_directive != InlineDirective::Always && count(body.instructions) > budget) return std::nullopt;
    const auto& statements = body.instructions;
    const auto total = returns(statements);
    const bool last_returns = !statements.empty() && statements.back().op == IrOp::Return;
    if (total == 0 && function.type.kind == TypeKind::Void) return Shape::Tail;
    const std::size_t returned = function.type.kind == TypeKind::Void ? 0 : 1;
    if (total == 1 && last_returns && statements.back().operands.size() == returned) {
        return Shape::Tail;
    }
    if (return_in_loop(statements, false)) return Shape::Nested;
    // A body that runs inside a loop needs a default for its result.
    if (function.type.kind == TypeKind::Float) return Shape::Nested;
    return Shape::Early;
}

SymbolId Inliner::fresh(SymbolId callee, const IrSymbol& original, const std::string& name) {
    IrSymbol local = original;
    local.kind = SymbolKind::Local;
    local.name = "$inline." + program_.symbols[callee].name + "." + name;
    local.owner = caller_;
    local.base = no_symbol;
    local.overrides = no_symbol;
    local.parameters.clear();
    local.generics.clear();
    local.exported = false;
    local.external = false;
    local.has_body = false;
    program_.symbols.push_back(std::move(local));
    return program_.symbols.size() - 1;
}

Inliner::Instance Inliner::instantiate(IrNode& call) {
    const auto callee = target(call);
    const auto module = receiver_module(call, callee);
    const std::size_t first = call.op == IrOp::VirtualCall ? 1 : 0;
    const auto& source = *callees_.at(callee).body;
    program_.inline_sites.push_back({callee, caller_, call.span, call.inline_site});
    const auto site = static_cast<std::uint32_t>(program_.inline_sites.size());
    // Sites of calls inlined into the callee earlier are copied under this
    // one, so each node's chain of sites still ends at its original caller.
    std::map<std::uint32_t, std::uint32_t> sites;
    const auto copy_site = [&](const auto& self, std::uint32_t old) -> std::uint32_t {
        if (old == 0) return site;
        if (const auto found = sites.find(old); found != sites.end()) return found->second;
        auto entry = program_.inline_sites[old - 1];
        entry.parent = self(self, entry.parent);
        program_.inline_sites.push_back(entry);
        const auto id = static_cast<std::uint32_t>(program_.inline_sites.size());
        sites.emplace(old, id);
        return id;
    };
    // Each parameter and local of the callee becomes a new local of the
    // caller, so every inlined copy has its own.
    std::map<SymbolId, SymbolId> renamed;
    // A copy: adding symbols below moves the table.
    const auto parameters = program_.symbols[callee].parameters;
    for (const auto parameter : parameters) {
        const auto original = program_.symbols[parameter];
        renamed.emplace(parameter, fresh(callee, original, original.name));
        program_.symbols[renamed.at(parameter)].constant = false;
    }
    for (const auto& [local, use] : scan_locals(program_, source.instructions)) {
        if (!renamed.contains(local)) {
            const auto original = program_.symbols[local];
            renamed.emplace(local, fresh(callee, original, original.name));
        }
    }
    Instance instance;
    instance.site = site;
    instance.statements = source.instructions;
    const auto rewrite = [&](const auto& self, std::vector<IrNode>& nodes) -> void {
        for (auto& node : nodes) {
            const auto found = renamed.find(node.symbol);
            if (found != renamed.end() && is_local(program_, node.symbol)) {
                node.symbol = found->second;
            }
            if (node.op == IrOp::Receiver) {
                // The module the call ran on, named statically.
                node.op = IrOp::Reference;
                node.symbol = module;
                node.type = program_.symbols[module].type;
            }
            // Spans stay the callee's; the site records the call.
            node.inline_site = copy_site(copy_site, node.inline_site);
            self(self, node.operands);
            self(self, node.body);
            self(self, node.alternative);
        }
    };
    rewrite(rewrite, instance.statements);
    // Arguments are evaluated once, left to right, before the body, as the
    // call evaluated them; each initializes its parameter's local.
    // A module call's receiver names a module and has no effect.
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        auto& argument = call.operands[first + i];
        auto initialize = make(IrOp::Initialize, void_type, argument.span, site);
        initialize.symbol = renamed.at(parameters[i]);
        initialize.operands.push_back(std::move(argument));
        instance.arguments.push_back(std::move(initialize));
    }
    return instance;
}

void Inliner::returns_to_breaks(std::vector<IrNode>& statements, SymbolId result, std::uint32_t site) const {
    std::vector<IrNode> rewritten;
    for (auto& statement : statements) {
        if (statement.op == IrOp::Block || statement.op == IrOp::Branch) {
            returns_to_breaks(statement.body, result, site);
            returns_to_breaks(statement.alternative, result, site);
        }
        if (statement.op != IrOp::Return) {
            rewritten.push_back(std::move(statement));
            continue;
        }
        if (!statement.operands.empty()) {
            auto value = std::move(statement.operands.front());
            if (result != no_symbol) {
                auto store = make(IrOp::Store, void_type, statement.span, site);
                store.operands.push_back(load(result, statement.span, site));
                store.operands.push_back(std::move(value));
                rewritten.push_back(std::move(store));
            } else {
                auto discard = make(IrOp::Discard, void_type, statement.span, site);
                discard.operands.push_back(std::move(value));
                rewritten.push_back(std::move(discard));
            }
        }
        rewritten.push_back(make(IrOp::Break, void_type, statement.span, statement.inline_site));
    }
    statements = std::move(rewritten);
}

// Wraps an Early body in `while (true) { body; break; }`, where each return
// stores its value to `result` (or discards it when there is no result)
// and breaks.
IrNode Inliner::run_once(Instance& instance, SymbolId result, const Span& span) const {
    auto loop = make(IrOp::Loop, void_type, span, instance.site);
    auto condition = make(IrOp::Boolean, bool_type, span, instance.site);
    condition.text = "true";
    loop.operands.push_back(std::move(condition));
    loop.body = std::move(instance.statements);
    returns_to_breaks(loop.body, result, instance.site);
    if (!std::ranges::any_of(loop.body, [](const IrNode& item) { return neura::never_completes(item); })) {
        loop.body.push_back(make(IrOp::Break, void_type, span, instance.site));
    }
    return loop;
}

// A new local for an Early body's result, initialized to zero or false;
// every path through the body stores the returned value over it.
IrNode Inliner::result_local(SymbolId callee, const IrType& type, const Span& span, std::uint32_t site,
                             SymbolId& result) {
    IrSymbol temporary{SymbolKind::Local, "result", span, caller_, no_symbol, no_symbol, type, {}, {}, {},
                       false, false, false, false};
    result = fresh(callee, temporary, "result");
    auto initial = make(type.kind == TypeKind::Bool ? IrOp::Boolean : IrOp::Integer, type, span, site);
    initial.text = type.kind == TypeKind::Bool ? "false" : "0";
    auto initialize = make(IrOp::Initialize, void_type, span, site);
    initialize.symbol = result;
    initialize.operands.push_back(std::move(initial));
    return initialize;
}

// Replaces each call with a value of a Tail or Early callee in the
// expressions of `node` with a scoped value that initializes the parameter
// locals from the arguments, runs the body, and yields the returned value.
// Statement regions below are handled by region().
void Inliner::expressions(IrNode& node) {
    const bool statements = node.op == IrOp::Block || node.op == IrOp::Branch || node.op == IrOp::Loop ||
                            node.op == IrOp::TaskScope;
    for (auto& operand : node.operands) expressions(operand);
    if (statements) {
        region(node.body);
        region(node.alternative);
    } else {
        for (auto& child : node.body) expressions(child);
        for (auto& child : node.alternative) expressions(child);
    }
    const auto callee = target(node);
    if (callee == no_symbol || node.type.kind == TypeKind::Void) return;
    const auto shape = callees_.at(callee).shape;
    if (shape == Shape::Nested) return;
    const auto type = node.type;
    const auto span = node.span;
    auto instance = instantiate(node);
    auto value = make(IrOp::ScopedValue, type, span, instance.site);
    value.body = std::move(instance.arguments);
    if (shape == Shape::Tail) {
        auto& body = instance.statements;
        auto result = std::move(body.back().operands.front());
        body.pop_back();
        std::ranges::move(body, std::back_inserter(value.body));
        value.body.push_back(std::move(result));
    } else {
        SymbolId result = no_symbol;
        value.body.push_back(result_local(callee, type, span, instance.site, result));
        value.body.push_back(run_once(instance, result, span));
        value.body.push_back(load(result, span, instance.site));
    }
    node = std::move(value);
    ++count_;
}

// Inlines calls that are whole statements: a returned call, whose returns
// then return from the caller, and a discarded call, whose body becomes a
// block. Every other call is handled by expressions().
void Inliner::region(std::vector<IrNode>& statements) {
    std::vector<IrNode> rewritten;
    rewritten.reserve(statements.size());
    for (auto& statement : statements) {
        IrNode* call = nullptr;
        if ((statement.op == IrOp::Return || statement.op == IrOp::Discard) && statement.operands.size() == 1) {
            call = &statement.operands.front();
        }
        const auto callee = call == nullptr ? no_symbol : target(*call);
        if (callee == no_symbol) {
            expressions(statement);
            rewritten.push_back(std::move(statement));
            continue;
        }
        for (auto& argument : call->operands) expressions(argument);
        const auto shape = callees_.at(callee).shape;
        const bool void_call = call->type.kind == TypeKind::Void;
        const auto span = call->span;
        if (statement.op == IrOp::Return) {
            if (void_call || program_.symbols[callee].type != program_.symbols[caller_].type) {
                expressions(statement);
                rewritten.push_back(std::move(statement));
                continue;
            }
            // The callee's returns return from the caller with the same
            // value; every path through its body returns.
            auto instance = instantiate(*call);
            auto block = make(IrOp::Block, void_type, span, instance.site);
            block.body = std::move(instance.arguments);
            std::ranges::move(instance.statements, std::back_inserter(block.body));
            rewritten.push_back(std::move(block));
            ++count_;
            continue;
        }
        if (shape == Shape::Nested) {
            expressions(statement);
            rewritten.push_back(std::move(statement));
            continue;
        }
        auto instance = instantiate(*call);
        auto block = make(IrOp::Block, void_type, span, instance.site);
        block.body = std::move(instance.arguments);
        if (shape == Shape::Tail) {
            // A body without returns, or with one at its end, whose value
            // the statement discards.
            auto& body = instance.statements;
            if (!body.empty() && body.back().op == IrOp::Return) {
                auto last = std::move(body.back());
                body.pop_back();
                if (!last.operands.empty()) {
                    auto discard = make(IrOp::Discard, void_type, last.span, last.inline_site);
                    discard.operands.push_back(std::move(last.operands.front()));
                    body.push_back(std::move(discard));
                }
            }
            std::ranges::move(body, std::back_inserter(block.body));
        } else {
            block.body.push_back(run_once(instance, no_symbol, span));
        }
        rewritten.push_back(std::move(block));
        ++count_;
    }
    statements = std::move(rewritten);
}


std::size_t Inliner::run() {
    // Inlining removes calls, and every call of a program with tasks is a
    // poll, so such programs are left alone to keep their schedule.
    if (uses_tasks(program_)) return 0;
    std::map<SymbolId, std::size_t> bodies;
    for (const auto& body : program_.bodies) {
        if (function_body(program_, body)) ++bodies[body.owner];
    }
    for (const auto& body : program_.bodies) {
        if (!function_body(program_, body) || bodies[body.owner] != 1) continue;
        if (const auto shape = inlinable(body)) {
            callees_.emplace(body.owner, Callee{&body, *shape, contains_op(body.instructions, {IrOp::Receiver})});
        }
    }
    if (callees_.empty()) return 0;
    for (std::size_t i = 0; i < program_.bodies.size(); ++i) {
        auto& body = program_.bodies[i];
        if (!function_body(program_, body) || program_.symbols[body.owner].host_binding) continue;
        caller_ = body.owner;
        // Callees are read while callers change; a callee has no calls, so
        // it is never itself rewritten.
        region(body.instructions);
    }
    return count_;
}

} // namespace

std::size_t inline_calls(IrProgram& program) { return Inliner(program).run(); }

} // namespace neura::optimization

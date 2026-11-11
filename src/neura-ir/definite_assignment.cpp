#include "definite_assignment.hpp"

#include "parser/syntax.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace neura {
namespace {

using ExprKind = Expr::Kind;
using StmtKind = Statement::Kind;

// The state of one deferred binding on the paths that reach a point. This
// follows the Flow of reference_initialization.cpp, with the assignments and
// a skipping path kept for the diagnostics.
enum class State { Unassigned, Assigned, MaybeAssigned };

// A path on which a binding stays unassigned, for a secondary label.
struct Skip {
    Span span;
    std::string text;
};

struct Slot {
    State state = State::Unassigned;
    // The assignments that ran on some path to this point, in source order.
    std::vector<Span> assignments;
    // When MaybeAssigned: a branch on which the binding is not assigned.
    std::optional<Skip> skip;
};

struct Flow {
    std::vector<Slot> slots;
    bool reachable = true;
    bool breaks = false;
    bool continues = false;
};

Flow fork(const Flow& input) {
    return {input.slots, input.reachable};
}

void add_assignment(std::vector<Span>& assignments, const Span& span) {
    const auto same = [&](const Span& other) { return other.file == span.file && other.offset == span.offset; };
    if (std::ranges::none_of(assignments, same)) assignments.push_back(span);
    std::ranges::sort(assignments, {}, &Span::offset);
}

// Merges one reachable arm of a branch into `output`. `reason` describes the
// arm for a binding that the arm leaves unassigned.
void merge(Flow& output, Flow arm, const std::optional<Skip>& reason) {
    output.breaks |= arm.breaks;
    output.continues |= arm.continues;
    if (!arm.reachable) return;
    for (auto& slot : arm.slots) {
        if (slot.state == State::Unassigned || (slot.state == State::MaybeAssigned && !slot.skip)) slot.skip = reason;
    }
    if (!output.reachable) {
        output.slots = std::move(arm.slots);
        output.reachable = true;
        return;
    }
    const auto size = std::max(output.slots.size(), arm.slots.size());
    output.slots.resize(size);
    arm.slots.resize(size);
    for (std::size_t i = 0; i < size; ++i) {
        auto& slot = output.slots[i];
        const auto& other = arm.slots[i];
        if (slot.state != other.state) {
            if (slot.state == State::Assigned) slot.skip = other.skip;
            slot.state = State::MaybeAssigned;
        }
        if (!slot.skip) slot.skip = other.skip;
        for (const auto& assignment : other.assignments) add_assignment(slot.assignments, assignment);
    }
}

// After the last arm: only a binding assigned on some paths keeps a skip.
void settle(Flow& flow) {
    for (auto& slot : flow.slots) {
        if (slot.state != State::MaybeAssigned) slot.skip.reset();
    }
}

std::optional<bool> literal_condition(const Expr& expr) {
    return expr.kind == ExprKind::Boolean ? std::optional{expr.text == "true"} : std::nullopt;
}

// The first `length` bytes of `span`, for a keyword or a name that starts it.
Span head(Span span, std::size_t length) {
    span.length = std::min(span.length, length);
    return span;
}

struct Binding {
    std::string name;
    Span declaration;
    bool field;
    // The number of loops around the declaration.
    std::size_t loops;
};

struct Loop {
    Span head;
};

class Checker {
public:
    Checker(std::span<const DeferredField> fields, DefiniteAssignmentOptions options) : options_(options) {
        scopes_.emplace_back();
        for (const auto& field : fields) {
            scopes_.back().insert_or_assign(field.name, bindings_.size());
            bindings_.push_back({field.name, field.declaration, true, 0});
        }
    }

    std::set<const Statement*> run(std::span<const Parameter> parameters, std::span<const Statement> body) {
        scopes_.emplace_back();
        for (const auto& parameter : parameters) scopes_.back().insert_or_assign(parameter.name, std::nullopt);
        Flow flow{std::vector<Slot>(bindings_.size())};
        check_body(body, flow);
        if (options_.constructor) check_exit(flow, *options_.constructor, false);
        return std::move(assignments_);
    }

private:
    DefiniteAssignmentOptions options_;
    std::vector<Binding> bindings_;
    // Each scope maps a name to its tracked binding, or to nothing when the
    // name is an untracked binding that hides an outer one.
    std::vector<std::map<std::string, std::optional<std::size_t>, std::less<>>> scopes_;
    std::vector<Loop> loops_;
    std::set<const Statement*> assignments_;

    std::optional<std::optional<std::size_t>> lookup(std::string_view name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (const auto found = scope->find(name); found != scope->end()) return found->second;
        }
        return std::nullopt;
    }

    void hide(std::string_view name) { scopes_.back().insert_or_assign(std::string(name), std::nullopt); }

    // The tracked binding `expr` names: a deferred local or field by its
    // name, or a deferred field as `instance.name`.
    std::optional<std::size_t> tracked(const Expr& expr) const {
        if (expr.kind == ExprKind::Name) {
            const auto found = lookup(expr.text);
            return found ? *found : std::nullopt;
        }
        if (expr.kind != ExprKind::Member || !options_.instance_is_receiver) return std::nullopt;
        const auto& receiver = expr.operands.front();
        if (receiver.kind != ExprKind::Name || receiver.text != "instance" || lookup("instance")) return std::nullopt;
        for (std::size_t i = 0; i < bindings_.size() && bindings_[i].field; ++i) {
            if (bindings_[i].name == expr.text) return i;
        }
        return std::nullopt;
    }

    static const Slot& slot(const Flow& flow, std::size_t index) {
        static const Slot unassigned{};
        return index < flow.slots.size() ? flow.slots[index] : unassigned;
    }

    std::string quoted(std::size_t index) const { return "'" + bindings_[index].name + "'"; }

    void label_declaration(Diagnostic& error, std::size_t index, std::string_view text) const {
        if (!bindings_[index].declaration.file.empty()) error.label(bindings_[index].declaration, std::string(text));
    }

    void label_skip(Diagnostic& error, const Slot& state, std::size_t index) const {
        if (state.state == State::MaybeAssigned && state.skip)
            error.label(state.skip->span, quoted(index) + " is " + state.skip->text);
    }

    std::string let_help(std::size_t index) const {
        return bindings_[index].field ? "consider declaring it with 'let' if it should change"
                                      : "consider declaring it with 'let', with an initial value, if it should change";
    }

    void use(std::size_t index, const Span& at, const Flow& flow) const {
        const auto& state = slot(flow, index);
        if (state.state == State::Assigned) return;
        const bool maybe = state.state == State::MaybeAssigned;
        auto error = Diagnostic(at, quoted(index) + (maybe ? " may be used before it is assigned"
                                                           : " is used before it is assigned"))
            .with_code("E0441")
            .label(maybe ? "used here, but " + quoted(index) + " is not assigned on every path to it"
                         : "used here before any assignment");
        label_declaration(error, index, "declared here without a value");
        label_skip(error, state, index);
        error.help(bindings_[index].field ? "assign " + quoted(index) + " in 'init' before reading it"
                                          : "assign " + quoted(index) + " exactly once before this use");
        throw error;
    }

    [[noreturn]] void assigned_twice(std::size_t index, const Span& at, const Slot& state) const {
        const bool maybe = state.state == State::MaybeAssigned;
        auto error = Diagnostic(at, "cannot assign twice to " + quoted(index)).with_code("E0440")
            .label(maybe ? quoted(index) + " may already be assigned here" : "second assignment");
        label_declaration(error, index, "declared here");
        for (const auto& earlier : state.assignments)
            error.label(earlier, maybe ? "assigned here on some paths" : "first assigned here");
        label_skip(error, state, index);
        error.help(let_help(index));
        throw error;
    }

    void assign(const Statement& stmt, std::size_t index, const Span& at, Flow& flow) {
        if (!flow.reachable) {
            if (stmt.assignment_operator == "=") assignments_.insert(&stmt);
            return;
        }
        if (flow.slots.size() <= index) flow.slots.resize(index + 1);
        auto& state = flow.slots[index];
        if (state.state != State::Unassigned) assigned_twice(index, at, state);
        if (bindings_[index].loops < loops_.size()) {
            const auto& loop = loops_[bindings_[index].loops];
            auto error = Diagnostic(at, quoted(index) + " is assigned inside a loop").with_code("E0442")
                .label("this assignment could run more than once")
                .label(loop.head, "the loop that can repeat it");
            label_declaration(error, index, "declared outside the loop here");
            error.help("assign " + quoted(index) + " once, before or after the loop, or declare it with 'let'" +
                       (bindings_[index].field ? "" : ", with an initial value,") + " if it should change");
            throw error;
        }
        state.state = State::Assigned;
        state.skip.reset();
        add_assignment(state.assignments, at);
        assignments_.insert(&stmt);
    }

    void check_exit(const Flow& flow, const Span& at, bool returns) const {
        if (!flow.reachable) return;
        for (std::size_t i = 0; i < bindings_.size() && bindings_[i].field; ++i) {
            const auto& state = slot(flow, i);
            if (state.state == State::Assigned) continue;
            const bool maybe = state.state == State::MaybeAssigned;
            auto error = Diagnostic(at, std::string(returns ? "'init' may return" : "'init' may end") +
                                        " without assigning 'last' field " + quoted(i))
                .with_code("E0446")
                .label(returns ? "returns here before " + quoted(i) + " is assigned"
                               : (maybe ? quoted(i) + " is not assigned on every path through this constructor"
                                        : "this constructor never assigns " + quoted(i)));
            label_declaration(error, i, "declared here without a value");
            label_skip(error, state, i);
            error.help("assign " + quoted(i) + " exactly once on every path through 'init'");
            throw error;
        }
    }

    // Reads every binding that `expr` evaluates.
    void read(const Expr& expr, const Flow& flow) {
        if (!flow.reachable) return;
        if (const auto index = tracked(expr)) use(*index, expr.span, flow);
        switch (expr.kind) {
        case ExprKind::Lambda:
            capture(*expr.lambda, expr.span, flow);
            return;
        case ExprKind::Conditional: {
            read(expr.operands[0], flow);
            const auto selected = literal_condition(expr.operands[0]);
            if (selected.value_or(true)) read(expr.operands[1], flow);
            if (!selected.value_or(false)) read(expr.operands[2], flow);
            return;
        }
        case ExprKind::Binary:
            if (expr.text == "&&" || expr.text == "||") {
                read(expr.operands[0], flow);
                const auto selected = literal_condition(expr.operands[0]);
                const bool reaches_right = expr.text == "&&" ? selected.value_or(true) : !selected.value_or(false);
                if (reaches_right) read(expr.operands[1], flow);
                return;
            }
            break;
        default:
            break;
        }
        for (const auto& operand : expr.operands) read(operand, flow);
        if ((expr.kind == ExprKind::PrefixUpdate || expr.kind == ExprKind::PostfixUpdate) && !expr.operands.empty()) {
            // The operand was read, so it is assigned; the update is a second assignment.
            if (const auto index = tracked(expr.operands.front()))
                assigned_twice(*index, expr.span, slot(flow, *index));
        }
        if (expr.match) {
            read(expr.match->expressions.front(), flow);
            for (const auto& arm : expr.match->arms) {
                begin_arm(arm);
                read(*arm.value, flow);
                scopes_.pop_back();
            }
        }
    }

    // A closure copies the locals it captures when it is created, and a
    // closure created in a constructor may run before a field is assigned.
    void capture(const Declaration& lambda, const Span& at, const Flow& flow) {
        scopes_.emplace_back();
        for (const auto& parameter : lambda.parameters) hide(parameter.name);
        const auto captured = [&](const Expr& expr) {
            const auto index = tracked(expr);
            if (!index || slot(flow, *index).state == State::Assigned) return;
            const auto& state = slot(flow, *index);
            const bool maybe = state.state == State::MaybeAssigned;
            auto error = Diagnostic(expr.span, "closure captures " + quoted(*index) + " before it is assigned")
                .with_code("E0443").label(quoted(*index) + " is captured here")
                .label(head(at, 4), maybe ? "the closure is created here, where " + quoted(*index) +
                                                " may not be assigned yet"
                                          : "the closure is created here, before " + quoted(*index) +
                                                " is assigned");
            label_declaration(error, *index, "declared here without a value");
            label_skip(error, state, *index);
            error.note(bindings_[*index].field
                           ? "the closure could run before 'init' assigns " + quoted(*index)
                           : "a closure copies the values it captures when it is created");
            error.help("assign " + quoted(*index) + " before creating the closure");
            throw error;
        };
        scan_body(lambda.body, captured);
        scopes_.pop_back();
    }

    using Visit = std::function<void(const Expr&)>;

    // Visits every name reference in a nested lambda body, with its scopes.
    void scan(const Expr& expr, const Visit& visit) {
        visit(expr);
        if (expr.kind == ExprKind::Lambda) {
            scopes_.emplace_back();
            for (const auto& parameter : expr.lambda->parameters) hide(parameter.name);
            scan_body(expr.lambda->body, visit);
            scopes_.pop_back();
            return;
        }
        for (const auto& operand : expr.operands) scan(operand, visit);
        if (expr.match) {
            scan(expr.match->expressions.front(), visit);
            for (const auto& arm : expr.match->arms) {
                begin_arm(arm);
                scan(*arm.value, visit);
                scopes_.pop_back();
            }
        }
    }

    void scan_body(std::span<const Statement> statements, const Visit& visit) {
        scopes_.emplace_back();
        for (const auto& stmt : statements) scan_statement(stmt, visit);
        scopes_.pop_back();
    }

    void scan_statement(const Statement& stmt, const Visit& visit) {
        scopes_.emplace_back();
        for (const auto& init : stmt.initializer) scan_statement(init, visit);
        for (const auto& expr : stmt.expressions) scan(expr, visit);
        scopes_.pop_back();
        switch (stmt.kind) {
        case StmtKind::Binding:
            if (stmt.declaration->initializer) scan(*stmt.declaration->initializer, visit);
            hide(stmt.declaration->name);
            return;
        case StmtKind::Destructure:
            for (const auto& binding : stmt.bindings) hide(binding.text);
            return;
        case StmtKind::Match:
            for (const auto& arm : stmt.arms) {
                begin_arm(arm);
                scan_body(arm.body, visit);
                scopes_.pop_back();
            }
            return;
        case StmtKind::Select:
            for (const auto& arm : stmt.select_arms) {
                if (arm.operation) scan(*arm.operation, visit);
                scopes_.emplace_back();
                if (arm.binding) hide(arm.binding->text);
                scan_body(arm.body, visit);
                scopes_.pop_back();
            }
            return;
        default:
            break;
        }
        // Loop headers, scope handles, and for-each elements are visible in the body.
        scopes_.emplace_back();
        for (const auto& init : stmt.initializer) {
            if (init.kind == StmtKind::Binding) hide(init.declaration->name);
        }
        if (stmt.kind == StmtKind::ForEach) hide(stmt.declaration->name);
        if (stmt.kind == StmtKind::Scope)
            for (const auto& binding : stmt.bindings) hide(binding.text);
        scan_body(stmt.body, visit);
        for (const auto& step : stmt.step) scan_statement(step, visit);
        scopes_.pop_back();
        scan_body(stmt.alternative, visit);
    }

    void check_body(std::span<const Statement> statements, Flow& flow) {
        scopes_.emplace_back();
        for (const auto& stmt : statements) check_statement(stmt, flow);
        scopes_.pop_back();
    }

    void begin_arm(const MatchArm& arm) {
        scopes_.emplace_back();
        for (const auto& binding : arm.bindings) hide(binding.text);
    }

    void branch(const Statement& stmt, Flow& flow) {
        const auto& condition = stmt.expressions.front();
        const auto known = literal_condition(condition);
        auto yes = fork(flow);
        auto no = fork(flow);
        yes.reachable &= known.value_or(true);
        no.reachable &= !known.value_or(false);
        check_body(stmt.body, yes);
        check_body(stmt.alternative, no);
        flow.reachable = false;
        merge(flow, std::move(yes), Skip{condition.span, "not assigned when this condition is true"});
        merge(flow, std::move(no), Skip{condition.span, "not assigned when this condition is false"});
        settle(flow);
    }

    void match(const Statement& stmt, Flow& flow) {
        // Lowering separately checks that the patterns cover the subject.
        const auto before = fork(flow);
        flow.reachable = false;
        for (const auto& arm : stmt.arms) {
            auto selected = fork(before);
            begin_arm(arm);
            for (const auto& child : arm.body) check_statement(child, selected);
            scopes_.pop_back();
            merge(flow, std::move(selected), Skip{head(arm.span, arm.variant.size()), "not assigned in this arm"});
        }
        settle(flow);
    }

    void select(const Statement& stmt, Flow& flow) {
        for (const auto& arm : stmt.select_arms) {
            if (arm.operation) read(*arm.operation, flow);
        }
        const auto before = fork(flow);
        flow.reachable = false;
        for (const auto& arm : stmt.select_arms) {
            auto selected = fork(before);
            scopes_.emplace_back();
            if (arm.binding) hide(arm.binding->text);
            check_body(arm.body, selected);
            scopes_.pop_back();
            const auto at = arm.operation ? arm.operation->span : head(arm.span, 1);
            merge(flow, std::move(selected), Skip{at, "not assigned in this arm"});
        }
        settle(flow);
    }

    void loop(const Statement& stmt, Flow& flow) {
        scopes_.emplace_back();
        for (const auto& init : stmt.initializer) check_statement(init, flow);

        const auto* condition = stmt.expressions.empty() ? nullptr : &stmt.expressions.front();
        const bool foreach = stmt.kind == StmtKind::ForEach;
        const bool post_test = stmt.kind == StmtKind::Do;
        const auto known = foreach ? std::nullopt : condition ? literal_condition(*condition) : std::optional{true};
        if (condition && !post_test) read(*condition, flow);
        if (foreach) hide(stmt.declaration->name);

        const std::size_t keyword = stmt.kind == StmtKind::While ? 5 : stmt.kind == StmtKind::Do ? 2 : 3;
        loops_.push_back({head(stmt.span, keyword)});
        auto iteration = fork(flow);
        if (!post_test) iteration.reachable &= known.value_or(true);
        check_body(stmt.body, iteration);
        const bool reaches_test = iteration.reachable || iteration.continues;
        auto next = fork(flow);
        next.reachable = reaches_test;
        for (const auto& step : stmt.step) check_statement(step, next);
        if (condition && post_test) read(*condition, next);
        loops_.pop_back();
        // A loop assigns no binding declared outside it, so the states after
        // it are the states before it.
        if (post_test || known.value_or(false)) {
            flow.reachable = iteration.breaks || (post_test && reaches_test && !known.value_or(false));
        }
        scopes_.pop_back();
    }

    void assignment(const Statement& stmt, Flow& flow) {
        const auto& destination = stmt.expressions[0];
        const auto index = tracked(destination);
        if (!index) {
            read(destination, flow);
            read(stmt.expressions[1], flow);
            return;
        }
        if (destination.kind == ExprKind::Member) read(destination.operands.front(), flow);
        if (stmt.assignment_operator != "=") read(destination, flow);
        read(stmt.expressions[1], flow);
        assign(stmt, *index, destination.span, flow);
    }

    void check_statement(const Statement& stmt, Flow& flow) {
        switch (stmt.kind) {
        case StmtKind::Assignment:
            assignment(stmt, flow);
            return;
        case StmtKind::While:
        case StmtKind::Do:
        case StmtKind::For:
        case StmtKind::ForEach:
            loop(stmt, flow);
            return;
        case StmtKind::Select:
            select(stmt, flow);
            return;
        default:
            break;
        }
        for (const auto& expr : stmt.expressions) read(expr, flow);
        switch (stmt.kind) {
        case StmtKind::Binding: {
            const auto& declaration = *stmt.declaration;
            if (declaration.initializer) read(*declaration.initializer, flow);
            if (declaration.name == "_") break;
            if (declaration.last && !declaration.initializer && declaration.kind == Declaration::Kind::Binding) {
                const auto index = bindings_.size();
                bindings_.push_back({declaration.name, declaration.name_span.value_or(declaration.span), false,
                                     loops_.size()});
                if (flow.slots.size() <= index) flow.slots.resize(index + 1);
                flow.slots[index] = Slot{};
                scopes_.back().insert_or_assign(declaration.name, index);
            } else {
                hide(declaration.name);
            }
            break;
        }
        case StmtKind::Destructure:
            for (const auto& binding : stmt.bindings) hide(binding.text);
            break;
        case StmtKind::Block:
            check_body(stmt.body, flow);
            break;
        case StmtKind::Scope:
            scopes_.emplace_back();
            for (const auto& binding : stmt.bindings) hide(binding.text);
            check_body(stmt.body, flow);
            scopes_.pop_back();
            break;
        case StmtKind::If:
            branch(stmt, flow);
            break;
        case StmtKind::Match:
            match(stmt, flow);
            break;
        case StmtKind::Return:
            if (options_.constructor) check_exit(flow, head(stmt.span, 6), true);
            flow.reachable = false;
            break;
        case StmtKind::Break:
            flow.breaks |= flow.reachable;
            flow.reachable = false;
            break;
        case StmtKind::Continue:
            flow.continues |= flow.reachable;
            flow.reachable = false;
            break;
        case StmtKind::Expression: {
            const auto& expr = stmt.expressions.front();
            if (options_.trap_is_builtin && !lookup("trap_with_message") && expr.kind == ExprKind::Call) {
                const auto& callee = expr.operands.front();
                if (callee.kind == ExprKind::Name && callee.text == "trap_with_message") flow.reachable = false;
            }
            break;
        }
        default:
            break;
        }
    }
};

} // namespace

std::set<const Statement*> check_definite_assignment(std::span<const Parameter> parameters,
                                                     std::span<const Statement> body,
                                                     std::span<const DeferredField> fields,
                                                     DefiniteAssignmentOptions options) {
    return Checker(fields, options).run(parameters, body);
}

} // namespace neura

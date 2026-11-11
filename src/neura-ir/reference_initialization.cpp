#include "reference_initialization.hpp"

#include "parser/syntax.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace neura {
namespace {

using ExprKind = Expr::Kind;
using StmtKind = Statement::Kind;

enum class Binding { Unbound, Bound, MaybeBound };

struct Flow {
    std::vector<Binding> fields;
    bool reachable = true;
    bool breaks = false;
    bool continues = false;
};

Flow fork(const Flow& input) {
    return {input.fields, input.reachable};
}

void merge(Flow& output, const Flow& arm) {
    output.breaks |= arm.breaks;
    output.continues |= arm.continues;
    if (!arm.reachable) return;
    if (!output.reachable) {
        output.fields = arm.fields;
        output.reachable = true;
        return;
    }
    for (std::size_t i = 0; i < output.fields.size(); ++i) {
        if (output.fields[i] != arm.fields[i]) output.fields[i] = Binding::MaybeBound;
    }
}

std::optional<bool> literal_condition(const Expr& expr) {
    return expr.kind == ExprKind::Boolean ? std::optional{expr.text == "true"} : std::nullopt;
}

class Checker {
public:
    Checker(std::span<const ReferenceFieldInitialization> fields, ReferenceConstructorOptions options)
        : fields_(fields), options_(options) {}

    std::set<const Statement*> run(const Declaration& constructor) {
        if (fields_.empty()) return {};
        names_.emplace_back();
        for (const auto& parameter : constructor.parameters) names_.back().insert(parameter.name);

        Flow flow{std::vector<Binding>(fields_.size(), Binding::Unbound)};
        auto statements = std::span(constructor.body);
        if (options_.initializes_base && !statements.empty()) {
            // Base arguments run before this class's declaration initializers.
            check_statement(statements.front(), flow, false);
            statements = statements.subspan(1);
        }
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (fields_[i].has_initializer) flow.fields[i] = Binding::Bound;
        }
        check_body(statements, flow, false);
        check_exit(flow, constructor.span);
        return std::move(first_bindings_);
    }

    void initializers(const Declaration& cls) {
        if (fields_.empty()) return;
        Flow flow{std::vector<Binding>(fields_.size(), Binding::Unbound)};
        for (const auto& member : cls.members) {
            if (member.kind != Declaration::Kind::Binding || member.static_member || !member.initializer)
                continue;
            read(*member.initializer, flow);
            if (const auto index = field_index(member.name)) flow.fields[*index] = Binding::Bound;
        }
    }

private:
    std::span<const ReferenceFieldInitialization> fields_;
    ReferenceConstructorOptions options_;
    std::vector<std::set<std::string_view, std::less<>>> names_;
    std::set<const Statement*> first_bindings_;

    bool shadowed(std::string_view name) const {
        return std::ranges::any_of(names_, [&](const auto& scope) { return scope.contains(name); });
    }

    std::optional<std::size_t> field_index(std::string_view name) const {
        const auto field = std::ranges::find(fields_, name, &ReferenceFieldInitialization::name);
        if (field == fields_.end()) return std::nullopt;
        return static_cast<std::size_t>(field - fields_.begin());
    }

    std::optional<std::size_t> owned_field(const Expr& expr) const {
        if (expr.kind == ExprKind::Name) {
            if (shadowed(expr.text)) return std::nullopt;
        } else if (expr.kind == ExprKind::Member) {
            const auto& receiver = expr.operands.front();
            if (!options_.instance_is_receiver || receiver.kind != ExprKind::Name ||
                receiver.text != "instance" || shadowed("instance"))
                return std::nullopt;
        } else {
            return std::nullopt;
        }
        return field_index(expr.text);
    }

    void check_exit(const Flow& flow, const Span& span) const {
        if (!flow.reachable) return;
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (flow.fields[i] != Binding::Bound)
                throw Diagnostic(span, "constructor may exit before binding reference field '" +
                                        fields_[i].name + "'").with_code("E0513");
        }
    }

    void read(const Expr& expr, const Flow& flow) {
        if (!flow.reachable) return;
        if (const auto field = owned_field(expr); field && flow.fields[*field] != Binding::Bound) {
            throw Diagnostic(expr.span, "reference field '" + fields_[*field].name +
                                        "' may be read before it is bound").with_code("E0513");
        }
        if (expr.kind == ExprKind::Lambda) return;
        if (expr.kind == ExprKind::Conditional) {
            read(expr.operands[0], flow);
            const auto selected = literal_condition(expr.operands[0]);
            if (selected.value_or(true)) read(expr.operands[1], flow);
            if (!selected.value_or(false)) read(expr.operands[2], flow);
            return;
        }
        if (expr.kind == ExprKind::Binary && (expr.text == "&&" || expr.text == "||")) {
            read(expr.operands[0], flow);
            const auto selected = literal_condition(expr.operands[0]);
            const bool reaches_right = expr.text == "&&" ? selected.value_or(true) : !selected.value_or(false);
            if (reaches_right) read(expr.operands[1], flow);
            return;
        }
        for (const auto& operand : expr.operands) read(operand, flow);
        if (expr.match) {
            read(expr.match->expressions.front(), flow);
            for (const auto& arm : expr.match->arms) {
                begin_arm(arm);
                read(*arm.value, flow);
                names_.pop_back();
            }
        }
    }

    void assignment(const Statement& stmt, Flow& flow, bool in_loop) {
        const auto& destination = stmt.expressions[0];
        const auto field = owned_field(destination);
        if (!field || stmt.assignment_operator != "=") {
            read(destination, flow);
            read(stmt.expressions[1], flow);
            return;
        }
        auto& state = flow.fields[*field];
        if (flow.reachable && state == Binding::MaybeBound) {
            throw Diagnostic(destination.span, "reference field '" + fields_[*field].name +
                "' is bound on only some paths to this assignment").with_code("E0513");
        }
        read(stmt.expressions[1], flow);
        if (state != Binding::Unbound) return;
        if (flow.reachable && in_loop) {
            throw Diagnostic(destination.span, "first binding of reference field '" + fields_[*field].name +
                "' must be outside a loop body or step").with_code("E0513");
        }
        first_bindings_.insert(&stmt);
        state = Binding::Bound;
    }

    void check_body(std::span<const Statement> statements, Flow& flow, bool in_loop) {
        names_.emplace_back();
        for (const auto& stmt : statements) check_statement(stmt, flow, in_loop);
        names_.pop_back();
    }

    void branch(const Statement& stmt, Flow& flow, bool in_loop) {
        const auto condition = literal_condition(stmt.expressions.front());
        auto yes = fork(flow);
        auto no = fork(flow);
        yes.reachable &= condition.value_or(true);
        no.reachable &= !condition.value_or(false);
        check_body(stmt.body, yes, in_loop);
        check_body(stmt.alternative, no, in_loop);
        flow.reachable = false;
        merge(flow, yes);
        merge(flow, no);
    }

    void begin_arm(const MatchArm& arm) {
        names_.emplace_back();
        for (const auto& binding : arm.bindings) names_.back().insert(binding.text);
    }

    void match(const Statement& stmt, Flow& flow, bool in_loop) {
        // Lowering separately checks that the patterns cover the subject.
        const auto before = fork(flow);
        flow.reachable = false;
        for (const auto& arm : stmt.arms) {
            auto selected = fork(before);
            begin_arm(arm);
            for (const auto& child : arm.body) check_statement(child, selected, in_loop);
            names_.pop_back();
            merge(flow, selected);
        }
    }

    void loop(const Statement& stmt, Flow& flow, bool in_loop) {
        names_.emplace_back();
        for (const auto& init : stmt.initializer) check_statement(init, flow, in_loop);

        const auto* condition = stmt.expressions.empty() ? nullptr : &stmt.expressions.front();
        const bool foreach = stmt.kind == StmtKind::ForEach;
        const bool post_test = stmt.kind == StmtKind::Do;
        const auto known = foreach ? std::nullopt : condition ? literal_condition(*condition) : std::optional{true};
        if (condition && !post_test) read(*condition, flow);
        if (foreach) names_.back().insert(stmt.declaration->name);

        auto iteration = fork(flow);
        if (!post_test) iteration.reachable &= known.value_or(true);
        check_body(stmt.body, iteration, true);
        const bool reaches_test = iteration.reachable || iteration.continues;
        auto next = fork(flow);
        next.reachable = reaches_test;
        for (const auto& step : stmt.step) check_statement(step, next, true);
        if (condition && post_test) read(*condition, next);
        if (post_test || known.value_or(false)) {
            flow.reachable = iteration.breaks || (post_test && reaches_test && !known.value_or(false));
        }
        names_.pop_back();
    }

    void check_statement(const Statement& stmt, Flow& flow, bool in_loop) {
        switch (stmt.kind) {
        case StmtKind::Assignment:
            assignment(stmt, flow, in_loop);
            return;
        case StmtKind::While:
        case StmtKind::Do:
        case StmtKind::For:
        case StmtKind::ForEach:
            loop(stmt, flow, in_loop);
            return;
        default:
            break;
        }
        for (const auto& expr : stmt.expressions) read(expr, flow);
        switch (stmt.kind) {
        case StmtKind::Binding:
            if (stmt.declaration->initializer) read(*stmt.declaration->initializer, flow);
            names_.back().insert(stmt.declaration->name);
            break;
        case StmtKind::Destructure:
            for (const auto& binding : stmt.bindings) names_.back().insert(binding.text);
            break;
        case StmtKind::Block:
            check_body(stmt.body, flow, in_loop);
            break;
        case StmtKind::If:
            branch(stmt, flow, in_loop);
            break;
        case StmtKind::Match:
            match(stmt, flow, in_loop);
            break;
        case StmtKind::Return:
            check_exit(flow, stmt.span);
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
            if (options_.trap_is_builtin && !shadowed("trap_with_message") && expr.kind == ExprKind::Call) {
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

std::set<const Statement*> check_reference_constructor(
    const Declaration& constructor, std::span<const ReferenceFieldInitialization> fields,
    ReferenceConstructorOptions options) {
    return Checker(fields, options).run(constructor);
}

void check_reference_field_initializers(const Declaration& cls,
                                        std::span<const ReferenceFieldInitialization> fields,
                                        bool instance_is_receiver) {
    Checker(fields, {.instance_is_receiver = instance_is_receiver}).initializers(cls);
}

} // namespace neura

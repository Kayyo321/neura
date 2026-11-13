#include "expression.hpp"

#include <set>

namespace neura::lift {

ExpressionPtr input(std::uint32_t type, std::size_t index) {
    auto e = std::make_shared<Expression>();
    e->kind = Expression::Kind::Input;
    e->type = type;
    e->input = index;
    return e;
}

ExpressionPtr immediate(std::uint32_t type, std::uint64_t bits) {
    auto e = std::make_shared<Expression>();
    e->kind = Expression::Kind::Immediate;
    e->type = type;
    e->bits = bits;
    return e;
}

ExpressionPtr unary(std::string op, std::uint32_t type, ExpressionPtr operand) {
    auto e = std::make_shared<Expression>();
    e->kind = Expression::Kind::Unary;
    e->op = std::move(op);
    e->type = type;
    e->operands = {std::move(operand)};
    return e;
}

ExpressionPtr convert(std::uint32_t type, ExpressionPtr operand) {
    auto e = std::make_shared<Expression>();
    e->kind = Expression::Kind::Convert;
    e->type = type;
    e->operands = {std::move(operand)};
    return e;
}

ExpressionPtr binary(std::string op, std::uint32_t type, ExpressionPtr left, ExpressionPtr right) {
    auto e = std::make_shared<Expression>();
    e->kind = Expression::Kind::Binary;
    e->op = std::move(op);
    e->type = type;
    e->operands = {std::move(left), std::move(right)};
    return e;
}

std::vector<std::pair<std::uint32_t, ExpressionPtr>> LiftedForm::computation_order() const {
    if (!order.empty()) {
        return order;
    }
    return {values.begin(), values.end()};
}

std::uint64_t width_mask(unsigned width) {
    return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

bool is_integer(const nvr::Type& type) {
    return type.kind == nvr::TypeKind::Signed || type.kind == nvr::TypeKind::Unsigned;
}

bool is_floating(const nvr::Type& type) {
    return type.kind == nvr::TypeKind::Float || type.kind == nvr::TypeKind::BFloat;
}

bool is_comparison(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

namespace {

bool scalar(const nvr::Type& type) {
    return type.kind == nvr::TypeKind::Bool || is_integer(type) || is_floating(type);
}

bool same_shape(const nvr::Type& a, const nvr::Type& b) { return a.kind == b.kind && a.width == b.width; }

class TypeChecker {
public:
    TypeChecker(const nvr::Image& image, const nvr::Block& block) : image_(image), block_(block) {}

    const char* check(const Expression* e, std::size_t depth = 0) {
        if (!e) return "a missing expression";
        if (depth > 256) return "expressions nested too deeply";
        if (checked_.contains(e)) return nullptr;
        if (++nodes_ > 100000) return "too many expressions";
        if (e->type >= image_.types.size()) return "an expression type outside the type table";
        const auto& type = image_.types[e->type];
        // A reference is an opaque 64-lane handle: it can only pass through
        // or be compared for identity, with another or with a constant
        // handle such as nil.
        if (!scalar(type) && !(nvr::is_reference(type.kind) && (e->kind == Expression::Kind::Input ||
                                                                 e->kind == Expression::Kind::Immediate))) {
            return "an expression of a non-scalar type";
        }
        const auto operand = [&](std::size_t i) -> const nvr::Type& { return image_.types[e->operands[i]->type]; };
        switch (e->kind) {
        case Expression::Kind::Input:
            if (!e->operands.empty()) return "an input with operands";
            if (e->input >= block_.inputs.size()) return "an input outside the block's inputs";
            if (block_.inputs[e->input].type != e->type) return "an input of another type";
            break;
        case Expression::Kind::Immediate:
            if (!e->operands.empty()) return "an immediate with operands";
            if ((e->bits & ~width_mask(nvr::lane_count(type))) != 0) return "an immediate wider than its type";
            break;
        case Expression::Kind::Unary: {
            if (e->operands.size() != 1) return "a unary operation without one operand";
            if (const auto* error = check(e->operands[0].get(), depth + 1)) return error;
            const auto& from = operand(0);
            if (e->op == "!") {
                if (type.kind != nvr::TypeKind::Bool || from.kind != nvr::TypeKind::Bool) return "'!' on a non-Boolean";
            } else if (e->op == "index") {
                if (!is_integer(from) || type.kind != nvr::TypeKind::Unsigned || type.width != 64) {
                    return "'index' not from an integer to uint64";
                }
            } else if (e->op == "~") {
                if (!is_integer(type) || !same_shape(type, from)) return "'~' not on an integer of its type";
            } else if (e->op == "hash") {
                if (!(from.kind == nvr::TypeKind::Bool || is_integer(from) || is_floating(from)) ||
                    type.kind != nvr::TypeKind::Unsigned || type.width != 32) {
                    return "'hash' not from a scalar to uint32";
                }
            } else if (e->op == "neg") {
                if (!(is_integer(type) || is_floating(type)) || !same_shape(type, from)) return "'neg' changes its type";
            } else {
                return "an unknown unary operation";
            }
            break;
        }
        case Expression::Kind::Convert: {
            if (e->operands.size() != 1) return "a conversion without one operand";
            if (const auto* error = check(e->operands[0].get(), depth + 1)) return error;
            const auto& from = operand(0);
            if (!(is_integer(from) || is_floating(from)) || !(is_integer(type) || is_floating(type))) {
                return "a conversion between non-numeric types";
            }
            break;
        }
        case Expression::Kind::Binary: {
            if (e->operands.size() != 2) return "a binary operation without two operands";
            if (const auto* error = check(e->operands[0].get(), depth + 1)) return error;
            if (const auto* error = check(e->operands[1].get(), depth + 1)) return error;
            const auto& a = operand(0);
            const auto& op = e->op;
            // A shift count is an integer of any width, read as unsigned
            // (docs/lang/Numbers.md): the circuit fails for any count with a
            // lane set at or above the word's width, as the lowering does.
            const bool shift = (op == "<<" || op == ">>") && is_integer(a) && is_integer(operand(1));
            if (!shift && !same_shape(a, operand(1))) return "a binary operation on operands of different types";
            const bool boolean_result = type.kind == nvr::TypeKind::Bool;
            if (a.kind == nvr::TypeKind::Bool) {
                if (op != "&&" && op != "||" && op != "==" && op != "!=") return "an unknown Boolean operation";
                if (!boolean_result) return "a Boolean operation with a non-Boolean result";
            } else if (!scalar(a)) {
                // Handles compare lane by lane, as the converter compares
                // object references.
                if ((op != "==" && op != "!=") || !boolean_result) return "an operation on references";
            } else if (is_comparison(op)) {
                if (!boolean_result) return "a comparison with a non-Boolean result";
            } else if (is_floating(a)) {
                if (op != "+" && op != "-" && op != "*" && op != "/") return "an unknown float operation";
                if (!same_shape(type, a)) return "a float operation that changes its type";
            } else {
                if (op != "+" && op != "+~" && op != "-" && op != "-~" && op != "*" && op != "/" && op != "%" && op != "&" &&
                    op != "|" && op != "^" && op != "<<" && op != ">>") {
                    return "an unknown integer operation";
                }
                if (!same_shape(type, a)) return "an integer operation that changes its type";
            }
            break;
        }
        }
        checked_.insert(e);
        return nullptr;
    }

private:
    const nvr::Image& image_;
    const nvr::Block& block_;
    std::set<const Expression*> checked_;
    std::size_t nodes_ = 0;
};

} // namespace

const char* form_type_error(const nvr::Image& image, const nvr::Block& block, const LiftedForm& form) {
    for (const auto& input : block.inputs) {
        if (input.type >= image.types.size()) return "an input type outside the type table";
    }
    if (!form.dispatch.empty()) {
        if (!form.values.empty() || form.condition || !form.order.empty() || !form.schedule.empty()) {
            return "a dispatch form with values";
        }
        return nullptr;
    }
    if (!form.shared && !form.schedule.empty()) return "a schedule in a form that is not shared";
    // The type each exit reads from the graph, keyed by first lane.
    std::map<std::uint32_t, std::uint32_t> targets;
    for (const auto& exit : block.exits) {
        for (const auto& value : exit.arguments) {
            if (value.kind == nvr::ValueKind::Lanes) targets.emplace(value.index, value.type);
        }
        for (const auto& assignment : exit.assignments) {
            if (assignment.value.kind == nvr::ValueKind::Lanes) targets.emplace(assignment.value.index, assignment.value.type);
        }
    }
    if (form.values.size() != targets.size()) return "values that do not match the lanes the exits read";
    TypeChecker checker(image, block);
    for (const auto& expression : form.schedule) {
        if (const auto* error = checker.check(expression.get())) return error;
    }
    for (const auto& [lane, expression] : form.values) {
        const auto target = targets.find(lane);
        if (target == targets.end()) return "a value for lanes no exit reads";
        if (const auto* error = checker.check(expression.get())) return error;
        if (target->second >= image.types.size() ||
            nvr::lane_count(image.types[target->second]) != nvr::lane_count(image.types[expression->type])) {
            return "a value whose width differs from the lanes the exit reads";
        }
    }
    if (!form.order.empty()) {
        if (form.order.size() != form.values.size()) return "an order that does not list every value once";
        std::set<std::uint32_t> seen;
        for (const auto& [lane, expression] : form.order) {
            const auto it = form.values.find(lane);
            if (it == form.values.end() || it->second != expression || !seen.insert(lane).second) {
                return "an order that does not list every value once";
            }
        }
    }
    if (form.condition) {
        if (const auto* error = checker.check(form.condition.get())) return error;
        if (image.types[form.condition->type].kind != nvr::TypeKind::Bool) return "a non-Boolean condition";
        if (!form.exit_when_true) return "a condition without its exit";
    }
    if (form.exit_when_true && !form.condition) return "an exit for a missing condition";
    return nullptr;
}

} // namespace neura::lift

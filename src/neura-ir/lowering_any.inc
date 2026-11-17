// The inline type `any`, and the rule that a scope-bound value is never
// converted to `class`, an interface or `any` (docs/design/Matching030.md).
// Included by lowering.cpp after the class definition; every function here
// is a member of Lowering.

// A scope-bound value converted to a type that forgets it, `class`, an
// interface or `any`, could outlive its scope (E0605).
void Lowering::reject_erasure(const IrType& actual, const IrType& target, const Span& span) const {
    if (!scope_bound(actual) || scope_bound(target)) return;
    if (!class_reference(target) && target.kind != TypeKind::Any) return;
    const auto what = target.kind == TypeKind::Any ? std::string("any")
                    : symbol(receiver_type(target).symbol).kind == SymbolKind::Interface ? "an interface"
                    : "'" + describe(target) + "'";
    throw Diagnostic(span, "a value of type " + describe(actual) + " cannot leave its scope: it cannot be converted to " +
                           (target.kind == TypeKind::Any ? "any" : describe(target)))
        .with_code("E0605").label("this value is bound to its task scope")
        .note("converted to " + what + ", it would no longer be known to be scope-bound")
        .help("compare task handles with '==' rather than passing them as 'class'");
}

// `any` is a type name wherever a type is read, so no type-like
// declaration takes the name (E0311). Functions, methods, fields,
// parameters and locals named `any` keep their meaning.
void Lowering::reserve_any(const std::string& name, const Span& span, std::string_view what) const {
    if (name != "any") return;
    throw Diagnostic(span, "'any' is a reserved type name").with_code("E0311")
        .label("'any' is the built-in type, so no " + std::string(what) + " takes its name")
        .help("choose another name");
}

// Packs a value where an `any` is expected. Packing an `any` gives the same
// `any`; a value that an `any` cannot hold is E0492, and a scope-bound one
// E0605. A type parameter's value is checked on the specialized program.
IrNode Lowering::pack_any(IrNode inner, const Span& span) {
    if (inner.type.kind == TypeKind::Any) return inner;
    reject_erasure(inner.type, any_type, span);
    if (!any_holdable(inner.type) && inner.type.kind != TypeKind::TypeParameter)
        throw Diagnostic(span, "a value of type " + describe(inner.type) + " cannot be held in any").with_code("E0492")
            .label("this has type " + describe(inner.type))
            .note("an any holds a bool, a sized integer, a float, a string, a class or interface reference, or nil")
            .help("hold it in a class and pack the reference");
    auto result = node(IrOp::AnyPack, span, any_type);
    result.operands.push_back(std::move(inner));
    return result;
}

IrNode Lowering::pack_expression(const Expr& expr, Context& context, bool call_argument) {
    if (expr.kind == ExprKind::Nil) return pack_any(node(IrOp::Nil, expr.span, symbol(root_class()).type), expr.span);
    return pack_any(value(expr, context, nullptr, call_argument), expr.span);
}

IrNode Lowering::any_tag_of(SymbolId local, const Span& span) {
    auto load = node(IrOp::Load, span, any_type);
    load.symbol = local;
    auto tag = node(IrOp::AnyTag, span, IrType{TypeKind::Integer, "uint32", no_symbol, {}});
    tag.operands.push_back(std::move(load));
    return tag;
}

IrNode Lowering::any_tag_is(SymbolId local, std::uint32_t tag, const Span& span) {
    auto constant = node(IrOp::Integer, span, IrType{TypeKind::Integer, "uint32", no_symbol, {}});
    constant.text = std::to_string(tag);
    auto result = node(IrOp::Binary, span, bool_type);
    result.text = "==";
    result.operands.push_back(any_tag_of(local, span));
    result.operands.push_back(std::move(constant));
    return result;
}

// The value of a constant local of type any as `type`, a reference as the
// root class; only inside a region its tag test selects.
IrNode Lowering::any_unpack(SymbolId local, const IrType& type, const Span& span) {
    auto load = node(IrOp::Load, span, any_type);
    load.symbol = local;
    auto result = node(IrOp::AnyUnpack, span, type.kind == TypeKind::Object ? symbol(root_class()).type : type);
    result.operands.push_back(std::move(load));
    return result;
}

// `value is T` on an `any`: the tag test for a scalar or string type, and
// for a class or interface the passive ClassIs of the reference when the
// tag is that of a reference.
IrNode Lowering::any_type_test(IrNode source, const IrType& target, const Span& span, const Context& context) {
    const auto tag = any_tag(target);
    if (!tag)
        throw Diagnostic(span, "a value of type any is never " + article(shown(target))).with_code("E0490")
            .label("an any never holds a value of type " + shown(target));
    const auto local = hidden_local("$any", any_type, source.span, context);
    auto save = node(IrOp::Initialize, source.span);
    save.symbol = local;
    save.operands.push_back(std::move(source));
    auto result = node(IrOp::ScopedValue, span, bool_type);
    result.body.push_back(std::move(save));
    if (target.kind != TypeKind::Object) {
        result.body.push_back(any_tag_is(local, *tag, span));
        return result;
    }
    auto test = node(IrOp::ClassIs, span, bool_type);
    test.type_arguments.push_back(target);
    test.operands.push_back(any_unpack(local, target, span));
    auto otherwise = node(IrOp::Boolean, span, bool_type);
    otherwise.text = "false";
    auto select = node(IrOp::SelectValue, span, bool_type);
    select.operands.push_back(any_tag_is(local, *tag, span));
    select.body.push_back(std::move(test));
    select.alternative.push_back(std::move(otherwise));
    result.body.push_back(std::move(select));
    return result;
}

// An operation that is not defined for `any` (E0493): no arithmetic,
// member access, comparison, `as`, value member or formatting.
Diagnostic Lowering::any_operation(const Span& span, const std::string& what) const {
    return Diagnostic(span, "operation is not defined for any").with_code("E0493")
        .label(what + " is not defined for any").help("match the value first, with 'match.is'");
}

bool Lowering::lowers_to_any(const Expr& expr, Context& context) {
    SpeculativeDiagnostics speculative;
    try {
        return expression(expr, context).type.kind == TypeKind::Any;
    } catch (const Diagnostic&) {
        return false;
    }
}

// Runs `lower` for an expression whose operands may be an `any`, and turns
// what it did, or the error it gave, into E0493 when one of them is.
IrNode Lowering::any_guard(const Expr& expr, Context& context, const std::function<IrNode()>& lower) {
    std::vector<const Expr*> operands;
    std::string what;
    switch (expr.kind) {
    case ExprKind::Binary:
        operands = {&expr.operands[0], &expr.operands[1]};
        what = "'" + expr.text + "'";
        break;
    case ExprKind::Unary:
    case ExprKind::PrefixUpdate:
    case ExprKind::PostfixUpdate:
        operands = {&expr.operands[0]};
        what = "'" + expr.text + "'";
        break;
    case ExprKind::ComparisonChain:
        for (const auto& operand : expr.operands) operands.push_back(&operand);
        what = "a comparison";
        break;
    case ExprKind::Member:
        operands = {&expr.operands[0]};
        what = "member access";
        break;
    case ExprKind::Index:
        operands = {&expr.operands[0]};
        what = "indexing";
        break;
    case ExprKind::Call:
        if (expr.operands.front().kind != ExprKind::Member) return lower();
        operands = {&expr.operands.front().operands.front()};
        what = "a member call";
        break;
    default:
        return lower();
    }
    IrNode result;
    try {
        result = lower();
    } catch (const Diagnostic& error) {
        if (error.code() == "E0493") throw;
        for (const auto* operand : operands)
            if (lowers_to_any(*operand, context)) throw any_operation(expr.span, what);
        throw;
    }
    if (expr.kind == ExprKind::Binary || expr.kind == ExprKind::Unary || expr.kind == ExprKind::ComparisonChain)
        for (const auto& operand : result.operands)
            if (operand.type.kind == TypeKind::Any) throw any_operation(expr.span, what);
    return result;
}

// An `any` at a boundary that would box it (E0498): a spawned call's
// result, a parallel item, a callback, a host call.
Diagnostic Lowering::any_boundary(const Span& span, const std::string& what) const {
    return Diagnostic(span, "an any cannot be " + what).with_code("E0498")
        .label("this has type any").note("the boundary would box the value; an any is four inline words")
        .help("match the value first and pass what it holds");
}

// A type as a message shows it, with a tuple as `[A, B]`.
std::string Lowering::shown(const IrType& type) const {
    if (type.kind != TypeKind::Tuple) return describe(type);
    std::string text = "[";
    for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? ", " : "") + shown(type.arguments[i]);
    return text + "]";
}

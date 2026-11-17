// Optional access `?.`, coalescing `??` and the postfix `?` that returns an
// error (docs/design/Matching030.md). Included by lowering.cpp after the
// class definition; every function here is a member of Lowering.
//
// `Std.Core.Optional`, `Std.Core.Unit` and `Std.Core.Result` are resolved
// from the trusted library by path, so a file needs no import to use the
// operators.

// The enum `name` of the library module `module` in the file `path`.
SymbolId Lowering::library_enum(std::string_view path, std::string_view module, std::string_view name,
                                const Span& span, const Context& context) {
    const auto found = library_module(path, module, span, context,
                                      "the operator needs Std.Core." + std::string(module));
    const auto result = own_member(found, name);
    if (result == no_symbol || symbol(result).kind != SymbolKind::Enum)
        throw Diagnostic(span, "Std.Core." + std::string(module) + " must export its " + std::string(name) + " enum")
            .with_code("E0899");
    resolve(result);
    return result;
}

SymbolId Lowering::library_variant(SymbolId enumeration, std::string_view name, const Span& span) const {
    const auto result = own_member(enumeration, name);
    if (result == no_symbol || symbol(result).kind != SymbolKind::Variant)
        throw Diagnostic(span, "Std.Core." + symbol(enumeration).name + " must declare the variant " + std::string(name))
            .with_code("E0899");
    return result;
}

// Whether `type` is the library's Optional or Result, or an overlay of it.
bool Lowering::library_enum_type(const IrType& type, std::string_view path, std::string_view module) const {
    if (type.kind != TypeKind::Enum || type.symbol == no_symbol) return false;
    const auto file = library_files_.find(path);
    if (file == library_files_.end()) return false;
    const auto found = own_member(file->second, module);
    return found != no_symbol && own_member(found, module) == type.symbol;
}

IrType Lowering::generic_enum(SymbolId enumeration, const IrType& argument) const {
    const auto& item = symbol(enumeration);
    if (item.generics.size() != 1) return item.type;
    return substitute(item.type, Substitution{{item.generics.front(), argument}});
}

IrNode Lowering::variant_value(SymbolId variant, const IrType& type, std::optional<IrNode> payload, const Span& span) {
    auto result = node(IrOp::EnumConstruct, span, type);
    result.symbol = variant;
    if (payload) result.operands.push_back(std::move(*payload));
    return result;
}

IrNode Lowering::tag_is(const IrNode& value, SymbolId variant, const Span& span) {
    auto tag = node(IrOp::EnumTag, span, IrType{TypeKind::Integer, "uint32", no_symbol, {}});
    tag.operands.push_back(value);
    auto ordinal = node(IrOp::Integer, span, tag.type);
    ordinal.text = std::to_string(enum_ordinal(program_, variant));
    auto result = node(IrOp::Binary, span, bool_type);
    result.text = "==";
    result.operands.push_back(std::move(tag));
    result.operands.push_back(std::move(ordinal));
    return result;
}

SymbolId Lowering::hidden_local(const std::string& name, const IrType& type, const Span& span, const Context& context) {
    auto owner = context.owner;
    if (symbol(owner).kind == SymbolKind::Global || symbol(owner).kind == SymbolKind::Field)
        owner = symbol(owner).owner;
    const auto id = add(SymbolKind::Local, name, span, owner);
    symbol(id).type = type;
    symbol(id).constant = true;
    return id;
}

// The receiver of `?.`: a class or interface reference, or an Optional of
// one. Anything else is E0494.
IrNode Lowering::optional_receiver(const Expr& access, Context& context) {
    const auto& written = access.operands.front();
    IrNode receiver;
    try {
        receiver = value(written, context);
    } catch (const Diagnostic& error) {
        if (error.code() != "E0422" || (written.kind != ExprKind::Name && written.kind != ExprKind::Member)) throw;
        throw Diagnostic(written.span, "optional access on a receiver that is never absent").with_code("E0494")
            .label("this names a module, not a value").help("use '.' to read a member of a module");
    }
    const auto& type = receiver.type;
    if (class_reference(type)) return receiver;
    if (library_enum_type(type, "std/core/optional.nra", "Optional") && type.arguments.size() == 1 &&
        class_reference(type.arguments.front()))
        return receiver;
    auto error = Diagnostic(written.span, "optional access on a receiver that is never absent").with_code("E0494")
        .label(describe(type) + " is not a reference or an Optional of one");
    if (library_enum_type(type, "std/core/result.nra", "Result"))
        error.help("to read a member of the success value, propagate the error first: '(" + source_text(written.span).value_or("r") +
                   "?)." + access.text + "'");
    else
        error.help("use '.' to read the member");
    throw error;
}

// `receiver?.member` and `receiver?.method(arguments)`: the receiver is
// evaluated once; when it is nil, or None, or Some(nil), the result is
// None and nothing else is evaluated; otherwise Some of the member or the
// call's result, or Some(Unit.Value) after a call that returns nothing.
IrNode Lowering::optional_access(const Expr& access, const Expr* call, Context& context) {
    auto receiver = optional_receiver(access, context);
    const auto receiver_type = receiver.type;
    const bool wrapped = receiver_type.kind == TypeKind::Enum;
    const auto reference_type = wrapped ? receiver_type.arguments.front() : receiver_type;
    const auto optional = library_enum("std/core/optional.nra", "Optional", "Optional", access.span, context);
    const auto none = library_variant(optional, "None", access.span);
    const auto some = library_variant(optional, "Some", access.span);

    const auto saved = hidden_local("$optional", receiver_type, receiver.span, context);
    const auto present = wrapped ? hidden_local("$present", reference_type, receiver.span, context) : saved;
    // The member or call, on the present reference, as written.
    context.scopes.emplace_back();
    context.scopes.back().emplace("$receiver", present);
    auto name = Expr{ExprKind::Name, access.operands.front().span, "$receiver", {}, {}, {}};
    auto member = Expr{ExprKind::Member, access.span, access.text, {std::move(name)}, {}, {}};
    IrNode result;
    if (call) {
        auto invocation = *call;
        invocation.operands.front() = std::move(member);
        result = expression(invocation, context);
    } else {
        result = value(member, context);
    }
    context.scopes.pop_back();
    const auto span = call ? call->span : access.span;
    if (result.type.kind == TypeKind::Void) {
        const auto unit = library_enum("std/core/unit.nra", "Unit", "Unit", span, context);
        auto unit_value = node(IrOp::Reference, span, symbol(unit).type);
        unit_value.symbol = library_variant(unit, "Value", span);
        auto then = node(IrOp::ScopedValue, span, symbol(unit).type);
        auto discard = node(IrOp::Discard, span);
        discard.operands.push_back(std::move(result));
        then.body.push_back(std::move(discard));
        then.body.push_back(std::move(unit_value));
        result = std::move(then);
    }
    const auto type = generic_enum(optional, result.type);
    auto load = [&](SymbolId local, const IrType& local_type) {
        auto item = node(IrOp::Load, receiver.span, local_type);
        item.symbol = local;
        return item;
    };
    auto absent = node(IrOp::Binary, access.span, bool_type);
    absent.text = "==";
    absent.operands.push_back(load(present, reference_type));
    absent.operands.push_back(node(IrOp::Nil, access.span, reference_type));
    auto select = node(IrOp::SelectValue, span, type);
    select.operands.push_back(std::move(absent));
    select.body.push_back(variant_value(none, type, std::nullopt, span));
    select.alternative.push_back(variant_value(some, type, std::move(result), span));
    if (wrapped) {
        auto project = node(IrOp::EnumProject, receiver.span, reference_type);
        project.symbol = some;
        project.text = "0";
        project.operands.push_back(load(saved, receiver_type));
        auto inner = node(IrOp::ScopedValue, span, type);
        auto bind = node(IrOp::Initialize, receiver.span);
        bind.symbol = present;
        bind.operands.push_back(std::move(project));
        inner.body.push_back(std::move(bind));
        inner.body.push_back(std::move(select));
        auto outer = node(IrOp::SelectValue, span, type);
        outer.operands.push_back(tag_is(load(saved, receiver_type), none, access.span));
        outer.body.push_back(variant_value(none, type, std::nullopt, span));
        outer.alternative.push_back(std::move(inner));
        select = std::move(outer);
    }
    auto scoped = node(IrOp::ScopedValue, span, type);
    auto save = node(IrOp::Initialize, receiver.span);
    save.symbol = saved;
    save.operands.push_back(std::move(receiver));
    scoped.body.push_back(std::move(save));
    scoped.body.push_back(std::move(select));
    return scoped;
}

// `left ?? right`: `left` is evaluated once; a nil reference or None gives
// `right`, evaluated only then and typed by the expression's type; a live
// reference gives itself, and Some(v) gives v.
IrNode Lowering::coalesce(const Expr& expr, Context& context) {
    const auto& written = expr.operands[0];
    const auto invalid = [&](const Span& at, const std::string& message, const std::string& label) {
        return Diagnostic(at, message).with_code("E0495").label(label);
    };
    if (written.kind == ExprKind::Nil)
        throw invalid(written.span, "invalid left operand of '?\?'", "nil has no type of its own here")
            .help("write the fallback alone");
    auto left = value(written, context);
    const auto left_type = left.type;
    const bool optional = library_enum_type(left_type, "std/core/optional.nra", "Optional") &&
                          left_type.arguments.size() == 1;
    if (!optional && !class_reference(left_type)) {
        auto error = invalid(written.span, "invalid left operand of '?\?'",
                             describe(left_type) + " is not a reference or an Optional");
        error.note("'?\?' gives its right operand when the left one is nil or None");
        throw error;
    }
    const auto type = optional ? left_type.arguments.front() : left_type;
    IrNode right;
    try {
        right = value(expr.operands[1], context, &type);
    } catch (const Diagnostic& error) {
        if (error.code() != "E0403" && error.code() != "E0425") throw;
        throw invalid(expr.operands[1].span, "invalid fallback of '?\?'",
                      "expected " + describe(type) + ", the type of the left operand's value")
            .note("the fallback is not converted; it must be assignable to " + describe(type));
    }
    const auto saved = hidden_local("$coalesce", left_type, left.span, context);
    auto load = node(IrOp::Load, left.span, left_type);
    load.symbol = saved;
    auto select = node(IrOp::SelectValue, expr.span, type);
    if (optional) {
        const auto some = own_member(left_type.symbol, "Some");
        const auto none = own_member(left_type.symbol, "None");
        select.operands.push_back(tag_is(load, none, expr.span));
        auto project = node(IrOp::EnumProject, left.span, type);
        project.symbol = some;
        project.text = "0";
        project.operands.push_back(load);
        select.body.push_back(std::move(right));
        select.alternative.push_back(std::move(project));
    } else {
        auto absent = node(IrOp::Binary, expr.span, bool_type);
        absent.text = "==";
        absent.operands.push_back(load);
        absent.operands.push_back(node(IrOp::Nil, expr.span, left_type));
        select.operands.push_back(std::move(absent));
        select.body.push_back(std::move(right));
        select.alternative.push_back(load);
    }
    auto scoped = node(IrOp::ScopedValue, expr.span, type);
    auto save = node(IrOp::Initialize, left.span);
    save.symbol = saved;
    save.operands.push_back(std::move(left));
    scoped.body.push_back(std::move(save));
    scoped.body.push_back(std::move(select));
    return scoped;
}

// `a?.b.c` applies `.c` to an Optional: the unknown member keeps its error
// (E0305), with the help to continue the chain with `?.`.
IrNode Lowering::member_of_optional(const Expr& member, Context& context, bool for_call) {
    try {
        return select(member, context, for_call);
    } catch (const Diagnostic& error) {
        if (error.code() != "E0305" && error.code() != "E0427") throw;
        const auto receiver = optional_access(member.operands.front(), nullptr, context);
        throw Diagnostic(member.span, "type " + describe(receiver.type) + " has no member '" + member.text + "'")
            .with_code("E0305").label("an optional access gives an Optional")
            .help("continue the chain with '?." + member.text + "', which skips when the value is absent");
    }
}

// `defer`, `using` and generators (docs/design/Lifetime030.md). Included by
// lowering.cpp after the Lowering class. A block's exit work is an
// IrOp::Cleanup statement of that block; the converter emits it at every
// exit (backend/cleanup_stack.inc).

// `defer` and `using` need a block of a function body to leave: not file
// level, and not a class's startup block. A lambda, a constructor and a
// destructor are function bodies.
void Lowering::require_function_block(const Span& keyword, std::string_view word, const Context& context) {
    const bool function = context.function != no_symbol && !is_static_initializer(symbol(context.function));
    if (function) return;
    throw Diagnostic(keyword, "'" + std::string(word) + "' needs a block of a function to leave").with_code("E0472")
        .label(context.function == no_symbol ? "this is outside every function"
                                             : "this is in a startup block, which is not a function body")
        .help("move the statements into a function");
}

namespace {

// An operand a deferred call may read when it runs instead of when it is
// registered, because evaluating it has no effect and gives the same value
// at both times: a literal, a module, or a primitive reference, which
// designates a place that the call reads when it runs.
bool stable_operand(const IrNode& operand) {
    switch (operand.op) {
    case IrOp::Integer:
    case IrOp::Float:
    case IrOp::String:
    case IrOp::Boolean:
    case IrOp::Nil:
        return true;
    default:
        return operand.type.kind == TypeKind::Module || is_primitive_reference(operand.type);
    }
}

} // namespace

// Replaces each operand of `call` that has to be evaluated by a load of a
// hidden local `$defer<N>_<k>`, whose initialization is appended to `saved`
// in written order. A reordered call (named arguments) or a qualified one
// has its prelude moved to `saved` as it stands.
void Lowering::hoist_operands(IrNode& call, std::vector<IrNode>& saved, std::size_t number, const Context& context) {
    while (call.op == IrOp::ScopedValue && !call.body.empty()) {
        auto inner = std::move(call.body.back());
        call.body.pop_back();
        for (auto& statement : call.body) saved.push_back(std::move(statement));
        call = std::move(inner);
    }
    for (auto& operand : call.operands) {
        if (stable_operand(operand)) continue;
        const auto local = hidden_local("$defer" + std::to_string(number) + "_" + std::to_string(saved.size()),
                                        operand.type, operand.span, context);
        auto save = node(IrOp::Initialize, operand.span);
        save.symbol = local;
        auto load = node(IrOp::Load, operand.span, operand.type);
        load.symbol = local;
        save.operands.push_back(std::exchange(operand, std::move(load)));
        saved.push_back(std::move(save));
    }
}

// `defer call;`: the callee, the receiver and the arguments are evaluated
// now, in written order, into hidden locals, and the call over them is the
// Cleanup's list, which runs at every exit of the enclosing block.
void Lowering::lower_defer(std::vector<IrNode>& instructions, const Statement& stmt, Context& context) {
    require_function_block(stmt.keyword, "defer", context);
    const auto& syntax = stmt.expressions.front();
    auto call = expression(syntax, context);
    if (call.type.kind != TypeKind::Void) {
        auto error = Diagnostic(syntax.span, "a deferred call returns nothing, and this one returns '" +
                                                 describe(call.type) + "'")
            .with_code("E0471");
        auto target = call;
        while (target.op == IrOp::ScopedValue && !target.body.empty()) target = target.body.back();
        const bool named = target.op == IrOp::Call || target.op == IrOp::VirtualCall || target.op == IrOp::ExternalCall;
        if (named && result_spans_.contains(target.symbol))
            error.label(result_spans_.at(target.symbol), "the callee returns '" + describe(call.type) + "'");
        else
            error.label("this call returns '" + describe(call.type) + "'");
        error.note("a deferred call's result would be lost when its block is left");
        error.help(library_enum_type(call.type, "std/core/result.nra", "Result")
                       ? "defer a function that handles the result, or use 'using' for a Closeable"
                       : "defer a function that handles the result");
        throw error;
    }
    const auto number = ++lifetime_statements_;
    std::vector<IrNode> saved;
    hoist_operands(call, saved, number, context);
    for (auto& item : saved) instructions.push_back(std::move(item));
    auto run = node(IrOp::Discard, syntax.span);
    run.operands.push_back(std::move(call));
    auto cleanup = node(IrOp::Cleanup, stmt.span);
    cleanup.body.push_back(std::move(run));
    instructions.push_back(std::move(cleanup));
}

// `using (name = acquisition) { body }` (docs/design/Lifetime030.md,
// `using`): a block that binds the resource, fails with the nil-reference
// failure when it is nil, saves it in the token `$using<N>`, and registers
// a Cleanup that closes the token at every exit of the block. On a return
// the close is combined with the returned value by close_outcome
// (std/io/internal/close-outcome.nra); on any other exit a failed close returns
// its error from the function.
IrNode Lowering::lower_using(const Statement& stmt, Context& context) {
    require_function_block(stmt.keyword, "using", context);
    const auto function = context.function;
    const auto result_type = symbol(function).type;
    if (!library_enum_type(result_type, "std/core/result.nra", "Result")) {
        auto error = Diagnostic(stmt.keyword, "'using' needs a function that returns 'Result<T>'").with_code("E0473");
        if (generator_bodies_.contains(function))
            error.label("a generator returns its values through 'yield'");
        else if (const auto written = result_spans_.find(function); written != result_spans_.end())
            error.label(written->second, "the function returns '" + describe(result_type) + "'");
        else
            error.label(result_type.kind == TypeKind::Void ? std::string("the function returns no result")
                                                           : "the function returns '" + describe(result_type) + "'");
        error.note("a failed close returns its error from the function");
        throw error.help("return a Result, or move the block into a function or lambda that returns one");
    }
    const auto& acquisition = stmt.expressions.front();
    if (acquisition.kind == ExprKind::Nil)
        throw Diagnostic(acquisition.span, "the resource is nil").with_code("E0474")
            .label("'using' closes the object it binds, and nil has nothing to close");
    const auto closeable = library_module("std/io/closeable.nra", "Closeable", acquisition.span, context,
                                          "'using' needs Std.Io.Closeable");
    const auto module = library_module("std/io/internal/close-outcome.nra", "CloseOutcome", acquisition.span,
                                       context, "'using' needs Std.Io.Internal.CloseOutcome");
    const auto interface = own_member(closeable, "Closeable");
    const auto combine = own_member(module, "close_outcome");
    if (interface == no_symbol || symbol(interface).kind != SymbolKind::Interface || combine == no_symbol)
        throw Diagnostic(acquisition.span, "the library must export Closeable and close_outcome").with_code("E0899");
    resolve(interface);
    resolve(combine);
    auto acquired = value(acquisition, context);
    const bool reference = acquired.type.kind == TypeKind::Object && !is_primitive_reference(acquired.type);
    if (acquired.op == IrOp::Nil || !reference || !assignable(acquired.type, symbol(interface).type))
        throw Diagnostic(acquisition.span, acquired.op == IrOp::Nil ? std::string("the resource is nil")
                                                                    : "'" + describe(acquired.type) +
                                                                          "' does not implement Closeable")
            .with_code("E0474")
            .label("this has type '" + describe(acquired.type) + "'")
            .help("bind an object whose class implements Std.Io.Closeable.Closeable");
    const auto number = ++lifetime_statements_;
    const auto& name = stmt.bindings.front();
    const auto close_span = source_span(stmt.span.file, stmt.span.offset + stmt.span.length - 1, 1).value_or(stmt.span);
    auto block = node(IrOp::Block, stmt.span);
    context.scopes.emplace_back();
    // The binding, admitted as any binding is; it is immutable.
    const auto binding = add(SymbolKind::Local, name.text, name.span, context.owner);
    symbol(binding).constant = true;
    symbol(binding).type = acquired.type;
    context.scopes.back().emplace(name.text, binding);
    const auto load = [&](SymbolId local, const Span& span) {
        auto result = node(IrOp::Load, span, symbol(local).type);
        result.symbol = local;
        return result;
    };
    auto initialize = node(IrOp::Initialize, name.span);
    initialize.symbol = binding;
    initialize.operands.push_back(std::move(acquired));
    block.body.push_back(std::move(initialize));
    // A nil resource fails as a nil reference does, before anything is
    // registered: in the branch where it is nil, reading its class hash
    // faults at once and has no other effect.
    auto absent = node(IrOp::Binary, acquisition.span, bool_type);
    absent.text = "==";
    absent.operands.push_back(load(binding, acquisition.span));
    absent.operands.push_back(node(IrOp::Nil, acquisition.span, symbol(binding).type));
    auto fault = node(IrOp::ClassHash, acquisition.span, uint32_type);
    fault.operands.push_back(load(binding, acquisition.span));
    auto faults = node(IrOp::Discard, acquisition.span);
    faults.operands.push_back(std::move(fault));
    auto check = node(IrOp::Branch, acquisition.span);
    check.operands.push_back(std::move(absent));
    check.body.push_back(std::move(faults));
    block.body.push_back(std::move(check));
    // The token: `close` is called on it, never on a later read of the
    // binding.
    const auto token_name = "$using" + std::to_string(number);
    const auto token = hidden_local(token_name, symbol(binding).type, stmt.keyword, context);
    auto save = node(IrOp::Initialize, stmt.keyword);
    save.symbol = token;
    save.operands.push_back(load(binding, stmt.keyword));
    block.body.push_back(std::move(save));
    context.scopes.back().emplace(token_name, token);
    // token.close(), lowered as the source call would be.
    const auto close_call = [&]() {
        Expr receiver{ExprKind::Name, close_span, token_name, {}, {}, std::nullopt};
        Expr member{ExprKind::Member, close_span, "close", {}, {}, std::nullopt};
        member.operands.push_back(std::move(receiver));
        Expr call{ExprKind::Call, close_span, "", {}, {}, std::nullopt};
        call.operands.push_back(std::move(member));
        call.open = close_span;
        return value(call, context);
    };
    const auto err = library_variant(result_type.symbol, "Err", close_span);
    auto cleanup = node(IrOp::Cleanup, close_span);
    {
        // Falling off the end, break or continue: a failed close returns its
        // error from the function.
        auto closing = close_call();
        const auto closing_type = closing.type;
        const auto closed = hidden_local("$closed" + std::to_string(number), closing_type, close_span, context);
        auto keep = node(IrOp::Initialize, close_span);
        keep.symbol = closed;
        keep.operands.push_back(std::move(closing));
        cleanup.body.push_back(std::move(keep));
        const auto closed_err = library_variant(closing_type.symbol, "Err", close_span);
        auto failure = node(IrOp::EnumProject, close_span, symbol(closed_err).payload_types.front());
        failure.symbol = closed_err;
        failure.text = "0";
        failure.operands.push_back(load(closed, close_span));
        auto leave = node(IrOp::Return, close_span);
        leave.operands.push_back(variant_value(err, result_type, std::move(failure), close_span));
        auto failed = node(IrOp::Branch, close_span);
        failed.operands.push_back(tag_is(load(closed, close_span), closed_err, close_span));
        failed.body.push_back(std::move(leave));
        cleanup.body.push_back(std::move(failed));
    }
    {
        // A return: the returned value combined with the close.
        auto combined = node(IrOp::VirtualCall, close_span, result_type);
        combined.symbol = combine;
        combined.type_arguments.push_back(result_type.arguments.front());
        auto receiver = node(IrOp::Reference, close_span, symbol(module).type);
        receiver.symbol = module;
        combined.operands.push_back(std::move(receiver));
        combined.operands.push_back(node(IrOp::Pending, close_span, result_type));
        combined.operands.push_back(close_call());
        auto replace = node(IrOp::Store, close_span);
        replace.operands.push_back(node(IrOp::Pending, close_span, result_type));
        replace.operands.push_back(std::move(combined));
        cleanup.alternative.push_back(std::move(replace));
    }
    block.body.push_back(std::move(cleanup));
    context.scopes.back().erase(token_name);
    for (const auto& inner : stmt.body) append_statement(block.body, inner, context);
    context.scopes.pop_back();
    return block;
}

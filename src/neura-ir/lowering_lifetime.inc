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

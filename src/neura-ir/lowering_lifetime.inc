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
namespace {
bool frame_owned(const IrProgram& program, const IrType& type, unsigned depth);
} // namespace

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
    // A return's value is combined with the close by close_outcome, which
    // takes the value as an argument: an array it carries would be returned
    // from a call that did not create it (docs/design/Storage030.md).
    if (!result_type.arguments.empty() && frame_owned(program_, result_type.arguments.front(), 0)) {
        auto error = Diagnostic(stmt.keyword, "'using' cannot carry the returned '" +
                                                  describe(result_type) + "' through its close").with_code("E0473");
        if (const auto written = result_spans_.find(function); written != result_spans_.end())
            error.label(written->second, "this result holds storage that belongs to the frame");
        error.note("a return inside 'using' passes its value to the close, which may replace it with the close error");
        throw error.help("return a list, which the result can hold, or close the resource before building the array");
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

// Generators (docs/design/Lifetime030.md, Generators). A generator is a
// function that returns Std.Collections.Sequence<T>: its call allocates the
// state class `$generator<N>`, which implements Iterator<T>, and wraps it
// with Sequence.from_iterator. The class is a closure layout, so the body,
// lowered as its method `$resume() -> bool`, captures outer bindings and
// `instance` into fields as a lambda does; its parameters, and every local
// whose scope contains a `yield`, then move into fields
// (lower_generator_body), and `has_next` and `next` are generated over it.

namespace {

// Storage owned by the frame cannot survive a suspension: an array, a
// primitive reference, or a tuple or enum that contains one.
bool frame_owned(const IrProgram& program, const IrType& type, unsigned depth = 0) {
    if (depth > 8) return false;
    if (type.kind == TypeKind::Array || is_primitive_reference(type)) return true;
    if (type.kind != TypeKind::Tuple && type.kind != TypeKind::Enum) return false;
    if (std::ranges::any_of(type.arguments, [&](const IrType& item) { return frame_owned(program, item, depth + 1); }))
        return true;
    if (type.kind == TypeKind::Enum && type.symbol < program.symbols.size() && type.arguments.empty())
        for (const auto& item : program.symbols)
            if (item.kind == SymbolKind::Variant && item.owner == type.symbol &&
                std::ranges::any_of(item.payload_types,
                                    [&](const IrType& payload) { return frame_owned(program, payload, depth + 1); }))
                return true;
    return false;
}


} // namespace

IrType Lowering::generator_sequence(const IrType& element, const Span& span, const Context& context) {
    if (element.kind == TypeKind::Void)
        throw Diagnostic(span, "a generator's element type cannot be void").with_code("E0416");
    const auto module = library_module("std/collections/sequence.nra", "Sequence", span, context,
                                       "a generator needs Std.Collections.Sequence");
    const auto sequence = own_member(module, "Sequence");
    if (sequence != no_symbol) resolve(sequence);
    if (sequence == no_symbol || symbol(sequence).generics.size() != 1)
        throw Diagnostic(span, "Std.Collections.Sequence must export its Sequence class").with_code("E0899");
    return substitute(symbol(sequence).type, Substitution{{symbol(sequence).generics.front(), element}});
}

IrNode Lowering::generator_lambda(const Declaration& declaration, Context& context) {
    const auto& span = declaration.span;
    std::vector<IrType> signature;
    std::vector<std::string> names;
    for (const auto& parameter : declaration.parameters) {
        if (parameter.default_value)
            throw Diagnostic(*parameter.default_span, "a parameter of a generator takes no default").with_code("E0226")
                .label("a generator is called through a function value, which carries no defaults");
        if (parameter.variadic)
            throw Diagnostic(parameter.span, "a variadic parameter requires a named function").with_code("E0207");
        signature.push_back(parameter_type(parameter, context));
        names.push_back(parameter.name);
        check_tags(parameter.tags, TagTarget::Parameter, parameter.span);
    }
    signature.push_back(type(*declaration.type, context));
    const auto callable = callable_type(std::move(signature), span);
    ClosureEnvironment environment{closure_layout(span, context), context, {}, {}};
    const auto method = closure_method(environment.layout, callable, span, names);
    Context inner{method, method, context.receiver, 0, {entries_[method].names}, &environment};
    check_definite_assignment(declaration, no_symbol);
    auto instructions = generator_factory(declaration, inner, method);
    program_.bodies.push_back({method, std::move(instructions)});
    return closure_value(callable, environment, span);
}

std::vector<IrNode> Lowering::generator_factory(const Declaration& declaration, Context& context, SymbolId factory) {
    const auto& span = declaration.span;
    const auto sequence_type = symbol(factory).type;
    const auto element = sequence_type.arguments.front();
    const auto& factory_parameters = symbol(factory).parameters;
    for (std::size_t i = 0; i < factory_parameters.size() && i < declaration.parameters.size(); ++i)
        if (frame_owned(program_, symbol(factory_parameters[i]).type))
            throw Diagnostic(declaration.parameters[i].type.span,
                             "a generator's parameter cannot have type '" + describe(symbol(factory_parameters[i]).type) +
                                 "'")
                .with_code("E0477")
                .label("this storage belongs to the frame that calls the generator, which a 'yield' outlives")
                .help("use a list, which the generator's state can hold");
    const auto number = ++generator_count_;
    ClosureEnvironment environment{closure_layout(span, context), context, {}, {}};
    const auto cls = environment.layout;
    symbol(cls).name = "$generator" + std::to_string(number);
    symbol(cls).type.name = symbol(cls).name;
    symbol(cls).base = root_class();
    const auto iterators = library_module("std/collections/iterator.nra", "Iterator", span, context,
                                          "a generator needs Std.Collections.Iterator");
    const auto iterator = own_member(iterators, "Iterator");
    if (iterator != no_symbol) resolve(iterator);
    if (iterator == no_symbol || symbol(iterator).generics.size() != 1)
        throw Diagnostic(span, "Std.Collections.Iterator must export its Iterator interface").with_code("E0899");
    const auto iterator_type = substitute(symbol(iterator).type, Substitution{{symbol(iterator).generics.front(), element}});
    symbol(cls).interfaces.push_back(iterator_type);
    const auto member = [&](SymbolKind kind, const std::string& name, const IrType& type) {
        const auto id = add(kind, name, span, cls);
        symbol(id).type = type;
        entries_[cls].names.emplace(name, id);
        return id;
    };
    GeneratorState state;
    state.cls = cls;
    state.index = member(SymbolKind::Field, "$index", int32_type);
    state.buffer = member(SymbolKind::Field, "$buffer", element);
    state.buffered = member(SymbolKind::Field, "$buffered", bool_type);
    state.finished = member(SymbolKind::Field, "$finished", bool_type);
    state.running = member(SymbolKind::Field, "$running", bool_type);
    const auto resume = member(SymbolKind::Function, "$resume", bool_type);
    symbol(resume).has_body = true;
    set_attribute(symbol(resume), AttributeId::GeneratorResume, attribute_symbol(state.index));
    generator_bodies_.insert(resume);
    // The body, with the parameters as its outermost locals.
    Context inner{resume, resume, context.receiver, 0, {{}}, &environment};
    for (std::size_t i = 0; i < factory_parameters.size(); ++i) {
        const auto& name = symbol(factory_parameters[i]).name;
        const auto local = add(SymbolKind::Local, name, symbol(factory_parameters[i]).span, resume);
        symbol(local).type = symbol(factory_parameters[i]).type;
        inner.scopes.back().emplace(name, local);
        state.parameters.push_back(local);
    }
    generator_states_.emplace(resume, state);
    auto instructions = body(declaration.body, inner);
    lower_generator_body(resume, instructions);
    program_.bodies.push_back({resume, std::move(instructions)});
    const auto& finished = generator_states_.at(resume);
    // The constructor: the parameters, then the captures; the index and
    // the flags start at 0 and false.
    const auto init = member(SymbolKind::Function, "init", void_type);
    symbol(init).has_body = true;
    std::vector<IrNode> setup;
    auto allocation = node(IrOp::Allocate, span, symbol(cls).type);
    allocation.symbol = init;
    const auto take = [&](SymbolId field, const std::string& name, IrNode input) {
        const auto parameter = add(SymbolKind::Parameter, name, span, init);
        symbol(parameter).type = symbol(field).type;
        symbol(init).parameters.push_back(parameter);
        auto load = node(IrOp::Load, span, symbol(parameter).type);
        load.symbol = parameter;
        auto store = node(IrOp::Store, span);
        store.operands = {closure_field(field, span), std::move(load)};
        setup.push_back(std::move(store));
        allocation.operands.push_back(std::move(input));
    };
    for (std::size_t i = 0; i < factory_parameters.size(); ++i) {
        auto input = node(IrOp::Load, span, symbol(factory_parameters[i]).type);
        input.symbol = factory_parameters[i];
        take(finished.moved.at(finished.parameters[i]), symbol(factory_parameters[i]).name, std::move(input));
    }
    for (auto& [field, input] : environment.inputs) take(field, symbol(field).name, std::move(input));
    const auto set = [&](std::vector<IrNode>& into, SymbolId field, IrNode value) {
        auto store = node(IrOp::Store, span);
        store.operands = {closure_field(field, span), std::move(value)};
        into.push_back(std::move(store));
    };
    const auto literal = [&](IrOp op, const IrType& type, std::string text) {
        auto result = node(op, span, type);
        result.text = std::move(text);
        return result;
    };
    set(setup, state.index, literal(IrOp::Integer, int32_type, "0"));
    for (const auto flag : {state.buffered, state.finished, state.running})
        set(setup, flag, literal(IrOp::Boolean, bool_type, "false"));
    program_.bodies.push_back({init, std::move(setup)});
    // has_next and next. Advancing runs the body to its next yield or its
    // end, unless a value is buffered or the body has ended.
    const auto read = [&](SymbolId field) { return closure_field(field, span); };
    const auto negated = [&](IrNode operand) {
        auto result = node(IrOp::Unary, span, bool_type);
        result.text = "!";
        result.operands.push_back(std::move(operand));
        return result;
    };
    const auto trap = [&](std::string message) {
        auto result = node(IrOp::TrapMessage, span);
        result.operands.push_back(literal(IrOp::String, string_type, std::move(message)));
        return result;
    };
    const auto advance = [&](SymbolId method) {
        auto idle = node(IrOp::ShortCircuit, span, bool_type);
        idle.text = "&&";
        idle.operands.push_back(negated(read(state.buffered)));
        idle.body.push_back(negated(read(state.finished)));
        auto step = node(IrOp::Branch, span);
        step.operands.push_back(std::move(idle));
        auto reentered = node(IrOp::Branch, span);
        reentered.operands.push_back(read(state.running));
        reentered.body.push_back(trap("invalid state: generator already running"));
        step.body.push_back(std::move(reentered));
        set(step.body, state.running, literal(IrOp::Boolean, bool_type, "true"));
        const auto more = add(SymbolKind::Local, "$more", span, method);
        symbol(more).type = bool_type;
        symbol(more).constant = true;
        auto call = node(IrOp::VirtualCall, span, bool_type);
        call.symbol = resume;
        auto self = node(IrOp::Receiver, span, symbol(cls).type);
        self.symbol = cls;
        call.operands.push_back(std::move(self));
        auto keep = node(IrOp::Initialize, span);
        keep.symbol = more;
        keep.operands.push_back(std::move(call));
        step.body.push_back(std::move(keep));
        set(step.body, state.running, literal(IrOp::Boolean, bool_type, "false"));
        auto outcome = node(IrOp::Branch, span);
        auto test = node(IrOp::Load, span, bool_type);
        test.symbol = more;
        outcome.operands.push_back(std::move(test));
        set(outcome.body, state.buffered, literal(IrOp::Boolean, bool_type, "true"));
        set(outcome.alternative, state.finished, literal(IrOp::Boolean, bool_type, "true"));
        step.body.push_back(std::move(outcome));
        return step;
    };
    const auto method = [&](const std::string& name, const IrType& type) {
        const auto id = member(SymbolKind::Function, name, type);
        symbol(id).has_body = true;
        symbol(id).exported = true;
        return id;
    };
    const auto has_next = method("has_next", bool_type);
    {
        std::vector<IrNode> code;
        code.push_back(advance(has_next));
        auto result = node(IrOp::Return, span);
        result.operands.push_back(read(state.buffered));
        code.push_back(std::move(result));
        program_.bodies.push_back({has_next, std::move(code)});
    }
    const auto next = method("next", element);
    {
        std::vector<IrNode> code;
        code.push_back(advance(next));
        auto empty = node(IrOp::Branch, span);
        empty.operands.push_back(negated(read(state.buffered)));
        empty.body.push_back(trap("no such element"));
        code.push_back(std::move(empty));
        set(code, state.buffered, literal(IrOp::Boolean, bool_type, "false"));
        auto result = node(IrOp::Return, span);
        result.operands.push_back(read(state.buffer));
        code.push_back(std::move(result));
        program_.bodies.push_back({next, std::move(code)});
    }
    // The call: the state in a hidden local, then the body of
    // Sequence.from_iterator<T> written out, new Sequence<T>(func() -> bool
    // { return state.has_next(); }, func() -> T { return state.next(); }).
    // Written out, a generator's call is one call shallower for tier 3,
    // whose verifier rejects from_iterator inlined two calls deep.
    std::vector<IrNode> code;
    const auto held = hidden_local("$state", symbol(cls).type, span, context);
    auto hold = node(IrOp::Initialize, span);
    hold.symbol = held;
    hold.operands.push_back(std::move(allocation));
    code.push_back(std::move(hold));
    const auto forward = [&](SymbolId target, const IrType& result) {
        ClosureEnvironment closure{closure_layout(span, context), context, {}, {}};
        const auto callable = callable_type({result}, span);
        const auto invoke = closure_method(closure.layout, callable, span, {});
        auto load = node(IrOp::Load, span, symbol(cls).type);
        load.symbol = held;
        auto call = node(IrOp::VirtualCall, span, result);
        call.symbol = target;
        call.operands.push_back(capture(closure, held, std::move(load), span));
        auto give = node(IrOp::Return, span);
        give.operands.push_back(std::move(call));
        std::vector<IrNode> body;
        body.push_back(std::move(give));
        program_.bodies.push_back({invoke, std::move(body)});
        // The sequence keeps the closure, so it owns its capture of the
        // state (docs/design/Storage030.md, owned closures).
        auto value = closure_value(callable, closure, span);
        value.text = owned_lambda;
        return value;
    };
    const auto sequence_class = sequence_type.symbol;
    const auto sequence_init = own_member(sequence_class, "init");
    if (sequence_init == no_symbol)
        throw Diagnostic(span, "Std.Collections.Sequence must declare its constructor").with_code("E0899");
    resolve(sequence_init);
    auto wrap = node(IrOp::Allocate, span, sequence_type);
    wrap.symbol = sequence_init;
    wrap.operands.push_back(forward(has_next, bool_type));
    wrap.operands.push_back(forward(next, element));
    auto result = node(IrOp::Return, span);
    result.operands.push_back(std::move(wrap));
    code.push_back(std::move(result));
    return code;
}

// `yield value;`: the value is admitted as the element, stored in the
// buffer, and the body suspends with the next index.
IrNode Lowering::lower_yield(const Statement& stmt, Context& context) {
    const auto found = generator_states_.find(context.function);
    if (found == generator_states_.end())
        throw Diagnostic(stmt.keyword, "'yield' is only a statement in the body of an 'iterator'").with_code("E0222");
    auto& state = found->second;
    const auto& element = symbol(state.buffer).type;
    auto value = this->value(stmt.expressions.front(), context, &element);
    auto block = node(IrOp::Block, stmt.span);
    auto store = node(IrOp::Store, stmt.span);
    store.operands = {closure_field(state.buffer, stmt.span), std::move(value)};
    block.body.push_back(std::move(store));
    auto suspend = node(IrOp::Yield, stmt.keyword);
    suspend.text = std::to_string(++state.yields);
    block.body.push_back(std::move(suspend));
    return block;
}

// `return;` ends the generator: `$resume` returns false. A value is E0478.
IrNode Lowering::generator_return(const Statement& stmt) {
    if (!stmt.expressions.empty()) {
        const auto& value = stmt.expressions.front();
        auto error = Diagnostic(value.span, "a generator's values come from 'yield'; 'return' only ends it")
            .with_code("E0478");
        if (const auto text = source_text(value.span))
            error.suggest("yield the value, then end the generator", stmt.span, "yield " + *text + "; return;",
                          Applicability::MaybeIncorrect);
        throw error;
    }
    auto result = node(IrOp::Return, stmt.span);
    auto done = node(IrOp::Boolean, stmt.span, bool_type);
    done.text = "false";
    result.operands.push_back(std::move(done));
    return result;
}

// After the body of `$resume` is lowered: the end of the body returns false;
// a yield with a deferred call or a task scope pending is E0476; every
// parameter, and every local whose scope contains a yield, moves into a
// field of the state (`$local<M>`, or `$temp<M>` for a hidden local), and a
// moved local that the frame owns is E0477.
void Lowering::lower_generator_body(SymbolId resume, std::vector<IrNode>& instructions) {
    auto& state = generator_states_.at(resume);
    if (!always_returns(instructions)) {
        Span end = symbol(resume).span;
        auto result = node(IrOp::Return, end);
        auto done = node(IrOp::Boolean, end, bool_type);
        done.text = "false";
        result.operands.push_back(std::move(done));
        instructions.push_back(std::move(result));
    }
    // Pending cleanups and the first yield of each local's scope.
    std::map<SymbolId, const IrNode*> moved_at;
    std::vector<const IrNode*> pending;
    const auto first_yield = [](const auto& self, std::span<const IrNode> nodes) -> const IrNode* {
        for (const auto& item : nodes) {
            if (item.op == IrOp::Yield) return &item;
            for (const auto* region : {&item.body, &item.alternative})
                if (const auto* found = self(self, std::span<const IrNode>(*region))) return found;
        }
        return nullptr;
    };
    const auto scan = [&](const auto& self, const std::vector<IrNode>& region) -> void {
        const auto depth = pending.size();
        for (std::size_t i = 0; i < region.size(); ++i) {
            const auto& item = region[i];
            if (item.op == IrOp::Yield && !pending.empty()) {
                const auto& construct = *pending.back();
                throw Diagnostic(item.span, std::string("a generator cannot suspend while ") +
                                                (construct.op == IrOp::TaskScope ? "a task scope"
                                                                                 : "a deferred call") +
                                                " is pending")
                    .with_code("E0476")
                    .label(construct.span, construct.op == IrOp::TaskScope
                                               ? "this scope would have to wait for its tasks across the 'yield'"
                                               : "this call runs when its block is left, after the 'yield'")
                    .help("end the block before the 'yield', or move the work into a function");
            }
            if (item.op == IrOp::Initialize && item.symbol < program_.symbols.size() &&
                symbol(item.symbol).kind == SymbolKind::Local) {
                const auto rest = std::span<const IrNode>(region).subspan(i + 1);
                if (const auto* yield = first_yield(first_yield, rest)) moved_at.emplace(item.symbol, yield);
            }
            if (item.op == IrOp::Cleanup) pending.push_back(&item);
            if (item.op == IrOp::TaskScope) {
                pending.push_back(&item);
                self(self, item.body);
                pending.pop_back();
                continue;
            }
            if (item.op == IrOp::Block || item.op == IrOp::Branch || item.op == IrOp::Loop) {
                self(self, item.body);
                self(self, item.alternative);
            }
        }
        pending.resize(depth);
    };
    scan(scan, instructions);
    const auto* body_yield = first_yield(first_yield, std::span<const IrNode>(instructions));
    for (const auto parameter : state.parameters) moved_at.emplace(parameter, body_yield);
    for (const auto& [local, yield] : moved_at) {
        if (!frame_owned(program_, symbol(local).type)) continue;
        if (!yield) continue;
        throw Diagnostic(yield->span, "'" + symbol(local).name + "' belongs to this frame and would not survive a 'yield'")
            .with_code("E0477")
            .label(symbol(local).span, "'" + symbol(local).name + "' has type '" + describe(symbol(local).type) + "'")
            .help("use a list, which the generator's state can hold, or end the block before the 'yield'");
    }
    // Move the locals into fields.
    std::size_t locals = 0;
    std::size_t temps = 0;
    for (const auto& [local, yield] : moved_at) {
        // A moved source local keeps its name after the index, for the
        // debugger, which shows a generator's state by source names.
        const bool hidden = symbol(local).name.starts_with('$');
        const auto name = hidden ? "$temp" + std::to_string(temps++)
                                 : "$local" + std::to_string(locals++) + ":" + symbol(local).name;
        const auto field = add(SymbolKind::Field, name, symbol(local).span, state.cls);
        symbol(field).type = symbol(local).type;
        entries_[state.cls].names.emplace(name, field);
        state.moved.emplace(local, field);
    }
    const auto rewrite = [&](const auto& self, std::vector<IrNode>& nodes) -> void {
        for (auto it = nodes.begin(); it != nodes.end();) {
            auto& item = *it;
            if (item.op == IrOp::ReferencePlace && state.moved.contains(item.symbol))
                throw Diagnostic(item.span, "'" + symbol(item.symbol).name +
                                                "' lives across a 'yield' and cannot be passed by reference")
                    .with_code("E0477")
                    .label(symbol(item.symbol).span, "declared here")
                    .help("pass a copy, or end the block before the 'yield'");
            if (item.op == IrOp::Initialize && state.moved.contains(item.symbol)) {
                if (item.operands.empty()) {
                    it = nodes.erase(it);
                    continue;
                }
                auto store = node(IrOp::Store, item.span);
                store.operands = {closure_field(state.moved.at(item.symbol), item.span), std::move(item.operands.front())};
                item = std::move(store);
            } else if (item.op == IrOp::Load && item.operands.empty() && state.moved.contains(item.symbol)) {
                item = closure_field(state.moved.at(item.symbol), item.span);
            }
            for (auto* region : {&item.operands, &item.body, &item.alternative}) self(self, *region);
            ++it;
        }
    };
    rewrite(rewrite, instructions);
}

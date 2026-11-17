// Named arguments and constant defaults (docs/design/Calls030.md). This
// file is included by lowering.cpp after the class declaration; the members
// it defines are declared there under "Calls".
//
// A call names its parameters by label after its positional arguments.
// Arguments are mapped to parameters here, once per alternative of an
// overload group, and assembled in parameter order. A call whose written
// order is the parameter order lowers to the same node as the positional
// call; any other named call evaluates each written argument into a hidden
// constant local, in written order, and then makes the call with loads of
// those locals in parameter order.

// The span of an argument with its label, if it has one.
Span Lowering::argument_span(const Expr& expr, std::size_t index) {
    const auto& value = expr.operands[index];
    if (expr.labels.empty() || expr.labels[index].empty()) return value.span;
    const auto& label = expr.label_spans[index];
    return {label.file, label.line, label.column, label.offset, value.span.offset + value.span.length - label.offset};
}

void Lowering::check_label_order(const Expr& expr, std::size_t first, SymbolId target) const {
    if (expr.labels.empty()) return;
    std::optional<std::size_t> named;
    for (std::size_t i = first; i < expr.operands.size(); ++i) {
        if (!expr.labels[i].empty()) {
            if (!named) named = i;
            continue;
        }
        if (!named) continue;
        auto error = Diagnostic(expr.operands[i].span, "a positional argument cannot follow a named one")
            .with_code("E0225")
            .label("this argument has no label")
            .label(expr.label_spans[*named], "arguments are named from this one on");
        // The parameter it would most likely fill: the first one that the
        // positional arguments before the first label and the labels leave.
        std::optional<std::string> name;
        if (target != no_symbol && symbol(target).kind == SymbolKind::Function) {
            const auto& params = symbol(target).parameters;
            for (std::size_t p = *named - first; p < params.size() && !name; ++p) {
                const auto& candidate = symbol(params[p]).name;
                if (std::ranges::find(expr.labels, candidate) == expr.labels.end()) name = candidate;
            }
        }
        if (name) {
            const auto at = source_span(expr.operands[i].span.file, expr.operands[i].span.offset, 0);
            if (at) error.suggest("name it", *at, *name + ": ", Applicability::MaybeIncorrect);
            else error.help("name it, as in '" + *name + ": ...'");
        } else {
            error.help("name this argument, or move it before the first named argument");
        }
        throw error;
    }
}

void Lowering::reject_labels(const Expr& expr, std::size_t first, std::string_view reason, bool order) const {
    if (expr.labels.empty()) return;
    if (order) check_label_order(expr, first, no_symbol);
    for (std::size_t i = first; i < expr.operands.size(); ++i) {
        if (expr.labels[i].empty()) continue;
        throw Diagnostic(expr.label_spans[i], "labels are not available for " + std::string(reason))
            .with_code("E0484")
            .label("named arguments need the parameter names of a declaration")
            .help("remove the labels and pass the arguments in parameter order");
    }
}

std::optional<Lowering::ArgumentMap> Lowering::map_arguments(SymbolId target, const Expr& expr, std::size_t first,
                                                            std::size_t leading, bool diagnose) {
    const auto& params = symbol(target).parameters;
    const auto total = params.size() - leading;
    const auto count = expr.operands.size() - first;
    const auto parameter_name = [&](std::size_t p) -> const std::string& { return symbol(params[p + leading]).name; };
    ArgumentMap map;
    map.source.assign(total, std::nullopt);
    map.parameter.assign(count, 0);
    std::size_t positional = 0;
    while (positional < count && (expr.labels.empty() || expr.labels[first + positional].empty())) ++positional;
    if (positional > total) {
        if (!diagnose) return std::nullopt;
        auto error = Diagnostic(expr.operands[first + total].span,
                                "expected at most " + plural(total, "argument")).with_code("E0407")
            .label("this argument has no parameter to fill");
        if (!symbol(target).span.file.empty())
            error.label(name_span(target), "'" + symbol(target).name + "' takes " + plural(total, "parameter"));
        throw error;
    }
    for (std::size_t i = 0; i < positional; ++i) {
        map.source[i] = first + i;
        map.parameter[i] = i;
    }
    std::optional<std::size_t> previous = positional == 0 ? std::nullopt : std::optional{positional - 1};
    for (std::size_t i = positional; i < count; ++i) {
        const auto& label = expr.labels[first + i];
        const auto& label_span = expr.label_spans[first + i];
        std::size_t p = 0;
        while (p < total && parameter_name(p) != label) ++p;
        if (p == total) {
            if (!diagnose) return std::nullopt;
            auto error = Diagnostic(label_span, "no parameter named '" + label + "'").with_code("E0482")
                .label("'" + symbol(target).name + "' has no parameter of this name");
            std::vector<std::string> unfilled;
            for (std::size_t q = 0; q < total; ++q)
                if (!map.source[q]) unfilled.push_back(parameter_name(q));
            suggest_name(error, label_span, label, unfilled, "a parameter");
            if (!symbol(target).span.file.empty())
                error.label(name_span(target), "'" + symbol(target).name + "' is declared here");
            throw error;
        }
        if (map.source[p]) {
            if (!diagnose) return std::nullopt;
            const auto earlier = *map.source[p];
            const bool earlier_named = !expr.labels[earlier].empty();
            throw Diagnostic(argument_span(expr, first + i), "parameter '" + label + "' is given two arguments")
                .with_code("E0483")
                .label("a second argument for '" + label + "'")
                .label(earlier_named ? expr.label_spans[earlier] : expr.operands[earlier].span,
                       earlier_named ? std::string("first named here") : "first given by position here")
                .help("remove one of the two arguments");
        }
        map.source[p] = first + i;
        map.parameter[i] = p;
        if (previous && p < *previous) map.reordered = true;
        previous = p;
    }
    for (std::size_t p = 0; p < total; ++p) {
        if (map.source[p] || has_default(params[p + leading])) continue;
        if (!diagnose) return std::nullopt;
        auto error = Diagnostic(expr.span, "missing an argument for parameter '" + parameter_name(p) + "'")
            .with_code("E0407")
            .label("'" + parameter_name(p) + "' has no default and is not given a value");
        if (!symbol(params[p + leading]).span.file.empty())
            error.label(symbol(params[p + leading]).span, "'" + parameter_name(p) + "' is declared here");
        error.help("pass it by position or as '" + parameter_name(p) + ": value'");
        throw error;
    }
    return map;
}

// Whether a call of `target` must be mapped by name: it is labelled, or the
// function gives a default to a parameter the call may leave out.
bool Lowering::named_call(SymbolId target, const Expr& expr) const {
    if (target == no_symbol || symbol(target).kind != SymbolKind::Function) return false;
    if (!expr.labels.empty()) return true;
    return std::ranges::any_of(symbol(target).parameters,
                               [&](SymbolId parameter) { return has_default(parameter); });
}

void Lowering::named_arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                               const Substitution& replacements, Context& context, std::size_t leading) {
    // Which arguments form a variadic tail must not depend on labels.
    if (symbol(target).variadic != VariadicKind::None)
        reject_labels(expr, first, "a call of a function with a variadic parameter", false);
    check_label_order(expr, first, target);
    const auto map = *map_arguments(target, expr, first, leading, true);
    const auto* function = entries_[target].declaration;
    std::vector<IrNode> admitted;
    for (std::size_t i = 0; i < map.parameter.size(); ++i) {
        const auto index = map.parameter[i] + leading;
        const auto& parameter = symbol(symbol(target).parameters[index]);
        auto expected = substitute(parameter.type, replacements);
        if (!parameter.span.file.empty()) {
            expected_origin_ = ExpectedOrigin{parameter.span, "parameter '" + parameter.name + "' is declared here",
                function && index < function->parameters.size() ? &function->parameters[index].type : nullptr};
        }
        admitted.push_back(argument_value(expr, first + i, expected, context));
    }
    assemble_arguments(call, expr, first, target, map, std::move(admitted), replacements, context, leading);
}

void Lowering::assemble_arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                                  const ArgumentMap& map, std::vector<IrNode> admitted,
                                  const Substitution& replacements, Context& context, std::size_t leading) {
    const auto& params = symbol(target).parameters;
    std::vector<std::optional<IrNode>> slots(map.source.size());
    if (map.reordered) {
        // A primitive reference is bound in place; a hidden local would copy
        // the binding, which the storage rules do not yet describe
        // (Calls030.md, Lowering and artifacts). Such a call is rejected.
        for (std::size_t i = 0; i < map.parameter.size(); ++i) {
            const auto p = map.parameter[i];
            if (!is_primitive_reference(symbol(params[p + leading]).type)) continue;
            throw Diagnostic(argument_span(expr, first + i), "labels are not available for a '&T' argument out of parameter order")
                .with_code("E0484")
                .label("'" + symbol(params[p + leading]).name + "' takes a primitive reference")
                .help("pass the arguments in parameter order");
        }
        // The receiver is evaluated first and once. A module qualifier has no
        // runtime value, and the receiver of the method being lowered
        // (`instance`, and the one `super.init` initializes) reads nothing
        // that an argument can change.
        std::vector<IrNode> prelude;
        for (auto& operand : call.operands) {
            if (operand.type.kind == TypeKind::Module || operand.op == IrOp::Receiver) continue;
            operand = save_temporary(std::move(operand), prelude, context, "$receiver");
        }
        for (std::size_t i = 0; i < admitted.size(); ++i)
            slots[map.parameter[i]] = save_temporary(std::move(admitted[i]), prelude, context, "$argument");
        named_prelude_ = std::move(prelude);
    } else {
        for (std::size_t i = 0; i < admitted.size(); ++i) slots[map.parameter[i]] = std::move(admitted[i]);
    }
    for (std::size_t p = 0; p < slots.size(); ++p) {
        if (slots[p]) {
            call.operands.push_back(std::move(*slots[p]));
            continue;
        }
        const auto parameter = params[p + leading];
        call.operands.push_back(default_value(parameter, substitute(symbol(parameter).type, replacements), expr.span));
    }
}

IrNode Lowering::finish_call(IrNode call) {
    if (named_prelude_.empty()) return call;
    auto result = node(IrOp::ScopedValue, call.span, call.type);
    result.body = std::exchange(named_prelude_, {});
    result.body.push_back(std::move(call));
    return result;
}

// Splits a reordered call that finish_call wrapped into its hidden locals
// and the call. Any other node is left as it is.
std::vector<IrNode> Lowering::named_call_prelude(IrNode& invocation) {
    if (invocation.op != IrOp::ScopedValue || invocation.body.size() < 2) return {};
    const auto& last = invocation.body.back();
    if (last.op != IrOp::Call && last.op != IrOp::VirtualCall) return {};
    for (std::size_t i = 0; i + 1 < invocation.body.size(); ++i) {
        const auto& item = invocation.body[i];
        if (item.op != IrOp::Initialize) return {};
        const auto& name = symbol(item.symbol).name;
        if (name != "$argument" && name != "$receiver") return {};
    }
    auto call = std::move(invocation.body.back());
    invocation.body.pop_back();
    auto prelude = std::move(invocation.body);
    invocation = std::move(call);
    return prelude;
}

// Constant defaults (Calls030.md, Constant defaults). A default is checked
// once, at the declaration, against its parameter's type, and recorded as an
// attribute of the parameter symbol. A caller reads the attribute, so a
// function from a prebuilt .nir module gives its defaults to its callers.

namespace {

bool mentions_type_parameter(const IrType& type) {
    return type.kind == TypeKind::TypeParameter || std::ranges::any_of(type.arguments, mentions_type_parameter);
}

// A literal, a negated numeric literal, nil, or a qualified name that may
// name a variant; lowering decides the last.
bool default_shape(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::Integer:
    case ExprKind::Float:
    case ExprKind::String:
    case ExprKind::Boolean:
    case ExprKind::Nil:
        return true;
    case ExprKind::Unary:
        return expr.text == "-" && (expr.operands.front().kind == ExprKind::Integer ||
                                    expr.operands.front().kind == ExprKind::Float);
    case ExprKind::Member: {
        const auto* prefix = &expr.operands.front();
        while (prefix->kind == ExprKind::Member) prefix = &prefix->operands.front();
        if (prefix->kind == ExprKind::TypeApplication) prefix = &prefix->operands.front();
        while (prefix->kind == ExprKind::Member) prefix = &prefix->operands.front();
        return prefix->kind == ExprKind::Name;
    }
    default:
        return false;
    }
}

// The shortest decimal that reads back as `value`. Every float16, bfloat16
// and float32 value is a double, and the decimal nearest a double is nearer
// to it than to any other value of the narrower format.
std::string float_text(double value) {
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text(buffer.data(), error == std::errc{} ? end : buffer.data());
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

double float_value(std::uint64_t bits, const FloatFormat& format) {
    const auto fraction = bits & ((std::uint64_t{1} << format.fraction_bits) - 1);
    const auto exponent = (bits >> format.fraction_bits) & ((std::uint64_t{1} << format.exponent_bits) - 1);
    const bool negative = ((bits >> (format.width() - 1)) & 1) != 0;
    const auto bias = static_cast<int>(format.bias());
    const auto fraction_bits = static_cast<int>(format.fraction_bits);
    const double magnitude = exponent == 0
        ? std::ldexp(static_cast<double>(fraction), 1 - bias - fraction_bits)
        : std::ldexp(static_cast<double>(fraction | (std::uint64_t{1} << format.fraction_bits)),
                     static_cast<int>(exponent) - bias - fraction_bits);
    return negative ? -magnitude : magnitude;
}

} // namespace

void Lowering::record_defaults(SymbolId function, const Declaration& decl, Context& context) {
    const auto params = symbol(function).parameters;
    for (std::size_t i = 0; i < decl.parameters.size() && i < params.size(); ++i) {
        const auto& parameter = decl.parameters[i];
        if (!parameter.default_value) continue;
        const auto& span = *parameter.default_span;
        const auto& type = symbol(params[i]).type;
        const auto reject = [&](const std::string& reason) {
            throw Diagnostic(span, "parameter '" + parameter.name + "' takes no default").with_code("E0226")
                .label(reason);
        };
        if (symbol(function).variadic != VariadicKind::None)
            reject("no parameter of a function with a variadic parameter takes a default");
        if (decl.host_binding || decl.kernel_binding || decl.scheduler_binding || decl.host_callback)
            reject("a host, kernel, scheduler or callback binding takes no default");
        if (is_primitive_reference(type)) reject("a primitive reference parameter is bound to a caller's place");
        if (mentions_type_parameter(type)) reject("its type mentions a type parameter");
        const auto& written = *parameter.default_value;
        const auto not_constant = [&] {
            return Diagnostic(written.span, "a default is a literal, nil, or a variant without payload")
                .with_code("E0485")
                .label("not a constant")
                .help("use an overload, or test for a marker value in the body");
        };
        if (!default_shape(written)) throw not_constant();
        auto lowered = value(written, context, &type);
        auto& target = symbol(params[i]);
        std::uint64_t kind = 0;
        IrAttributeValue literal;
        switch (lowered.op) {
        case IrOp::Boolean:
            literal = attribute_bool(lowered.text == "true");
            break;
        case IrOp::Integer: {
            const bool negative = lowered.text.starts_with('-');
            std::uint64_t magnitude = 0;
            const auto digits = std::string_view(lowered.text).substr(negative ? 1 : 0);
            std::from_chars(digits.data(), digits.data() + digits.size(), magnitude);
            if (type.name.starts_with("int")) {
                kind = 1;
                literal = attribute_signed(negative ? static_cast<std::int64_t>(0 - magnitude)
                                                    : static_cast<std::int64_t>(magnitude));
            } else {
                kind = 2;
                literal = attribute_unsigned(magnitude);
            }
            break;
        }
        case IrOp::Float:
            kind = 3;
            literal = attribute_unsigned(parse_float_literal(lowered.text, *float_format(type)).bits);
            break;
        case IrOp::String:
            kind = 4;
            literal = attribute_text(lowered.text);
            break;
        case IrOp::Nil:
            kind = 5;
            literal = attribute_bool(false);
            break;
        case IrOp::Reference:
        case IrOp::EnumConstruct:
            if (lowered.symbol == no_symbol || symbol(lowered.symbol).kind != SymbolKind::Variant ||
                !lowered.operands.empty())
                throw not_constant();
            set_attribute(target, AttributeId::ParameterDefaultVariant, attribute_symbol(lowered.symbol));
            continue;
        default:
            throw not_constant();
        }
        set_attribute(target, AttributeId::ParameterDefault, attribute_list({attribute_unsigned(kind), literal}));
    }
}

bool Lowering::has_default(SymbolId parameter) const {
    const auto& item = symbol(parameter);
    return has_attribute(item, AttributeId::ParameterDefault) || has_attribute(item, AttributeId::ParameterDefaultVariant);
}

IrNode Lowering::default_value(SymbolId parameter, const IrType& type, const Span& span) {
    const auto& item = symbol(parameter);
    if (const auto* variant = find_attribute(item, AttributeId::ParameterDefaultVariant)) {
        auto result = node(IrOp::Reference, span, type);
        result.symbol = variant->value.symbol;
        if (has_enum_payload(program_, symbol(result.symbol).owner)) result.op = IrOp::EnumConstruct;
        return result;
    }
    const auto* recorded = find_attribute(item, AttributeId::ParameterDefault);
    if (!recorded || recorded->value.items.size() != 2)
        throw Diagnostic(span, "parameter '" + item.name + "' has no default").with_code("E0899");
    const auto& literal = recorded->value.items[1];
    switch (recorded->value.items[0].integer) {
    case 0: {
        auto result = node(IrOp::Boolean, span, type);
        result.text = literal.integer != 0 ? "true" : "false";
        return result;
    }
    case 1:
    case 2: {
        auto result = node(IrOp::Integer, span, type);
        result.text = recorded->value.items[0].integer == 1 ? std::to_string(attribute_signed_value(literal))
                                                            : std::to_string(literal.integer);
        return result;
    }
    case 3: {
        auto result = node(IrOp::Float, span, type);
        result.text = float_text(float_value(literal.integer, *float_format(type)));
        return result;
    }
    case 4: {
        auto result = node(IrOp::String, span, type);
        result.text = literal.text;
        return result;
    }
    default:
        return node(IrOp::Nil, span, type);
    }
}

// E0409 for two alternatives that the ranking cannot order. A call that uses
// labels or defaults also names both alternatives, since which parameters an
// argument fills differs between them.
Diagnostic Lowering::ambiguous_overload(const Expr& expr, std::size_t first, SymbolId group, SymbolId one,
                                        SymbolId other, const std::map<SymbolId, ArgumentMap>& maps) const {
    auto error = Diagnostic(expr.span, "ambiguous overload of '" + symbol(group).name + "'").with_code("E0409");
    const auto omitted = [&](SymbolId branch) {
        const auto& map = maps.at(branch);
        return std::ranges::any_of(map.source, [](const auto& source) { return !source.has_value(); });
    };
    const bool lambdas = std::ranges::any_of(expr.operands.begin() + static_cast<std::ptrdiff_t>(first),
                                             expr.operands.end(), untyped_lambda_expr);
    if (expr.labels.empty() && !omitted(one) && !omitted(other) && !lambdas) return error;
    for (const auto branch : {one, other}) {
        std::string signature;
        for (const auto parameter : symbol(branch).parameters)
            signature += (signature.empty() ? "" : ", ") + symbol(parameter).name + ": " +
                         (is_function_value(symbol(parameter).type) ? function_text(symbol(parameter).type)
                                                                    : describe(symbol(parameter).type));
        error.label(name_span(branch), "this alternative takes (" + signature + ")");
    }
    const auto count = expr.operands.size() - first;
    error.help(count == 0 ? std::string("pass an argument that only one alternative accepts")
               : lambdas ? std::string("write the lambda's parameter types; its body does not choose an alternative")
                         : "name or add an argument that only one alternative accepts; a default does not decide");
    return error;
}

// The parameter names of an override, or of an implementation of an
// interface method, are those of the method it replaces, position by
// position: a label means one parameter whatever the receiver's static type.
void Lowering::check_parameter_names(SymbolId method, SymbolId base) {
    const auto& ours = symbol(method).parameters;
    const auto& theirs = symbol(base).parameters;
    for (std::size_t i = 0; i < ours.size() && i < theirs.size(); ++i) {
        const auto& name = symbol(ours[i]).name;
        const auto& expected = symbol(theirs[i]).name;
        if (name == expected) continue;
        const auto& at = symbol(ours[i]).span;
        const bool implements = symbol(symbol(base).owner).kind == SymbolKind::Interface;
        auto error = Diagnostic(at, "parameter '" + name + "' is named '" + expected + "' in the method it " +
                                        (implements ? "implements" : "overrides"))
            .with_code("E0560")
            .label("a call may name this parameter '" + expected + "'");
        if (!symbol(theirs[i]).span.file.empty())
            error.label(symbol(theirs[i]).span, "the parameter is named '" + expected + "' here");
        if (!at.file.empty() && source_text(at) == name)
            error.suggest("rename it to the base name", at, expected, Applicability::MaybeIncorrect);
        else
            error.help("rename it to '" + expected + "'");
        throw error;
    }
    // The defaults too: a caller inserts the default of the method it names,
    // so an override that changed one would mean a different call through a
    // base reference.
    for (std::size_t i = 0; i < ours.size() && i < theirs.size(); ++i) {
        const auto describe_default = [&](SymbolId parameter) -> std::optional<IrAttributeValue> {
            if (const auto* variant = find_attribute(symbol(parameter), AttributeId::ParameterDefaultVariant))
                return variant->value;
            if (const auto* literal = find_attribute(symbol(parameter), AttributeId::ParameterDefault))
                return literal->value;
            return std::nullopt;
        };
        const auto mine = describe_default(ours[i]);
        const auto base_default = describe_default(theirs[i]);
        if (mine == base_default) continue;
        const auto& name = symbol(ours[i]).name;
        const bool implements = symbol(symbol(base).owner).kind == SymbolKind::Interface;
        const auto what = std::string(implements ? "implements" : "overrides");
        auto error = Diagnostic(symbol(ours[i]).span,
                                !mine ? "parameter '" + name + "' drops the default of the method it " + what
                                : !base_default ? "parameter '" + name + "' has a default that the method it " + what +
                                                      " does not have"
                                                : "parameter '" + name + "' changes the default of the method it " +
                                                      what)
            .with_code("E0561")
            .label("a call through the base would pass a different value");
        if (!symbol(theirs[i]).span.file.empty())
            error.label(symbol(theirs[i]).span, base_default ? "the base default is given here"
                                                             : "the base parameter has no default");
        error.help(base_default ? "copy the base default" : "remove the default");
        throw error;
    }
}

// Contextual inference of type arguments (Calls030.md, Contextual inference).
// The arguments whose parameter mentions a type parameter of the callee and
// that are not contextual (a numeric literal, nil, an empty collection
// literal, a target-typed `new`) are lowered once, without an expected
// type, and their static types are matched against their parameters. A type
// parameter left unsolved is taken from the expected type of the call, and
// then from the default types of numeric literals. The lowered arguments are
// kept in inferred_ and admitted when the call assembles its arguments; every
// other argument is lowered then, as in a call with written type arguments.

namespace {

bool contextual_argument(const Expr& input) {
    if (input.kind == ExprKind::Integer || input.kind == ExprKind::Float || input.kind == ExprKind::Nil) return true;
    if (input.kind == ExprKind::Unary && input.text == "-" &&
        (input.operands.front().kind == ExprKind::Integer || input.operands.front().kind == ExprKind::Float))
        return true;
    if (input.kind == ExprKind::Collection && input.operands.empty()) return true;
    return input.kind == ExprKind::New && !input.type;
}

bool numeric_argument(const Expr& input) {
    return input.kind == ExprKind::Integer || input.kind == ExprKind::Float ||
           (input.kind == ExprKind::Unary && input.text == "-" &&
            (input.operands.front().kind == ExprKind::Integer || input.operands.front().kind == ExprKind::Float));
}

bool float_argument(const Expr& input) {
    return input.kind == ExprKind::Float || (input.kind == ExprKind::Unary && input.operands.front().kind == ExprKind::Float);
}

} // namespace

bool Lowering::mentions(const IrType& type, const std::vector<SymbolId>& generics) const {
    if (type.kind == TypeKind::TypeParameter && std::ranges::find(generics, type.symbol) != generics.end()) return true;
    return std::ranges::any_of(type.arguments, [&](const IrType& argument) { return mentions(argument, generics); });
}

void Lowering::unify(const IrType& parameter, const IrType& argument, const std::vector<SymbolId>& generics,
                     Inference& inference, const Span& source) {
    if (parameter.kind == TypeKind::TypeParameter && std::ranges::find(generics, parameter.symbol) != generics.end()) {
        auto& solution = inference.solutions[parameter.symbol];
        if (!solution) {
            solution = Inference::Solution{argument, source};
            return;
        }
        if (solution->type == argument) return;
        throw Diagnostic(source, "type parameter '" + symbol(parameter.symbol).name + "' is " + describe(argument) +
                                     " from this argument and " + describe(solution->type) + " from that one")
            .with_code("E0480")
            .label("gives " + describe(argument))
            .label(solution->source, "gives " + describe(solution->type))
            .help("write the type arguments; no common type is computed");
    }
    if (parameter.arguments.empty()) return;
    const auto pairwise = [&](const IrType& actual) {
        for (std::size_t i = 0; i < parameter.arguments.size(); ++i)
            unify(parameter.arguments[i], actual.arguments[i], generics, inference, source);
    };
    if (argument.kind == parameter.kind && argument.symbol == parameter.symbol &&
        argument.arguments.size() == parameter.arguments.size()) {
        pairwise(argument);
        return;
    }
    if (parameter.kind != TypeKind::Object || argument.kind != TypeKind::Object) return;
    for (const auto& view : nominal_views(program_, argument)) {
        if (view.symbol == parameter.symbol && view.arguments.size() == parameter.arguments.size()) {
            pairwise(view);
            return;
        }
    }
}

Substitution Lowering::call_type_arguments(SymbolId target, const Expr& expr, const Substitution& replacements,
                                           Context& context, const IrType* expected) {
    const auto generics = symbol(target).generics;
    if (!expr.type_arguments.empty() || generics.empty())
        return type_arguments(generics, expr.type_arguments, expr.span, context);
    return infer_type_arguments(target, expr, 1, replacements, context, expected);
}

Substitution Lowering::infer_type_arguments(SymbolId target, const Expr& expr, std::size_t first,
                                            const Substitution& replacements, Context& context,
                                            const IrType* expected) {
    const auto generics = symbol(target).generics;
    const auto& params = symbol(target).parameters;
    const auto count = expr.operands.size() - first;
    // The parameter each written argument fills; the tail of a variadic
    // function fills the element type of its last parameter.
    std::vector<std::optional<IrType>> filled(count);
    const bool rest = symbol(target).variadic != VariadicKind::None;
    if (!expr.labels.empty() && !rest) {
        check_label_order(expr, first, target);
        const auto map = *map_arguments(target, expr, first, 0, true);
        for (std::size_t i = 0; i < count; ++i) filled[i] = symbol(params[map.parameter[i]]).type;
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            if (rest && i + 1 >= params.size()) {
                const auto& pack = symbol(params.back()).type;
                if (!pack.arguments.empty()) filled[i] = pack.arguments.front();
            } else if (i < params.size()) {
                filled[i] = symbol(params[i]).type;
            }
        }
    }
    Inference inference;
    std::vector<std::pair<std::size_t, IrType>> literals;
    std::vector<std::optional<IrNode>> lowered_arguments(expr.operands.size());
    // Arguments lowered before the call, such as an extension call's receiver.
    if (const auto earlier = inferred_.find(&expr); earlier != inferred_.end()) {
        lowered_arguments = std::move(earlier->second);
        lowered_arguments.resize(expr.operands.size());
        inferred_.erase(earlier);
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!filled[i]) continue;
        const auto declared = substitute(*filled[i], replacements);
        if (!mentions(declared, generics)) continue;
        const auto& input = expr.operands[first + i];
        if (lowered_arguments[first + i]) {
            unify(declared, lowered_arguments[first + i]->type, generics, inference, input.span);
            continue;
        }
        if (untyped_lambda_expr(input)) continue;
        if (contextual_argument(input)) {
            if (numeric_argument(input) && declared.kind == TypeKind::TypeParameter)
                literals.emplace_back(first + i, float_argument(input) ? float64_type : int32_type);
            continue;
        }
        if (input.kind == ExprKind::Lambda && input.lambda) {
            // A typed lambda's type is in its syntax. It is lowered later, at
            // its place among the arguments, as in the written call.
            std::vector<IrType> signature;
            for (const auto& parameter : input.lambda->parameters) signature.push_back(parameter_type(parameter, context));
            signature.push_back(type(*input.lambda->type, context));
            unify(declared, callable_type(std::move(signature), input.span), generics, inference, input.span);
            continue;
        }
        auto lowered = value(input, context);
        unify(declared, lowered.type, generics, inference, input.span);
        lowered_arguments[first + i] = std::move(lowered);
    }
    const auto unsolved = [&](SymbolId generic) {
        const auto found = inference.solutions.find(generic);
        return found == inference.solutions.end() || !found->second;
    };
    const auto solved_so_far = [&] {
        Substitution partial;
        for (const auto& [generic, solution] : inference.solutions)
            if (solution) partial.emplace(generic, solution->type);
        return partial;
    };
    // Steps 2 and 3: each untyped lambda, in written order, takes its
    // parameter types from its parameter's function type, and its result
    // type then gives constraints.
    for (std::size_t i = 0; i < count; ++i) {
        const auto& input = expr.operands[first + i];
        if (!filled[i] || !untyped_lambda_expr(input)) continue;
        const auto declared = substitute(*filled[i], replacements);
        if (!mentions(declared, generics)) continue;
        const auto current = substitute(declared, solved_so_far());
        if (!is_function_value(current)) {
            throw Diagnostic(input.span, "cannot infer the parameter types of this lambda").with_code("E0419")
                .label("its parameter has type " + describe(declared) + ", which is not a function type yet")
                .help("write the lambda's parameter types, or the call's type arguments");
        }
        for (std::size_t p = 0; p + 1 < current.arguments.size(); ++p) {
            if (!mentions(current.arguments[p], generics)) continue;
            std::string names;
            for (const auto generic : generics)
                if (unsolved(generic)) names += (names.empty() ? "'" : ", '") + symbol(generic).name + "'";
            throw Diagnostic(input.span, "cannot infer type parameter " + names + " before this lambda")
                .with_code("E0419")
                .label("no earlier argument gives the types of this lambda's parameters")
                .help("write the lambda's parameter types, or the call's type arguments");
        }
        const bool open_result = mentions(current.arguments.back(), generics);
        auto lowered = untyped_lambda(*input.lambda, context, &current, open_result);
        if (open_result) unify(declared.arguments.back(), lowered.type.arguments.back(), generics, inference, input.span);
        lowered_arguments[first + i] = std::move(lowered);
    }
    // The expected type of the call solves what the arguments leave.
    if (expected && std::ranges::any_of(generics, unsolved)) {
        Inference context_inference;
        const auto result = substitute(symbol(target).type, replacements);
        SpeculativeDiagnostics speculative;
        try {
            unify(result, *expected, generics, context_inference, expr.span);
        } catch (const Diagnostic&) {
            // A conflict within the expected type leaves the parameters unsolved.
        }
        for (const auto generic : generics)
            if (unsolved(generic) && context_inference.solutions[generic])
                inference.solutions[generic] = context_inference.solutions[generic];
    }
    // Numeric literals alone give their default types, which must agree.
    for (const auto& [index, type] : literals) {
        const auto generic = substitute(*filled[index - first], replacements).symbol;
        if (!unsolved(generic) && !inference.solutions[generic]->from_literal) continue;
        auto& solution = inference.solutions[generic];
        if (!solution) {
            solution = Inference::Solution{type, expr.operands[index].span, true};
            continue;
        }
        if (solution->type != type)
            throw Diagnostic(expr.operands[index].span, "type parameter '" + symbol(generic).name + "' is " +
                                                            describe(type) + " from this argument and " +
                                                            describe(solution->type) + " from that one")
                .with_code("E0480")
                .label("a literal of default type " + describe(type))
                .label(solution->source, "a literal of default type " + describe(solution->type))
                .help("write the type arguments, or give the literals one type");
    }
    Substitution result;
    for (const auto generic : generics) {
        if (unsolved(generic)) {
            std::string written;
            for (const auto other : generics) written += (written.empty() ? "" : ", ") + symbol(other).name;
            throw Diagnostic(expr.span, "cannot infer type parameter '" + symbol(generic).name + "'")
                .with_code("E0419")
                .label("no argument or expected type gives '" + symbol(generic).name + "'")
                .help("write the type arguments, as in '" + symbol(target).name + "<" + written + ">(...)'");
        }
        result.emplace(generic, inference.solutions[generic]->type);
    }
    // The solution is checked like written type arguments.
    for (const auto generic : generics) {
        const auto& solution = *inference.solutions[generic];
        const auto& actual = solution.type;
        if (actual.kind == TypeKind::Void)
            throw Diagnostic(solution.source, "void is not a type argument").with_code("E0416");
        if (is_primitive_reference(actual))
            throw Diagnostic(solution.source, "a primitive reference cannot be a type argument").with_code("E0428");
        if (scope_bound(actual))
            throw Diagnostic(solution.source, "a value of type " + describe(actual) +
                                                  " cannot leave its scope: it cannot be a type argument")
                .with_code("E0605");
        const auto& parameter = symbol(generic);
        const auto check = [&](const IrType& declared) {
            const auto bound = substitute(declared, result);
            if (!assignable(actual, bound))
                throw Diagnostic(solution.source, "type argument " + describe(actual) + " does not satisfy the bound " +
                                                      describe(bound) + " of type parameter '" + parameter.name + "'")
                    .with_code("E0418")
                    .label("'" + parameter.name + "' is inferred as " + describe(actual) + " from this argument");
        };
        if (parameter.bound) check(*parameter.bound);
        for (const auto& bound : parameter.additional_bounds) check(bound);
    }
    if (std::ranges::any_of(lowered_arguments, [](const auto& item) { return item.has_value(); }))
        inferred_[&expr] = std::move(lowered_arguments);
    return result;
}

// The argument at operand `index` for a parameter of type `expected`: the one
// inference lowered, admitted to the parameter, or a newly lowered one.
IrNode Lowering::argument_value(const Expr& expr, std::size_t index, const IrType& expected, Context& context) {
    const auto found = inferred_.find(&expr);
    if (found != inferred_.end() && index < found->second.size() && found->second[index]) {
        expected_origin_.reset();
        auto lowered = std::move(*found->second[index]);
        found->second[index].reset();
        if (std::ranges::none_of(found->second, [](const auto& item) { return item.has_value(); }))
            inferred_.erase(found);
        if (is_primitive_reference(expected)) return bind_reference(std::move(lowered), expected);
        if (!assignable(lowered.type, expected)) {
            if (const auto method = cast_method(lowered.type, expected, context); method != no_symbol)
                return cast_call(method, std::move(lowered), expected, expr.operands[index].span);
        }
        require_type(lowered.type, expected, expr.operands[index].span);
        return lowered;
    }
    return value(expr.operands[index], context, &expected, true);
}

// Lambdas with untyped parameters (Calls030.md, Lambdas with untyped
// parameters): `func(a, b) => a + b` takes its parameter types, and its
// result type unless it writes one, from the function type it is expected
// to have. With `infer_result`, an expression body gives the result type
// instead, for the third step of inference.

// A function type as it is written, `func(int32) -> bool`.
std::string Lowering::function_text(const IrType& type) const {
    std::string text = "func(";
    for (std::size_t i = 0; i + 1 < type.arguments.size(); ++i)
        text += (i ? ", " : "") + describe(type.arguments[i]);
    return text + ") -> " + describe(type.arguments.back());
}

bool Lowering::untyped_lambda_expr(const Expr& expr) {
    return expr.kind == ExprKind::Lambda && expr.lambda && expr.lambda->untyped_parameters;
}

bool Lowering::lambda_fits(const Expr& expr, const IrType& expected) {
    return is_function_value(expected) && expected.arguments.size() == expr.lambda->parameters.size() + 1;
}

IrNode Lowering::untyped_lambda(const Declaration& declaration, Context& context, const IrType* expected,
                                bool infer_result) {
    const auto& span = declaration.span;
    const auto& parameters = declaration.parameters;
    Span list = span;
    if (!parameters.empty()) {
        const auto& first = parameters.front().span;
        const auto& last = parameters.back().span;
        list = {first.file, first.line, first.column, first.offset, last.offset + last.length - first.offset};
    }
    if (!expected || !is_function_value(*expected)) {
        throw Diagnostic(parameters.front().span, "parameter '" + parameters.front().name + "' has no type")
            .with_code("E0214")
            .label("a lambda with untyped parameters takes its types from the function type expected here")
            .help("write the parameter types, or pass the lambda where a function type is expected");
    }
    const auto expected_count = expected->arguments.size() - 1;
    if (expected_count != parameters.size()) {
        throw Diagnostic(list, "this lambda takes " + plural(parameters.size(), "parameter") +
                                   "; the expected function type takes " + std::to_string(expected_count))
            .with_code("E0481")
            .label("expected " + function_text(*expected))
            .help("give the lambda " + plural(expected_count, "parameter"));
    }
    std::vector<IrType> signature(expected->arguments.begin(), expected->arguments.end() - 1);
    std::vector<std::string> names;
    for (const auto& parameter : parameters) {
        names.push_back(parameter.name);
        check_tags(parameter.tags, TagTarget::Parameter, parameter.span);
    }
    std::optional<IrType> result;
    if (declaration.result_written) {
        result = type(*declaration.type, context);
        if (!infer_result && *result != expected->arguments.back())
            throw Diagnostic(declaration.type->span, "this lambda returns " + describe(*result) +
                                                         "; the expected function type returns " +
                                                         describe(expected->arguments.back()))
                .with_code("E0481")
                .label("expected " + function_text(*expected))
                .help("remove the result type, or write " + describe(expected->arguments.back()));
    } else if (!infer_result) {
        result = expected->arguments.back();
    } else if (!declaration.expression_body) {
        throw Diagnostic(span, "cannot infer the result type of a lambda with a block body").with_code("E0419")
            .label("the call's type parameters wait for this result")
            .help("write the lambda's result type, as in 'func(...) -> T { ... }'");
    }
    ClosureEnvironment environment{closure_layout(span, lexical_owner(context)), context, {}, {}};
    auto provisional = signature;
    provisional.push_back(result ? *result : void_type);
    const auto method = closure_method(environment.layout, callable_type(provisional, span), span, names);
    if (declaration.result_written) result_spans_.emplace(method, declaration.type->span);
    Context inner{method, method, context.receiver, 0, {entries_[method].names}, &environment};
    check_definite_assignment(declaration, no_symbol);
    std::vector<IrNode> instructions;
    const auto& only = declaration.body.front();
    if (!result) {
        // The expression body gives the result.
        auto lowered = expression(only.expressions.front(), inner);
        if (lowered.op == IrOp::Reference)
            throw Diagnostic(lowered.span, "cannot infer the result type of this lambda").with_code("E0419")
                .help("write the lambda's result type, as in 'func(...) -> T => ...'");
        result = lowered.type;
        symbol(method).type = *result;
        auto finish = node(result->kind == TypeKind::Void ? IrOp::Discard : IrOp::Return, only.span);
        finish.operands.push_back(std::move(lowered));
        instructions.push_back(std::move(finish));
    } else if (declaration.expression_body && !declaration.result_written && result->kind != TypeKind::Void &&
               only.kind == StmtKind::Expression) {
        // `=> expression` without a written result type was read as a void
        // body; the expected result makes it the returned value.
        std::vector<Statement> body_statements;
        Statement returned{};
        returned.kind = StmtKind::Return;
        returned.span = only.span;
        returned.expressions.push_back(only.expressions.front());
        body_statements.push_back(std::move(returned));
        instructions = body(body_statements, inner);
    } else {
        instructions = body(declaration.body, inner);
    }
    if (symbol(method).type.kind != TypeKind::Void && !always_returns(instructions))
        throw missing_return(span, method, declaration);
    program_.bodies.push_back({method, std::move(instructions)});
    signature.push_back(*result);
    return closure_value(callable_type(std::move(signature), span), environment, span);
}

// Extension sets (Calls030.md, Extension sets). An extension function is an
// ordinary function marked function.extension. `value.name(arguments)` calls
// one when the static type of `value` has no accessible member and no
// primitive companion function of that name, and exactly one enabled
// extension function named `name` accepts the receiver. The functions a
// file or module enables are its own and those of the modules it imports
// with `let.extensions`.

void Lowering::check_extension(SymbolId function, const Declaration& decl) {
    if (!decl.extension) return;
    const auto& params = symbol(function).parameters;
    if (params.empty()) return;  // E0228 in the parser
    const auto& receiver = symbol(params.front());
    const auto& type = receiver.type;
    std::optional<std::string> reason;
    if (decl.parameters.front().variadic) reason = "the receiver cannot be a variadic parameter";
    else if (type.kind == TypeKind::Void) reason = "the receiver cannot have type void";
    else if (is_function_value(type)) reason = "the receiver cannot have a function type";
    else if (type.kind == TypeKind::TypeParameter && !symbol(type.symbol).bound)
        reason = "the receiver cannot be a bare type parameter without a bound";
    if (reason)
        throw Diagnostic(receiver.span, "invalid extension function").with_code("E0488").label(*reason)
            .help("an extension function extends one type, named by its first parameter");
    set_attribute(symbol(function), AttributeId::FunctionExtension, attribute_bool(true));
}

const std::vector<SymbolId>& Lowering::extension_functions(std::string_view name) {
    // Source declarations are seen through their syntax, prebuilt ones
    // through the attribute; new symbols are indexed as they appear.
    for (; extensions_indexed_ < program_.symbols.size(); ++extensions_indexed_) {
        const auto id = static_cast<SymbolId>(extensions_indexed_);
        const auto& item = symbol(id);
        if (item.kind != SymbolKind::Function) continue;
        const auto* declaration = id < entries_.size() ? entries_[id].declaration : nullptr;
        if ((declaration && declaration->extension) || has_attribute(item, AttributeId::FunctionExtension))
            extension_index_[item.name].push_back(id);
    }
    static const std::vector<SymbolId> none;
    const auto found = extension_index_.find(name);
    return found == extension_index_.end() ? none : found->second;
}

// The module or file that declares `function`, and the group it belongs to.
SymbolId Lowering::extension_group(SymbolId function) const {
    const auto owner = symbol(function).owner;
    if (owner == no_symbol || owner >= entries_.size()) return function;
    const auto found = entries_[owner].names.find(symbol(function).name);
    return found != entries_[owner].names.end() && symbol(found->second).kind == SymbolKind::Function
        ? found->second : function;
}

// The enabled sets in the lexical scopes of `context`: each scope's own
// functions, and the modules its `let.extensions` declarations import, with
// their aliases.
std::vector<std::pair<SymbolId, SymbolId>> Lowering::enabled_extension_sets(const Context& context) {
    std::vector<std::pair<SymbolId, SymbolId>> result;
    for (auto scope = lexical_owner(context); scope != no_symbol; scope = symbol(scope).owner) {
        const auto kind = symbol(scope).kind;
        if (kind != SymbolKind::Module && kind != SymbolKind::File) continue;
        result.emplace_back(scope, no_symbol);
        if (scope >= entries_.size()) continue;
        const auto names = entries_[scope].names;
        for (const auto& [name, alias] : names) {
            const auto* declaration = alias < entries_.size() ? entries_[alias].declaration : nullptr;
            if (!declaration || !declaration->extension_set) continue;
            resolve(alias);
            if (symbol(alias).type.kind == TypeKind::Module) result.emplace_back(symbol(alias).type.symbol, alias);
        }
    }
    return result;
}

// Whether `value.name` finds something before extensions do: a member of the
// static type or an intrinsic member, or a primitive companion function.
bool Lowering::finds_member(const IrNode& receiver, std::string_view name, const Context& context,
                            const Span& span) const {
    const auto nominal = receiver_type(receiver.type);
    if (nominal.kind != TypeKind::Object && intrinsic_value_member(name) && supports_value_members(program_, nominal))
        return true;
    if (nominal.kind == TypeKind::String && (name == "byte_length" || name == "byte_at" || name == "slice"))
        return true;
    if (nominal.kind == TypeKind::Float && name == "to_bits") return true;
    if (collection(nominal))
        return name == "length" ||
               (nominal.kind == TypeKind::List && (name == "version" || name == "append" || name == "remove_at"));
    if (nominal.kind == TypeKind::Object) {
        const bool exported = !inside(lexical_owner(context), nominal.symbol);
        return value_member(receiver.type, name, exported) != no_symbol;
    }
    if (nominal.kind == TypeKind::Module || nominal.kind == TypeKind::Enum) return true;
    if (const auto wrapper = primitive_wrapper(nominal)) {
        const auto file = library_files_.find(wrapper->path);
        if (file == library_files_.end()) {
            // The companion is loaded on demand, and lowering starts again
            // with it, as wrapped_member asks for it.
            MissingPrimitiveWrapper missing(span, "type " + describe(nominal) + " has no member '" + std::string(name) + "'",
                                            std::string(wrapper->path));
            missing.with_code("E0305");
            throw missing;
        }
        const auto module = own_member(file->second, wrapper->module);
        if (module == no_symbol) return false;
        const auto method = member(module, name, true);
        return method != no_symbol && symbol(method).kind == SymbolKind::Function;
    }
    return false;
}

// Whether the first parameter of `function` accepts a receiver of `type`: an
// equal type, a base class or interface of it, the referent of a `&T`, or a
// type that inference solves.
bool Lowering::accepts_receiver(SymbolId function, const IrType& type) {
    resolve(function);
    const auto& params = symbol(function).parameters;
    if (params.empty()) return false;
    auto expected = symbol(params.front()).type;
    if (is_primitive_reference(expected)) return reference_field(expected) != no_symbol &&
        symbol(reference_field(expected)).type == type;
    const auto& generics = symbol(function).generics;
    if (mentions(expected, generics)) {
        Inference inference;
        SpeculativeDiagnostics speculative;
        try {
            unify(expected, type, generics, inference, symbol(function).span);
        } catch (const Diagnostic&) {
            return false;
        }
        Substitution solved;
        for (const auto& [generic, solution] : inference.solutions)
            if (solution) solved.emplace(generic, solution->type);
        expected = substitute(expected, solved);
        if (mentions(expected, generics)) return false;
        for (const auto& [generic, solution] : solved) {
            const auto& parameter = symbol(generic);
            if (parameter.bound && !assignable(solution, substitute(*parameter.bound, solved))) return false;
        }
    }
    return expected == type || (class_reference(expected) && assignable(type, expected));
}

std::optional<IrNode> Lowering::extension_member(const Expr& expr, const IrNode& receiver, Context& context,
                                                 bool for_call) {
    const auto& named = extension_functions(expr.text);
    if (named.empty()) return std::nullopt;
    // Only a value has extension members, not a module, a type or an alias.
    if (receiver.op == IrOp::Reference && receiver.symbol != no_symbol &&
        symbol(receiver.symbol).kind != SymbolKind::Variant)
        return std::nullopt;
    if (receiver.type.kind == TypeKind::Module) return std::nullopt;
    if (finds_member(receiver, expr.text, context, expr.span)) return std::nullopt;
    const auto sets = enabled_extension_sets(context);
    struct Candidate {
        SymbolId group;
        SymbolId alias;
    };
    std::vector<Candidate> candidates;
    std::vector<SymbolId> elsewhere;
    for (const auto function : named) {
        const auto owner = symbol(function).owner;
        const auto set = std::ranges::find_if(sets, [&](const auto& entry) { return entry.first == owner; });
        const bool own = set != sets.end() && set->second == no_symbol;
        if (set == sets.end() || (!own && !symbol(function).exported)) {
            if (symbol(function).exported) elsewhere.push_back(function);
            continue;
        }
        const auto group = extension_group(function);
        if (std::ranges::any_of(candidates, [&](const Candidate& c) { return c.group == group; })) continue;
        bool accepts = accepts_receiver(group, receiver.type);
        for (const auto branch : entries_[group].overloads) accepts = accepts || accepts_receiver(branch, receiver.type);
        if (accepts) candidates.push_back({group, set->second});
    }
    if (candidates.empty()) {
        if (elsewhere.empty()) return std::nullopt;
        // Edge case 57: the member is unknown, and an imported module that
        // is not enabled here exports an extension function of that name.
        // The import that names a module exporting it, as the set would.
        std::string path;
        for (auto scope = lexical_owner(context); scope != no_symbol && path.empty(); scope = symbol(scope).owner) {
            if (scope >= entries_.size()) continue;
            for (const auto& [name, alias] : entries_[scope].names) {
                const auto* declaration = alias < entries_.size() ? entries_[alias].declaration : nullptr;
                if (!declaration || !declaration->initializer || declaration->initializer->kind != ExprKind::Import ||
                    symbol(alias).kind != SymbolKind::Import)
                    continue;
                const auto target = symbol(alias).type.symbol;
                if (std::ranges::any_of(elsewhere, [&](SymbolId f) { return symbol(f).owner == target; })) {
                    path = declaration->initializer->import_path ? "\"" + *declaration->initializer->import_path + "\""
                                                                  : declaration->initializer->text;
                    break;
                }
            }
        }
        const auto help = path.empty()
            ? "'" + expr.text + "' is an extension function of the module " +
                  symbol(symbol(elsewhere.front()).owner).name +
                  ", whose set this file or module does not enable; a set is enabled with 'let.extensions'"
            : "'" + expr.text + "' is an extension function of " + path +
                  "; enable its set in this file or module with 'let.extensions <alias> = import:" + path + ";'";
        auto error = Diagnostic(expr.span, "unknown or inaccessible member '" + expr.text + "' of " +
                                               describe(receiver_type(receiver.type)))
            .with_code("E0305")
            .label(describe(receiver_type(receiver.type)) + " has no member '" + expr.text + "'")
            .help(help);
        throw error;
    }
    if (candidates.size() > 1) {
        auto error = Diagnostic(expr.span, "ambiguous extension function '" + expr.text + "'").with_code("E0315")
            .label(std::to_string(candidates.size()) + " enabled extension functions accept " +
                   describe(receiver.type));
        for (const auto& candidate : candidates) {
            const auto from = candidate.alias == no_symbol ? std::string("declared here")
                                                           : "enabled by the set '" + symbol(candidate.alias).name + "'";
            error.label(name_span(candidate.group), "'" + expr.text + "' " + from);
        }
        const auto& first = candidates.front();
        error.help(first.alias == no_symbol
                       ? std::string("call one of them as an ordinary function, with the receiver first")
                       : "call one through its set's alias, as in '" + symbol(first.alias).name + "." + expr.text +
                             "(receiver, ...)'");
        throw error;
    }
    if (!for_call)
        throw Diagnostic(expr.span, "an extension function is not a bound method").with_code("E0316")
            .label("'" + expr.text + "' is an extension function")
            .help(candidates.front().alias == no_symbol
                      ? "call it, or pass '" + expr.text + "' itself as a function value"
                      : "call it, or pass '" + symbol(candidates.front().alias).name + "." + expr.text + "'");
    auto result = node(IrOp::Reference, expr.span, {TypeKind::Callable, "[extension]", candidates.front().group, {}});
    result.symbol = candidates.front().group;
    result.operands.push_back(receiver);
    return result;
}

// `value.name(arguments)` as the ordinary call `name(value, arguments)`:
// the receiver, already evaluated once, is the first argument.
IrNode Lowering::extension_call(const Expr& expr, IrNode callee, Context& context, const IrType* expected) {
    const auto target = callee.symbol;
    Expr call = expr;
    call.operands.insert(call.operands.begin() + 1, expr.operands.front().operands.front());
    if (!call.labels.empty()) {
        call.labels.insert(call.labels.begin() + 1, std::string{});
        call.label_spans.insert(call.label_spans.begin() + 1, Span{});
    }
    std::vector<std::optional<IrNode>> receiver(call.operands.size());
    receiver[1] = std::move(callee.operands.front());
    inferred_[&call] = std::move(receiver);
    resolve(target);
    auto result = node(IrOp::Call, expr.span);
    result.symbol = target;
    const auto owner = symbol(target).owner;
    if (symbol(owner).kind == SymbolKind::Module) {
        // A module's function is called with the module as its static
        // qualifier, as `alias.name(value, ...)` calls it.
        result.op = IrOp::VirtualCall;
        auto module = node(IrOp::Reference, expr.span, symbol(owner).type);
        module.symbol = owner;
        result.operands.push_back(std::move(module));
    }
    Substitution replacements;
    if (!symbol(target).generics.empty()) {
        replacements = call_type_arguments(target, call, {}, context, expected);
        for (const auto generic : symbol(target).generics) result.type_arguments.push_back(replacements.at(generic));
    }
    result.symbol = select_overload(target, call, 1, replacements, context, result);
    result.type = substitute(symbol(result.symbol).type, replacements);
    inferred_.erase(&call);
    return finish_call(std::move(result));
}

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
        admitted.push_back(value(expr.operands[first + i], context, &expected, true));
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
    if (expr.labels.empty() && !omitted(one) && !omitted(other)) return error;
    for (const auto branch : {one, other}) {
        std::string signature;
        for (const auto parameter : symbol(branch).parameters)
            signature += (signature.empty() ? "" : ", ") + symbol(parameter).name + ": " + describe(symbol(parameter).type);
        error.label(name_span(branch), "this alternative takes (" + signature + ")");
    }
    const auto count = expr.operands.size() - first;
    error.help(count == 0 ? std::string("pass an argument that only one alternative accepts")
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

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

// Defaults. Step 3 records them; until then no parameter has one.
bool Lowering::has_default(SymbolId) const {
    return false;
}

IrNode Lowering::default_value(SymbolId parameter, const IrType&, const Span& span) {
    throw Diagnostic(span, "parameter '" + symbol(parameter).name + "' has no default").with_code("E0899");
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
}

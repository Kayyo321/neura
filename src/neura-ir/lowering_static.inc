// Static interface requirements (docs/design/Records030.md, Static interface
// requirements).
//
// Included by lowering.cpp inside its anonymous namespace: these are members
// of Lowering. A static function without a body in an interface is a static
// requirement, which may use the interface's type parameters. A class that
// lists the interface implements it with an exported static function of its
// own with exactly the substituted signature, which carries
// static.implements. Inside a generic body, `T.name(arguments)` calls the
// requirement `name` of the bounds of the type parameter `T` as the NeuraIR
// operation ConformanceCall, which specialization replaces with a direct call
// of the implementation for the type argument. There is no call through the
// interface's name or through a value.

// Whether `function` is a static requirement: a bodyless static function of
// an interface.
bool Lowering::static_requirement(SymbolId function) const {
    if (function >= program_.symbols.size()) return false;
    const auto& item = symbol(function);
    if (item.kind != SymbolKind::Function || item.has_body || item.external || item.owner == no_symbol) return false;
    const auto& scope = symbol(item.owner);
    return is_static_scope(scope) && scope.owner != no_symbol && symbol(scope.owner).kind == SymbolKind::Interface;
}

// The forms a static requirement does not take (E0576), checked before it is
// declared. `owner` is the interface.
void Lowering::check_static_requirement_syntax(const Declaration& decl) const {
    const auto reject = [&](const Span& at, const std::string& label) {
        return Diagnostic(at, "invalid static requirement").with_code("E0576")
            .label(label)
            .note("a static requirement is a plain signature that each implementing class declares")
            .help("write it as 'static let " + decl.name + " = func(parameters) -> T;'");
    };
    const auto name = decl.name_span.value_or(decl.span);
    if (!decl.generics.empty()) throw reject(decl.generics.front().span, "a static requirement is not generic");
    if (!decl.overloads.empty())
        throw reject(decl.overloads.front().span, "a static requirement is not an overload group");
    for (const auto& parameter : decl.parameters) {
        if (parameter.variadic) throw reject(parameter.span, "a static requirement is not variadic");
        if (parameter.default_span) throw reject(*parameter.default_span, "a static requirement has no parameter default");
    }
    if (decl.host_binding || decl.kernel_binding || decl.scheduler_binding)
        throw reject(name, "a static requirement has no host, kernel or scheduler binding");
}

// The static requirements of the interfaces that class `id` lists, directly
// or through interfaces they extend, each implemented by a static function
// the class itself declares (E0504). Each implementation carries
// static.implements.
void Lowering::check_static_requirements(SymbolId id) {
    if (symbol(id).kind != SymbolKind::Class) return;
    const auto& class_name = symbol(id).name;
    std::set<std::pair<SymbolId, SymbolId>> attached;
    const auto listed = symbol(id).interfaces;
    for (const auto& interface : listed) {
        for (const auto& view : nominal_views(program_, interface)) {
            if (symbol(view.symbol).kind != SymbolKind::Interface) continue;
            const auto names = entries_[view.symbol].names;
            for (const auto& [name, requirement] : names) {
                if (!static_requirement(requirement)) continue;
                resolve(requirement);
                const auto replacements = receiver_arguments(view, view.symbol);
                std::vector<IrType> required;
                for (const auto parameter : symbol(requirement).parameters)
                    required.push_back(substitute(symbol(parameter).type, replacements));
                const auto result = substitute(symbol(requirement).type, replacements);
                const auto signature = [&] {
                    std::string text = "static let " + name + " = func(";
                    for (std::size_t i = 0; i < required.size(); ++i)
                        text += (i ? ", " : "") + symbol(symbol(requirement).parameters[i]).name + ": " +
                                describe(required[i]);
                    return text + ") -> " + describe(result) + "'";
                };
                const auto missing = [&](const std::string& why, SymbolId found) {
                    auto error = Diagnostic(name_span(id), "'" + class_name + "' must declare '" + signature())
                        .with_code("E0504")
                        .label("'" + class_name + "' implements '" + describe(view) + "'")
                        .label(name_span(requirement), "'" + name + "' is required here");
                    if (found != no_symbol) error.label(name_span(found), why);
                    else error.note(why);
                    return error;
                };
                const auto own = own_member(id, name);
                if (own == no_symbol)
                    throw missing("'" + class_name + "' declares no static function '" + name +
                                  "'; an inherited one does not implement it", no_symbol);
                resolve(own);
                const auto& candidate = symbol(own);
                if (candidate.kind != SymbolKind::Function || !static_member(own))
                    throw missing("this '" + name + "' is not a static function", own);
                if (!candidate.exported) throw missing("this '" + name + "' is not exported", own);
                if (!candidate.generics.empty() || !entries_[own].overloads.empty() ||
                    candidate.variadic != VariadicKind::None)
                    throw missing("a generic, variadic or overloaded function does not implement a requirement", own);
                std::vector<IrType> actual;
                for (const auto parameter : candidate.parameters) actual.push_back(symbol(parameter).type);
                if (actual != required || candidate.type != result)
                    throw missing("this '" + name + "' has another signature", own);
                check_parameter_names(own, requirement);
                if (attached.emplace(own, requirement).second) {
                    add_attribute(symbol(own), AttributeId::StaticImplements,
                                  attribute_list({attribute_symbol(requirement), attribute_type(symbol(id).type)}));
                }
            }
        }
    }
}

// The bounds of a type parameter, as written.
std::vector<IrType> Lowering::parameter_bounds(SymbolId parameter) const {
    std::vector<IrType> bounds;
    if (symbol(parameter).bound) bounds.push_back(*symbol(parameter).bound);
    bounds.insert(bounds.end(), symbol(parameter).additional_bounds.begin(), symbol(parameter).additional_bounds.end());
    return bounds;
}

// The requirement `name` of the bounds of type parameter `parameter`, with
// the substitution of its interface, or none. Two with different signatures
// are E0577; equal ones are one requirement.
std::optional<std::pair<SymbolId, Substitution>> Lowering::bound_requirement(SymbolId parameter,
                                                                             const std::string& name,
                                                                             const Span& at) {
    std::optional<std::pair<SymbolId, Substitution>> found;
    IrType found_view;
    std::vector<IrType> found_signature;
    for (const auto& bound : parameter_bounds(parameter)) {
        for (const auto& view : nominal_views(program_, bound)) {
            if (view.kind != TypeKind::Object || symbol(view.symbol).kind != SymbolKind::Interface) continue;
            const auto requirement = own_member(view.symbol, name);
            if (!static_requirement(requirement)) continue;
            resolve(requirement);
            auto replacements = receiver_arguments(view, view.symbol);
            std::vector<IrType> signature;
            for (const auto item : symbol(requirement).parameters)
                signature.push_back(substitute(symbol(item).type, replacements));
            signature.push_back(substitute(symbol(requirement).type, replacements));
            if (!found) {
                found = std::pair{requirement, std::move(replacements)};
                found_view = view;
                found_signature = std::move(signature);
            } else if (signature != found_signature) {
                throw Diagnostic(at, "ambiguous static requirement").with_code("E0577")
                    .label("'" + name + "' is required by '" + describe(found_view) + "' and by '" + describe(view) +
                           "' with different signatures")
                    .label(name_span(found->first), "required by '" + describe(found_view) + "' here")
                    .label(name_span(requirement), "and by '" + describe(view) + "' here")
                    .help("call it through one bound only, or rename one of the requirements");
            }
        }
    }
    return found;
}

// `T.name(arguments)` with `T` a type parameter in scope (not hidden by a
// local), or nothing when the callee is not of that form.
std::optional<IrNode> Lowering::conformance_call(const Expr& expr, Context& context) {
    const auto& callee = expr.operands.front();
    if (callee.kind != ExprKind::Member || callee.operands.empty() || callee.operands.front().kind != ExprKind::Name)
        return std::nullopt;
    const auto& qualifier = callee.operands.front();
    const auto parameter = find(qualifier.text, context, qualifier.span);
    if (parameter == no_symbol || symbol(parameter).kind != SymbolKind::TypeParameter) return std::nullopt;
    const auto found = bound_requirement(parameter, callee.text, callee.span);
    if (!found) {
        auto error = Diagnostic(callee.span, "unknown or inaccessible member '" + callee.text + "' of " +
                                                 qualifier.text).with_code("E0305")
            .label("no bound of '" + qualifier.text + "' requires a static function '" + callee.text + "'");
        if (parameter_bounds(parameter).empty())
            error.help("bound '" + qualifier.text + "' by an interface with a static requirement, as in '<" +
                       qualifier.text + ": I<" + qualifier.text + ">>'");
        throw error;
    }
    const auto& [requirement, replacements] = *found;
    auto result = node(IrOp::ConformanceCall, expr.span, substitute(symbol(requirement).type, replacements));
    result.symbol = requirement;
    result.type_arguments.push_back(symbol(parameter).type);
    arguments(result, expr, 1, requirement, replacements, context, 0);
    return finish_call(std::move(result));
}

// `T.name` without a call, `Interface<...>.name(...)` and `value.name(...)`
// for a static requirement `name` (E0576).
[[noreturn]] void Lowering::requirement_misuse(const Span& at, const std::string& name, bool value) const {
    throw Diagnostic(at, "invalid call of a static requirement").with_code("E0576")
        .label(value ? "'" + name + "' is a static requirement, which is called, not taken as a value"
                     : "a static requirement is called through a type parameter or an implementing class")
        .help("call it as 'T." + name + "(...)' in a function with a type parameter 'T' bounded by the interface, or "
              "through the name of a class that implements it");
}

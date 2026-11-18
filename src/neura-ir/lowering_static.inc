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

// Primitive conformance (Records030.md, Primitive conformance and the
// contracts): `module implements I<...>, ... (` declares that a primitive's
// companion in the trusted standard library implements static contracts.

// The primitive whose companion module `module` is, or none.
std::optional<IrType> Lowering::companion_primitive(SymbolId module) const {
    const auto file = source_file(program_, module);
    if (file == no_symbol || symbol(file).library_path.empty()) return std::nullopt;
    for (const auto* name : {"bool", "string", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32",
                             "uint64", "float16", "bfloat16", "float32", "float64"}) {
        const auto type = primitive(name);
        if (!type) continue;
        const auto wrapper = primitive_wrapper(*type);
        if (wrapper && wrapper->module == symbol(module).name && wrapper->path == symbol(file).library_path)
            return type;
    }
    return std::nullopt;
}

// Checks a module's conformance list (E0578, E0504) and marks each
// implementation static.implements for the companion's primitive.
void Lowering::check_module_conformance(SymbolId id) {
    if (!conformance_checked_.insert(id).second) return;
    const auto* decl = entries_[id].declaration;
    // A companion linked from a .nir module has no declaration here; its
    // conformances are those its source declares for its kind of primitive.
    if (decl && decl->interfaces.empty()) return;
    const auto at = decl ? decl->implements_span.value_or(decl->name_span.value_or(decl->span)) : symbol(id).span;
    const auto primitive = companion_primitive(id);
    if (!primitive) {
        if (!decl) return;
        throw Diagnostic(at, "conformance declared by a module").with_code("E0578")
            .label("only a primitive's companion in the standard library declares 'implements'")
            .note("a class conforms to an interface by listing it after 'implements'; a primitive conforms only "
                  "through its companion")
            .help("declare a class that implements the interface");
    }
    auto ctx = context(symbol(id).owner);
    std::vector<std::pair<TypeSyntax, Span>> contracts;
    if (decl) {
        for (const auto& syntax : decl->interfaces) contracts.emplace_back(syntax, syntax.span);
    } else {
        std::vector<std::string> names;
        if (primitive->kind == TypeKind::Integer) names = {"Additive", "Multiplicative", "Ordered", "Parseable"};
        else if (primitive->kind == TypeKind::Float) names = {"Additive", "Multiplicative", "Parseable"};
        else if (primitive->kind == TypeKind::Bool) names = {"Ordered", "Parseable"};
        for (const auto& name : names) {
            TypeSyntax syntax{at, name, {}, {}};
            syntax.arguments.push_back(TypeSyntax{at, primitive->name, {}, {}});
            contracts.emplace_back(std::move(syntax), at);
        }
    }
    for (const auto& [syntax, span] : contracts) {
        // A companion names the contracts of Std.Core.Arithmetic without
        // importing them, so that a program that checks no conformance never
        // loads that file and its image is unchanged.
        IrType interface;
        const auto arithmetic = library_module("std/core/arithmetic.nra", "Arithmetic", span, ctx,
                                               "the contracts of Std.Core.Arithmetic are needed here");
        if (const auto contract = own_member(arithmetic, syntax.name);
            contract != no_symbol && symbol(contract).kind == SymbolKind::Interface) {
            resolve(contract);
            interface = symbol(contract).type;
            interface.arguments.clear();
            for (const auto& argument : syntax.arguments)
                interface.arguments.push_back(decl ? type(argument, ctx) : *primitive);
        } else {
            interface = type(syntax, ctx);
        }
        if (interface.kind != TypeKind::Object || symbol(interface.symbol).kind != SymbolKind::Interface)
            throw Diagnostic(syntax.span, "an interface is required here").with_code("E0421");
        for (const auto& view : nominal_views(program_, interface)) {
            // The umbrella class every interface extends adds nothing a
            // conformance must supply.
            if (symbol(view.symbol).kind != SymbolKind::Interface) continue;
            const auto names = entries_[view.symbol].names;
            for (const auto& [name, member] : names) {
                if (static_requirement(member) || static_member(member) ||
                    symbol(member).kind == SymbolKind::TypeParameter)
                    continue;
                throw Diagnostic(syntax.span, "conformance declared by a module").with_code("E0578")
                    .label("'" + describe(view) + "' has instance members, so a primitive cannot conform to it")
                    .label(name_span(member), "'" + name + "' is an instance member");
            }
            for (const auto& [name, requirement] : names) {
                if (!static_requirement(requirement)) continue;
                resolve(requirement);
                const auto replacements = receiver_arguments(view, view.symbol);
                std::vector<IrType> required;
                for (const auto parameter : symbol(requirement).parameters)
                    required.push_back(substitute(symbol(parameter).type, replacements));
                const auto result = substitute(symbol(requirement).type, replacements);
                const auto own = own_member(id, name);
                bool matches = own != no_symbol;
                if (matches) {
                    resolve(own);
                    const auto& candidate = symbol(own);
                    std::vector<IrType> actual;
                    for (const auto parameter : candidate.parameters) actual.push_back(symbol(parameter).type);
                    matches = candidate.kind == SymbolKind::Function && candidate.exported &&
                              candidate.generics.empty() && entries_[own].overloads.empty() &&
                              candidate.variadic == VariadicKind::None && actual == required && candidate.type == result;
                }
                if (!matches) {
                    std::string signature = "let " + name + " = func(";
                    for (std::size_t i = 0; i < required.size(); ++i)
                        signature += (i ? ", " : "") + symbol(symbol(requirement).parameters[i]).name + ": " +
                                     describe(required[i]);
                    throw Diagnostic(syntax.span, "'" + symbol(id).name + "' must declare 'export " + signature + ") -> " +
                                                      describe(result) + "'")
                        .with_code("E0504")
                        .label("'" + symbol(id).name + "' implements '" + describe(view) + "'")
                        .label(name_span(requirement), "'" + name + "' is required here");
                }
                check_parameter_names(own, requirement);
                bool attached = false;
                for (const auto* existing : find_attributes(symbol(own), AttributeId::StaticImplements))
                    attached = attached || existing->value.items[0].symbol == requirement;
                if (!attached)
                    add_attribute(symbol(own), AttributeId::StaticImplements,
                                  attribute_list({attribute_symbol(requirement), attribute_type(*primitive)}));
            }
        }
    }
}

// Whether primitive `actual` satisfies interface bound `bound` through its
// companion, which lowering loads for the purpose as it does for a
// primitive-qualified call.
bool Lowering::satisfies_by_conformance(const IrType& actual, const IrType& bound, const Span& span,
                                        const Context& context) {
    if (!primitive_type(actual) || bound.kind != TypeKind::Object || bound.symbol >= program_.symbols.size() ||
        symbol(bound.symbol).kind != SymbolKind::Interface)
        return false;
    const auto wrapper = primitive_wrapper(actual);
    if (!wrapper) return false;
    const auto file = library_files_.find(wrapper->path);
    if (file == library_files_.end()) {
        MissingPrimitiveWrapper missing(span, "type argument " + describe(actual) + " does not satisfy the bound " +
                                                  describe(bound) + " until its companion is loaded",
                                        std::string(wrapper->path));
        missing.with_code("E0418");
        throw missing;
    }
    const auto module = own_member(file->second, wrapper->module);
    if (module == no_symbol || symbol(module).kind != SymbolKind::Module) return false;
    resolve(module);
    check_module_conformance(module);
    if (!primitive_conforms(program_, actual, bound)) return false;
    // The using file depends on the companion, as a primitive-qualified call
    // makes it (wrapped_member).
    const auto from = source_file(program_, lexical_owner(context));
    if (from != no_symbol && from != file->second && wrapper_imports_.emplace(from, module).second) {
        const auto dependency = add(SymbolKind::Import, "$primitive", span, from);
        symbol(dependency).type = symbol(module).type;
    }
    return true;
}

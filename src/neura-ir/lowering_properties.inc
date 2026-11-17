// Interface properties (docs/design/Records030.md, Interface properties).
//
// Included by lowering.cpp inside its anonymous namespace: these are members
// of Lowering. An interface member `let name: T;` followed by `| get;`, and
// optionally `| set;`, is a property requirement. It is declared as a field
// of the interface that has no storage: its clauses are the bodyless hidden
// functions `$get_name` and `$set_name` of the interface, linked from the
// field with accessor.get and accessor.set like a class field's accessors and
// marked property.requirement. Every access through an interface-typed
// receiver, or a type parameter bounded by the interface, goes through
// route_field, which calls them as virtual calls; the runtime selects the
// method of that name in the receiver's class or its bases. A class field
// that has no clause of the needed kind gets an adapter: a hidden method of
// that name that reads or stores the field, marked accessor.of and
// generated. Access through a class-typed receiver is unchanged.

namespace property_text {

inline std::string keyword(const Declaration& decl) {
    if (decl.static_member) return "static";
    if (decl.last) return "last";
    if (decl.constant) return "const";
    return "let";
}

} // namespace property_text

// A member of an interface that is a property requirement by its syntax.
bool Lowering::property_requirement_syntax(const Declaration& decl) {
    return decl.kind == DeclKind::Binding && !decl.accessors.empty() && !decl.cast_target;
}

bool Lowering::property_requirement(SymbolId field) const {
    return field < program_.symbols.size() && symbol(field).kind == SymbolKind::Field &&
           symbol(field).owner < program_.symbols.size() &&
           symbol(symbol(field).owner).kind == SymbolKind::Interface;
}

// The checks of a property requirement's syntax (E0573, E0574), before it is
// declared.
void Lowering::check_property_requirement(const Declaration& decl) const {
    const auto name = decl.name_span.value_or(decl.span);
    for (const auto& clause : decl.accessors) {
        if (!clause.dispatch_words.empty()) {
            const auto& [word, at] = clause.dispatch_words.front();
            throw Diagnostic(at, "invalid virtual accessor").with_code("E0574")
                .label(word == "virtual" ? "'.virtual' has no meaning in an interface"
                                         : "'.override' has no meaning in an interface")
                .note("every property of an interface is reached through the receiver's class")
                .help("write the clause as '| " + std::string(clause.setter ? "set" : "get") + ";'");
        }
    }
    if (decl.kind != DeclKind::Binding || decl.cast_target || !decl.overloads.empty()) return;
    if (decl.static_member || decl.constant) {
        auto error = Diagnostic(name, "invalid interface property").with_code("E0573")
            .label("an interface property is declared with 'let', not '" + property_text::keyword(decl) + "'");
        throw error.help("declare it as 'let " + decl.name + ": T;' followed by '| get;'");
    }
    if (decl.initializer)
        throw Diagnostic(decl.initializer->span, "invalid interface property").with_code("E0573")
            .label("an interface property has no initializer")
            .label(name, "'" + decl.name + "' declares no storage")
            .help("remove the initializer; the class that implements the interface holds the value");
    for (const auto& clause : decl.accessors) {
        if (clause.bodyless) continue;
        const auto body = clause.expression ? clause.expression->span
                        : !clause.body.empty() ? clause.body.front().span : clause.span;
        throw Diagnostic(body, "invalid interface property").with_code("E0573")
            .label("a property clause in an interface has no body")
            .help("write the clause as '| " + std::string(clause.setter ? "set" : "get") +
                  ";', and the body in the class that implements the interface");
    }
    const AccessorClause* seen[2] = {nullptr, nullptr};
    for (const auto& clause : decl.accessors) {
        if (const auto* earlier = seen[clause.setter ? 1 : 0]) {
            const auto word = std::string(clause.setter ? "set" : "get");
            throw Diagnostic(clause.keyword, "duplicate '" + word + "' accessor for '" + decl.name + "'")
                .with_code("E0520")
                .label("a second '" + word + "' clause")
                .label(earlier->keyword, "the first '" + word + "' clause is here")
                .help("remove one of them; a property has at most one 'get' and one 'set'");
        }
        seen[clause.setter ? 1 : 0] = &clause;
    }
    if (!seen[0])
        throw Diagnostic(name, "invalid interface property").with_code("E0573")
            .label("an interface property needs '| get;'")
            .help("add '| get;'; a property that can only be written is not supported");
}

// A binding member of an interface with no clause at all: a property that
// lacks its '| get;' (E0573).
void Lowering::reject_clauseless_property(const Declaration& member) const {
    const auto name = member.name_span.value_or(member.span);
    auto error = Diagnostic(name, "invalid interface property").with_code("E0573")
        .label("an interface property needs '| get;'");
    const auto end = member.span.offset + member.span.length;
    if (!member.span.file.empty() && user_source(member.span.file) && !member.initializer &&
        !member.static_member && !member.constant) {
        if (const auto after = source_span(member.span.file, end, 0))
            error.suggest("require a getter", *after, "\n        | get;", Applicability::MaybeIncorrect);
    }
    throw error.help("a member of an interface is a method, or a property followed by '| get;' and "
                     "optionally '| set;'");
}

// The bodyless functions of a property requirement, as declare_accessors
// declares a field's accessors.
void Lowering::declare_requirement_accessors(const Declaration& decl, SymbolId field) {
    const auto owner = symbol(field).owner;
    for (const auto& clause : decl.accessors) {
        const auto kind = clause.setter ? AccessorKind::Set : AccessorKind::Get;
        const auto function = add(SymbolKind::Function, (clause.setter ? "$set_" : "$get_") + decl.name,
                                  clause.span, owner);
        symbol(function).exported = true;
        symbol(function).has_body = false;
        entries_[function].resolution = Resolution::Resolved;
        if (clause.setter) {
            const auto parameter = add(SymbolKind::Parameter, "value", clause.keyword, function);
            entries_[function].names.emplace("value", parameter);
            symbol(function).parameters.push_back(parameter);
        }
        set_attribute(symbol(field), clause.setter ? AttributeId::AccessorSet : AttributeId::AccessorGet,
                      attribute_symbol(function));
        set_attribute(symbol(function), AttributeId::AccessorOf, accessor_of_payload(field, kind));
        set_attribute(symbol(function), AttributeId::PropertyRequirement,
                      attribute_list({attribute_text(decl.name), attribute_unsigned(clause.setter ? 1 : 0)}));
    }
}

// The signatures of a requirement's functions, once the field's type is
// known.
void Lowering::type_requirement_accessors(SymbolId field) {
    resolve(field);
    const auto& type = symbol(field).type;
    if (const auto get = accessor(field, AccessorKind::Get); get != no_symbol) symbol(get).type = type;
    if (const auto set = accessor(field, AccessorKind::Set); set != no_symbol) {
        symbol(set).type = void_type;
        symbol(symbol(set).parameters.front()).type = type;
    }
}

// The hidden method named `name` (`$get_x` or `$set_x`) that class `cls` or
// one of its bases declares, or none.
SymbolId Lowering::hidden_method(SymbolId cls, const std::string& name) const {
    std::set<SymbolId> chain;
    for (auto scope = cls; scope != no_symbol && chain.insert(scope).second; scope = symbol(scope).base) {}
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind == SymbolKind::Function && item.name == name && chain.contains(item.owner)) return id;
    }
    return no_symbol;
}

// Checks that class `id` satisfies the property requirements of the
// interfaces it implements (E0504), and generates the adapters it needs.
void Lowering::check_property_requirements(SymbolId id) {
    struct Required {
        IrType type;
        bool set = false;
        SymbolId field = no_symbol;
        std::string interface;
        std::string setter_interface;
    };
    std::map<std::string, Required> required;
    for (const auto& view : nominal_views(program_, symbol(id).type)) {
        if (symbol(view.symbol).kind != SymbolKind::Interface) continue;
        const auto names = entries_[view.symbol].names;
        for (const auto& [name, member] : names) {
            if (!property_requirement(member)) continue;
            resolve(member);
            const auto type = substitute(symbol(member).type, receiver_arguments(view, view.symbol));
            const bool set = accessor(member, AccessorKind::Set) != no_symbol;
            const auto [found, added] = required.emplace(name, Required{type, set, member, view.name,
                                                                        set ? view.name : std::string()});
            if (added) continue;
            if (found->second.type != type)
                throw Diagnostic(symbol(id).span, "incompatible interface requirements for '" + name + "'")
                    .with_code("E0504")
                    .label("'" + found->second.interface + "' requires '" + name + ": " + describe(found->second.type) +
                           "' and '" + view.name + "' requires '" + name + ": " + describe(type) + "'")
                    .label(name_span(found->second.field), "required here")
                    .label(name_span(member), "and here");
            if (set && !found->second.set) found->second.setter_interface = view.name;
            found->second.set = found->second.set || set;
        }
    }
    if (required.empty() || symbol(id).kind != SymbolKind::Class) return;
    const auto& class_name = symbol(id).name;
    for (const auto& [name, requirement] : required) {
        const auto field = member(id, name, true);
        const auto missing = [&](const std::string& what) {
            auto error = Diagnostic(symbol(id).span, "'" + class_name + "' must have an exported field '" + name + ": " +
                                                         describe(requirement.type) + "' that " + what)
                .with_code("E0504")
                .label("'" + class_name + "' does not satisfy the property '" + name + "' of '" +
                       (what == "can be assigned" ? requirement.setter_interface : requirement.interface) + "'")
                .label(name_span(requirement.field), "'" + name + "' is required here");
            if (field != no_symbol) error.label(name_span(field), "'" + name + "' is declared here");
            return error;
        };
        if (field == no_symbol || symbol(field).kind != SymbolKind::Field) throw missing("can be read");
        resolve(field);
        const auto type = substitute(symbol(field).type, receiver_arguments(symbol(id).type, symbol(field).owner));
        if (type != requirement.type)
            throw missing("can be read").note("'" + name + "' has type " + describe(type) +
                                              ", and a property is satisfied by exactly its type");
        const auto get = accessor(field, AccessorKind::Get);
        const auto set = accessor(field, AccessorKind::Set);
        const bool plain = get == no_symbol && set == no_symbol;
        if (!plain && get == no_symbol) throw missing("can be read").note("'" + name + "' has only a setter");
        if (requirement.set && !(plain ? !symbol(field).constant : set != no_symbol))
            throw missing("can be assigned").note(plain ? "'" + name + "' cannot be assigned after construction"
                                                        : "'" + name + "' has no setter");
        if (!plain) continue;
        // A field without clauses: an adapter for each needed kind that the
        // class or its bases do not have yet.
        const auto& span = name_span(id);
        const auto generated = attribute_list({attribute_text("property.adapter"), attribute_span(span)});
        const auto receiver = [&] {
            auto input = node(IrOp::Receiver, span, symbol(id).type);
            input.symbol = id;
            return input;
        };
        const auto place = [&] {
            auto result = node(IrOp::Field, span, symbol(field).type);
            result.symbol = field;
            result.operands.push_back(receiver());
            return result;
        };
        for (const bool setter : {false, true}) {
            if (setter && !requirement.set) continue;
            const auto method_name = (setter ? "$set_" : "$get_") + name;
            if (hidden_method(id, method_name) != no_symbol) continue;
            const auto function = add(SymbolKind::Function, method_name, span, id);
            symbol(function).exported = true;
            symbol(function).has_body = true;
            entries_[function].resolution = Resolution::Resolved;
            set_attribute(symbol(function), AttributeId::AccessorOf,
                          accessor_of_payload(field, setter ? AccessorKind::Set : AccessorKind::Get));
            set_attribute(symbol(function), AttributeId::Generated, generated);
            std::vector<IrNode> body;
            if (setter) {
                const auto parameter = add(SymbolKind::Parameter, "value", span, function);
                symbol(parameter).type = symbol(field).type;
                symbol(function).parameters.push_back(parameter);
                symbol(function).type = void_type;
                auto value = node(IrOp::Load, span, symbol(field).type);
                value.symbol = parameter;
                auto store = node(IrOp::Store, span);
                store.operands.push_back(place());
                store.operands.push_back(std::move(value));
                body.push_back(std::move(store));
            } else {
                symbol(function).type = symbol(field).type;
                auto result = node(IrOp::Return, span);
                result.operands.push_back(place());
                body.push_back(std::move(result));
            }
            program_.bodies.push_back({function, std::move(body)});
        }
    }
}

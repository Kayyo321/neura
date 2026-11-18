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

// Virtual accessors (docs/design/Records030.md, Virtual accessors).
//
// `| get.virtual: body;` marks the field's hidden getter accessor.virtual: a
// slot. A subclass overrides it with `export let.override name: T;` and
// `| get.override: body;`, which declares no field: the clause becomes a
// hidden method of the same name in the subclass, linked to the method it
// overrides and, by accessor.of, to the inherited field. Every outside access
// is already a virtual call of the declaring class's accessor, so it reaches
// the most derived override, and an unmarked accessor, which no subclass can
// override, keeps its behaviour. Inside an override clause the property's own
// name calls the accessor of the nearest base statically. The runtime has no
// static call of a method, so each slot and override also has a *twin*: a
// hidden method with its body under a name that only its class declares,
// `$get_name$D` where D is the class's depth in its base chain, whose virtual
// call can therefore reach no other method.

// A class member `let.override name: T;`, whatever its clauses.
bool Lowering::accessor_override_syntax(const Declaration& decl) {
    return decl.kind == DeclKind::Binding && decl.override_member && !decl.static_member && !decl.cast_target;
}

std::size_t Lowering::class_depth(SymbolId cls) const {
    std::size_t depth = 0;
    std::set<SymbolId> seen;
    for (auto scope = symbol(cls).base; scope != no_symbol && seen.insert(scope).second; scope = symbol(scope).base)
        ++depth;
    return depth;
}

// `.override` and doubled dispatch words on an ordinary field's clauses.
void Lowering::check_dispatch_words(const Declaration& decl) const {
    for (const auto& clause : decl.accessors) {
        if (clause.dispatch_words.size() > 1)
            throw Diagnostic(clause.dispatch_words[1].second, "invalid virtual accessor").with_code("E0574")
                .label("a clause is '.virtual' or '.override', not both")
                .label(clause.dispatch_words[0].second, "the clause is already '." + clause.dispatch_words[0].first + "'");
        if (clause.dispatch == AccessorClause::Dispatch::Override && !decl.override_member)
            throw Diagnostic(clause.dispatch_words.front().second, "invalid accessor override").with_code("E0575")
                .label("'.override' needs 'let.override'")
                .label(decl.name_span.value_or(decl.span), "'" + decl.name + "' declares a field")
                .help("override an inherited property with 'export let.override " + decl.name +
                      ": T;' followed by its '.override' clauses");
    }
}

SymbolId Lowering::declare_accessor_override(const Declaration& decl, SymbolId owner) {
    const auto name = decl.name_span.value_or(decl.span);
    check_dispatch_words(decl);
    if (!decl.exported)
        throw Diagnostic(name, "invalid accessor override").with_code("E0575")
            .label("an accessor override is exported")
            .help("write 'export let.override " + decl.name + ": T;'");
    if (decl.initializer)
        throw Diagnostic(decl.initializer->span, "invalid accessor override").with_code("E0575")
            .label("an accessor override has no initializer")
            .label(name, "'" + decl.name + "' declares no field; the base class holds the value")
            .help("remove the initializer");
    if (decl.accessors.empty())
        throw Diagnostic(name, "invalid accessor override").with_code("E0575")
            .label("an accessor override needs a '.override' clause")
            .help("follow the declaration with '| get.override: ...;' or '| set.override: ...;'");
    const AccessorClause* seen[2] = {nullptr, nullptr};
    for (const auto& clause : decl.accessors) {
        if (clause.dispatch != AccessorClause::Dispatch::Override)
            throw Diagnostic(clause.keyword, "invalid accessor override").with_code("E0575")
                .label("an accessor override needs a '.override' clause")
                .help("write '| " + std::string(clause.setter ? "set" : "get") + ".override:'");
        if (const auto* earlier = seen[clause.setter ? 1 : 0]) {
            const auto word = std::string(clause.setter ? "set" : "get");
            throw Diagnostic(clause.keyword, "duplicate '" + word + "' accessor for '" + decl.name + "'")
                .with_code("E0520")
                .label("a second '" + word + "' accessor")
                .label(earlier->keyword, "the first '" + word + "' accessor is here")
                .help("remove one of them");
        }
        seen[clause.setter ? 1 : 0] = &clause;
    }
    AccessorOverride entry{&decl, no_symbol, no_symbol};
    for (const auto& clause : decl.accessors) {
        const auto function = add(SymbolKind::Function, (clause.setter ? "$set_" : "$get_") + decl.name,
                                  clause.span, owner);
        symbol(function).exported = true;
        symbol(function).has_body = true;
        entries_[function].resolution = Resolution::Resolved;
        if (clause.setter) {
            const auto parameter = add(SymbolKind::Parameter, "value", clause.keyword, function);
            entries_[function].names.emplace("value", parameter);
            symbol(function).parameters.push_back(parameter);
        }
        accessor_clauses_.emplace(function, &clause);
        (clause.setter ? entry.set : entry.get) = function;
    }
    accessor_overrides_[owner].push_back(entry);
    return no_symbol;
}

// The hidden method named `name` that class `cls` itself declares.
SymbolId Lowering::own_hidden_method(SymbolId cls, const std::string& name) const {
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind == SymbolKind::Function && item.owner == cls && item.name == name) return id;
    }
    return no_symbol;
}

// The accessor of kind `setter` of property `name` that the nearest class
// from `start` up its base chain declares.
SymbolId Lowering::nearest_accessor(SymbolId start, const std::string& name, bool setter) const {
    const auto method = (setter ? "$set_" : "$get_") + name;
    std::set<SymbolId> seen;
    for (auto scope = start; scope != no_symbol && seen.insert(scope).second; scope = symbol(scope).base) {
        if (const auto found = own_hidden_method(scope, method); found != no_symbol) return found;
    }
    return no_symbol;
}

// Whether accessor `function` is a slot or an override, which a subclass may
// override.
bool Lowering::overridable_accessor(SymbolId function) const {
    return function != no_symbol &&
           (has_attribute(symbol(function), AttributeId::AccessorVirtual) || symbol(function).overrides != no_symbol);
}

// The method a static call of accessor `function` reaches: its twin when
// it can be overridden, and itself otherwise, since no subclass can declare
// a method of its name.
SymbolId Lowering::static_accessor(SymbolId function) {
    if (!overridable_accessor(function)) return function;
    return ensure_twin(function);
}

// Lowers the body of accessor clause `clause` into `function`, which serves
// `field` and has the field's type.
void Lowering::lower_clause_body(SymbolId function, const AccessorClause& clause, SymbolId field,
                                 const IrType& field_type) {
    const auto& name = symbol(field).name;
    auto ctx = context(function);
    ctx.function = function;
    std::vector<IrNode> instructions;
    if (clause.expression) {
        expected_origin_ = ExpectedOrigin{name_span(field), "'" + name + "' has type " + describe(field_type) +
                                                                ", which its getter returns"};
        auto result = node(IrOp::Return, clause.expression->span);
        result.operands.push_back(value(*clause.expression, ctx, &field_type));
        instructions.push_back(std::move(result));
    } else {
        instructions = body(clause.body, ctx);
        if (!clause.setter && !always_returns(instructions))
            throw Diagnostic(clause.keyword, "the getter of '" + name + "' may exit without returning " +
                                                 describe(field_type)).with_code("E0405")
                .label("the getter must return " + describe(field_type) + " on every path")
                .note("a path through the body ends without 'return' or a trap");
    }
    program_.bodies.push_back({function, std::move(instructions)});
}

// The twin of a slot or override `function`: a method that only its class
// declares, whose body is the same clause lowered again. The symbol exists
// from the first request; its body is lowered after every declaration
// (lower_pending_twins), with the context the clause had.
SymbolId Lowering::ensure_twin(SymbolId function) {
    const auto owner = symbol(function).owner;
    const auto name = symbol(function).name + "$" + std::to_string(class_depth(owner));
    if (const auto existing = own_hidden_method(owner, name); existing != no_symbol) return existing;
    const auto* of = find_attribute(symbol(function), AttributeId::AccessorOf);
    const auto clause = accessor_clauses_.find(function);
    if (!of || clause == accessor_clauses_.end()) return function;
    const auto field = of->value.items[0].symbol;
    const bool setter = of->value.items[1].integer == 1;
    resolve(field);
    const auto field_type = substitute(symbol(field).type, receiver_arguments(symbol(owner).type, symbol(field).owner));
    const auto twin = add(SymbolKind::Function, name, clause->second->span, owner);
    symbol(twin).exported = true;
    symbol(twin).has_body = true;
    symbol(twin).type = setter ? void_type : field_type;
    entries_[twin].resolution = Resolution::Resolved;
    if (setter) {
        const auto parameter = add(SymbolKind::Parameter, "value", clause->second->keyword, twin);
        symbol(parameter).type = field_type;
        entries_[twin].names.emplace("value", parameter);
        symbol(twin).parameters.push_back(parameter);
    }
    std::optional<OverrideClause> context;
    if (const auto found = override_contexts_.find(function); found != override_contexts_.end()) context = found->second;
    pending_twins_.push_back({twin, clause->second, field, field_type, context});
    return twin;
}

void Lowering::lower_pending_twins() {
    for (std::size_t i = 0; i < pending_twins_.size(); ++i) {
        const auto pending = pending_twins_[i];
        const auto saved = override_clause_;
        override_clause_ = pending.context;
        lower_clause_body(pending.twin, *pending.clause, pending.field, pending.field_type);
        override_clause_ = saved;
    }
    pending_twins_.clear();
}

// Checks and lowers the accessor overrides of class `id` (E0575), once its
// base is resolved.
void Lowering::lower_accessor_overrides(SymbolId id) {
    const auto found = accessor_overrides_.find(id);
    if (found == accessor_overrides_.end()) return;
    const auto overrides = found->second;
    accessor_overrides_.erase(found);
    resolve(id);
    const auto base = symbol(id).base;
    // The overrides of the bases first, whose links and twins these use.
    for (auto scope = base; scope != no_symbol; scope = symbol(scope).base) lower_accessor_overrides(scope);
    for (const auto& entry : overrides) {
        const auto& decl = *entry.decl;
        const auto name = decl.name_span.value_or(decl.span);
        const auto base_name = base != no_symbol ? symbol(base).name : symbol(id).name;
        const auto field = base != no_symbol ? member(base, decl.name, true) : no_symbol;
        if (field == no_symbol || symbol(field).kind != SymbolKind::Field || property_requirement(field)) {
            auto error = Diagnostic(name, "invalid accessor override").with_code("E0575")
                .label("'" + decl.name + "' overrides no property of '" + base_name + "'");
            if (base != no_symbol) {
                std::vector<std::string> names;
                for (auto scope = base; scope != no_symbol; scope = symbol(scope).base) {
                    for (const auto& [member_name, member_id] : entries_[scope].names)
                        if (symbol(member_id).kind == SymbolKind::Field && symbol(member_id).exported)
                            names.push_back(member_name);
                }
                suggest_name(error, name, decl.name, names, "a property");
            }
            throw error.help("an accessor override names an exported field of a base class");
        }
        resolve(field);
        const auto field_type = substitute(symbol(field).type, receiver_arguments(symbol(id).type, symbol(field).owner));
        auto ctx = context(id);
        const auto written = decl.type ? type(*decl.type, ctx) : field_type;
        if (written != field_type)
            throw Diagnostic(decl.type ? decl.type->span : name, "invalid accessor override").with_code("E0575")
                .label("an accessor override repeats the property's type '" + describe(field_type) + "'")
                .label(name_span(field), "'" + decl.name + "' has type " + describe(field_type) + " here");
        for (const auto& clause : decl.accessors) {
            const auto function = clause.setter ? entry.set : entry.get;
            const auto slot = nearest_accessor(base, decl.name, clause.setter);
            if (!overridable_accessor(slot)) {
                const auto word = std::string(clause.setter ? "set" : "get");
                auto error = Diagnostic(clause.dispatch_words.front().second, "invalid accessor override")
                    .with_code("E0575")
                    .label("'" + symbol(symbol(field).owner).name + "." + decl.name + "' has no virtual " +
                           (clause.setter ? "setter" : "getter") + " to override");
                if (slot != no_symbol && user_source(symbol(slot).span.file))
                    error.label(symbol(slot).span, "this clause is not '.virtual'");
                throw error.help("mark it '| " + word + ".virtual:'");
            }
            symbol(function).overrides = slot;
            set_attribute(symbol(function), AttributeId::AccessorOf,
                          accessor_of_payload(field, clause.setter ? AccessorKind::Set : AccessorKind::Get));
            if (clause.setter) {
                symbol(function).type = void_type;
                symbol(symbol(function).parameters.front()).type = field_type;
            } else {
                symbol(function).type = field_type;
            }
        }
        // Inside the clauses, the property's name is the nearest base's
        // accessor, called statically.
        const auto saved = override_clause_;
        const OverrideClause context{field, static_accessor(nearest_accessor(base, decl.name, false)),
                                     static_accessor(nearest_accessor(base, decl.name, true))};
        for (const auto& clause : decl.accessors) {
            const auto function = clause.setter ? entry.set : entry.get;
            override_contexts_.emplace(function, context);
            override_clause_ = context;
            lower_clause_body(function, clause, field, field_type);
            override_clause_ = saved;
            ensure_twin(function);
        }
    }
}

// The property's name inside one of its override clauses (route_field).
IrNode Lowering::override_access(IrNode field, const Expr& expr) {
    const auto& clause = *override_clause_;
    const auto& name = symbol(clause.field).name;
    if (&expr == place_expr_) {
        if (clause.set == no_symbol)
            throw Diagnostic(expr.span, "'" + name + "' has no setter in the base class").with_code("E0510")
                .label("inside an override, '" + name + "' calls the base class's accessor")
                .help("assign it through a setter of the base class, or override only the getter");
        field.text = "$override";
        return field;
    }
    if (clause.get == no_symbol)
        throw Diagnostic(expr.span, "'" + name + "' has no getter in the base class").with_code("E0515")
            .label("inside an override, '" + name + "' calls the base class's accessor");
    auto call = node(IrOp::VirtualCall, field.span, field.type);
    call.symbol = clause.get;
    call.operands = std::move(field.operands);
    return call;
}

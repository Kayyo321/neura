// Class hooks (docs/design/Hooks030.md, Class hook clauses).
//
// Included by lowering.cpp inside its anonymous namespace: these are members
// of Lowering. Each clause `| get: ...;` or `| set: ...;` of a declared class
// becomes a hidden static function of the class: `$class_get(instance,
// $policy) -> C` or `$class_set(value, instance, $policy) -> C`. The class
// carries the attribute hook.get or hook.set naming the function, and the
// function carries hook.of: [the class, 0 for get or 1 for set]. `instance`
// and `value` are its immutable parameters, so no other source name reaches
// them; `$policy` is the uint32 policy word the converter passes, a name no
// source can write. The functions are not in the class's name table.
//
// The checks that need the class's base and tags run when the class is
// lowered: E0538 (here, at declaration), E0539, E0540, E0541, E0542, E0543.
// E0545 runs once over the program, in check_hooked_storage. E0544 and
// E0546 need the linked, specialized program and run in elaboration.

namespace hook_text {

inline std::string quoted(const std::string& name) { return "'" + name + "'"; }

// True when `expr` is a `new` expression.
inline bool is_new(const Expr& expr) { return expr.kind == ExprKind::New; }

// The `new` expression a clause returns directly, when it has one.
inline const Expr* returned_new(const std::vector<Statement>& statements) {
    for (const auto& item : statements) {
        if (item.kind == StmtKind::Return && !item.expressions.empty() && is_new(item.expressions.front()))
            return &item.expressions.front();
        for (const auto* region : {&item.body, &item.alternative}) {
            if (const auto* found = returned_new(*region)) return found;
        }
        for (const auto& arm : item.arms) {
            if (const auto* found = returned_new(arm.body)) return found;
        }
    }
    return nullptr;
}

// The spans of the values the clause's `return` statements give.
inline void return_spans(const std::vector<Statement>& statements, std::vector<Span>& spans) {
    for (const auto& item : statements) {
        if (item.kind == StmtKind::Return && !item.expressions.empty()) spans.push_back(item.expressions.front().span);
        for (const auto* region : {&item.body, &item.alternative, &item.initializer, &item.step})
            return_spans(*region, spans);
        for (const auto& arm : item.arms) return_spans(arm.body, spans);
        for (const auto& arm : item.select_arms) return_spans(arm.body, spans);
    }
}

inline bool same_span(const Span& a, const Span& b) {
    return a.file == b.file && a.offset == b.offset && a.length == b.length;
}

} // namespace hook_text

void Lowering::declare_hooks(const Declaration& decl, SymbolId cls) {
    const HookClause* seen[2] = {nullptr, nullptr};
    for (const auto& clause : decl.hooks) {
        const auto kind = clause.setter ? HookKind::Set : HookKind::Get;
        const auto word = std::string(hook_word(kind));
        if (const auto* earlier = seen[clause.setter ? 1 : 0])
            throw Diagnostic(clause.keyword, "class " + hook_text::quoted(decl.name) + " already has a '" + word +
                                                 "' hook").with_code("E0538")
                .label("a second '" + word + "' clause")
                .label(earlier->keyword, "the first '" + word + "' clause is here")
                .help("remove one of them; a class has at most one 'get' and one 'set' clause");
        seen[clause.setter ? 1 : 0] = &clause;
        const auto function = add(SymbolKind::Function, clause.setter ? "$class_set" : "$class_get", clause.span,
                                  static_scope(cls));
        symbol(function).has_body = true;
        entries_[function].resolution = Resolution::Resolved;
        const auto parameter = [&](const std::string& name, const Span& span) {
            const auto id = add(SymbolKind::Parameter, name, span, function);
            symbol(id).constant = true;
            entries_[function].names.emplace(name, id);
            symbol(function).parameters.push_back(id);
            return id;
        };
        if (clause.setter) parameter("value", clause.keyword);
        parameter("instance", clause.keyword);
        parameter("$policy", clause.keyword);
        set_attribute(symbol(cls), clause.setter ? AttributeId::HookSet : AttributeId::HookGet,
                      attribute_symbol(function));
        set_attribute(symbol(function), AttributeId::HookOf,
                      attribute_list({attribute_symbol(cls), attribute_unsigned(clause.setter ? 1 : 0)}));
        hook_clauses_.emplace(function, &clause);
    }
}

// The #Synchronized method of `cls` or of one of its bases, or none.
SymbolId Lowering::synchronized_method(SymbolId cls) const {
    for (auto current = cls; current != no_symbol; current = symbol(current).base) {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.owner == current && item.kind == SymbolKind::Function && has_tag(item, TagId::Synchronized))
                return id;
        }
    }
    return no_symbol;
}

void Lowering::lower_hooks(SymbolId cls) {
    const auto base = symbol(cls).base;
    const auto& name = symbol(cls).name;
    std::optional<SymbolId> synchronized;
    const auto guarded = [&] {
        if (!synchronized) synchronized = synchronized_method(cls);
        return *synchronized;
    };
    for (const auto kind : {HookKind::Get, HookKind::Set}) {
        const auto word = std::string(hook_word(kind));
        const auto function = declared_hook(symbol(cls), kind);
        const auto inherited = base == no_symbol ? no_symbol : effective_hook(program_, base, kind);
        if (function == no_symbol) {
            // The stricter reading of E0541: a class that inherits a clause
            // cannot add a synchronized method either.
            if (inherited != no_symbol && guarded() != no_symbol && symbol(guarded()).owner == cls) {
                auto error = Diagnostic(name_span(guarded()), "a hook would read a " + hook_text::quoted(name) +
                                                                  " outside its guard").with_code("E0541")
                    .label("a #Synchronized method of a class with a '" + word + "' hook");
                if (user_source(symbol(inherited).span.file))
                    error.label(symbol(inherited).span, "the inherited '" + word + "' hook");
                throw error.help("validate in the synchronized methods instead of a class hook");
            }
            continue;
        }
        const auto& clause = *hook_clauses_.at(function);
        if (const auto method = guarded(); method != no_symbol) {
            auto error = Diagnostic(clause.span, "a hook would read a " + hook_text::quoted(name) +
                                                     " outside its guard").with_code("E0541")
                .label("a class hook on a class with synchronized methods");
            if (user_source(symbol(method).span.file)) error.label(name_span(method), "this method is #Synchronized");
            throw error.help("validate in the synchronized methods instead of a class hook");
        }
        if (inherited != no_symbol && !clause.override_word) {
            const auto from = hook_owner(symbol(inherited));
            auto error = Diagnostic(clause.keyword, "class " + hook_text::quoted(name) + " inherits a '" + word +
                                                        "' hook from " + hook_text::quoted(symbol(from).name))
                .with_code("E0539")
                .label("this clause replaces the inherited one");
            if (user_source(symbol(inherited).span.file))
                error.label(symbol(inherited).span, "the inherited '" + word + "' hook");
            const Span after{clause.keyword.file, clause.keyword.line, clause.keyword.column + 3,
                             clause.keyword.offset + clause.keyword.length, 0};
            throw error.suggest("mark the clause as replacing it", after, ".override",
                                Applicability::MachineApplicable);
        }
        if (inherited == no_symbol && clause.override_word) {
            const auto& word_span = *clause.override_word;
            const Span dotted{clause.keyword.file, clause.keyword.line, clause.keyword.column + 3,
                              clause.keyword.offset + clause.keyword.length,
                              word_span.offset + word_span.length - (clause.keyword.offset + clause.keyword.length)};
            throw Diagnostic(word_span, "no base of " + hook_text::quoted(name) + " has a '" + word + "' hook")
                .with_code("E0540")
                .label("there is no '" + word + "' hook to override")
                .suggest("remove '.override'", dotted, "", Applicability::MachineApplicable);
        }
        const auto class_type = symbol(cls).type;
        symbol(function).type = class_type;
        for (const auto parameter : symbol(function).parameters)
            symbol(parameter).type = symbol(parameter).name == "$policy" ? uint32_type : class_type;
        auto ctx = context(function);
        ctx.function = function;
        std::vector<Span> results;
        if (clause.expression) results.push_back(clause.expression->span);
        hook_text::return_spans(clause.body, results);
        std::vector<IrNode> instructions;
        try {
            if (clause.expression) {
                auto result = node(IrOp::Return, clause.expression->span);
                result.operands.push_back(value(*clause.expression, ctx, &class_type));
                instructions.push_back(std::move(result));
            } else {
                instructions = body(clause.body, ctx);
            }
        } catch (const Diagnostic& error) {
            if (error.code() != "E0403" || std::ranges::none_of(results, [&](const Span& span) {
                    return hook_text::same_span(span, error.span);
                }))
                throw;
            auto converted = Diagnostic(error.span, std::string("a '") + word + "' hook of " + hook_text::quoted(name) +
                                                        " yields a " + hook_text::quoted(name) + ", found " +
                                                        describe_found(error.what()))
                .with_code("E0542")
                .label("a hook's result is a " + hook_text::quoted(name) + " or nil");
            throw converted.help("return 'instance', 'value', 'nil' or another " + hook_text::quoted(name));
        }
        // After the types are checked, so that a `new` of another class is
        // the E0542 it is.
        const auto* created = clause.expression && hook_text::is_new(*clause.expression)
                                  ? &*clause.expression : hook_text::returned_new(clause.body);
        if (created && !has_tag(symbol(cls), TagId::Final))
            throw Diagnostic(created->span, "a subclass of " + hook_text::quoted(name) +
                                                " would inherit this hook and receive a " + hook_text::quoted(name))
                .with_code("E0543")
                .label("a new " + hook_text::quoted(name) + " for every subclass's references")
                .note("a hook's result must be nil or an object of exactly the class of the reference it handles")
                .help("mark the class #Final, or return 'instance', 'value', 'nil' or an existing object of the "
                      "same class");
        if (!always_returns(instructions))
            throw Diagnostic(clause.close, "this path returns no value").with_code("E0542")
                .label("the '" + word + "' hook of " + hook_text::quoted(name) + " can end here without a result")
                .note("a hook returns a " + hook_text::quoted(name) + " or traps on every path")
                .help("return 'instance' (or 'value' in a setter) at the end of the block");
        program_.bodies.push_back({function, std::move(instructions)});
    }
}

// The type a mismatch diagnostic found, from its message "expected T, got U".
std::string Lowering::describe_found(const std::string& message) {
    const auto at = message.rfind(", got ");
    return at == std::string::npos ? std::string("another type") : "'" + message.substr(at + 6) + "'";
}

// E0545: a declaration of a class type that has an effective setter, with no
// initializer, among what this compilation declared. A deferred `last`
// binding has no default and is exempt; `= nil` is an initializer.
void Lowering::check_hooked_storage(SymbolId first) {
    for (SymbolId id = first; id < program_.symbols.size(); ++id) {
        const auto& item = program_.symbols[id];
        if (item.kind != SymbolKind::Field && item.kind != SymbolKind::Global && item.kind != SymbolKind::Local)
            continue;
        if (id >= entries_.size() || !entries_[id].declaration) continue;
        const auto& decl = *entries_[id].declaration;
        if (decl.kind != DeclKind::Binding || decl.initializer || decl.last || decl.name == "_" || !decl.type)
            continue;
        if (item.type.kind != TypeKind::Object || item.type.symbol == no_symbol) continue;
        const auto setter = effective_hook(program_, item.type.symbol, HookKind::Set);
        if (setter == no_symbol) continue;
        const auto& cls = symbol(item.type.symbol).name;
        const auto at = name_span(id);
        auto error = Diagnostic(at, hook_text::quoted(cls) + " has a 'set' hook, so " + hook_text::quoted(decl.name) +
                                        " needs an initializer").with_code("E0545")
            .label("declared without an initializer");
        if (user_source(symbol(setter).span.file)) error.label(symbol(setter).span, "the 'set' hook");
        const auto end = decl.type->span;
        if (const auto after = source_span(end.file, end.offset + end.length, 0))
            error.suggest("initialize it with nil, which the 'set' hook then admits when the declaration runs",
                          *after, " = nil", Applicability::MaybeIncorrect);
        throw error.note("a default nil would be an admission the source does not show");
    }
}

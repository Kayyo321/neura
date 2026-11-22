// Laws031.md: translate ordinary typed NeuraIR expressions and proof syntax
// into a separate certificate. The kernel derives every judgment afresh.

struct Lowering::LawEnvironment {
    Context context;
    SymbolId owner;
    std::map<SymbolId, LawTerm> values;
    std::map<std::string, std::pair<std::uint32_t, LawTerm>, std::less<>> evidence;
    std::uint32_t next = 0;
};

LawTerm law_truth(LawTerm value, const Span& span) {
    LawTerm yes{LawTermKind::Boolean, bool_type, span};
    yes.index = 1;
    return {LawTermKind::Equal, bool_type, span, no_symbol, no_symbol, 0, {std::move(value), std::move(yes)}};
}

[[noreturn]] void unsupported_law(const Span& span, std::string feature, unsigned phase) {
    throw Diagnostic(span, std::move(feature) + " is deferred to Phase " + std::to_string(phase)).with_code("E1421");
}

[[noreturn]] void bad_law_type(const Span& span, std::string message) {
    throw Diagnostic(span, std::move(message)).with_code("E1409");
}

std::uint32_t Lowering::law_value_binding(LawEnvironment& env, std::string name,
                                          const Span& span, const IrType& type) {
    const auto index = env.next++;
    if (name.empty() || name == "_") bad_law_type(span, "a law binding requires a name");
    auto& scope = env.context.scopes.back();
    if (scope.contains(name) || env.evidence.contains(name))
        bad_law_type(span, "duplicate law binding '" + name + "'");
    const auto id = add(SymbolKind::Parameter, name, span, env.owner);
    symbol(id).type = type;
    symbol(id).constant = true;
    env.context.scopes.back().emplace(std::move(name), id);
    env.values.emplace(id, LawTerm{LawTermKind::Variable, type, span, no_symbol, no_symbol, index});
    return index;
}

LawTerm Lowering::law_term(const Expr& syntax, LawEnvironment& env, const IrType* expected) {
    if (syntax.kind == ExprKind::Spawn || syntax.kind == ExprKind::Import ||
        (syntax.kind == ExprKind::Binary && (syntax.text == "===" || syntax.text == "!==")))
        throw Diagnostic(syntax.span, "host, task and identity operations are forbidden in laws").with_code("E1410");
    if (syntax.kind == ExprKind::Integer || syntax.kind == ExprKind::Float || syntax.kind == ExprKind::String)
        unsupported_law(syntax.span, "proofs over machine numbers and strings", 2);
    if (syntax.kind == ExprKind::Index || syntax.kind == ExprKind::Cast)
        throw Diagnostic(syntax.operator_span.value_or(syntax.span), "this operation may trap in a total law")
            .with_code("E1411").help("use a total operation, or express the checked precondition");

    try {
        auto lowered = value(syntax, env.context, expected);
        return law_term(lowered, env);
    } catch (const MissingPrimitiveWrapper&) {
        throw;
    } catch (Diagnostic& error) {
        if (error.data() && !error.data()->code.starts_with("E14"))
            error.with_code("E1409");
        throw;
    }
}

LawTerm Lowering::law_term(const IrNode& input, LawEnvironment& env) {
    LawTerm out{LawTermKind::Empty, input.type, input.span};
    const auto operands = [&](std::size_t begin = 0) {
        for (std::size_t i = begin; i < input.operands.size(); ++i)
            out.terms.push_back(law_term(input.operands[i], env));
    };
    switch (input.op) {
    case IrOp::Load:
    case IrOp::Reference:
        if (const auto binding = env.values.find(input.symbol); binding != env.values.end()) {
            auto result = binding->second;
            result.span = input.span;
            return result;
        }
        if (input.symbol < program_.symbols.size() && symbol(input.symbol).kind == SymbolKind::Variant) {
            out.kind = LawTermKind::Constructor;
            out.symbol = input.symbol;
            break;
        }
        if (input.symbol < entries_.size() && symbol(input.symbol).kind == SymbolKind::Global &&
            symbol(input.symbol).constant) {
            out.kind = LawTermKind::Constant;
            out.symbol = input.symbol;
            const auto owner = symbol(input.symbol).owner;
            if (owner != no_symbol && symbol(owner).kind == SymbolKind::Module) {
                if (input.operands.empty()) out.receiver = owner;
                else if (input.operands.front().op != IrOp::Receiver)
                    out.receiver = input.operands.front().type.symbol;
            }
            break;
        }
        throw Diagnostic(input.span, "a law cannot read mutable or non-constant storage").with_code("E1410");
    case IrOp::Boolean:
        out.kind = LawTermKind::Boolean;
        out.index = input.text == "true";
        break;
    case IrOp::Nil:
        out.kind = LawTermKind::Nil;
        break;
    case IrOp::Allocate:
        out.kind = LawTermKind::Construct;
        out.symbol = input.symbol;
        operands();
        break;
    case IrOp::Tuple:
        out.kind = LawTermKind::Constructor;
        out.symbol = input.symbol;
        operands();
        break;
    case IrOp::EnumConstruct: {
        out.kind = LawTermKind::Constructor;
        out.symbol = input.symbol;
        const auto order = enum_field_order(input.text, input.operands.size());
        if (!order) bad_law_type(input.span, "invalid constructor payload order");
        out.terms.resize(input.operands.size());
        for (std::size_t i = 0; i < input.operands.size(); ++i)
            out.terms[(*order)[i]] = law_term(input.operands[i], env);
        break;
    }
    case IrOp::Field:
    case IrOp::Project:
    case IrOp::EnumProject:
        out.kind = LawTermKind::Project;
        out.symbol = input.symbol;
        if (!input.text.empty()) {
            const auto [end, error] = std::from_chars(input.text.data(), input.text.data() + input.text.size(), out.index);
            if (error != std::errc{} || end != input.text.data() + input.text.size())
                bad_law_type(input.span, "invalid law projection");
        }
        operands();
        break;
    case IrOp::Call:
    case IrOp::VirtualCall:
        out.kind = LawTermKind::Call;
        out.symbol = input.symbol;
        if (input.op == IrOp::VirtualCall) {
            const auto& receiver = input.operands.front();
            if (receiver.type.kind == TypeKind::Object) {
                operands();
                break;
            }
            if (receiver.type.kind != TypeKind::Module)
                bad_law_type(receiver.span, "Phase 1 laws require statically resolved pure calls");
            if (receiver.op != IrOp::Receiver) out.receiver = receiver.type.symbol;
            operands(1);
        } else {
            const auto owner = symbol(input.symbol).owner;
            if (owner != no_symbol && symbol(owner).kind == SymbolKind::Module) out.receiver = owner;
            operands();
        }
        break;
    case IrOp::Unary:
        if (input.text != "!" || input.operands.front().type != bool_type)
            throw Diagnostic(input.span, "operator may trap or observe class identity in a law").with_code("E1411");
        out.kind = LawTermKind::Not;
        operands();
        break;
    case IrOp::Binary:
        if (input.text == "==" || input.text == "!=") {
            const auto& compared = input.operands.front().type;
            if (compared.kind == TypeKind::Object &&
                std::ranges::none_of(input.operands, [](const IrNode& node) { return node.op == IrOp::Nil; }))
                throw Diagnostic(input.span, "object identity cannot be observed in a law").with_code("E1410");
            out.kind = LawTermKind::BooleanEqual;
            operands();
            if (input.text == "!=") return {LawTermKind::Not, bool_type, input.span, no_symbol, no_symbol, 0, {out}};
            break;
        }
        throw Diagnostic(input.span, "unchecked potential trap in a total law").with_code("E1411")
            .help("use a total operation in a Phase 1 law");
    case IrOp::ShortCircuit:
        out.kind = input.text == "&&" ? LawTermKind::And : LawTermKind::Or;
        operands();
        out.terms.push_back(law_term(input.body.front(), env));
        break;
    case IrOp::SelectValue:
        if (input.operands.size() != 1 || input.body.size() != 1 || input.alternative.size() != 1)
            bad_law_type(input.span, "unsupported conditional expression in a Phase 1 law");
        out.kind = LawTermKind::If;
        operands();
        out.terms.push_back(law_term(input.body.front(), env));
        out.terms.push_back(law_term(input.alternative.front(), env));
        break;
    case IrOp::ExternalCall:
    case IrOp::SchedulerCall:
    case IrOp::Spawn:
    case IrOp::ClassHash:
    case IrOp::ClassText:
    case IrOp::ValueEquals:
    case IrOp::HookRead:
        throw Diagnostic(input.span, "host, task, identity and hooked operations are forbidden in laws")
            .with_code("E1410");
    case IrOp::Index:
    case IrOp::ClassCast:
    case IrOp::Convert:
    case IrOp::TrapMessage:
    case IrOp::Fail:
        throw Diagnostic(input.span, "this operation may trap in a total law").with_code("E1411");
    case IrOp::Integer:
    case IrOp::Float:
    case IrOp::String:
        unsupported_law(input.span, "proofs over machine numbers and strings", 2);
    default:
        bad_law_type(input.span, "expression is unavailable in Phase 1 law propositions");
    }
    return out;
}

SymbolId Lowering::law_name(const Expr& syntax, LawEnvironment& env, SymbolId* explicit_receiver) {
    if (syntax.kind == ExprKind::Name)
        return find(syntax.text, env.context, syntax.span, explicit_receiver);
    if (syntax.kind != ExprKind::Member) return no_symbol;
    auto module = law_name(syntax.operands.front(), env);
    if (module == no_symbol) return module;
    resolve(module);
    if (symbol(module).kind == SymbolKind::Import) module = symbol(module).type.symbol;
    if (symbol(module).kind != SymbolKind::Module) return no_symbol;
    const auto id = member(module, syntax.text, !inside(env.context.owner, module));
    if (explicit_receiver) *explicit_receiver = module;
    return id;
}

LawTerm Lowering::law_proposition(const PropositionSyntax& syntax, LawEnvironment& env) {
    using Kind = PropositionSyntax::Kind;
    switch (syntax.kind) {
    case Kind::Equality: {
        auto expected = syntax.type ? std::optional{type(*syntax.type, env.context)} : std::nullopt;
        auto left = law_term(syntax.expressions[0], env, expected ? &*expected : nullptr);
        auto right = law_term(syntax.expressions[1], env, &left.type);
        auto result_type = expected.value_or(left.type);
        return {LawTermKind::Equal, std::move(result_type), syntax.span, no_symbol, no_symbol, 0,
                {std::move(left), std::move(right)}};
    }
    case Kind::Boolean: {
        const auto& expression = syntax.expressions.front();
        const bool call = expression.kind == ExprKind::Call;
        const auto& callee = call ? expression.operands.front() : expression;
        SymbolId receiver = no_symbol;
        const auto target = law_name(callee, env, &receiver);
        if (target == no_symbol || symbol(target).kind != SymbolKind::Law)
            return law_truth(law_term(expression, env, &bool_type), syntax.span);

        program_.name_uses.emplace_back(callee.span, target);
        const auto& record = resolve_law_statement(target, callee.span);
        auto statement = record.statement;
        const auto introductions = record.introductions;
        std::map<std::uint32_t, LawTerm> inputs;
        std::size_t argument = 1;
        auto* claim = &statement;
        std::vector<LawTerm> hypotheses;
        for (std::uint32_t i = 0; i < introductions; ++i) {
            if (claim->kind == LawTermKind::Implies) {
                hypotheses.push_back(claim->terms.front());
            } else {
                if (argument >= expression.operands.size())
                    bad_law_type(callee.span, "missing quantified argument to law '" + symbol(target).name + "'");
                inputs.emplace(claim->index, law_term(expression.operands[argument++], env, &claim->type));
            }
            claim = &claim->terms.back();
        }
        if (call && argument != expression.operands.size())
            bad_law_type(expression.operands[argument].span, "too many arguments to law '" + symbol(target).name + "'");
        const auto instantiate = [&](const auto& self, const LawTerm& source) -> LawTerm {
            if (source.kind == LawTermKind::Variable) {
                if (const auto input = inputs.find(source.index); input != inputs.end()) return input->second;
            }
            auto term = source;
            if (source.kind == LawTermKind::Forall || source.kind == LawTermKind::NullableForall ||
                source.kind == LawTermKind::Exists) {
                term.index = env.next++;
                inputs.emplace(source.index, LawTerm{LawTermKind::Variable, source.type, source.span,
                                                      no_symbol, no_symbol, term.index});
            }
            if (receiver != no_symbol && term.receiver == no_symbol &&
                (term.kind == LawTermKind::Call || term.kind == LawTermKind::Constant)) {
                const auto owner = symbol(term.symbol).owner;
                for (auto module = receiver; module != no_symbol; module = symbol(module).base) {
                    if (module != owner) continue;
                    term.receiver = receiver;
                    break;
                }
            }
            term.terms.clear();
            for (const auto& subterm : source.terms) term.terms.push_back(self(self, subterm));
            return term;
        };
        auto result = instantiate(instantiate, *claim);
        for (auto it = hypotheses.rbegin(); it != hypotheses.rend(); ++it)
            result = {LawTermKind::Implies, {}, syntax.span, no_symbol, no_symbol, law_no_binding,
                      {instantiate(instantiate, *it), std::move(result)}};
        return result;
    }
    case Kind::All:
    case Kind::Either: {
        LawTerm result{syntax.kind == Kind::All ? LawTermKind::All : LawTermKind::Either, {}, syntax.span};
        for (const auto& part : syntax.propositions) result.terms.push_back(law_proposition(part, env));
        return result;
    }
    case Kind::Not:
        return {LawTermKind::Implies, {}, syntax.span, no_symbol, no_symbol, law_no_binding,
                {law_proposition(syntax.propositions.front(), env), LawTerm{LawTermKind::Empty, {}, syntax.span}}};
    case Kind::Given:
        return {LawTermKind::Implies, {}, syntax.span, no_symbol, no_symbol, law_no_binding,
                {law_proposition(syntax.propositions[0], env), law_proposition(syntax.propositions[1], env)}};
    case Kind::For: {
        const auto saved = env.context.scopes.size();
        env.context.scopes.emplace_back();
        std::vector<LawTerm> quantifiers;
        for (const auto& param : syntax.parameters) {
            auto domain = type(param.type, env.context);
            const auto index = law_value_binding(env, param.name, param.span, domain);
            quantifiers.push_back({LawTermKind::Forall, std::move(domain), param.span, no_symbol, no_symbol, index});
        }
        auto body = law_proposition(syntax.propositions.front(), env);
        env.context.scopes.resize(saved);
        for (auto it = quantifiers.rbegin(); it != quantifiers.rend(); ++it) {
            it->terms.push_back(std::move(body));
            body = std::move(*it);
        }
        return body;
    }
    case Kind::NoTrap:
    case Kind::Traps:
    case Kind::Outcome:
        unsupported_law(syntax.span, "trap and outcome propositions", 2);
    }
    bad_law_type(syntax.span, "unknown law proposition");
}

Lowering::LawEnvironment Lowering::law_statement(IrLawRecord& record, const Declaration& decl) {
    using Kind = LawItemSyntax::Kind;
    LawEnvironment env{context(record.owner), record.law_symbol, {}, {}};
    env.context.scopes.emplace_back();
    std::vector<LawTerm> introductions;
    std::vector<LawTerm> existentials;
    std::vector<LawTerm> claims;
    for (const auto& item : decl.law->items) {
        switch (item.kind) {
        case Kind::For:
        case Kind::Exists:
            for (const auto& parameter : item.parameters) {
                auto domain = type(parameter.type, env.context);
                if (domain.kind == TypeKind::Integer || domain.kind == TypeKind::Float || domain.kind == TypeKind::String)
                    unsupported_law(parameter.type.span, "proofs over machine numbers and strings", 2);
                if (item.nullable && domain.kind != TypeKind::Object)
                    bad_law_type(parameter.type.span, "'for?' requires an immutable class type");
                if (parameter.default_value || parameter.variadic)
                    bad_law_type(parameter.span, "a law quantifier cannot be variadic or have a default");
                const auto index = law_value_binding(env, parameter.name, parameter.span, domain);
                const auto id = env.context.scopes.back().find(parameter.name);
                if (id != env.context.scopes.back().end()) symbol(record.law_symbol).parameters.push_back(id->second);
                const auto kind = item.kind == Kind::Exists ? LawTermKind::Exists
                    : item.nullable ? LawTermKind::NullableForall : LawTermKind::Forall;
                auto& binders = item.kind == Kind::Exists ? existentials : introductions;
                binders.push_back({kind, std::move(domain), parameter.span, no_symbol, no_symbol, index});
            }
            break;
        case Kind::Given: {
            auto statement = law_proposition(*item.proposition, env);
            const auto index = item.binding ? env.next++ : law_no_binding;
            if (item.binding) {
                const auto& name = item.binding->text;
                if (env.context.scopes.back().contains(name) ||
                    !env.evidence.emplace(name, std::pair{index, statement}).second)
                    bad_law_type(item.binding->span, "duplicate hypothesis '" + name + "'");
                const auto id = add(SymbolKind::Parameter, name, item.binding->span, record.law_symbol);
                symbol(id).constant = true;
                symbol(id).type = void_type;
                symbol(record.law_symbol).parameters.push_back(id);
            }
            introductions.push_back({LawTermKind::Implies, {}, item.span, no_symbol, no_symbol, index,
                                     {std::move(statement)}});
            break;
        }
        case Kind::Last: {
            const auto expected = item.type ? std::optional{type(*item.type, env.context)} : std::nullopt;
            auto value = law_term(item.expressions.front(), env, expected ? &*expected : nullptr);
            const auto& name = item.binding->text;
            law_value_binding(env, name, item.binding->span, value.type);
            env.values[env.context.scopes.back().at(name)] = value;
            record.abbreviations.push_back({name, std::move(value)});
            break;
        }
        case Kind::Claim:
            claims.push_back(law_proposition(*item.proposition, env));
            break;
        case Kind::Holds:
            claims.push_back(law_truth(law_term(item.expressions.front(), env, &bool_type), item.span));
            break;
        case Kind::Refutes:
            claims.push_back({LawTermKind::Empty, {}, item.span});
            break;
        case Kind::Measure:
            unsupported_law(item.keyword, "measure-based induction", 2);
        case Kind::NoTrap:
        case Kind::Traps:
        case Kind::Outcome:
            unsupported_law(item.keyword, "trap and outcome claims", 2);
        }
    }
    if (claims.empty())
        throw Diagnostic(decl.law->close, "a law must declare a claim").with_code("E1403");
    auto body = claims.size() == 1 ? std::move(claims.front())
        : LawTerm{LawTermKind::All, {}, decl.law->keyword, no_symbol, no_symbol, 0, std::move(claims)};
    const auto quantify = [&](std::vector<LawTerm>& terms) {
        for (auto it = terms.rbegin(); it != terms.rend(); ++it) {
            it->terms.push_back(std::move(body));
            body = std::move(*it);
        }
    };
    quantify(existentials);
    record.introductions = static_cast<std::uint32_t>(introductions.size());
    quantify(introductions);
    record.statement = std::move(body);
    return env;
}

const LawTerm* law_goal(const IrLawRecord& record) {
    const auto* goal = &record.statement;
    for (std::uint32_t i = 0; i < record.introductions; ++i) goal = &goal->terms.back();
    return goal;
}

std::uint32_t next_law_binder(const LawTerm& term) {
    auto next = term.index < law_wildcard_arm ? term.index + 1 : 0;
    for (const auto& nested : term.terms) next = std::max(next, next_law_binder(nested));
    return next;
}

Lowering::LawEnvironment Lowering::inherited_law_environment(const IrLawRecord& law, SymbolId module) {
    LawEnvironment env{context(module), law.law_symbol, {}, {}};
    env.context.scopes.emplace_back();
    env.next = next_law_binder(law.statement);
    const auto& parameters = symbol(law.law_symbol).parameters;
    const LawTerm* item = &law.statement;
    std::size_t pos = 0;
    for (std::uint32_t i = 0; i < law.introductions; ++i) {
        // Named hypotheses carry void entries. Anonymous hypotheses have
        // no reusable source name but still have a proof binder.
        if (item->index == law_no_binding) {
            item = &item->terms.back();
            continue;
        }
        if (pos < parameters.size()) {
            auto id = parameters[pos++];
            const auto& parameter = symbol(id);
            if (item->kind == LawTermKind::Implies)
                env.evidence.emplace(parameter.name, std::pair{item->index, item->terms.front()});
            else {
                env.context.scopes.back().emplace(parameter.name, id);
                env.values.emplace(id, LawTerm{LawTermKind::Variable, item->type, item->span,
                                              no_symbol, no_symbol, item->index});
            }
        }
        item = &item->terms.back();
    }
    while (item->kind == LawTermKind::Exists && pos < parameters.size()) {
        const auto id = parameters[pos++];
        env.context.scopes.back().emplace(symbol(id).name, id);
        env.values.emplace(id, LawTerm{LawTermKind::Variable, item->type, item->span,
                                      no_symbol, no_symbol, item->index});
        item = &item->terms.back();
    }
    for (const auto& [name, value] : law.abbreviations) {
        law_value_binding(env, name, value.span, value.type);
        env.values[env.context.scopes.back().at(name)] = value;
    }
    return env;
}

LawProof Lowering::law_evidence(const Expr& syntax, LawEnvironment& env) {
    if (syntax.kind == ExprKind::Name) {
        if (const auto given = env.evidence.find(syntax.text); given != env.evidence.end())
            return {LawProofKind::Assumption, syntax.span, no_symbol, no_symbol, given->second.first};
    }
    const bool call = syntax.kind == ExprKind::Call;
    const auto& name = call ? syntax.operands.front() : syntax;
    if (call && name.kind == ExprKind::Name && (name.text == "sym" || name.text == "trans")) {
        LawProof result{name.text == "sym" ? LawProofKind::Sym : LawProofKind::Trans, syntax.span};
        for (std::size_t i = 1; i < syntax.operands.size(); ++i)
            result.proofs.push_back(law_evidence(syntax.operands[i], env));
        return result;
    }
    if (call && name.kind == ExprKind::Name && name.text == "cong") {
        if (syntax.operands.size() != 3)
            bad_law_type(syntax.span, "cong requires a function and an equality witness");
        auto fact = law_evidence(syntax.operands[2], env);
        const auto equation = law_evidence_type(fact, env);
        if (!equation || equation->kind != LawTermKind::Equal)
            bad_law_type(syntax.operands[2].span, "cong requires equality evidence");
        const auto& callee = syntax.operands[1];
        SymbolId receiver = no_symbol;
        const auto function = law_name(callee, env, &receiver);
        if (function == no_symbol || symbol(function).kind != SymbolKind::Function)
            bad_law_type(callee.span, "cong requires a named pure function");
        resolve(function);
        LawTerm applied{LawTermKind::Call, symbol(function).type, callee.span, function, receiver, 0,
                        {LawTerm{LawTermKind::Hole, equation->type, callee.span}}};
        LawProof proof{LawProofKind::Cong, syntax.span};
        proof.terms.push_back(std::move(applied));
        proof.proofs.push_back(std::move(fact));
        return proof;
    }
    SymbolId receiver = no_symbol;
    const auto target = law_name(name, env, &receiver);
    if (target == no_symbol || symbol(target).kind != SymbolKind::Law)
        bad_law_type(name.span, "expected a hypothesis or a law proof");
    program_.name_uses.emplace_back(name.span, target);
    LawProof result{LawProofKind::Apply, syntax.span, target, receiver};
    const auto declaration = std::ranges::find_if(program_.laws, [&](const IrLawRecord& record) {
        return record.law_symbol == target && record.owner == symbol(target).owner;
    });
    if (declaration == program_.laws.end()) bad_law_type(name.span, "law has no typed statement");
    auto term = &declaration->statement;
    for (std::size_t i = 1; i < syntax.operands.size(); ++i) {
        if (term->kind == LawTermKind::Forall || term->kind == LawTermKind::NullableForall) {
            result.terms.push_back(law_term(syntax.operands[i], env, &term->type));
        } else if (term->kind == LawTermKind::Implies) {
            result.proofs.push_back(law_evidence(syntax.operands[i], env));
        } else {
            bad_law_type(syntax.operands[i].span, "too many arguments to law '" + symbol(target).name + "'");
        }
        term = &term->terms.back();
    }
    return result;
}

std::optional<LawTerm> Lowering::law_evidence_type(const LawProof& proof, const LawEnvironment& env) {
    if (proof.kind == LawProofKind::Assumption) {
        for (const auto& item : env.evidence)
            if (item.second.first == proof.index) return item.second.second;
    }
    if (proof.kind == LawProofKind::Apply) {
        const auto record = std::ranges::find(program_.laws, proof.symbol, &IrLawRecord::law_symbol);
        if (record != program_.laws.end()) return *law_goal(*record);
    }
    if ((proof.kind == LawProofKind::Sym || proof.kind == LawProofKind::Trans) && !proof.proofs.empty())
        return law_evidence_type(proof.proofs.front(), env);
    return std::nullopt;
}

LawProof Lowering::law_proof(const ProofSyntax& syntax, LawEnvironment& env, const LawTerm* goal) {
    using Kind = ProofSyntax::Kind;
    LawProof proof{LawProofKind::Block, syntax.keyword};
    if (proof.span.length == 0) proof.span = syntax.span;
    const auto child = [&](std::size_t i, const LawTerm* expected = nullptr) {
        return law_proof(syntax.children.at(i), env, expected);
    };
    const auto evidence = [&] {
        return child(0);
    };
    const auto bind_evidence = [&](const Token& name, LawTerm type) {
        const auto index = env.next++;
        if (!env.evidence.emplace(name.text, std::pair{index, std::move(type)}).second)
            bad_law_type(name.span, "duplicate hypothesis '" + name.text + "'");
        return index;
    };
    const auto branch = [&](const ProofSyntax& body, LawEnvironment local, const LawTerm* expected) {
        local.context.scopes.emplace_back();
        return law_proof(body, local, expected);
    };
    switch (syntax.kind) {
    case Kind::Reflexivity:
        proof.kind = LawProofKind::Refl;
        proof.span = syntax.span;
        break;
    case Kind::Decide:
        proof.kind = LawProofKind::Decide;
        break;
    case Kind::Term:
        return law_evidence(*syntax.expression, env);
    case Kind::Block: {
        auto local = env;
        local.context.scopes.emplace_back();
        const auto* remaining = goal;
        for (const auto& step : syntax.children) {
            if (step.kind == Kind::Last) {
                const auto expected = step.type ? std::optional{type(*step.type, local.context)} : std::nullopt;
                auto value = law_term(*step.expression, local, expected ? &*expected : nullptr);
                const auto& name = step.bindings.front();
                law_value_binding(local, name.text, name.span, value.type);
                local.values[local.context.scopes.back().at(name.text)] = value;
                LawProof definition{LawProofKind::Local, step.span};
                definition.terms.push_back(std::move(value));
                proof.proofs.push_back(std::move(definition));
                continue;
            }
            proof.proofs.push_back(law_proof(step, local, remaining));
            if (remaining && ((step.kind == Kind::Witness && remaining->kind == LawTermKind::Exists) ||
                (step.kind == Kind::Intro && (remaining->kind == LawTermKind::Implies ||
                                             remaining->kind == LawTermKind::Forall))))
                remaining = &remaining->terms.back();
        }
        break;
    }
    case Kind::Rewrite: {
        if (!syntax.proposition)
            throw Diagnostic(syntax.keyword, "rewrite without an explicit motive is deferred to a later phase")
                .with_code("E1421");
        proof.kind = syntax.reverse ? LawProofKind::RewriteReverse : LawProofKind::Rewrite;
        proof.proofs.push_back(evidence());
        const auto equation = law_evidence_type(proof.proofs.front(), env);
        if (!equation || equation->kind != LawTermKind::Equal)
            bad_law_type(syntax.span, "rewrite requires equality evidence");
        auto local = env;
        local.context.scopes.emplace_back();
        const auto id = add(SymbolKind::Parameter, "_", syntax.span, env.owner);
        symbol(id).type = equation->type;
        symbol(id).constant = true;
        local.context.scopes.back().emplace("_", id);
        local.values.emplace(id, LawTerm{LawTermKind::Hole, equation->type, syntax.span});
        proof.terms.push_back(law_proposition(*syntax.proposition, local));
        break;
    }
    case Kind::Have: {
        proof.kind = LawProofKind::Have;
        auto proposition = law_proposition(*syntax.proposition, env);
        proof.proofs.push_back(child(0, &proposition));
        proof.index = bind_evidence(syntax.bindings.front(), proposition);
        proof.terms.push_back(std::move(proposition));
        break;
    }
    case Kind::Witness:
        proof.kind = LawProofKind::Witness;
        proof.terms.push_back(law_term(*syntax.expression, env,
            goal && goal->kind == LawTermKind::Exists ? &goal->type : nullptr));
        break;
    case Kind::Intro:
        proof.kind = LawProofKind::Intro;
        if (goal && goal->kind == LawTermKind::Implies)
            proof.index = bind_evidence(syntax.bindings.front(), goal->terms.front());
        else if (goal && goal->kind == LawTermKind::Forall)
            proof.index = law_value_binding(env, syntax.bindings.front().text, syntax.bindings.front().span, goal->type);
        else
            bad_law_type(syntax.span, "intro requires an implication or universal goal");
        break;
    case Kind::Absurd:
    case Kind::Contradiction:
    case Kind::Inject: {
        proof.kind = syntax.kind == Kind::Absurd ? LawProofKind::Absurd :
            syntax.kind == Kind::Contradiction ? LawProofKind::Contradiction : LawProofKind::Inject;
        proof.proofs.push_back(evidence());
        if (syntax.kind == Kind::Inject) {
            const auto equation = law_evidence_type(proof.proofs.front(), env);
            std::size_t index = 0;
            for (const auto& name : syntax.bindings) {
                LawTerm extracted{LawTermKind::Equal, {}, name.span};
                if (equation && equation->kind == LawTermKind::Equal && equation->terms[0].terms.size() > index)
                    extracted.type = equation->terms[0].terms[index].type;
                proof.bindings.push_back(bind_evidence(name, std::move(extracted)));
                ++index;
            }
        }
        break;
    }
    case Kind::Split:
        proof.kind = LawProofKind::Split;
        for (std::size_t i = 0; i < syntax.children.size(); ++i) {
            const auto* part = goal && goal->kind == LawTermKind::All && i < goal->terms.size() ? &goal->terms[i] : nullptr;
            proof.proofs.push_back(branch(syntax.children[i], env, part));
        }
        break;
    case Kind::ChooseLeft:
    case Kind::ChooseRight: {
        const auto side = syntax.kind == Kind::ChooseRight ? 1 : 0;
        proof.kind = side ? LawProofKind::ChooseRight : LawProofKind::ChooseLeft;
        const auto* choice = goal && goal->kind == LawTermKind::Either ? &goal->terms[side] : nullptr;
        proof.proofs.push_back(branch(syntax.children.front(), env, choice));
        break;
    }
    case Kind::If: {
        proof.kind = LawProofKind::If;
        proof.terms.push_back(law_term(*syntax.expression, env, &bool_type));
        proof.index = syntax.bindings.empty() ? law_no_binding : env.next++;
        for (std::size_t i = 0; i < 2; ++i) {
            auto local = env;
            if (!syntax.bindings.empty()) {
                auto claim = law_truth(proof.terms.front(), syntax.bindings.front().span);
                claim.terms[1].index = i == 0;
                if (!local.evidence.try_emplace(syntax.bindings.front().text, proof.index, std::move(claim)).second)
                    bad_law_type(syntax.bindings.front().span, "duplicate hypothesis in proof branch");
            }
            proof.proofs.push_back(branch(syntax.children[i], std::move(local), goal));
        }
        break;
    }
    case Kind::Match: {
        proof.kind = LawProofKind::Match;
        const auto& scrutinee = *syntax.expression;
        const auto hypothesis = scrutinee.kind == ExprKind::Name ? env.evidence.find(scrutinee.text) : env.evidence.end();
        if (hypothesis == env.evidence.end())
            proof.terms.push_back(law_term(scrutinee, env));
        else {
            if (hypothesis->second.second.kind != LawTermKind::Either)
                bad_law_type(scrutinee.span, "proof match requires a bool, enum, or disjunction");
            proof.proofs.push_back(law_evidence(scrutinee, env));
        }
        for (const auto& arm : syntax.arms) {
            const auto& pattern = arm.pattern;
            LawProof entry{LawProofKind::Arm, pattern.span};
            auto local = env;
            local.context.scopes.emplace_back();
            if (hypothesis != env.evidence.end()) {
                if ((pattern.name != "Left" && pattern.name != "Right") || pattern.elements.size() != 1)
                    bad_law_type(pattern.span, "disjunction arms must be Left(witness) or Right(witness)");
                entry.index = pattern.name == "Right";
                const auto& element = pattern.elements.front();
                const auto index = local.next++;
                if (element.kind == PatternSyntax::Kind::Binding)
                    local.evidence.emplace(element.name, std::pair{index, hypothesis->second.second.terms[entry.index]});
                entry.bindings.push_back(index);
            } else if (pattern.kind == PatternSyntax::Kind::Wildcard) {
                entry.index = law_wildcard_arm;
            } else if (proof.terms.front().type == bool_type) {
                if (pattern.name != "true" && pattern.name != "false")
                    bad_law_type(pattern.span, "boolean proof pattern must be true or false");
                entry.index = pattern.name == "true";
            } else if (proof.terms.front().type.kind == TypeKind::Enum) {
                const auto& domain = proof.terms.front().type;
                const auto name = pattern.name.substr(pattern.name.find_last_of('.') + 1);
                entry.symbol = own_member(domain.symbol, name);
                if (entry.symbol == no_symbol || symbol(entry.symbol).kind != SymbolKind::Variant)
                    bad_law_type(pattern.name_span, "unknown proof pattern variant '" + pattern.name + "'");
                const auto types = enum_arguments(domain);
                const auto payloads = symbol(entry.symbol).payload_types;
                if (pattern.elements.size() != payloads.size())
                    bad_law_type(pattern.span, "proof pattern has the wrong number of payload bindings");
                for (std::size_t i = 0; i < pattern.elements.size(); ++i) {
                    const auto& binding = pattern.elements[i];
                    if (binding.kind == PatternSyntax::Kind::Wildcard) entry.bindings.push_back(law_no_binding);
                    else if (binding.kind == PatternSyntax::Kind::Binding)
                        entry.bindings.push_back(law_value_binding(local, binding.name, binding.name_span,
                                                                   substitute(payloads[i], types)));
                    else
                        bad_law_type(binding.span, "use a name or '_' to match a proof payload");
                }
            } else {
                bad_law_type(pattern.span, "proof match requires a bool, enum, or disjunction");
            }
            entry.proofs.push_back(law_proof(arm.body.front(), local, goal));
            proof.proofs.push_back(std::move(entry));
        }
        break;
    }
    case Kind::Calc:
        proof.kind = LawProofKind::Calc;
        proof.terms.push_back(law_term(*syntax.expression, env, goal && goal->kind == LawTermKind::Equal ? &goal->type : nullptr));
        for (const auto& step : syntax.children) {
            auto next = law_term(*step.expression, env, &proof.terms.back().type);
            LawTerm equality{LawTermKind::Equal, next.type, step.span, no_symbol, no_symbol, 0,
                             {proof.terms.back(), next}};
            proof.proofs.push_back(branch(step.children.front(), env, &equality));
            proof.terms.push_back(std::move(next));
        }
        break;
    case Kind::LoopInduct:
        unsupported_law(syntax.keyword, "loop induction", 4);
    case Kind::Last:
    case Kind::CalcStep:
        bad_law_type(syntax.span, "proof step must appear inside its enclosing block");
    }
    return proof;
}

const IrLawRecord& Lowering::resolve_law_statement(SymbolId id, const Span& use) {
    const auto existing = std::ranges::find_if(program_.laws, [&](const IrLawRecord& record) {
        return record.law_symbol == id && record.owner == symbol(id).owner;
    });
    if (existing != program_.laws.end()) return *existing;
    const auto source = law_declarations_.find(id);
    if (source == law_declarations_.end()) bad_law_type(use, "law has no typed statement");
    if (!resolving_law_statements_.insert(id).second)
        throw Diagnostic(use, "cyclic law statement reference to '" + qualified_law_name(program_, id) + "'")
            .with_code("E1414");
    const auto& syntax = *source->second->law;
    if (syntax.axiom) {
        const auto file = source_file(program_, id);
        if (file == no_symbol || symbol(file).library_path.empty())
            throw Diagnostic(*syntax.axiom, "'| axiom;' is reserved for trusted Std source").with_code("E1420");
        unsupported_law(*syntax.axiom, "trusted law axioms", 2);
    }
    if (syntax.todo)
        throw Diagnostic(*syntax.todo, "incomplete law proofs are deferred to a later phase").with_code("E1421");
    IrLawRecord record;
    record.law_symbol = id;
    record.owner = symbol(id).owner;
    record.status = syntax.proof ? LawStatus::Proven : LawStatus::Abstract;
    law_environments_[id] = std::make_shared<LawEnvironment>(law_statement(record, *source->second));
    resolving_law_statements_.erase(id);
    program_.laws.push_back(std::move(record));
    return program_.laws.back();
}

void Lowering::lower_laws(std::size_t proof_fuel) {
    if (law_declarations_.empty() && law_fulfillments_.empty() && program_.laws.empty()) return;
    // Static initialization can append resolved symbols directly to the IR.
    entries_.resize(program_.symbols.size());
    const auto first = program_.laws.size();
    for (const auto& [id, declaration] : law_declarations_)
        (void)resolve_law_statement(id, declaration->span);

    for (std::size_t i = first; i < program_.laws.size(); ++i) {
        auto& law = program_.laws[i];
        const auto& syntax = *law_declarations_.at(law.law_symbol)->law;
        if (syntax.proof)
            law.certificate = law_proof(*syntax.proof, *law_environments_.at(law.law_symbol), law_goal(law));
    }

    for (const auto& [owner, declaration] : law_fulfillments_) {
        const auto& syntax = *declaration->law;
        const auto& target = syntax.target;
        const auto base = symbol(owner).base;
        if (base == no_symbol)
            bad_law_type(syntax.keyword, "let.proof requires an inherited module law");
        auto base_name = std::string{};
        for (std::size_t i = 0; i + 1 < target.size(); ++i) {
            if (i) base_name += ".";
            base_name += target[i].text;
        }
        auto selected = base;
        if (!base_name.empty()) {
            selected = qualified(base_name, target.front().span, context(owner));
            if (symbol(selected).kind == SymbolKind::Import) selected = symbol(selected).type.symbol;
        }
        bool ancestor = false;
        for (auto parent = base; parent != no_symbol; parent = symbol(parent).base)
            ancestor |= parent == selected;
        if (!ancestor)
            bad_law_type(target.front().span, "proof target must belong to an ancestor module");
        const auto id = member(selected, target.back().text, true);
        if (id == no_symbol || symbol(id).kind != SymbolKind::Law)
            bad_law_type(target.back().span, "unknown inherited law '" + target.back().text + "'");
        if (std::ranges::any_of(program_.laws, [&](const IrLawRecord& law) {
                return law.law_symbol == id && law.owner == owner;
            }))
            bad_law_type(target.back().span, "duplicate fulfillment of law '" + target.back().text + "'");
        program_.name_uses.emplace_back(target.back().span, id);
        const auto inherited = std::ranges::find_if(program_.laws, [&](const IrLawRecord& candidate) {
            return candidate.law_symbol == id && candidate.owner == symbol(id).owner;
        });
        IrLawRecord record = *inherited;
        record.owner = owner;
        record.footprint.clear();
        record.status = LawStatus::Proven;
        auto env = inherited_law_environment(*inherited, owner);
        record.certificate = law_proof(*syntax.proof, env, law_goal(record));
        program_.laws.push_back(std::move(record));
    }

    LawCheckSites sites;
    for (SymbolId id = 0; id < entries_.size(); ++id) {
        const auto* declaration = entries_[id].declaration;
        if (!declaration) continue;
        if (declaration->base) sites.bases.emplace(id, declaration->base->span);
        if (declaration->override_member || declaration->kind == DeclKind::Law)
            sites.declarations.emplace(id, name_span(id));
    }
    check_program_laws(program_, proof_fuel, LawCheckOrigin::Source, sites);
}

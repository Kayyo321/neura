// Records (docs/design/Records030.md, Records).
//
// Included by lowering.cpp inside its anonymous namespace: these are members
// of Lowering. The parser built a record's fields and its expanded
// constructor. Here a record becomes a class: it is declared from a copy of
// its declaration that has three more members, the value members `equals`,
// `hash_code` and `to_string`, whose bodies are written as source would write
// them once the field types are known (fill_record_members). A hand-written
// class with the same members builds the same image apart from names. The
// record implies #Final and #Immutable, carries class.record, and its
// generated members carry `generated`.

namespace record_syntax {

inline Expr make(ExprKind kind, const Span& span, std::string text = {}, std::vector<Expr> operands = {}) {
    Expr result{kind, span, std::move(text), std::move(operands), {}, std::nullopt};
    return result;
}

inline Expr name(const Span& span, std::string text) { return make(ExprKind::Name, span, std::move(text)); }

inline Expr member(Expr receiver, const std::string& text) {
    const auto span = receiver.span;
    return make(ExprKind::Member, span, text, {std::move(receiver)});
}

// `$instance.field` or `$that.field`.
inline Expr field(const Span& span, const std::string& receiver, const std::string& field) {
    return member(name(span, receiver), field);
}

inline Expr call(Expr callee, std::vector<Expr> arguments = {}) {
    const auto span = callee.span;
    std::vector<Expr> operands{std::move(callee)};
    for (auto& argument : arguments) operands.push_back(std::move(argument));
    auto result = make(ExprKind::Call, span, {}, std::move(operands));
    result.open = span;
    return result;
}

inline Expr binary(std::string op, Expr left, Expr right) {
    const auto span = left.span;
    return make(ExprKind::Binary, span, std::move(op), {std::move(left), std::move(right)});
}

inline Expr conditional(Expr condition, Expr yes, Expr no) {
    const auto span = condition.span;
    return make(ExprKind::Conditional, span, {}, {std::move(condition), std::move(yes), std::move(no)});
}

inline TypeSyntax type(const Span& span, std::string text) { return TypeSyntax{span, std::move(text), {}, {}}; }

inline Expr cast(Expr operand, TypeSyntax target, bool test = false) {
    const auto span = operand.span;
    auto result = make(test ? ExprKind::TypeTest : ExprKind::Cast, span, {}, {std::move(operand)});
    result.type = std::move(target);
    return result;
}

inline Expr string(const Span& span, std::string_view text) {
    std::string literal = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') literal += '\\';
        literal += c;
    }
    return make(ExprKind::String, span, literal + "\"");
}

inline Expr integer(const Span& span, std::string digits) { return make(ExprKind::Integer, span, std::move(digits)); }

inline Statement expression_statement(StmtKind kind, Expr value) {
    Statement result{};
    result.kind = kind;
    result.span = value.span;
    result.expressions.push_back(std::move(value));
    return result;
}

inline Statement assignment(Expr place, Expr value) {
    Statement result{};
    result.kind = StmtKind::Assignment;
    result.assignment_operator = "=";
    result.span = place.span;
    result.expressions.push_back(std::move(place));
    result.expressions.push_back(std::move(value));
    return result;
}

inline Statement binding(const Span& span, std::string name, std::optional<TypeSyntax> type, Expr value,
                         bool constant) {
    Statement result{};
    result.kind = StmtKind::Binding;
    result.span = span;
    auto declaration = std::make_unique<Declaration>();
    declaration->kind = DeclKind::Binding;
    declaration->span = span;
    declaration->name = std::move(name);
    declaration->name_span = span;
    declaration->constant = constant;
    declaration->type = std::move(type);
    declaration->initializer = std::move(value);
    result.declaration = std::move(declaration);
    return result;
}

inline Declaration method(const Span& span, std::string name, std::string result_type) {
    Declaration result{};
    result.kind = DeclKind::Function;
    result.span = span;
    result.name = std::move(name);
    result.name_span = span;
    result.exported = true;
    result.override_member = true;
    result.has_body = true;
    result.result_written = true;
    result.generated = true;
    result.type = type(span, std::move(result_type));
    return result;
}

} // namespace record_syntax

bool Lowering::record_class(SymbolId cls) const {
    return cls < program_.symbols.size() && program_.symbols[cls].kind == SymbolKind::Class &&
           has_attribute(program_.symbols[cls], AttributeId::ClassRecord);
}

// The checks of a record's syntax (E0570) and its duplicate components,
// made before any member is declared.
void Lowering::check_record_syntax(const Declaration& decl) const {
    const auto& syntax = *decl.record;
    if (!syntax.problems.empty()) {
        const auto& [span, message] = syntax.problems.front();
        throw Diagnostic(span, "invalid record declaration").with_code("E0570")
            .label(message)
            .help("remove it, or declare a class instead of a record");
    }
    static constexpr std::string_view reserved[] = {"init", "deinit", "equals", "hash_code", "to_string", "instance"};
    std::map<std::string, const Declaration*> seen;
    for (const auto& member : decl.members) {
        if (member.kind != DeclKind::Binding) continue;
        const auto at = member.name_span.value_or(member.span);
        if (std::ranges::find(reserved, member.name) != std::end(reserved))
            throw Diagnostic(at, "invalid record declaration").with_code("E0570")
                .label("a record component cannot be named '" + member.name + "'")
                .help("rename the component");
        // The generated `equals` names the record's type, which a field of
        // the same name would hide.
        if (member.name == decl.name)
            throw Diagnostic(at, "invalid record declaration").with_code("E0570")
                .label("a record component cannot be named like its record")
                .help("rename the component");
        if (const auto [earlier, added] = seen.emplace(member.name, &member); !added)
            throw Diagnostic(at, "duplicate component '" + member.name + "'").with_code("E0303")
                .label("'" + member.name + "' is declared again here")
                .label(earlier->second->name_span.value_or(earlier->second->span),
                       "'" + member.name + "' is first declared here")
                .help("rename one of them");
    }
}

// After the record's class symbol and its written members are declared:
// declares the value members, whose bodies check_record writes, and the
// record's attributes.
void Lowering::declare_record(SymbolId id, const Declaration& decl) {
    const auto& at = decl.record->keyword;
    auto& members = record_members_[id];
    auto& equals = generated_declarations_.emplace_back(record_syntax::method(at, "equals", "bool"));
    Parameter other{};
    other.span = at;
    other.name = "other";
    other.type = record_syntax::type(at, "class");
    equals.parameters.push_back(std::move(other));
    members.push_back(&equals);
    members.push_back(&generated_declarations_.emplace_back(record_syntax::method(at, "hash_code", "uint32")));
    members.push_back(&generated_declarations_.emplace_back(record_syntax::method(at, "to_string", "string")));
    for (const auto* member : members) declare(*member, id, SymbolKind::Field);
    const auto init = own_member(id, "init");
    set_attribute(symbol(id), AttributeId::ClassRecord, attribute_symbol(init));
    const auto generated = attribute_list({attribute_text("record"), attribute_span(at)});
    set_attribute(symbol(init), AttributeId::Generated, generated);
    for (const auto* member : members) set_attribute(symbol(own_member(id, member->name)), AttributeId::Generated, generated);
}

void Lowering::lower_record_members(SymbolId id) {
    const auto found = record_members_.find(id);
    if (found == record_members_.end()) return;
    for (const auto* member : found->second) lower_declaration(own_member(id, member->name));
}

// The written tags of a record and the #Final and #Immutable it implies.
std::vector<TagSyntax> Lowering::record_tags(const Declaration& decl) const {
    auto tags = decl.tags;
    for (const auto* implied : {"Final", "Immutable"}) {
        if (std::ranges::any_of(tags, [&](const TagSyntax& tag) { return tag.name == implied; })) continue;
        TagSyntax tag{};
        tag.span = decl.record->keyword;
        tag.name_span = decl.record->keyword;
        tag.name = implied;
        tags.push_back(std::move(tag));
    }
    return tags;
}

// Why a field of type `type` cannot be held by a record, or nothing when it
// can (Records030.md, the record-eligible types). `inner` names the part of
// the type that fails, when it is not the type itself.
std::optional<std::string> Lowering::record_ineligible(const IrType& type, IrType& inner) const {
    switch (type.kind) {
    case TypeKind::Bool:
    case TypeKind::Integer:
    case TypeKind::Float:
    case TypeKind::String:
        return std::nullopt;
    case TypeKind::Tuple:
        for (const auto& element : type.arguments) {
            if (auto reason = record_ineligible(element, inner)) return reason;
        }
        return std::nullopt;
    case TypeKind::Enum: {
        if (!has_enum_payload(program_, type.symbol)) return std::nullopt;
        const auto arguments = enum_arguments(type);
        for (const auto& item : program_.symbols) {
            if (item.kind != SymbolKind::Variant || item.owner != type.symbol) continue;
            for (const auto& payload : item.payload_types) {
                const auto concrete = substitute(payload, arguments);
                if (concrete == type) continue;
                if (auto reason = record_ineligible(concrete, inner)) return reason;
            }
        }
        // The value members of payload enums are the step of the milestone
        // that follows the storage milestone's inline payload enums
        // (Records030.md, Order of work, step 2). Until then a record cannot
        // compare, hash or print such a field, so it cannot hold one.
        inner = type;
        return "a payload enum, which has no value members in this release yet";
    }
    case TypeKind::Object:
        if (record_class(type.symbol)) return std::nullopt;
        inner = type;
        if (type.name.starts_with('&')) return "a reference, which is not a value";
        if (type.symbol < program_.symbols.size() && program_.symbols[type.symbol].kind == SymbolKind::Interface)
            return "an interface, whose implementations need not be records";
        if (type.symbol == root_class_) return "the umbrella type 'class', which holds any class";
        if (is_function_value(type)) return "a function, which has no value equality";
        return "a class that is not a record, whose fields need not be immutable values";
    case TypeKind::Array:
    case TypeKind::List:
        inner = type;
        return "mutable";
    default:
        inner = type;
        if (is_function_value(type) || type.kind == TypeKind::Callable) return "a function, which has no value equality";
        return "not a value a record can hold";
    }
}

// Checks the field types of a record (E0571) and writes the bodies of its
// value members, which depend on them.
void Lowering::check_record(SymbolId id) {
    const auto found = record_members_.find(id);
    if (found == record_members_.end()) return;
    const auto& decl = *entries_[id].declaration;
    struct Field {
        std::string name;
        bool reference;
    };
    std::vector<Field> fields;
    for (const auto& member : decl.members) {
        if (member.kind != DeclKind::Binding) continue;
        const auto field = own_member(id, member.name);
        try {
            resolve(field);
        } catch (Diagnostic& error) {
            // `let x = record(count: limit);` is a record by the lookahead.
            if (error.code() == "E0421" && member.type && error.span.offset == member.type->span.offset)
                error.help("if 'record(...)' is meant as a call, write '(record)(...)'");
            throw;
        }
        const auto& type = symbol(field).type;
        IrType inner = type;
        if (const auto reason = record_ineligible(type, inner)) {
            const auto at = member.type ? member.type->span : member.name_span.value_or(member.span);
            auto error = Diagnostic(at, "field '" + member.name + "' has type '" + describe(type) +
                                            "', which a record cannot hold").with_code("E0571");
            if (inner != type) error.label("it holds '" + describe(inner) + "', " + *reason);
            else error.label(*reason == "mutable" ? "a collection is mutable" : "'" + describe(type) + "' is " + *reason);
            const bool immutable_class = inner.kind == TypeKind::Object && inner.symbol < program_.symbols.size() &&
                program_.symbols[inner.symbol].kind == SymbolKind::Class &&
                has_tag(program_.symbols[inner.symbol], TagId::Immutable);
            if (immutable_class)
                error.help("declare '" + describe(inner) + "' as a record, so that a record can hold it");
            else
                error.help("hold a record, a tuple or an enum of eligible types, or declare '" + decl.name +
                           "' as an #Immutable class and not as a record");
            throw error;
        }
        fields.push_back({member.name, type.kind == TypeKind::Object});
    }
    // The qualified name, as the runtime writes a class's name.
    std::string qualified = symbol(id).name;
    for (auto owner = symbol(id).owner; owner != no_symbol && symbol(owner).kind != SymbolKind::File;
         owner = symbol(owner).owner) {
        if (!is_static_scope(symbol(owner))) qualified = symbol(owner).name + "." + qualified;
    }
    namespace rs = record_syntax;
    const auto& at = decl.record->keyword;
    const auto self = [&](const std::string& field) { return rs::field(at, "$instance", field); };
    const auto nil_test = [&](Expr value) { return rs::binary("==", std::move(value), rs::make(ExprKind::Nil, at)); };
    for (auto* generated : found->second) {
        auto& member = *generated;
        member.body.clear();
        if (member.name == "equals") {
            // if (!(other is R)) { return false; }
            Statement guard{};
            guard.kind = StmtKind::If;
            guard.span = at;
            guard.expressions.push_back(rs::make(ExprKind::Unary, at, "!", {rs::cast(rs::name(at, "other"), rs::type(at, decl.name), true)}));
            guard.body.push_back(rs::expression_statement(StmtKind::Return, rs::make(ExprKind::Boolean, at, "false")));
            member.body.push_back(std::move(guard));
            // const $that = other as R;
            member.body.push_back(rs::binding(at, "$that", std::nullopt, rs::cast(rs::name(at, "other"), rs::type(at, decl.name)), true));
            std::optional<Expr> result;
            for (const auto& item : fields) {
                auto mine = self(item.name);
                auto theirs = rs::field(at, "$that", item.name);
                auto equal = rs::call(rs::member(mine, "equals"), {theirs});
                if (item.reference)
                    equal = rs::conditional(nil_test(std::move(mine)), nil_test(std::move(theirs)), std::move(equal));
                result = result ? rs::binary("&&", std::move(*result), std::move(equal)) : std::move(equal);
            }
            member.body.push_back(rs::expression_statement(StmtKind::Return, std::move(*result)));
        } else if (member.name == "hash_code") {
            // Start with 1; for each field, 31 times the hash plus the field's
            // hash, wrapping at 32 bits, computed in uint64 where nothing
            // overflows.
            member.body.push_back(rs::binding(at, "$hash", rs::type(at, "uint32"), rs::integer(at, "1"), false));
            for (const auto& item : fields) {
                auto hash = rs::call(rs::member(self(item.name), "hash_code"));
                auto wide = rs::cast(std::move(hash), rs::type(at, "uint64"));
                if (item.reference)
                    wide = rs::conditional(nil_test(self(item.name)), rs::cast(rs::integer(at, "0"), rs::type(at, "uint64")), std::move(wide));
                auto product = rs::binary("*", rs::cast(rs::name(at, "$hash"), rs::type(at, "uint64")), rs::integer(at, "31"));
                auto sum = rs::binary("+", std::move(product), std::move(wide));
                auto masked = rs::binary("&", std::move(sum), rs::integer(at, "4294967295"));
                member.body.push_back(rs::assignment(rs::name(at, "$hash"), rs::cast(std::move(masked), rs::type(at, "uint32"))));
            }
            member.body.push_back(rs::expression_statement(StmtKind::Return, rs::name(at, "$hash")));
        } else {
            // "R(a: text, b: text)"
            std::optional<Expr> text;
            const auto append = [&](Expr piece) {
                text = text ? rs::binary("+", std::move(*text), std::move(piece)) : std::move(piece);
            };
            bool first = true;
            for (const auto& item : fields) {
                append(rs::string(at, (first ? qualified + "(" : std::string(", ")) + item.name + ": "));
                first = false;
                auto piece = rs::call(rs::member(self(item.name), "to_string"));
                if (item.reference) piece = rs::conditional(nil_test(self(item.name)), rs::string(at, "nil"), std::move(piece));
                append(std::move(piece));
            }
            append(rs::string(at, ")"));
            member.body.push_back(rs::expression_statement(StmtKind::Return, std::move(*text)));
        }
    }
}

// `destroy` of a record (E0514): copies share their children, so the
// collector owns every record.
void Lowering::reject_record_destroy(const IrNode& object) const {
    if (object.type.kind != TypeKind::Object || !record_class(object.type.symbol)) return;
    throw Diagnostic(object.span, "a record cannot be destroyed").with_code("E0514")
        .label("'" + symbol(object.type.symbol).name + "' is a record")
        .note("a copy shares the values of its source, so the collector owns every record")
        .help("remove the 'destroy'; the record is collected when nothing refers to it");
}

// `let P = record ();` and `let P = record (x, y: T);` are calls by the
// record lookahead. When no `record` is in scope, the unknown name gains the
// help for the record that was probably meant.
void Lowering::record_call_help(const Expr& initializer, Diagnostic& error) {
    if (error.code() != "E0304" || initializer.kind != ExprKind::Call || initializer.operands.empty()) return;
    const auto& callee = initializer.operands.front();
    if (callee.kind != ExprKind::Name || callee.text != "record" || error.span.offset != callee.span.offset) return;
    if (initializer.operands.size() == 1)
        error.help("a record needs at least one component, written 'name: Type'");
    else if (std::ranges::any_of(initializer.labels, [](const std::string& label) { return !label.empty(); }))
        error.help("a record's components are not grouped: write 'a: T, b: T'");
}

// Accessors of exported instance fields (docs/lang/Types.md, Accessors).
//
// Included by lowering.cpp inside its anonymous namespace: these are members
// of Lowering. A field `export let data: T = ...;` followed by `| get: ...;`
// and `| set: ...;` keeps its storage. Each clause becomes a hidden method of
// the class, `$get_data() -> T` or `$set_data(value: T)`, linked from the
// field with the NeuraIR 1.10 attributes accessor.get and accessor.set and
// marked accessor.of. The methods are not in the class's name table, so no
// source name reaches them and a subclass cannot override them.
//
// Inside the declaring class (its methods, constructor, field initializers,
// and the accessor bodies), a bare field name or `instance.data` reads and
// writes the storage. Every other access, `other.data` in the class, any
// access from a subclass, and all access from outside, calls the accessor;
// inside the class a missing accessor falls back to the storage, and outside
// it is an error (E0510, E0515). A read-modify-write evaluates the receiver
// once, then calls the getter, computes, and calls the setter.

namespace accessor_text {

inline std::string kind_word(bool setter) { return setter ? "set" : "get"; }

inline std::string member_kind(const Declaration& decl) {
    if (decl.cast_target) return "a cast";
    switch (decl.kind) {
    case DeclKind::Function: return "a function";
    case DeclKind::Class: return "a class";
    case DeclKind::Module: return "a module";
    case DeclKind::Enum: return "an enum";
    case DeclKind::Interface: return "an interface";
    case DeclKind::Binding: break;
    }
    return "a field";
}

} // namespace accessor_text

// The checks that need only the syntax and the owner, made before the member
// is declared.
void Lowering::check_accessor_member(const Declaration& decl, SymbolId owner) const {
    const auto& first = decl.accessors.front();
    const auto name = decl.name_span.value_or(decl.span);
    const auto owner_kind = symbol(owner).kind;
    if (owner_kind == SymbolKind::Interface)
        throw Diagnostic(first.span, "an interface cannot declare accessors").with_code("E0519")
            .label("accessor clause in an interface")
            .label(name, "'" + decl.name + "' is an interface member")
            .note("interfaces cannot require accessors in this release")
            .help("declare the accessors on the field of a class that implements the interface, "
                  "or require a method instead");
    if (owner_kind != SymbolKind::Class)
        throw Diagnostic(first.span, "accessors are only allowed on class fields").with_code("E0519")
            .label("accessor clause outside a class")
            .label(name, "'" + decl.name + "' is a member of a " +
                             (owner_kind == SymbolKind::Module ? std::string("module") : std::string("file")))
            .help("move the field into a class, or declare a function that computes the value");
    if (decl.kind != DeclKind::Binding || decl.cast_target || !decl.overloads.empty())
        throw Diagnostic(first.span, "accessors require a field, but '" + decl.name + "' is " +
                                         accessor_text::member_kind(decl)).with_code("E0518")
            .label("accessor clause on " + accessor_text::member_kind(decl))
            .label(name, "'" + decl.name + "' is declared here")
            .help("remove the clause; only an exported instance field can have 'get' and 'set' accessors");
    if (decl.static_member)
        throw Diagnostic(first.span, "a static field cannot have accessors").with_code("E0517")
            .label("accessor clause on a static field")
            .label(name, "'" + decl.name + "' is declared 'static'")
            .help("accessors serve instance fields; expose a static field through a static function instead");
    if (!decl.exported) {
        auto error = Diagnostic(first.span, "accessors require an exported field").with_code("E0516")
            .label("accessor clause on a field that is not exported")
            .label(name, "'" + decl.name + "' is declared here without 'export'")
            .note("inside its class a field is always read and written directly, so accessors only apply "
                  "to access from outside the class");
        if (!decl.span.file.empty() && user_source(decl.span.file)) {
            if (const auto start = source_span(decl.span.file, decl.span.offset, 0))
                error.suggest("export the field", *start, "export ", Applicability::MaybeIncorrect);
        }
        throw error;
    }
    const AccessorClause* seen[2] = {nullptr, nullptr};
    for (const auto& clause : decl.accessors) {
        const auto word = accessor_text::kind_word(clause.setter);
        if (const auto* earlier = seen[clause.setter ? 1 : 0])
            throw Diagnostic(clause.keyword, "duplicate '" + word + "' accessor for '" + decl.name + "'")
                .with_code("E0520")
                .label("a second '" + word + "' accessor")
                .label(earlier->keyword, "the first '" + word + "' accessor is here")
                .help("remove one of them; a field has at most one 'get' and one 'set' accessor");
        seen[clause.setter ? 1 : 0] = &clause;
        if (clause.setter && decl.constant)
            throw Diagnostic(clause.keyword, "a constant field cannot have a setter").with_code("E0521")
                .label("setter of a constant field")
                .label(name, "'" + decl.name + "' is declared with 'const'")
                .help("declare the field with 'let', or remove the setter");
        if (clause.setter && decl.name == "value")
            throw Diagnostic(clause.keyword, "a field named 'value' cannot have a setter").with_code("E0522")
                .label("setter of the field 'value'")
                .label(name, "the field is named 'value'")
                .note("in a setter, 'value' names the incoming value, so the field's storage would be hidden")
                .help("rename the field");
    }
    if (decl.type && decl.type->reference)
        throw Diagnostic(first.span, "a primitive reference field cannot have accessors").with_code("E0523")
            .label("accessor clause on a reference field")
            .label(decl.type->span, "'" + decl.name + "' is a primitive reference")
            .help("store the value in a field of the primitive type itself");
}

void Lowering::declare_accessors(const Declaration& decl, SymbolId field) {
    const auto owner = symbol(field).owner;
    for (const auto& clause : decl.accessors) {
        const auto kind = clause.setter ? AccessorKind::Set : AccessorKind::Get;
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
        set_attribute(symbol(field), clause.setter ? AttributeId::AccessorSet : AttributeId::AccessorGet,
                      attribute_symbol(function));
        set_attribute(symbol(function), AttributeId::AccessorOf, accessor_of_payload(field, kind));
        accessor_clauses_.emplace(function, &clause);
    }
}

SymbolId Lowering::accessor(SymbolId field, AccessorKind kind) const {
    const auto* link = find_attribute(symbol(field),
                                      kind == AccessorKind::Get ? AttributeId::AccessorGet : AttributeId::AccessorSet);
    return link ? link->value.symbol : no_symbol;
}

// A function body that stores into `field` somewhere, directly or by a
// compound assignment.
static bool stores_field(const std::vector<IrNode>& region, SymbolId field) {
    return std::ranges::any_of(region, [&](const IrNode& item) {
        if (item.op == IrOp::Store && !item.operands.empty() && item.operands.front().op == IrOp::Field &&
            item.operands.front().symbol == field)
            return true;
        return stores_field(item.operands, field) || stores_field(item.body, field) ||
               stores_field(item.alternative, field);
    });
}

void Lowering::lower_accessors(SymbolId field) {
    if (symbol(field).kind != SymbolKind::Field) {
        const auto get = accessor(field, AccessorKind::Get);
        const auto clause = get != no_symbol ? get : accessor(field, AccessorKind::Set);
        throw Diagnostic(clause != no_symbol ? symbol(clause).span : symbol(field).span,
                         "accessors require a field, but '" + symbol(field).name + "' names a type")
            .with_code("E0518")
            .label("accessor clause on a type alias")
            .label(name_span(field), "'" + symbol(field).name + "' is an alias of a type")
            .help("remove the clause; only an exported instance field can have 'get' and 'set' accessors");
    }
    const auto field_type = symbol(field).type;
    const auto& name = symbol(field).name;
    for (const auto kind : {AccessorKind::Get, AccessorKind::Set}) {
        const auto function = accessor(field, kind);
        const auto found = function == no_symbol ? accessor_clauses_.end() : accessor_clauses_.find(function);
        if (found == accessor_clauses_.end()) continue;
        const auto& clause = *found->second;
        if (clause.setter) {
            symbol(function).type = void_type;
            symbol(symbol(function).parameters.front()).type = field_type;
        } else {
            symbol(function).type = field_type;
        }
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
            const auto* declaration = entries_[field].declaration;
            if (!clause.setter && declaration && declaration->type)
                result_spans_.emplace(function, declaration->type->span);
            instructions = body(clause.body, ctx);
            if (!clause.setter && !always_returns(instructions))
                throw Diagnostic(clause.keyword, "the getter of '" + name + "' may exit without returning " +
                                                     describe(field_type)).with_code("E0405")
                    .label("the getter must return " + describe(field_type) + " on every path")
                    .note("a path through the body ends without 'return' or a trap");
            if (clause.setter && !stores_field(instructions, field))
                report_warning(Diagnostic::warning(clause.keyword, "setter-never-stores",
                                                   "the setter of '" + name + "' never stores '" + name + "'")
                    .label("this setter never assigns '" + name + "'")
                    .note("a write from outside the class calls the setter, so it has no effect on '" + name + "'")
                    .help("assign the field in the setter, such as '" + name + " = value;', or remove the setter "
                          "to make '" + name + "' read-only outside its class"));
        }
        program_.bodies.push_back({function, std::move(instructions)});
    }
}

// The node for a field access `field` (an IrOp::Field with its receiver)
// written as `expr`: the storage itself, a getter call, or, when `expr` is
// the place an assignment writes, the field marked for store_place.
IrNode Lowering::route_field(IrNode field, const Expr& expr, Context& context) {
    if (field.op != IrOp::Field || field.symbol == no_symbol || field.operands.empty()) return field;
    const auto target = field.symbol;
    const auto get = accessor(target, AccessorKind::Get);
    const auto set = accessor(target, AccessorKind::Set);
    if (get == no_symbol && set == no_symbol) return field;
    const auto owner = symbol(target).owner;
    const bool inside_class = inside(lexical_owner(context), owner);
    const auto& receiver = expr.kind == ExprKind::Member ? expr.operands.front() : expr;
    const bool own_receiver = expr.kind == ExprKind::Name ||
        (receiver.kind == ExprKind::Name && receiver.text == "instance" &&
         find("instance", context, receiver.span) == no_symbol);
    if (inside_class && own_receiver) return field;
    const auto& name = symbol(target).name;
    const auto declaring = "'" + symbol(owner).name + "'";
    if (&expr == place_expr_) {
        if (set != no_symbol) {
            field.text = "$accessor";
            return field;
        }
        if (inside_class) return field;
        auto error = Diagnostic(expr.span, "'" + name + "' is read-only outside its class").with_code("E0510")
            .label("cannot assign to '" + name + "' here");
        if (user_source(symbol(get).span.file)) error.label(symbol(get).span, "'" + name + "' declares only a getter");
        throw error
            .note("'" + name + "' has no 'set' accessor, so only code inside " + declaring + " can change it")
            .help("add a setter to the field, such as '| set: " + name + " = value;', or change it through a "
                  "method of " + declaring);
    }
    if (get == no_symbol) {
        if (inside_class) return field;
        auto error = Diagnostic(expr.span, "'" + name + "' is write-only outside its class").with_code("E0515")
            .label("cannot read '" + name + "' here");
        if (user_source(symbol(set).span.file)) error.label(symbol(set).span, "'" + name + "' declares only a setter");
        throw error
            .note("'" + name + "' has no 'get' accessor, so only code inside " + declaring + " can read it")
            .help("add a getter to the field, such as '| get: " + name + ";'");
    }
    auto call = node(IrOp::VirtualCall, field.span, field.type);
    call.symbol = get;
    call.operands = std::move(field.operands);
    return call;
}

IrNode Lowering::place_expression(const Expr& expr, Context& context) {
    const auto* saved = std::exchange(place_expr_, &expr);
    try {
        auto result = expression(expr, context);
        place_expr_ = saved;
        return result;
    } catch (...) {
        place_expr_ = saved;
        throw;
    }
}

// The value currently in `place`: through the getter for a field that
// route_field marked, otherwise the place itself.
IrNode Lowering::read_place(IrNode place, Context& context) {
    if (place.op != IrOp::Field || place.text != "$accessor") return place;
    place.text.clear();
    const auto target = place.symbol;
    const auto get = accessor(target, AccessorKind::Get);
    if (get == no_symbol) {
        const auto owner = symbol(target).owner;
        if (inside(lexical_owner(context), owner)) return place;
        const auto& name = symbol(target).name;
        auto error = Diagnostic(place.span, "'" + name + "' is write-only outside its class").with_code("E0515")
            .label("this updates '" + name + "', which reads it first");
        const auto set = accessor(target, AccessorKind::Set);
        if (user_source(symbol(set).span.file)) error.label(symbol(set).span, "'" + name + "' declares only a setter");
        throw error
            .note("'" + name + "' has no 'get' accessor, so code outside '" + symbol(owner).name +
                  "' can assign it but not update it")
            .help("assign a new value with '=', or add a getter such as '| get: " + name + ";'");
    }
    auto call = node(IrOp::VirtualCall, place.span, place.type);
    call.symbol = get;
    call.operands = std::move(place.operands);
    return call;
}

// Stores `value` into `place`: a Store, or a setter call for a marked field.
IrNode Lowering::store_place(IrNode place, IrNode value, const Span& span) {
    if (place.op != IrOp::Field || place.text != "$accessor") {
        auto store = node(IrOp::Store, span);
        store.operands.push_back(std::move(place));
        store.operands.push_back(std::move(value));
        return store;
    }
    auto call = node(IrOp::VirtualCall, span, void_type);
    call.symbol = accessor(place.symbol, AccessorKind::Set);
    call.operands = std::move(place.operands);
    call.operands.push_back(std::move(value));
    auto result = node(IrOp::Discard, span);
    result.operands.push_back(std::move(call));
    return result;
}

void Lowering::accessor_reference(const IrNode& place, const IrType& type) const {
    const auto& of = find_attribute(symbol(place.symbol), AttributeId::AccessorOf)->value;
    const auto field = of.items.front().symbol;
    const auto& name = symbol(field).name;
    throw Diagnostic(place.span, describe(type) + " cannot refer to '" + name + "', which has accessors")
        .with_code("E0524")
        .label("'" + name + "' is reached through its getter here, which yields a value and not storage")
        .note("outside its class, a field with accessors is read and written only through 'get' and 'set', "
              "so a reference cannot alias its storage")
        .help("copy the value into a local, pass that local, and assign the result back to '" + name + "'");
}

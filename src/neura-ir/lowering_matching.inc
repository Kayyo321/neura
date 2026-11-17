// Guards, nested and tuple patterns in `match`, and `match.is`
// (docs/design/Matching030.md). Included by lowering.cpp after the class
// definition; every function here is a member of Lowering.
//
// A match whose arms are all in the form of 0.2 keeps lower_match. Any
// other match lowers in two phases inside one ScopedValue or Block: the
// head saves the subject in `$match`, declares each binding of each arm as
// a deferred `last` local, and sets the hidden local `$arm` to the number of
// arms. Phase one visits the arms in written order; each one runs while
// `$arm` is unset, projects, tests and binds in written order as nested
// Branch nodes, evaluates its guard, and stores its index in `$arm`. Phase
// two is the chain of 0.2 on `$arm`.

namespace {

const IrType arm_index_type{TypeKind::Integer, "uint32", no_symbol, {}};

IrNode arm_index(std::size_t value, const Span& span) {
    auto result = node(IrOp::Integer, span, arm_index_type);
    result.text = std::to_string(value);
    return result;
}

IrNode load_local(SymbolId local, const IrType& type, const Span& span) {
    auto result = node(IrOp::Load, span, type);
    result.symbol = local;
    return result;
}

IrNode equals(IrNode left, IrNode right, const Span& span) {
    auto result = node(IrOp::Binary, span, bool_type);
    result.text = "==";
    result.operands.push_back(std::move(left));
    result.operands.push_back(std::move(right));
    return result;
}

IrNode store_local(SymbolId local, const IrType& type, IrNode value, const Span& span) {
    auto result = node(IrOp::Store, span);
    result.operands.push_back(load_local(local, type, span));
    result.operands.push_back(std::move(value));
    return result;
}

// "a Shape", "an int64".
std::string article(const std::string& text) {
    const bool vowel = !text.empty() && std::string_view("aeioAEIO").find(text.front()) != std::string_view::npos;
    return (vowel ? "an " : "a ") + text;
}

} // namespace

// The steps of one arm's phase one, in written order: a statement, or a
// test that the rest of the arm runs under.
struct Lowering::PatternStep {
    std::optional<IrNode> test;
    std::optional<IrNode> statement;
};

// A pattern as coverage sees it: a constructor with its arguments, or a
// wildcard. Variants are an enum's constructors, a tuple has one
// (`no_symbol`), and every other type has none.
struct Lowering::CoverPattern {
    bool wildcard = true;
    SymbolId constructor = no_symbol;
    std::vector<CoverPattern> arguments{};
};

const std::vector<SymbolId>& Lowering::enum_variants(SymbolId enumeration) {
    auto found = variants_.find(enumeration);
    if (found != variants_.end()) return found->second;
    std::vector<SymbolId> result;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind == SymbolKind::Variant && item.owner == enumeration) result.push_back(id);
    }
    return variants_.emplace(enumeration, std::move(result)).first->second;
}

std::vector<IrType> Lowering::constructor_arguments(const IrType& type, SymbolId constructor) const {
    if (type.kind == TypeKind::Tuple) return type.arguments;
    auto fields = symbol(constructor).payload_types;
    const auto replacements = enum_arguments(type);
    for (auto& field : fields) field = substitute(std::move(field), replacements);
    return fields;
}

// Whether `row` is useful after the rows of `matrix`, all of the column
// types `types`: some value matches `row` and no row of the matrix. The
// witness is such a value, as a pattern, when the result is true.
bool Lowering::useful(const std::vector<std::vector<CoverPattern>>& matrix, const std::vector<CoverPattern>& row,
                      const std::vector<IrType>& types, std::vector<CoverPattern>& witness) {
    if (row.empty()) {
        witness.clear();
        return matrix.empty();
    }
    const auto& type = types.front();
    const auto specialize = [&](const std::vector<CoverPattern>& line, SymbolId constructor, std::size_t arity,
                                std::vector<CoverPattern>& out) {
        const auto& head = line.front();
        if (!head.wildcard && head.constructor != constructor) return false;
        out.clear();
        if (head.wildcard) out.resize(arity);
        else out = head.arguments;
        out.insert(out.end(), line.begin() + 1, line.end());
        return true;
    };
    const auto try_constructor = [&](SymbolId constructor) {
        const auto arguments = constructor_arguments(type, constructor);
        std::vector<IrType> inner = arguments;
        inner.insert(inner.end(), types.begin() + 1, types.end());
        std::vector<std::vector<CoverPattern>> specialized;
        std::vector<CoverPattern> line;
        for (const auto& item : matrix)
            if (specialize(item, constructor, arguments.size(), line)) specialized.push_back(line);
        std::vector<CoverPattern> query;
        if (!specialize(row, constructor, arguments.size(), query)) return false;
        std::vector<CoverPattern> found;
        if (!useful(specialized, query, inner, found)) return false;
        CoverPattern head{false, constructor, {}};
        head.arguments.assign(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(arguments.size()));
        witness.clear();
        witness.push_back(std::move(head));
        witness.insert(witness.end(), found.begin() + static_cast<std::ptrdiff_t>(arguments.size()), found.end());
        return true;
    };
    if (!row.front().wildcard) return try_constructor(row.front().constructor);
    std::vector<SymbolId> constructors;
    if (type.kind == TypeKind::Tuple) constructors.push_back(no_symbol);
    else if (type.kind == TypeKind::Enum) constructors = enum_variants(type.symbol);
    std::set<SymbolId> present;
    for (const auto& item : matrix)
        if (!item.front().wildcard) present.insert(item.front().constructor);
    if (!constructors.empty() && present.size() == constructors.size()) {
        for (const auto constructor : constructors)
            if (try_constructor(constructor)) return true;
        return false;
    }
    std::vector<std::vector<CoverPattern>> rest;
    for (const auto& item : matrix)
        if (item.front().wildcard) rest.emplace_back(item.begin() + 1, item.end());
    std::vector<IrType> rest_types(types.begin() + 1, types.end());
    std::vector<CoverPattern> found;
    if (!useful(rest, std::vector<CoverPattern>(row.begin() + 1, row.end()), rest_types, found)) return false;
    // A missing constructor, or `_` when the column names none.
    CoverPattern head{};
    for (const auto constructor : present.empty() ? std::vector<SymbolId>{} : constructors) {
        if (present.contains(constructor)) continue;
        head = CoverPattern{false, constructor, {}};
        head.arguments.resize(constructor_arguments(type, constructor).size());
        break;
    }
    witness.clear();
    witness.push_back(std::move(head));
    witness.insert(witness.end(), found.begin(), found.end());
    return true;
}

std::string Lowering::cover_text(const CoverPattern& pattern, const IrType& type) const {
    if (pattern.wildcard) return "_";
    const auto arguments = constructor_arguments(type, pattern.constructor);
    std::string result;
    if (type.kind == TypeKind::Tuple) {
        result = "[";
        for (std::size_t i = 0; i < arguments.size(); ++i)
            result += (i ? ", " : "") + cover_text(pattern.arguments[i], arguments[i]);
        return result + "]";
    }
    result = symbol(type.symbol).name + "." + symbol(pattern.constructor).name;
    if (arguments.empty()) return result;
    const auto* declaration = pattern.constructor < entries_.size() ? entries_[pattern.constructor].declaration : nullptr;
    const bool named = declaration && declaration->named_payload;
    result += named ? " { " : "(";
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        result += i ? ", " : "";
        if (named) result += declaration->parameters[i].name + ": ";
        result += cover_text(pattern.arguments[i], arguments[i]);
    }
    return result + (named ? " }" : ")");
}

// The fix for E0410 in a general match or a `match.is`: a final `_` arm,
// inserted before the closing brace when it is on a line of its own after a
// complete arm, as lower_match's fix inserts the missing variants.
void Lowering::wildcard_fix(Diagnostic& error, const Statement& stmt, bool produces_value) const {
    const auto close = source_span(stmt.span.file, stmt.span.offset + stmt.span.length - 1, 1);
    const auto file = SourceMap::global().find(stmt.span.file);
    if (!close || !file || !user_source(stmt.span.file) || source_text(*close) != "}" || stmt.arms.empty()) return;
    const auto& text = file->text();
    const auto line_start = file->line_start(close->line - 1);
    const auto indent = text.substr(line_start, close->offset - line_start);
    if (indent.find_first_not_of(" \t") != std::string::npos) return;
    auto before = line_start;
    while (before > 0 && std::string_view(" \t\r\n").find(text[before - 1]) != std::string_view::npos) --before;
    if (before == 0 || (text[before - 1] != ',' && text[before - 1] != '{')) return;
    const auto arm_indent = std::string(stmt.arms.front().span.column - 1, ' ');
    error.suggest("insert the '_' arm before the closing brace", *source_span(stmt.span.file, line_start, 0),
                  arm_indent + (produces_value ? "_ => <value>,\n" : "_ => {},\n"), Applicability::HasPlaceholders);
}

// The variant a pattern names. A whole pattern names a variant qualified
// by its enum; an unqualified name that names nothing is E0412, with the
// qualified spelling when the enum has such a variant.
SymbolId Lowering::arm_variant(const std::string& name, const Span& span, const Context& context, const IrType& type) {
    if (name.find('.') != std::string::npos) return qualified(name, span, context);
    try {
        return qualified(name, span, context);
    } catch (const Diagnostic&) {
        auto error = Diagnostic(span, "match arm must name a variant of " + describe(type)).with_code("E0412")
            .label("'" + name + "' names no variant here");
        if (type.kind == TypeKind::Enum) {
            const auto& variants = enum_variants(type.symbol);
            if (std::ranges::any_of(variants, [&](SymbolId id) { return symbol(id).name == name; }))
                error.help("qualify the variant with its enum: '" + symbol(type.symbol).name + "." + name + "'");
        }
        throw error;
    }
}

// Lowers one pattern of an arm against `value`, an expression that reads
// the matched value and may be repeated. Tests and stores are appended to
// `steps` in written order, bindings declared in `declarations` and in the
// innermost scope, and the pattern as coverage sees it is returned.
Lowering::CoverPattern Lowering::lower_pattern(const PatternSyntax& pattern, const IrNode& value,
                                               std::vector<PatternStep>& steps,
                                               std::vector<IrNode>& declarations, Context& context,
                                               SymbolId owner) {
    using Kind = PatternSyntax::Kind;
    const auto& type = value.type;
    switch (pattern.kind) {
    case Kind::Wildcard:
        return {};
    case Kind::Binding: {
        auto& names = context.scopes.back();
        if (names.contains(pattern.name))
            throw duplicate_local(pattern.name_span, "duplicate match binding '" + pattern.name + "'",
                                  names.find(pattern.name)->second);
        if (type.kind == TypeKind::Void)
            throw Diagnostic(pattern.span, "a pattern cannot bind a value of type void").with_code("E0416");
        const auto local = add(SymbolKind::Local, pattern.name, pattern.name_span, owner);
        names.emplace(pattern.name, local);
        symbol(local).type = type;
        symbol(local).constant = true;
        set_attribute(symbol(local), AttributeId::BindingLast, attribute_bool(true));
        auto declare = node(IrOp::Initialize, pattern.span);
        declare.symbol = local;
        declarations.push_back(std::move(declare));
        steps.push_back({std::nullopt, store_local(local, type, value, pattern.span)});
        return {};
    }
    case Kind::Tuple: {
        if (type.kind != TypeKind::Tuple)
            throw Diagnostic(pattern.span, "a tuple pattern cannot match a value of type " + describe(type))
                .with_code("E0412").label("the value has type " + describe(type));
        if (pattern.elements.size() != type.arguments.size())
            throw Diagnostic(pattern.span, "a tuple pattern must have one sub-pattern per element")
                .with_code("E0426").label("expected " + plural(type.arguments.size(), "element") + ", found " +
                                          std::to_string(pattern.elements.size()));
        CoverPattern result{false, no_symbol, {}};
        for (std::size_t i = 0; i < pattern.elements.size(); ++i) {
            auto project = node(IrOp::Project, pattern.elements[i].span, type.arguments[i]);
            project.text = std::to_string(i);
            project.operands.push_back(value);
            result.arguments.push_back(
                element_pattern(pattern.elements[i], std::move(project), steps, declarations, context, owner));
        }
        return result;
    }
    case Kind::Variant:
        break;
    case Kind::Type:
    case Kind::Nil:
        throw Diagnostic(pattern.span, "a type pattern is not supported inside a match pattern").with_code("E0231");
    }
    if (type.kind != TypeKind::Enum)
        throw Diagnostic(pattern.span, "a variant pattern cannot match a value of type " + describe(type))
            .with_code("E0412").label("the value has type " + describe(type));
    const auto variant = arm_variant(pattern.name, pattern.span, context, type);
    if (symbol(variant).kind != SymbolKind::Variant || symbol(variant).owner != type.symbol)
        throw Diagnostic(pattern.span, "match arm must name a variant of " + describe(type)).with_code("E0412");
    if (const auto separator = pattern.name.rfind('.'); separator != std::string::npos) {
        const auto qualifier = qualified(pattern.name.substr(0, separator), pattern.span, context);
        if (symbol(qualifier).kind == SymbolKind::Alias && symbol(qualifier).type != type)
            throw Diagnostic(pattern.span, "match arm must use an overlay of " + describe(type)).with_code("E0412");
    }
    const auto fields = constructor_arguments(type, variant);
    const auto* declaration = entries_[variant].declaration;
    if (pattern.elements.size() != fields.size() || (pattern.payload && pattern.named != (declaration && declaration->named_payload)))
        throw Diagnostic(pattern.span, "match pattern must bind each payload field, using _ to ignore a field")
            .with_code("E0412");
    // The test, on a payload enum's tag or a fieldless enum's value.
    IrNode test;
    if (has_enum_payload(program_, type.symbol)) {
        auto tag = node(IrOp::EnumTag, pattern.span, arm_index_type);
        tag.operands.push_back(value);
        auto ordinal = node(IrOp::Integer, pattern.span, arm_index_type);
        ordinal.text = std::to_string(enum_ordinal(program_, variant));
        test = equals(std::move(tag), std::move(ordinal), pattern.span);
    } else {
        auto constant = node(IrOp::Reference, pattern.span, type);
        constant.symbol = variant;
        test = equals(value, std::move(constant), pattern.span);
    }
    steps.push_back({std::move(test), std::nullopt});
    CoverPattern result{false, variant, std::vector<CoverPattern>(fields.size())};
    std::set<std::string> seen;
    for (std::size_t i = 0; i < pattern.elements.size(); ++i) {
        auto field = i;
        if (pattern.named) {
            const auto& name = pattern.fields[i];
            const auto found = std::ranges::find(declaration->parameters, name, &Parameter::name);
            if (found == declaration->parameters.end() || !seen.insert(name).second)
                throw Diagnostic(pattern.field_spans[i], "unknown or repeated enum payload field '" + name + "'")
                    .with_code("E0427");
            field = static_cast<std::size_t>(found - declaration->parameters.begin());
        }
        auto project = node(IrOp::EnumProject, pattern.elements[i].span, fields[field]);
        project.symbol = variant;
        project.text = std::to_string(field);
        project.operands.push_back(value);
        result.arguments[field] =
            element_pattern(pattern.elements[i], std::move(project), steps, declarations, context, owner);
    }
    return result;
}

// A payload or element: bound directly, or saved in a hidden constant and
// matched when it is itself a variant or tuple pattern. `_` projects nothing.
Lowering::CoverPattern Lowering::element_pattern(const PatternSyntax& pattern, IrNode projection,
                                                 std::vector<PatternStep>& steps, std::vector<IrNode>& declarations,
                                                 Context& context, SymbolId owner) {
    using Kind = PatternSyntax::Kind;
    if (pattern.kind == Kind::Wildcard) return {};
    if (pattern.kind == Kind::Binding)
        return lower_pattern(pattern, projection, steps, declarations, context, owner);
    const auto saved = add(SymbolKind::Local, "$pattern", pattern.span, owner);
    symbol(saved).type = projection.type;
    symbol(saved).constant = true;
    const auto type = projection.type;
    auto save = node(IrOp::Initialize, pattern.span);
    save.symbol = saved;
    save.operands.push_back(std::move(projection));
    steps.push_back({std::nullopt, std::move(save)});
    return lower_pattern(pattern, load_local(saved, type, pattern.span), steps, declarations, context, owner);
}

// `match.is` (docs/design/Matching030.md). The subject is a class or
// interface reference, `class`, `any`, or a scalar or string.
void Lowering::is_subject(const IrNode& subject) const {
    const auto& type = subject.type;
    if (class_reference(type) || type.kind == TypeKind::Any || type.kind == TypeKind::Bool || type.kind == TypeKind::Integer ||
        type.kind == TypeKind::Float || type.kind == TypeKind::String)
        return;
    auto error = Diagnostic(subject.span, "match.is cannot test a value of type " + describe(type)).with_code("E0411")
        .label("the subject has type " + describe(type));
    if (type.kind == TypeKind::TypeParameter)
        error.help("convert it with 'as any' to test the type of its value");
    else if (type.kind == TypeKind::Enum || type.kind == TypeKind::Tuple)
        error.help("use 'match' for " + std::string(type.kind == TypeKind::Enum ? "an enum" : "a tuple"));
    else
        error.note("a match.is subject is a class or interface reference, 'class', 'any', a scalar or a string");
    throw error;
}

bool Lowering::scalar_subject(const IrType& type) const {
    return type.kind == TypeKind::Bool || type.kind == TypeKind::Integer || type.kind == TypeKind::Float ||
           type.kind == TypeKind::String;
}

// One `match.is` pattern: the tests and the binding of its arm, and what it
// covers. A pattern that cannot match the subject's type is E0490.
Lowering::IsCover Lowering::is_pattern(const PatternSyntax& pattern, const IrNode& value,
                                       std::vector<PatternStep>& steps, std::vector<IrNode>& declarations,
                                       Context& context, SymbolId owner) {
    using Kind = PatternSyntax::Kind;
    const auto& subject = value.type;
    const auto never = [&](const std::string& what) {
        return Diagnostic(pattern.span, "a value of type " + describe(subject) + " is never " + what)
            .with_code("E0490").label("the subject has type " + describe(subject));
    };
    if (pattern.kind == Kind::Wildcard) return {IsCover::Kind::Wildcard, {}};
    if (subject.kind == TypeKind::Any) return any_pattern(pattern, value, steps, declarations, context, owner);
    if (pattern.kind == Kind::Nil) {
        if (!class_reference(subject)) throw never("nil");
        steps.push_back({equals(value, node(IrOp::Nil, pattern.span, subject), pattern.span), std::nullopt});
        return {IsCover::Kind::Nil, {}};
    }
    auto tested = type(*pattern.type, context);
    if (is_primitive_reference(tested))
        throw Diagnostic(pattern.type->span, "a primitive reference is not a match.is pattern").with_code("E0490");
    const bool reference = class_reference(tested) && tested.kind == TypeKind::Object;
    if (scalar_subject(subject)) {
        if (tested != subject) throw never(article(describe(tested)));
    } else if (!reference) {
        throw never(article(describe(tested)));
    } else {
        const auto subject_class = receiver_type(subject);
        const bool related = assignable(tested, subject_class) || assignable(subject_class, tested) ||
                             symbol(tested.symbol).kind == SymbolKind::Interface ||
                             symbol(subject_class.symbol).kind == SymbolKind::Interface;
        if (!related) throw never(article(describe(tested)));
        auto test = node(IrOp::ClassIs, pattern.span, bool_type);
        test.type_arguments.push_back(tested);
        test.operands.push_back(value);
        steps.push_back({std::move(test), std::nullopt});
    }
    if (pattern.kind == Kind::Binding) {
        // A binding that forgets a scope-bound subject is E0605.
        reject_erasure(subject, tested, pattern.span);
        auto& names = context.scopes.back();
        const auto local = add(SymbolKind::Local, pattern.name, pattern.name_span, owner);
        names.emplace(pattern.name, local);
        symbol(local).type = tested;
        symbol(local).constant = true;
        set_attribute(symbol(local), AttributeId::BindingLast, attribute_bool(true));
        auto declare = node(IrOp::Initialize, pattern.span);
        declare.symbol = local;
        declarations.push_back(std::move(declare));
        IrNode bound = value;
        if (!scalar_subject(subject)) {
            bound = node(IrOp::ClassCast, pattern.span, tested);
            bound.operands.push_back(value);
        }
        steps.push_back({std::nullopt, store_local(local, tested, std::move(bound), pattern.span)});
    }
    return {scalar_subject(subject) ? IsCover::Kind::Wildcard : IsCover::Kind::Type, tested};
}

// A `match.is` pattern on an `any` subject, whose snapshot is a constant
// local: the tag test, and for a class pattern the ClassIs of the
// unpacked reference; a binding is the unpacked value, or the ClassCast of
// the unpacked reference.
Lowering::IsCover Lowering::any_pattern(const PatternSyntax& pattern, const IrNode& value,
                                        std::vector<PatternStep>& steps, std::vector<IrNode>& declarations,
                                        Context& context, SymbolId owner) {
    using Kind = PatternSyntax::Kind;
    const auto local = value.symbol;
    if (pattern.kind == Kind::Nil) {
        steps.push_back({any_tag_is(local, any_reference_tag, pattern.span), std::nullopt});
        const auto root = symbol(root_class()).type;
        steps.push_back({equals(any_unpack(local, root, pattern.span), node(IrOp::Nil, pattern.span, root), pattern.span),
                         std::nullopt});
        return {IsCover::Kind::Nil, {}};
    }
    auto tested = type(*pattern.type, context);
    const auto tag = any_tag(tested);
    if (!tag)
        throw Diagnostic(pattern.span, "a value of type any is never " + article(shown(tested))).with_code("E0490")
            .label("an any never holds a value of type " + shown(tested));
    steps.push_back({any_tag_is(local, *tag, pattern.span), std::nullopt});
    const bool reference = tested.kind == TypeKind::Object;
    if (reference) {
        auto test = node(IrOp::ClassIs, pattern.span, bool_type);
        test.type_arguments.push_back(tested);
        test.operands.push_back(any_unpack(local, tested, pattern.span));
        steps.push_back({std::move(test), std::nullopt});
    }
    if (pattern.kind == Kind::Binding) {
        const auto bound_local = add(SymbolKind::Local, pattern.name, pattern.name_span, owner);
        context.scopes.back().emplace(pattern.name, bound_local);
        symbol(bound_local).type = tested;
        symbol(bound_local).constant = true;
        set_attribute(symbol(bound_local), AttributeId::BindingLast, attribute_bool(true));
        auto declare = node(IrOp::Initialize, pattern.span);
        declare.symbol = bound_local;
        declarations.push_back(std::move(declare));
        auto bound = any_unpack(local, tested, pattern.span);
        if (reference) {
            auto cast = node(IrOp::ClassCast, pattern.span, tested);
            cast.operands.push_back(std::move(bound));
            bound = std::move(cast);
        }
        steps.push_back({std::nullopt, store_local(bound_local, tested, std::move(bound), pattern.span)});
    }
    return {IsCover::Kind::Type, tested};
}

// Whether an unguarded arm `earlier` matches every value that `later` matches.
bool Lowering::is_covered(const IsCover& earlier, const IsCover& later, const IrType&) const {
    switch (earlier.kind) {
    case IsCover::Kind::Wildcard:
        return true;
    case IsCover::Kind::Nil:
        return later.kind == IsCover::Kind::Nil;
    case IsCover::Kind::Type:
        return later.kind == IsCover::Kind::Type && assignable(later.type, earlier.type);
    }
    return false;
}

// A class, interface, `class` or `any` subject needs a final unguarded `_`;
// a scalar or string one an unguarded arm of its type, or `_`.
void Lowering::is_exhaustive(const Statement& stmt, const IrType& subject, const std::vector<IsCover>& covered,
                             bool produces_value) const {
    if (std::ranges::any_of(covered, [](const IsCover& item) { return item.kind == IsCover::Kind::Wildcard; }))
        return;
    auto error = Diagnostic(Span{stmt.span.file, stmt.span.line, stmt.span.column, stmt.span.offset, 5},
                            "non-exhaustive match.is on a value of type " + describe(subject))
        .with_code("E0410").label("a value may match no arm");
    if (scalar_subject(subject))
        error.help("add an arm for " + describe(subject) + ", or a final '_' arm");
    else
        error.note("type patterns never cover a class, an interface, 'class' or 'any', whatever its attributes")
            .help("add a final '_' arm");
    wildcard_fix(error, stmt, produces_value);
    throw error;
}

IrNode Lowering::general_match(const Statement& stmt, Context& context, bool produces_value, const IrType* expected,
                               bool call_argument, std::optional<IrNode> lowered_subject) {
    // A collection literal matched by tuple patterns is a tuple, as it is
    // where a tuple is expected.
    const auto& written = stmt.expressions.front();
    const bool tuple_literal = written.kind == ExprKind::Collection &&
        std::ranges::any_of(stmt.arms, [](const MatchArm& arm) {
            return arm.pattern && arm.pattern->kind == PatternSyntax::Kind::Tuple;
        });
    auto subject = lowered_subject ? std::move(*lowered_subject)
                 : tuple_literal ? tuple(written, context, nullptr) : value(written, context);
    const auto subject_type = subject.type;
    if (stmt.match_is) is_subject(subject);
    else if (subject_type.kind != TypeKind::Enum && subject_type.kind != TypeKind::Tuple)
        throw Diagnostic(subject.span, "match requires an enum or tuple value").with_code("E0411");
    if (stmt.arms.empty())
        throw Diagnostic(stmt.span, stmt.match_is ? "match.is requires at least one arm" : "match requires at least one arm")
            .with_code("E0411");
    auto owner = context.owner;
    if (symbol(owner).kind == SymbolKind::Global || symbol(owner).kind == SymbolKind::Field)
        owner = symbol(owner).owner;
    const auto temporary = add(SymbolKind::Local, "$match", subject.span, owner);
    symbol(temporary).type = subject_type;
    symbol(temporary).constant = true;
    const auto selected = add(SymbolKind::Local, "$arm", stmt.span, owner);
    symbol(selected).type = arm_index_type;

    auto result = node(produces_value ? IrOp::ScopedValue : IrOp::Block, stmt.span);
    auto save = node(IrOp::Initialize, subject.span);
    save.symbol = temporary;
    save.operands.push_back(std::move(subject));
    result.body.push_back(std::move(save));
    auto unset = node(IrOp::Initialize, stmt.span);
    unset.symbol = selected;
    unset.operands.push_back(arm_index(stmt.arms.size(), stmt.span));
    result.body.push_back(std::move(unset));

    // Phase one, with coverage: an arm that the earlier unguarded arms
    // cover is unreachable, and the unguarded arms must cover every value.
    std::vector<IrNode> declarations;
    std::vector<IrNode> tests;
    std::vector<Names> arm_scopes;
    std::vector<std::vector<CoverPattern>> covered;
    std::vector<IsCover> covered_types;
    std::vector<std::size_t> covering_arms;
    const std::vector<IrType> column{subject_type};
    for (std::size_t index = 0; index < stmt.arms.size(); ++index) {
        const auto& arm = stmt.arms[index];
        context.scopes.emplace_back();
        std::vector<PatternStep> steps;
        const auto subject_value = load_local(temporary, subject_type, arm.pattern->span);
        CoverPattern cover{};
        IsCover type_cover{};
        bool reachable = true;
        std::optional<std::size_t> by;
        if (stmt.match_is) {
            type_cover = is_pattern(*arm.pattern, subject_value, steps, declarations, context, owner);
            for (std::size_t i = 0; i < covered_types.size() && !by; ++i)
                if (is_covered(covered_types[i], type_cover, subject_type)) by = i;
            reachable = !by;
        } else {
            cover = lower_pattern(*arm.pattern, subject_value, steps, declarations, context, owner);
            std::vector<CoverPattern> witness;
            reachable = useful(covered, {cover}, column, witness);
            for (std::size_t i = 0; !reachable && i < covered.size() && !by; ++i)
                if (std::vector<CoverPattern> unused; !useful({covered[i]}, {cover}, column, unused)) by = i;
        }
        if (!reachable) {
            auto error = Diagnostic(arm.pattern->span, "unreachable match arm").with_code("E0491")
                .label("no value reaches this arm");
            if (by) error.label(stmt.arms[covering_arms[*by]].pattern->span, "this earlier arm covers every value it matches");
            else if (!covering_arms.empty())
                error.label(stmt.arms[covering_arms.front()].pattern->span, "the arms from here on cover every value it matches");
            error.help("remove the arm, or move it before the arms that cover it");
            throw error;
        }
        IrNode finish = store_local(selected, arm_index_type, arm_index(index, arm.span), arm.span);
        if (arm.guard) {
            auto guard = value(*arm.guard, context, &bool_type);
            auto branch = node(IrOp::Branch, arm.guard->span);
            branch.operands.push_back(std::move(guard));
            branch.body.push_back(std::move(finish));
            finish = std::move(branch);
        } else {
            if (stmt.match_is) covered_types.push_back(type_cover);
            else covered.push_back({cover});
            covering_arms.push_back(index);
        }
        std::vector<IrNode> region{std::move(finish)};
        for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
            if (step->statement) {
                region.insert(region.begin(), std::move(*step->statement));
                continue;
            }
            auto branch = node(IrOp::Branch, step->test->span);
            branch.operands.push_back(std::move(*step->test));
            branch.body = std::move(region);
            region.clear();
            region.push_back(std::move(branch));
        }
        if (index == 0) {
            for (auto& item : region) tests.push_back(std::move(item));
        } else {
            auto pending = node(IrOp::Branch, arm.span);
            pending.operands.push_back(equals(load_local(selected, arm_index_type, arm.span),
                                              arm_index(stmt.arms.size(), arm.span), arm.span));
            pending.body = std::move(region);
            tests.push_back(std::move(pending));
        }
        arm_scopes.push_back(std::move(context.scopes.back()));
        context.scopes.pop_back();
    }
    if (stmt.match_is) {
        is_exhaustive(stmt, subject_type, covered_types, produces_value);
    } else if (std::vector<CoverPattern> witness; useful(covered, {CoverPattern{}}, column, witness)) {
        const auto missing = cover_text(witness.front(), subject_type);
        auto error = Diagnostic(Span{stmt.span.file, stmt.span.line, stmt.span.column, stmt.span.offset, 5},
                                "non-exhaustive match, '" + missing + "' is not covered")
            .with_code("E0410").label("'" + missing + "' is not covered");
        error.note("a guarded arm covers no value; a match without a '_' arm needs an unguarded arm for every value");
        error.help("add an arm for '" + missing + "', or a '_' arm");
        wildcard_fix(error, stmt, produces_value);
        throw error;
    }
    for (auto& item : declarations) result.body.push_back(std::move(item));
    for (auto& item : tests) result.body.push_back(std::move(item));

    // Phase two: the selected arm's result.
    std::optional<IrType> match_type;
    if (expected) match_type = *expected;
    std::vector<IrNode> regions;
    for (std::size_t index = 0; index < stmt.arms.size(); ++index) {
        const auto& arm = stmt.arms[index];
        auto region = node(produces_value ? IrOp::ScopedValue : IrOp::Block, arm.span);
        context.scopes.push_back(std::move(arm_scopes[index]));
        if (produces_value) {
            auto returned = value(*arm.value, context, match_type ? &*match_type : nullptr, call_argument && expected);
            if (!match_type) match_type = returned.type;
            region.type = *match_type;
            region.body.push_back(std::move(returned));
        } else {
            for (const auto& child : arm.body) append_statement(region.body, child, context);
        }
        context.scopes.pop_back();
        regions.push_back(std::move(region));
    }
    if (match_type) result.type = *match_type;
    auto remaining = std::move(regions.back());
    regions.pop_back();
    for (auto index = regions.size(); index-- > 0;) {
        const auto span = regions[index].span;
        auto branch = node(produces_value ? IrOp::SelectValue : IrOp::Branch, span, result.type);
        branch.operands.push_back(equals(load_local(selected, arm_index_type, span), arm_index(index, span), span));
        branch.body.push_back(std::move(regions[index]));
        branch.alternative.push_back(std::move(remaining));
        remaining = std::move(branch);
    }
    result.body.push_back(std::move(remaining));
    return result;
}

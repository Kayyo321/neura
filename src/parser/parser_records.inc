// Records (docs/design/Records030.md, Grammar and Records).
//
// Included by parser.cpp inside the Parser class: these are members of
// Parser. `record` is a contextual word. It begins a record only as the first
// token of the initializer of an unannotated `let` or `const` declaration,
// and only when the tokens after it are `implements`, or `(` followed by any
// tags and then a name and ':', a declaration keyword, or `last` and a name.
// None of those begins an argument list, except a named first argument, which
// the note gives to the record.

// The offset just past the delimited group that opens `offset` tokens ahead,
// or the offset of the end of the file when it is not closed.
std::size_t skip_group_ahead(std::size_t offset) const {
    std::size_t depth = 0;
    while (cursor_ + offset < tokens_.size()) {
        const auto& token = tokens_[cursor_ + offset];
        if (token.kind == TokenKind::End) return offset;
        if (token.kind == TokenKind::Symbol) {
            if (token.text == "(" || token.text == "[" || token.text == "{") {
                ++depth;
            } else if (token.text == ")" || token.text == "]" || token.text == "}") {
                if (depth <= 1) return offset + 1;
                --depth;
            }
        }
        ++offset;
    }
    return offset;
}

// The offset after the tags that start `offset` tokens ahead.
std::size_t skip_tags_ahead(std::size_t offset) const {
    while (next_is(offset, "#")) {
        ++offset;
        if (next_is(offset, "[")) {
            offset = skip_group_ahead(offset);
            continue;
        }
        if (cursor_ + offset >= tokens_.size() || tokens_[cursor_ + offset].kind != TokenKind::Identifier)
            return offset;
        ++offset;
        if (next_is(offset, "(")) offset = skip_group_ahead(offset);
    }
    return offset;
}

// Whether the components of a record begin `offset` tokens ahead, just
// after its '('.
bool at_record_component(std::size_t offset) const {
    offset = skip_tags_ahead(offset);
    if (cursor_ + offset + 1 < tokens_.size() && tokens_[cursor_ + offset].kind == TokenKind::Identifier &&
        next_is(offset + 1, ":"))
        return true;
    return at_declaration(offset);
}

bool at_record() const {
    if (!at_contextual("record")) return false;
    if (next_keyword(1, "implements")) return true;
    return next_is(1, "(") && at_record_component(2);
}

// The initializer of an unannotated declaration, when it is a record or a
// near miss of one. Returns false when the tokens are an expression.
bool record_initializer(Declaration& result) {
    if (!at_contextual("record")) return false;
    if (next_is(1, "<")) {
        // `record<T> (x: T)` would be a generic call with a named argument.
        std::size_t offset = 2;
        std::size_t depth = 1;
        while (cursor_ + offset < tokens_.size() && depth > 0) {
            const auto& token = tokens_[cursor_ + offset];
            if (token.kind == TokenKind::End || next_is(offset, ";") || next_is(offset, "(")) break;
            if (next_is(offset, "<")) ++depth;
            if (next_is(offset, ">")) --depth;
            ++offset;
        }
        if (depth == 0 && next_is(offset, "(")) {
            const auto component = skip_tags_ahead(offset + 1);
            if (cursor_ + component + 1 < tokens_.size() &&
                tokens_[cursor_ + component].kind == TokenKind::Identifier && next_is(component + 1, ":")) {
                throw Diagnostic(tokens_[cursor_ + component + 1].span, "unexpected ':' in an argument list")
                    .with_code("E0202")
                    .label("'record<...>' is read as a call, and its arguments are expressions")
                    .help("a record is not generic in this release");
            }
        }
        return false;
    }
    if (!at_record()) return false;
    record_declaration(result);
    return true;
}

// After an initializer that is the bare name `record`: `record: Base (...)`.
void check_record_expression(const Declaration& result) {
    if (result.type || !result.initializer || result.initializer->kind != Expr::Kind::Name ||
        result.initializer->text != "record" || !at(":"))
        return;
    throw Diagnostic(current().span, "expected ';'").with_code("E0201")
        .label("the initializer ends after the name 'record'")
        .help("a record has no base class");
}

// `let p: record (x: int) = q;`: a record is not written in a type position.
void check_record_type(const Declaration& result) {
    if (!result.type || result.type->name != "record" || !result.type->arguments.empty() || !at("(")) return;
    throw Diagnostic(current().span, "unexpected '(' after the type 'record'").with_code("E0202")
        .label("a type ends before this")
        .help("a record is declared as 'let Name = record (...);' and named by that name in a type");
}

void record_declaration(Declaration& result) {
    const auto& word = advance();
    auto syntax = std::make_shared<RecordSyntax>();
    syntax->keyword = word.span;
    result.kind = Declaration::Kind::Class;
    if (match("implements")) {
        do {
            result.interfaces.push_back(type());
        } while (match(","));
    }
    expect("(");
    Declaration constructor{};
    constructor.kind = Declaration::Kind::Function;
    constructor.name = "init";
    constructor.name_span = word.span;
    constructor.span = word.span;
    constructor.exported = true;
    constructor.has_body = true;
    constructor.expression_body = true;
    constructor.generated = true;
    constructor.type = TypeSyntax{word.span, "void", {}, {}};
    while (!at_end() && !at(")")) {
        const auto tags_start = cursor_;
        auto tags = tag_list();
        const auto tags_documentation = tag_documentation(tags_start);
        const auto component_start = cursor_;
        const auto documentation = current().documentation ? current().documentation : tags_documentation;
        while (at("export") || at("let") || at("const") || at("static") || at_last_declaration()) {
            syntax->problems.emplace_back(current().span, "a record component takes no '" + current().text + "'");
            advance();
        }
        const auto& name = identifier();
        if (!at(":")) {
            throw Diagnostic(current().kind == TokenKind::End ? after_previous() : current().span,
                             "expected ':' and a type after the component name")
                .with_code("E0202")
                .label(current().kind == TokenKind::End ? "expected ':' before the end of the file"
                                                        : "expected ':', " + found_text(current()))
                .help("a record's components are not grouped: write each as 'name: Type'");
        }
        advance();
        auto component_type = type();
        if (at("=")) {
            syntax->problems.emplace_back(current().span, "a record component has no default value");
            advance();
            expression();
        }
        if (at(";")) {
            auto error = Diagnostic(current().span, "expected ',' or ')' after a record component").with_code("E0202")
                .label("a record's components are separated by ','")
                .help("a record holds components only: declare a class to give it methods or other members");
            // Skip the rest of the record, so that what follows its ')' is read
            // as the rest of the declaration.
            std::size_t depth = 0;
            while (!at_end()) {
                if (at("(") || at("[") || at("{")) ++depth;
                if (at(")") || at("]") || at("}")) {
                    if (depth == 0) {
                        advance();
                        break;
                    }
                    --depth;
                }
                advance();
            }
            throw error;
        }
        Declaration field{};
        field.kind = Declaration::Kind::Binding;
        field.name = name.text;
        field.name_span = name.span;
        field.exported = true;
        field.constant = true;
        field.last = true;
        field.type = component_type;
        field.tags = std::move(tags);
        field.documentation = documentation;
        field.span = span_from(component_start);
        Parameter parameter{};
        parameter.span = name.span;
        parameter.name = name.text;
        parameter.type = std::move(component_type);
        constructor.parameters.push_back(std::move(parameter));
        result.members.push_back(std::move(field));
        if (!match(",")) break;
    }
    syntax->close = current().span;
    expect(")");
    if (result.members.empty()) syntax->problems.emplace_back(syntax->close, "a record needs at least one component");
    Statement set{};
    set.kind = Statement::Kind::Expression;
    set.span = word.span;
    set.expressions.push_back(make_expression(Expr::Kind::Name, word.span, "set"));
    constructor.body.push_back(std::move(set));
    result.members.push_back(std::move(constructor));
    expand_constructor_shorthands(result);
    result.record = std::move(syntax);
}

// `new with(source) { name: value, ... }` (Records030.md, Copy construction).
// `new with` always begins a copy. The source is one expression in
// parentheses; the braces are required and may be empty.
Expr copy_expression(std::size_t start) {
    advance(); // with
    expect("(");
    if (at(")"))
        throw Diagnostic(current().span, "expected the record to copy").with_code("E0202")
            .label("expected an expression, found ')'")
            .help("write the record to copy in the parentheses: 'new with(source) { }'");
    auto source = expression();
    if (!at(")"))
        throw Diagnostic(current().kind == TokenKind::End ? after_previous() : current().span,
                         "expected ')' after the record to copy").with_code("E0202")
            .label(current().kind == TokenKind::End ? "expected ')' before the end of the file"
                                                    : "expected ')', " + found_text(current()))
            .help("a copy has one source: 'new with(source) { field: value }'");
    advance();
    if (!at("{")) {
        throw Diagnostic(current().kind == TokenKind::End ? after_previous() : current().span,
                         "expected '{' after 'new with(...)'")
            .with_code("E0202")
            .label(current().kind == TokenKind::End ? "expected '{' before the end of the file"
                                                    : "expected '{', " + found_text(current()))
            .help("a copy lists its replacements in braces, which may be empty: 'new with(source) { }'");
    }
    advance();
    auto result = make_expression(Expr::Kind::Copy, tokens_[start].span);
    result.operands.push_back(std::move(source));
    result.labels.emplace_back();
    result.label_spans.push_back(tokens_[start].span);
    while (!at("}")) {
        if (current().kind != TokenKind::Identifier) {
            if (current().kind == TokenKind::End) expected("'}'", "}");
            throw Diagnostic(current().span, "expected a field name").with_code("E0202")
                .label("expected a field name, " + found_text(current()))
                .help("a replacement is written 'field: value'");
        }
        const auto& name = advance();
        if (!at(":")) {
            throw Diagnostic(current().kind == TokenKind::End ? after_previous() : current().span,
                             "expected ':' after the field name").with_code("E0202")
                .label(current().kind == TokenKind::End ? "expected ':' before the end of the file"
                                                        : "expected ':', " + found_text(current()))
                .help("a replacement is written 'field: value', and names a field of the record itself");
        }
        advance();
        result.labels.push_back(name.text);
        result.label_spans.push_back(name.span);
        result.operands.push_back(expression());
        if (!match(",")) break;
    }
    expect("}");
    result.span = span_from(start);
    return result;
}

// After the word of an accessor clause (Records030.md, Interface properties
// and Virtual accessors): the dispatch words `.virtual` and `.override`, which
// may be separated by spaces or comments, and the bodyless form `| get;` of
// an interface property. Returns true when the clause ended with its ';'.
bool accessor_clause_form(AccessorClause& clause, Declaration::Kind owner) {
    while (at(".")) {
        advance();
        // `override` is a keyword and `virtual` a contextual word.
        if (!(current().kind == TokenKind::Identifier && current().text == "virtual") && !at("override")) {
            throw Diagnostic(current().kind == TokenKind::End ? after_previous() : current().span,
                             "expected 'virtual' or 'override' after '.'").with_code("E0202")
                .label(current().kind == TokenKind::End ? "expected 'virtual' or 'override' before the end of the file"
                                                        : "expected 'virtual' or 'override', " + found_text(current()))
                .help("an accessor clause is '| get:', '| get.virtual:' or '| get.override:'");
        }
        const auto& word = advance();
        clause.dispatch_words.emplace_back(word.text, word.span);
        if (clause.dispatch == AccessorClause::Dispatch::None)
            clause.dispatch = word.text == "virtual" ? AccessorClause::Dispatch::Virtual
                                                     : AccessorClause::Dispatch::Override;
    }
    if (!at(";")) return false;
    if (owner != Declaration::Kind::Interface) {
        throw Diagnostic(current().span, "expected ':' after '" + std::string(clause.setter ? "set" : "get") + "'")
            .with_code("E0202")
            .label("expected ':' and a body, found ';'")
            .help("a clause without a body, '| get;', declares an interface property; in a class write "
                  "'| get: <expression>;'");
    }
    clause.bodyless = true;
    advance();
    return true;
}

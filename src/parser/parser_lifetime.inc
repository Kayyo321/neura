    // `defer` and `using` statements (docs/design/Lifetime030.md). Both
    // words are read by context, like `scope` and `select`: they are
    // keywords only at the start of a statement and only before tokens that
    // no valid statement begins with today, and ordinary identifiers
    // everywhere else. Included in the Parser class by parser.cpp.

    // `defer` followed by an identifier (which includes `spawn`), `{` or
    // `new`. `defer(x);`, `defer = 1;`, `defer.x();` and `defer;` keep their
    // meaning as uses of a name.
    bool at_defer_statement() const {
        if (!at_contextual("defer") || cursor_ + 1 >= tokens_.size()) return false;
        const auto& next = tokens_[cursor_ + 1];
        return next.kind == TokenKind::Identifier || next_is(1, "{") ||
               (next.kind == TokenKind::Keyword && next.text == "new");
    }

    // defer-statement = "defer" call-expression ";"
    Statement defer_statement() {
        Statement result{};
        result.kind = Statement::Kind::Defer;
        result.keyword = advance().span;
        const auto help = "put the statements in a function and defer a call of it";
        if (at("{"))
            throw Diagnostic(current().span, "a deferred block is not supported").with_code("E0219")
                .label("'defer' takes one call, not a block")
                .help(help);
        auto operand = expression();
        const bool call = operand.kind == Expr::Kind::Call &&
                          operand.operands.front().kind != Expr::Kind::OptionalMember;
        if (!call) {
            auto error = Diagnostic(operand.span, "'defer' takes one call").with_code("E0219");
            if (operand.kind == Expr::Kind::New)
                error.label("this constructs an object; a deferred call names a function or method");
            else if (operand.kind == Expr::Kind::Spawn)
                error.label("this starts a task; a deferred call runs in the frame that is left");
            else if (operand.kind == Expr::Kind::Call)
                error.label("an optional-chained call may skip, so it cannot be deferred");
            else
                error.label("this is not a call");
            throw error.help(help);
        }
        if (!at(";")) {
            const auto after = after_previous();
            throw Diagnostic(after, "expected ';' after the deferred call").with_code("E0219")
                .label("expected ';' here")
                .suggest("add the missing ';'", after, ";", Applicability::MachineApplicable);
        }
        advance();
        result.expressions.push_back(std::move(operand));
        return result;
    }
    // `using` followed by `(`, an identifier and `=`, or by a balanced
    // `(...)` that is directly followed by `{`, which no call is today.
    bool at_using_statement() const {
        if (!at_contextual("using") || !next_is(1, "(")) return false;
        if (cursor_ + 3 < tokens_.size() && tokens_[cursor_ + 2].kind == TokenKind::Identifier && next_is(3, "="))
            return true;
        std::size_t depth = 0;
        for (auto i = cursor_ + 1; i < tokens_.size(); ++i) {
            const auto& token = tokens_[i];
            if (token.kind == TokenKind::End) return false;
            if (token.kind != TokenKind::Symbol) continue;
            if (token.text == "(" || token.text == "[" || token.text == "{") {
                ++depth;
            } else if (token.text == ")" || token.text == "]" || token.text == "}") {
                if (--depth == 0) return i + 1 < tokens_.size() && tokens_[i + 1].kind == TokenKind::Symbol &&
                                         tokens_[i + 1].text == "{";
            }
        }
        return false;
    }

    // using-statement = "using" "(" name "=" expression ")" block
    Statement using_statement() {
        Statement result{};
        result.kind = Statement::Kind::Using;
        result.keyword = advance().span;
        const auto form = "'using' binds one resource: using (name = value) { ... }";
        const auto nest = "nest a second 'using' for a second resource";
        expect("(");
        if (current().kind != TokenKind::Identifier || !next_is(1, "=")) {
            if (current().kind == TokenKind::Identifier && next_is(1, ":"))
                throw Diagnostic(tokens_[cursor_ + 1].span, "a 'using' binding has no type annotation")
                    .with_code("E0220")
                    .label("the resource's type is the type of its value")
                    .help("write 'using (" + current().text + " = value) { ... }'");
            throw Diagnostic(current().span, form).with_code("E0220")
                .label("expected a name and '=' here, " + found_text(current()))
                .help("bind the resource to a name, which the block uses");
        }
        result.bindings.push_back(declared_name());
        expect("=");
        result.expressions.push_back(expression());
        if (!at(")"))
            throw Diagnostic(current().span, form).with_code("E0220")
                .label(at(",") ? "a second resource begins here" : "expected ')', " + found_text(current()))
                .help(nest);
        advance();
        if (!at("{"))
            throw Diagnostic(current().span, "expected a block after 'using (...)'").with_code("E0220")
                .label("expected '{', " + found_text(current()))
                .help("the resource is closed when its block is left: using (name = value) { ... }");
        result.body = block();
        if (at(";"))
            throw Diagnostic(current().span, "a 'using' block has no ';' after its '}'").with_code("E0220")
                .label("remove this ';'")
                .suggest("remove the ';'", current().span, "", Applicability::MachineApplicable);
        return result;
    }

    // Generators and `yield` (docs/design/Lifetime030.md, Generators).

    // A function body being parsed, for the duration of the parse.
    struct BodyScope {
        Parser& parser;
        BodyScope(Parser& owner, bool generator) : parser(owner) {
            parser.bodies_.push_back({generator, parser.body_keyword()});
        }
        ~BodyScope() { parser.bodies_.pop_back(); }
        BodyScope(const BodyScope&) = delete;
        BodyScope& operator=(const BodyScope&) = delete;
    };

    // The `func` or `iterator` before the parameter list at the cursor,
    // skipping a type parameter list.
    Span body_keyword() const {
        auto i = cursor_;
        if (i > 0 && tokens_[i - 1].kind == TokenKind::Symbol &&
            (tokens_[i - 1].text == ">" || tokens_[i - 1].text == ">>")) {
            int depth = 0;
            while (i > 0) {
                const auto& token = tokens_[--i];
                if (token.kind != TokenKind::Symbol) continue;
                if (token.text == ">") ++depth;
                else if (token.text == ">>") depth += 2;
                else if (token.text == "<" && --depth == 0) break;
            }
        }
        return i > 0 ? tokens_[i - 1].span : current().span;
    }

    bool in_generator_body() const { return !bodies_.empty() && bodies_.back().generator; }

    // The index after the bracketed list that starts at `i`, or 0 when it
    // is not closed before a `;`, `{`, `}` or the end. `>>` closes two.
    std::size_t after_brackets(std::size_t i, std::string_view open, std::string_view close) const {
        int depth = 0;
        for (; i < tokens_.size(); ++i) {
            const auto& token = tokens_[i];
            if (token.kind == TokenKind::End) return 0;
            if (token.kind != TokenKind::Symbol) continue;
            if (token.text == open) {
                ++depth;
            } else if (token.text == close || (close == ">" && token.text == ">>")) {
                depth -= token.text == ">>" ? 2 : 1;
                if (depth <= 0) return depth == 0 ? i + 1 : 0;
            } else if (open == "<" && (token.text == ";" || token.text == "{" || token.text == "}")) {
                return 0;
            }
        }
        return 0;
    }

    // `iterator` followed by an optional balanced `<...>`, a balanced
    // `(...)`, and then `->` or `{`. Everywhere else it is a name.
    bool at_generator() const {
        if (!at_contextual("iterator")) return false;
        auto i = cursor_ + 1;
        if (next_is(1, "<")) {
            i = after_brackets(i, "<", ">");
            if (i == 0) return false;
        }
        if (i >= tokens_.size() || tokens_[i].kind != TokenKind::Symbol || tokens_[i].text != "(") return false;
        i = after_brackets(i, "(", ")");
        if (i == 0 || i >= tokens_.size() || tokens_[i].kind != TokenKind::Symbol) return false;
        return tokens_[i].text == "->" || tokens_[i].text == "{";
    }

    // iterator [type-parameters] (parameters) -> T { body }, with the
    // cursor at `iterator`. The result type becomes `[generator]` of T.
    void generator_declaration(Declaration& result) {
        result.generator_keyword = advance().span;
        result.kind = Declaration::Kind::Function;
        result.generator = true;
        result.generics = generic_parameters();
        const BodyScope scope(*this, true);
        result.parameters = parameters(nullptr);
        if (!at("->"))
            throw Diagnostic(current().span, "a generator declares its element type: iterator(...) -> T { ... }")
                .with_code("E0221")
                .label("expected '->' and the type of the values the generator yields")
                .suggest("declare the element type", Span{current().span.file, current().span.line,
                                                         current().span.column, current().span.offset, 0},
                         "-> T ", Applicability::HasPlaceholders);
        advance();
        auto element = type();
        result.result_written = true;
        result.has_body = true;
        result.body = block();
        TypeSyntax wrapped{element.span, "[generator]", {}, {}};
        wrapped.arguments.push_back(std::move(element));
        result.type = std::move(wrapped);
    }

    // A generator as a member, `let name = iterator(...) -> T { ... };`: a
    // function whose call returns the sequence.
    void named_generator(Declaration& result) { generator_declaration(result); }

    // A generator in an expression: a function value. Type parameters need
    // a named generator, as a lambda takes none.
    Expr generator_expression(std::size_t start) {
        auto declaration = std::make_shared<Declaration>();
        generator_declaration(*declaration);
        if (!declaration->generics.empty())
            throw Diagnostic(declaration->generics.front().span,
                             "a generator in an expression takes no type parameters").with_code("E0221")
                .label("type parameters need a named generator")
                .help("declare it as a member: let name = iterator<T>(...) -> T { ... };");
        declaration->span = span_from(start);
        auto result = make_expression(Expr::Kind::Generator, declaration->span);
        result.lambda = std::move(declaration);
        return result;
    }

    // yield = "yield" expression ";", directly in a generator body.
    Statement yield_statement() {
        Statement result{};
        result.kind = Statement::Kind::Yield;
        result.keyword = advance().span;
        if (at(";"))
            throw Diagnostic(current().span, "'yield' takes one value").with_code("E0221")
                .label("expected the value to yield")
                .help("a generator ends with 'return;' or at the end of its body");
        result.expressions.push_back(expression());
        if (at(","))
            throw Diagnostic(current().span, "'yield' takes one value").with_code("E0221")
                .label("a second value begins here")
                .help("yield the values one at a time, or yield a tuple");
        expect(";");
        return result;
    }

    // `yield` followed by a name, a literal or `new` at the start of a
    // statement outside a generator body, which no statement begins with.
    bool at_yield_outside_generator() const {
        if (!at_contextual("yield") || cursor_ + 1 >= tokens_.size()) return false;
        const auto& next = tokens_[cursor_ + 1];
        return next.kind == TokenKind::Identifier || next.kind == TokenKind::Integer ||
               next.kind == TokenKind::Float || next.kind == TokenKind::String ||
               (next.kind == TokenKind::Keyword && next.text == "new");
    }

    Diagnostic yield_outside_generator() const {
        auto error = Diagnostic(current().span, "'yield' is only a statement in the body of an 'iterator'")
            .with_code("E0222");
        if (!bodies_.empty())
            error.label(bodies_.back().keyword, "this function is not a generator");
        return error.help("declare a generator with 'iterator(...) -> T { ... }' and yield in its body");
    }

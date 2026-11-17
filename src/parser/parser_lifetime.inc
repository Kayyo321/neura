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

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

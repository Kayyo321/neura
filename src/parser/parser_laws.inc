    // Law declarations are recognized only in the initializer slot. `law`
    // remains an identifier in expressions, including calls such as `law(42)`.
    static std::optional<LawItemSyntax::Kind> law_item_kind(const Token& token) {
        using Kind = LawItemSyntax::Kind;
        if (token.kind == TokenKind::Keyword && token.text == "for") return Kind::For;
        if (token.kind != TokenKind::Identifier) return std::nullopt;
        const auto& word = token.text;
        if (word == "exists") return Kind::Exists;
        if (word == "given") return Kind::Given;
        if (word == "last") return Kind::Last;
        if (word == "measure") return Kind::Measure;
        if (word == "claim") return Kind::Claim;
        if (word == "holds") return Kind::Holds;
        if (word == "no_trap") return Kind::NoTrap;
        if (word == "traps") return Kind::Traps;
        if (word == "outcome") return Kind::Outcome;
        if (word == "refutes") return Kind::Refutes;
        return std::nullopt;
    }

    bool at_law_body(std::size_t open) const {
        return next_is(open, "(") && cursor_ + open + 1 < tokens_.size() &&
               (next_is(open + 1, ")") || law_item_kind(tokens_[cursor_ + open + 1]).has_value());
    }

    bool at_law_initializer() const {
        if (!at_contextual("law")) return false;
        if (next_is(1, ".")) return next_is_word(2, "invariant") && next_is(3, "(");
        if (!next_is(1, "<")) return at_law_body(1);
        if (next_is(2, "<")) return false;
        // A comparison can contain balanced angle brackets, and an ordinary
        // generic call can have the same prefix as a law. Check the actual
        // generic parameter syntax and the law body's opening tokens.
        Parser lookahead(tokens_, template_neura_);
        lookahead.cursor_ = cursor_ + 1;
        SpeculativeDiagnostics speculative;
        try {
            lookahead.generic_parameters();
            return lookahead.at_law_body(0);
        } catch (const Diagnostic&) {
            return false;
        }
    }

    bool at_law_attachment() const {
        return at("|") && (next_is_word(1, "proof") || next_is_word(1, "axiom") || next_is_word(1, "todo"));
    }

    void misplaced_law_attachment() {
        advance();
        record(Diagnostic(current().span, "a law attachment must follow a law declaration").with_code("E0202")
                   .label("this attachment belongs to a law"));
        skip_hook_clause();
    }

    // Retain the other law or proof entries after an error. Delimiters
    // consumed by the failed entry also count towards nesting.
    void recover_law(const Diagnostic& error, std::size_t start, std::string_view close,
                     std::string_view separator) {
        if (stopped_) throw;
        record(error);
        std::vector<std::string_view> nested;
        const auto track = [&](const Token& token) {
            if (token.kind != TokenKind::Symbol) return;
            const std::string_view text = token.text;
            if (text == "(") nested.push_back(")");
            else if (text == "[") nested.push_back("]");
            else if (text == "{") nested.push_back("}");
            else if (!nested.empty() && nested.back() == text) nested.pop_back();
        };
        for (auto i = start; i < cursor_; ++i) track(tokens_[i]);
        while (!at_end()) {
            const bool next_law_item = close == ")" && cursor_ + 1 < tokens_.size() &&
                                       law_item_kind(tokens_[cursor_ + 1]).has_value();
            if (at(separator) && (nested.empty() || next_law_item)) {
                advance();
                return;
            }
            if (at(close) && (nested.empty() || nested.back() != close)) return;
            if (at(")") && close != ")" && (nested.empty() || nested.back() != ")")) return;
            if (nested.empty() && cursor_ > start &&
                ((close == ")" && law_item_kind(current())) || (close == "}" && at_proof_statement()))) return;
            track(advance());
        }
        skipped_to_end_ = true;
    }

    void law_initializer(Declaration& declaration, std::optional<Span> override_word) {
        declaration.kind = Declaration::Kind::Law;
        declaration.law = std::make_shared<LawSyntax>();
        auto& law = *declaration.law;
        law.keyword = advance().span;
        law.override_word = std::move(override_word);
        if (match(".")) law.invariant = identifier().span;
        declaration.generics = generic_parameters();
        law.open = current().span;
        expect("(");
        const bool bare_empty_law = !law.invariant && declaration.generics.empty() && at(")");
        while (!at_end() && !at(")")) {
            const auto start = cursor_;
            try {
                law.items.push_back(law_item());
            } catch (const Diagnostic& error) {
                recover_law(error, start, ")", ";");
            }
        }
        law.close = current().span;
        expect(")");
        const bool continued = !at_law_attachment() &&
            (at("(") || at("[") || at(".") || at("?.") || at("as") || at("is") || at("?") || at("??") ||
             at("..") || at("..=") || precedence() != 0);
        if (!continued && block_depth_ == 0) return;

        auto error = continued
            ? Diagnostic(current().span, "a law declaration must be the whole initializer").with_code("E0202")
                .label("an expression continues here").label(law.keyword, "the law declaration starts here")
            : Diagnostic(law.keyword, "a law declaration belongs at file or module level").with_code("E0431")
                .label("this law is declared inside a block");
        if (bare_empty_law) error.help("write '(law)()' to call a function named 'law'");
        if (continued) throw error;
        record(error);
    }

    void law_attachment(Declaration& declaration) {
        auto& law = *declaration.law;
        while (at("|")) {
            const auto start = cursor_;
            advance();
            try {
                if (const auto& comment = tokens_[start].documentation)
                    record(Diagnostic(comment->span, "document the law above its declaration").with_code("E0202")
                               .label("documentation cannot separate a law from its proof attachment"));
                if (law.attachment)
                    throw Diagnostic(current().span, "a law takes one proof attachment").with_code("E0202")
                        .label("another attachment here").label(*law.attachment, "the first attachment");
                if (at_contextual("proof")) {
                    advance();
                    expect(":");
                    // Invariant proof branches are a Phase 3 form. Preserve
                    // their span so lowering can report the deferred feature.
                    if (law.invariant && at("{")) {
                        const auto open = cursor_;
                        skip_deferred_proof("an invariant proof block");
                        ProofSyntax body{};
                        body.kind = ProofSyntax::Kind::Block;
                        body.keyword = tokens_[open].span;
                        body.span = span_from(open);
                        law.proof = std::move(body);
                    } else {
                        law.proof = proof_expression();
                    }
                } else if (at_contextual("axiom")) {
                    law.axiom = advance().span;
                } else if (at_contextual("todo")) {
                    law.todo = advance().span;
                } else {
                    expected("'proof', 'axiom' or 'todo' after '|' in a law");
                }
                expect(";");
                law.attachment = span_from(start);
            } catch (const Diagnostic& error) {
                if (stopped_) throw;
                record(error);
                if (!at("|") && !at_declaration() && !at_tag_sigil()) skip_hook_clause();
            }
        }
    }

    void law_fulfillment(Declaration& declaration) {
        declaration.kind = Declaration::Kind::LawProof;
        declaration.law = std::make_shared<LawSyntax>();
        auto& law = *declaration.law;
        law.keyword = advance().span;
        do {
            law.target.push_back(identifier());
        } while (match("."));
        declaration.name = law.target.back().text;
        declaration.name_span = law.target.back().span;
        expect(":");
        law.proof = proof_expression();
        expect(";");
    }

    std::vector<Parameter> law_parameters() {
        const bool grouped = match("(");
        std::vector<Parameter> result;
        do {
            const auto& name = declared_name();
            expect(":");
            result.push_back({name.span, name.text, type()});
        } while (grouped && match(",") && !at(")"));
        if (grouped) expect(")");
        return result;
    }

    LawItemSyntax law_item() {
        const auto start = cursor_;
        const auto kind = law_item_kind(current());
        if (!kind) expected("a law clause ('for', 'given', 'last', or a claim)");
        LawItemSyntax result{};
        result.kind = *kind;
        result.keyword = advance().span;
        using Kind = LawItemSyntax::Kind;
        switch (*kind) {
        case Kind::For:
        case Kind::Exists:
            result.nullable = *kind == Kind::For && match("?");
            result.parameters = law_parameters();
            break;
        case Kind::Given:
            if (current().kind == TokenKind::Identifier && next_is(1, ":")) {
                result.binding = declared_name();
                expect(":");
            }
            result.proposition = proposition();
            break;
        case Kind::Last:
            result.binding = declared_name();
            if (match(":")) result.type = type();
            expect("=");
            result.expressions.push_back(expression());
            break;
        case Kind::Measure:
            expect(":");
            do {
                result.expressions.push_back(expression());
            } while (match(","));
            break;
        case Kind::Claim:
            expect(":");
            result.proposition = proposition();
            break;
        case Kind::Outcome:
            expect(":");
            result.proposition = equality_proposition();
            break;
        case Kind::Traps:
            if (match("(")) {
                result.trap_class = identifier();
                expect(")");
            }
            [[fallthrough]];
        case Kind::Holds:
        case Kind::NoTrap:
            expect(":");
            result.expressions.push_back(expression());
            break;
        case Kind::Refutes:
            break;
        }
        expect(";");
        result.span = span_from(start);
        return result;
    }

    // A proof delimiter is independent of ordinary expression precedence.
    // Parentheses and other delimited groups can still contain it.
    std::optional<std::size_t> proof_separator(std::string_view separator, bool followed_by_group = false) const {
        std::size_t depth = 0;
        for (std::size_t i = 0; cursor_ + i < tokens_.size(); ++i) {
            const auto& token = tokens_[cursor_ + i];
            if (token.kind == TokenKind::End) break;
            if (token.kind != TokenKind::Symbol && token.kind != TokenKind::Keyword) continue;
            const auto& text = token.text;
            if (depth == 0 && text == separator && (!followed_by_group || next_is(i + 1, "(")))
                return cursor_ + i;
            if (text == "(" || text == "[" || text == "{") ++depth;
            if (text == ")" || text == "]" || text == "}") {
                if (depth == 0) break;
                --depth;
            }
            if (depth == 0 && (text == ";" || (text == "," && separator == "=>"))) break;
        }
        return std::nullopt;
    }

    Expr delimited_proof_term(std::string_view separator, bool followed_by_group = false) {
        const auto end = proof_separator(separator, followed_by_group);
        struct RestoreEnd {
            std::optional<std::size_t>& end;
            std::optional<std::size_t> previous;
            ~RestoreEnd() { end = previous; }
        } restore{proof_term_end_, std::exchange(proof_term_end_, end)};
        return expression(1, false);
    }

    PropositionSyntax equality_proposition() {
        const auto start = cursor_;
        expect("{");
        PropositionSyntax result{};
        result.kind = PropositionSyntax::Kind::Equality;
        result.expressions.push_back(delimited_proof_term("=="));
        expect("==");
        result.expressions.push_back(expression(1, false));
        if (match(":")) result.type = type();
        expect("}");
        result.span = span_from(start);
        return result;
    }

    PropositionSyntax proposition() {
        const auto start = cursor_;
        if (at("{")) return equality_proposition();
        PropositionSyntax result{};
        using Kind = PropositionSyntax::Kind;
        if ((at_contextual("all") || at_contextual("either")) && next_is(1, "(")) {
            result.kind = advance().text == "all" ? Kind::All : Kind::Either;
            expect("(");
            result.propositions.push_back(proposition());
            if (result.kind == Kind::Either) {
                expect(",");
                result.propositions.push_back(proposition());
            } else {
                while (match(",") && !at(")")) result.propositions.push_back(proposition());
            }
            expect(")");
        } else if (match("not")) {
            result.kind = Kind::Not;
            result.propositions.push_back(proposition());
        } else if (match("for")) {
            result.kind = Kind::For;
            result.parameters = law_parameters();
            expect("=>");
            result.propositions.push_back(proposition());
        } else if (at_contextual("given") && proof_separator("=>")) {
            advance();
            result.kind = Kind::Given;
            result.propositions.push_back(proposition());
            expect("=>");
            result.propositions.push_back(proposition());
        } else if (at_contextual("outcome") && next_is(1, "(")) {
            advance();
            expect("(");
            result.kind = Kind::Outcome;
            result.propositions.push_back(equality_proposition());
            expect(")");
        } else if ((at_contextual("no_trap") || at_contextual("traps") || at_contextual("holds")) && next_is(1, "(")) {
            const auto& word = advance().text;
            result.kind = word == "no_trap" ? Kind::NoTrap : word == "traps" ? Kind::Traps : Kind::Boolean;
            expect("(");
            if (result.kind == Kind::Traps && current().kind == TokenKind::Identifier &&
                next_is(1, ")") && next_is(2, "(")) {
                result.trap_class = identifier();
                expect(")");
                expect("(");
            }
            result.expressions.push_back(expression());
            expect(")");
        } else {
            result.kind = Kind::Boolean;
            result.expressions.push_back(expression());
        }
        result.span = span_from(start);
        return result;
    }

    static std::optional<ProofSyntax::Kind> proof_statement_kind(const Token& token) {
        using Kind = ProofSyntax::Kind;
        if (token.kind != TokenKind::Identifier) return std::nullopt;
        const auto& word = token.text;
        if (word == "rewrite") return Kind::Rewrite;
        if (word == "have") return Kind::Have;
        if (word == "last") return Kind::Last;
        if (word == "witness") return Kind::Witness;
        if (word == "intro") return Kind::Intro;
        if (word == "inject") return Kind::Inject;
        if (word == "absurd") return Kind::Absurd;
        if (word == "contradiction") return Kind::Contradiction;
        if (word == "calc") return Kind::Calc;
        if (word == "split") return Kind::Split;
        if (word == "loop_induct") return Kind::LoopInduct;
        return std::nullopt;
    }

    bool at_proof_statement() const {
        return proof_statement_kind(current()).has_value() || at("if") || at("match");
    }

    ProofSyntax proof_statement(ProofSyntax::Kind kind) {
        const auto start = cursor_;
        ProofSyntax result{};
        result.kind = kind;
        result.keyword = advance().span;
        using Kind = ProofSyntax::Kind;
        switch (kind) {
        case Kind::Rewrite:
            if (match(".")) {
                if (!at_contextual("rev")) expected("'rev' after 'rewrite.'");
                advance();
                result.reverse = true;
            }
            result.children.push_back(proof_expression());
            if (match("in")) result.proposition = proposition();
            break;
        case Kind::Have:
            result.bindings.push_back(declared_name());
            expect(":");
            result.proposition = proposition();
            expect("=");
            result.children.push_back(proof_expression());
            break;
        case Kind::Last:
            result.bindings.push_back(declared_name());
            if (match(":")) result.type = type();
            expect("=");
            result.expression = expression();
            break;
        case Kind::Witness:
            result.expression = expression();
            break;
        case Kind::Intro:
            result.bindings.push_back(declared_name());
            break;
        case Kind::Inject: {
            const auto witness = cursor_;
            ProofSyntax evidence{};
            evidence.kind = Kind::Term;
            evidence.keyword = current().span;
            evidence.expression = delimited_proof_term("as", true);
            evidence.span = span_from(witness);
            result.children.push_back(std::move(evidence));
            expect("as");
            expect("(");
            do {
                result.bindings.push_back(declared_name());
            } while (match(","));
            expect(")");
            break;
        }
        case Kind::Absurd:
        case Kind::Contradiction:
            result.children.push_back(proof_expression());
            break;
        case Kind::Calc:
            law_calc(result);
            break;
        case Kind::Split:
            expect("{");
            while (!at_end() && !at("}") && !at(")")) {
                const auto start_arm = cursor_;
                try {
                    auto arm = proof_expression();
                    expect(";");
                    result.children.push_back(std::move(arm));
                } catch (const Diagnostic& error) {
                    recover_law(error, start_arm, "}", ";");
                }
            }
            expect("}");
            break;
        case Kind::LoopInduct:
            if (match("(")) {
                result.bindings.push_back(declared_name());
                expect(")");
            }
            skip_deferred_proof("a loop induction block");
            break;
        default:
            expected("a proof statement");
        }
        expect(";");
        result.span = span_from(start);
        return result;
    }

    void skip_deferred_proof(std::string_view description) {
        if (!at("{")) expected(std::string(description));
        const auto end = skip_group_ahead(0);
        const auto& closing = tokens_[cursor_ + end - 1];
        if (closing.kind != TokenKind::Symbol || closing.text != "}")
            expected("'}' to close the proof block");
        cursor_ += end;
    }

    void law_calc(ProofSyntax& result) {
        expect("{");
        result.expression = delimited_proof_term("==");
        do {
            const auto start = cursor_;
            ProofSyntax step{};
            step.kind = ProofSyntax::Kind::CalcStep;
            step.keyword = current().span;
            expect("==");
            step.expression = expression();
            if (!at_contextual("by")) expected("'by' and a proof of this equality");
            advance();
            step.children.push_back(proof_expression());
            expect(";");
            step.span = span_from(start);
            result.children.push_back(std::move(step));
        } while (!at("}"));
        expect("}");
    }

    ProofSyntax proof_block() {
        const auto start = cursor_;
        ProofSyntax result{};
        result.kind = ProofSyntax::Kind::Block;
        result.keyword = current().span;
        expect("{");
        while (!at_end() && !at("}") && !at(")")) {
            const auto entry = cursor_;
            try {
                if (const auto kind = proof_statement_kind(current())) {
                    result.children.push_back(proof_statement(*kind));
                } else {
                    auto proof = proof_expression();
                    if (proof.kind == ProofSyntax::Kind::If || proof.kind == ProofSyntax::Kind::Match) match(";");
                    else if (!at("}")) expect(";");
                    result.children.push_back(std::move(proof));
                }
            } catch (const Diagnostic& error) {
                recover_law(error, entry, "}", ";");
            }
        }
        expect("}");
        result.span = span_from(start);
        return result;
    }

    PatternSyntax proof_boolean_pattern() {
        const auto& literal = advance();
        PatternSyntax result{};
        result.kind = PatternSyntax::Kind::Variant;
        result.span = result.name_span = literal.span;
        result.name = literal.text;
        return result;
    }

    ProofSyntax proof_expression() {
        const auto start = cursor_;
        ProofSyntax result{};
        result.keyword = current().span;
        using Kind = ProofSyntax::Kind;
        if (at("{")) {
            if (!next_is(1, "==")) return proof_block();
            result.kind = Kind::Reflexivity;
            advance();
            expect("==");
            expect("}");
        } else if (match("match")) {
            result.kind = Kind::Match;
            expect("(");
            result.expression = expression();
            expect(")");
            expect("{");
            while (!at_end() && !at("}") && !at(")")) {
                const auto arm_start = cursor_;
                try {
                    ProofMatchArmSyntax arm{};
                    arm.pattern = whole_pattern(true);
                    expect("=>");
                    arm.body.push_back(proof_expression());
                    expect(",");
                    arm.span = span_from(arm_start);
                    result.arms.push_back(std::move(arm));
                } catch (const Diagnostic& error) {
                    recover_law(error, arm_start, "}", ",");
                }
            }
            expect("}");
        } else if (match("if")) {
            result.kind = Kind::If;
            expect("(");
            if (current().kind == TokenKind::Identifier && next_is(1, ":")) {
                result.bindings.push_back(declared_name());
                expect(":");
            }
            result.expression = expression();
            expect(")");
            result.children.push_back(proof_block());
            expect("else");
            result.children.push_back(proof_block());
        } else if (at_contextual("decide") && !next_is(1, "(") && !next_is(1, ".")) {
            advance();
            result.kind = Kind::Decide;
        } else if (at_contextual("choose_left") || at_contextual("choose_right")) {
            result.kind = advance().text == "choose_left" ? Kind::ChooseLeft : Kind::ChooseRight;
            result.children.push_back(proof_expression());
        } else {
            result.kind = Kind::Term;
            result.expression = expression();
        }
        result.span = span_from(start);
        return result;
    }

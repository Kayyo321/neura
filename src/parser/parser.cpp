#include "syntax.hpp"
#include "util/source_map.hpp"
#include "html_template.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <cstddef>
#include <string_view>
#include <utility>

namespace neura {
namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens, bool template_neura = false)
        : tokens_(tokens), template_neura_(template_neura) {}

    Expr template_expression() {
        auto value = expression();
        if (!at_end()) expected("the end of the expression");
        return value;
    }

    TemplateForHead template_for_head() {
        const bool constant = match("const");
        if (!constant) expect("let");
        const auto binding = identifier();
        std::optional<TypeSyntax> annotation;
        if (match(":")) annotation = type();
        expect("in");
        auto input = expression();
        expect(";");
        if (!at_contextual("key")) expected("'key'");
        advance();
        expect(":");
        auto key = template_expression();
        return {constant, binding, std::move(annotation), std::move(input), std::move(key)};
    }

    ParsedFile file(std::vector<Diagnostic>* diagnostics = nullptr) {
        ParsedFile result;
        result.file = current().span.file;
        while (!at_end()) {
            const auto start = cursor_;
            try {
                result.statements.push_back(statement());
            } catch (const Diagnostic& error) {
                recover(error, start);
            }
        }
        if (!errors_.empty()) {
            if (diagnostics) diagnostics->insert(diagnostics->end(), errors_.begin(), errors_.end());
            else {
                auto first = errors_.front();
                for (std::size_t i = 1; i < errors_.size(); ++i) first.also(errors_[i]);
                throw first;
            }
        }
        result.untyped_variadic = std::move(untyped_variadic_);
        return result;
    }

private:
    const std::vector<Token>& tokens_;
    bool template_neura_;
    std::size_t cursor_{};
    // Syntax errors found so far (Tooling.md, Diagnostics): the parser
    // records an error, skips to the end of the statement or member that
    // failed, and goes on, up to error_limit() errors.
    std::vector<Diagnostic> errors_;
    // Set when skipping after an error reached the end of the file. Later
    // errors there only restate that something was left open.
    bool skipped_to_end_ = false;
    // Set when error_limit() errors have been recorded.
    bool stopped_ = false;
    // Set when a declaration's value reached the end of the file and its
    // error is recorded, so that the declaration is kept without its ';'.
    bool unfinished_at_end_ = false;
    unsigned block_depth_ = 0;
    // Narrowing (docs/design/Expressions030.md): the cursor where the
    // condition of the `if` being parsed starts, and whether the next cast
    // operand may be the subject of a narrowing `x is y: T`. The flag is set
    // at the start of the condition and after each `&&` of its top level.
    std::size_t narrowing_root_ = static_cast<std::size_t>(-1);
    bool narrowing_next_ = false;
    std::optional<Span> untyped_variadic_;
    // The function bodies being parsed, innermost last, and whether each is
    // a generator's (docs/design/Lifetime030.md): `yield` is a statement
    // only directly in a generator's body. `keyword` is the `func` or
    // `iterator` that begins it.
    struct BodyKind {
        bool generator;
        Span keyword;
    };
    std::vector<BodyKind> bodies_;

    const Token& current() const { return tokens_[cursor_]; }
    bool at_end() const { return current().kind == TokenKind::End; }

    bool at(std::string_view text) const {
        return (current().kind == TokenKind::Keyword || current().kind == TokenKind::Symbol) &&
               current().text == text;
    }

    const Token& advance() { return tokens_[cursor_++]; }

    bool match(std::string_view text) {
        if (!at(text)) {
            return false;
        }
        advance();
        return true;
    }

    void expect(std::string_view text) {
        if (!match(text)) {
            if (text == ";" && cursor_ > 0) missing_semicolon();
            expected("'" + std::string(text) + "'", text);
        }
    }

    // The span just after the previous token, where a missing token belongs.
    Span after_previous() const {
        const auto& token = tokens_[cursor_ - 1];
        const auto& previous = token.span;
        Span after{previous.file, previous.line, previous.column, previous.offset + previous.length, 0};
        const bool island = token.kind == TokenKind::HtmlIsland || token.kind == TokenKind::CssIsland;
        if (island) ++after.column;
        // Columns count code points, and lines end as the lexer ends them.
        const std::string_view text = tokens_[cursor_ - 1].text;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const auto c = static_cast<unsigned char>(text[i]);
            if (c == '\r' || c == '\n') {
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
                ++after.line;
                after.column = 1;
            } else if ((c & 0xC0) != 0x80) {
                ++after.column;
            }
        }
        if (island && token.span.length == token.text.size() + 2) ++after.column;
        return after;
    }

    std::string found_text(const Token& token) const {
        if (token.kind == TokenKind::HtmlIsland) return "found an HTML body";
        if (token.kind == TokenKind::CssIsland) return "found a CSS body";
        return token.kind == TokenKind::End ? "found the end of the file" : "found '" + token.text + "'";
    }

    // The index of the unclosed opening delimiter that `close` would end,
    // searching back from the current token, or none.
    std::optional<std::size_t> unclosed(std::string_view close) const {
        const std::string_view open = close == ")" ? "(" : close == "]" ? "[" : close == "}" ? "{" : "";
        if (open.empty()) return std::nullopt;
        std::size_t depth = 0;
        for (auto i = cursor_; i-- > 0;) {
            if (tokens_[i].kind != TokenKind::Symbol) continue;
            if (tokens_[i].text == close) {
                ++depth;
            } else if (tokens_[i].text == open) {
                if (depth == 0) return i;
                --depth;
            }
        }
        return std::nullopt;
    }

    // Reports that `what` was expected at the current token. When that token
    // starts a later line (or is the end of the file), the error points just
    // after the previous token, where the missing text belongs. A missing
    // closing delimiter is labelled at the delimiter it would close.
    [[noreturn]] void expected(const std::string& what, std::string_view token = {}) const {
        const auto& next = current();
        const bool later_line = cursor_ > 0 &&
            (next.kind == TokenKind::End || next.span.line > tokens_[cursor_ - 1].span.line);
        auto error = Diagnostic(later_line ? after_previous() : next.span, "expected " + what).with_code("E0202");
        const auto open = token.empty() ? std::nullopt : unclosed(token);
        if (open) error.with_code("E0203");
        if (later_line) {
            error.label("expected " + what + " here");
            error.label(next.span, found_text(next));
        } else {
            error.label(next.kind == TokenKind::End ? "expected " + what + " before the end of the file"
                                                    : "expected " + what + ", " + found_text(next));
        }
        if (open) error.label(tokens_[*open].span, "unclosed '" + tokens_[*open].text + "' opened here");
        throw error;
    }

    // Records `error` and skips past the statement or member that began at
    // token `start`: to its terminating ';' at the same depth, to a closing
    // delimiter of the enclosing construct (which is left for the caller), or
    // to a keyword that begins the next statement. A speculative parse
    // never reaches here: it catches errors itself.
    void recover(const Diagnostic& error, std::size_t start) {
        // Past the limit, the combined error unwinds through every level.
        if (stopped_) throw;
        if (!(skipped_to_end_ && error.span.offset >= tokens_.back().span.offset)) errors_.push_back(error);
        if (errors_.size() >= error_limit()) {
            auto first = errors_.front();
            for (std::size_t i = 1; i < errors_.size(); ++i) first.also(errors_[i]);
            stopped_ = true;
            throw first;
        }
        std::size_t depth = 0;
        for (auto i = start; i < cursor_; ++i) {
            if (tokens_[i].kind != TokenKind::Symbol) continue;
            const auto& text = tokens_[i].text;
            if (text == "(" || text == "[" || text == "{") ++depth;
            else if ((text == ")" || text == "]" || text == "}") && depth > 0) --depth;
        }
        // A keyword that begins a statement cannot continue this one, and a
        // ';' outside a for header ends it, even inside an unclosed
        // delimiter: stop there rather than skip the statements that follow.
        if (cursor_ > start && ((current().kind == TokenKind::Keyword && statement_keyword(current().text)) ||
                               at_last_declaration())) return;
        if (cursor_ > start && at(";")) {
            bool header = false;
            for (auto i = start; i < cursor_ && !header; ++i)
                header = tokens_[i].kind == TokenKind::Keyword && tokens_[i].text == "for";
            if (!header) {
                advance();
                return;
            }
        }
        if (cursor_ == start && !at_end()) {
            const auto& text = current().text;
            if (current().kind == TokenKind::Symbol && (text == "(" || text == "[" || text == "{")) ++depth;
            advance();
        }
        while (!at_end()) {
            const auto& token = current();
            if (token.kind == TokenKind::Symbol) {
                const auto& text = token.text;
                if (text == "(" || text == "[" || text == "{") {
                    ++depth;
                } else if (text == ")" || text == "]" || text == "}") {
                    if (depth == 0) return;
                    --depth;
                } else if (text == ";" && depth == 0) {
                    advance();
                    return;
                }
            } else if (depth == 0 && cursor_ > start &&
                       ((token.kind == TokenKind::Keyword && statement_keyword(token.text)) || at_last_declaration())) {
                return;
            }
            advance();
        }
        skipped_to_end_ = true;
    }

    // Records an error that leaves the parse in a known state, so nothing
    // needs to be skipped, up to error_limit() errors.
    void record(const Diagnostic& error) {
        if (stopped_) return;
        errors_.push_back(error);
        if (errors_.size() >= error_limit()) {
            auto first = errors_.front();
            for (std::size_t i = 1; i < errors_.size(); ++i) first.also(errors_[i]);
            stopped_ = true;
            throw first;
        }
    }

    static bool statement_keyword(std::string_view text) {
        return text == "let" || text == "const" || text == "export" || text == "static" || text == "return" ||
               text == "if" || text == "while" || text == "for" || text == "do" || text == "break" ||
               text == "continue" || text == "destroy";
    }

    // A missing ';' is reported where it belongs, immediately after the
    // previous token, with an insertion that `neura check --fix` applies.
    [[noreturn]] void missing_semicolon(std::string_view note = {}) const {
        const auto after = after_previous();
        const auto& next = current();
        auto error = Diagnostic(after, "expected ';'").with_code("E0201").label("expected ';' here");
        if (next.span.offset != after.offset)
            error.label(next.span, next.kind == TokenKind::End ? "found the end of the file" : "found '" + next.text + "'");
        error.suggest("add the missing ';'", after, ";", Applicability::MachineApplicable);
        if (!note.empty()) error.note(std::string(note));
        throw error;
    }

    const Token& identifier(bool allow_module = false) {
        if (current().kind != TokenKind::Identifier && !((allow_module || template_neura_) && at("module"))) {
            if (current().kind == TokenKind::Keyword) {
                throw Diagnostic(current().span, "expected an identifier").with_code("E0202")
                    .label("'" + current().text + "' is a keyword");
            }
            expected("an identifier");
        }
        return advance();
    }

    // The name a declaration introduces. `nameof` is an operator from
    // 0.3.0, recognized without name lookup, so nothing is named so.
    const Token& declared_name() {
        if (at_contextual("yield") && in_generator_body())
            throw Diagnostic(current().span, "'yield' is a reserved name in a generator body").with_code("E0311")
                .label("'yield' gives the generator's next value here")
                .help("choose another name");
        if (at_contextual("nameof"))
            throw Diagnostic(current().span, "'nameof' is a reserved name").with_code("E0311")
                .label("'nameof' is an operator, so no declaration takes its name")
                .help("choose another name");
        return identifier();
    }

    Span span_from(std::size_t start) const {
        Span span = tokens_[start].span;
        const Span& end = tokens_[cursor_ - 1].span;
        span.length = end.offset + end.length - span.offset;
        return span;
    }

    std::string qualified_name(bool wildcard = false) {
        std::string name = identifier().text;
        while (match(".")) {
            name += '.';
            if (wildcard && match("*")) {
                name += '*';
                break;
            }
            name += identifier().text;
        }
        return name;
    }

    TypeSyntax type() {
        const auto start = cursor_;
        const bool reference = match("&");
        TypeSyntax result{};
        if (match("[")) {
            result.name = "[tuple]";
            do {
                result.arguments.push_back(type());
            } while (match(",") && !at("]"));
            expect("]");
        } else if (match("(")) {
            result = type();
            expect(")");
        } else if (match("func")) {
            result.name = "[function]";
            expect("(");
            if (!at(")")) {
                do {
                    result.arguments.push_back(type());
                } while (match(","));
            }
            expect(")");
            expect("->");
            result.arguments.push_back(type());
        } else if (at_anonymous_class()) {
            result = anonymous_class();
        } else if (match("class")) {
            result.name = "class";
        } else {
            result.name = qualified_name();
            if (!at_shift() && match("<")) {
                result.arguments = type_arguments();
            }
        }
        while (match("[")) {
            const bool list = current().kind == TokenKind::Identifier && current().text == "list";
            if (list) {
                advance();
            }
            result.collections.push_back(list ? CollectionKind::List : CollectionKind::Array);
            expect("]");
        }
        result.span = span_from(start);
        if (reference && result.reference)
            throw Diagnostic(result.span, "a type can have only one reference prefix").with_code("E0206");
        result.reference |= reference;
        return result;
    }

    // Anonymous classes (Types.md, Anonymous classes). In a type
    // position, `class` begins a class body only when what follows cannot
    // continue the umbrella type `class`:
    //   class ( )            class ( <member> ...
    //   class implements I, J ( ...
    //   class : Base ( <member> ...      class : Base implements I ( ...
    // Bare `class` stays the umbrella type. `class : Base ( )` with an empty
    // body is not recognized, because `c ? x as class : f()` already means
    // a conditional expression.
    // A class member declaration starts at `offset`: the same words that
    // begin a declaration anywhere, including a contextual `last` binding,
    // or the '#' of its tags, which no expression starts with.
    // A '|' there begins a class hook clause, which an anonymous class cannot
    // have (E0216), and no expression begins with '|' either.
    bool at_class_member(std::size_t offset) const {
        return at_declaration(offset) || next_is(offset, "#") || next_is(offset, "|");
    }

    bool next_keyword(std::size_t offset, std::string_view text) const {
        if (cursor_ + offset >= tokens_.size()) return false;
        const auto& token = tokens_[cursor_ + offset];
        return token.kind == TokenKind::Keyword && token.text == text;
    }

    bool at_anonymous_class() const {
        if (!at("class")) return false;
        if (next_is(1, "(")) return next_is(2, ")") || at_class_member(2);
        if (next_keyword(1, "implements")) return true;
        if (!next_is(1, ":")) return false;
        // A qualified base name, then a body with a member or an
        // implements clause.
        std::size_t offset = 2;
        while (true) {
            if (cursor_ + offset >= tokens_.size() || tokens_[cursor_ + offset].kind != TokenKind::Identifier)
                return false;
            ++offset;
            if (!next_is(offset, ".")) break;
            ++offset;
        }
        return next_keyword(offset, "implements") || (next_is(offset, "(") && at_class_member(offset + 1));
    }

    // `class [: Base] [implements I, ...] ( members )` in a type position.
    // The members use the grammar of a class declaration's body.
    TypeSyntax anonymous_class() {
        const auto start = cursor_;
        expect("class");
        auto body = std::make_shared<Declaration>();
        body->kind = Declaration::Kind::Class;
        if (match(":")) body->base = type();
        if (match("implements")) {
            do {
                body->interfaces.push_back(type());
            } while (match(","));
        }
        expect("(");
        // A method of the body is a named function wherever the type is
        // written, as it is in a class declaration at file level.
        // An error in a member ends the type: the statement that contains
        // it recovers, and a speculative parse of type arguments sees it.
        const auto depth = std::exchange(block_depth_, 0u);
        struct Restore {
            unsigned& depth;
            unsigned saved;
            ~Restore() { depth = saved; }
        } restore{block_depth_, depth};
        hook_clauses(*body, "an anonymous class");
        while (!at_end() && !at(")")) {
            if (startup_member(*body)) continue;
            auto member = tagged_declaration(Declaration::Kind::Class);
            if (member.cast_target) {
                auto more_casts = std::exchange(member.overloads, {});
                body->members.push_back(std::move(member));
                for (auto& cast : more_casts) body->members.push_back(std::move(cast));
            } else {
                body->members.push_back(std::move(member));
            }
        }
        expect(")");
        expand_constructor_shorthands(*body);
        body->span = span_from(start);
        body->name_span = tokens_[start].span;
        TypeSyntax result{};
        result.name = "[anonymous class]";
        result.anonymous = std::move(body);
        return result;
    }

    // The opening angle bracket has already been consumed.
    std::vector<TypeSyntax> type_arguments() {
        std::vector<TypeSyntax> arguments;
        do {
            arguments.push_back(type());
        } while (match(","));
        expect(">");
        return arguments;
    }

    std::vector<GenericParameter> generic_parameters() {
        std::vector<GenericParameter> result;
        if (match("<")) {
            do {
                const auto start = cursor_;
                GenericParameter parameter{};
                parameter.name = declared_name().text;
                if (match(":")) {
                    if (match("[")) {
                        do {
                            parameter.bounds.push_back(type());
                        } while (match(","));
                        expect("]");
                    } else {
                        parameter.bounds.push_back(type());
                    }
                }
                parameter.span = span_from(start);
                result.push_back(std::move(parameter));
            } while (match(","));
            expect(">");
        }
        return result;
    }

    // A parameter list is a sequence of groups: one or more names, then ':'
    // and one type that every name of the group takes. The type is parsed
    // once and copied into each parameter, so copies share an anonymous
    // class declaration (docs/lang/CoreLanguage.md, Grouped parameters).
    // With `untyped_list`, a list of names without any type, as a lambda
    // may write it, gives parameters of type "[untyped]". A factory's
    // parameters may be named `module` (Html030.md).
    std::vector<Parameter> parameters(bool* untyped_list = nullptr, bool factory = false) {
        std::vector<Parameter> result;
        if (!match("(")) return result;
        if (!at(")")) {
            // Names read since the last type, still waiting for one.
            std::vector<Parameter> group;
            const auto untyped = [&](const Parameter& name) {
                return Diagnostic(name.span, "parameter '" + name.name + "' has no type").with_code("E0214")
                    .label("a parameter takes the type that follows its group of names")
                    .help("write ': <type>' after the last name of the group, as in 'func(a, b: int)'");
            };
            while (true) {
                auto parameter_tags = tag_list();
                const auto& name = factory ? identifier(true) : declared_name();
                group.push_back({name.span, name.text, {}, false, std::move(parameter_tags)});
                if (match(",")) {
                    if (at(")")) throw untyped(group.front());
                    continue;
                }
                if (at(")") && untyped_list && result.empty()) {
                    for (auto& member : group) {
                        member.type = TypeSyntax{member.span, "[untyped]", {}, {}};
                        result.push_back(std::move(member));
                    }
                    *untyped_list = true;
                    break;
                }
                if (at(")")) throw untyped(group.front());
                expect(":");
                const bool variadic = match("...");
                const auto type_start = cursor_;
                TypeSyntax parameter_type;
                if (variadic && (at(")") || at(","))) {
                    parameter_type = {tokens_[cursor_ - 1].span, "[variadic argument]", {}, {}};
                    if (!untyped_variadic_) untyped_variadic_ = parameter_type.span;
                } else {
                    parameter_type = type();
                }
                if (variadic && group.size() > 1)
                    throw Diagnostic(group.front().span, "a variadic parameter is a group of one name")
                        .with_code("E0207")
                        .label("'" + group.front().name + "' would take the variadic type")
                        .help("give '" + group.front().name + "' its own type, as in 'func(" +
                              group.front().name + ": <type>, " + group.back().name + ": ...<type>)'");
                if (variadic && at(",") && !(factory && next_is(1, ")")))
                    throw Diagnostic(current().span, "a variadic parameter must be last").with_code("E0207");
                // `= value` after the type is a default, checked in lowering.
                std::shared_ptr<Expr> default_value;
                std::optional<Span> default_span;
                if (at("=")) {
                    const auto equals = cursor_;
                    advance();
                    default_value = std::make_shared<Expr>(expression());
                    default_span = span_from(equals);
                    if (group.size() > 1) {
                        std::string written;
                        for (auto i = type_start; i < equals; ++i)
                            written += tokens_[i].text + (tokens_[i].text == "," ? " " : "");
                        throw Diagnostic(*default_span, "a group of several names takes no default")
                            .with_code("E0226")
                            .label("this default would belong to " + std::to_string(group.size()) + " parameters")
                            .help("split the group so that the default belongs to one parameter, as in '" +
                                  group[group.size() - 2].name + ": " + written + ", " + group.back().name + ": " +
                                  written + " = ...'");
                    }
                    if (variadic)
                        throw Diagnostic(*default_span, "a variadic parameter takes no default").with_code("E0226")
                            .label("an omitted variadic argument is an empty array");
                }
                for (auto& member : group) {
                    member.type = parameter_type;
                    member.variadic = variadic;
                    member.default_value = default_value;
                    member.default_span = default_span;
                    result.push_back(std::move(member));
                }
                group.clear();
                if (!match(",") || (factory && at(")"))) break;
            }
        }
        expect(")");
        return result;
    }

    // A constructor written `init = func(x, y: int) => set;` is the
    // constructor that stores each parameter in the field of its name. The
    // expansion happens here, so every later stage sees the written-out
    // form. `set` is contextual: only the whole expression body of an
    // instance `init` is the shorthand.
    static void expand_constructor_shorthand(Declaration& constructor, const Declaration& owner) {
        if (owner.kind != Declaration::Kind::Class || constructor.kind != Declaration::Kind::Function ||
            constructor.name != "init" || constructor.static_member || constructor.cast_target ||
            !constructor.expression_body || constructor.body.size() != 1)
            return;
        const auto& only = constructor.body.front();
        if (only.kind != Statement::Kind::Expression || only.expressions.size() != 1 ||
            only.expressions.front().kind != Expr::Kind::Name || only.expressions.front().text != "set")
            return;
        const auto keyword = only.expressions.front().span;
        constructor.set_shorthand = keyword;
        std::vector<Statement> body;
        if (owner.base) {
            Statement call{};
            call.kind = Statement::Kind::Expression;
            call.span = keyword;
            auto super_name = make_expression(Expr::Kind::Name, keyword, "super");
            auto member = make_expression(Expr::Kind::Member, keyword, "init");
            member.operands.push_back(std::move(super_name));
            auto invocation = make_expression(Expr::Kind::Call, keyword);
            invocation.open = keyword;
            invocation.operands.push_back(std::move(member));
            call.expressions.push_back(std::move(invocation));
            body.push_back(std::move(call));
        }
        for (const auto& parameter : constructor.parameters) {
            Statement store{};
            store.kind = Statement::Kind::Assignment;
            store.assignment_operator = "=";
            store.span = parameter.span;
            auto member = make_expression(Expr::Kind::Member, parameter.span, parameter.name);
            member.operands.push_back(make_expression(Expr::Kind::Name, parameter.span, "$instance"));
            store.expressions.push_back(std::move(member));
            store.expressions.push_back(make_expression(Expr::Kind::Name, parameter.span, parameter.name));
            body.push_back(std::move(store));
        }
        constructor.body = std::move(body);
    }

    static void expand_constructor_shorthands(Declaration& owner) {
        for (auto& member : owner.members) {
            expand_constructor_shorthand(member, owner);
            for (auto& branch : member.overloads) expand_constructor_shorthand(branch, owner);
        }
    }

    // Return true when an expression body still needs its terminating semicolon.
    bool function_body(Declaration& result, std::string_view branch_keyword = {}, bool lambda = false) {
        const BodyScope scope(*this, false);
        result.parameters = parameters(lambda ? &result.untyped_parameters : nullptr);
        result.result_written = at("->");
        result.type = match("->") ? type() : TypeSyntax{current().span, "void", {}, {}};
        result.has_body = at("{") || at("=>");
        if (!result.has_body) return false;
        if (!match("=>")) {
            result.body = block();
            return false;
        }
        result.expression_body = true;
        Statement statement{};
        statement.kind = result.type->name == "void" && result.type->collections.empty()
            ? Statement::Kind::Expression : Statement::Kind::Return;
        statement.expressions.push_back(expression(1, true, branch_keyword));
        statement.span = statement.expressions.front().span;
        result.body.push_back(std::move(statement));
        return true;
    }

    // The module generated for a C header: its file name without the
    // extension, in PascalCase, so "include/math-ops.h" is MathOps.
    static std::string c_module_name(std::string_view header) {
        const auto slash = header.find_last_of("/\\");
        auto stem = slash == std::string_view::npos ? header : header.substr(slash + 1);
        if (const auto dot = stem.find('.'); dot != std::string_view::npos) stem = stem.substr(0, dot);
        std::string name;
        bool upper = true;
        for (const char c : stem) {
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            const bool digit = c >= '0' && c <= '9';
            if (!letter && !digit) {
                upper = true;
                continue;
            }
            if (name.empty() && digit) name += 'H';
            name += upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            upper = false;
        }
        return name.empty() ? std::string("Header") : name;
    }

    bool named_function(Declaration& result, std::string_view branch_keyword) {
        result.kind = Declaration::Kind::Function;
        result.generics = generic_parameters();
        const bool expression_body = function_body(result, branch_keyword);
        const bool host = !result.has_body && current().kind == TokenKind::Identifier && current().text == "host";
        const bool kernel = !result.has_body && current().kind == TokenKind::Identifier &&
                            current().text == "kernel" && tokens_[cursor_ + 1].kind == TokenKind::Symbol &&
                            tokens_[cursor_ + 1].text == "(";
        if (!result.has_body && current().kind == TokenKind::Identifier && current().text == "scheduler" &&
            tokens_[cursor_ + 1].kind == TokenKind::Symbol && tokens_[cursor_ + 1].text == "(") {
            advance();
            expect("(");
            const auto& operation = current();
            if (operation.kind != TokenKind::String)
                throw Diagnostic(operation.span, "expected a scheduler operation name").with_code("E0213");
            advance();
            expect(")");
            result.scheduler_binding = operation.text.substr(1, operation.text.size() - 2);
            return expression_body;
        }
        const bool callback = result.has_body && !expression_body && current().kind == TokenKind::Identifier &&
                              current().text == "callback";
        if (host || callback || kernel) {
            advance();
            expect("(");
            const auto& service = current();
            if (service.kind != TokenKind::String) {
                if (kernel) throw Diagnostic(service.span, "expected a kernel name").with_code("E0783");
                throw Diagnostic(service.span, "expected a host service name").with_code("E0213");
            }
            advance();
            expect(",");
            const auto& version = current();
            std::uint32_t number = 0;
            const auto end = version.text.data() + version.text.size();
            const auto [next, error] = std::from_chars(version.text.data(), end, number);
            if (version.kind != TokenKind::Integer || error != std::errc{} || next != end || number == 0) {
                if (kernel)
                    throw Diagnostic(version.span, "kernel ABI version must be a positive uint32").with_code("E0783");
                throw Diagnostic(version.span, "host service version must be a positive uint32").with_code("E0213");
            }
            advance();
            expect(")");
            auto& binding = kernel ? result.kernel_binding : host ? result.host_binding : result.host_callback;
            binding.emplace(service.text.substr(1, service.text.size() - 2), number);
        }
        return expression_body;
    }

    bool at_branch(std::string_view branch_keyword) const {
        return !branch_keyword.empty() && at("or") &&
            tokens_[cursor_ + 1].kind == TokenKind::Keyword && tokens_[cursor_ + 1].text == branch_keyword;
    }

    bool at_declaration(std::size_t offset = 0) const {
        return next_keyword(offset, "export") || next_keyword(offset, "static") || next_keyword(offset, "let") ||
               next_keyword(offset, "const") || at_last_declaration(offset);
    }

    // `last` is contextual: it starts a declaration only when an identifier
    // follows, which no expression statement can begin with, so `last = v;`,
    // `last(x);`, `last.next`, and `last[i] = v;` keep their meaning.
    bool at_last_declaration(std::size_t offset = 0) const {
        if (cursor_ + offset + 1 >= tokens_.size()) return false;
        const auto& word = tokens_[cursor_ + offset];
        return word.kind == TokenKind::Identifier && word.text == "last" &&
               tokens_[cursor_ + offset + 1].kind == TokenKind::Identifier;
    }

    // Tags (docs/lang/Tags.md):
    //   tags := ( '#' tag | '#' '[' tag (',' tag)* [','] ']' )+
    //   tag  := Name [ '(' [ arg (',' arg)* [','] ] ')' ]
    //   arg  := [ident ':'] literal
    // where a literal is a string, a number, true or false, a qualified
    // name, or a bracketed list of those. Empty when no '#' is next.
    std::vector<TagSyntax> tag_list() {
        std::vector<TagSyntax> result;
        while (at("#")) {
            const auto hash = cursor_;
            advance();
            if (match("[")) {
                do {
                    if (at("]")) break;
                    result.push_back(tag(cursor_));
                } while (match(","));
                expect("]");
            } else {
                result.push_back(tag(hash));
            }
        }
        return result;
    }

    TagSyntax tag(std::size_t start) {
        if (current().kind != TokenKind::Identifier) {
            throw Diagnostic(current().span, "expected a tag name").with_code("E0910")
                .label(current().kind == TokenKind::End ? "expected a tag name before the end of the file"
                                                        : "expected a tag name, " + found_text(current()))
                .help("a tag is '#Name' or '#Name(arguments)', such as '#Deprecated(use_instead: \"next\")'");
        }
        TagSyntax result{};
        const auto& name = advance();
        result.name = name.text;
        result.name_span = name.span;
        if (match("(")) {
            while (!at(")")) {
                result.arguments.push_back(tag_argument(false));
                if (!match(",")) break;
            }
            expect(")");
        }
        result.span = span_from(start);
        return result;
    }

    TagArgumentSyntax tag_argument(bool in_list) {
        TagArgumentSyntax result{};
        if (!in_list && current().kind == TokenKind::Identifier && next_is(1, ":")) {
            result.name = current().text;
            result.name_span = advance().span;
            advance();
        }
        const auto value = cursor_;
        const bool negative = match("-");
        const auto& token = current();
        if (!negative && token.kind == TokenKind::String) {
            result.kind = TagArgumentSyntax::Kind::String;
            result.text = decode_string_literal(advance().text);
        } else if (token.kind == TokenKind::Integer || token.kind == TokenKind::Float) {
            result.kind = token.kind == TokenKind::Integer ? TagArgumentSyntax::Kind::Integer
                                                           : TagArgumentSyntax::Kind::Float;
            const bool character = is_character_literal(token);
            result.text = advance().text;
            if (character) result.text = std::to_string(decode_character_literal(result.text));
            if (negative) result.text = "-" + result.text;
        } else if (!negative && (at("true") || at("false"))) {
            result.kind = TagArgumentSyntax::Kind::Boolean;
            result.text = advance().text;
        } else if (!negative && token.kind == TokenKind::Identifier) {
            result.kind = TagArgumentSyntax::Kind::Name;
            result.text = qualified_name();
        } else if (!negative && !in_list && match("[")) {
            result.kind = TagArgumentSyntax::Kind::List;
            while (!at("]")) {
                result.items.push_back(tag_argument(true));
                if (!match(",")) break;
            }
            expect("]");
        } else {
            cursor_ = value;
            throw Diagnostic(current().span, "expected a tag argument").with_code("E0910")
                .label("expected a string, a number, true or false, a name" +
                       std::string(in_list ? "" : ", or a list") + ", " + found_text(current()))
                .note("a tag argument is a literal, optionally written 'name: literal'");
        }
        // The value's span; a named argument's name has its own.
        result.span = span_from(value);
        return result;
    }

    // The documentation comment written among the tags that start at token
    // `first` and end before the current token: the last one, which is the
    // closest to the declaration.
    std::shared_ptr<const DocComment> tag_documentation(std::size_t first) const {
        std::shared_ptr<const DocComment> result;
        for (auto i = first; i < cursor_; ++i)
            if (tokens_[i].documentation) result = tokens_[i].documentation;
        return result;
    }

    // A declaration with the tags written before it. The documentation
    // comment may come before the tags, among them, or between them and the
    // declaration; the one closest to the declaration documents it.
    Declaration tagged_declaration(std::optional<Declaration::Kind> owner = std::nullopt) {
        if (!at("#")) return declaration(owner);
        const auto first = cursor_;
        auto tags = tag_list();
        const auto documentation = tag_documentation(first);
        const auto tags_span = span_from(first);
        if (at_startup_block())
            throw Diagnostic(tags_span, "a startup block takes no tags (no 'export')").with_code("E0217")
                .label("a startup block takes no tags")
                .help("remove the tags; a startup block is 'static scope { ... }'");
        if (!at_declaration() || ((at("let") || at("const")) && next_is(1, "["))) {
            const bool destructure = at_declaration();
            auto error = Diagnostic(current().kind == TokenKind::End ? after_previous() : current().span,
                                    destructure ? "tags cannot apply to a destructuring declaration"
                                                : "expected a declaration after tags")
                .with_code("E0909")
                .label(destructure ? "a destructuring declaration takes no tags"
                       : current().kind == TokenKind::End ? "expected 'let' or 'const' before the end of the file"
                                                          : "expected 'let' or 'const', " + found_text(current()))
                .label(tags_span, "these tags need a declaration to apply to");
            if (!destructure) error.note("tags apply to declarations only, not to statements or expressions");
            throw error;
        }
        auto result = declaration(owner);
        if (result.name == "_") {
            throw Diagnostic(tags_span, "tags cannot apply to '_'").with_code("E0909")
                .label("these tags apply to nothing")
                .label(result.name_span.value_or(result.span), "'_' declares no name");
        }
        if (!result.documentation) result.documentation = documentation;
        result.tags = std::move(tags);
        return result;
    }

    void derive_initializer(Declaration& declaration, std::optional<Declaration::Kind> owner) {
        const auto word = advance().span;
        if (block_depth_ || (owner && owner != Declaration::Kind::Module))
            throw Diagnostic(word, "a derivation is a member of a file or module").with_code("E0579");
        definition_has_no_annotation(declaration);
        auto syntax = std::make_shared<DeriveSyntax>();
        const auto generator = cursor_;
        syntax->generator.name = qualified_name();
        syntax->generator.span = span_from(generator);
        if (!match("<")) expected("'<' and the type to derive");
        syntax->arguments = type_arguments();
        if (match("{")) {
            while (!at_end() && !at("}")) {
                DeriveMapping mapping;
                if (!next_is_word(0, "field") && !next_is_word(0, "variant"))
                    expected("'field' or 'variant' in a derivation mapping");
                mapping.variant = advance().text == "variant";
                const auto path = cursor_;
                do {
                    const auto& name = identifier();
                    mapping.parts.push_back(name.text);
                    mapping.part_spans.push_back(name.span);
                } while (match("."));
                mapping.path = span_from(path);
                expect(":");
                if (current().kind != TokenKind::String || current().parts) expected("a wire name as one string literal");
                mapping.wire_span = current().span;
                mapping.wire = decode_string_literal(advance().text);
                if (!match(";")) expected("';' after the mapping");
                syntax->mappings.push_back(std::move(mapping));
            }
            syntax->close_brace = current().span;
            expect("}");
        }
        declaration.kind = Declaration::Kind::Module;
        declaration.derive = std::move(syntax);
    }

    Declaration declaration(std::optional<Declaration::Kind> owner = std::nullopt) {
        const auto start = cursor_;
        bool expression_body = false;
        Declaration result{};
        result.kind = Declaration::Kind::Binding;
        result.documentation = current().documentation;
        result.exported = match("export");
        if (result.exported && at_startup_block())
            throw Diagnostic(tokens_[cursor_ - 1].span, "a startup block takes no tags (no 'export')")
                .with_code("E0217").label("a startup block is not exported")
                .suggest("remove 'export'", Span{tokens_[cursor_ - 1].span.file, tokens_[cursor_ - 1].span.line,
                         tokens_[cursor_ - 1].span.column, tokens_[cursor_ - 1].span.offset,
                         current().span.offset - tokens_[cursor_ - 1].span.offset}, "",
                         Applicability::MachineApplicable);
        result.static_member = match("static");
        result.constant = match("const");
        if (!result.constant && at_last_declaration()) {
            advance();
            result.constant = result.last = true;
        } else if (!result.constant) {
            expect("let");
        }
        if (match(".")) {
            const auto malformed_extension = [&](const Span& word, const std::string& reason) {
                return Diagnostic(word, "malformed extension declaration").with_code("E0228").label(reason);
            };
            if (current().kind == TokenKind::Identifier &&
                (current().text == "extension" || current().text == "extensions")) {
                // `extension` and `extensions` are words only directly after
                // `let.` (Calls030.md, Grammar).
                const auto& word = advance();
                result.extension = word.text == "extension";
                result.extension_set = !result.extension;
                result.extension_word = word.span;
                const auto form = "'let." + word.text + "'";
                if (result.constant)
                    throw malformed_extension(word.span, form + " is written with 'let', not 'const'");
                if (result.static_member)
                    throw malformed_extension(word.span, form + " takes no 'static'");
                if (block_depth_ > 0)
                    throw malformed_extension(word.span, form + " belongs to a file or a module, not a block");
                if (owner == Declaration::Kind::Class || owner == Declaration::Kind::Interface)
                    throw malformed_extension(word.span, form + " belongs to a file or a module, not a class");
                if (at(".")) throw malformed_extension(word.span, form + " combines with no other form");
            } else if (!result.constant && match("promote")) {
                result.promote_import = true;
                if (at(".") && next_is_word(1, "extensions"))
                    throw Diagnostic(tokens_[cursor_ + 1].span, "malformed extension declaration").with_code("E0228")
                        .label("an extension set is not promoted; call its functions as members or through its alias");
            } else if (!result.constant && at("as")) {
                if (owner != Declaration::Kind::Class)
                    throw Diagnostic(current().span, "a cast declaration requires a class").with_code("E0512");
                const auto& name = advance();
                result.name = name.text;
                result.name_span = name.span;
                result.cast_target = type();
            } else {
                expect("override");
                result.override_member = true;
            }
        }
        if (!result.cast_target) {
            const auto& name = declared_name();
            result.name = name.text;
            result.name_span = name.span;
            if (match(":")) {
                result.type = type();
                check_record_type(result);
            }
        }

        if (result.cast_target) expect("=");
        if (result.cast_target || match("=")) {
            if (result.cast_target || (!result.type && block_depth_ == 0 && at("func"))) {
                expect("func");
                const auto function_start = cursor_ - 1;
                const std::string_view branch_keyword = result.cast_target ? "as" : "func";
                expression_body = named_function(result, branch_keyword);
                result.span = span_from(start);
                while (at_branch(branch_keyword)) {
                    const auto& connector = advance(); // or
                    const auto branch_start = cursor_;
                    Declaration branch{};
                    branch.name = result.name;
                    branch.documentation = connector.documentation;
                    branch.name_span = current().span;
                    branch.constant = result.constant;
                    branch.exported = result.exported;
                    branch.override_member = result.override_member;
                    branch.extension = result.extension;
                    branch.static_member = result.static_member;
                    if (result.cast_target) {
                        expect("as");
                        branch.cast_target = type();
                        expect("=");
                    }
                    expect("func");
                    expression_body = named_function(branch, branch_keyword);
                    branch.span = span_from(branch_start);
                    result.overloads.push_back(std::move(branch));
                }
                if (!result.cast_target && result.overloads.empty() && result.has_body && !expression_body &&
                    result.generics.empty() && !result.override_member &&
                    (at("(") || at("[") || at(".") || at("as") || precedence() != 0)) {
                    auto lambda = std::make_shared<Declaration>();
                    lambda->kind = Declaration::Kind::Function;
                    lambda->parameters = std::move(result.parameters);
                    lambda->type = std::move(result.type);
                    lambda->body = std::move(result.body);
                    lambda->has_body = true;
                    lambda->span = span_from(function_start);
                    auto value = make_expression(Expr::Kind::Lambda, lambda->span);
                    value.lambda = std::move(lambda);
                    result.kind = Declaration::Kind::Binding;
                    result.type.reset();
                    result.has_body = false;
                    auto postfix = postfix_tail(function_start, std::move(value));
                    auto cast = cast_tail(function_start, std::move(postfix));
                    result.initializer = expression_tail(function_start, std::move(cast), 1);
                }
            } else if (!result.cast_target && !result.type && block_depth_ == 0 && at_generator()) {
                named_generator(result);
                result.span = span_from(start);
            } else if (match("enum")) {
                definition_has_no_annotation(result);
                result.kind = Declaration::Kind::Enum;
                result.generics = generic_parameters();
                expect("(");
                hook_clauses(result, "an enum");
                while (!at(")")) {
                    if (startup_member(result)) continue;
                    const auto tags_start = cursor_;
                    auto variant_tags = tag_list();
                    const auto variant_documentation = tag_documentation(tags_start);
                    const auto variant_start = cursor_;
                    const auto& variant = identifier();
                    Declaration member{};
                    member.kind = Declaration::Kind::Binding;
                    member.name = variant.text;
                    member.documentation = variant.documentation ? variant.documentation : variant_documentation;
                    member.tags = std::move(variant_tags);
                    member.name_span = variant.span;
                    const bool positional = match("(");
                    member.named_payload = !positional && match("{");
                    if (positional || member.named_payload) {
                        const auto close = positional ? ")" : "}";
                        do {
                            const auto field_start = cursor_;
                            std::string name;
                            if (member.named_payload) {
                                name = identifier().text;
                                expect(":");
                            }
                            auto payload = type();
                            if (at("=")) {
                                const auto equals = cursor_;
                                advance();
                                expression();
                                throw Diagnostic(span_from(equals), "an enum payload field takes no default")
                                    .with_code("E0226")
                                    .label("a variant is constructed with every payload field");
                            }
                            member.parameters.push_back({span_from(field_start), std::move(name), std::move(payload)});
                        } while (match(",") && !at(close));
                        expect(close);
                    }
                    member.span = span_from(variant_start);
                    result.members.push_back(std::move(member));
                    if (!match(",")) {
                        break;
                    }
                }
                if (result.members.empty()) {
                    throw Diagnostic(current().span, "an enum needs at least one variant").with_code("E0209");
                }
                expect(")");
            } else if (at("module") || at("class") || at("interface") || at("singleton")) {
                definition_has_no_annotation(result);
                const auto kind = advance().text;
                result.singleton = kind == "singleton";
                result.kind = kind == "class" || result.singleton ? Declaration::Kind::Class
                            : kind == "interface" ? Declaration::Kind::Interface : Declaration::Kind::Module;
                if (result.kind != Declaration::Kind::Module && !result.singleton) {
                    result.generics = generic_parameters();
                }
                if (result.kind == Declaration::Kind::Interface) {
                    if (match(":")) {
                        do {
                            result.interfaces.push_back(type());
                        } while (match(","));
                    }
                } else if (!result.singleton && match(":")) {
                    result.base = type();
                }
                if ((result.kind == Declaration::Kind::Class || result.kind == Declaration::Kind::Module) &&
                    at("implements")) {
                    // A module's list is a primitive companion's conformance
                    // (docs/design/Records030.md), which lowering checks.
                    result.implements_span = advance().span;
                    do {
                        result.interfaces.push_back(type());
                    } while (match(","));
                }
                expect("(");
                hook_clauses(result, result.kind == Declaration::Kind::Class ? nullptr
                                     : result.kind == Declaration::Kind::Interface ? "an interface" : "a module");
                // Set when a member's error reached the end of the file.
                bool member_failed_at_end = false;
                while (!at_end() && !at(")")) {
                    const auto member_start = cursor_;
                    if (startup_member(result)) continue;
                    Declaration member;
                    try {
                        member = tagged_declaration(result.kind);
                    } catch (const Diagnostic& error) {
                        recover(error, member_start);
                        skip_accessor_clauses();
                        member_failed_at_end = at_end();
                        continue;
                    }
                    if (member.cast_target) {
                        auto more_casts = std::exchange(member.overloads, {});
                        result.members.push_back(std::move(member));
                        for (auto& cast : more_casts) result.members.push_back(std::move(cast));
                    } else {
                        result.members.push_back(std::move(member));
                    }
                }
                // Edge case 143 (docs/design/Records030.md): a member whose
                // error ran to the end of the file has reported it; the body
                // keeps its other members, as a record does (edge case 141).
                // A body around such a member, or around such a record,
                // stops in the same way.
                if ((member_failed_at_end || unfinished_at_end_) && at_end())
                    unfinished_at_end_ = true;
                else
                    expect(")");
                expand_constructor_shorthands(result);
            } else if (next_is_word(0, "derive") && cursor_ + 1 < tokens_.size() &&
                       tokens_[cursor_ + 1].kind == TokenKind::Identifier) {
                derive_initializer(result, owner);
            } else if (next_is_word(0, "derive") && tokens_[cursor_ + 1].kind == TokenKind::End) {
                advance();
                expected("a generator or ';' after 'derive'");
            } else if (result.constant && !result.last && block_depth_ == 0 &&
                       (!owner || *owner == Declaration::Kind::Module) && at_stylesheet()) {
                const auto initializer = cursor_;
                result.initializer = stylesheet(initializer);
                if (at("(") || at("[") || at(".") || at("as") || at("is") || at("?") || precedence() != 0)
                    throw misplaced_stylesheet(span_from(initializer));
            } else if (!result.type && !result.last && record_initializer(result)) {
                // A record (parser_records.inc).
            } else {
                result.initializer = expression();
                if (result.initializer->kind == Expr::Kind::HtmlFactory) {
                    auto& alternatives = result.initializer->html_factory->overloads;
                    while (at("or") && contextual_at(1, "html")) {
                        advance();
                        const auto factory = cursor_;
                        alternatives.push_back(html_factory(factory, false));
                    }
                }
                check_record_expression(result);
            }
        } else if (!result.type && result.last) {
            throw deferred_last_without_type(result);
        } else if (!result.type) {
            throw Diagnostic(current().span, "expected a type annotation or an initializer").with_code("E0205")
                .label("'" + result.name + "' needs a type, a value, or both")
                .help("write ': T' after the name for its type, and '= value' for its value");
        }

        if (result.extension || result.extension_set) check_extension_form(result);

        // Every declaration ends with ';', including one whose value is a body.
        const auto& final_branch = result.overloads.empty() ? result : result.overloads.back();
        const bool body_value = (final_branch.has_body && !expression_body) ||
                                result.kind == Declaration::Kind::Class ||
                                result.kind == Declaration::Kind::Interface ||
                                result.kind == Declaration::Kind::Module;
        // A declaration whose value ran to the end of the file has recorded
        // its error already (parser_records.inc, edge case 141).
        if (!(unfinished_at_end_ && at_end())) {
            if (body_value && !at(";"))
                missing_semicolon("every declaration ends with ';', including one whose value is a function, "
                                  "class or module body");
            expect(";");
        }
        if (result.overloads.empty()) result.span = span_from(start);
        if (owner) accessor_clauses(result, *owner);
        return result;
    }

    // `let.extension` declares a function with a receiver parameter, and
    // `let.extensions` imports a module by an alias (Calls030.md, E0228).
    static void check_extension_form(const Declaration& declaration) {
        const auto& word = *declaration.extension_word;
        const auto malformed = [&](const std::string& reason) {
            return Diagnostic(word, "malformed extension declaration").with_code("E0228").label(reason);
        };
        if (declaration.extension) {
            if (declaration.kind != Declaration::Kind::Function)
                throw malformed("'let.extension' declares a function, as in 'let.extension name = func(receiver: T)'");
            const auto no_receiver = [](const Declaration& branch) { return branch.parameters.empty(); };
            if (no_receiver(declaration) || std::ranges::any_of(declaration.overloads, no_receiver))
                throw malformed("an extension function takes its receiver as its first parameter");
            return;
        }
        const auto& value = declaration.initializer;
        if (!value || value->kind != Expr::Kind::Import)
            throw malformed("'let.extensions' imports a module, as in 'let.extensions alias = import:Module'");
        if (!value->c_import.empty())
            throw malformed("a C header has no extension functions");
        if (value->text.ends_with(".*"))
            throw malformed("an extension set names one module, not a wildcard");
        if (declaration.name == "_")
            throw malformed("an extension set needs an alias, which calls its functions when they are ambiguous");
    }

    // `last x;`: a deferred `last` binding is assigned later, so only its
    // type can say what it holds.
    static Diagnostic deferred_last_without_type(const Declaration& declaration) {
        const auto name = *declaration.name_span;
        const Span after{name.file, name.line, name.column + name.length, name.offset + name.length, 0};
        return Diagnostic(name, "a deferred 'last' binding needs a type").with_code("E0444")
            .label("'" + declaration.name + "' has neither a type nor a value")
            .suggest("give it a type and assign it exactly once later", after, ": <type>",
                     Applicability::HasPlaceholders)
            .suggest("or give it its value now", after, " = <value>", Applicability::HasPlaceholders);
    }

    // `last [a, b] ...`: `last` declares one name, and an index into a
    // binding named `last` can hold neither a ',' nor a ':' after its ']',
    // so this can only be meant as a destructuring declaration.
    bool at_last_destructuring() const {
        return at_contextual("last") && next_is(1, "[") && cursor_ + 3 < tokens_.size() &&
               tokens_[cursor_ + 2].kind == TokenKind::Identifier &&
               (next_is(3, ",") || (next_is(3, "]") && next_is(4, ":")));
    }

    Diagnostic last_destructuring() const {
        const auto& word = current().span;
        auto close = cursor_ + 1;
        while (close + 1 < tokens_.size() && !next_is(close - cursor_, "]")) ++close;
        const auto& end = tokens_[close].span;
        Span names = word;
        names.length = end.offset + end.length - word.offset;
        return Diagnostic(word, "a destructuring declaration cannot use 'last'").with_code("E0536")
            .label("'last' declares a single name")
            .label(names, "these names are destructured")
            .note("'last' is a contextual word: followed by '[' it is a binding named 'last' being indexed")
            .suggest("declare the names with 'const', which binds each of them once", word, "const",
                     Applicability::MachineApplicable);
    }

    // `static scope { ... }` (Modules.md, Startup blocks). `scope` after
    // `static` is a contextual word; `static let` and every other use of
    // either word keep their meaning.
    bool at_startup_block() const {
        return at("static") && cursor_ + 1 < tokens_.size() &&
               tokens_[cursor_ + 1].kind == TokenKind::Identifier && tokens_[cursor_ + 1].text == "scope";
    }

    static Diagnostic misplaced_startup_block(const Span& keyword) {
        return Diagnostic(keyword, "a startup block is written at file or module level").with_code("E0218")
            .label("not at file or module level")
            .help("move it to the top level of the file or into a module, or call a function from a file-level "
                  "block");
    }

    // Skips one balanced group that starts at the current '(' or '{'.
    void skip_group() {
        std::size_t depth = 0;
        while (!at_end()) {
            const auto& text = current().text;
            if (current().kind == TokenKind::Symbol && (text == "(" || text == "[" || text == "{")) ++depth;
            else if (current().kind == TokenKind::Symbol && (text == ")" || text == "]" || text == "}")) --depth;
            advance();
            if (depth == 0) return;
        }
    }

    // A Block with `startup` set. A malformed block is recorded as E0217,
    // and the parse goes on after the group the block would have been:
    // a parenthesized group and a braced body, or a ';'.
    Statement startup_block() {
        const auto start = cursor_;
        Statement result{};
        result.kind = Statement::Kind::Block;
        result.startup = true;
        advance();
        advance();
        result.keyword = span_from(start);
        if (!at("{")) {
            const auto& next = current();
            const bool later_line = next.kind == TokenKind::End || next.span.line > tokens_[cursor_ - 1].span.line;
            auto error = Diagnostic(later_line && !next_is(0, ";") ? after_previous() : next.span,
                                    "expected '{' after 'static scope'").with_code("E0217")
                .label(next.kind == TokenKind::End ? "expected '{' before the end of the file"
                                                   : "expected '{', " + found_text(next))
                .help("a startup block is 'static scope { ... }'");
            if (at("(")) error.note("a startup block names no scope handle");
            record(error);
            if (at("(")) skip_group();
            if (at("{")) skip_group();
            else match(";");
            result.span = span_from(start);
            return result;
        }
        result.body = block();
        result.span = span_from(start);
        if (at(";")) {
            const auto semicolon = current().span;
            record(Diagnostic(semicolon, "a startup block has no ';' after its '}'").with_code("E0217")
                       .label("remove this ';'")
                       .suggest("remove the ';'", semicolon, "", Applicability::MachineApplicable));
            advance();
        }
        return result;
    }

    // A startup block among the members of `owner`: kept for a module, and
    // E0218 in a class, an interface or an enum. False when none is next.
    bool startup_member(Declaration& owner) {
        if (!at_startup_block()) return false;
        auto block = startup_block();
        if (owner.kind == Declaration::Kind::Module && block_depth_ == 0) owner.startup_blocks.push_back(std::move(block));
        else record(misplaced_startup_block(block.keyword));
        return true;
    }

    // Class hook clauses (docs/design/Hooks030.md, Class hook clauses):
    //   hook-clause = "|" ("get" | "set") [ "." "override" ] ":" (expression | block) ";"
    // written after the '(' of a declared class and before its first member.
    // A clause there in an interface, a module, an enum or an anonymous class
    // is E0216 and is dropped. Tags or `export` before a clause are E0215.
    bool at_hook_clause() const {
        if (at("|")) return true;
        if (at("export")) return next_is(1, "|");
        if (!at("#")) return false;
        auto i = cursor_;
        const auto skip = [&](std::string_view open, std::string_view close) {
            std::size_t depth = 0;
            for (; i < tokens_.size() && tokens_[i].kind != TokenKind::End; ++i) {
                if (tokens_[i].kind != TokenKind::Symbol) continue;
                if (tokens_[i].text == open) ++depth;
                else if (tokens_[i].text == close && --depth == 0) {
                    ++i;
                    return;
                }
            }
        };
        while (i < tokens_.size() && tokens_[i].kind == TokenKind::Symbol && tokens_[i].text == "#") {
            ++i;
            if (i < tokens_.size() && tokens_[i].kind == TokenKind::Symbol && tokens_[i].text == "[") {
                skip("[", "]");
                continue;
            }
            if (i >= tokens_.size() || tokens_[i].kind != TokenKind::Identifier) return false;
            ++i;
            if (i < tokens_.size() && tokens_[i].kind == TokenKind::Symbol && tokens_[i].text == "(") skip("(", ")");
        }
        return i < tokens_.size() && tokens_[i].kind == TokenKind::Symbol && tokens_[i].text == "|";
    }

    static Diagnostic malformed_hook(const Span& at, std::string message) {
        return Diagnostic(at, std::move(message)).with_code("E0215")
            .help("a class hook clause is '| get: <expression>;' or '| set: { ... };'");
    }

    // `outside` names what the clauses are written in when it cannot have
    // hooks, such as "an interface"; null for a declared class.
    void hook_clauses(Declaration& owner, const char* outside) {
        while (at_hook_clause()) {
            try {
                auto clause = hook_clause();
                if (!outside) {
                    owner.hooks.push_back(std::move(clause));
                    continue;
                }
                std::string kind = outside;
                const Span bar{clause.span.file, clause.span.line, clause.span.column, clause.span.offset, 1};
                record(Diagnostic(bar, kind + " cannot declare class hooks").with_code("E0216")
                           .label("class hook clause in " + kind)
                           .help("declare the hooks on a named class"));
            } catch (const Diagnostic& error) {
                if (stopped_) throw;
                record(error);
                skip_hook_clause();
            }
        }
    }

    HookClause hook_clause() {
        if (at("#")) {
            const auto first = cursor_;
            tag_list();
            record(malformed_hook(span_from(first), "a class hook clause takes no tags")
                       .label("tags before a class hook clause")
                       .note("a tag of the class is written before 'let'"));
        } else if (at("export")) {
            const auto word = advance().span;
            record(malformed_hook(word, "a class hook clause takes no tags")
                       .label("'export' does not apply to a class hook clause")
                       .suggest("remove 'export'", Span{word.file, word.line, word.column, word.offset,
                                                        current().span.offset - word.offset},
                                "", Applicability::MachineApplicable));
        }
        const auto start = cursor_;
        advance();
        const auto& keyword = current();
        if (keyword.kind != TokenKind::Identifier || (keyword.text != "get" && keyword.text != "set"))
            throw malformed_hook(keyword.span, "expected 'get' or 'set' after '|'")
                .label(keyword.kind == TokenKind::End ? "expected 'get' or 'set' before the end of the file"
                                                      : "expected 'get' or 'set', " + found_text(keyword));
        advance();
        HookClause clause;
        clause.setter = keyword.text == "set";
        clause.keyword = keyword.span;
        if (match(".")) {
            if (!at("override"))
                throw malformed_hook(current().span, "only '.override' may follow 'get' or 'set'")
                    .label("expected 'override', " + found_text(current()));
            clause.override_word = advance().span;
        }
        if (!at(":"))
            throw malformed_hook(current().kind == TokenKind::End ? after_previous() : current().span,
                                 "expected ':'")
                .label(current().kind == TokenKind::End ? "expected ':' before the end of the file"
                                                        : "expected ':', " + found_text(current()));
        advance();
        if (at("{")) {
            clause.block = true;
            clause.body = block();
            clause.close = tokens_[cursor_ - 1].span;
        } else {
            if (at(")") || at(";") || at_end() || at_declaration())
                throw malformed_hook(current().kind == TokenKind::End ? after_previous() : current().span,
                                     "a class hook clause needs an expression or a block after ':'")
                    .label(current().kind == TokenKind::End ? "expected an expression before the end of the file"
                                                            : "expected an expression, " + found_text(current()));
            clause.expression = expression();
        }
        if (!at(";")) {
            const auto after = after_previous();
            record(malformed_hook(current().kind == TokenKind::End ? after : current().span,
                                  "a class hook clause ends with ';'")
                       .label(current().kind == TokenKind::End ? "expected ';' before the end of the file"
                                                               : "expected ';', " + found_text(current()))
                       .suggest("add the missing ';'", after, ";", Applicability::MachineApplicable));
        } else {
            advance();
        }
        clause.span = span_from(start);
        return clause;
    }

    // After an error in a clause: skips to its ';', or to the ')' or '}'
    // that closes the enclosing body, which is left for the caller.
    void skip_hook_clause() {
        std::size_t depth = 0;
        while (!at_end()) {
            const auto& token = current();
            if (token.kind == TokenKind::Symbol) {
                const auto& text = token.text;
                if (text == "(" || text == "[" || text == "{") {
                    ++depth;
                } else if (text == ")" || text == "]" || text == "}") {
                    if (depth == 0) return;
                    --depth;
                } else if (text == ";" && depth == 0) {
                    advance();
                    return;
                }
            }
            advance();
        }
    }

    // Accessor clauses after a member's ';' (Types.md, Accessors). No member
    // begins with '|', so the position is unambiguous, and `get`, `set`, and
    // `value` stay ordinary identifiers everywhere else. Lowering checks
    // which members may have them.
    void accessor_clauses(Declaration& member, Declaration::Kind owner = Declaration::Kind::Class) {
        while (at("|")) {
            const auto start = cursor_;
            advance();
            const auto& keyword = current();
            if (keyword.kind != TokenKind::Identifier || (keyword.text != "get" && keyword.text != "set"))
                throw Diagnostic(keyword.span, "expected 'get' or 'set' after '|'").with_code("E0202")
                    .label(keyword.kind == TokenKind::End ? "expected 'get' or 'set' before the end of the file"
                                                          : "expected 'get' or 'set', " + found_text(keyword))
                    .help("an accessor clause is '| get: <expression>;' or '| set: <statement>;'");
            advance();
            AccessorClause clause;
            clause.setter = keyword.text == "set";
            clause.keyword = keyword.span;
            // `.virtual` and `.override` (parser_records.inc), and the
            // bodyless clause of an interface property.
            if (accessor_clause_form(clause, owner)) {
                clause.span = span_from(start);
                member.accessors.push_back(std::move(clause));
                continue;
            }
            expect(":");
            if (at("{")) {
                clause.block = true;
                clause.body = block();
                if (!at(";")) missing_semicolon("an accessor clause ends with ';', including one whose body is a block");
                expect(";");
            } else if (clause.setter) {
                const auto statement_start = cursor_;
                auto body = simple_statement();
                body.span = span_from(statement_start);
                clause.body.push_back(std::move(body));
                expect(";");
            } else {
                clause.expression = expression();
                expect(";");
            }
            clause.span = span_from(start);
            member.accessors.push_back(std::move(clause));
        }
    }

    // After an error in a member, skips the accessor clauses that follow it,
    // each through its ';', so that they are not read as members of their own.
    void skip_accessor_clauses() {
        while (at("|")) {
            advance();
            std::size_t depth = 0;
            while (!at_end()) {
                const auto& token = current();
                if (token.kind == TokenKind::Symbol) {
                    const auto& text = token.text;
                    if (text == "(" || text == "[" || text == "{") {
                        ++depth;
                    } else if (text == ")" || text == "]" || text == "}") {
                        if (depth == 0) return;
                        --depth;
                    } else if (text == ";" && depth == 0) {
                        advance();
                        break;
                    }
                }
                advance();
            }
        }
    }

#include "parser_records.inc"

    static void definition_has_no_annotation(const Declaration& declaration) {
        if (declaration.type) {
            throw Diagnostic(declaration.type->span, "a definition cannot have a binding type annotation")
                .with_code("E0208");
        }
    }

    std::vector<Statement> block() {
        expect("{");
        ++block_depth_;
        std::vector<Statement> result;
        while (!at_end() && !at("}")) {
            const auto start = cursor_;
            try {
                result.push_back(statement());
            } catch (const Diagnostic& error) {
                recover(error, start);
            }
        }
        expect("}");
        --block_depth_;
        return result;
    }

    Expr condition(std::string_view keyword = {}) {
        expect("(");
        try {
            auto result = expression();
            expect(")");
            return result;
        } catch (const Diagnostic&) {
            misplaced_narrowing(keyword);
        }
    }

    // The condition of an `if`, where narrowings may stand
    // (docs/design/Expressions030.md, Narrowing).
    Expr if_condition(Statement& statement) {
        expect("(");
        const auto saved_root = std::exchange(narrowing_root_, cursor_);
        const auto saved_next = std::exchange(narrowing_next_, true);
        std::optional<Expr> result;
        try {
            result = expression();
        } catch (const Diagnostic&) {
            narrowing_root_ = saved_root;
            narrowing_next_ = saved_next;
            misplaced_narrowing("if");
        }
        narrowing_root_ = saved_root;
        narrowing_next_ = saved_next;
        check_narrowings(*result, statement);
        try {
            expect(")");
        } catch (const Diagnostic&) {
            misplaced_narrowing("if");
        }
        return std::move(*result);
    }

    // Called while a condition's parse error propagates: when it failed at a
    // ':' directly after `is name` or `is not name`, the source meant a
    // narrowing where none may stand, and the error is E0265. Otherwise the
    // error propagates unchanged.
    [[noreturn]] void misplaced_narrowing(std::string_view keyword) const {
        if (!keyword.empty() && at(":") && cursor_ >= 2 && tokens_[cursor_ - 1].kind == TokenKind::Identifier) {
            const auto& before = tokens_[cursor_ - 2];
            const bool test = before.kind == TokenKind::Keyword &&
                (before.text == "is" || (before.text == "not" && cursor_ >= 3 &&
                                         tokens_[cursor_ - 3].kind == TokenKind::Keyword &&
                                         tokens_[cursor_ - 3].text == "is"));
            if (test) throw misplaced_narrowing(tokens_[cursor_ - 1].span, tokens_[cursor_ - 1].text, keyword);
        }
        throw;
    }

    static Diagnostic misplaced_narrowing(const Span& name, const std::string& binding, std::string_view keyword) {
        auto error = Diagnostic(name, "a narrowing binding is allowed only in the condition of an 'if'")
            .with_code("E0265").label("'" + binding + "' cannot be bound here");
        if (keyword == "if") {
            error.note("a narrowing 'x is " + binding + ": T' must be the whole condition, or an operand of the "
                       "'&&' chain that is the whole condition, outside parentheses");
            error.help("move the narrowing to the top level of the condition, or test with 'is' here and "
                       "narrow in an 'if' inside");
        } else {
            error.note("the condition of '" + std::string(keyword) + "' takes no narrowing binding");
            error.help("test with 'is' in the condition and narrow with an 'if' inside the loop, as in "
                       "'if (x is " + binding + ": T) { ... }'");
        }
        return error;
    }

    // Each narrowing must be a conjunct root of the condition; `is not` only
    // the whole condition. Records the bound names on the statement.
    void check_narrowings(const Expr& condition, Statement& statement) const {
        std::vector<const Expr*> conjuncts;
        const Expr* chain = &condition;
        while (chain->kind == Expr::Kind::Binary && chain->text == "&&") {
            conjuncts.insert(conjuncts.begin(), &chain->operands[1]);
            chain = &chain->operands[0];
        }
        conjuncts.insert(conjuncts.begin(), chain);
        std::vector<const Expr*> allowed;
        for (const auto* conjunct : conjuncts) {
            if (conjunct->kind == Expr::Kind::TypeTest && conjunct->narrowing) {
                allowed.push_back(conjunct);
            } else if (conjunct->kind == Expr::Kind::Unary && conjunct->is_not &&
                       conjunct->operands.front().narrowing) {
                const auto& test = conjunct->operands.front();
                if (conjuncts.size() > 1)
                    throw Diagnostic(*conjunct->operator_span, "an 'is not' narrowing inside an '&&' chain")
                        .with_code("E0266").label("'" + test.binding + "' would be bound when the chain is false")
                        .note("when the chain is false, any of its conjuncts may be the false one, so nothing "
                              "proves that the type test failed")
                        .help("make 'x is not " + test.binding + ": T' the whole condition, or test with 'is' and "
                              "bind in the true branch");
                allowed.push_back(&test);
                statement.negated_narrowing = true;
            }
        }
        const std::function<void(const Expr&)> visit = [&](const Expr& expr) {
            if (expr.narrowing && std::ranges::find(allowed, &expr) == allowed.end())
                throw misplaced_narrowing(*expr.narrowing, expr.binding, "if");
            for (const auto& operand : expr.operands) visit(operand);
        };
        visit(condition);
        for (const auto* test : allowed)
            statement.bindings.push_back({TokenKind::Identifier, test->binding, *test->narrowing});
        statement.constant_bindings = !allowed.empty();
    }

    Statement statement() {
        const auto start = cursor_;
        Statement result{};
        if (at_startup_block()) {
            result = startup_block();
            if (block_depth_ > 0) record(misplaced_startup_block(result.keyword));
            return result;
        }
        if (at("#")) {
            result.kind = Statement::Kind::Binding;
            result.declaration = std::make_unique<Declaration>(tagged_declaration());
            result.span = result.declaration->span;
            return result;
        }

        if ((at("let") || at("const")) && tokens_[cursor_ + 1].text == "[") {
            result.kind = Statement::Kind::Destructure;
            result.documentation = current().documentation;
            result.constant_bindings = match("const");
            if (!result.constant_bindings) expect("let");
            expect("[");
            do {
                result.bindings.push_back(declared_name());
            } while (match(",") && !at("]"));
            expect("]");
            destructure_tail(result);
        } else if (at_last_destructuring()) {
            throw last_destructuring();
        } else if (at_declaration()) {
            result.kind = Statement::Kind::Binding;
            result.declaration = std::make_unique<Declaration>(declaration());
        } else if (at("{")) {
            result.kind = Statement::Kind::Block;
            result.body = block();
        } else if (match("if")) {
            result.kind = Statement::Kind::If;
            result.expressions.push_back(if_condition(result));
            result.body = block();
            if (match("else")) {
                if (at("if")) {
                    result.alternative.push_back(statement());
                } else {
                    result.alternative = block();
                }
            }
        } else if (match("match")) {
            result = parse_match(false, start);
        } else if (match("while")) {
            result.kind = Statement::Kind::While;
            result.expressions.push_back(condition("while"));
            result.body = block();
        } else if (match("do")) {
            result.kind = Statement::Kind::Do;
            result.body = block();
            if (match("while")) {
                result.expressions.push_back(condition("do"));
                expect(";");
            }
        } else if (match("for")) {
            result = for_statement();
        } else if (at_scope_statement()) {
            result.kind = Statement::Kind::Scope;
            result.keyword = advance().span;
            if (match("(")) {
                result.bindings.push_back(declared_name());
                expect(")");
            }
            result.body = block();
        } else if (at_defer_statement()) {
            result = defer_statement();
        } else if (at_using_statement()) {
            result = using_statement();
        } else if (at_contextual("yield") && in_generator_body()) {
            result = yield_statement();
        } else if (at_yield_outside_generator()) {
            throw yield_outside_generator();
        } else if (at_contextual("select") && next_is(1, "{")) {
            result.kind = Statement::Kind::Select;
            result.keyword = advance().span;
            expect("{");
            while (!at_end() && !at("}")) {
                result.select_arms.push_back(select_arm());
                if (!match(",")) break;
            }
            expect("}");
        } else {
            // `scope (a, b) {` and `scope () {` are not scope statements, so
            // they read as a call followed by a block. Say what a scope
            // takes instead of asking for a semicolon.
            const bool scope_call = at_contextual("scope") && next_is(1, "(");
            const auto parenthesis = scope_call ? tokens_[cursor_ + 1].span : Span{};
            result = simple_statement();
            if (scope_call && at("{") && result.kind == Statement::Kind::Expression &&
                result.expressions.front().kind == Expr::Kind::Call) {
                throw Diagnostic(parenthesis, "a scope statement names one scope or none: scope (name) { } or scope { }")
                    .with_code("E0211");
            }
            expect(";");
        }
        result.span = span_from(start);
        return result;
    }

    // After the names of a destructuring declaration (Types.md, Tuples):
    //   let [a, b] = tuple;
    //   let [a, b]: [A, B] = tuple;
    //   let [a, b] = x, y;            let [a, b]: [A, B] = x, y;
    // A comma after the first initializer makes a list with one
    // initializer per name. Lowering checks the counts.
    void destructure_tail(Statement& result) {
        if (match(":")) {
            result.binding_type = type();
            if (result.binding_type->name != "[tuple]" || !result.binding_type->collections.empty() ||
                result.binding_type->reference) {
                const auto names = result.bindings.size();
                throw Diagnostic(result.binding_type->span, "a destructuring annotation must be a tuple type")
                    .with_code("E0437")
                    .label("expected a tuple type with " + std::to_string(names) + " element" +
                           (names == 1 ? "" : "s"))
                    .help("write one type per name in brackets, as in 'let [a, b]: [int, string] = ...'");
            }
        }
        expect("=");
        do {
            result.expressions.push_back(expression());
        } while (match(","));
        expect(";");
    }

    // The contextual words of Concurrency.md section 4.4 stay identifiers.
    // Each form is recognized only where no expression can begin that way.
    bool at_contextual(std::string_view word) const {
        return current().kind == TokenKind::Identifier && current().text == word;
    }

    bool next_is_word(std::size_t offset, std::string_view word) const {
        if (cursor_ + offset >= tokens_.size()) return false;
        const auto& token = tokens_[cursor_ + offset];
        return token.kind == TokenKind::Identifier && token.text == word;
    }

    bool next_is(std::size_t offset, std::string_view symbol) const {
        if (cursor_ + offset >= tokens_.size()) return false;
        const auto& token = tokens_[cursor_ + offset];
        return token.kind == TokenKind::Symbol && token.text == symbol;
    }

#include "parser_lifetime.inc"

    bool at_scope_statement() const {
        if (!at_contextual("scope")) return false;
        if (next_is(1, "{")) return true;
        return next_is(1, "(") && cursor_ + 2 < tokens_.size() &&
               tokens_[cursor_ + 2].kind == TokenKind::Identifier && next_is(3, ")") && next_is(4, "{");
    }

    SelectArm select_arm() {
        const auto start = cursor_;
        SelectArm arm;
        if (current().kind == TokenKind::Identifier && current().text == "_" && next_is(1, "=>")) {
            advance();
        } else {
            if (at("let") || at("const")) {
                arm.constant_binding = advance().text == "const";
                arm.binding = declared_name();
                expect("=");
            }
            arm.operation = expression();
        }
        expect("=>");
        arm.body = block();
        arm.span = span_from(start);
        return arm;
    }

    Statement parse_match(bool produces_value, std::size_t start) {
        Statement result{};
        result.kind = Statement::Kind::Match;
        // `match.is (subject)` (docs/design/Matching030.md): `match`
        // directly followed by `.` and `is`.
        if (at(".") && next_is_keyword(1, "is")) {
            advance();
            advance();
            result.match_is = true;
            if (!at("("))
                throw Diagnostic(current().span, "malformed match.is: expected '(' before the subject")
                    .with_code("E0230").label(current().kind == TokenKind::End ? std::string("expected '('")
                                                                               : "expected '(', " + found_text(current()))
                    .help("write the subject in parentheses, as in 'match.is (value) { ... }'");
        }
        result.expressions.push_back(condition());
        expect("{");
        while (!at("}")) {
            result.arms.push_back(result.match_is ? is_arm(produces_value) : match_arm(produces_value));
            if (!match(",")) break;
        }
        expect("}");
        result.span = span_from(start);
        return result;
    }

    MatchArm match_arm(bool produces_value) {
        const auto start = cursor_;
        MatchArm result{};
        auto pattern = std::make_shared<PatternSyntax>(whole_pattern());
        if (match("if")) result.guard = expression();
        flatten_arm(result, *pattern);
        result.pattern = std::move(pattern);
        expect("=>");
        if (produces_value) {
            result.value = expression();
        } else {
            result.body = block();
        }
        result.span = span_from(start);
        return result;
    }

    // An arm of a `match.is`: `_`, `nil`, `name: Type` or a type, with an
    // optional guard. An identifier directly followed by ':' is a binding;
    // anything else is read by the type parser.
    MatchArm is_arm(bool produces_value) {
        const auto start = cursor_;
        MatchArm result{};
        auto pattern = std::make_shared<PatternSyntax>();
        if (current().kind == TokenKind::Identifier && current().text == "_") {
            pattern->span = advance().span;
        } else if (at("nil")) {
            pattern->kind = PatternSyntax::Kind::Nil;
            pattern->span = advance().span;
        } else {
            if (current().kind == TokenKind::Identifier && next_is(1, ":")) {
                const auto& name = declared_name();
                advance();
                pattern->kind = PatternSyntax::Kind::Binding;
                pattern->name = name.text;
                pattern->name_span = name.span;
                result.bindings.push_back(name);
            } else {
                pattern->kind = PatternSyntax::Kind::Type;
                if (current().kind != TokenKind::Identifier && !at("[") && !at("&") && !at("func") && !at("class"))
                    throw Diagnostic(current().span, "malformed match.is pattern").with_code("E0230")
                        .label("expected a type, 'name: Type', 'nil' or '_'")
                        .help("a 'match.is' arm tests the type of the subject's value");
            }
            pattern->type = type();
            pattern->span = span_from(start);
            if (at("(") || at("{"))
                throw Diagnostic(current().span, "a variant pattern is not a 'match.is' pattern").with_code("E0230")
                    .label("'match.is' arms test types")
                    .help("use 'match' for an enum");
        }
        if (match("if")) result.guard = expression();
        result.general = true;
        result.variant = pattern->kind == PatternSyntax::Kind::Wildcard ? "_" : "";
        result.pattern = std::move(pattern);
        expect("=>");
        if (produces_value) {
            result.value = expression();
        } else {
            result.body = block();
        }
        result.span = span_from(start);
        return result;
    }

    bool next_is_keyword(std::size_t offset, std::string_view word) const {
        if (cursor_ + offset >= tokens_.size()) return false;
        const auto& token = tokens_[cursor_ + offset];
        return token.kind == TokenKind::Keyword && token.text == word;
    }

    // The whole pattern of a `match` arm (docs/design/Matching030.md): `_`,
    // a tuple pattern, or a name read as a variant, with or without a
    // payload. A single name stays a variant here, as in 0.2.
    PatternSyntax whole_pattern() {
        if (at("[")) return tuple_pattern();
        if (current().kind == TokenKind::Identifier && current().text == "_") {
            PatternSyntax result{};
            result.span = advance().span;
            return result;
        }
        if (current().kind != TokenKind::Identifier) unsupported_pattern();
        return variant_pattern();
    }

    // A payload or element position: `_`, a binding, a variant pattern
    // (a dotted name), or a tuple pattern.
    PatternSyntax sub_pattern(bool positional) {
        if (at("[")) return tuple_pattern();
        if (current().kind != TokenKind::Identifier) unsupported_pattern();
        if (current().text == "_") {
            PatternSyntax result{};
            result.span = advance().span;
            return result;
        }
        if (next_is(1, ".")) return variant_pattern();
        if (positional && next_is(1, ":")) {
            // `name: Type` is a type pattern, which only a `match.is` arm has.
            const auto start = cursor_;
            advance();
            advance();
            type();
            throw Diagnostic(span_from(start), "a type pattern is not supported inside a match pattern")
                .with_code("E0231").label("type patterns are whole patterns of a 'match.is' arm")
                .help("bind the value and test it in a guard");
        }
        PatternSyntax result{};
        result.kind = PatternSyntax::Kind::Binding;
        const auto& name = declared_name();
        result.span = name.span;
        result.name_span = name.span;
        result.name = name.text;
        return result;
    }

    PatternSyntax variant_pattern() {
        const auto start = cursor_;
        PatternSyntax result{};
        result.kind = PatternSyntax::Kind::Variant;
        result.name = qualified_name();
        const bool positional = match("(");
        result.named = !positional && match("{");
        result.payload = positional || result.named;
        if (result.payload) {
            const auto close = positional ? ")" : "}";
            while (!at(close)) {
                if (result.named) {
                    const auto& field = identifier();
                    result.fields.push_back(field.text);
                    result.field_spans.push_back(field.span);
                    if (match(":")) {
                        result.elements.push_back(sub_pattern(false));
                    } else {
                        // `{ label }` binds the field to its own name.
                        PatternSyntax binding{};
                        binding.kind = PatternSyntax::Kind::Binding;
                        binding.span = field.span;
                        binding.name_span = field.span;
                        binding.name = field.text;
                        if (field.text == "_") binding.kind = PatternSyntax::Kind::Wildcard;
                        result.elements.push_back(std::move(binding));
                    }
                } else {
                    result.elements.push_back(sub_pattern(true));
                }
                if (!match(",")) break;
            }
            expect(close);
        }
        result.span = span_from(start);
        return result;
    }

    PatternSyntax tuple_pattern() {
        const auto start = cursor_;
        expect("[");
        PatternSyntax result{};
        result.kind = PatternSyntax::Kind::Tuple;
        while (!at("]")) {
            result.elements.push_back(sub_pattern(true));
            if (!match(",")) break;
        }
        expect("]");
        result.span = span_from(start);
        return result;
    }

    // A literal, a range, a rest element or another form that is not a
    // pattern of 0.3.0 (E0231). The form is read as an expression, or as
    // `...name`, so that the error covers all of it.
    [[noreturn]] void unsupported_pattern() {
        const auto start = cursor_;
        const auto& token = current();
        const bool literal = token.kind == TokenKind::Integer || token.kind == TokenKind::Float ||
                             token.kind == TokenKind::String || at("true") || at("false") || at("nil") ||
                             at("-");
        if (match("...")) {
            if (current().kind == TokenKind::Identifier) advance();
            throw Diagnostic(span_from(start), "a rest pattern is not supported").with_code("E0231")
                .label("rest and list patterns are not part of 0.3.0")
                .help("match a tuple with one sub-pattern per element");
        }
        if (!literal) expected("a pattern");
        const auto form = expression();
        const bool range = form.kind == Expr::Kind::Range;
        throw Diagnostic(form.span, range ? "a range pattern is not supported" : "a literal pattern is not supported")
            .with_code("E0231").label(range ? "range patterns are not part of 0.3.0" : "literal patterns are not part of 0.3.0")
            .help("bind the value and test it in a guard");
    }

    // Keeps an arm in the form of 0.2 in that form as well (syntax.hpp,
    // MatchArm), and lists every name a general arm binds.
    static void flatten_arm(MatchArm& arm, const PatternSyntax& pattern) {
        using Kind = PatternSyntax::Kind;
        const auto simple_element = [](const PatternSyntax& element) {
            return element.kind == Kind::Wildcard || element.kind == Kind::Binding;
        };
        arm.general = arm.guard.has_value() || pattern.kind == Kind::Tuple ||
                      !std::ranges::all_of(pattern.elements, simple_element);
        arm.variant = pattern.kind == Kind::Variant ? pattern.name : pattern.kind == Kind::Wildcard ? "_" : "";
        if (!arm.general) {
            arm.named = pattern.named;
            arm.fields = pattern.fields;
            for (const auto& element : pattern.elements)
                arm.bindings.push_back(Token{TokenKind::Identifier, element.kind == Kind::Wildcard ? "_" : element.name,
                                             element.span});
            return;
        }
        const std::function<void(const PatternSyntax&)> collect = [&](const PatternSyntax& item) {
            if (item.kind == Kind::Binding)
                arm.bindings.push_back(Token{TokenKind::Identifier, item.name, item.name_span});
            for (const auto& element : item.elements) collect(element);
        };
        collect(pattern);
    }

    // After `for`. A header of the form `(let name [: Type] in collection)`
    // is a for-each loop; anything else is a three-part loop.
    Statement for_statement() {
        Statement result{};
        expect("(");
        if (at("let") || at("const")) {
            const auto start = cursor_;
            const bool constant = at("const");
            advance();
            if (current().kind == TokenKind::Identifier) {
                auto declaration = std::make_unique<Declaration>();
                declaration->kind = Declaration::Kind::Binding;
                declaration->constant = constant;
                declaration->documentation = tokens_[start].documentation;
                const auto& name = declared_name();
                declaration->name = name.text;
                declaration->name_span = name.span;
                if (match(":")) {
                    declaration->type = type();
                }
                if (match("in")) {
                    declaration->span = span_from(start);
                    result.kind = Statement::Kind::ForEach;
                    result.declaration = std::move(declaration);
                    result.expressions.push_back(expression());
                    expect(")");
                    result.body = block();
                    return result;
                }
            }
            cursor_ = start;
        }
        result.kind = Statement::Kind::For;
        if (at_declaration()) {
            const auto start = cursor_;
            Statement binding{};
            binding.kind = Statement::Kind::Binding;
            binding.declaration = std::make_unique<Declaration>(declaration());
            binding.span = span_from(start);
            result.initializer.push_back(std::move(binding));
        } else if (!match(";")) {
            const auto start = cursor_;
            auto init = simple_statement();
            init.span = span_from(start);
            expect(";");
            result.initializer.push_back(std::move(init));
        }
        if (!at(";")) {
            try {
                result.expressions.push_back(expression());
                expect(";");
            } catch (const Diagnostic&) {
                misplaced_narrowing("for");
            }
        } else {
            expect(";");
        }
        if (!at(")")) {
            const auto start = cursor_;
            auto step = simple_statement();
            step.span = span_from(start);
            result.step.push_back(std::move(step));
        }
        expect(")");
        result.body = block();
        return result;
    }

    // docs/design/Expressions030.md, `..=` as a statement: a statement whose
    // whole expression is `place ..= right` stores `place .. right` into the
    // place. An unparenthesized chain on the right, `s ..= a .. b;`, stays
    // one chain whose operands follow the old value. The operator is kept in
    // `keyword`; lowering checks the destination.
    bool concatenation_statement(Statement& result) {
        auto& top = result.expressions.front();
        if (top.kind != Expr::Kind::Range || top.grouped || top.braced || top.range_inclusive.empty() ||
            !top.range_inclusive.front())
            return false;
        const auto operator_span = top.range_operators.front();
        if (top.step_colon)
            throw Diagnostic(operator_span, "a '..=' statement takes no step").with_code("E0262")
                .label("'..=' as a statement").label(*top.step_colon, "a step is written here")
                .note("as a statement, 'place ..= text;' appends text to a string; "
                      "in an expression, 'start ..= stop : step' is an inclusive range")
                .help("to make a stepped range, use it as a value: 'let r = start ..= stop : step;'");
        if (const auto* access = optional_access(top.operands.front()))
            throw Diagnostic(*access->operator_span, "malformed optional access: '?.' is not an assignment target")
                .with_code("E0233").label("an optional access gives a value, not a place")
                .help("test the receiver for nil and assign through '.'");
        auto place = std::move(top.operands.front());
        Expr right = std::move(top.operands[1]);
        if (top.operands.size() > 2) {
            auto chain = make_expression(Expr::Kind::Range, right.span);
            chain.range_operators.assign(top.range_operators.begin() + 1, top.range_operators.end());
            chain.range_inclusive.assign(top.range_inclusive.begin() + 1, top.range_inclusive.end());
            const auto& last = top.operands.back().span;
            chain.span.length = last.offset + last.length - chain.span.offset;
            chain.operands.push_back(std::move(right));
            for (std::size_t i = 2; i < top.operands.size(); ++i) chain.operands.push_back(std::move(top.operands[i]));
            right = std::move(chain);
        }
        result.kind = Statement::Kind::Assignment;
        result.assignment_operator = "..=";
        result.keyword = operator_span;
        result.expressions.clear();
        result.expressions.push_back(std::move(place));
        result.expressions.push_back(std::move(right));
        return true;
    }

    // docs/design/Expressions030.md, Return forms. `return if (c1) -> v1;
    // else if (c2) -> v2; else -> v3` is the statement `if (c1) { return v1; }
    // else if (c2) { return v2; } else { return v3; }`; the `;` after a
    // non-final arm is optional. The current token is `if`.
    Statement return_if(std::size_t start) {
        const auto malformed = [&](const std::string& message, const std::string& label) {
            return Diagnostic(current().span, message).with_code("E0264").label(label)
                .help("write each arm as '-> value', as in 'return if (c) -> a; else -> b;'");
        };
        const auto arm_value = [&] {
            if (at("{"))
                throw malformed("a block arm of 'return if' is not available yet", "expected '->' and a value")
                    .note("arms written as blocks that yield a value need the lifetime milestone's 'yield'; "
                          "they are specified and pending");
            if (!at("->")) throw malformed("expected '->' before the value of a 'return if' arm", "expected '->'");
            advance();
            Statement value{};
            value.kind = Statement::Kind::Return;
            value.expressions.push_back(expression());
            value.span = span_from(start);
            return value;
        };
        advance();
        Statement arm{};
        arm.kind = Statement::Kind::If;
        arm.expressions.push_back(condition("if"));
        arm.body.push_back(arm_value());
        match(";");
        if (!at("else"))
            throw malformed("a 'return if' needs a final 'else'", "expected 'else -> value'");
        advance();
        if (at("if") && next_is(1, "(")) arm.alternative.push_back(return_if(start));
        else arm.alternative.push_back(arm_value());
        arm.span = span_from(start);
        return arm;
    }

    Statement simple_statement() {
        Statement result{};
        if (match("return")) {
            const auto start = cursor_ - 1;
            result.kind = Statement::Kind::Return;
            if (at("if") && next_is(1, "(")) {
                result = return_if(start);
            } else if (!at(";")) {
                result.expressions.push_back(expression());
            }
            // docs/design/Expressions030.md, Return forms: `return v unless g;`
            // is `if (!(g)) { return v; }`. `unless` is a guard only after a
            // complete return value; elsewhere it is an ordinary name.
            if ((result.kind != Statement::Kind::Return || !result.expressions.empty()) && at_contextual("unless")) {
                result.span = span_from(start);
                const auto guard_word = advance().span;
                auto guard = expression();
                auto negated = make_expression(Expr::Kind::Unary, guard.span, "!");
                negated.implicit = true;
                negated.operands.push_back(std::move(guard));
                Statement guarded{};
                guarded.kind = Statement::Kind::If;
                guarded.keyword = guard_word;
                guarded.expressions.push_back(std::move(negated));
                guarded.body.push_back(std::move(result));
                result = std::move(guarded);
            }
        } else if (match("break")) {
            result.kind = Statement::Kind::Break;
        } else if (match("continue")) {
            result.kind = Statement::Kind::Continue;
        } else if (match("destroy")) {
            result.kind = Statement::Kind::Destroy;
            result.expressions.push_back(expression());
        } else {
            result.kind = Statement::Kind::Expression;
            result.expressions.push_back(expression());
            if (concatenation_statement(result)) return result;
            if (at("=") || at("+=") || at("-=") || at("*=") || at("/=") || at("%=") ||
                at("&=") || at("^=") || at("|=") || at("<<=") || at(">>=")) {
                if (const auto* access = optional_access(result.expressions.front()))
                    throw Diagnostic(*access->operator_span, "malformed optional access: '?.' is not an assignment target")
                        .with_code("E0233").label("an optional access gives a value, not a place")
                        .help("test the receiver for nil and assign through '.'");
                result.kind = Statement::Kind::Assignment;
                result.assignment_operator = advance().text;
                result.expressions.push_back(expression());
            }
        }
        return result;
    }

    // Angle brackets remain individual tokens for nested type arguments.
    bool at_shift() const {
        if (!at("<") && !at(">")) {
            return false;
        }
        const auto& next = tokens_[cursor_ + 1];
        return next.text == current().text &&
               next.span.offset == current().span.offset + current().span.length;
    }

    static constexpr int equality_precedence = 3;
    static constexpr int ordering_precedence = 4;

    int precedence() const {
        if (at("||")) return 1;
        if (at("&&")) return 2;
        if (at("==") || at("!=") || at("===") || at("!==")) return equality_precedence;
        if (at_shift()) return 8;
        if (at("<") || at("<=") || at(">") || at(">=")) return ordering_precedence;
        if (at("|")) return 5;
        if (at("^")) return 6;
        if (at("&")) return 7;
        if (at("+") || at("-")) return 9;
        if (at("*") || at("/") || at("%")) return 10;
        return 0;
    }

    Expr expression(int minimum_precedence = 1, bool range_step = true, std::string_view branch_keyword = {}) {
        const auto start = cursor_;
        return expression_tail(start, cast(branch_keyword), minimum_precedence, range_step, branch_keyword);
    }

    Expr expression_tail(std::size_t start, Expr left, int minimum_precedence, bool range_step = true,
                         std::string_view branch_keyword = {}, bool binary_only = false) {
        while (precedence() >= minimum_precedence) {
            const auto priority = precedence();
            const bool shift = at_shift();
            auto op = advance().text;
            if (shift) {
                op += advance().text;
            }
            // Each top-level conjunct of an `if` condition may narrow.
            if (op == "&&" && start == narrowing_root_ && minimum_precedence == 1) narrowing_next_ = true;
            auto right = expression(priority + 1, true, branch_keyword);
            // `===` and `!==` form no comparison chain (docs/design/Expressions030.md).
            if (op != "===" && op != "!==" && (priority == equality_precedence || priority == ordering_precedence) &&
                at_comparison_connector(branch_keyword)) {
                left = comparison_chain(start, std::move(op), std::move(left), std::move(right), branch_keyword);
                continue;
            }
            auto binary = make_expression(Expr::Kind::Binary, span_from(start), op);
            binary.operands.push_back(std::move(left));
            binary.operands.push_back(std::move(right));
            left = std::move(binary);
        }
        if (binary_only) return left;
        if (minimum_precedence == 1 && at("??")) left = coalesce(start, std::move(left), branch_keyword);
        if (minimum_precedence == 1 && (at("..") || at("..="))) {
            // One node for `a .. b`, `a ..= b` and the chain `a .. b .. c`
            // (docs/design/Expressions030.md); lowering checks the shape.
            auto range = make_expression(Expr::Kind::Range, left.span);
            range.operands.push_back(std::move(left));
            while (at("..") || at("..=")) {
                range.range_inclusive.push_back(current().text == "..=");
                range.range_operators.push_back(advance().span);
                range.operands.push_back(range_bound(branch_keyword));
            }
            if (range_step && at(":")) {
                range.step_colon = advance().span;
                range.operands.push_back(range_bound(branch_keyword));
            }
            range.span = span_from(start);
            left = std::move(range);
        }
        if (minimum_precedence == 1 && at("?")) {
            const auto after = tokens_[cursor_ + 1];
            advance();
            auto when_true = expression(1, false);
            if (!at(":")) {
                try {
                    expect(":");
                } catch (Diagnostic& error) {
                    // `r? - 1` is a conditional (docs/design/Matching030.md).
                    if ((after.kind == TokenKind::Symbol &&
                         (after.text == "-" || after.text == "(" || after.text == "[" || after.text == "{" ||
                          after.text == "++" || after.text == "--" || after.text == "!" || after.text == "~")) ||
                        (after.kind == TokenKind::Keyword && after.text == "not")) {
                        const auto operand = source_text(left.span);
                        const auto rest = source_text(when_true.span);
                        error.help("to return an error here, write '(" + operand.value_or("r") + "?) " +
                                   rest.value_or("") + "'");
                    }
                    throw;
                }
            }
            expect(":");
            auto when_false = expression(1, range_step, branch_keyword);
            auto conditional = make_expression(Expr::Kind::Conditional, span_from(start));
            conditional.operands.push_back(std::move(left));
            conditional.operands.push_back(std::move(when_true));
            conditional.operands.push_back(std::move(when_false));
            return conditional;
        }
        return left;
    }

    // A range's end or step: `??` is tighter than `..`, so `a .. b ?? c`
    // ends at `b ?? c`.
    Expr range_bound(std::string_view branch_keyword) {
        const auto start = cursor_;
        auto bound = expression(2, true, branch_keyword);
        if (at("??")) bound = coalesce(start, std::move(bound), branch_keyword);
        return bound;
    }

    // `left ?? right` (docs/design/Matching030.md): looser than '||',
    // tighter than '..' and '? :', and right-associative.
    Expr coalesce(std::size_t start, Expr left, std::string_view branch_keyword) {
        const auto token = advance().span;
        const auto right_start = cursor_;
        auto right = expression_tail(right_start, cast(branch_keyword), 1, true, branch_keyword, true);
        if (at("??")) right = coalesce(right_start, std::move(right), branch_keyword);
        auto result = make_expression(Expr::Kind::Coalesce, span_from(start), "??");
        result.operator_span = token;
        result.operands.push_back(std::move(left));
        result.operands.push_back(std::move(right));
        return result;
    }

    bool at_comparison_connector(std::string_view branch_keyword) const {
        return at("and") || (at("or") && !at_branch(branch_keyword));
    }

    Expr comparison_chain(std::size_t start, std::string op, Expr left, Expr first,
                          std::string_view branch_keyword) {
        const auto connector = current().text;
        auto result = make_expression(Expr::Kind::ComparisonChain, left.span, std::move(op));
        result.chain_conjunction = connector == "and";
        result.operands.push_back(std::move(left));
        result.operands.push_back(std::move(first));
        do {
            advance();
            result.operands.push_back(expression(ordering_precedence + 1, true, branch_keyword));
            if (precedence() == equality_precedence || precedence() == ordering_precedence) {
                throw Diagnostic(current().span, "a comparison chain expects another value; use '&&' or '||' between comparisons")
                    .with_code("E0210");
            }
        } while (at(connector) && at_comparison_connector(branch_keyword));
        if (at_comparison_connector(branch_keyword)) {
            throw Diagnostic(current().span, "a comparison chain must use only 'and' or only 'or'").with_code("E0210");
        }
        result.span = span_from(start);
        return result;
    }

    // `as` binds tighter than the binary operators and looser than unary
    // operators: `-x as int64` converts `-x`, and `a * b as int64` converts
    // only `b`.
    Expr cast(std::string_view branch_keyword) {
        const auto start = cursor_;
        const bool narrowing = std::exchange(narrowing_next_, false);
        return cast_tail(start, unary(branch_keyword), narrowing);
    }

    // `is name :` or `is not name :` after a narrowing subject.
    bool at_narrowing() const {
        if (!at("is") || cursor_ + 3 >= tokens_.size()) return false;
        if (tokens_[cursor_ + 1].kind == TokenKind::Identifier) return next_is(2, ":");
        return next_is_keyword(1, "not") && tokens_[cursor_ + 2].kind == TokenKind::Identifier && next_is(3, ":");
    }

    Expr cast_tail(std::size_t start, Expr result, bool narrowing = false) {
        if (narrowing && at_narrowing()) {
            // `x is y: T` (docs/design/Expressions030.md, Narrowing).
            advance();
            std::optional<Span> negation;
            if (at("not")) negation = advance().span;
            const auto name = advance();
            advance();
            auto target = type();
            auto test = make_expression(Expr::Kind::TypeTest, span_from(start));
            test.type = std::move(target);
            test.binding = name.text;
            test.narrowing = name.span;
            test.operands.push_back(std::move(result));
            result = std::move(test);
            if (negation) {
                auto negated = make_expression(Expr::Kind::Unary, span_from(start), "!");
                negated.spelled_not = negated.is_not = true;
                negated.operator_span = negation;
                negated.operands.push_back(std::move(result));
                result = std::move(negated);
            }
        }
        while (at("as") || at("is")) {
            const bool test = advance().text == "is";
            // `x is not T` is `!(x is T)` (docs/design/Expressions030.md).
            std::optional<Span> negation;
            if (test && at("not")) negation = advance().span;
            auto target = type();
            auto conversion = make_expression(test ? Expr::Kind::TypeTest : Expr::Kind::Cast, span_from(start));
            conversion.type = std::move(target);
            conversion.operands.push_back(std::move(result));
            result = std::move(conversion);
            if (negation) {
                auto negated = make_expression(Expr::Kind::Unary, span_from(start), "!");
                negated.spelled_not = negated.is_not = true;
                negated.operator_span = negation;
                negated.operands.push_back(std::move(result));
                result = std::move(negated);
            }
        }
        return result;
    }

    Expr unary(std::string_view branch_keyword) {
        const auto start = cursor_;
        if (at("++") || at("--")) {
            const auto op = advance().text;
            auto operand = unary(branch_keyword);
            if (const auto* access = optional_access(operand))
                throw Diagnostic(*access->operator_span, "malformed optional access: '?.' takes no '" + op + "'")
                    .with_code("E0233").label("an optional access gives a value, not a place");
            auto result = make_expression(Expr::Kind::PrefixUpdate, span_from(start), op);
            result.operands.push_back(std::move(operand));
            return result;
        }
        if (at("!") || at("not") || at("-") || at("~")) {
            // `not` is `!` spelled as a word (docs/design/Expressions030.md).
            const auto& token = advance();
            const bool word = token.text == "not";
            const auto operator_span = token.span;
            auto operand = unary(branch_keyword);
            auto result = make_expression(Expr::Kind::Unary, span_from(start), word ? std::string("!") : token.text);
            if (word) {
                result.spelled_not = true;
                result.operator_span = operator_span;
            }
            result.operands.push_back(std::move(operand));
            return result;
        }
        auto operand = postfix(branch_keyword);
        if (at("++") || at("--")) {
            if (const auto* access = optional_access(operand))
                throw Diagnostic(*access->operator_span, "malformed optional access: '?.' takes no '" + current().text + "'")
                    .with_code("E0233").label("an optional access gives a value, not a place");
            const auto op = advance().text;
            auto result = make_expression(Expr::Kind::PostfixUpdate, span_from(start), op);
            result.operands.push_back(std::move(operand));
            return result;
        }
        return operand;
    }

    // The optional access that `expr` ends in, `a?.b`, if any.
    static const Expr* optional_access(const Expr& expr) {
        return expr.kind == Expr::Kind::OptionalMember ? &expr : nullptr;
    }

    std::vector<Expr> expression_list(std::string_view close) {
        std::vector<Expr> result;
        if (!at(close)) {
            do {
                result.push_back(expression());
            } while (match(","));
        }
        expect(close);
        return result;
    }

    // The arguments of a call, a `new` or a `super.init`, after the '(':
    // each one is an expression, or a label and an expression when an
    // identifier is directly followed by ':' (docs/design/Calls030.md). No
    // expression starts that way, so an unlabelled call reads as before.
    // `labels` and `label_spans` are left empty when no argument is named.
    std::vector<Expr> argument_list(std::vector<std::string>& labels, std::vector<Span>& label_spans) {
        std::vector<Expr> result;
        std::vector<std::string> names;
        std::vector<Span> spans;
        if (!at(")")) {
            do {
                std::string name;
                Span span{};
                if (current().kind == TokenKind::Identifier && next_is(1, ":")) {
                    span = current().span;
                    name = advance().text;
                    advance();
                }
                names.push_back(std::move(name));
                spans.push_back(span);
                result.push_back(expression());
            } while (match(","));
        }
        expect(")");
        if (std::ranges::any_of(names, [](const std::string& name) { return !name.empty(); })) {
            labels = std::move(names);
            label_spans = std::move(spans);
        }
        return result;
    }

    // Only unambiguous postfix or terminating tokens commit a type argument list.
    std::vector<TypeSyntax> postfix_type_arguments() {
        const auto start = cursor_;
        advance();
        SpeculativeDiagnostics speculative;
        try {
            auto result = type_arguments();
            if (at("(") || at(".") || at(";")) {
                speculative.commit();
                return result;
            }
        } catch (const Diagnostic&) {
            // The angle bracket can instead be an ordering operator.
        }
        cursor_ = start;
        return {};
    }

    Expr postfix(std::string_view branch_keyword) {
        const auto start = cursor_;
        return propagation(start, postfix_tail(start, primary(branch_keyword)));
    }

    // A postfix `?` (docs/design/Matching030.md) is decided by the next
    // token alone: one that can begin an expression makes the `?` the
    // conditional operator's, `.` is an error, and anything else makes it
    // propagation.
    Expr propagation(std::size_t start, Expr result) {
        while (at("?")) {
            const auto& next = tokens_[cursor_ + 1];
            if (begins_expression(next)) break;
            if (next.kind == TokenKind::Symbol && next.text == ".")
                throw Diagnostic(current().span, "'?' is followed by '.'").with_code("E0232")
                    .label("a postfix '?' cannot take a member")
                    .help("to read a member of the success value, write '(r?).name'; for optional access, write 'r?.name'");
            const auto token = advance().span;
            auto propagate = make_expression(Expr::Kind::Propagate, span_from(start), "?");
            propagate.operator_span = token;
            propagate.operands.push_back(std::move(result));
            result = std::move(propagate);
        }
        return result;
    }

    static bool begins_expression(const Token& token) {
        switch (token.kind) {
        case TokenKind::Identifier:
        case TokenKind::Integer:
        case TokenKind::Float:
        case TokenKind::String:
            return true;
        case TokenKind::Keyword:
            return token.text == "new" || token.text == "func" || token.text == "match" || token.text == "import" ||
                   token.text == "true" || token.text == "false" || token.text == "nil" || token.text == "not";
        case TokenKind::Symbol:
            return token.text == "(" || token.text == "[" || token.text == "{" || token.text == "-" ||
                   token.text == "!" || token.text == "~" || token.text == "++" || token.text == "--";
        default:
            return false;
        }
    }

    Expr postfix_tail(std::size_t start, Expr result) {
        while (true) {
            if (at("?.")) {
                // `receiver?.member` (docs/design/Matching030.md).
                const auto token = advance().span;
                if (current().kind != TokenKind::Identifier)
                    throw Diagnostic(token, "malformed optional access: '?.' must be followed by a member name")
                        .with_code("E0233").label(current().kind == TokenKind::End
                                                      ? std::string("expected a member name before the end of the file")
                                                      : "expected a member name, " + found_text(current()));
                const auto name = advance().text;
                auto member = make_expression(Expr::Kind::OptionalMember, span_from(start), name);
                member.operator_span = token;
                member.operands.push_back(std::move(result));
                result = std::move(member);
                continue;
            }
            if (match(".")) {
                const auto name = identifier().text;
                auto member = make_expression(Expr::Kind::Member, span_from(start), name);
                member.operands.push_back(std::move(result));
                result = std::move(member);
                continue;
            }
            if (match("[")) {
                auto index = expression();
                expect("]");
                auto access = make_expression(Expr::Kind::Index, span_from(start));
                access.operands.push_back(std::move(result));
                access.operands.push_back(std::move(index));
                result = std::move(access);
                continue;
            }
            if (result.kind == Expr::Kind::Member && match("{")) {
                auto construct = make_expression(Expr::Kind::NamedVariant, result.span);
                construct.operands.push_back(std::move(result));
                while (!at("}")) {
                    const auto& field = identifier();
                    construct.field_names.push_back(field.text);
                    construct.field_spans.push_back(field.span);
                    expect(":");
                    construct.operands.push_back(expression());
                    if (!match(",")) break;
                }
                expect("}");
                construct.span = span_from(start);
                result = std::move(construct);
                continue;
            }
            std::vector<TypeSyntax> arguments;
            if (at("<") && !at_shift() && (result.kind == Expr::Kind::Name || result.kind == Expr::Kind::Member)) {
                arguments = postfix_type_arguments();
                if (!arguments.empty() && !at("(")) {
                    auto application = make_expression(Expr::Kind::TypeApplication, span_from(start));
                    application.operands.push_back(std::move(result));
                    application.type_arguments = std::move(arguments);
                    result = std::move(application);
                    continue;
                }
            }
            if (!at("(")) {
                return result;
            }
            const auto open = advance().span;
            std::vector<std::string> labels;
            std::vector<Span> label_spans;
            auto operands = argument_list(labels, label_spans);
            auto call = make_expression(Expr::Kind::Call, span_from(start));
            call.open = open;
            call.type_arguments = std::move(arguments);
            if (!labels.empty()) {
                // The callee is operand 0 and carries no label.
                call.labels.push_back({});
                call.label_spans.push_back({});
                call.labels.insert(call.labels.end(), labels.begin(), labels.end());
                call.label_spans.insert(call.label_spans.end(), label_spans.begin(), label_spans.end());
            }
            call.operands.reserve(1 + operands.size());
            call.operands.push_back(std::move(result));
            for (auto& operand : operands) {
                call.operands.push_back(std::move(operand));
            }
            result = std::move(call);
        }
    }

    bool contextual_at(std::size_t offset, std::string_view word) const {
        return cursor_ + offset < tokens_.size() && tokens_[cursor_ + offset].kind == TokenKind::Identifier &&
               tokens_[cursor_ + offset].text == word;
    }

    bool kind_at(std::size_t offset, TokenKind kind) const {
        return cursor_ + offset < tokens_.size() && tokens_[cursor_ + offset].kind == kind;
    }

    // `html(x)` remains a call. A top-level parameter annotation cannot
    // begin an argument in the ordinary call grammar on this compiler line.
    bool at_html_factory() const {
        if (!at_contextual("html")) return false;
        if (kind_at(1, TokenKind::HtmlIsland)) return true;
        auto index = std::size_t{1};
        if (next_is(index, "<")) {
            std::size_t angles = 1;
            for (++index; cursor_ + index < tokens_.size() && angles > 0; ++index) {
                if (next_is(index, "<")) ++angles;
                else if (next_is(index, ">")) --angles;
                else if (next_is(index, ";")) return false;
            }
        }
        if (!next_is(index, "(")) return false;
        bool annotated = false;
        bool conditional = false;
        std::size_t depth = 1;
        for (++index; cursor_ + index < tokens_.size(); ++index) {
            if (next_is(index, "(") || next_is(index, "[") || next_is(index, "{")) ++depth;
            else if (next_is(index, ")") || next_is(index, "]") || next_is(index, "}")) {
                if (depth == 1 && !next_is(index, ")")) return false;
                if (--depth == 0) {
                    return annotated || kind_at(index + 1, TokenKind::HtmlIsland) ||
                           contextual_at(index + 1, "from") || next_is(index + 1, "->");
                }
            } else if (depth == 1 && next_is(index, "?")) conditional = true;
            else if (depth == 1 && next_is(index, ",")) conditional = false;
            else if (depth == 1 && next_is(index, ":") && !conditional && index > 1 &&
                     kind_at(index - 1, TokenKind::Identifier)) annotated = true;
            else if (depth == 1 && next_is(index, ";")) return false;
        }
        return false;
    }

    bool at_stylesheet() const {
        if (!at_contextual("css")) return false;
        if (kind_at(1, TokenKind::CssIsland) || contextual_at(1, "from")) return true;
        if (!next_is(1, ".")) return false;
        return next_keyword(2, "module") || kind_at(3, TokenKind::CssIsland) || contextual_at(3, "from");
    }

    Span through(std::size_t first, const Token& last) const {
        auto span = tokens_[first].span;
        span.length = last.span.offset + last.span.length - span.offset;
        return span;
    }

    Diagnostic malformed_factory(std::size_t first, std::string reason) const {
        return Diagnostic(through(first, current()), "malformed html factory").with_code("E0240")
            .label(std::move(reason)).help("write 'html(parameters) { ... }' or 'html(parameters) from \"path.html\"'");
    }

    Token form_path() {
        advance(); // from
        if (current().kind != TokenKind::String || current().text.find('\\') != std::string::npos)
            throw Diagnostic(current().span, "malformed from clause").with_code("E0254")
                .label("expected a literal path without escapes");
        auto path = advance();
        if (precedence() != 0)
            throw Diagnostic(through(cursor_ - 1, current()), "malformed from clause").with_code("E0254")
                .label("write the path as one string literal");
        return path;
    }

    Expr html_factory(std::size_t start, bool retained) {
        const auto word = cursor_;
        advance(); // html
        if (at("<")) throw malformed_factory(word, "an HTML factory has no type parameters");
        if (!at("(")) throw malformed_factory(word, "the parameter list is required");
        auto params = parameters(nullptr, true);
        if (at("->")) throw malformed_factory(word, "an HTML factory has no result annotation");
        std::variant<TemplateSyntax, Token> source;
        if (at_contextual("from")) {
            source = form_path();
        } else if (current().kind == TokenKind::HtmlIsland) {
            const auto& body = advance();
            auto contents = body.span;
            ++contents.column;
            ++contents.offset;
            contents.length = body.text.size();
            source = parse_template(body.text, std::move(contents));
        } else {
            throw malformed_factory(word, "expected a body or 'from' after the parameter list");
        }
        auto result = make_expression(Expr::Kind::HtmlFactory, span_from(start));
        result.html_factory = std::make_shared<HtmlFactorySyntax>(HtmlFactorySyntax{
            retained, std::move(params), std::move(source)});
        return result;
    }

    Diagnostic malformed_stylesheet(std::size_t start, std::string reason) const {
        return Diagnostic(through(start, current()), "malformed css declaration").with_code("E0252")
            .label(std::move(reason)).help("write 'css { ... }' or 'css.module { ... }'");
    }

    Expr stylesheet(std::size_t start, bool validate_body = true) {
        advance(); // css
        const bool module = match(".");
        if (module && !match("module")) throw malformed_stylesheet(start, "only '.module' can follow 'css'");
        if (current().kind != TokenKind::CssIsland && !at_contextual("from"))
            throw malformed_stylesheet(start, "expected a body or 'from'");
        auto source = at_contextual("from") ? form_path() : advance();
        std::optional<StylesheetBody> body;
        if (validate_body && source.kind == TokenKind::CssIsland) {
            auto contents = source.span;
            ++contents.column;
            ++contents.offset;
            contents.length = source.text.size();
            body = parse_stylesheet(source.text, std::move(contents), module);
        }
        auto result = make_expression(Expr::Kind::Stylesheet, span_from(start));
        result.stylesheet = std::make_shared<StylesheetSyntax>(StylesheetSyntax{module, std::move(source), std::move(body)});
        return result;
    }

    static Diagnostic misplaced_stylesheet(const Span& span) {
        return Diagnostic(span, "html or css form not allowed here").with_code("E0255")
            .label("a stylesheet must be the whole initializer of a module-level 'const'");
    }

    Expr primary(std::string_view branch_keyword) {
        if (next_is_word(0, "derive") && tokens_[cursor_ + 1].kind == TokenKind::Identifier)
            throw Diagnostic(current().span, "a derivation is a declaration, not an expression").with_code("E0202");
        const auto start = cursor_;
        if (match("match")) {
            auto statement = parse_match(true, start);
            auto result = make_expression(Expr::Kind::Match, statement.span);
            result.match = std::make_shared<Statement>(std::move(statement));
            return result;
        }
        if (at_generator()) return generator_expression(start);
        if (at_contextual("yield") && in_generator_body())
            throw Diagnostic(current().span, "'yield' is a statement").with_code("E0221")
                .label("'yield' gives the generator's next value; it is not an expression")
                .help("write 'yield value;' on its own line");
        if (match("func")) return lambda_expression(start, branch_keyword);
        if (match("(")) {
            auto result = expression();
            expect(")");
            result.span = span_from(start);
            result.grouped = true;
            return result;
        }
        if (match("[")) {
            auto elements = expression_list("]");
            auto result = make_expression(Expr::Kind::Collection, span_from(start));
            result.operands = std::move(elements);
            return result;
        }
        if (match("{")) {
            auto result = expression();
            if (result.kind != Expr::Kind::Range)
                throw Diagnostic(result.span, "braces in an expression must enclose a range").with_code("E0212");
            expect("}");
            result.span = span_from(start);
            result.braced = true;
            return result;
        }
        if (match("new")) {
            if (at_html_factory()) return html_factory(start, true);
            if (at_stylesheet()) {
                (void)stylesheet(cursor_, false);
                throw misplaced_stylesheet(span_from(start));
            }
            return new_expression(start, branch_keyword);
        }
        if (at_html_factory()) return html_factory(start, false);
        if (at_stylesheet()) throw misplaced_stylesheet(stylesheet(start, false).span);
        if (match("import")) {
            expect(":");
            if (current().kind == TokenKind::Identifier && current().text == "c" &&
                tokens_[cursor_ + 1].kind == TokenKind::Symbol && tokens_[cursor_ + 1].text == "(") {
                // A C header import: a header path and any number of static
                // libraries or object files, all string literals.
                advance();
                expect("(");
                std::vector<std::string> paths;
                do {
                    if (current().kind != TokenKind::String)
                        throw Diagnostic(current().span, "expected a header or library path string").with_code("E0202");
                    paths.push_back(decode_string_literal(advance().text));
                } while (match(","));
                expect(")");
                auto result = make_expression(Expr::Kind::Import, span_from(start), "C." + c_module_name(paths[0]));
                result.c_import = std::move(paths);
                return result;
            }
            if (current().kind == TokenKind::String) {
                const auto path = decode_string_literal(advance().text);
                const auto name = match(".") ? identifier().text : std::string{};
                auto result = make_expression(Expr::Kind::Import, span_from(start), name);
                result.import_path = path;
                return result;
            }
            auto name = qualified_name(true);
            return make_expression(Expr::Kind::Import, span_from(start), std::move(name));
        }

        const auto& token = current();
        if (at("typeof")) return type_of(start);
        // `nameof(path)` (docs/design/Calls030.md): recognized where an
        // expression starts and '(' follows; lowering checks the operand.
        if (at_contextual("nameof") && next_is(1, "(")) {
            advance();
            advance();
            auto result = make_expression(Expr::Kind::NameOf, token.span, "nameof");
            if (!at(")")) result.operands.push_back(expression());
            expect(")");
            result.span = span_from(start);
            return result;
        }
        // `spawn name...`: two adjacent identifiers begin no other expression,
        // and `spawn(x)` stays a call of a function named spawn.
        if (at_contextual("spawn") && tokens_[cursor_ + 1].kind == TokenKind::Identifier) {
            advance();
            auto operand = postfix(branch_keyword);
            if (operand.kind != Expr::Kind::Call)
                throw Diagnostic(operand.span, "spawn requires a call, such as 'spawn work(item)'").with_code("E0603");
            auto result = make_expression(Expr::Kind::Spawn, span_from(start), "spawn");
            result.operands.push_back(std::move(operand));
            return result;
        }
        if (at("true") || at("false")) return token_expression(Expr::Kind::Boolean);
        if (at("nil")) return token_expression(Expr::Kind::Nil);
        if (token.kind == TokenKind::Identifier || (template_neura_ && at("module")))
            return token_expression(Expr::Kind::Name);
        if (is_character_literal(token)) {
            // A character literal is the integer literal of its byte
            // (docs/design/Expressions030.md, Character literals).
            auto result = token_expression(Expr::Kind::Integer);
            result.text = std::to_string(decode_character_literal(result.text));
            result.character = true;
            return result;
        }
        if (token.kind == TokenKind::Integer) return token_expression(Expr::Kind::Integer);
        if (token.kind == TokenKind::Float) return token_expression(Expr::Kind::Float);
        if (token.kind == TokenKind::String && token.parts) return interpolated_string();
        if (token.kind == TokenKind::String) return token_expression(Expr::Kind::String);
        throw Diagnostic(token.span, "expected an expression").with_code("E0204")
            .label(token.kind == TokenKind::End ? std::string("expected an expression before the end of the file")
                                                : "expected an expression, found '" + token.text + "'");
    }

    // `typeof(operand)` (docs/design/Expressions030.md, typeof). The operand
    // is read as a type and as an expression, each where it parses up to the
    // closing parenthesis; lowering decides which it names. When neither
    // parses, the expression's error is reported.
    Expr type_of(std::size_t start) {
        const auto keyword = advance();
        if (!at("("))
            throw Diagnostic(keyword.span, "typeof takes its operand in parentheses").with_code("E0267")
                .label("expected '(' after typeof")
                .help("write 'typeof(value)' for the type of a value, or 'typeof(Type)' for a type");
        advance();
        auto result = make_expression(Expr::Kind::TypeOf, keyword.span, "typeof");
        if (at(")"))
            throw Diagnostic(current().span, "typeof needs an operand").with_code("E0267")
                .label("expected a value or a type before ')'")
                .help("write 'typeof(value)' for the type of a value, or 'typeof(Type)' for a type");
        const auto operand = cursor_;
        std::optional<std::size_t> type_end;
        {
            SpeculativeDiagnostics speculative;
            try {
                auto written = type();
                if (at(")")) {
                    result.type = std::move(written);
                    type_end = cursor_;
                }
            } catch (const Diagnostic&) {
            }
        }
        cursor_ = operand;
        if (!type_end) {
            result.operands.push_back(expression());
        } else {
            SpeculativeDiagnostics speculative;
            try {
                auto value = expression();
                if (at(")")) {
                    speculative.commit();
                    result.operands.push_back(std::move(value));
                }
            } catch (const Diagnostic&) {
            }
            if (result.operands.empty()) cursor_ = *type_end;
        }
        expect(")");
        result.span = span_from(start);
        return result;
    }

    Expr lambda_expression(std::size_t start, std::string_view branch_keyword, bool owned = false) {
        auto lambda = std::make_shared<Declaration>();
        lambda->kind = Declaration::Kind::Function;
        function_body(*lambda, branch_keyword, true);
        if (!lambda->has_body)
            throw Diagnostic(current().span, "expected a function body").with_code("E0202");
        lambda->span = span_from(start);
        auto result = make_expression(Expr::Kind::Lambda, lambda->span, owned ? "new" : "");
        result.lambda = std::move(lambda);
        return result;
    }

    // After `new` (ObjectLifecycle.md, Construction):
    //   new T(arguments)     the class T
    //   new(arguments)       target-typed: `(` always begins arguments
    //   new                  target-typed, no arguments: `new` followed by
    //                        anything that cannot begin a type, such as
    //                        `;`, `,`, `)`, `]`, or `}`
    // A target-typed `new` has no type; lowering takes the expected type.
    Expr new_expression(std::size_t start, std::string_view branch_keyword) {
        if (at_contextual("with")) return copy_expression(start);
        if (match("func")) return lambda_expression(start, branch_keyword, true);
        if (at_generator()) {
            // `new iterator(...) -> T { ... }`: an owned capturing generator
            // (docs/design/Lifetime030.md, Generators).
            auto result = generator_expression(start);
            result.text = "new";
            return result;
        }
        if (at_contextual("bound") && next_is(1, "(")) {
            advance();
            expect("(");
            auto target = expression();
            expect(")");
            auto result = make_expression(Expr::Kind::New, span_from(start), "bound");
            result.operands.push_back(std::move(target));
            return result;
        }
        const bool type_start = current().kind == TokenKind::Identifier || at("&") || at("[") ||
                                at("class");
        if (!at("(") && !type_start) return make_expression(Expr::Kind::New, span_from(start));
        std::optional<TypeSyntax> object_type;
        if (at("class") && next_is(1, "(") && next_is(2, ")") && !next_is(3, "(")) {
            // `new class()` names the umbrella type and passes no arguments;
            // the empty anonymous class is constructed with `new class()()`.
            object_type = TypeSyntax{advance().span, "class", {}, {}};
        } else if (!at("(")) {
            object_type = type();
        }
        const auto open = current().span;
        const auto open_index = cursor_;
        expect("(");
        std::vector<std::string> labels;
        std::vector<Span> label_spans;
        auto arguments = argument_list(labels, label_spans);
        if (!object_type && at("(")) {
            // `new (T)(x)` once parenthesized the type. The first list is
            // now the arguments of a target-typed `new`.
            auto error = Diagnostic(span_from(start), "a type after 'new' is not parenthesized").with_code("E0435")
                .label("the class cannot be written in parentheses here")
                .label(open, "this '(' begins the arguments of a target-typed 'new'")
                .label(current().span, "a second argument list follows");
            if (arguments.size() == 1 && (arguments.front().kind == Expr::Kind::Name ||
                                          arguments.front().kind == Expr::Kind::Member)) {
                std::string name;
                for (auto i = open_index + 1; i + 1 < cursor_; ++i) name += tokens_[i].text;
                const auto& close = tokens_[cursor_ - 1].span;
                error.suggest("write the class without parentheses",
                              Span{open.file, open.line, open.column, open.offset,
                                   close.offset + close.length - open.offset},
                              name, Applicability::MaybeIncorrect);
                error.help("where the type is expected, the class can be left out: 'new(x)'");
            } else {
                error.help("write 'new T(arguments)', or 'new(arguments)' where the type is expected");
            }
            throw error;
        }
        auto result = make_expression(Expr::Kind::New, span_from(start));
        result.type = std::move(object_type);
        result.operands = std::move(arguments);
        result.open = open;
        result.labels = std::move(labels);
        result.label_spans = std::move(label_spans);
        return result;
    }

    // `$"..."`: each hole's tokens are parsed as one expression. Without a
    // hole the literal is the string literal of its text.
    Expr interpolated_string() {
        const auto& token = advance();
        auto syntax = std::make_shared<InterpolationSyntax>();
        auto result = make_expression(Expr::Kind::Interpolation, token.span, token.text);
        const auto escape_braces = [](const std::string& text) {
            std::string escaped;
            for (const char c : text) {
                escaped += c;
                if (c == '{' || c == '}') escaped += c;
            }
            return escaped;
        };
        const auto hole_expression = [](const InterpolationPart& part) {
            Parser nested(part.tokens);
            auto value = nested.expression();
            if (!nested.at_end())
                throw Diagnostic(nested.current().span, "expected the end of the hole").with_code("E0202")
                    .label("the hole's expression ends before this");
            return value;
        };
        std::string text;
        for (const auto& part : *token.parts) {
            if (part.kind == InterpolationPart::Kind::Text) {
                syntax->pattern += escape_braces(part.text);
                text += part.text;
                continue;
            }
            InterpolationHole hole{result.operands.size(), {}, {}, part.specification_span, true};
            result.operands.push_back(hole_expression(part));
            syntax->pattern += "{" + std::to_string(hole.operand);
            if (part.specification_span) {
                for (const auto& piece : part.specification) {
                    if (piece.kind == InterpolationPart::Kind::Text) {
                        hole.specification += piece.text;
                        continue;
                    }
                    hole.plain_specification = false;
                    hole.nested.push_back(result.operands.size());
                    hole.specification += "{" + std::to_string(result.operands.size()) + "}";
                    result.operands.push_back(hole_expression(piece));
                }
                syntax->pattern += ":" + hole.specification;
            }
            syntax->pattern += "}";
            syntax->holes.push_back(std::move(hole));
        }
        if (syntax->holes.empty()) {
            std::string literal = "\"";
            for (const char c : text) {
                if (c == '\n') literal += "\\n";
                else if (c == '\r') literal += "\\r";
                else if (c == '\t') literal += "\\t";
                else if (c == '\\') literal += "\\\\";
                else if (c == '"') literal += "\\\"";
                else literal += c;
            }
            return make_expression(Expr::Kind::String, token.span, literal + "\"");
        }
        result.interpolation = std::move(syntax);
        return result;
    }

    Expr token_expression(Expr::Kind kind) {
        const auto& token = advance();
        return make_expression(kind, token.span, token.text);
    }

    static Expr make_expression(Expr::Kind kind, Span span, std::string text = {}) {
        return {kind, std::move(span), std::move(text), {}, {}, std::nullopt};
    }
};

} // namespace

ParsedFile parse(const std::vector<Token>& tokens) {
    if (tokens.empty() || tokens.back().kind != TokenKind::End) {
        throw std::invalid_argument("parser requires an end-of-file token");
    }
    return Parser(tokens).file();
}

ParsedFile parse_recovering(const std::vector<Token>& tokens, std::vector<Diagnostic>& diagnostics) {
    if (tokens.empty() || tokens.back().kind != TokenKind::End)
        throw std::invalid_argument("parser requires an end-of-file token");
    return Parser(tokens).file(&diagnostics);
}

Expr parse_template_expression(const std::vector<Token>& tokens) {
    return Parser(tokens, true).template_expression();
}

TemplateForHead parse_template_for_head(const std::vector<Token>& tokens) {
    return Parser(tokens, true).template_for_head();
}

} // namespace neura

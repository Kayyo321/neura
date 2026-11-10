#include "syntax.hpp"

#include <charconv>
#include <cstddef>
#include <string_view>
#include <utility>

namespace neura {
namespace {

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    ParsedFile file() {
        ParsedFile result;
        result.file = current().span.file;
        while (!at_end()) {
            result.statements.push_back(statement());
        }
        return result;
    }

private:
    const std::vector<Token>& tokens_;
    std::size_t cursor_{};
    unsigned block_depth_ = 0;

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
            throw Diagnostic(current().span, "expected '" + std::string(text) + "'");
        }
    }

    const Token& identifier() {
        if (current().kind != TokenKind::Identifier) {
            throw Diagnostic(current().span, "expected an identifier");
        }
        return advance();
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
                parameter.name = identifier().text;
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

    std::vector<Parameter> parameters() {
        std::vector<Parameter> result;
        if (!match("(")) return result;
        if (!at(")")) {
            do {
                const auto& name = identifier();
                expect(":");
                auto parameter_type = type();
                result.push_back({name.span, name.text, std::move(parameter_type)});
            } while (match(","));
        }
        expect(")");
        return result;
    }

    // Return true when an expression body still needs its terminating semicolon.
    bool function_body(Declaration& result) {
        result.parameters = parameters();
        result.type = match("->") ? type() : TypeSyntax{current().span, "void", {}, {}};
        result.has_body = at("{") || at("=>");
        if (!result.has_body) return false;
        if (!match("=>")) {
            result.body = block();
            return false;
        }
        Statement statement{};
        statement.kind = result.type->name == "void" && result.type->collections.empty()
            ? Statement::Kind::Expression : Statement::Kind::Return;
        statement.expressions.push_back(expression());
        statement.span = statement.expressions.front().span;
        result.body.push_back(std::move(statement));
        return true;
    }

    bool at_declaration() const { return at("export") || at("static") || at("let") || at("const"); }

    Declaration declaration(bool member) {
        const auto start = cursor_;
        bool expression_body = false;
        Declaration result{};
        result.kind = Declaration::Kind::Binding;
        result.exported = match("export");
        result.static_member = match("static");
        result.constant = match("const");
        if (!result.constant) {
            expect("let");
        }
        if (match(".")) {
            if (!result.constant && match("promote")) {
                result.promote_import = true;
            } else {
                expect("override");
                result.override_member = true;
            }
        }
        result.name = identifier().text;
        if (match(":")) {
            result.type = type();
        }

        if (match("=")) {
            if (!result.type && block_depth_ == 0 && match("func")) {
                const auto function_start = cursor_ - 1;
                definition_has_no_annotation(result);
                result.kind = Declaration::Kind::Function;
                result.generics = generic_parameters();
                expression_body = function_body(result);
                if (!result.has_body && current().kind == TokenKind::Identifier && current().text == "host") {
                    advance();
                    expect("(");
                    const auto& service = current();
                    if (service.kind != TokenKind::String)
                        throw Diagnostic(service.span, "expected a host service name");
                    advance();
                    expect(",");
                    const auto& version = current();
                    std::uint32_t number = 0;
                    const auto end = version.text.data() + version.text.size();
                    const auto [next, error] = std::from_chars(version.text.data(), end, number);
                    if (version.kind != TokenKind::Integer || error != std::errc{} || next != end || number == 0)
                        throw Diagnostic(version.span, "host service version must be a positive uint32");
                    advance();
                    expect(")");
                    result.host_binding.emplace(service.text.substr(1, service.text.size() - 2), number);
                }
                if (result.has_body && !expression_body && result.generics.empty() && !result.override_member &&
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
            } else if (match("enum")) {
                definition_has_no_annotation(result);
                result.kind = Declaration::Kind::Enum;
                result.generics = generic_parameters();
                expect("(");
                while (!at(")")) {
                    const auto variant_start = cursor_;
                    const auto& variant = identifier();
                    Declaration member{};
                    member.kind = Declaration::Kind::Binding;
                    member.name = variant.text;
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
                    throw Diagnostic(current().span, "an enum needs at least one variant");
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
                if (result.kind == Declaration::Kind::Class && match("implements")) {
                    do {
                        result.interfaces.push_back(type());
                    } while (match(","));
                }
                expect("(");
                while (!at_end() && !at(")")) {
                    result.members.push_back(declaration(true));
                }
                expect(")");
            } else {
                result.initializer = expression();
            }
        } else if (!result.type) {
            throw Diagnostic(current().span, "expected a type annotation or an initializer");
        }

        // Aggregate members can omit the semicolon after a function body.
        if (member && result.has_body && !expression_body) {
            match(";");
        } else {
            expect(";");
        }
        result.span = span_from(start);
        return result;
    }

    static void definition_has_no_annotation(const Declaration& declaration) {
        if (declaration.type) {
            throw Diagnostic(declaration.type->span, "a definition cannot have a binding type annotation");
        }
    }

    std::vector<Statement> block() {
        expect("{");
        ++block_depth_;
        std::vector<Statement> result;
        while (!at_end() && !at("}")) {
            result.push_back(statement());
        }
        expect("}");
        --block_depth_;
        return result;
    }

    Expr condition() {
        expect("(");
        auto result = expression();
        expect(")");
        return result;
    }

    Statement statement() {
        const auto start = cursor_;
        Statement result{};

        if ((at("let") || at("const")) && tokens_[cursor_ + 1].text == "[") {
            result.kind = Statement::Kind::Destructure;
            result.constant_bindings = match("const");
            if (!result.constant_bindings) expect("let");
            expect("[");
            do {
                result.bindings.push_back(identifier());
            } while (match(",") && !at("]"));
            expect("]");
            expect("=");
            result.expressions.push_back(expression());
            expect(";");
        } else if (at_declaration()) {
            result.kind = Statement::Kind::Binding;
            result.declaration = std::make_unique<Declaration>(declaration(false));
        } else if (at("{")) {
            result.kind = Statement::Kind::Block;
            result.body = block();
        } else if (match("if")) {
            result.kind = Statement::Kind::If;
            result.expressions.push_back(condition());
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
            result.expressions.push_back(condition());
            result.body = block();
        } else if (match("do")) {
            result.kind = Statement::Kind::Do;
            result.body = block();
            if (match("while")) {
                result.expressions.push_back(condition());
                expect(";");
            }
        } else if (match("for")) {
            result = for_statement();
        } else {
            result = simple_statement();
            expect(";");
        }
        result.span = span_from(start);
        return result;
    }

    Statement parse_match(bool produces_value, std::size_t start) {
        Statement result{};
        result.kind = Statement::Kind::Match;
        result.expressions.push_back(condition());
        expect("{");
        while (!at("}")) {
            result.arms.push_back(match_arm(produces_value));
            if (!match(",")) break;
        }
        expect("}");
        result.span = span_from(start);
        return result;
    }

    MatchArm match_arm(bool produces_value) {
        const auto start = cursor_;
        MatchArm result{};
        result.variant = qualified_name();
        const bool positional = match("(");
        result.named = !positional && match("{");
        if (positional || result.named) {
            const auto close = positional ? ")" : "}";
            while (!at(close)) {
                auto binding = identifier();
                if (result.named) {
                    result.fields.push_back(binding.text);
                    if (match(":")) binding = identifier();
                }
                result.bindings.push_back(std::move(binding));
                if (!match(",")) break;
            }
            expect(close);
        }
        expect("=>");
        if (produces_value) {
            result.value = expression();
        } else {
            result.body = block();
        }
        result.span = span_from(start);
        return result;
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
                declaration->name = identifier().text;
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
            binding.declaration = std::make_unique<Declaration>(declaration(false));
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
            result.expressions.push_back(expression());
        }
        expect(";");
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

    Statement simple_statement() {
        Statement result{};
        if (match("return")) {
            result.kind = Statement::Kind::Return;
            if (!at(";")) {
                result.expressions.push_back(expression());
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
            if (at("=") || at("+=") || at("-=") || at("*=") || at("/=") || at("%=") ||
                at("&=") || at("^=") || at("|=") || at("<<=") || at(">>=")) {
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
        if (at("==") || at("!=") || at("===")) return equality_precedence;
        if (at_shift()) return 8;
        if (at("<") || at("<=") || at(">") || at(">=")) return ordering_precedence;
        if (at("|")) return 5;
        if (at("^")) return 6;
        if (at("&")) return 7;
        if (at("+") || at("-")) return 9;
        if (at("*") || at("/") || at("%")) return 10;
        return 0;
    }

    Expr expression(int minimum_precedence = 1) {
        const auto start = cursor_;
        return expression_tail(start, cast(), minimum_precedence);
    }

    Expr expression_tail(std::size_t start, Expr left, int minimum_precedence) {
        while (precedence() >= minimum_precedence) {
            const auto priority = precedence();
            const bool shift = at_shift();
            auto op = advance().text;
            if (shift) {
                op += advance().text;
            }
            auto right = expression(priority + 1);
            if (op != "===" && (priority == equality_precedence || priority == ordering_precedence) &&
                (at("and") || at("or"))) {
                left = comparison_chain(start, std::move(op), std::move(left), std::move(right));
                continue;
            }
            auto binary = make_expression(Expr::Kind::Binary, span_from(start), op);
            binary.operands.push_back(std::move(left));
            binary.operands.push_back(std::move(right));
            left = std::move(binary);
        }
        if (minimum_precedence == 1 && match("?")) {
            auto when_true = expression();
            expect(":");
            auto when_false = expression();
            auto conditional = make_expression(Expr::Kind::Conditional, span_from(start));
            conditional.operands.push_back(std::move(left));
            conditional.operands.push_back(std::move(when_true));
            conditional.operands.push_back(std::move(when_false));
            return conditional;
        }
        return left;
    }

    Expr comparison_chain(std::size_t start, std::string op, Expr left, Expr first) {
        const auto connector = current().text;
        auto result = make_expression(Expr::Kind::ComparisonChain, left.span, std::move(op));
        result.chain_conjunction = connector == "and";
        result.operands.push_back(std::move(left));
        result.operands.push_back(std::move(first));
        do {
            advance();
            result.operands.push_back(expression(ordering_precedence + 1));
            if (precedence() == equality_precedence || precedence() == ordering_precedence) {
                throw Diagnostic(current().span, "a comparison chain expects another value; use '&&' or '||' between comparisons");
            }
        } while (at(connector));
        if (at("and") || at("or")) {
            throw Diagnostic(current().span, "a comparison chain must use only 'and' or only 'or'");
        }
        result.span = span_from(start);
        return result;
    }

    // `as` binds tighter than the binary operators and looser than unary
    // operators: `-x as int64` converts `-x`, and `a * b as int64` converts
    // only `b`.
    Expr cast() {
        const auto start = cursor_;
        return cast_tail(start, unary());
    }

    Expr cast_tail(std::size_t start, Expr result) {
        while (at("as") || at("is")) {
            const bool test = advance().text == "is";
            auto target = type();
            auto conversion = make_expression(test ? Expr::Kind::TypeTest : Expr::Kind::Cast, span_from(start));
            conversion.type = std::move(target);
            conversion.operands.push_back(std::move(result));
            result = std::move(conversion);
        }
        return result;
    }

    Expr unary() {
        const auto start = cursor_;
        if (at("++") || at("--")) {
            const auto op = advance().text;
            auto operand = unary();
            auto result = make_expression(Expr::Kind::PrefixUpdate, span_from(start), op);
            result.operands.push_back(std::move(operand));
            return result;
        }
        if (at("!") || at("-") || at("~")) {
            const auto op = advance().text;
            auto operand = unary();
            auto result = make_expression(Expr::Kind::Unary, span_from(start), op);
            result.operands.push_back(std::move(operand));
            return result;
        }
        auto operand = postfix();
        if (at("++") || at("--")) {
            const auto op = advance().text;
            auto result = make_expression(Expr::Kind::PostfixUpdate, span_from(start), op);
            result.operands.push_back(std::move(operand));
            return result;
        }
        return operand;
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

    // Only unambiguous postfix or terminating tokens commit a type argument list.
    std::vector<TypeSyntax> postfix_type_arguments() {
        const auto start = cursor_;
        advance();
        try {
            auto result = type_arguments();
            if (at("(") || at(".") || at(";")) {
                return result;
            }
        } catch (const Diagnostic&) {
            // The angle bracket can instead be an ordering operator.
        }
        cursor_ = start;
        return {};
    }

    Expr postfix() {
        const auto start = cursor_;
        return postfix_tail(start, primary());
    }

    Expr postfix_tail(std::size_t start, Expr result) {
        while (true) {
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
                    construct.field_names.push_back(identifier().text);
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
            if (!match("(")) {
                return result;
            }
            auto operands = expression_list(")");
            auto call = make_expression(Expr::Kind::Call, span_from(start));
            call.type_arguments = std::move(arguments);
            call.operands.reserve(1 + operands.size());
            call.operands.push_back(std::move(result));
            for (auto& operand : operands) {
                call.operands.push_back(std::move(operand));
            }
            result = std::move(call);
        }
    }

    Expr primary() {
        const auto start = cursor_;
        if (match("match")) {
            auto statement = parse_match(true, start);
            auto result = make_expression(Expr::Kind::Match, statement.span);
            result.match = std::make_shared<Statement>(std::move(statement));
            return result;
        }
        if (match("func")) {
            auto lambda = std::make_shared<Declaration>();
            lambda->kind = Declaration::Kind::Function;
            function_body(*lambda);
            if (!lambda->has_body)
                throw Diagnostic(current().span, "expected a function body");
            lambda->span = span_from(start);
            auto result = make_expression(Expr::Kind::Lambda, lambda->span);
            result.lambda = std::move(lambda);
            return result;
        }
        if (match("(")) {
            auto result = expression();
            expect(")");
            result.span = span_from(start);
            return result;
        }
        if (match("[")) {
            auto elements = expression_list("]");
            auto result = make_expression(Expr::Kind::Collection, span_from(start));
            result.operands = std::move(elements);
            return result;
        }
        if (match("new")) {
            auto object_type = type();
            expect("(");
            auto arguments = expression_list(")");
            auto result = make_expression(Expr::Kind::New, span_from(start));
            result.type = std::move(object_type);
            result.operands = std::move(arguments);
            return result;
        }
        if (match("import")) {
            expect(":");
            auto name = qualified_name(true);
            return make_expression(Expr::Kind::Import, span_from(start), std::move(name));
        }

        const auto& token = current();
        if (at("true") || at("false")) return token_expression(Expr::Kind::Boolean);
        if (at("nil")) return token_expression(Expr::Kind::Nil);
        if (token.kind == TokenKind::Identifier) return token_expression(Expr::Kind::Name);
        if (token.kind == TokenKind::Integer) return token_expression(Expr::Kind::Integer);
        if (token.kind == TokenKind::Float) return token_expression(Expr::Kind::Float);
        if (token.kind == TokenKind::String) return token_expression(Expr::Kind::String);
        throw Diagnostic(token.span, "expected an expression");
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

} // namespace neura

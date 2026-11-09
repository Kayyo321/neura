#include "syntax.hpp"

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

    std::string qualified_name() {
        std::string name = identifier().text;
        while (match(".")) {
            name += '.';
            name += identifier().text;
        }
        return name;
    }

    TypeSyntax type() {
        const auto start = cursor_;
        TypeSyntax result{};
        result.name = qualified_name();
        if (match("<")) {
            result.arguments = type_arguments();
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
                    parameter.bound = type();
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
        expect("(");
        if (!at(")")) {
            do {
                auto parameter_type = type();
                const auto& name = identifier();
                result.push_back({name.span, name.text, std::move(parameter_type)});
            } while (match(","));
        }
        expect(")");
        return result;
    }

    bool at_declaration() const { return at("export") || at("let") || at("const"); }

    Declaration declaration(bool member) {
        const auto start = cursor_;
        Declaration result{};
        result.kind = Declaration::Kind::Binding;
        result.exported = match("export");
        result.constant = match("const");
        if (!result.constant) {
            expect("let");
        }
        if (match(".")) {
            expect("override");
            result.override_member = true;
        }
        result.name = identifier().text;
        if (match(":")) {
            result.type = type();
        }

        if (match("=")) {
            if (match("func")) {
                definition_has_no_annotation(result);
                result.kind = Declaration::Kind::Function;
                result.generics = generic_parameters();
                result.parameters = parameters();
                expect("->");
                result.type = type();
                result.has_body = at("{");
                if (result.has_body) {
                    result.body = block();
                }
            } else if (at("module") || at("class")) {
                definition_has_no_annotation(result);
                const bool class_definition = match("class");
                if (!class_definition) {
                    expect("module");
                }
                result.kind = class_definition ? Declaration::Kind::Class : Declaration::Kind::Module;
                if (class_definition) {
                    result.generics = generic_parameters();
                }
                if (match(":")) {
                    result.base = type();
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
        if (member && result.has_body) {
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
        std::vector<Statement> result;
        while (!at_end() && !at("}")) {
            result.push_back(statement());
        }
        expect("}");
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

        if (at_declaration()) {
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
        } else if (match("while")) {
            result.kind = Statement::Kind::While;
            result.expressions.push_back(condition());
            result.body = block();
        } else {
            result = simple_statement();
            expect(";");
        }
        result.span = span_from(start);
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
            if (match("=")) {
                result.kind = Statement::Kind::Assignment;
                result.expressions.push_back(expression());
            }
        }
        return result;
    }

    int precedence() const {
        if (at("||")) return 1;
        if (at("&&")) return 2;
        if (at("==") || at("!=")) return 3;
        if (at("<") || at("<=") || at(">") || at(">=")) return 4;
        if (at("+") || at("-")) return 5;
        if (at("*") || at("/") || at("%")) return 6;
        return 0;
    }

    Expr expression(int minimum_precedence = 1) {
        const auto start = cursor_;
        auto left = cast();
        while (precedence() >= minimum_precedence) {
            const auto priority = precedence();
            const auto op = advance().text;
            auto right = expression(priority + 1);
            auto binary = make_expression(Expr::Kind::Binary, span_from(start), op);
            binary.operands.push_back(std::move(left));
            binary.operands.push_back(std::move(right));
            left = std::move(binary);
        }
        return left;
    }

    // `as` binds tighter than the binary operators and looser than unary
    // operators: `-x as int64` converts `-x`, and `a * b as int64` converts
    // only `b`.
    Expr cast() {
        const auto start = cursor_;
        auto result = unary();
        while (match("as")) {
            auto target = type();
            auto conversion = make_expression(Expr::Kind::Cast, span_from(start));
            conversion.type = std::move(target);
            conversion.operands.push_back(std::move(result));
            result = std::move(conversion);
        }
        return result;
    }

    Expr unary() {
        const auto start = cursor_;
        if (at("!") || at("-")) {
            const auto op = advance().text;
            auto operand = unary();
            auto result = make_expression(Expr::Kind::Unary, span_from(start), op);
            result.operands.push_back(std::move(operand));
            return result;
        }
        return postfix();
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

    // A type argument list on an expression is only recognized before a call.
    std::vector<TypeSyntax> call_type_arguments() {
        const auto start = cursor_;
        advance();
        try {
            auto result = type_arguments();
            if (at("(")) {
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
        auto result = primary();
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
            std::vector<TypeSyntax> arguments;
            if (at("<") && (result.kind == Expr::Kind::Name || result.kind == Expr::Kind::Member)) {
                arguments = call_type_arguments();
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
            auto name = qualified_name();
            return make_expression(Expr::Kind::Import, span_from(start), std::move(name));
        }

        const auto& token = current();
        if (at("true") || at("false")) return token_expression(Expr::Kind::Boolean);
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

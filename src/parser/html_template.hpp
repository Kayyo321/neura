#pragma once

#include "syntax.hpp"

#include <variant>

namespace neura {

struct TemplateText { std::string value; };
struct TemplateHole { Expr expression; };
struct TemplateElement;
struct TemplateIf;
struct TemplateFor;

struct TemplateNode {
    Span span;
    std::variant<TemplateText, TemplateHole, std::shared_ptr<TemplateElement>,
                 std::shared_ptr<TemplateIf>, std::shared_ptr<TemplateFor>> value;
};

struct TemplateAttribute {
    enum class Kind { Attribute, Event, Property, Variable };
    Kind kind;
    std::string name;
    Span name_span;
    std::vector<Token> modifiers;
    std::variant<std::monostate, std::string, Expr> value;
    std::optional<Span> value_span;
};

struct TemplateElement {
    std::string name;
    Span name_span;
    Span opening;
    bool svg;
    std::vector<TemplateAttribute> attributes;
    std::vector<TemplateNode> children;
};

struct TemplateIfBranch {
    Span span;
    std::optional<Expr> condition;
    std::vector<TemplateNode> children;
};

struct TemplateIf { std::vector<TemplateIfBranch> branches; };

struct TemplateForHead {
    bool constant;
    Token binding;
    std::optional<TypeSyntax> type;
    Expr input;
    Expr key;
};

struct TemplateFor {
    TemplateForHead head;
    std::vector<TemplateNode> children;
};

struct TemplateSyntax {
    Span span;
    std::vector<TemplateNode> nodes;
};

struct HtmlFactorySyntax {
    bool retained;
    std::vector<Parameter> parameters;
    std::variant<TemplateSyntax, Token> source;
    // Additional signatures in a declaration written with 'or html'.
    std::vector<Expr> overloads{};
};

struct StylesheetSyntax {
    bool module;
    // CssIsland for inline CSS, String for a path read by the source loader.
    Token source;
};

TemplateSyntax parse_template(std::string_view body, Span origin);
Expr parse_template_expression(const std::vector<Token>& tokens);
TemplateForHead parse_template_for_head(const std::vector<Token>& tokens);

} // namespace neura

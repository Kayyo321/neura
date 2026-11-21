#pragma once

#include "syntax.hpp"
#include "css_stylesheet.hpp"

#include <variant>

namespace neura {

// A file the build writes beside the image: a stylesheet, or an asset that a
// stylesheet or a template references. The path is relative to the
// directory that holds index.html: 'styles/<sha256>.css' or
// 'assets/<sha256>.<extension>'.
struct BundleFileSyntax {
    std::string path;
    std::string sha256;
    std::string contents;
};

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
    // The local media files named by a static src, poster, srcset or SVG
    // <use> href, as the source loader resolved them, and the value that
    // replaces the written one. Empty when the value names no local file.
    std::vector<std::shared_ptr<BundleFileSyntax>> assets{};
    std::string asset_target{};
    // Why the file could not be resolved; lowering reports it after the
    // element's own checks.
    std::shared_ptr<Diagnostic> asset_error{};
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

struct ResolvedTemplate {
    TemplateSyntax body;
    std::string sha256;
};

struct HtmlFactorySyntax {
    bool retained;
    std::vector<Parameter> parameters;
    std::variant<TemplateSyntax, Token> source;
    // Additional signatures in a declaration written with 'or html'.
    std::vector<Expr> overloads{};
    std::optional<ResolvedTemplate> external{};
};

// What the source loader resolved for a stylesheet form.
struct ResolvedStylesheet {
    // The CSS bytes, without a byte-order mark, and their analysis.
    std::string text;
    StylesheetBody body;
    // A file's path relative to the source root, or for an inline sheet the
    // relative path of the declaring source file.
    std::string path;
    bool external;
    // One rewritten target per reference of `body`; empty keeps it as written.
    std::vector<std::string> targets;
    // Imported stylesheets and assets, each once, in first-reference order.
    std::vector<BundleFileSyntax> files;
};

struct StylesheetSyntax {
    bool module;
    // CssIsland for inline CSS, String for a path read by the source loader.
    Token source;
    std::optional<StylesheetBody> body{};
    std::optional<ResolvedStylesheet> resolved{};
};

TemplateSyntax parse_template(std::string_view body, Span origin,
                              std::vector<std::size_t>* tag_sigils = nullptr);
Expr parse_template_expression(const std::vector<Token>& tokens,
                               std::vector<std::size_t>* tag_sigils = nullptr);
TemplateForHead parse_template_for_head(const std::vector<Token>& tokens,
                                       std::vector<std::size_t>* tag_sigils = nullptr);

// The URLs of a srcset value, as byte ranges: each candidate is a URL, then
// optional descriptors, separated by commas (HTML, "srcset"). A URL may
// contain a comma, but one that ends the URL separates candidates.
std::vector<std::pair<std::size_t, std::size_t>> srcset_urls(std::string_view value);

} // namespace neura

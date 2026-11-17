#include "cli/source_loader.hpp"
#include "parser/html_template.hpp"
#include "util/source_map.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>

namespace {
using namespace neura;

void check(bool value, int line) {
    if (!value) throw std::runtime_error("check failed on line " + std::to_string(line));
}

#define CHECK(value) check((value), __LINE__)

const Expr& initializer(const ParsedFile& file, std::size_t index) {
    return *file.statements.at(index).declaration->initializer;
}

const TemplateElement& element(const TemplateNode& node) {
    return *std::get<std::shared_ptr<TemplateElement>>(node.value);
}

std::string_view text(const TemplateNode& node) {
    return std::get<TemplateText>(node.value).value;
}

std::string_view spelling(std::string_view source, const Span& span) {
    return source.substr(span.offset, span.length);
}

void fragments() {
    const std::string source =
        "let view = new html(name, label: string, #Unused handler: func(string) -> void,) {\r\n"
        "    <p title='{not a hole}' data-note={label} on:click.stop_propagation={handler}>"
        "caf\xc3\xa9 {name /* } */}</p>\r\n"
        "    <!-- ignored { content -->\r\n"
        "    <pre>  first\r\n second </pre><br/><span>&NotEqualTilde; &amp; &#x1F600;</span>\r\n"
        "    <svg viewBox=\"0 0 1 1\"><linearGradient/><path/></svg>\r\n"
        "};";
    const auto file = parse(lex(source, "template.nra"));
    const auto& declaration = initializer(file, 0);
    CHECK(declaration.kind == Expr::Kind::HtmlFactory);
    const auto& factory = *declaration.html_factory;
    CHECK(factory.retained);
    CHECK(factory.parameters.size() == 3);
    CHECK(factory.parameters[0].type.name == "string");
    CHECK(factory.parameters[1].type.name == "string");
    CHECK(factory.parameters[2].tags.at(0).name == "Unused");
    CHECK(factory.parameters[2].type.name == "[function]");
    const auto& body = std::get<TemplateSyntax>(factory.source);
    CHECK(body.nodes.size() == 5);
    const auto& paragraph = element(body.nodes[0]);
    CHECK(paragraph.name == "p");
    CHECK(paragraph.attributes.size() == 3);
    CHECK(std::get<std::string>(paragraph.attributes[0].value) == "{not a hole}");
    CHECK(std::get<Expr>(paragraph.attributes[1].value).text == "label");
    CHECK(paragraph.attributes[2].kind == TemplateAttribute::Kind::Event);
    CHECK(paragraph.attributes[2].name == "click");
    CHECK(paragraph.attributes[2].modifiers.at(0).text == "stop_propagation");
    CHECK(text(paragraph.children.at(0)) == "caf\xc3\xa9 ");
    const auto& hole = paragraph.children.at(1);
    const auto& name = std::get<TemplateHole>(hole.value).expression;
    CHECK(name.kind == Expr::Kind::Name && name.text == "name");
    CHECK(spelling(source, hole.span) == "{name /* } */}");
    CHECK(spelling(source, name.span) == "name");
    CHECK(name.span.file == "template.nra");
    CHECK(name.span.line == 2);
    CHECK(name.span.column == name.span.offset - source.find('\n') - 1);
    CHECK(text(element(body.nodes[1]).children.at(0)) == "  first\r\n second ");
    CHECK(element(body.nodes[2]).name == "br");
    CHECK(text(element(body.nodes[3]).children.at(0)) == "\xe2\x89\x82\xcc\xb8 & \xf0\x9f\x98\x80");
    CHECK(element(body.nodes[4]).svg);
    CHECK(element(element(body.nodes[4]).children.at(0)).name == "linearGradient");
}

void control_flow() {
    constexpr std::string_view source =
        "let rows = html(parts: int[]) {"
        "<ul>@if (present(\"}\")) {"
        "@for (const part: int in parts; key: part + 1) {"
        "<li title={label(\"}\")}>{func() -> string { return \"}\"; }}</li>}"
        "} @else if (fallback) { } @else { <li>None</li> }</ul>"
        "};";
    const auto file = parse(lex(source, "control.nra"));
    const auto& root = std::get<TemplateSyntax>(initializer(file, 0).html_factory->source);
    const auto& list = element(root.nodes.at(0));
    const auto& choice = *std::get<std::shared_ptr<TemplateIf>>(list.children.at(0).value);
    CHECK(choice.branches.size() == 3);
    CHECK(choice.branches[0].condition->kind == Expr::Kind::Call);
    CHECK(choice.branches[1].condition->text == "fallback");
    CHECK(choice.branches[2].condition == std::nullopt);
    const auto& loop = *std::get<std::shared_ptr<TemplateFor>>(choice.branches[0].children.at(0).value);
    CHECK(loop.head.constant);
    CHECK(loop.head.binding.text == "part");
    CHECK(loop.head.type->name == "int");
    CHECK(spelling(source, loop.head.input.span) == "parts");
    CHECK(spelling(source, loop.head.key.span) == "part + 1");
    const auto& row = element(loop.children.at(0));
    CHECK(row.name == "li");
    CHECK(std::get<TemplateHole>(row.children.at(0).value).expression.kind == Expr::Kind::Lambda);
}

void contextual_forms() {
    const auto file = parse(lex(
        "let html = func(x: int) -> int { return x; };"
        "let called = html(3); let chosen = html(true ? first : other);"
        "let view = html(from, key: string) from \"./view.html\";"
        "const Styles = css.module from \"./view.module.css\";"
        "const Inline = css { .note { color: #a40; } };"
        "let css = 1; let field = theme.css; let read = page.html(3);"
        "let named = html(module: string) { {module} };",
        "forms.nra"));
    CHECK(initializer(file, 1).kind == Expr::Kind::Call);
    CHECK(initializer(file, 2).kind == Expr::Kind::Call);
    CHECK(std::get<Token>(initializer(file, 3).html_factory->source).text == "\"./view.html\"");
    CHECK(initializer(file, 4).stylesheet->module);
    CHECK(initializer(file, 4).stylesheet->source.kind == TokenKind::String);
    CHECK(initializer(file, 5).stylesheet->source.text == " .note { color: #a40; } ");
    CHECK(initializer(file, 7).kind == Expr::Kind::Member);
    CHECK(initializer(file, 8).kind == Expr::Kind::Call);
    const auto& contextual = *initializer(file, 9).html_factory;
    CHECK(contextual.parameters[0].name == "module");
    const auto& contextual_body = std::get<TemplateSyntax>(contextual.source);
    CHECK(std::get<TemplateHole>(contextual_body.nodes[1].value).expression.text == "module");
}

void recovery() {
    constexpr std::string_view source = "let view = html() {\n  <butt\nlast next = 1;\n";
    std::vector<Diagnostic> diagnostics;
    const auto tokens = lex_recovering(source, "recovery.nra", diagnostics);
    const auto file = parse_recovering(tokens, diagnostics);
    CHECK(!diagnostics.empty() && diagnostics[0].code() == "E0241");
    CHECK(file.statements.size() == 1);
    CHECK(file.statements[0].declaration->name == "next");
    CHECK(file.statements[0].declaration->span.line == 3);
}

void overloads() {
    const auto file = parse(lex(
        "let row = html(value: int) { {value} } "
        "or html(value: string) { <p>{value}</p> } "
        "or html() from \"./row.html\";",
        "overloads.nra"));
    const auto& primary = *initializer(file, 0).html_factory;
    CHECK(primary.parameters[0].type.name == "int");
    CHECK(primary.overloads.size() == 2);
    CHECK(primary.overloads[0].html_factory->parameters[0].type.name == "string");
    CHECK(primary.overloads[1].html_factory->parameters.empty());
    CHECK(std::get<Token>(primary.overloads[1].html_factory->source).text == "\"./row.html\"");
}

void template_digest() {
    const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("template-overlay"));
    const auto source = root / "page.nra";
    const auto external = root / "view.html";
    const std::map<std::string, std::string, std::less<>> overlay{
        {source.generic_string(), "let view = html() from \"./view.html\";"},
        {external.generic_string(), "\xEF\xBB\xBF<p>Ready</p>\r\n"},
    };
    SourceMap sources;
    SourceMapScope source_map(sources);
    SourceOverlayScope documents(overlay);
    const auto files = read_sources({source}, {});
    const auto& template_source = *initializer(files.at(0), 0).html_factory->external;
    CHECK(template_source.sha256 == "e2ca32dec9451274a62f98dde17cabb3eecec095e606a5a4862c743a01ffdd22");
    CHECK(template_source.body.span.file == external.generic_string());
    CHECK(template_source.body.span.offset == 3);
    CHECK(template_source.body.span.column == 1);
    CHECK(text(element(template_source.body.nodes[0]).children[0]) == "Ready");
}

} // namespace

int main() {
    try {
        fragments();
        control_flow();
        contextual_forms();
        recovery();
        overloads();
        template_digest();
    } catch (const neura::Diagnostic& error) {
        std::cerr << error.code() << " at " << error.span.line << ':' << error.span.column << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

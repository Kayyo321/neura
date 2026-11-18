// Lexer-level tests for the HTML and CSS island boundary. Parser validation
// of the contents belongs to the template and stylesheet tests.
#include "lexer/lexer.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace neura;

int failures = 0;
void check(bool value, std::string_view message, int line) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << message << '\n';
    }
}
#define CHECK(value) check((value), #value, __LINE__)

void island(std::string_view prefix, std::string_view body, std::string_view suffix,
            TokenKind kind) {
    const std::string source = std::string(prefix) + "{" + std::string(body) + "}" + std::string(suffix);
    const auto tokens = lex(source, "island.nra");
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i] == '\r') {
            ++line;
            column = 1;
        } else if (prefix[i] == '\n') {
            if (i == 0 || prefix[i - 1] != '\r') ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    std::size_t count = 0;
    for (const auto& token : tokens) {
        if (token.kind != kind) continue;
        ++count;
        CHECK(token.text == body);
        CHECK(token.span.file == "island.nra");
        CHECK(token.span.offset == prefix.size());
        CHECK(token.span.line == line);
        CHECK(token.span.column == column);
        CHECK(token.span.length == body.size() + 2);
        CHECK(source.substr(token.span.offset, token.span.length) == "{" + std::string(body) + "}");
    }
    CHECK(count == 1);
    CHECK(tokens.back().kind == TokenKind::End);
}

void boundaries() {
    // CRLF and UTF-8 are bytes in the token, not normalized source text.
    island("let view = html(x: string)\r\n  ",
           "\r\n<p title=\"{not a hole}\">caf\xC3\xA9</p>\r\n", ";\n",
           TokenKind::HtmlIsland);
    // Parentheses in function types and strings must not end the parameter list.
    island("let f = html(cb: func(string) -> int, x: string = \"{)\",) // }\n",
           "<p title='{x}'>ok</p>", ";", TokenKind::HtmlIsland);
    island("let f = html() ",
           "<!-- } { ignored --> <p>{call(\"}\", /* } */ 1 // }\n)}</p>",
           ";", TokenKind::HtmlIsland);
    island("let f = new html() ", "<p>{label(\"}\")}</p>", ";", TokenKind::HtmlIsland);
    island("let f = html() ", "caf\xC3\xA9@work{label(\"}\")}", ";", TokenKind::HtmlIsland);
    island("let f = html(from, key: string) ",
           R"nra(<p title={$"{label("}")} parts"} data-end={'}'}>don't quote ops@parts.example</p>)nra",
           ";", TokenKind::HtmlIsland);
    island("let f = html() ",
           "@if (test(\"}\")) { <p>{func() { return \"}\"; }}</p> "
           "@if (true) { <span>{x}</span> } } @else { <p>no</p> }",
           ";", TokenKind::HtmlIsland);
    island("const styles = css ",
           "\r\n.a { content: \"}\"; /* } */ background: url(data:x,}); "
           "--shape: { a: b }; }\r\n", ";", TokenKind::CssIsland);
    island("const styles = css.module ",
           "@media (min-width: 1px) { .a { color: red; } }", ";", TokenKind::CssIsland);
    island("const styles = css ", " ", ";", TokenKind::CssIsland);
    island("const styles = css ",
           R"css(.a { background: u\72l(data:x,{), URL(data:x,\)), url("}"); --value: \}; })css",
           ";", TokenKind::CssIsland);
    // A later declaration-shaped line is part of a complete body.
    island("let f = html() ", "\nlet verbatim\n", ";", TokenKind::HtmlIsland);
    // Malformed forms still yield an island for the parser's diagnostic.
    island("let f = html<int>(x: int) -> View ", "<p>x</p>", ";", TokenKind::HtmlIsland);
    island("let f = html(x: int) -> (func(int) -> ui.View)[] ", "<p>x</p>", ";", TokenKind::HtmlIsland);
    island("let f = html ", "<p>x</p>", ";", TokenKind::HtmlIsland);
    island("const s = css.global ", ".a {}", ";", TokenKind::CssIsland);
    island("use(new css ", ".a {}", ");", TokenKind::CssIsland);
}

void legacy() {
    const std::string source =
        "let html = 3; let css = 1; html(x); page.html(x); theme.css; "
        "html<int>(x); if (html(a)) { x(); } "
        "match (v) { css { a, b } => a, html { a } => a } "
        "let f = func() -> html { return x; }; use(first, css { 1 .. 3 });";
    const std::vector<std::string> expected = {
        "let", "html", "=", "3", ";", "let", "css", "=", "1", ";",
        "html", "(", "x", ")", ";", "page", ".", "html", "(", "x", ")", ";",
        "theme", ".", "css", ";", "html", "<", "int", ">", "(", "x", ")", ";",
        "if", "(", "html", "(", "a", ")", ")", "{", "x", "(", ")", ";", "}",
        "match", "(", "v", ")", "{", "css", "{", "a", ",", "b", "}", "=>", "a", ",",
        "html", "{", "a", "}", "=>", "a", "}", "let", "f", "=", "func", "(", ")",
        "->", "html", "{", "return", "x", ";", "}", ";",
        "use", "(", "first", ",", "css", "{", "1", "..", "3", "}", ")", ";"
    };
    const auto tokens = lex(source, "legacy.nra");
    CHECK(tokens.size() == expected.size() + 1);
    for (std::size_t i = 0; i < expected.size() && i < tokens.size(); ++i) {
        CHECK(tokens[i].text == expected[i]);
        if (expected[i] == "html" || expected[i] == "css") CHECK(tokens[i].kind == TokenKind::Identifier);
        CHECK(tokens[i].kind != TokenKind::HtmlIsland);
        CHECK(tokens[i].kind != TokenKind::CssIsland);
    }
}

void unterminated(std::string_view prefix, std::string_view body, std::string_view suffix, TokenKind kind) {
    const auto source = std::string(prefix) + "{" + std::string(body) + std::string(suffix);
    bool threw = false;
    try {
        (void)lex(source, "incomplete.nra");
    } catch (const Diagnostic& diagnostic) {
        threw = true;
        CHECK(diagnostic.code() == "E0241");
        CHECK(diagnostic.span.offset == prefix.size());
        CHECK(diagnostic.span.length == 1);
    }
    CHECK(threw);
    std::vector<Diagnostic> diagnostics;
    const auto tokens = lex_recovering(source, "incomplete.nra", diagnostics);
    CHECK(diagnostics.size() == 1);
    if (!diagnostics.empty()) CHECK(diagnostics.front().code() == "E0241");
    bool found_island = false;
    bool found_next = false;
    for (const auto& token : tokens) {
        if (token.kind == kind) {
            found_island = true;
            CHECK(token.text == body);
            CHECK(token.span.offset == prefix.size());
            CHECK(token.span.length == body.size() + 1);
        }
        if (token.text == "next" && token.kind == TokenKind::Identifier) {
            found_next = true;
            CHECK(token.span.offset == prefix.size() + body.size() + 1 + suffix.find("next"));
        }
    }
    CHECK(found_island);
    CHECK(found_next == (suffix.find("next") != std::string_view::npos));
    CHECK(tokens.back().kind == TokenKind::End);
}
} // namespace

int main() {
    try {
        boundaries();
        legacy();
        unterminated("let broken = html() ", "\n  unfinished\n", "let next = 1;\n", TokenKind::HtmlIsland);
        unterminated("const broken = css ", "\n  unfinished\n", "const next = 1;\n", TokenKind::CssIsland);
        unterminated("    let broken =\r\n      html() ",
                     "\r\n        <butt\r\n      let deeper = 1;\r\n    letter\r\n",
                     "    last next: int;\r\n", TokenKind::HtmlIsland);
        unterminated("    let broken = html() ", "\n        <!-- unfinished\n",
                     "}\nexport let next = 1;\n", TokenKind::HtmlIsland);
        unterminated("let broken = html() ", " unfinished", "", TokenKind::HtmlIsland);
    } catch (const neura::Diagnostic& diagnostic) {
        ++failures;
        std::cerr << "unexpected " << diagnostic.code() << ": " << diagnostic.what() << '\n';
    }
    return failures == 0 ? 0 : 1;
}

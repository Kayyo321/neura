#include "lexer/lexer.hpp"
#include "parser/css_stylesheet.hpp"
#include "parser/html_template.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace neura;

void check(bool value, std::string_view label) {
    if (!value) throw std::runtime_error(std::string(label));
}

template <typename Operation>
Diagnostic reject(Operation operation, std::string_view code, std::string_view reason) {
    try {
        operation();
    } catch (const Diagnostic& error) {
        check(error.data() && error.data()->code == code, std::string("wrong diagnostic: ") + error.what());
        check(error.data()->primary_label.find(reason) != std::string::npos, "wrong diagnostic reason");
        return error;
    }
    throw std::runtime_error("accepted a malformed stylesheet");
}

RewrittenStylesheet rewrite(std::string_view source, std::string_view key = "Stock.Ui.Inline") {
    return rewrite_stylesheet(source, parse_stylesheet(source, {"stock.css"}, true), "stock", key);
}

void unchanged() {
    const std::string bytes =
        "/* .row and a literal } */\r\n"
        "@import url('local.css') layer(theme) screen;\r\n"
        "@container (width > 5px) { .row:is(a,[value='.row']) {\r\n"
        "  --shape: { a: b; c: .row; };\r\n"
        "  content: \"} \\\" .row\"; unknown-feature: [ value (\"{\") ];\r\n"
        "  background: url(data:image/svg+xml,{.row});\r\n"
        "  -vendor-unknown: 1.5px;\r\n"
        "} }\r\n";
    const auto parsed = parse_stylesheet(bytes, {"main.nra", 3, 9, 21}, false);
    check(parsed.span.file == "main.nra" && parsed.span.offset == 21 && parsed.span.length == bytes.size(), "body span");
    const auto result = rewrite_stylesheet(bytes, parsed, "stock", "Main.Plain");
    check(result.text == bytes && result.classes.empty() && result.keyframes.empty(), "ordinary CSS byte identity");
    check(rewrite("").text.empty(), "empty module");
}

void module_text() {
    constexpr std::string_view css = R"CSS(/* .row */
@layer utility {
  @media (width > 1px) {
    @scope (.scope) to (:global(.reset)) {
      :global(.reset, [data-note=".row"]) .row:is(.chip, :global(.no :local(.literal))) {
        content: ".row :global(.class)";
        color: unknown(.row);
        --animation: { fade; .row { animation-name: fade; } };
        background: url(data:image/svg+xml,{.row});
        cursor: url("file .row.svg"), auto;
        animation-name: fade, "fade", missing, var(--x, fade) ! /* stay */ important;
        animation: 100ms linear linear, 1e-2s steps(2, end) fade, 1s var(--duration) fade, 1s "fade";
        & > .nested, p:hover .row { animation-name: linear; }
      }
    }
  }
}
@supports selector(.active:global(.on)) { .row { } }
@keyframes fade { from { color: black; } 50% { color: red; } }
@KEYFRAMES linear { to { opacity: 1; } }
@keyframes "quoted" { from { } }
/* .row */
)CSS";
    constexpr std::string_view expected = R"CSS(/* .row */
@layer utility {
  @media (width > 1px) {
    @scope (.scope__uq7xylblmx) to (.reset) {
      .reset, [data-note=".row"] .row__uswtdzxqnm:is(.chip__ww3xqrj6sm, .no :local(.literal)) {
        content: ".row :global(.class)";
        color: unknown(.row);
        --animation: { fade; .row { animation-name: fade; } };
        background: url(data:image/svg+xml,{.row});
        cursor: url("file .row.svg"), auto;
        animation-name: fade__ekuyxlbj5c, "fade", missing, var(--x, fade) ! /* stay */ important;
        animation: 100ms linear linear__6efms76knk, 1e-2s steps(2, end) fade__ekuyxlbj5c, 1s var(--duration) fade__ekuyxlbj5c, 1s "fade";
        & > .nested__p55t3hpgmc, p:hover .row__uswtdzxqnm { animation-name: linear__6efms76knk; }
      }
    }
  }
}
@supports selector(.active__yzgzua5lnp.on) { .row__uswtdzxqnm { } }
@keyframes fade__ekuyxlbj5c { from { color: black; } 50% { color: red; } }
@KEYFRAMES linear__6efms76knk { to { opacity: 1; } }
@keyframes "quoted" { from { } }
/* .row */
)CSS";
    const auto result = rewrite(css);
    if (result.text != expected) {
        std::cerr << result.text;
        throw std::runtime_error("selected token edits preserve all other bytes");
    }
    check(result.classes.size() == 5 && result.classes.at("row") == "row__uswtdzxqnm", "local class exports");
    check(result.keyframes.size() == 2 && result.keyframes.at("fade") == "fade__ekuyxlbj5c", "declared animation exports");
}

void escapes_and_vectors() {
    constexpr std::string_view bytes = R"CSS(./**/r\6f w, .row, .é, .a\ b {
  .\e9  { --keep: { ".row"; selector(.literal); }; }
  animation-name: f\61 de;
  animation: .25s ease-in f\61 de;
  background: U\52L(data:opaque\)end);
}
:/**/global(.row) {}
@keyframes f\61 de {}
)CSS";
    constexpr std::string_view expected = R"CSS(./**/r\6f w__jmfjjl4ndf, .row__jmfjjl4ndf, .é__32s37ksvsx, .a\ b__rrsigcl4ab {
  .\e9 __32s37ksvsx { --keep: { ".row"; selector(.literal); }; }
  animation-name: f\61 de__di2xipzlvh;
  animation: .25s ease-in f\61 de__di2xipzlvh;
  background: U\52L(data:opaque\)end);
}
/**/.row {}
@keyframes f\61 de__di2xipzlvh {}
)CSS";
    const auto result = rewrite(bytes, "ui/é.module.css");
    if (result.text != expected) {
        std::cerr << result.text;
        throw std::runtime_error("CSS escapes preserve spelling while sharing a decoded export");
    }
    check(result.classes.size() == 3 && result.classes.at("a b") == "a b__rrsigcl4ab", "decoded CSS name");
    check(result.keyframes.at("fade") == "fade__di2xipzlvh", "independent file and keyframe vectors");
    check(rewrite(".row { color: red; }").classes == rewrite(".row { color: blue; }").classes, "content-independent name");
    const auto parsed = parse_stylesheet(".row {}", {"keys.css"}, true);
    const auto one = rewrite_stylesheet(".row {}", parsed, "ab", "c.css");
    const auto two = rewrite_stylesheet(".row {}", parsed, "a", "bc.css");
    check(one.classes.at("row") == "row__airtfscf4u" && two.classes.at("row") == "row__hvcwrmxrnt",
          "NUL separators distinguish package and key boundaries");
}

void diagnostics() {
    constexpr std::string_view outside = "external.css";
    const auto structural = [&](std::string_view css, std::string_view token, std::string_view reason) {
        const auto error = reject([&] { parse_stylesheet(css, {std::string(outside)}, false); }, "E0253", reason);
        check(error.span.file == outside && error.span.length == token.size() &&
                  css.substr(error.span.offset, error.span.length) == token, "structural CSS span");
    };
    structural(".row { content: \"unfinished", "\"", "closing quote");
    structural(".row { content: 'bad\r\n' }", "'", "closing quote");
    structural(".row { /* unterminated", "/*", "closing '*/'");
    structural(".row { background: url(data:{abc}", "url(", "closing ')'");
    structural(".row { background: url(\"item\" }", "}", "does not match");
    structural(".row { color: red; ", "{", "close this delimiter");
    structural(".row [color=green) { }", ")", "does not match");

    const auto malformed = [&](std::string_view css, std::string_view token, std::string_view reason) {
        const auto error = reject([&] { parse_stylesheet(css, {"practice.module.css"}, true); }, "E0256", reason);
        check(css.substr(error.span.offset, error.span.length) == token, "CSS module syntax span");
    };
    malformed(":global .row {}", ":global", "parenthesized selector");
    malformed(".row:LOCAL(.chip) {}", ":LOCAL", "already make class names local");
    malformed(".row:is(.chip,:local) {}", ":local", "already make class names local");
    constexpr std::string_view bad = ".row {\r\n  é: bad; ] }";
    const auto offset_error = reject([&] { parse_stylesheet(bad, {"inline.nra", 7, 13, 90}, true); },
                                     "E0253", "does not match");
    check(offset_error.span.file == "inline.nra" && offset_error.span.line == 8 && offset_error.span.column == 11 &&
              offset_error.span.offset == 90 + bad.find(']') && offset_error.span.length == 1, "CRLF and UTF-8 positions");
}

void parser_boundary() {
    constexpr std::string_view bytes =
        "const Style = css.module {\r\n  .row { animation-name: fade; }\r\n  @keyframes fade {}\r\n};\r\n"
        "const From = css.module from \"./style.css\";";
    const auto file = parse(lex(bytes, "inline.nra"));
    const auto& sheet = *file.statements.at(0).declaration->initializer->stylesheet;
    const auto& external = *file.statements.at(1).declaration->initializer->stylesheet;
    check(sheet.body && sheet.body->span.offset == bytes.find('{') + 1 && sheet.body->names.size() == 3, "inline parser stores body");
    check(external.source.kind == TokenKind::String && !external.body, "external source is handled by its caller");
    check(rewrite_stylesheet(sheet.source.text, *sheet.body, "stock", "Stock.Ui.Inline").classes.at("row") == "row__uswtdzxqnm",
          "inline analysis feeds rewriting");

    constexpr std::string_view framed = "const Style = css { .row { color: red; ] } };";
    const auto inner = reject([&] { parse(lex(framed, "inline.nra")); }, "E0253", "does not match");
    check(inner.span.offset == framed.find(']'), "framed inline failure at the inner token");
    constexpr std::string_view unframed = "const Style = css { .row { color: red; }";
    const auto outer = reject([&] { parse(lex(unframed, "inline.nra")); }, "E0241", "no closing");
    check(outer.span.offset == unframed.find('{'), "lexer owns a missing outer delimiter");
    reject([&] { parse(lex("let broken = new css.module { :local(.row) {} };", "inline.nra")); },
           "E0255", "module-level 'const'");
}

} // namespace

int main() {
    try {
        unchanged();
        module_text();
        escapes_and_vectors();
        diagnostics();
        parser_boundary();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stylesheet parser: " << error.what() << '\n';
        return 1;
    }
}

// Unit tests for the diagnostics core: the warning sink and its speculative
// guard, the source map, the human, short, and JSON renderers, display
// widths, and the edit application that `neura check --fix` uses. The
// rendered form of real compiler errors is covered by the UI tests in
// tests/ui (scripts/ui-test.py).
//
// Usage: neura_diagnostic_tests <sink|source-map|render|width|edits>

#include "cli/diagnostic_render.hpp"
#include "util/diagnostic.hpp"
#include "util/diagnostic_codes.hpp"
#include "util/source_map.hpp"
#include "util/suggest.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace neura;

int failures = 0;

void check(bool condition, const std::string& what, int line) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL line " << line << ": " << what << '\n';
}

#define CHECK(condition) check((condition), #condition, __LINE__)

void check_text(const std::string& actual, const std::string& expected, int line) {
    if (actual == expected) return;
    ++failures;
    std::cout << "FAIL line " << line << ": got\n" << actual << "expected\n" << expected;
}

Span at(std::string file, std::size_t line, std::size_t column, std::size_t offset, std::size_t length) {
    return {std::move(file), line, column, offset, length};
}

int sink_suite() {
    // Without an installed sink, a warning is dropped.
    CHECK(DiagnosticSink::current() == nullptr);
    report_warning(Diagnostic::warning(at("a.nra", 1, 1, 0, 1), "unused", "dropped"));

    DiagnosticSink sink;
    {
        const DiagnosticSinkScope scope(sink);
        CHECK(DiagnosticSink::current() == &sink);
        report_warning(Diagnostic::warning(at("a.nra", 1, 1, 0, 1), "unused", "kept"));
        {
            // A speculative region that fails discards what it reported.
            SpeculativeDiagnostics speculative;
            report_warning(Diagnostic::warning(at("a.nra", 2, 1, 5, 1), "unused", "abandoned"));
            CHECK(sink.size() == 2);
        }
        CHECK(sink.size() == 1);
        {
            // A committed one keeps it.
            SpeculativeDiagnostics speculative;
            report_warning(Diagnostic::warning(at("a.nra", 3, 1, 9, 1), "unused", "committed"));
            speculative.commit();
        }
        CHECK(sink.size() == 2);
        // Nested regions: the inner one's discard leaves the outer one's work.
        {
            SpeculativeDiagnostics outer;
            report_warning(Diagnostic::warning(at("a.nra", 4, 1, 12, 1), "unused", "outer"));
            {
                SpeculativeDiagnostics inner;
                report_warning(Diagnostic::warning(at("a.nra", 5, 1, 14, 1), "unused", "inner"));
            }
            CHECK(sink.size() == 3);
            outer.commit();
        }
        CHECK(sink.size() == 3);
    }
    CHECK(DiagnosticSink::current() == nullptr);
    CHECK(std::string(sink.entries().at(0).what()) == "kept");
    CHECK(sink.entries().at(0).severity() == Severity::Warning);
    CHECK(sink.entries().at(0).name() == "unused");

    // The printer sorts warnings by file and offset, filters -Wno-<name>, and
    // hides library warnings unless asked.
    SourceMap sources;
    sources.add("lib.nra", "let a = 1;\n", SourceKind::Library);
    sources.add("b.nra", "let b = 1;\n", SourceKind::User);
    std::vector<Diagnostic> warnings{
        Diagnostic::warning(at("b.nra", 1, 5, 4, 1), "style", "second"),
        Diagnostic::warning(at("b.nra", 1, 1, 0, 3), "style", "first"),
        Diagnostic::warning(at("b.nra", 1, 9, 8, 1), "noisy", "disabled"),
        Diagnostic::warning(at("lib.nra", 1, 1, 0, 3), "style", "library"),
    };
    cli::DiagnosticConfig config;
    config.format = cli::ErrorFormat::Short;
    config.warnings.emplace("noisy", false);
    std::ostringstream out;
    cli::DiagnosticPrinter printer(config, {}, sources);
    CHECK(printer.warnings(out, warnings) == 2);
    check_text(out.str(), "b.nra:1:1: warning: first\nb.nra:1:5: warning: second\n", __LINE__);

    config.library_warnings = true;
    config.warnings_are_errors = true;
    std::ostringstream all;
    cli::DiagnosticPrinter strict(config, {}, sources);
    CHECK(strict.warnings(all, warnings) == 3);
    check_text(all.str(), "b.nra:1:1: error: first\nb.nra:1:5: error: second\nlib.nra:1:1: error: library\n", __LINE__);

    // After coded errors, the human form ends with one line that points to
    // neura explain; the short form has none.
    cli::DiagnosticConfig human;
    human.format = cli::ErrorFormat::Human;
    std::ostringstream one;
    cli::DiagnosticPrinter single(human, {}, sources);
    single.print(one, Diagnostic(at("b.nra", 1, 1, 0, 3), "first").with_code("E0302"));
    single.print(one, Diagnostic(at("b.nra", 1, 5, 4, 1), "again").with_code("E0302"));
    single.finish(one);
    CHECK(one.str().ends_with("\n\nFor more information about this error, try `neura explain E0302`.\n"));
    std::ostringstream several;
    cli::DiagnosticPrinter mixed(human, {}, sources);
    mixed.print(several, Diagnostic(at("b.nra", 1, 5, 4, 1), "b").with_code("E0403"));
    mixed.print(several, Diagnostic(at("b.nra", 1, 1, 0, 3), "a").with_code("E0201"));
    mixed.print(several, Diagnostic(at("b.nra", 1, 1, 0, 3), "uncoded"));
    mixed.finish(several);
    CHECK(several.str().ends_with("\nSome errors have detailed explanations: E0201, E0403.\n"
                                  "For more information about an error, try `neura explain E0201`.\n"));
    std::ostringstream brief;
    cli::DiagnosticPrinter short_form(config, {}, sources);
    short_form.print(brief, Diagnostic(at("b.nra", 1, 1, 0, 3), "first").with_code("E0302"));
    short_form.finish(brief);
    check_text(brief.str(), "b.nra:1:1: error[E0302]: first\n", __LINE__);

    // Errors that a recovering stage carries keep their order.
    auto first = Diagnostic(at("b.nra", 1, 1, 0, 3), "first").with_code("E0101");
    first.also(Diagnostic(at("b.nra", 1, 5, 4, 1), "second").with_code("E0101"));
    CHECK(first.following().size() == 1 && std::string(first.following().front().what()) == "second");
    CHECK(Diagnostic(at("b.nra", 1, 1, 0, 3), "plain").following().empty());

    // Warnings that a #Deny tag made errors leave the sink as errors, and a
    // warning reported twice for one construct is kept once; entries before
    // the mark are not touched.
    DiagnosticSink tags;
    tags.report(Diagnostic::warning(at("b.nra", 1, 1, 0, 3), "deprecated", "before").with_code("W0001"));
    tags.report(Diagnostic::warning(at("b.nra", 1, 1, 0, 3), "deprecated", "before").with_code("W0001"));
    const auto mark = tags.size();
    tags.report(Diagnostic::warning(at("b.nra", 1, 5, 4, 1), "deprecated", "use").with_code("W0001"));
    tags.report(Diagnostic::warning(at("b.nra", 1, 5, 4, 1), "deprecated", "use").with_code("W0001"));
    auto denied = Diagnostic::warning(at("b.nra", 1, 9, 8, 1), "experimental", "denied").with_code("W0002");
    denied.edit().severity = Severity::Error;
    tags.report(denied);
    tags.report(Diagnostic::warning(at("b.nra", 1, 9, 8, 1), "experimental", "other").with_code("W0002"));
    tags.deduplicate(mark);
    CHECK(tags.size() == mark + 3);
    const auto errors = tags.take_errors(mark);
    CHECK(errors.size() == 1 && std::string(errors.front().what()) == "denied");
    CHECK(tags.size() == mark + 2);
    CHECK(std::string(tags.entries().at(mark).what()) == "use");
    CHECK(std::string(tags.entries().at(mark + 1).what()) == "other");
    CHECK(std::string(tags.entries().at(1).what()) == "before");

    // Warning codes are registered with their names, and explain finds them.
    CHECK(find_warning("deprecated") && find_warning("deprecated")->code == "W0001");
    CHECK(find_warning("W0003") && find_warning("W0003")->name == "unused-result");
    CHECK(find_warning("E0101") == nullptr);
    CHECK(diagnostic_title("W0004") == "#Inline(always: true) function that cannot be inlined");
    return failures;
}

int source_map_suite() {
    SourceFile file("f.nra", "one\r\ntwo\rthree\n", SourceKind::User, {});
    CHECK(file.line_count() == 4);
    CHECK(file.line_text(0) == "one");
    CHECK(file.line_text(1) == "two");
    CHECK(file.line_text(2) == "three");
    CHECK(file.line_text(3).empty());
    CHECK(file.line_of(0) == 0);
    CHECK(file.line_of(4) == 0);
    CHECK(file.line_of(5) == 1);
    CHECK(file.line_of(9) == 2);
    CHECK(file.line_of(100) == 3);

    SourceMap map;
    map.add("f.nra", "x", SourceKind::Generated, "f.h");
    CHECK(map.find("f.nra")->origin() == "f.h");
    map.add("f.nra", "y", SourceKind::User);
    CHECK(map.find("f.nra")->text() == "y");
    CHECK(map.find("g.nra") == nullptr);
    map.clear();
    CHECK(map.find("f.nra") == nullptr);

    CHECK(diagnostic_title("E0201") == "expected a semicolon");
    CHECK(diagnostic_title("E9999").empty());
    return failures;
}

int render_suite() {
    SourceMap sources;
    sources.add("main.nra", "let x = 5\nlet y = 6;\n", SourceKind::User);
    sources.add("other.nra", "\tconst limit = 3;\n", SourceKind::User);
    sources.add("gen.nra", "let a = b;\n", SourceKind::Generated, "/usr/include/thing.h");

    // A plain diagnostic in the old form keeps its short form unchanged.
    const Diagnostic plain(at("main.nra", 2, 5, 14, 1), "unknown name 'y'");
    check_text(cli::render_short(plain), "main.nra:2:5: error: unknown name 'y'\n", __LINE__);
    check_text(cli::render_human(plain, sources, {}),
               "error: unknown name 'y'\n"
               " --> main.nra:2:5\n"
               "  |\n"
               "2 | let y = 6;\n"
               "  |     ^\n",
               __LINE__);

    // A coded error with a suggestion: an insertion shows the edited line.
    auto missing = Diagnostic(at("main.nra", 1, 10, 9, 0), "expected ';'").with_code("E0201").label("here");
    missing.suggest("add it", at("main.nra", 1, 10, 9, 0), ";", Applicability::MachineApplicable);
    check_text(cli::render_short(missing), "main.nra:1:10: error[E0201]: expected ';'\n", __LINE__);
    check_text(cli::render_human(missing, sources, {}),
               "error[E0201]: expected ';'\n"
               " --> main.nra:1:10\n"
               "  |\n"
               "1 | let x = 5\n"
               "  |          ^ here\n"
               "  |\n"
               "help: add it\n"
               "  |\n"
               "1 | let x = 5;\n"
               "  |          +\n",
               __LINE__);

    // A secondary label in another file, after a tab (four columns), notes,
    // and a removal shown under the original line.
    auto assign = Diagnostic(at("main.nra", 1, 5, 4, 1), "cannot assign").with_code("E0401").label("assigned");
    assign.label(at("other.nra", 1, 2, 1, 5), "declared here").note("first\nsecond");
    assign.suggest("remove it", at("main.nra", 1, 1, 0, 4), "", Applicability::MaybeIncorrect);
    check_text(cli::render_human(assign, sources, {}),
               "error[E0401]: cannot assign\n"
               " --> main.nra:1:5\n"
               "  |\n"
               "1 | let x = 5\n"
               "  |     ^ assigned\n"
               " ::: other.nra:1:2\n"
               "  |\n"
               "1 |     const limit = 3;\n"
               "  |     ----- declared here\n"
               "  |\n"
               "  = note: first\n"
               "          second\n"
               "help: remove it\n"
               "  |\n"
               "1 | let x = 5\n"
               "  | ----\n",
               __LINE__);

    // A span over several lines underlines its first line and marks the
    // continuation; a skipped single line is shown, a longer gap is "...".
    sources.add("long.nra", "a = (1 +\n  2);\nb\nc\nd\ne\n", SourceKind::User);
    auto wide = Diagnostic(at("long.nra", 1, 5, 4, 10), "spans lines").label("this");
    wide.label(at("long.nra", 3, 1, 15, 1), "skip one");
    wide.label(at("long.nra", 6, 1, 21, 1), "far");
    check_text(cli::render_human(wide, sources, {}),
               "error: spans lines\n"
               " --> long.nra:1:5\n"
               "  |\n"
               "1 | a = (1 +\n"
               "  |     ^^^^... this\n"
               "2 |   2);\n"
               "3 | b\n"
               "  | - skip one\n"
               "...\n"
               "6 | e\n"
               "  | - far\n",
               __LINE__);

    // A file that is not in the map shows its location only; a generated
    // file names its header.
    const Diagnostic unknown(at("missing.nra", 3, 4, 20, 1), "lost");
    check_text(cli::render_human(unknown, sources, {}), "error: lost\n --> missing.nra:3:4\n", __LINE__);
    const Diagnostic generated(at("gen.nra", 1, 9, 8, 1), "unknown name 'b'");
    check_text(cli::render_human(generated, sources, {}),
               "error: unknown name 'b'\n"
               " --> gen.nra:1:9\n"
               "  |\n"
               "1 | let a = b;\n"
               "  |         ^\n"
               "  |\n"
               "  = note: this module was generated from the C header '/usr/include/thing.h'\n",
               __LINE__);

    // A long line is trimmed around the label.
    std::string line = "let s = \"" + std::string(120, 'a') + "\"; let t = q;\n";
    sources.add("wide.nra", line, SourceKind::User);
    const auto offset = line.find('q');
    const Diagnostic trimmed(at("wide.nra", 1, offset + 1, offset, 1), "unknown name 'q'");
    const auto rendered = cli::render_human(trimmed, sources, {false, 60});
    CHECK(rendered.find("1 | ...aaa") != std::string::npos);
    CHECK(rendered.find("\"; let t = q;\n") != std::string::npos);
    const auto caret_row = rendered.substr(rendered.rfind("  | "));
    const auto source_row = rendered.substr(rendered.find("1 | "));
    CHECK(caret_row.find('^') == source_row.find('q'));

    // Color wraps the parts in SGR sequences only when asked.
    CHECK(cli::render_human(plain, sources, {true, 100}).starts_with("\x1b[1;31merror\x1b[0m"));
    CHECK(cli::render_human(plain, sources, {false, 100}).find('\x1b') == std::string::npos);

    // JSON is one line with every span, suggestion, and argument.
    auto structured = missing;
    structured.arg("kind", "name").arg("name", "x \"q\"");
    const auto json = cli::render_json(structured, sources, 100);
    CHECK(json.find('\n') == json.size() - 1);
    CHECK(json.starts_with("{\"code\":\"E0201\",\"severity\":\"error\",\"message\":\"expected ';'\",\"spans\":[{"
                           "\"file\":\"main.nra\",\"offset\":9,\"length\":0,\"line\":1,\"column\":10,"
                           "\"primary\":true,\"label\":\"here\"}]"));
    CHECK(json.find("\"applicability\":\"MachineApplicable\",\"edits\":[{\"file\":\"main.nra\",\"offset\":9,"
                    "\"length\":0,\"line\":1,\"column\":10,\"replacement\":\";\"}]") != std::string::npos);
    CHECK(json.find("\"args\":{\"kind\":\"name\",\"name\":\"x \\\"q\\\"\"}") != std::string::npos);
    CHECK(json.find("\"rendered\":\"error[E0201]: expected ';'\\n --> main.nra:1:10\\n") != std::string::npos);
    CHECK(cli::render_json(plain, sources, 100).starts_with("{\"code\":null,"));

    // Copying a diagnostic and adding a note leaves the original unchanged.
    auto copy = missing;
    copy.note("added");
    CHECK(copy.data()->notes.size() == 1);
    CHECK(missing.data()->notes.empty());
    return failures;
}

int width_suite() {
    CHECK(cli::display_width(U'a') == 1);
    CHECK(cli::display_width(U'é') == 1);
    CHECK(cli::display_width(U'́') == 0);   // combining acute accent
    CHECK(cli::display_width(U'​') == 0);   // zero width space
    CHECK(cli::display_width(U'日') == 2);   // CJK ideograph
    CHECK(cli::display_width(U'가') == 2);   // Hangul syllable
    CHECK(cli::display_width(U'Ａ') == 2);   // fullwidth A
    CHECK(cli::display_width(U'\U0001F600') == 2);
    CHECK(cli::display_width(U'─') == 1);   // box drawing stays narrow

    // Columns under wide text and tabs.
    SourceMap sources;
    sources.add("w.nra", "\t\"日本\" x\n", SourceKind::User);
    const Diagnostic after(at("w.nra", 1, 7, 10, 1), "here");
    check_text(cli::render_human(after, sources, {}),
               "error: here\n"
               " --> w.nra:1:7\n"
               "  |\n"
               "1 |     \"日本\" x\n"
               "  | " + std::string(11, ' ') + "^\n",
               __LINE__);
    return failures;
}

int edits_suite() {
    std::string text = "let a = 1\nlet b = 2\n";
    // Applied back to front whatever their order, duplicates once.
    const auto count = cli::apply_edits(text, {{at("f", 1, 10, 9, 0), ";"},
                                               {at("f", 2, 10, 19, 0), ";"},
                                               {at("f", 1, 10, 9, 0), ";"}});
    CHECK(count == 2);
    CHECK(text == "let a = 1;\nlet b = 2;\n");
    // Overlapping and out-of-range edits are skipped.
    std::string other = "abcdef";
    CHECK(cli::apply_edits(other, {{at("f", 1, 1, 1, 3), "X"}, {at("f", 1, 1, 2, 3), "Y"}, {at("f", 1, 1, 10, 0), "Z"}}) == 1);
    CHECK(other == "abYf");

    // A suggestion that inserts whole lines shows each new line.
    SourceMap sources;
    sources.add("m.nra", "match (x) {\n    A.B => {},\n}\n", SourceKind::User);
    const auto insertion = Diagnostic(at("m.nra", 1, 1, 0, 5), "missing")
        .suggest("add the arms", at("m.nra", 3, 1, 27, 0), "    A.C => {},\n    A.D => {},\n",
                 Applicability::HasPlaceholders);
    check_text(cli::render_human(insertion, sources, {}),
               "error: missing\n"
               " --> m.nra:1:1\n"
               "  |\n"
               "1 | match (x) {\n"
               "  | ^^^^^\n"
               "  |\n"
               "help: add the arms\n"
               "  |\n"
               "3 |     A.C => {},\n"
               "  |     ++++++++++\n"
               "4 |     A.D => {},\n"
               "  |     ++++++++++\n",
               __LINE__);

    // "Did you mean": a swap of two letters is one edit, a case difference
    // is none, and nothing is suggested for a distant or exact name.
    CHECK(edit_distance("cuont", "count") == 1);
    CHECK(edit_distance("kitten", "sitting") == 3);
    CHECK(closest_name("cuont", {"total", "count"}) == std::optional<std::string>("count"));
    CHECK(closest_name("Count", {"amount", "count"}) == std::optional<std::string>("count"));
    CHECK(!closest_name("x", {"total", "count"}));
    CHECK(!closest_name("count", {"count", "counts"}));
    CHECK(!closest_name("cap", {"$cap"}));
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: neura_diagnostic_tests <sink|source-map|render|width|edits>\n";
        return 2;
    }
    const std::string_view suite = argv[1];
    int result;
    if (suite == "sink") result = sink_suite();
    else if (suite == "source-map") result = source_map_suite();
    else if (suite == "render") result = render_suite();
    else if (suite == "width") result = width_suite();
    else if (suite == "edits") result = edits_suite();
    else {
        std::cerr << "unknown suite " << suite << '\n';
        return 2;
    }
    if (result == 0) std::cout << "ok " << suite << '\n';
    return result == 0 ? 0 : 1;
}

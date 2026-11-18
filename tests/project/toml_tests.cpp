#include "project/toml.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {

using namespace neura;
using namespace neura::project;

void check(bool condition, std::string_view context) {
    if (!condition) throw std::runtime_error(std::string(context));
}

std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(stream.is_open(), "open fixture " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

const TomlEntry& entry(const TomlTable& table, std::string_view key) {
    const auto found = table.entries.find(key);
    check(found != table.entries.end(), "missing test entry " + std::string(key));
    return found->second;
}

const TomlValue& value(const TomlTable& table, std::string_view key) {
    return std::get<TomlValue>(entry(table, key).item);
}

const TomlTable& table(const TomlDocument& doc, const TomlTable& parent, std::string_view key) {
    return doc.table(std::get<TomlTableId>(entry(parent, key).item));
}

const TomlTableArray& table_array(const TomlTable& table, std::string_view key) {
    return std::get<TomlTableArray>(entry(table, key).item);
}

void fixtures(const std::filesystem::path& dir) {
    const auto manifest = read(dir / "neura.toml");
    const auto doc = parse_toml(manifest, "neura.toml");
    auto part = [&](const Span& span) { return std::string_view(manifest).substr(span.offset, span.length); };
    const auto& root = doc.root();
    check(std::get<std::int64_t>(value(root, "schema").value) == 1, "manifest schema");
    const auto& project = table(doc, root, "project");
    check(std::get<std::string>(value(project, "name").value) == "stock", "project name");
    const auto& ui = table(doc, table(doc, root, "packages"), "ui");
    const auto& assets = value(ui, "assets");
    const auto& items = std::get<TomlValue::Array>(assets.value);
    check(items.size() == 2, "asset array");
    check(std::get<std::string>(items.at(0).value) == "ui source/画像", "raw UTF-8 path");
    check(std::get<std::string>(items.at(1).value) == "ui source/é", "decoded escape");
    check(part(items.at(1).span) == R"("ui source/\u00e9")", "array element span");
    check(part(assets.span).starts_with("[\n") && part(assets.span).ends_with("\n]"), "multiline array span");
    check(std::get<TomlValue::Array>(value(ui, "depends").value).empty(), "empty array");
    const auto& targets = table(doc, root, "targets");
    const auto& web = table(doc, targets, "web");
    check(part(web.span) == "[targets.web]", "explicit parent after child table");
    check(part(targets.entries.at("web").key_span) == "web", "table key segment span");
    check(std::get<std::string>(value(web, "kind").value) == "web", "late table key");
    check(table(doc, web, "origins").entries.contains("connect"), "retained child table");
    check(std::get<std::int64_t>(value(table(doc, web, "limits"), "W-1").value) == 16, "bare limit identifier");

    const auto lock = parse_toml(read(dir / "neura.lock"), "neura.lock");
    const auto& components = table_array(lock.root(), "components");
    check(components.size() == 2, "component order");
    const auto& browser = lock.table(components.at(0));
    const auto& desktop = lock.table(components.at(1));
    check(std::get<std::string>(value(browser, "name").value) == "browser", "first component");
    check(std::get<std::string>(value(desktop, "name").value) == "desktop", "second component");
    check(table(lock, browser, "version").entries.contains("api") && !desktop.entries.contains("version"),
          "child table belongs to its closest array element");
    const auto& browser_files = table_array(browser, "files");
    const auto& desktop_files = table_array(desktop, "files");
    check(browser_files.size() == 2 && desktop_files.size() == 1, "nested arrays of tables");
    check(std::get<std::string>(value(lock.table(browser_files.at(0)), "path").value) == "runtime.wasm",
          "first nested file");
    check(std::get<std::string>(value(lock.table(desktop_files.at(0)), "path").value) == "shell",
          "second component's nested file");
}

void accepted() {
    constexpr auto grammar = R"(
min = -9_223_372_036_854_775_808
max = +9223372036854775807
zeroes = [0, -0, +0,]
switch = true
switch-off = false
"" = ""
key . "with.dot" = "one"
key.two = "two"
escapes = "\"\\\b\t\n\f\r\u0000\u007f\u0800\U0001f642\U0010ffff#"
[key.more]
tab = "	"
)";
    const auto doc = parse_toml(grammar, "neura.lock");
    const auto& root = doc.root();
    check(std::get<std::int64_t>(value(root, "min").value) == std::numeric_limits<std::int64_t>::min(), "minimum");
    check(std::get<std::int64_t>(value(root, "max").value) == std::numeric_limits<std::int64_t>::max(), "maximum");
    const auto& zeros = std::get<TomlValue::Array>(value(root, "zeroes").value);
    check(zeros.size() == 3, "integer array with trailing separator");
    for (const auto& element : zeros) check(std::get<std::int64_t>(element.value) == 0, "signed zero");
    check(std::get<bool>(value(root, "switch").value) && !std::get<bool>(value(root, "switch-off").value), "Booleans");
    check(std::get<std::string>(value(root, "").value).empty(), "empty quoted key and value");
    const auto& key = table(doc, root, "key");
    check(std::get<std::string>(value(key, "with.dot").value) == "one", "quoted dot is not a separator");
    check(std::get<std::string>(value(key, "two").value) == "two", "same dotted parent");
    check(std::get<std::string>(value(table(doc, key, "more"), "tab").value) == "\t", "literal tab");
    const char escaped[] = "\"\\\b\t\n\f\r\0\x7f\xe0\xa0\x80\xf0\x9f\x99\x82\xf4\x8f\xbf\xbf#";
    check(std::get<std::string>(value(root, "escapes").value) == std::string(escaped, sizeof escaped - 1),
          "escapes are decoded, including NUL and the last Unicode scalar");

    const auto crlf = parse_toml("# é\r\n[\"é\"]\r\n\"🙂\" = \"abc\"", "utf8.neura.lock");
    const auto& str = value(table(crlf, crlf.root(), "é"), "🙂");
    check(str.span.file == "utf8.neura.lock" && str.span.line == 3 && str.span.column == 7 &&
              str.span.offset == 23 && str.span.length == 5, "CRLF, Unicode column and absolute byte offset");

    const auto empty = parse_toml(" \t# empty\r\n", "empty.toml");
    check(empty.tables.size() == 1 && empty.root().entries.empty(), "empty document syntax");

    std::string deep;
    for (int i = 0; i < 4096; ++i) deep += "a.";
    deep += "end = false";
    const auto nested = parse_toml(deep, "deep.toml");
    check(nested.tables.size() == 4097 && nested.tables.back().entries.contains("end"), "iterative table paths");
}

Diagnostic reject(std::string_view source, std::string_view token, std::string_view reason,
                  std::size_t expected = std::string_view::npos, std::string_view filename = "neura.toml") {
    try {
        parse_toml(source, filename);
    } catch (const Diagnostic& error) {
        const auto offset = expected == std::string_view::npos
            ? (token.empty() ? source.size() : source.find(token))
            : expected;
        check(error.data() && error.data()->code == "E1020", "diagnostic code");
        check(offset != std::string_view::npos && error.span.file == filename &&
                  error.span.offset == offset && error.span.length == token.size() &&
                  source.substr(offset, token.size()) == token, source);
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              std::string("unhelpful diagnostic: ") + error.what());
        return error;
    }
    throw std::runtime_error("accepted invalid syntax: " + std::string(source));
}

void prior_definition(const Diagnostic& error, Span expect) {
    const auto& labels = error.data()->labels;
    check(labels.size() == 1 && labels.front().text == "first defined here", "one prior definition label");
    const auto& at = labels.front().span;
    check(at.file == expect.file && at.offset == expect.offset && at.length == expect.length &&
              at.line == expect.line && at.column == expect.column, "exact earlier definition span");
}

void rejected() {
    reject("name = 'stock'", "'", "literal strings");
    reject("ratio = 1.5", "1.5", "decimal integer");
    reject("time = 2026-11-17", "2026-11-17", "decimal integer");
    reject("time = 12:30:00", "12:30:00", "decimal integer");
    reject("inline = { a = 1 }", "{", "inline tables");
    reject(R"(x = """multi""")", "\"\"\"", "multi-line strings");
    reject("x = [true]", "true", "arrays require strings");
    reject("x = [1, \"2\"]", "\"2\"", "same type");
    reject("x = [\"1\", 2]", "2", "same type", 10);
    reject("x = [[1]]", "[", "nested arrays", 5);
    reject("x = [1,,]", ",", "expected a value", 7);
    reject("x = [1 2]", "2", "expected ',' or ']'");
    reject("x = [1,\r\n", "", "expected a value");
    reject("x = 9223372036854775808", "9223372036854775808", "signed 64-bit range");
    reject("x = -9223372036854775809", "-9223372036854775809", "signed 64-bit range");
    reject("x = 0xff", "0xff", "decimal integer");
    reject("x = 00", "00", "decimal integer");
    reject("x = -0_1", "-0_1", "decimal integer");
    reject("x = 1__2", "1__2", "decimal integer");
    reject("x = 1_", "1_", "decimal integer");
    reject("x = +", "+", "decimal integer");
    reject("x = TRUE", "TRUE", "Boolean");
    reject("x = false true", "t", "end of the line", 10);
    reject("x = \"unfinished", "", "closing double quote");
    reject("x = \"unfinished\r\nnext\" ", "\r\n", "closing double quote");
    reject(R"(x = "\q")", R"(\q)", "invalid basic string escape");
    reject(R"(x = "\uabcg")", "g", "hexadecimal digit");
    reject(R"(x = "\ud800")", R"(\ud800)", "Unicode scalar value");
    reject(R"(x = "\U00110000")", R"(\U00110000)", "Unicode scalar value");
    reject("x = 1\rnot-newline", "\r", "expected LF");
    reject(std::string_view("x = \"\0\"", 7), std::string_view("\0", 1), "control character");
    reject("# \x7f", "\x7f", "control character");
    reject("# \xc0\xaf", "\xc0", "invalid UTF-8");
    reject("x = \"\xed\xa0\x80\"", "\xed", "invalid UTF-8");
    reject("# \xf4\x90\x80\x80", "\xf4", "invalid UTF-8");
    reject("# \xe2\x82", "\xe2", "invalid UTF-8");
    reject("\x80", "\x80", "invalid UTF-8");
    reject("\xef\xbb\xbfx = 1", "\xef\xbb\xbf", "expected a bare or double-quoted key");
    reject("x..y = 1", ".", "expected a bare or double-quoted key", 2);
    reject("x\n = 1", "\n", "expected '='");
    reject("[]", "]", "expected a bare or double-quoted key");
    reject("[a", "", "expected ']'");
    reject("[[a] ]", " ", "expected ']]'");
    reject("[a] [b]", "[", "end of the line", 4);

    const auto escaped = reject("key = 1\r\n\"\\u006bey\" = 2", R"("\u006bey")", "already defined");
    check(escaped.span.line == 2 && escaped.span.column == 1, "duplicate CRLF position");
    prior_definition(escaped, {"neura.toml", 1, 1, 0, 3});
    const auto unicode = reject("\"é\" = 1\r\n\"é\" = 2", "\"é\"", "already defined", 10, "neura.lock");
    check(unicode.span.line == 2 && unicode.span.column == 1, "Unicode key duplicate");
    prior_definition(unicode, {"neura.lock", 1, 1, 0, 4});
    const auto scalar_then_table = reject("root = 1\n[root.part]", "root", "conflicts", 10);
    prior_definition(scalar_then_table, {"neura.toml", 1, 1, 0, 4});
    const auto dotted_then_table = reject("root.item = 1\n[root]", "root", "conflicts", 15);
    prior_definition(dotted_then_table, {"neura.toml", 1, 1, 0, 4});
    const auto repeated = reject("[a.b]\nx = 1\n[a]\n[a]\n", "a", "conflicts", 17);
    prior_definition(repeated, {"neura.toml", 3, 2, 13, 1});
    reject("[a.b]\n[a]\nb.item = 1", "b", "dotted key cannot extend", 10);
    reject("x = []\n[[x]]", "x", "array of tables conflicts", 9);
    reject("[x]\n[[x]]", "x", "array of tables conflicts", 6);
    reject("[[x]]\n[x]", "x", "table conflicts", 7);
    reject("[x.y]\n[[x]]", "x", "array of tables conflicts", 8);
    reject("[[x]]\nname = 1\nname = 2", "name", "already defined", 15);
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "usage: neura_project_toml_tests <test directory>");
        fixtures(std::filesystem::path(argv[1]) / "fixtures");
        accepted();
        rejected();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project TOML: " << error.what() << '\n';
        return 1;
    }
}

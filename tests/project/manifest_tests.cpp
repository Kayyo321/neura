#include "project/manifest.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace neura;
using namespace neura::project;

constexpr std::string_view prefix = R"(schema = 1

[project]
name = "stock-1"
id = "example.stock-1"
version = "1.0.0"
language = "0.3"
)";

constexpr std::string_view package = R"(
[packages.ui]
root = "ui"
)";

constexpr std::string_view web = R"(
[targets.web]
kind = "web"
package = "ui"
entry = "Stock.Ui.Main"
capabilities = ["ui", "http_client"]
)";

constexpr std::string_view server = R"(
[targets.server]
kind = "server"
package = "ui"
entry = "Stock.Server.Main"
capabilities = ["sql", "http_server"]
)";

constexpr std::string_view desktop = R"(
[targets.desktop]
kind = "desktop"
package = "ui"
entry = "Stock.Desktop.Main"
capabilities = ["desktop"]
)";

void check(bool condition, std::string_view context) {
    if (!condition) throw std::runtime_error(std::string(context));
}

std::string concatenate(std::initializer_list<std::string_view> chunks) {
    std::string result;
    for (const auto chunk : chunks) result += chunk;
    return result;
}

std::string replace(std::string source, std::string_view from, std::string_view to) {
    const auto at = source.find(from);
    check(at != std::string::npos, "replace a known fixture field");
    source.replace(at, from.size(), to);
    return source;
}

Diagnostic reject(std::string_view source, std::string_view code, std::string_view token, std::string_view reason,
                  std::size_t offset = std::string_view::npos) {
    try {
        parse_manifest(source, "neura.toml");
    } catch (const Diagnostic& error) {
        const auto at = offset != std::string_view::npos ? offset : (token.empty() ? 0 : source.rfind(token));
        const auto& span = error.span;
        check(error.data() && error.data()->code == code, std::string("unexpected code: ") + error.what());
        check(at != std::string_view::npos && span.file == "neura.toml" &&
                  span.offset == at && span.length == token.size() && source.substr(at, token.size()) == token,
              std::string("unexpected span: ") + error.what());
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              std::string("unexpected diagnostic: ") + error.what());
        return error;
    }
    throw std::runtime_error("accepted invalid manifest: " + std::string(source));
}

void documented_manifest(const char* path) {
    std::ifstream file(path, std::ios::binary);
    check(file.is_open(), "open the manifest design");
    const std::string design{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    constexpr std::string_view fence = "\x60\x60\x60";
    const auto start = design.find(std::string(fence) + "toml\n");
    check(start != std::string::npos, "find the accepted TOML example");
    const auto content = start + fence.size() + 5;
    const auto end = design.find(fence, content);
    check(end != std::string::npos, "find the end of the example");
    const auto source = std::string_view(design).substr(content, end - content);
    const auto app = parse_manifest(source, "neura.toml");
    check(app.schema.value == 1 && app.project.id.value == "example.stock", "identity");
    check(app.packages.size() == 4 && app.packages.at("ui").depends.front().value == "contracts", "package graph");
    check(app.contracts_manifest->value == "contracts/contracts.toml", "contracts manifest");
    const auto& web_target = app.targets.at("web");
    const auto& browser = std::get<WebSettings>(web_target.settings);
    check(browser.routing->value == Routing::Hash && browser.styles.front().value == "ui/styles/base.css", "web settings");
    check(browser.origins.connect->front().value.host == "api.stock.example" &&
              web_target.capabilities.back().value == Capability::Bridge, "web origin and capability");
    const auto& server_target = app.targets.at("server");
    const auto& host = std::get<ServerSettings>(server_target.settings);
    check(host.http.listen->value == "127.0.0.1:8080" && host.providers.at(1).value == "postgresql" &&
              host.sql.at("main").migrations->value == "server/migrations", "server settings");
    check(std::get<Limit>(server_target.limits.at("S-1")).value == 16, "server limit");
    const auto& desktop_target = app.targets.at("desktop");
    const auto& native = std::get<DesktopSettings>(desktop_target.settings);
    check(native.ui.value == "web" && desktop_target.package.value == "desktop", "desktop references");
    check(native.operations.front().value == "parts.export" && native.packaging.formats.size() == 2 &&
              native.packaging.icon->value == "desktop/icon.png", "desktop packaging");
    check(source.substr(native.packaging.icon->span.offset, native.packaging.icon->span.length) == "\"desktop/icon.png\"",
          "value source span");
}

void accepted() {
    const auto shared = concatenate({prefix, package, R"(assets = ["ui space/画像", "a/../ok"]
depends = ["charts", "core", "flags"]

[packages.core]
root = "."

[dependencies.charts]
path = "../charts"
digest = "retained exactly"

[dependencies.flags]
archive = "vendor/flags.tar.gz"
digest = "also retained"
)"});
    const auto library = parse_manifest(shared, "neura.toml");
    check(library.targets.empty(), "a shared package project may omit targets");
    check(parse_manifest(shared + "\n[targets]\n", "neura.toml").targets.empty(), "empty targets");
    check(library.packages.at("ui").assets.front().value == "ui space/画像", "spaces and UTF-8 path spelling");
    check(library.dependencies.at("charts").source == DependencySource::Directory &&
              library.dependencies.at("flags").source == DependencySource::Archive &&
              library.dependencies.at("charts").digest.value == "retained exactly", "dependency declarations");

    std::string chain(prefix);
    for (int i = 0; i < 2048; ++i) {
        chain += "\n[packages.p" + std::to_string(i) + "]\nroot = \".\"\n";
        if (i + 1 < 2048) chain += "depends = [\"p" + std::to_string(i + 1) + "\"]\n";
    }
    check(parse_manifest(chain, "neura.toml").packages.size() == 2048, "iterative package traversal");

    const auto whole = concatenate({prefix, package, desktop, R"(ui = "web"
[targets.desktop.packaging]
sign_identity = "Vendor"
[targets.desktop.sql.local]
provider = "not-installed-yet"
migrate = "manual"
[targets.desktop.limits]
D-3 = 67108864
C-2 = 67108864
)", server, R"([targets.server.http]
tls_listen = "[::1]:443"
admin_listen = "localhost:9000"
trusted_proxies = ["127.0.0.1", "::1"]
[targets.server.config]
prefix = "STOCK_"
expose = ["Region"]
[targets.server.sql.remote]
provider = "postgresql"
migrate = "on_start"
[targets.server.limits]
S-10 = [65536, 600]
S-12 = [3600, 60]
S-13 = [1048576, 1000000]
)", web, R"([targets.web.limits]
U-2 = 255
W-1 = 1073741824
C-4 = 128
[targets.web.origins]
connect = ["https://example.test:8443", "http://127.0.0.1", "app.v1+test://[2001:db8::ffff:192.0.2.4]:0"]
load = ["https://localhost", "https://[2001:db8:0:0:0:0:0:1]"]
style = ["https://a.example."]
)"});
    const auto app = parse_manifest(whole, "neura.toml");
    const auto& front = std::get<WebSettings>(app.targets.at("web").settings);
    const auto& server_target = app.targets.at("server");
    const auto& back = std::get<ServerSettings>(server_target.settings);
    const auto& native = std::get<DesktopSettings>(app.targets.at("desktop").settings);
    check(front.origins.connect->front().value.port == 8443 &&
              front.origins.connect->at(2).value.scheme == "app.v1+test" &&
              front.origins.connect->at(2).value.host == "[2001:db8::ffff:192.0.2.4]" &&
              front.origins.connect->at(2).value.port == 0, "origin shape and typed port");
    check(!back.connect_origins && !native.origins.connect, "no native origin list by default");
    const auto scoped = parse_manifest(whole + R"(
[targets.server.origins]
connect = ["https://api.example:8443"]
[targets.desktop.origins]
connect = []
)", "neura.toml");
    const auto& scoped_server = std::get<ServerSettings>(scoped.targets.at("server").settings);
    const auto& scoped_desktop = std::get<DesktopSettings>(scoped.targets.at("desktop").settings);
    check(scoped_server.connect_origins && scoped_server.connect_origins->size() == 1 &&
              scoped_server.connect_origins->front().value.text == "https://api.example:8443",
          "a server preserves a configured connect origin");
    check(scoped_desktop.origins.connect && scoped_desktop.origins.connect->empty(),
          "a desktop distinguishes an explicit empty connect list from an omitted list");
    check(back.http.trusted_proxies.size() == 2 && back.config.prefix->value == "STOCK_" &&
              back.sql.at("remote").migrate->value == MigrationMode::OnStart, "typed server configuration");
    check(native.packaging.sign_identity->value == "Vendor" &&
              native.sql.at("local").provider.value == "not-installed-yet", "provider spelling retained");
    const auto& pair = std::get<std::array<Limit, 2>>(server_target.limits.at("S-10"));
    check(pair[0].value == 65536 && pair[1].value == 600, "paired maximum limits");
    const auto bound = pair[1].span;
    check(whole.substr(bound.offset, bound.length) == "600", "paired limit element location");
}

void syntax_and_identity() {
    const auto sample = concatenate({prefix, package, web});
    reject(replace(sample, "schema = 1", "schema = 2"), "E1021", "2", "supports schema 1");
    reject(replace(sample, "schema = 1", "schema = true"), "E1020", "schema", "decimal integer");
    reject(replace(sample, "schema = 1", ""), "E1020", "", "missing required key 'schema'");
    reject(replace(sample, "\"0.3\"", "\"0.4\""), "E1021", "\"0.4\"", "supports \"0.3\"");
    reject(replace(sample, "\"stock-1\"", "\"Stock_1\""), "E1020", "\"Stock_1\"", "lowercase letters");
    reject(replace(sample, "\"example.stock-1\"", "\"example..stock\""), "E1020", "\"example..stock\"", "reverse-domain");
    reject(replace(sample, "\"1.0.0\"", "\"01.0.0\""), "E1020", "\"01.0.0\"", "MAJOR.MINOR.PATCH");
    reject(replace(sample, "\"1.0.0\"", "\"1.0.0-beta\""), "E1020", "\"1.0.0-beta\"", "MAJOR.MINOR.PATCH");
    reject(replace(sample, "root = \"ui\"", ""), "E1020", "[packages.ui]", "missing required key 'root'");
    reject(sample + "postbuild = \"ignored\"\n", "E1020", "postbuild", "unknown manifest key");
    const auto typo = reject(replace(sample, "entry =", "entri ="), "E1020", "entri", "unknown manifest key");
    const auto& suggestions = typo.data()->suggestions;
    check(suggestions.size() == 1 && suggestions[0].edits.at(0).replacement == "entry" &&
              suggestions[0].edits.at(0).span.offset == typo.span.offset, "entry typo suggestion");
    std::string crlf;
    for (const char c : replace(sample, "entry =", "entri =")) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const auto line = reject(crlf, "E1020", "entri", "unknown manifest key").span;
    check(line.line == 15 && line.column == 1, "manifest model keeps CRLF line and column");
    const auto repeated = replace(sample, "[packages.ui]", "[[packages.ui]]");
    reject(repeated, "E1020", "ui", "expected a table", repeated.find("packages.ui") + 9);
    reject(replace(sample, "capabilities = [\"ui\", \"http_client\"]", "capabilities = [1, 2]"),
           "E1020", "capabilities", "array of strings");
    reject(sample + "[targets.web.origins]\nlinks = []\n", "E1020", "links", "unknown manifest key");
    reject(sample + "[targets.web.config]\nprefix = \"A\"\n", "E1020", "config", "unknown manifest key");
}

void packages_and_targets() {
    const auto sample = concatenate({prefix, package, web});
    for (const auto path : {"../shared", "/usr/source", "C:/source", "C:source", "https://example.test/x", "ui/../../x"}) {
        const auto quoted = "\"" + std::string(path) + "\"";
        reject(replace(sample, "\"ui\"", quoted), "E1025", quoted, "path");
    }
    reject(replace(sample, "root = \"ui\"", "root = \"ui\\\\img\""), "E1025", "\"ui\\\\img\"", "using '/'");
    reject(replace(sample, "root = \"ui\"", "root = \"ui\\u0000img\""), "E1025", "\"ui\\u0000img\"", "expected a local path");
    reject(concatenate({prefix, package, "assets = [\"/usr/share/icons\"]\n"}), "E1025", "\"/usr/share/icons\"", "expected a local path");
    reject(sample + "styles = [\"../shared.css\"]\n", "E1025", "\"../shared.css\"", "leaves the project");
    reject(sample + "\n[contracts]\nmanifest = \"../contracts.toml\"\n", "E1025", "\"../contracts.toml\"", "leaves the project");
    // Names that Windows cannot create are refused on every host, so that a
    // manifest means the same everywhere.
    for (const auto path : {"ui/a:b", "ui/con", "ui/AUX.txt", "ui/com1/x", "ui/Lpt9.log", "ui/nul.tar.gz", "ui/name.",
                            "ui/name "}) {
        const auto quoted = "\"" + std::string(path) + "\"";
        reject(replace(sample, "\"ui\"", quoted), "E1025", quoted, "path component");
    }
    reject(sample + "styles = [\"ui/prn.css\"]\n", "E1025", "\"ui/prn.css\"", "device name that Windows reserves");
    reject(sample + "styles = [\"ui/x:y.css\"]\n", "E1025", "\"ui/x:y.css\"", "stream name");
    reject(sample + "styles = [\"ui/x.css. \"]\n", "E1025", "\"ui/x.css. \"", "dot or a space");
    for (const auto path : {"ui/console", "ui/com0", "ui/lpt10", "ui/nul-tools", "ui/.hidden", "ui/./a.b", "ui/x/../y.z"}) {
        const auto app = parse_manifest(replace(sample, "\"ui\"", "\"" + std::string(path) + "\""), "neura.toml");
        check(app.packages.at("ui").root.value == path, std::string("portable path component: ") + path);
    }
    reject(concatenate({prefix, package, "depends = [\"missing\"]\n"}), "E1020", "\"missing\"", "unknown package dependency");
    reject(concatenate({prefix, package, "depends = [\"ui\"]\n"}), "E1020", "\"ui\"", "ui -> ui");
    reject(concatenate({prefix, package, "depends = [\"z\"]\n[packages.z]\nroot=\"z\"\ndepends=[\"ui\"]\n"}),
           "E1020", "\"ui\"", "ui -> z -> ui");
    reject(sample + "\n[dependencies.charts]\npath = \"../charts\"\n", "E1032", "[dependencies.charts]", "requires one");
    reject(sample + "\n[dependencies.charts]\npath = \"../charts\"\narchive = \"charts.zip\"\ndigest = \"pin\"\n",
           "E1032", "[dependencies.charts]", "requires one");
    reject(sample + "\n[dependencies.charts]\narchive = \"charts.zip\"\ndigest = \" \"\n",
           "E1032", "[dependencies.charts]", "requires a digest");
    reject(sample + "\n[dependencies.charts]\npath = \"charts\"\ndigest = \"pin\"\n",
           "E1025", "\"charts\"", "outside the project");
    reject(sample + "\n[dependencies.charts]\narchive = \"../charts.zip\"\ndigest = \"pin\"\n",
           "E1025", "\"../charts.zip\"", "leaves the project");
    const auto collision = reject(sample + "\n[dependencies.ui]\npath = \"../vendor\"\ndigest = \"pin\"\n",
                                  "E1020", "ui", "same name as a project package");
    check(collision.data()->labels.size() == 1 && collision.data()->labels.front().span.line == 9,
          "package collision labels the first package");
    reject(replace(sample, "\"web\"", "\"mobile\""), "E1023", "\"mobile\"", "target kind");
    reject(replace(sample, "package = \"ui\"", ""), "E1023", "web", "requires 'package'",
           sample.find("[targets.web]") + 9);
    reject(replace(sample, "package = \"ui\"", "package = \"shared\""), "E1023", "\"shared\"", "unknown target package");
    reject(replace(sample, "\"Stock.Ui.Main\"", "\"\""), "E1020", "\"\"", "module name");
    reject(replace(sample, "\"http_client\"", "\"fs\""), "E1020", "\"fs\"", "cannot be listed");
    reject(replace(sample, "\"http_client\"", "\"clock\""), "E1020", "\"clock\"", "cannot be listed");
    reject(replace(sample, "\"http_client\"", "\"future\""), "E1020", "\"future\"", "cannot be listed");
    reject(sample + "routing = \"query\"\n", "E1020", "\"query\"", "routing");
    const auto missing_ui = concatenate({sample, desktop});
    reject(missing_ui, "E1023", "desktop", "requires 'ui'", missing_ui.find("[targets.desktop]") + 9);
    reject(concatenate({sample, desktop, "ui = \"missing\"\n"}), "E1023", "\"missing\"", "needs a web target");
    reject(concatenate({sample, server, desktop, "ui = \"server\"\n"}), "E1023", "\"server\"", "needs a web target");
    reject(concatenate({sample, desktop, "ui = \"web\"\n[targets.desktop.package]\nformats = []\n"}),
           "E1020", "package", "conflicts");
    reject(concatenate({sample, server, "[targets.server.sql.main]\nprovider=\"sqlite\"\nmigrate=\"inferred\"\n"}),
           "E1020", "\"inferred\"", "migration mode");
}

void origins_and_limits() {
    const auto sample = concatenate({prefix, package, web});
    for (const auto invalid : {"https://example.test/path", "https://example.test/", "https://*.example.test",
                               "https://example.test?query", "https://example.test#x", "https://user@example.test",
                               "https://example.test:65536", "https://example.test:", "https://example.test:-1",
                               "https://", "https://[2001::db8::1]", "https://[::1", "https://::1",
                               "https://[192.0.2.1]", "https://[::ffff:192.0.999.1]", "1https://example.test",
                               "https://a..b", "https://-example.test", "https://256.1.1.1", "//example.test"}) {
        const auto quoted = "\"" + std::string(invalid) + "\"";
        reject(sample + "\n[targets.web.origins]\nconnect = [" + quoted + "]\n", "E1020", quoted, "absolute origin");
    }
    reject(sample + "\n[targets.web.limits]\nS-1=16\n", "E1028", "16", "another target kind");
    reject(sample + "\n[targets.web.limits]\nU-2=256\n", "E1028", "256", "between 1 and 255");
    reject(sample + "\n[targets.web.limits]\nU-2=0\n", "E1028", "0", "between 1 and 255");
    reject(sample + "\n[targets.web.limits]\nW-9=1\n", "E1020", "W-9", "unknown limit");
    const auto server_limits = concatenate({prefix, package, server, "[targets.server.limits]\n"});
    const auto minimum = parse_manifest(server_limits + "S-10=[1, 1]\n", "neura.toml");
    const auto& limits = minimum.targets.at("server").limits;
    check(std::get<std::array<Limit, 2>>(limits.at("S-10"))[1].value == 1, "paired minimum limits");
    reject(server_limits + "S-1=5000\n", "E1028", "5000", "between 1 and 1024");
    reject(server_limits + "S-10=65536\n", "E1020", "S-10", "pair of decimal integer");
    reject(server_limits + "S-10=[1]\n", "E1020", "S-10", "pair of decimal integer");
    reject(server_limits + "S-10=[1, 2, 3]\n", "E1020", "S-10", "pair of decimal integer");
    reject(server_limits + "S-10=[\"1\", \"2\"]\n", "E1020", "S-10", "decimal integer");
    reject(server_limits + "S-10=[65537, 600]\n", "E1028", "65537", "between 1 and 65536");
    reject(server_limits + "S-10=[65536, 601]\n", "E1028", "601", "between 1 and 600");
    reject(server_limits + "S-12=[3601, 60]\n", "E1028", "3601", "between 1 and 3600");
    reject(server_limits + "S-12=[3600, 61]\n", "E1028", "61", "between 1 and 60");
    reject(server_limits + "S-12=[1, 0]\n", "E1028", "0", "between 1 and 60");
    reject(server_limits + "S-13=[1048577, 1000000]\n", "E1028", "1048577", "between 1 and 1048576");
    reject(server_limits + "S-13=[1048576, 1000001]\n", "E1028", "1000001", "between 1 and 1000000");
    reject(concatenate({sample, desktop, "ui=\"web\"\n[targets.desktop.limits]\nW-1=99\n"}),
           "E1028", "99", "another target kind");
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "usage: neura_project_manifest_tests <Projects030.md>");
        documented_manifest(argv[1]);
        accepted();
        syntax_and_identity();
        packages_and_targets();
        origins_and_limits();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project manifest: " << error.what() << '\n';
        return 1;
    }
}

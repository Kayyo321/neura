#include "project/resolve.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace neura;
using namespace neura::project;

// Independent hashlib vectors for the fixture bytes and tree format.
constexpr std::string_view pinned_tree = "fa877b662acbcca3847a25042762208aa296b59b0a16c6edfd6b9b67d3a9df0b";
constexpr std::string_view pinned_archive = "b8561a84d43a4e1fab3f6ccd57c47000d413c88e80a2efff82281e4c43cc4132";
constexpr std::string_view pinned_contract = "923528312f7f7fe5534bbf7be99f912cbff48f03a696e214378f560f8367c28b";
constexpr char archive_bytes[] = "archive\0bytes\r\n";
constexpr char nested_bytes[] = "nul\0tail\r\n";
constexpr std::string_view contract_bytes = "{\"contract\":1}\r\n";
const fs::path generated = "src app/generated/contracts/contracts.lock";

void check(bool condition, std::string_view context) {
    if (!condition) throw std::runtime_error(std::string(context));
}

fs::path utf8(std::string_view value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

class Sandbox {
public:
    Sandbox() {
        const auto prefix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = fs::temp_directory_path() / ("neura-project-test-" + prefix + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("cannot reserve a project fixture directory");
    }

    ~Sandbox() {
        std::error_code error;
        fs::remove_all(path, error);
    }
    fs::path path;
};

void write(const fs::path& path, std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    check(out.good(), "write a fixture file");
}

std::string replace(std::string text, std::string_view before, std::string_view after) {
    const auto at = text.find(before);
    check(at != std::string::npos, "replace an existing fixture path");
    text.replace(at, before.size(), after);
    return text;
}

template <typename Operation>
Diagnostic rejected(Operation run, std::string_view code, std::string_view reason) {
    try {
        run();
    } catch (const Diagnostic& error) {
        check(error.data() && error.data()->code == code, std::string("wrong diagnostic code: ") + error.what());
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              std::string("wrong diagnostic reason: ") + error.what());
        return error;
    }
    throw std::runtime_error("accepted invalid project: " + std::string(reason));
}

void at(const Diagnostic& error, std::string_view source, std::string_view file, std::string_view token) {
    check(error.span.file == file && error.span.offset == source.find(token) && error.span.length == token.size(),
          std::string("wrong diagnostic location: ") + error.what());
}

struct Fixture {
    Sandbox sandbox;
    const fs::path root = sandbox.path / utf8("stock é");
    const fs::path dependency = sandbox.path / utf8("stock é-vendor");
    const fs::path manifest_path = root / "neura.toml";
    const fs::path archive = root / "vendor/chart package.tar.gz";
    std::string source = R"(schema = 1
[project]
name = "stock"
id = "example.stock"
version = "1.0.0"
language = "0.3"
[packages.stock]
root = "src app"
assets = ["assets/画像"]
[targets.web]
kind = "web"
package = "stock"
entry = "Stock.App"
capabilities = ["ui"]
styles = ["assets/site.css"]
[targets.server]
kind = "server"
package = "stock"
entry = "Stock.Api"
capabilities = ["sql"]
[targets.server.sql.main]
provider = "sqlite"
migrations = "src app/sql"
[targets.desktop]
kind = "desktop"
package = "stock"
entry = "Stock.Desktop"
capabilities = ["desktop"]
ui = "web"
[targets.desktop.packaging]
icon = "assets/icon.ico"
[targets.desktop.sql.local]
provider = "sqlite"
migrations = "src app/sql"
[contracts]
manifest = "src app/contracts.toml"
[dependencies.charts]
archive = "vendor/chart package.tar.gz"
digest = ")" + std::string(pinned_archive) + R"("
[dependencies.sources]
path = "../stock é-vendor"
digest = ")" + std::string(pinned_tree) + "\"\n";

    Fixture() {
        fs::create_directories(root / utf8("assets/画像"));
        fs::create_directories(root / "src app/sql");
        write(root / "assets/site.css", "main { color: black; }\n");
        write(root / "assets/icon.ico", "asset");
        write(root / "src app/contracts.toml", "schema = 1\n");
        write(root / generated, contract_bytes);
        write(archive, std::string_view(archive_bytes, sizeof archive_bytes - 1));
        write(dependency / "z.nra", "stock\n");
        write(dependency / utf8("déjà/é.txt"), std::string_view(nested_bytes, sizeof nested_bytes - 1));
        fs::create_directories(dependency / "empty");
        write(manifest_path, source);
    }

    Manifest manifest() const { return parse_manifest(source, "neura.toml"); }
    ResolvedProject resolve(const Manifest& model, const std::optional<fs::path>& contracts = generated) const {
        auto result = resolve_project(model, manifest_path, contracts);
        // Component metadata comes from the caller, independently of local files.
        for (const auto& [name, target] : model.targets) result.lock_inputs.targets.try_emplace(name);
        return result;
    }
};

void pins_and_paths() {
    Fixture fixture;
    const auto manifest = fixture.manifest();
    const auto found = fixture.resolve(manifest);
    check(found.root == fs::canonical(fixture.root) && found.manifest == fs::canonical(fixture.manifest_path), "canonical project root");
    check(found.packages.at("stock").root.value == found.root / "src app", "source root with spaces");
    check(found.packages.at("stock").assets.front().value == found.root / utf8("assets/画像"), "Unicode asset root");
    check(found.targets.at("web").styles.front().value == found.root / "assets/site.css", "stylesheet");
    check(found.targets.at("desktop").icon->value == found.root / "assets/icon.ico", "desktop icon");
    check(found.targets.at("server").migrations.at("main").value == found.root / "src app/sql" &&
          found.targets.at("desktop").migrations.at("local").value == found.root / "src app/sql", "migration roots");
    check(found.contracts_manifest->value == found.root / "src app/contracts.toml", "contracts manifest");
    check(found.contracts_lock->value == found.root / generated, "contracts lock");
    check(found.dependencies.at("charts").value == found.root / "vendor/chart package.tar.gz", "local vendored archive");
    check(found.dependencies.at("sources").value == fs::canonical(fixture.dependency), "outside sibling with same prefix");
    check(found.lock_inputs.dependencies.at("charts").hex() == pinned_archive, "known archive bytes");
    check(found.lock_inputs.dependencies.at("sources").hex() == pinned_tree, "known directory digest");
    check(found.lock_inputs.contracts_digest->hex() == pinned_contract, "known contracts.lock bytes");
    const auto encoded = write_lock(make_lock(manifest, found.lock_inputs));
    const auto lock = parse_lock(encoded, "neura.lock");
    validate_lock(&lock, manifest, found.lock_inputs);
    check(write_lock(make_lock(manifest, fixture.resolve(manifest).lock_inputs)) == encoded, "stable canonical lock");

    const auto alternate = fixture.sandbox.path / "alternate";
    fs::create_directories(alternate / "empty");
    write(alternate / utf8("déjà/é.txt"), std::string_view(nested_bytes, sizeof nested_bytes - 1));
    write(alternate / "z.nra", "stock\n");
    auto alternate_manifest = parse_manifest(replace(fixture.source, "../stock é-vendor", "../alternate"), "neura.toml");
    const auto other = fixture.resolve(alternate_manifest);
    validate_lock(&lock, alternate_manifest, other.lock_inputs);
    check(write_lock(make_lock(alternate_manifest, other.lock_inputs)) == encoded,
          "creation order, root name and metadata do not change the digest");
    fs::remove(alternate / "empty");
    check(fixture.resolve(alternate_manifest).lock_inputs.dependencies.at("sources").hex() != pinned_tree,
          "empty directories are part of the pin");

    const auto check_change = [&](std::string_view expected, std::string_view subject) {
        const auto edited = fixture.resolve(manifest);
        const auto error = rejected([&] { validate_lock(&lock, manifest, edited.lock_inputs); }, "E1026", subject);
        at(error, encoded, "neura.lock", "\"" + std::string(expected) + "\"");
        check(error.data()->args.at("expected_digest") == expected &&
              error.data()->args.at("found_digest") != expected, "both SHA-256 values are reported");
    };
    write(fixture.archive, "new archive");
    check_change(pinned_archive, "dependency 'charts'");
    write(fixture.archive, std::string_view(archive_bytes, sizeof archive_bytes - 1));
    write(fixture.dependency / "z.nra", "stock!\n");
    check_change(pinned_tree, "dependency 'sources'");
    write(fixture.dependency / "z.nra", "stock\n");
    write(fixture.root / generated, "{\"contract\":2}\r\n");
    check_change(pinned_contract, "contracts.lock");
    fs::remove(fixture.root / generated);
    const auto absent = fixture.resolve(manifest);
    check(!absent.contracts_lock && !absent.lock_inputs.contracts_digest, "missing generated lock");
    rejected([&] { validate_lock(&lock, manifest, absent.lock_inputs); }, "E1022", "absent contracts.lock");
    write(fixture.root / generated, contract_bytes);
    check(!fixture.resolve(manifest, std::nullopt).contracts_lock, "caller owns generated lock discovery");
}

void invalid_paths() {
    Fixture fixture;
    const auto check_declared = [&](std::string_view before, std::string_view after, std::string_view code,
                                    std::string_view reason, std::string_view token) {
        const auto bytes = replace(fixture.source, before, after);
        const auto model = parse_manifest(bytes, "neura.toml");
        const auto error = rejected([&] { fixture.resolve(model); }, code, reason);
        at(error, bytes, "neura.toml", token);
    };
    check_declared("assets/site.css", "assets/missing.css", "E1020", "cannot resolve", "\"assets/missing.css\"");
    check_declared("assets/site.css", "assets", "E1020", "regular file", "\"assets\"");
    check_declared("assets/site.css", "assets/site.css/.", "E1020", "resolve", "\"assets/site.css/.\"");
    check_declared("assets/site.css", "assets/site.css/", "E1020", "resolve", "\"assets/site.css/\"");
    check_declared("../stock é-vendor", "..", "E1025", "contains the project", "\"..\"");
    check_declared("../stock é-vendor", "../stock é/.", "E1025", "inside the project", "\"../stock é/.\"");
    check_declared("../stock é-vendor", "../missing", "E1032", "cannot resolve", "[dependencies.sources]");
    check_declared("vendor/chart package.tar.gz", "vendor/missing", "E1032", "cannot resolve", "[dependencies.charts]");
    check_declared(pinned_archive, "bad-pin", "E1032", "requires a SHA-256 pin", "[dependencies.charts]");
    const auto model = fixture.manifest();
    rejected([&] { fixture.resolve(model, "missing/../../outside/contracts.lock"); }, "E1025", "leaves the project");
    rejected([&] { fixture.resolve(model, fixture.root / generated); }, "E1025", "project-relative");
    rejected([&] { fixture.resolve(model, "src app"); }, "E1020", "regular file");
}

#ifndef _WIN32
void links_and_special_files() {
    Fixture fixture;
    fs::create_directory_symlink(fixture.dependency, fixture.root / "portal");
    const auto bytes = replace(fixture.source, "root = \"src app\"", "root = \"portal/../stock é/src app\"");
    const auto model = parse_manifest(bytes, "neura.toml");
    at(rejected([&] { fixture.resolve(model); }, "E1025", "outside the project"), bytes, "neura.toml",
       "\"portal/../stock é/src app\"");
    fs::create_directory_symlink("src app", fixture.root / "alias");
    const auto alias = parse_manifest(replace(fixture.source, "root = \"src app\"", "root = \"alias\""), "neura.toml");
    check(fixture.resolve(alias).packages.at("stock").root.value == fs::canonical(fixture.root / "src app"),
          "in-project directory alias opens to its canonical target");
    fs::create_symlink(fixture.dependency / "z.nra", fixture.root / "assets/outside.css");
    const auto css = replace(fixture.source, "assets/site.css", "assets/outside.css");
    at(rejected([&] { fixture.resolve(parse_manifest(css, "neura.toml")); }, "E1025", "outside the project"),
       css, "neura.toml", "\"assets/outside.css\"");
    fs::create_directory_symlink(fixture.root / "src app", fixture.sandbox.path / "in-project");
    const auto outside = replace(fixture.source, "../stock é-vendor", "../in-project");
    at(rejected([&] { fixture.resolve(parse_manifest(outside, "neura.toml")); }, "E1025", "inside the project"),
       outside, "neura.toml", "\"../in-project\"");
    fs::create_symlink(fixture.dependency / "z.nra", fixture.root / "vendor/escaped.tar");
    const auto archive = replace(fixture.source, "vendor/chart package.tar.gz", "vendor/escaped.tar");
    at(rejected([&] { fixture.resolve(parse_manifest(archive, "neura.toml")); }, "E1025", "outside the project"),
       archive, "neura.toml", "\"vendor/escaped.tar\"");

    const auto ordinary = fixture.manifest();
    rejected([&] { fixture.resolve(ordinary, "portal/missing/contracts.lock"); }, "E1025", "outside the project");
    fs::create_symlink("missing", fixture.root / "broken.lock");
    rejected([&] { fixture.resolve(ordinary, "broken.lock"); }, "E1020", "cannot resolve");
    fs::create_symlink(fixture.root / "assets/site.css", fixture.dependency / "escape");
    at(rejected([&] { fixture.resolve(ordinary); }, "E1025", "leaves the dependency directory"), fixture.source,
       "neura.toml", "\"../stock é-vendor\"");
    fs::remove(fixture.dependency / "escape");
    fs::create_symlink("z.nra", fixture.dependency / "alias");
    rejected([&] { fixture.resolve(ordinary); }, "E1032", "symbolic link");
    fs::remove(fixture.dependency / "alias");

    fs::create_hard_link(fixture.dependency / "z.nra", fixture.dependency / "duplicate");
    const auto hard = fixture.resolve(ordinary).lock_inputs.dependencies.at("sources");
    fs::remove(fixture.dependency / "duplicate");
    write(fixture.dependency / "duplicate", "stock\n");
    check(fixture.resolve(ordinary).lock_inputs.dependencies.at("sources") == hard,
          "a regular hard link hashes like the same bytes at its logical name");
    fs::remove(fixture.dependency / "duplicate");
    const auto fifo = fixture.dependency / "pipe";
    check(::mkfifo(fifo.c_str(), 0600) == 0, "create a special-file fixture");
    rejected([&] { fixture.resolve(ordinary); }, "E1032", "not a regular file or directory");
    fs::remove(fifo);
    const auto direct = fixture.root / "vendor/pipe";
    check(::mkfifo(direct.c_str(), 0600) == 0, "create the direct-file fixture");
    const auto special = replace(fixture.source, "vendor/chart package.tar.gz", "vendor/pipe");
    rejected([&] { fixture.resolve(parse_manifest(special, "neura.toml")); }, "E1032", "regular file");

    write(fixture.dependency / "ambiguous\\name", "name\n");
    rejected([&] { fixture.resolve(ordinary); }, "E1032", "unambiguous UTF-8");
    fs::remove(fixture.dependency / "ambiguous\\name");
    write(fixture.dependency / "invalid-\xff", "name\n");
    rejected([&] { fixture.resolve(ordinary); }, "E1032", "unambiguous UTF-8");
}
#endif

} // namespace

int main() {
    try {
        pins_and_paths();
        invalid_paths();
#ifndef _WIN32
        links_and_special_files();
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project resolution: " << error.what() << '\n';
        return 1;
    }
}

#include "project/lock.hpp"
#include "project/toml.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using namespace neura;
using namespace neura::project;

constexpr std::string_view minimal = "schema = 1\nlanguage = \"0.3\"\n";
constexpr std::string_view project = "schema = 1\n"
    "[project]\nname = \"stock\"\nid = \"example.stock\"\nversion = \"1.0.0\"\nlanguage = \"0.3\"\n"
    "[packages.stock]\nroot = \"src\"\n";
constexpr std::string_view lock_file = "neura.lock";
constexpr std::string_view manifest_file = "neura.toml";

void check(bool condition, std::string_view context) {
    if (!condition) throw std::runtime_error(std::string(context));
}

std::string hex(char c) {
    return std::string(64, c);
}

Sha256Digest digest(char c) {
    return Sha256Digest::parse(hex(c)).value();
}

std::string replace(std::string source, std::string_view before, std::string_view after) {
    const auto at = source.find(before);
    check(at != std::string::npos, "replace an existing fixture entry");
    source.replace(at, before.size(), after);
    return source;
}

template <typename Operation>
Diagnostic reject(Operation operation, std::string_view code, std::string_view reason) {
    try {
        operation();
    } catch (const Diagnostic& error) {
        check(error.data() && error.data()->code == code, std::string("unexpected code: ") + error.what());
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              std::string("unexpected diagnostic: ") + error.what());
        return error;
    }
    throw std::runtime_error("accepted invalid lock: " + std::string(reason));
}

void at(const Diagnostic& error, std::string_view source, std::string_view file, std::string_view token) {
    const auto offset = source.rfind(token);
    check(error.span.file == file && error.span.offset == offset && error.span.length == token.size(),
          std::string("wrong location for ") + error.what());
}

void documented_lock(const char* path) {
    std::ifstream file(path, std::ios::binary);
    check(file.is_open(), "open Projects030.md");
    const std::string document{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto section = document.find("## The lock file\n");
    constexpr std::string_view fence = "\x60\x60\x60";
    const auto start = document.find(std::string(fence) + "toml\n", section);
    check(start != std::string::npos, "find the documented lock example");
    const auto content = start + fence.size() + 5;
    const auto end = document.find(fence, content);
    check(end != std::string::npos, "find the end of the lock example");
    const auto bytes = std::string_view(document).substr(content, end - content);
    const auto model = parse_lock(bytes, lock_file);
    check(model.schema.value == 1 && model.language.value == "0.3", "lock axes");
    check(model.contracts_digest && model.contracts_digest->value == digest('a'), "contracts.lock pin");
    check(model.dependencies.at("charts").value == digest('b'), "dependency pin");
    check(model.targets.at("server").components.at("postgresql").version.value == "18.7", "component version");
    check(model.targets.at("web").components.at("browser-runtime").digest.value == digest('d'), "component pin");
    check(write_lock(model) == bytes, "documented example is byte-for-byte canonical");
}

struct Fixture {
    std::string source = std::string(project) +
        "[dependencies.charts]\narchive = \"vendor/charts.tar.gz\"\ndigest = \"" + hex('b') +
        "\"\n[contracts]\nmanifest = \"contracts/contracts.toml\"\n"
        "[targets.web]\nkind = \"web\"\npackage = \"stock\"\nentry = \"Stock.Main\"\ncapabilities = [\"ui\"]\n";
    Manifest manifest = parse_manifest(source, manifest_file);
    ResolvedLockInputs resolved{
        {{"charts", digest('b')}},
        {{"web", {{"browser-runtime", {"0.3.0", digest('c')}}}}},
        digest('a')
    };
};

void canonical_round_trips() {
    const auto shared = parse_manifest(project, manifest_file);
    const auto empty = make_lock(shared, {});
    check(write_lock(empty) == minimal, "minimal lock");
    const auto parsed_empty = parse_lock(minimal, lock_file);
    validate_lock(&parsed_empty, shared, {});

    Fixture fixture;
    auto resolved = fixture.resolved;
    resolved.targets.at("web").emplace("zeta", ResolvedComponent{"1+meta", digest('f')});
    resolved.targets.at("web").emplace("alpha", ResolvedComponent{"18.7", digest('d')});
    const auto output = write_lock(make_lock(fixture.manifest, resolved));
    const auto parsed = parse_lock(output, lock_file);
    validate_lock(&parsed, fixture.manifest, resolved);
    check(write_lock(parsed) == output, "generated lock round trip");
    check(output.find(".\"alpha\"]") < output.find(".\"browser-runtime\"]") &&
              output.find(".\"browser-runtime\"]") < output.find(".\"zeta\"]"), "component order");
    check(output.find("vendor/") == std::string::npos && output.find("contracts/contracts.toml") == std::string::npos,
          "lock omits dependency and manifest paths");

    auto reordered = std::string("language = \"0.3\"\r\nschema = 1 # dimensions\r\n"
        "[components.\"é.name\".\"z\\\"eta\"]\r\ndigest = \"") + hex('f') + "\"\r\nversion = \"1+meta\"\r\n"
        "[components.\"é.name\".\"alpha\"]\r\nversion = \"18.7\"\r\ndigest = \"" + hex('d') + "\"\r\n"
        "[components.\"a\"]\r\n"
        "[dependencies]\r\nz = \"" + hex('b') + "\"\r\n\"a\\u0000\" = \"" + hex('a') + "\"\r\n";
    const auto canonical = write_lock(parse_lock(reordered, lock_file));
    const auto normalized = "schema = 1\nlanguage = \"0.3\"\n\n[dependencies]\n\"a\\u0000\" = \"" + hex('a') +
        "\"\n\"z\" = \"" + hex('b') + "\"\n\n[components.\"a\"]\n"
        "\n[components.\"é.name\".\"alpha\"]\nversion = \"18.7\"\ndigest = \"" + hex('d') +
        "\"\n\n[components.\"é.name\".\"z\\\"eta\"]\nversion = \"1+meta\"\ndigest = \"" + hex('f') + "\"\n";
    check(canonical == normalized, "decoder order, escaped keys, UTF-8 and LF canonicalization");
    check(write_lock(parse_lock(canonical, lock_file)) == canonical, "canonical bytes are idempotent");

    auto no_components = fixture.resolved;
    no_components.targets.at("web").clear();
    const auto locked_none = write_lock(make_lock(fixture.manifest, no_components));
    check(locked_none.ends_with("[components.\"web\"]\n"), "zero components have an explicit target");
    const auto parsed_none = parse_lock(locked_none, lock_file);
    validate_lock(&parsed_none, fixture.manifest, no_components);

    std::string arbitrary = "\"\\é\n\x7f";
    arbitrary.push_back('\0');
    const auto quoted = quote_toml_string(arbitrary);
    const auto encoded = parse_toml("x = " + quoted, lock_file);
    check(std::get<std::string>(std::get<TomlValue>(encoded.root().entries.at("x").item).value) == arbitrary,
          "writer escapes arbitrary basic-string content");
    try {
        quote_toml_string("\xc0\xaf");
        check(false, "writer accepted invalid UTF-8");
    } catch (const std::invalid_argument&) {}
}

void malformed_inputs() {
    const auto parse_reject = [](std::string_view source, std::string_view code,
                                 std::string_view token, std::string_view reason) {
        const auto problem = reject([&] { parse_lock(source, lock_file); }, code, reason);
        at(problem, source, lock_file, token);
    };
    parse_reject("schema = 2\nlanguage = \"0.3\"\n", "E1022", "2", "lock schema");
    parse_reject("schema = 1\nlanguage = \"0.4\"\n", "E1022", "\"0.4\"", "lock language");
    parse_reject("schema = \"1\"\nlanguage = \"0.3\"\n", "E1020", "schema", "integer schema");
    parse_reject("schema = 1\nlanguage = true\n", "E1020", "language", "language version string");
    for (const auto key : {"time", "path", "machine", "source"}) {
        parse_reject(std::string(minimal) + key + " = \"unknown\"\n", "E1020", key, "unknown lock-file key");
    }
    parse_reject(std::string(minimal) + "dependencies = true\n", "E1020", "dependencies", "lock-file table");
    parse_reject(std::string(minimal) + "[[components.web]]\n", "E1020", "web", "lock-file table");
    parse_reject(std::string(minimal) + "[dependencies]\ncharts = 5\n", "E1020", "charts", "digest string");
    parse_reject(std::string(minimal) + "[components.web.engine]\nversion = \"1\"\n",
                 "E1020", "[components.web.engine]", "missing lock-file key 'digest'");
    parse_reject(std::string(minimal) + "[components.web.engine]\nversion = \"\"\ndigest = \"" + hex('c') + "\"\n",
                 "E1020", "\"\"", "component version token");
    parse_reject(std::string(minimal) + "[components.web.engine]\nversion = \"../local\"\ndigest = \"" + hex('c') + "\"\n",
                 "E1020", "\"../local\"", "component version token");
    parse_reject(std::string(minimal) + "[components.web.engine]\nlocal_path = \"root\"\n",
                 "E1020", "local_path", "unknown lock-file key");
    for (const auto& spelling : {hex('B'), hex('g'), hex('a') + 'a', hex('a').substr(1), "sha256:" + hex('a')}) {
        const auto quoted = "\"" + spelling + "\"";
        parse_reject(std::string(minimal) + "contracts_digest = " + quoted + "\n",
                     "E1020", quoted, "64 lowercase hexadecimal digits");
    }
    const auto repeated = std::string(minimal) + "[dependencies]\nx = \"" + hex('a') + "\"\nx = \"" + hex('b') + "\"\n";
    const auto duplicate = reject([&] { parse_lock(repeated, lock_file); }, "E1020", "already defined");
    at(duplicate, repeated, lock_file, "x");
    check(duplicate.data()->labels.size() == 1 && duplicate.data()->labels.front().span.offset == repeated.find("x ="),
          "duplicate pin keeps the earlier key span");
}

void changed_metadata() {
    Fixture fixture;
    const auto output = write_lock(make_lock(fixture.manifest, fixture.resolved));
    const auto missing = reject([&] { validate_lock(nullptr, fixture.manifest, fixture.resolved); },
                                "E1022", "neura.lock is missing");
    at(missing, fixture.source, manifest_file, "\"0.3\"");
    check(!missing.data()->helps.empty(), "missing lock indicates how to update");
    const auto validate = [&](std::string_view bytes) {
        const auto lock = parse_lock(bytes, lock_file);
        validate_lock(&lock, fixture.manifest, fixture.resolved);
    };
    const auto missing_pin = replace(output, "\"charts\" = \"" + hex('b') + "\"\n", "");
    at(reject([&] { validate(missing_pin); }, "E1022", "missing dependency"), fixture.source, manifest_file,
       "\"" + hex('b') + "\"");
    const auto stale_pin = replace(output, hex('b'), hex('f'));
    const auto stale = reject([&] { validate(stale_pin); }, "E1022", "manifest pins " + hex('b'));
    at(stale, stale_pin, lock_file, "\"" + hex('f') + "\"");
    check(std::string_view(stale.what()).find(hex('f')) != std::string_view::npos, "stale dependency includes both pins");
    const auto extra_pin = replace(output, "[dependencies]\n", "[dependencies]\nextra = \"" + hex('e') + "\"\n");
    at(reject([&] { validate(extra_pin); }, "E1022", "undeclared dependency"), extra_pin, lock_file,
       "\"" + hex('e') + "\"");
    const auto removed_target = output.substr(0, output.find("[components."));
    at(reject([&] { validate(removed_target); }, "E1022", "missing target"), fixture.source, manifest_file,
       "[targets.web]");
    const auto extra_target = output + "\n[components.\"other\"]\n";
    at(reject([&] { validate(extra_target); }, "E1022", "undeclared target"), extra_target, lock_file,
       "[components.\"other\"]");
    const auto removed_component = removed_target + "[components.web]\n";
    at(reject([&] { validate(removed_component); }, "E1022", "missing component"), fixture.source, manifest_file,
       "[targets.web]");
    const auto new_component = output + "\n[components.web.extra]\nversion = \"2\"\ndigest = \"" + hex('e') + "\"\n";
    at(reject([&] { validate(new_component); }, "E1022", "unused component"), new_component, lock_file,
       "[components.web.extra]");
    const auto older_component = replace(output, "\"0.3.0\"", "\"0.2.0\"");
    at(reject([&] { validate(older_component); }, "E1022", "installed version is '0.3.0'"), older_component, lock_file,
       "\"0.2.0\"");
    const auto other_component_digest = replace(output, hex('c'), hex('d'));
    at(reject([&] { validate(other_component_digest); }, "E1022", "installed SHA-256 is " + hex('c')),
       other_component_digest, lock_file, "\"" + hex('d') + "\"");
    const auto missing_contracts = replace(output, "contracts_digest = \"" + hex('a') + "\"\n", "");
    at(reject([&] { validate(missing_contracts); }, "E1022", "missing the contracts.lock digest"),
       fixture.source, manifest_file, "\"contracts/contracts.toml\"");

    auto resolved = fixture.resolved;
    resolved.contracts_digest.reset();
    const auto original = parse_lock(output, lock_file);
    at(reject([&] { validate_lock(&original, fixture.manifest, resolved); }, "E1022", "absent contracts.lock"),
       output, lock_file, "\"" + hex('a') + "\"");
    const auto omitted = parse_lock(missing_contracts, lock_file);
    validate_lock(&omitted, fixture.manifest, resolved);

    resolved.targets.clear();
    reject([&] { make_lock(fixture.manifest, resolved); }, "E1022", "resolution is missing for target");
    resolved = fixture.resolved;
    resolved.targets.try_emplace("extra");
    reject([&] { make_lock(fixture.manifest, resolved); }, "E1022", "resolution contains an undeclared target");
    resolved = fixture.resolved;
    resolved.dependencies.clear();
    reject([&] { validate_lock(&original, fixture.manifest, resolved); }, "E1032", "was not resolved");
    resolved = fixture.resolved;
    resolved.dependencies.emplace("extra", digest('f'));
    reject([&] { make_lock(fixture.manifest, resolved); }, "E1022", "resolution contains an undeclared dependency");
    const auto invalid_manifest = parse_manifest(replace(fixture.source, hex('b'), hex('B')), manifest_file);
    const auto pin_shape = reject([&] { make_lock(invalid_manifest, fixture.resolved); }, "E1032", "requires a SHA-256 pin");
    check(pin_shape.data()->labels.size() == 1 && pin_shape.data()->labels.front().span.file == manifest_file,
          "invalid declared pin labels the manifest");
}

void changed_content() {
    Fixture fixture;
    const auto bytes = write_lock(make_lock(fixture.manifest, fixture.resolved));
    const auto lock = parse_lock(bytes, lock_file);
    const auto check_digests = [](const Diagnostic& error, char expected, char found) {
        check(error.data()->args.at("expected_digest") == hex(expected) &&
                  error.data()->args.at("found_digest") == hex(found) &&
                  std::string_view(error.what()).find(hex(expected)) != std::string_view::npos &&
                  std::string_view(error.what()).find(hex(found)) != std::string_view::npos,
              "E1026 preserves exact expected and found digests");
    };
    fixture.resolved.dependencies.insert_or_assign("charts", digest('f'));
    const auto dependency = reject([&] { validate_lock(&lock, fixture.manifest, fixture.resolved); },
                                   "E1026", "dependency 'charts'");
    at(dependency, bytes, lock_file, "\"" + hex('b') + "\"");
    check_digests(dependency, 'b', 'f');
    const auto regenerated = reject([&] { make_lock(fixture.manifest, fixture.resolved); }, "E1026", "dependency 'charts'");
    at(regenerated, fixture.source, manifest_file, "\"" + hex('b') + "\"");
    check_digests(regenerated, 'b', 'f');
    fixture.resolved.dependencies.insert_or_assign("charts", digest('b'));
    fixture.resolved.contracts_digest = digest('e');
    const auto contracts = reject([&] { validate_lock(&lock, fixture.manifest, fixture.resolved); },
                                  "E1026", "contracts.lock");
    at(contracts, bytes, lock_file, "\"" + hex('a') + "\"");
    check_digests(contracts, 'a', 'e');
}

} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "usage: neura_project_lock_tests <Projects030.md>");
        documented_lock(argv[1]);
        canonical_round_trips();
        malformed_inputs();
        changed_metadata();
        changed_content();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "project lock: " << error.what() << '\n';
        return 1;
    }
}

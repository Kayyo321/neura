#include "project/bundle.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <stdexcept>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace neura;
using namespace neura::project;

constexpr std::string_view image_path = "app.fixture.nvr";
constexpr std::string_view asset_path = "assets/caf\xc3\xa9 space.svg";
constexpr std::string_view image_bytes{"NEUR\0\x01\xff\r\n", 9};
constexpr std::string_view wasm_bytes{"\0asm\x01\0\0\0", 8};
constexpr std::string_view html_bytes = "<!doctype html><p>ok";
constexpr std::string_view asset_bytes = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";

// Independent hashlib pins for binary and UTF-8 fixtures and the exact
// compact, key-sorted JSON document produced by Python's json.dumps.
constexpr std::string_view image_pin = "efca5fd2a04eca9fe31f828687a31318cbff6a33b5cb5b1b6d34d214bf946916";
constexpr std::string_view asset_pin = "900fbe934249ad120004bd24adf66aad8817d89586273c0cc50e187bddebb601";
constexpr std::string_view html_pin = "8d2d42e718c9adbbfc39db9c602d2b4afd9091f7644af60de146315694b8c7b2";
constexpr std::string_view wasm_pin = "93a44bbb96c751218e4c00d479e4c14358122a389acca16205b1e4d0dc5f9476";
constexpr std::string_view manifest_pin = "4a722b868549c8aa3e93a05304995698b8c2b3a1bb562c24856289d64d9ebf4a";
constexpr std::string_view empty_pin = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

void check(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

template <typename Operation>
void reject(Operation action, std::string_view code, std::string_view reason) {
    try {
        action();
    } catch (const Diagnostic& error) {
        check(error.data() && error.data()->code == code, std::string("wrong code: ") + error.what());
        check(std::string_view(error.what()).find(reason) != std::string_view::npos,
              std::string("wrong reason: ") + error.what());
        return;
    }
    throw std::runtime_error("accepted invalid bundle: " + std::string(reason));
}

fs::path utf8(std::string_view value) { return fs::path(std::u8string(value.begin(), value.end())); }
Sha256Digest pin(std::string_view value) { return Sha256Digest::parse(value).value(); }

class Sandbox {
public:
    Sandbox() {
        const auto prefix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = fs::temp_directory_path() / ("neura-bundle-test-" + prefix + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("cannot reserve a bundle fixture directory");
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
    check(out.good(), "write a bundle fixture");
}

std::string replace(std::string input, std::string_view before, std::string_view after) {
    const auto found = input.find(before);
    check(found != std::string::npos, "find the canonical JSON field to change");
    input.replace(found, before.size(), after);
    return input;
}

BundleManifest model() {
    BundleManifest result{TargetKind::Web, "example.neura.stock", "stock", "1.2.3", "0.3", {std::string(image_path), pin(image_pin)}};
    result.capabilities = {Capability::Ui, Capability::Bridge};
    result.origins["connect"] = {"https://other.example.org", "https://api.example.org"};
    result.requirements["browserAdapter"] = 1;
    result.limits["W-1"] = std::uint32_t{10000};
    result.contracts["parts"] = 1;
    result.csp = "default-src 'none'";
    result.files.emplace(image_path, BundleFile{pin(image_pin), image_bytes.size(), "application/octet-stream", true});
    result.files.emplace(asset_path, BundleFile{pin(asset_pin), asset_bytes.size(), "image/svg+xml", false});
    result.files.emplace("nvm.fixture.wasm", BundleFile{pin(wasm_pin), wasm_bytes.size(), "application/wasm", true});
    result.files.emplace("index.html", BundleFile{pin(html_pin), html_bytes.size(), "text/html", true});
    return result;
}

void tree(const fs::path& root, std::string_view json, bool reverse = false) {
    if (reverse) write(root / "index.html", html_bytes);
    write(root / utf8(asset_path), asset_bytes);
    write(root / utf8(image_path), image_bytes);
    write(root / "nvm.fixture.wasm", wasm_bytes);
    if (!reverse) write(root / "index.html", html_bytes);
    write(root / "bundle.json", json);
}

struct GroupedNumbers : std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};

void encoding() {
    auto bundle = model();
    const auto before = std::locale::global(std::locale(std::locale::classic(), new GroupedNumbers));
    const auto bytes = write_bundle_manifest(bundle);
    std::locale::global(before);
    check(bytes.size() == 1099 && bytes.starts_with(R"({"capabilities":["bridge","ui"],"contracts":[)"), "canonical byte order");
    check(bytes.ends_with(R"("schema":1,"version":"1.2.3"})"), "canonical final key and no trailing bytes");
    const auto parsed = parse_bundle_manifest(bytes);
    check(write_bundle_manifest(parsed) == bytes && parsed.image.digest == pin(image_pin), "exact canonical round trip");

    reject([&] { parse_bundle_manifest(bytes + "\n"); }, "E1029", "not canonical");
    auto order = replace(bytes, R"("bridge","ui")", R"("ui","bridge")");
    reject([&] { parse_bundle_manifest(order); }, "E1029", "not canonical");
    auto fields = replace(bytes, R"("schema":1,"version":"1.2.3")", R"("version":"1.2.3","schema":1)");
    reject([&] { parse_bundle_manifest(fields); }, "E1029", "not canonical");
    auto duplicate = replace(bytes, R"("schema":1)", R"("schema":1,"schema":1)");
    reject([&] { parse_bundle_manifest(duplicate); }, "E1029", "duplicate JSON key");
    const auto row = bytes.substr(bytes.find(R"({"bytes":9)"), bytes.find('}', bytes.find(R"({"bytes":9)")) - bytes.find(R"({"bytes":9)") + 1);
    reject([&] { parse_bundle_manifest(replace(bytes, row, row + "," + row)); }, "E1029", "duplicate bundle file");
    reject([&] { parse_bundle_manifest(replace(bytes, R"("schema":1)", R"("schema":2)")); }, "E1021", "schema 2");
    reject([&] { parse_bundle_manifest(replace(bytes, R"("schema":1)", R"("schema":1.0)")); }, "E1029", "integer");
    reject([&] { parse_bundle_manifest(replace(bytes, "browserAdapter\":1", "browserAdapter\":{}")); }, "E1029", "browserAdapter");
    reject([&] { parse_bundle_manifest(replace(bytes, R"("bridge","ui")", R"("fs","ui")")); }, "E1029", "fs");
    reject([&] { parse_bundle_manifest(replace(bytes, R"("schema":1)", R"("schema":1,"source":"/checkout/x")")); }, "E1029", "unknown bundle field 'source'");
    reject([&] { parse_bundle_manifest(replace(bytes, "café", "caf\xff")); }, "E1029", "not UTF-8");
    reject([&] { parse_bundle_manifest(replace(bytes, image_pin, std::string(64, 'A'))); }, "E1029", "lowercase SHA-256");

    for (const auto path : {"../escape", "/absolute", "assets//double", "C:/absolute", "assets\\file", "nul.txt", "bundle.json"}) {
        bundle.files.emplace(path, BundleFile{pin(empty_pin), 0, "application/octet-stream", false});
        reject([&] { write_bundle_manifest(bundle); }, "E1029", "invalid bundle file path");
        bundle.files.erase(path);
    }
    bundle.files.emplace("Assets/other", BundleFile{pin(empty_pin), 0, "application/octet-stream", false});
    reject([&] { write_bundle_manifest(bundle); }, "E1029", "differ only in case");
    bundle.files.erase("Assets/other");
    bundle.files.at("nvm.fixture.wasm").media_type = "application/octet-stream";
    reject([&] { write_bundle_manifest(bundle); }, "E1029", "wrong media type");
    bundle.files.at("nvm.fixture.wasm").media_type = "application/wasm";
    bundle.image.digest = pin(empty_pin);
    reject([&] { write_bundle_manifest(bundle); }, "E1029", "image path or digest");
    bundle.image.digest = pin(image_pin);
    bundle.limits["W-1"] = std::uint32_t{1073741825};
    reject([&] { write_bundle_manifest(bundle); }, "E1028", "W-1");
    bundle.limits["W-1"] = std::uint32_t{41};
    check(!write_bundle_manifest(bundle).empty(), "asset byte count exactly at W-1");
    bundle.limits["W-1"] = std::uint32_t{40};
    reject([&] { write_bundle_manifest(bundle); }, "E1029", "exceeds W-1");

    bundle.kind = TargetKind::Desktop;
    bundle.limits.clear();
    bundle.csp.reset();
    bundle.capabilities = {Capability::Desktop};
    reject([&] { write_bundle_manifest(bundle); }, "E1029", "embedded web bundle digest");
    bundle.ui = pin(manifest_pin);
    const auto desktop = parse_bundle_manifest(write_bundle_manifest(bundle));
    check(desktop.ui == pin(manifest_pin), "desktop UI digest metadata");
}

void directory() {
    Sandbox sandbox;
    auto bundle = model();
    const auto bytes = write_bundle_manifest(bundle);
    const auto first = sandbox.path / "root one";
    const auto second = sandbox.path / utf8("root deux \xc3\xa9");
    tree(first, bytes);
    tree(second, write_bundle_manifest(model()), true);
    const BundleVerification policy{{{"browserAdapter", 1}}, pin(manifest_pin), TargetKind::Web, bundle.id};
    const auto verify = [&] { return verify_bundle_directory(first, policy); };
    const auto stored = verify();
    const auto rebuilt = verify_bundle_directory(second, policy);
    check(stored.digest() == pin(manifest_pin) && rebuilt.digest() == stored.digest(), "independent roots reproduce canonical SHA");
    const auto loaded = stored.read_file(image_path);
    check(std::string_view(reinterpret_cast<const char*>(loaded.data()), loaded.size()) == image_bytes, "exact checked image bytes");
    reject([&] { stored.read_file("../outside"); }, "E1029", "unlisted bundle file");

    write(first / "extra", "x");
    reject(verify, "E1029", "unlisted file");
    fs::remove(first / "extra");
    fs::create_directory(first / "empty extra");
    reject(verify, "E1029", "unlisted directory");
    fs::remove(first / "empty extra");
    write(first / utf8(asset_path), std::string(asset_bytes.size(), 'x'));
    reject(verify, "E1029", "SHA-256 mismatch for bundle file 'assets/");
    reject([&] { stored.read_file(asset_path); }, "E1029", "SHA-256 mismatch");
    auto wrong = policy;
    wrong.supported_requirements["browserAdapter"] = 2;
    reject([&] { verify_bundle_directory(first, wrong); }, "E1031", "needs version 1, host has 2");
    wrong = policy;
    wrong.supported_requirements.clear();
    reject([&] { verify_bundle_directory(first, wrong); }, "E1031", "host has unavailable");
    wrong = policy;
    wrong.manifest_digest = pin(empty_pin);
    reject([&] { verify_bundle_directory(first, wrong); }, "E1029", "SHA-256 mismatch for bundle.json");

    fs::remove(first / utf8(asset_path));
    reject(verify, "E1029", "missing bundle file 'assets/");
    write(first / utf8(asset_path), asset_bytes);
    fs::remove(first / "bundle.json");
    reject(verify, "E1029", "bundle.json");
    write(first / "bundle.json", bytes);

    bundle.files.emplace("debug.map.json", BundleFile{pin(empty_pin), 0, "application/json", false});
    write(first / "debug.map.json", "");
    write(first / "bundle.json", write_bundle_manifest(bundle));
    wrong = policy;
    wrong.manifest_digest.reset();
    check(verify_bundle_directory(first, wrong).manifest().image.digest == pin(image_pin), "separate source map preserves image pin");
    fs::remove(first / "debug.map.json");
    write(first / "bundle.json", bytes);

#ifndef _WIN32
    fs::resize_file(first / utf8(asset_path), std::uint64_t{2} << 30);
    reject(verify, "E1029", "exceeds W-1");
    fs::remove(first / utf8(asset_path));
    fs::create_symlink(second / utf8(asset_path), first / utf8(asset_path));
    reject(verify, "E1029", "symbolic link");
    reject([&] { stored.read_file(asset_path); }, "E1029", "symbolic link");
    fs::remove_all(first / "assets");
    fs::create_directory_symlink(second / "assets", first / "assets");
    reject(verify, "E1029", "symbolic link");
    fs::remove(first / "assets");
    write(first / utf8(asset_path), asset_bytes);
    const auto alias = sandbox.path / "linked-root";
    fs::create_directory_symlink(first, alias);
    reject([&] { verify_bundle_directory(alias / ".", policy); }, "E1029", "root must be an existing directory without links");
    fs::remove(first / "index.html");
    check(::mkfifo((first / "index.html").c_str(), 0600) == 0, "create a pipe fixture");
    reject(verify, "E1029", "non-regular entry");
#endif
}

} // namespace

int main() {
    try {
        encoding();
        directory();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

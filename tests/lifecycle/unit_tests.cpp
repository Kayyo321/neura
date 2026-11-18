// Unit tests for the pure parts of the install lifecycle: SHA-256, version
// precedence, the release metadata and install marker parsers, and the
// update-check state. The commands that use them are tested through the
// installed tool by the lifecycle-* CMake scripts.
//
// Usage: neura_lifecycle_tests <sha256|semver|release-metadata|marker|manifest|journal|generation>

#include "cli/install_layout.hpp"
#include "cli/release_channel.hpp"
#include "util/sha256.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using namespace neura::lifecycle;

int failures = 0;
int components_metadata_cases();

void check(bool condition, const std::string& what, int line) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL line " << line << ": " << what << '\n';
}

#define CHECK(condition) check((condition), #condition, __LINE__)

int sha256_suite() {
    using neura::util::sha256_hex;
    // FIPS 180-4 and NIST CAVS examples.
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    {
        neura::util::Sha256 hash;
        const std::string block(1000, 'a');
        for (int i = 0; i < 1000; ++i) hash.update(block);
        const auto digest = hash.finish();
        std::string hex;
        for (const auto byte : digest) {
            hex += "0123456789abcdef"[byte >> 4];
            hex += "0123456789abcdef"[byte & 15];
        }
        CHECK(hex == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }
    // The same input split at every length across a block boundary.
    {
        const std::string text(200, 'x');
        const auto whole = sha256_hex(text);
        for (std::size_t cut = 0; cut <= text.size(); ++cut) {
            neura::util::Sha256 hash;
            hash.update(std::string_view(text).substr(0, cut));
            hash.update(std::string_view(text).substr(cut));
            const auto digest = hash.finish();
            std::string hex;
            for (const auto byte : digest) {
                hex += "0123456789abcdef"[byte >> 4];
                hex += "0123456789abcdef"[byte & 15];
            }
            CHECK(hex == whole);
        }
    }
    // Messages whose padding spills into a second block (55, 56, 63, 64 bytes).
    CHECK(sha256_hex(std::string(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(sha256_hex(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(sha256_hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    {
        const auto path = fs::temp_directory_path() / ("neura-sha256-test-" + std::to_string(::getpid()));
        {
            std::ofstream out(path, std::ios::binary);
            out << "abc";
        }
        const auto digest = neura::util::sha256_file(path);
        CHECK(digest && digest->size == 3 && digest->hex == sha256_hex("abc"));
        fs::remove(path);
        CHECK(!neura::util::sha256_file(path));
    }
    return failures;
}

int compare(const char* a, const char* b) {
    const auto left = parse_semver(a);
    const auto right = parse_semver(b);
    if (!left || !right) {
        ++failures;
        std::cout << "FAIL: cannot parse " << a << " or " << b << '\n';
        return 99;
    }
    return compare_semver(*left, *right);
}

int semver_suite() {
    // The precedence chain from semver.org section 11.
    const char* chain[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
                           "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"};
    for (std::size_t i = 0; i < std::size(chain); ++i) {
        for (std::size_t j = 0; j < std::size(chain); ++j) {
            CHECK(compare(chain[i], chain[j]) == (i < j ? -1 : i > j ? 1 : 0));
        }
    }
    CHECK(compare("1.2.3", "1.2.3") == 0);
    CHECK(compare("1.10.0", "1.9.9") == 1);
    CHECK(compare("2.0.0", "1.99.99") == 1);
    CHECK(compare("0.1.0-dev", "0.1.0") == -1);
    CHECK(compare("0.1.0", "0.1.1-dev") == -1);
    CHECK(compare("0.1.0-dev", "0.1.0-rc.1") == -1);
    CHECK(compare("0.1.0-rc.2", "0.1.0-rc.10") == -1);
    CHECK(compare("1.0.0-1", "1.0.0-a") == -1);
    CHECK(compare("1.0.0-a-b", "1.0.0-a") == 1);
    CHECK(compare("18446744073709551615.0.0", "18446744073709551614.0.0") == 1);
    CHECK(compare("1.0.0-99999999999999999999999", "1.0.0-100000000000000000000000") == -1);
    CHECK(is_newer("9.9.9", "0.1.0-dev"));
    CHECK(is_newer("0.1.0", "0.1.0-dev"));
    CHECK(!is_newer("0.1.0-dev", "0.1.0"));
    CHECK(!is_newer("0.1.0", "0.1.0"));
    CHECK(!is_newer("garbage", "0.1.0"));
    CHECK(!is_newer("0.2.0", "garbage"));
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03", "1.2.3-", "1.2.3-01", "1.2.3-a..b",
                            "1.2.3-a.", "1.2.3+build", "1.2.3-a+b", "v1.2.3", "1.2.-3", "1.2.3 ", "1.2.x", "-1.2.3",
                            "18446744073709551616.0.0", "1.2.3-é"}) {
        CHECK(!parse_semver(bad));
    }
    CHECK(parse_semver("0.0.0") && parse_semver("1.2.3-0") && parse_semver("1.2.3-0a") && parse_semver("1.2.3-x.y.0"));
    // GitHub publication order is not semantic version order. A later page
    // can contain the newest candidate; a stable release beats its own RC.
    std::string error;
    const auto first_page = parse_github_release_page(R"([
        {"tag_name":"v1.2.3-rc.2","draft":false,"prerelease":true,"published_at":"2026-11-12T00:00:00Z"},
        {"tag_name":"v1.2.3","draft":false,"prerelease":false,"published_at":"2026-11-11T00:00:00Z"},
        {"tag_name":"v99.0.0","draft":true,"prerelease":false,"published_at":"2026-11-13T00:00:00Z"},
        {"tag_name":"v98.0.0","draft":false,"prerelease":true,"published_at":null},
        {"tag_name":"../../evil","draft":false,"prerelease":true,"published_at":"today"}
    ])", error);
    CHECK(first_page && first_page->count == 5 && first_page->releases.size() == 2);
    std::optional<PublishedRelease> selected;
    if (first_page) select_newest_release(selected, *first_page);
    CHECK(selected && selected->version == "1.2.3" && selected->tag == "v1.2.3");
    const auto next_page = parse_github_release_page(R"([
        {"tag_name":"1.3.0-rc.10","draft":false,"prerelease":true,"published_at":"2026-11-10T00:00:00Z"},
        {"tag_name":"v1.3.0-rc.2","draft":false,"prerelease":true,"published_at":"2026-11-12T00:00:00Z"}
    ])", error);
    CHECK(next_page.has_value());
    if (next_page) select_newest_release(selected, *next_page);
    CHECK(selected && selected->version == "1.3.0-rc.10" && selected->tag == "1.3.0-rc.10");
    for (const char* bad : {"", "{}", "null", "[null]", "[{\"tag_name\":\"v9.0.0\"}]",
                            "[{\"tag_name\":\"v9.0.0\",\"draft\":\"false\",\"prerelease\":true}]"}) {
        error.clear();
        CHECK(!parse_github_release_page(bad, error));
        CHECK(!error.empty());
    }
    const auto empty_page = parse_github_release_page("[]", error);
    CHECK(empty_page && empty_page->count == 0 && empty_page->releases.empty());
    return failures;
}

const std::string digest_a(64, 'a');
const std::string digest_b = std::string(63, 'b') + "c";

int release_metadata_suite() {
    std::string error;
    const std::string good = "neura-release 1\nversion 0.2.0\nasset x86_64-linux-gnu neura-0.2.0-x86_64-linux-gnu.tar.gz " +
                             digest_a + " 1234\nasset aarch64-apple-darwin neura-0.2.0-aarch64-apple-darwin.tar.gz " + digest_b +
                             " 1073741824\n";
    const auto parsed = parse_release_metadata(good, error);
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->version == "0.2.0");
        CHECK(parsed->assets.size() == 2);
        const auto* asset = parsed->find("x86_64-linux-gnu");
        CHECK(asset && asset->file == "neura-0.2.0-x86_64-linux-gnu.tar.gz" && asset->sha256 == digest_a && asset->size == 1234);
        CHECK(parsed->find("aarch64-apple-darwin") && parsed->find("aarch64-apple-darwin")->size == 1073741824);
        CHECK(!parsed->find("riscv64-linux-gnu"));
    }
    CHECK(parse_release_metadata("neura-release 1\nversion 1.0.0-rc.1\n", error).has_value());

    const std::string header = "neura-release 1\nversion 0.2.0\n";
    const std::string t = "asset x86_64-linux-gnu f.tar.gz ";
    const struct {
        const char* why;
        std::string text;
    } rejected[] = {
        {"empty", ""},
        {"no trailing newline", "neura-release 1\nversion 0.2.0"},
        {"wrong magic", "neura-release 2\nversion 0.2.0\n"},
        {"no version", "neura-release 1\n"},
        {"two versions", header + "version 0.2.1\n"},
        {"bad version", "neura-release 1\nversion 1.2\n"},
        {"asset before version", "neura-release 1\n" + t + digest_a + " 5\nversion 0.2.0\n"},
        {"unknown line kind", header + "note hello\n"},
        {"unknown line kind with asset fields", header + "signature x86_64-linux-gnu f " + digest_a + " 5\n"},
        {"duplicate target", header + t + digest_a + " 5\n" + t + digest_b + " 6\n"},
        {"uppercase hex", header + t + std::string(64, 'A') + " 5\n"},
        {"short hex", header + t + std::string(63, 'a') + " 5\n"},
        {"non-hex", header + t + std::string(63, 'a') + "g 5\n"},
        {"slash in file name", "neura-release 1\nversion 0.2.0\nasset x86_64-linux-gnu a/b.tar.gz " + digest_a + " 5\n"},
        {"dot-dot file name", "neura-release 1\nversion 0.2.0\nasset x86_64-linux-gnu ../b.tar.gz " + digest_a + " 5\n"},
        {"leading dot file name", "neura-release 1\nversion 0.2.0\nasset x86_64-linux-gnu .hidden " + digest_a + " 5\n"},
        {"leading dash file name", "neura-release 1\nversion 0.2.0\nasset x86_64-linux-gnu -o " + digest_a + " 5\n"},
        {"size over 1 GiB", header + t + digest_a + " 1073741825\n"},
        {"zero size", header + t + digest_a + " 0\n"},
        {"non-numeric size", header + t + digest_a + " 12x\n"},
        {"negative size", header + t + digest_a + " -5\n"},
        {"leading zero size", header + t + digest_a + " 05\n"},
        {"overflowing size", header + t + digest_a + " 99999999999999999999999\n"},
        {"missing field", header + t + digest_a + "\n"},
        {"extra field", header + t + digest_a + " 5 extra\n"},
        {"double space", header + "asset x86_64-linux-gnu  f.tar.gz " + digest_a + " 5\n"},
        {"tab", header + "asset\tx86_64-linux-gnu f.tar.gz " + digest_a + " 5\n"},
        {"carriage return", "neura-release 1\r\nversion 0.2.0\r\n"},
        {"blank line", header + "\n"},
        {"bad triple", header + "asset x86/64 f.tar.gz " + digest_a + " 5\n"},
    };
    for (const auto& [why, text] : rejected) {
        error.clear();
        const bool accepted = parse_release_metadata(text, error).has_value();
        if (accepted) {
            ++failures;
            std::cout << "FAIL: metadata accepted: " << why << '\n';
        } else if (error.empty()) {
            ++failures;
            std::cout << "FAIL: no error message for: " << why << '\n';
        }
    }
    return components_metadata_cases();
}

// Accepts every text of `cases` that should parse and rejects the others,
// each with a message.
template <typename Parse>
void expect_rejected(const char* what, Parse parse, std::initializer_list<std::pair<const char*, std::string>> cases) {
    for (const auto& [why, text] : cases) {
        std::string error;
        const bool accepted = parse(text, error).has_value();
        if (accepted) {
            ++failures;
            std::cout << "FAIL: " << what << " accepted: " << why << '\n';
        } else if (error.empty()) {
            ++failures;
            std::cout << "FAIL: no error message for " << what << ": " << why << '\n';
        }
    }
}

int marker_suite() {
    std::string error;
    // Schema 2 round trip.
    Marker marker;
    marker.method = "archive";
    marker.bin = "/home/u/my bin";
    marker.components = {"sdk", "core"};
    marker.generations = {"0.3.0+1a2b3c4d5e6f", "0.2.1-rc.3"};
    marker.links = {"neurac", "neura", "neura-doctor"};
    const auto text = format_marker(marker);
    CHECK(text == "neura-install 2\nmethod archive\nbin /home/u/my bin\ncomponents core sdk\ngeneration 0.2.1-rc.3\n"
                  "generation 0.3.0+1a2b3c4d5e6f\nlink neura\nlink neura-doctor\nlink neurac\n");
    const auto parsed = parse_marker(text, error);
    CHECK(parsed && parsed->schema == 2 && parsed->method == "archive" && parsed->bin == "/home/u/my bin" &&
          parsed->components == std::vector<std::string>({"core", "sdk"}) && parsed->generations.size() == 2 && parsed->links.size() == 3);
    CHECK(parsed && format_marker(*parsed) == text);
    CHECK(parse_marker("neura-install 2\nmethod archive\nbin /b\ncomponents core service\n", error).has_value());

    // Schema 1, which 0.2 wrote, reads as one link named by its path.
    const std::string legacy = "neura-install 1\nmethod archive\nlink /home/u/.local/bin/neura\nversion 0.1.0\nversion 0.2.0-rc.1\n";
    const auto old = parse_marker(legacy, error);
    CHECK(old && old->schema == 1 && old->link == "/home/u/.local/bin/neura" && old->bin == "/home/u/.local/bin" &&
          old->links == std::vector<std::string>{"neura"} && old->generations == std::vector<std::string>({"0.1.0", "0.2.0-rc.1"}) &&
          old->components.empty());
    CHECK(parse_marker("neura-install 1\nmethod homebrew\nlink /opt/bin/neura\nversion 1.0.0\n", error)->method == "homebrew");
    const auto spaced = parse_marker("neura-install 1\nmethod archive\nlink /home/a b/bin/neura\nversion 1.0.0\n", error);
    CHECK(spaced && spaced->link == "/home/a b/bin/neura");

    expect_rejected("marker", parse_marker, {
        {"empty", ""},
        {"schema 1, no lines", "neura-install 1\n"},
        {"schema 1, no version", "neura-install 1\nmethod archive\nlink /b/neura\n"},
        {"schema 1 lines under schema 2", "neura-install 2\nmethod archive\nlink /b/neura\nversion 1.0.0\n"},
        {"schema 3", "neura-install 3\nmethod archive\nbin /b\ncomponents core sdk\n"},
        {"schema 1 out of order", "neura-install 1\nversion 1.0.0\nmethod archive\nlink /b/neura\n"},
        {"schema 1 relative link", "neura-install 1\nmethod archive\nlink b/neura\nversion 1.0.0\n"},
        {"schema 1 no final newline", "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0"},
        {"schema 1 extra line", "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0\nextra thing\n"},
        {"schema 1 repeated version", "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0\nversion 1.0.0\n"},
        {"schema 1 path as version", "neura-install 1\nmethod archive\nlink /b/neura\nversion ../../etc\n"},
        {"schema 1 short version", "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0\n"},
        {"uppercase method", "neura-install 1\nmethod Archive\nlink /b/neura\nversion 1.0.0\n"},
        {"method with a space", "neura-install 1\nmethod arch ive\nlink /b/neura\nversion 1.0.0\n"},
        {"empty method", "neura-install 1\nmethod \nlink /b/neura\nversion 1.0.0\n"},
        {"carriage returns", "neura-install 1\r\nmethod archive\r\nlink /b/neura\r\nversion 1.0.0\r\n"},
        {"trailing space", "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0 \n"},
        {"tab in link", "neura-install 1\nmethod archive\nlink /b\tx\nversion 1.0.0\n"},
        {"schema 2 no components", "neura-install 2\nmethod archive\nbin /b\ncomponents\n"},
        {"schema 2 relative bin", "neura-install 2\nmethod archive\nbin b\ncomponents core sdk\n"},
        {"schema 2 components out of order", "neura-install 2\nmethod archive\nbin /b\ncomponents sdk core\n"},
        {"schema 2 repeated component", "neura-install 2\nmethod archive\nbin /b\ncomponents core core\n"},
        {"schema 2 bad generation", "neura-install 2\nmethod archive\nbin /b\ncomponents core sdk\ngeneration 0.3.0+xyz\n"},
        {"schema 2 generations unsorted",
         "neura-install 2\nmethod archive\nbin /b\ncomponents core sdk\ngeneration 0.3.0\ngeneration 0.2.0\n"},
        {"schema 2 link before generation",
         "neura-install 2\nmethod archive\nbin /b\ncomponents core sdk\nlink neura\ngeneration 0.3.0\n"},
        {"schema 2 repeated link", "neura-install 2\nmethod archive\nbin /b\ncomponents core sdk\nlink neura\nlink neura\n"},
        {"schema 2 link with a slash", "neura-install 2\nmethod archive\nbin /b\ncomponents core sdk\nlink ../neura\n"},
        {"schema 2 unknown line", "neura-install 2\nmethod archive\nbin /b\ncomponents core sdk\nversion 1.0.0\n"},
        {"schema 2 version line", "neura-install 2\nmethod archive\nlink /b/neura\ncomponents core sdk\n"},
    });

    CHECK(valid_component_set({"core", "sdk"}) && valid_component_set({"service", "core"}) && valid_component_set({"core", "sdk", "service"}));
    CHECK(!valid_component_set({"core"}) && !valid_component_set({"sdk"}) && !valid_component_set({"core", "sdk", "web"}) &&
          !valid_component_set({}));

    // Update-check state.
    UpdateCheck state{1700000000, "0.3.0"};
    CHECK(format_update_check(state) == "neura-update-check 1\nchecked 1700000000\nlatest 0.3.0\n");
    const auto reread = parse_update_check(format_update_check(state), error);
    CHECK(reread && reread->checked == 1700000000 && reread->latest == "0.3.0");
    for (const char* bad : {"", "neura-update-check 1\nchecked 1\n", "neura-update-check 1\nchecked x\nlatest 1.0.0\n",
                            "neura-update-check 1\nchecked 1\nlatest nope\n", "neura-update-check 2\nchecked 1\nlatest 1.0.0\n",
                            "neura-update-check 1\nchecked 1\nlatest 1.0.0\nmore\n"}) {
        CHECK(!parse_update_check(bad, error));
    }
    const std::int64_t day = 24 * 60 * 60;
    CHECK(update_check_due(std::nullopt, 5));
    CHECK(!update_check_due(state, state.checked + 6 * day));
    CHECK(update_check_due(state, state.checked + 7 * day));
    CHECK(update_check_due(state, state.checked - 1));

    // Finding the install root from an executable path.
    const auto root = fs::temp_directory_path() / ("neura-marker-test-" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "versions/0.1.0/bin");
    const auto executable = root / "versions/0.1.0/bin/neura";
    CHECK(detect_install(executable).kind == Detection::Kind::Unmanaged);
    {
        std::ofstream(root / "neura-install.txt") << "garbage\n";
    }
    CHECK(detect_install(executable).kind == Detection::Kind::Broken);
    {
        std::ofstream(root / "neura-install.txt") << legacy;
    }
    const auto detected = detect_install(executable);
    CHECK(detected.archive() && detected.root == root && detected.running_generation == "0.1.0");
    CHECK(detect_install(root / "versions/0.1.0/neura").kind == Detection::Kind::Unmanaged);
    CHECK(detect_install(root / "versions/not-a-version/bin/neura").kind == Detection::Kind::Unmanaged);
    CHECK(detect_install(root / "bin/neura").kind == Detection::Kind::Unmanaged);
    CHECK(detect_install("/").kind == Detection::Kind::Unmanaged);
    CHECK(detect_install("").kind == Detection::Kind::Unmanaged);
    {
        std::ofstream(root / "neura-install.txt") << "neura-install 1\nmethod deb\nlink /usr/bin/neura\nversion 0.1.0\n";
    }
    const auto managed = detect_install(executable);
    CHECK(managed.kind == Detection::Kind::Installed && !managed.archive() && managed.marker.method == "deb");
    // A 0.3 generation name and a schema 2 marker.
    fs::create_directories(root / "versions/0.3.0+0123456789ab/bin");
    {
        std::ofstream(root / "neura-install.txt") << text;
    }
    const auto generation = detect_install(root / "versions/0.3.0+0123456789ab/bin/neurac");
    CHECK(generation.archive() && generation.running_generation == "0.3.0+0123456789ab" && generation.marker.schema == 2);
    CHECK(detect_install(root / "versions/0.3.0+0123456789AB/bin/neura").kind == Detection::Kind::Unmanaged);
    CHECK(detect_install(root / "versions/0.3.0+0123/bin/neura").kind == Detection::Kind::Unmanaged);
    fs::remove_all(root);
    return failures;
}


const std::string digest_x = std::string(64, 'a');
const std::string digest_y = std::string(63, 'b') + "c";

std::string sdk_manifest_text() {
    return "neura-component 1\nname sdk\nversion 0.3.0\nbuild 3ffc2120c0ffee00000000000000000000000000\ntarget x86_64-linux-gnu\n"
           "requires core\ntool neura-test\ntool neurac\nfile 755 " + digest_x + " 10 bin/neura-test\nfile 755 " + digest_x +
           " 12 bin/neurac\nfile 644 " + digest_y + " 3 share/neura/stdlib/std/core/version.nra\n";
}

int manifest_suite() {
    std::string error;
    const auto text = sdk_manifest_text();
    const auto manifest = parse_manifest(text, error);
    CHECK(manifest.has_value());
    if (!manifest) {
        std::cout << error << '\n';
        return failures;
    }
    CHECK(manifest->name == "sdk" && manifest->version == "0.3.0" && manifest->target == "x86_64-linux-gnu");
    CHECK(manifest->requirements == std::vector<std::string>{"core"} && manifest->protocol.empty());
    CHECK(manifest->tools == std::vector<std::string>({"neura-test", "neurac"}));
    CHECK(manifest->files.size() == 3 && manifest->files[0].mode == 0755 && manifest->files[2].mode == 0644 && manifest->files[1].size == 12);
    CHECK(format_manifest(*manifest) == text);
    CHECK(check_known_manifest(*manifest).empty());

    const std::string service = "neura-component 1\nname service\nversion 0.3.0\nbuild abc\ntarget x86_64-linux-gnu\nrequires core\n"
                                "protocol service 1.0\ntool neurad\nfile 755 " + digest_x + " 1 bin/neurad\n";
    const auto parsed_service = parse_manifest(service, error);
    CHECK(parsed_service && parsed_service->protocol == "service 1.0" && check_known_manifest(*parsed_service).empty());
    CHECK(parsed_service && format_manifest(*parsed_service) == service);
    // The service component of a build without neurad: no tool and no file.
    const auto empty_service = parse_manifest(
        "neura-component 1\nname service\nversion 0.3.0\nbuild abc\ntarget x86_64-linux-gnu\nrequires core\nprotocol service 1.0\n", error);
    CHECK(empty_service && empty_service->tools.empty() && empty_service->files.empty() && check_known_manifest(*empty_service).empty());
    const auto core = parse_manifest("neura-component 1\nname core\nversion 0.3.0\nbuild abc\ntarget t\nfile 644 " + digest_x +
                                         " 0 share/doc/neura/LICENSE\n", error);
    CHECK(core && check_known_manifest(*core).empty());
    // Application target components, which later milestones define, parse;
    // whether they are known is a separate question.
    const auto other = parse_manifest("neura-component 1\nname web-target\nversion 0.3.0\nbuild abc\ntarget t\nrequires core\n", error);
    CHECK(other && !check_known_manifest(*other).empty());
    // The rules of the known components.
    const auto core_requiring = parse_manifest("neura-component 1\nname core\nversion 0.3.0\nbuild abc\ntarget t\nrequires sdk\n", error);
    CHECK(core_requiring && !check_known_manifest(*core_requiring).empty());
    const auto sdk_alone = parse_manifest("neura-component 1\nname sdk\nversion 0.3.0\nbuild abc\ntarget t\n", error);
    CHECK(sdk_alone && !check_known_manifest(*sdk_alone).empty());
    const auto service_no_protocol = parse_manifest("neura-component 1\nname service\nversion 0.3.0\nbuild abc\ntarget t\nrequires core\n", error);
    CHECK(service_no_protocol && !check_known_manifest(*service_no_protocol).empty());
    const auto sdk_protocol =
        parse_manifest("neura-component 1\nname sdk\nversion 0.3.0\nbuild abc\ntarget t\nrequires core\nprotocol service 1.0\n", error);
    CHECK(sdk_protocol && !check_known_manifest(*sdk_protocol).empty());

    const std::string head = "neura-component 1\nname sdk\nversion 0.3.0\nbuild abc\ntarget x86_64-linux-gnu\nrequires core\n";
    const std::string file_a = "file 644 " + digest_x + " 1 a\n";
    const std::string file_b = "file 644 " + digest_x + " 1 b\n";
    // Edge case 23 and the strict format.
    expect_rejected("manifest", parse_manifest, {
        {"no final newline", text.substr(0, text.size() - 1)},
        {"unknown line", head + "note hello\n"},
        {"unknown line after files", head + file_a + "tool x\n"},
        {"repeated file", head + file_a + file_a},
        {"files unsorted", head + file_b + file_a},
        {"path with ..", head + "file 644 " + digest_x + " 1 share/../etc/passwd\n"},
        {"absolute path", head + "file 644 " + digest_x + " 1 /etc/passwd\n"},
        {"path with a space", head + "file 644 " + digest_x + " 1 a b\n"},
        {"path with a backslash", head + "file 644 " + digest_x + " 1 a\\\\b\n"},
        {"path ending in a slash", head + "file 644 " + digest_x + " 1 share/\n"},
        {"path with an empty component", head + "file 644 " + digest_x + " 1 share//x\n"},
        {"path with a dot component", head + "file 644 " + digest_x + " 1 ./x\n"},
        {"manifest listed", head + "file 644 " + digest_x + " 1 share/neura/components/sdk.txt\n"},
        {"mode 600", head + "file 600 " + digest_x + " 1 a\n"},
        {"mode 0755", head + "file 0755 " + digest_x + " 1 a\n"},
        {"uppercase digest", head + "file 644 " + std::string(64, 'A') + " 1 a\n"},
        {"short digest", head + "file 644 " + std::string(63, 'a') + " 1 a\n"},
        {"size with a sign", head + "file 644 " + digest_x + " -1 a\n"},
        {"size with a leading zero", head + "file 644 " + digest_x + " 01 a\n"},
        {"tool without its file", head + "tool neurac\n"},
        {"tool with a 644 file", head + "tool neurac\nfile 644 " + digest_x + " 1 bin/neurac\n"},
        {"tools unsorted", head + "tool neurac\ntool neura-test\nfile 755 " + digest_x + " 1 bin/neura-test\nfile 755 " + digest_x + " 1 bin/neurac\n"},
        {"repeated tool", head + "tool neurac\ntool neurac\nfile 755 " + digest_x + " 1 bin/neurac\n"},
        {"tool with a slash", head + "tool ../x\n"},
        {"repeated requires", "neura-component 1\nname sdk\nversion 0.3.0\nbuild abc\ntarget t\nrequires core\nrequires core\n"},
        {"requires after tool", head + "tool neurac\nrequires core\nfile 755 " + digest_x + " 1 bin/neurac\n"},
        {"protocol after tool", head + "tool neurac\nprotocol service 1.0\nfile 755 " + digest_x + " 1 bin/neurac\n"},
        {"protocol with one field", head + "protocol service\n"},
        {"wrong magic", "neura-component 2\nname sdk\nversion 0.3.0\nbuild abc\ntarget t\n"},
        {"missing target", "neura-component 1\nname sdk\nversion 0.3.0\nbuild abc\n"},
        {"lines out of order", "neura-component 1\nversion 0.3.0\nname sdk\nbuild abc\ntarget t\n"},
        {"bad version", "neura-component 1\nname sdk\nversion 0.3\nbuild abc\ntarget t\n"},
        {"version with build metadata", "neura-component 1\nname sdk\nversion 0.3.0+x\nbuild abc\ntarget t\n"},
        {"uppercase build", "neura-component 1\nname sdk\nversion 0.3.0\nbuild ABC\ntarget t\n"},
        {"uppercase name", "neura-component 1\nname SDK\nversion 0.3.0\nbuild abc\ntarget t\n"},
        {"double space", "neura-component 1\nname  sdk\nversion 0.3.0\nbuild abc\ntarget t\n"},
        {"tab", "neura-component 1\nname\tsdk\nversion 0.3.0\nbuild abc\ntarget t\n"},
        {"carriage return", "neura-component 1\r\nname sdk\r\nversion 0.3.0\r\nbuild abc\r\ntarget t\r\n"},
        {"blank line", head + "\n"},
    });
    CHECK(is_safe_relative_path("share/neura/stdlib/std/core/version.nra") && is_safe_relative_path("lib/neura/libneura-core-0.3.0-rc.1+x.so"));
    CHECK(!is_safe_relative_path("") && !is_safe_relative_path("a//b") && !is_safe_relative_path("a/./b") && !is_safe_relative_path("a/ b") &&
          !is_safe_relative_path("a/\xc3\xa9"));
    return failures;
}

int journal_suite() {
    std::string error;
    Journal journal;
    journal.operation = "update";
    journal.old_generation = "0.2.1-rc.3";
    journal.new_generation = "0.3.0+0123456789ab";
    journal.placed = true;
    journal.aside = "/home/u/.local/share/neura/.damaged-0.3.0+0123456789ab-17";
    journal.remove = {"/home/u/my bin/neura-old"};
    journal.add = {"/home/u/my bin/neurac", "/home/u/my bin/neura-test"};
    journal.old_marker = "neura-install 1\nmethod archive\nlink /home/u/my bin/neura\nversion 0.2.1-rc.3\n";
    journal.new_marker = "neura-install 2\nmethod archive\nbin /home/u/my bin\ncomponents core sdk\n";
    const auto text = format_journal(journal);
    const auto parsed = parse_journal(text, error);
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->operation == journal.operation && parsed->old_generation == journal.old_generation &&
              parsed->new_generation == journal.new_generation && parsed->placed && parsed->aside == journal.aside &&
              parsed->remove == journal.remove &&
              parsed->add == journal.add && parsed->old_marker == journal.old_marker && parsed->new_marker == journal.new_marker);
        CHECK(format_journal(*parsed) == text);
    }
    // A first install has no old generation and no old marker.
    Journal first;
    first.operation = "install";
    first.new_generation = "0.3.0+0123456789ab";
    first.new_marker = journal.new_marker;
    const auto reread = parse_journal(format_journal(first), error);
    CHECK(reread && reread->old_generation.empty() && reread->old_marker.empty() && !reread->placed);
    expect_rejected("journal", parse_journal, {
        {"empty", ""},
        {"wrong magic", "neura-journal 2\noperation update\nold -\nnew 0.3.0\nplaced no\nnew-marker x\n"},
        {"no new marker", "neura-journal 1\noperation update\nold -\nnew 0.3.0\nplaced no\n"},
        {"relative link", "neura-journal 1\noperation update\nold -\nnew 0.3.0\nplaced no\nadd bin/neurac\nnew-marker x\n"},
        {"bad generation", "neura-journal 1\noperation update\nold -\nnew ../x\nplaced no\nnew-marker x\n"},
        {"bad placed", "neura-journal 1\noperation update\nold -\nnew 0.3.0\nplaced maybe\nnew-marker x\n"},
        {"unknown line", "neura-journal 1\noperation update\nold -\nnew 0.3.0\nplaced no\nnew-marker x\nother y\n"},
        {"truncated", "neura-journal 1\noperation update\nold -\nnew 0.3.0\nplaced no\nnew-marker x"},
    });
    return failures;
}

int generation_suite() {
    const auto core = std::string("neura-component 1\nname core\n");
    const auto sdk = std::string("neura-component 1\nname sdk\n");
    const auto service = std::string("neura-component 1\nname service\n");
    const auto name = generation_name("0.3.0", {{"sdk", sdk}, {"core", core}});
    // The digest is of the manifests in the order core, sdk, service,
    // whatever order they are given in.
    CHECK(name == "0.3.0+" + neura::util::sha256_hex(core + sdk).substr(0, 12));
    CHECK(name == generation_name("0.3.0", {{"core", core}, {"sdk", sdk}}));
    CHECK(generation_name("0.3.0", {{"service", service}, {"core", core}, {"sdk", sdk}}) ==
          "0.3.0+" + neura::util::sha256_hex(core + sdk + service).substr(0, 12));
    // Another component set or another build: another name.
    CHECK(generation_name("0.3.0", {{"core", core}, {"service", service}}) != name);
    CHECK(generation_name("0.3.0", {{"core", core + "x"}, {"sdk", sdk}}) != name);
    const auto parsed = parse_generation_name(name);
    CHECK(parsed && parsed->version == "0.3.0" && parsed->id.size() == 12);
    const auto legacy = parse_generation_name("0.2.1-rc.3");
    CHECK(legacy && legacy->version == "0.2.1-rc.3" && legacy->id.empty());
    for (const char* bad : {"", "0.3", "0.3.0+", "0.3.0+0123456789a", "0.3.0+0123456789abc", "0.3.0+0123456789AB", "0.3.0+012345678 ab",
                            "../0.3.0", "0.3.0+0123456789ab+0123456789ab", "x+0123456789ab"}) {
        CHECK(!parse_generation_name(bad));
    }
    return failures;
}

int components_metadata_cases() {
    std::string error;
    const std::string head = "neura-components 1\nversion 0.3.0\nbuild 3ffc2120\n";
    const std::string runtime = "archive runtime x86_64-linux-gnu neura-runtime-0.3.0-x86_64-linux-gnu.tar.gz " + digest_x + " 10\n";
    const std::string sdk = "archive sdk x86_64-linux-gnu neura-sdk-0.3.0-x86_64-linux-gnu.tar.gz " + digest_y + " 20\n";
    const std::string sdk_mac = "archive sdk aarch64-apple-darwin neura-sdk-0.3.0-aarch64-apple-darwin.tar.gz " + digest_y + " 20\n";
    const auto parsed = parse_components_metadata(head + runtime + sdk_mac + sdk, error);
    CHECK(parsed && parsed->version == "0.3.0" && parsed->build == "3ffc2120" && parsed->archives.size() == 3);
    CHECK(parsed && parsed->find("sdk", "x86_64-linux-gnu") && parsed->find("sdk", "x86_64-linux-gnu")->size == 20);
    CHECK(parsed && !parsed->find("runtime", "aarch64-apple-darwin"));
    CHECK(product_archive_name("runtime", "0.3.0-rc.1", "x86_64-linux-gnu") == "neura-runtime-0.3.0-rc.1-x86_64-linux-gnu.tar.gz");
    // Edge case 24.
    expect_rejected("component metadata", parse_components_metadata, {
        {"unknown product", head + "archive service x86_64-linux-gnu neura-service-0.3.0-x86_64-linux-gnu.tar.gz " + digest_x + " 10\n"},
        {"two archives for one product and target", head + sdk + sdk},
        {"size 0", head + "archive sdk x86_64-linux-gnu neura-sdk-0.3.0-x86_64-linux-gnu.tar.gz " + digest_x + " 0\n"},
        {"unsorted", head + sdk + runtime},
        {"file of another version", head + "archive sdk x86_64-linux-gnu neura-sdk-0.2.0-x86_64-linux-gnu.tar.gz " + digest_x + " 1\n"},
        {"legacy file name", head + "archive sdk x86_64-linux-gnu neura-0.3.0-x86_64-linux-gnu.tar.gz " + digest_x + " 1\n"},
        {"no build", "neura-components 1\nversion 0.3.0\n" + sdk},
        {"wrong magic", "neura-components 2\nversion 0.3.0\nbuild a\n"},
        {"release metadata", "neura-release 1\nversion 0.3.0\n"},
        {"unknown line", head + "asset x86_64-linux-gnu f " + digest_x + " 5\n"},
        {"missing field", head + "archive sdk x86_64-linux-gnu neura-sdk-0.3.0-x86_64-linux-gnu.tar.gz " + digest_x + "\n"},
        {"no final newline", head + sdk.substr(0, sdk.size() - 1)},
    });
    CHECK(parse_components_metadata(head, error).has_value());
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: neura_lifecycle_tests <sha256|semver|release-metadata|marker|manifest|journal|generation>\n";
        return 2;
    }
    const std::string suite = argv[1];
    int result;
    if (suite == "sha256") result = sha256_suite();
    else if (suite == "semver") result = semver_suite();
    else if (suite == "release-metadata") result = release_metadata_suite();
    else if (suite == "marker") result = marker_suite();
    else if (suite == "manifest") result = manifest_suite();
    else if (suite == "journal") result = journal_suite();
    else if (suite == "generation") result = generation_suite();
    else {
        std::cerr << "unknown suite " << suite << '\n';
        return 2;
    }
    if (result != 0) {
        std::cout << result << " check(s) failed in " << suite << '\n';
        return 1;
    }
    std::cout << suite << " passed\n";
    return 0;
}

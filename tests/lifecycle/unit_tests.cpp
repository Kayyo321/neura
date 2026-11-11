// Unit tests for the pure parts of the install lifecycle: SHA-256, version
// precedence, the release metadata and install marker parsers, and the
// update-check state. The commands that use them are tested through the
// installed tool by the lifecycle-* CMake scripts.
//
// Usage: neura_lifecycle_tests <sha256|semver|release-metadata|marker>

#include "cli/install_layout.hpp"
#include "util/sha256.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <unistd.h>

namespace {

namespace fs = std::filesystem;
using namespace neura::lifecycle;

int failures = 0;

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
    return failures;
}

int marker_suite() {
    std::string error;
    Marker marker{"archive", "/home/u/.local/bin/neura", {"0.1.0", "0.2.0-rc.1"}};
    const auto text = format_marker(marker);
    CHECK(text == "neura-install 1\nmethod archive\nlink /home/u/.local/bin/neura\nversion 0.1.0\nversion 0.2.0-rc.1\n");
    const auto parsed = parse_marker(text, error);
    CHECK(parsed && parsed->method == "archive" && parsed->link == marker.link && parsed->versions == marker.versions);
    CHECK(parse_marker("neura-install 1\nmethod homebrew\nlink /opt/bin/neura\nversion 1.0.0\n", error)->method == "homebrew");
    const auto spaced = parse_marker("neura-install 1\nmethod archive\nlink /home/a b/bin/neura\nversion 1.0.0\n", error);
    CHECK(spaced && spaced->link == "/home/a b/bin/neura");

    const char* rejected[] = {
        "",
        "neura-install 1\n",
        "neura-install 1\nmethod archive\nlink /b/neura\n",
        "neura-install 2\nmethod archive\nlink /b/neura\nversion 1.0.0\n",
        "neura-install 1\nversion 1.0.0\nmethod archive\nlink /b/neura\n",
        "neura-install 1\nmethod archive\nlink b/neura\nversion 1.0.0\n",
        "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0",
        "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0\nextra thing\n",
        "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0\nversion 1.0.0\n",
        "neura-install 1\nmethod archive\nlink /b/neura\nversion ../../etc\n",
        "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0\n",
        "neura-install 1\nmethod Archive\nlink /b/neura\nversion 1.0.0\n",
        "neura-install 1\nmethod arch ive\nlink /b/neura\nversion 1.0.0\n",
        "neura-install 1\nmethod \nlink /b/neura\nversion 1.0.0\n",
        "neura-install 1\r\nmethod archive\r\nlink /b/neura\r\nversion 1.0.0\r\n",
        "neura-install 1\nmethod archive\nlink /b/neura\nversion 1.0.0 \n",
        "neura-install 1\nmethod archive\nlink /b\tx\nversion 1.0.0\n",
    };
    for (const char* text_case : rejected) {
        error.clear();
        if (parse_marker(text_case, error)) {
            ++failures;
            std::cout << "FAIL: marker accepted: " << text_case << '\n';
        } else if (error.empty()) {
            ++failures;
            std::cout << "FAIL: no error message for marker: " << text_case << '\n';
        }
    }

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
        std::ofstream(root / "neura-install.txt") << text;
    }
    const auto detected = detect_install(executable);
    CHECK(detected.archive() && detected.root == root && detected.running_version == "0.1.0");
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
    fs::remove_all(root);
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: neura_lifecycle_tests <sha256|semver|release-metadata|marker>\n";
        return 2;
    }
    const std::string suite = argv[1];
    int result;
    if (suite == "sha256") result = sha256_suite();
    else if (suite == "semver") result = semver_suite();
    else if (suite == "release-metadata") result = release_metadata_suite();
    else if (suite == "marker") result = marker_suite();
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

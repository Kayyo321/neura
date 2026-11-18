#pragma once

#include "lock.hpp"

#include <filesystem>

namespace neura::project {

struct BundleImage {
    std::string path;
    Sha256Digest digest;
};

struct BundleFile {
    Sha256Digest sha256;
    std::uint64_t bytes;
    std::string media_type;
    bool initial;
};

using BundleLimit = std::variant<std::uint32_t, std::array<std::uint32_t, 2>>;

struct BundleManifest {
    TargetKind kind;
    std::string id;
    std::string name;
    std::string version;
    std::string language;
    BundleImage image;
    Named<BundleFile> files{};
    Named<std::uint32_t> requirements{};
    std::vector<Capability> capabilities{};
    Named<std::vector<std::string>> origins{};
    Named<BundleLimit> limits{};
    Named<std::uint32_t> contracts{};
    std::optional<Sha256Digest> ui{};
    std::optional<std::string> csp{};
    std::optional<bool> dev{};
    // Web bundles: the routing mode ("hash" or "history") and the URL path
    // the page's references start at under history routing.
    std::optional<std::string> routing{};
    std::optional<std::string> base{};
};

// Canonical schema-1 JSON uses UTF-8, sorted keys and file paths, integer
// numbers, and compact separators. The result is the exact input to SHA-256.
std::string write_bundle_manifest(const BundleManifest& manifest, std::string_view file = "bundle.json");
BundleManifest parse_bundle_manifest(std::string_view bytes, std::string_view file = "bundle.json");

struct BundleVerification {
    // The caller supplies only the versions and providers installed for the
    // target. Verify before binding services or reading executable bytes.
    Named<std::uint32_t> supported_requirements{};
    std::optional<Sha256Digest> manifest_digest{};
    std::optional<TargetKind> expected_kind{};
    std::optional<std::string> expected_id{};
    // Only `neura dev` runs a bundle marked `dev`.
    bool allow_dev = false;
};

class VerifiedBundle {
public:
    const BundleManifest& manifest() const { return manifest_; }
    const Sha256Digest& digest() const { return digest_; }

    // Returns these exact bytes after checking their recorded size and
    // digest again. Only declared files can be read.
    std::vector<std::uint8_t> read_file(std::string_view relative) const;

private:
    friend VerifiedBundle verify_bundle_directory(const std::filesystem::path&, const BundleVerification&);
    VerifiedBundle(std::filesystem::path directory, BundleManifest manifest, Sha256Digest digest);

    std::filesystem::path directory_;
    BundleManifest manifest_;
    Sha256Digest digest_;
};

// Keep the directory and its ancestors protected from concurrent writers
// and renames during verification and use. Intake is not a filesystem
// snapshot. Resolve an installed or extracted bundle before starting it.
VerifiedBundle verify_bundle_directory(const std::filesystem::path& directory,
                                       const BundleVerification& verification);

} // namespace neura::project

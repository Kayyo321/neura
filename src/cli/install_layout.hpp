#pragma once

// The layout of an archive install and the text formats that describe it
// (docs/Install.md). Everything here is pure data handling: parsing,
// formatting, version comparison, and finding the install root of an
// executable. Running child processes, downloading, and changing the file
// system belong to lifecycle.cpp.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura::lifecycle {

// A semantic version (semver.org 2.0.0) without build metadata.
struct SemVer {
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    std::vector<std::string> prerelease;
};

// Parses MAJOR.MINOR.PATCH[-prerelease]. Numeric fields have no leading
// zeros, prerelease identifiers are non-empty and made of [0-9A-Za-z-], and
// numeric prerelease identifiers have no leading zeros. Build metadata
// ("+...") is not accepted.
std::optional<SemVer> parse_semver(std::string_view text);

// Precedence as semver.org section 11 defines it: -1, 0, or 1.
int compare_semver(const SemVer& left, const SemVer& right);

// The marker file neura-install.txt at the root of an archive install.
struct Marker {
    // "archive" when `neura update` manages the install; the name of the
    // package manager that owns it otherwise.
    std::string method;
    // The absolute path of the symlink in the user's bin directory.
    std::string link;
    // One entry for each directory in versions/.
    std::vector<std::string> versions;
};

// Parses the marker strictly: the exact lines and order that format_marker
// writes, and nothing else. On failure returns nothing and sets `error`.
std::optional<Marker> parse_marker(std::string_view text, std::string& error);
std::string format_marker(const Marker& marker);

// Release metadata, the neura-release.txt file of a release.
struct ReleaseAsset {
    std::string triple;
    std::string file;
    std::string sha256;
    std::uint64_t size = 0;
};

struct ReleaseMetadata {
    std::string version;
    std::vector<ReleaseAsset> assets;

    const ReleaseAsset* find(std::string_view triple) const;
};

constexpr std::uint64_t max_asset_size = std::uint64_t{1} << 30;

std::optional<ReleaseMetadata> parse_release_metadata(std::string_view text, std::string& error);

// The state file update-check.txt that update notices keep at the root.
struct UpdateCheck {
    std::int64_t checked = 0;
    std::string latest;
};

std::optional<UpdateCheck> parse_update_check(std::string_view text, std::string& error);
std::string format_update_check(const UpdateCheck& state);

constexpr std::int64_t update_check_interval_seconds = std::int64_t{7} * 24 * 60 * 60;

// Whether the network check that refreshes the state is due.
bool update_check_due(const std::optional<UpdateCheck>& state, std::int64_t now);

// Whether `latest` is a newer version than `current`; false when either
// does not parse.
bool is_newer(std::string_view latest, std::string_view current);

// What the running executable is part of.
struct Detection {
    enum class Kind {
        // Not below <root>/versions/<version>/bin with a marker: a source
        // build or a copy.
        Unmanaged,
        // Below an install root whose marker parsed.
        Installed,
        // Below an install root whose marker exists but is not valid.
        Broken,
    };
    Kind kind = Kind::Unmanaged;
    std::filesystem::path root;
    // The directory name under versions/ that holds the executable.
    std::string running_version;
    Marker marker;
    // For Broken, why the marker was rejected.
    std::string problem;

    bool archive() const { return kind == Kind::Installed && marker.method == "archive"; }
};

// `executable` is the real path of the running executable.
Detection detect_install(const std::filesystem::path& executable);

// Names under the root that this layout creates and owns.
inline constexpr std::string_view marker_name = "neura-install.txt";
inline constexpr std::string_view update_check_name = "update-check.txt";
inline constexpr std::string_view lock_name = ".update-lock";

} // namespace neura::lifecycle

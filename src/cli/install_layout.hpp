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
#include <utility>
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
// Schema 2 is written (docs/Install.md); schema 1, which 0.2 wrote and a
// 0.2 updater still writes, is read.
struct Marker {
    // 1 or 2: the schema the text had.
    int schema = 2;
    // "archive" when `neura update` manages the install; the name of the
    // package manager that owns it otherwise.
    std::string method;
    // The absolute directory that holds the public links. Schema 1 names
    // the one link instead; this is then its directory.
    std::string bin;
    // Schema 1 only: the absolute path of the one link it names.
    std::string link;
    // Schema 2: the components an update preserves, in the order core,
    // sdk, service.
    std::vector<std::string> components;
    // One entry for each directory in versions/: a generation name.
    std::vector<std::string> generations;
    // The names of the links in `bin` that the installation owns, sorted.
    // Schema 1 reads as the one link the marker names.
    std::vector<std::string> links;
};

// Parses either schema strictly: the exact lines and order that each
// writes, and nothing else. On failure returns nothing and sets `error`.
std::optional<Marker> parse_marker(std::string_view text, std::string& error);
// Writes schema 2. Components are put in their canonical order and
// generations and links are sorted.
std::string format_marker(const Marker& marker);

// The order of the known components: core, sdk, service. Every other name
// sorts after them, alphabetically.
bool component_before(std::string_view left, std::string_view right);
bool known_component(std::string_view name);
// Whether `components` (in any order) is a set an installation may have:
// core sdk, core service, or core sdk service.
bool valid_component_set(const std::vector<std::string>& components);

// A component manifest, share/neura/components/<name>.txt.
struct ManifestFile {
    // 0755 or 0644.
    unsigned mode = 0644;
    std::string sha256;
    std::uint64_t size = 0;
    // Relative to the generation, such as bin/neurac.
    std::string path;
};

struct ComponentManifest {
    std::string name;
    std::string version;
    std::string build;
    std::string target;
    std::vector<std::string> requirements;
    // The protocol line's value after "protocol ", such as "service 1.0";
    // empty when there is none.
    std::string protocol;
    std::vector<std::string> tools;
    std::vector<ManifestFile> files;
};

inline constexpr std::string_view components_directory = "share/neura/components";

// Parses a manifest strictly (Lifecycle rules: order, single spaces, a
// final newline, sorted and unique tools and files, safe paths, every tool
// a 755 file under bin/). Any well-formed name is accepted; whether the
// name is known is the caller's question (check_known_manifest).
std::optional<ComponentManifest> parse_manifest(std::string_view text, std::string& error);
std::string format_manifest(const ComponentManifest& manifest);
// For core, sdk and service: the requires and protocol lines their rules
// fix. Returns an empty string when the manifest keeps them.
std::string check_known_manifest(const ComponentManifest& manifest);
// A path an archive or manifest may hold: letters, digits, '.', '_', '+',
// '-' and '/', relative, with no empty, "." or ".." component.
bool is_safe_relative_path(std::string_view path);

// The component release metadata, neura-components.txt.
struct ComponentArchive {
    // "sdk" or "runtime".
    std::string product;
    std::string triple;
    std::string file;
    std::string sha256;
    std::uint64_t size = 0;
};

struct ComponentsMetadata {
    std::string version;
    std::string build;
    std::vector<ComponentArchive> archives;

    const ComponentArchive* find(std::string_view product, std::string_view triple) const;
};

std::optional<ComponentsMetadata> parse_components_metadata(std::string_view text, std::string& error);
// The file name of a product's archive: neura-<product>-<version>-<triple>.tar.gz.
std::string product_archive_name(std::string_view product, std::string_view version, std::string_view triple);

// The journal of a change, <root>/.journal. Private to one build.
struct Journal {
    // install, update, rollback, components, migrate, repair.
    std::string operation;
    // The generation `current` named before the change; empty when there
    // was none.
    std::string old_generation;
    std::string new_generation;
    // Whether this change put the new generation's directory in place, so
    // that undoing the change removes it.
    bool placed = false;
    // Absolute paths of links this change removes and adds.
    std::vector<std::string> remove;
    std::vector<std::string> add;
    // The text of the marker before and after the change; the first is
    // empty when there was none.
    std::string old_marker;
    std::string new_marker;
};

std::optional<Journal> parse_journal(std::string_view text, std::string& error);
std::string format_journal(const Journal& journal);

// Generation names: <version>, as a 0.2 updater places one, or
// <version>+<id> with the first 12 hexadecimal digits of the SHA-256 of the
// manifests' bytes, concatenated in component order.
struct GenerationName {
    std::string version;
    // Empty for a name that a 0.2 updater placed.
    std::string id;
};
std::optional<GenerationName> parse_generation_name(std::string_view name);
// `manifests` holds (component name, manifest bytes) in any order.
std::string generation_name(std::string_view version, std::vector<std::pair<std::string, std::string>> manifests);

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
        // Not below <root>/versions/<generation>/bin with a marker: a
        // source build or a copy.
        Unmanaged,
        // Below an install root whose marker parsed.
        Installed,
        // Below an install root whose marker exists but is not valid.
        Broken,
    };
    Kind kind = Kind::Unmanaged;
    std::filesystem::path root;
    // The directory name under versions/ that holds the executable.
    std::string running_generation;
    Marker marker;
    // For Broken, why the marker was rejected.
    std::string problem;

    bool archive() const { return kind == Kind::Installed && marker.method == "archive"; }
};

// `executable` is the real path of the running executable. Both forms of
// generation name and both marker schemas are accepted.
Detection detect_install(const std::filesystem::path& executable);

// Names under the root that this layout creates and owns.
inline constexpr std::string_view marker_name = "neura-install.txt";
inline constexpr std::string_view update_check_name = "update-check.txt";
// The lock of a 0.2 update, which schema 1 roots still honour.
inline constexpr std::string_view lock_name = ".update-lock";
// The installation lock (flock) and the journal of a change.
inline constexpr std::string_view install_lock_name = ".lock";
inline constexpr std::string_view journal_name = ".journal";
inline constexpr std::string_view leases_name = "leases";

} // namespace neura::lifecycle

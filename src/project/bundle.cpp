#include "bundle.hpp"

#include "files.hpp"
#include "toml.hpp"
#include "tooling/json.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <limits>
#include <set>

namespace neura::project {
namespace {

namespace fs = std::filesystem;
using tooling::Json;

constexpr std::string_view manifest_name = "bundle.json";
constexpr std::uint64_t default_file_limit = 64 * 1024 * 1024;

[[noreturn]] void refuse(const Span& site, std::string detail) {
    throw Diagnostic(site, std::move(detail)).with_code("E1029");
}

[[noreturn]] void refuse_limit(const Span& site, std::string detail) {
    throw Diagnostic(site, std::move(detail)).with_code("E1028");
}

[[noreturn]] void refuse_version(const Span& site, std::string detail) {
    throw Diagnostic(site, std::move(detail)).with_code("E1021");
}

[[noreturn]] void refuse_requirement(const Span& site, std::string detail) {
    throw Diagnostic(site, std::move(detail)).with_code("E1031");
}

bool digit(char c) { return c >= '0' && c <= '9'; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool letter(char c) { return lower(c) >= 'a' && lower(c) <= 'z'; }

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), lower);
    return result;
}

using detail::path_from_text;
using detail::path_text;

std::string_view kind_name(TargetKind kind) {
    switch (kind) {
    case TargetKind::Web: return "web";
    case TargetKind::Server: return "server";
    case TargetKind::Desktop: return "desktop";
    }
    return {};
}

TargetKind read_kind(std::string_view value, const Span& site) {
    for (const auto kind : {TargetKind::Web, TargetKind::Server, TargetKind::Desktop}) {
        if (value == kind_name(kind)) return kind;
    }
    refuse(site, "unknown bundle kind '" + std::string(value) + "'");
}

bool identifier(std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](char c) {
        return letter(c) || digit(c) || c == '-' || c == '_' || c == '.';
    });
}

bool device_name(std::string_view component) {
    const auto stem = lowercase(component.substr(0, component.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
           stem.back() >= '1' && stem.back() <= '9';
}

bool safe_path(std::string_view value) {
    if (value.empty() || !valid_utf8(value)) return false;
    for (;;) {
        const auto slash = value.find('/');
        const auto component = value.substr(0, slash);
        if (component.empty() || component.ends_with('.') || component.ends_with(' ') || device_name(component) ||
            component.find_first_of("\\:*?\"<>|") != std::string_view::npos ||
            std::ranges::any_of(component, [](unsigned char c) { return c < 32 || c == 127; })) return false;
        if (slash == std::string_view::npos) return true;
        value.remove_prefix(slash + 1);
    }
}

std::string extension(std::string_view path) {
    const auto slash = path.rfind('/');
    if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
    const auto dot = path.rfind('.');
    return dot == std::string_view::npos ? std::string{} : lowercase(path.substr(dot));
}

bool valid_media(std::string_view mime, std::string_view suffix) {
    const auto slash = mime.find('/');
    const auto token = [](std::string_view value) {
        return !value.empty() && std::ranges::all_of(value, [](char c) {
            return (c >= 'a' && c <= 'z') || digit(c) || std::string_view("!#$&^_.+-").find(c) != std::string_view::npos;
        });
    };
    if (slash == std::string_view::npos || !token(mime.substr(0, slash)) || !token(mime.substr(slash + 1))) return false;
    if (suffix == ".html") return mime == "text/html";
    if (suffix == ".css") return mime == "text/css";
    if (suffix == ".wasm") return mime == "application/wasm";
    if (suffix == ".js" || suffix == ".mjs") return mime == "text/javascript" || mime == "application/javascript";
    if (suffix == ".json") return mime == "application/json";
    if (suffix == ".nvr") return mime == "application/octet-stream";
    return true;
}

std::uint64_t file_limit(const BundleManifest& bundle) {
    const auto limit = bundle.limits.find("W-1");
    return limit == bundle.limits.end() ? default_file_limit : std::get<std::uint32_t>(limit->second);
}

void validate_limits(const BundleManifest& bundle, const Span& site) {
    const auto in_range = [](std::uint32_t value, std::uint32_t maximum) { return value > 0 && value <= maximum; };
    for (const auto& [name, value] : bundle.limits) {
        const auto bounds = target_limit_bounds(bundle.kind, name);
        const auto* scalar = std::get_if<std::uint32_t>(&value);
        const auto* pair = std::get_if<std::array<std::uint32_t, 2>>(&value);
        if (!bounds || (bounds->second == 0 && (!scalar || !in_range(*scalar, bounds->first))) ||
            (bounds->second != 0 && (!pair || !in_range((*pair)[0], bounds->first) || !in_range((*pair)[1], bounds->second)))) {
            refuse_limit(site, "invalid or out-of-range bundle limit '" + name + "'");
        }
    }
}

// The manifest's file table implies the set of directories. Record each
// component to catch paths which would alias on a case-insensitive host.
std::set<std::string, std::less<>> validate_files(const BundleManifest& bundle, const Span& site) {
    const auto limit = file_limit(bundle);
    std::set<std::string, std::less<>> directories;
    Named<std::string_view> folded{{std::string(manifest_name), manifest_name}};
    const auto unique = [&](std::string_view name) {
        const auto [at, inserted] = folded.emplace(lowercase(name), name);
        if (!inserted && at->second != name) refuse(site, "bundle paths differ only in case: '" + std::string(name) + "'");
    };
    for (const auto& [path, file] : bundle.files) {
        if (!safe_path(path) || path == manifest_name) refuse(site, "invalid bundle file path '" + path + "'");
        if (file.bytes > limit) refuse(site, "bundle file '" + path + "' exceeds W-1 (" + std::to_string(limit) + " bytes)");
        const auto suffix = extension(path);
        if (suffix == ".nra") refuse(site, "source file '" + path + "' cannot appear in a bundle");
        if (!valid_media(file.media_type, suffix)) refuse(site, "wrong media type for bundle file '" + path + "'");
        unique(path);
        for (auto slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            const auto parent = std::string_view(path).substr(0, slash);
            if (bundle.files.contains(parent) || parent == manifest_name)
                refuse(site, "bundle path is both a file and a directory: '" + std::string(parent) + "'");
            unique(parent);
            directories.emplace(parent);
        }
    }
    const auto image = bundle.files.find(bundle.image.path);
    // A desktop bundle's image is its native child executable (Desktop030.md).
    const bool image_kind = bundle.kind == TargetKind::Desktop ? !bundle.image.path.empty() : extension(bundle.image.path) == ".nvr";
    if (image == bundle.files.end() || image->second.sha256 != bundle.image.digest || !image_kind) {
        refuse(site, "bundle image path or digest disagrees with the file table: '" + bundle.image.path + "'");
    }
    if (bundle.kind == TargetKind::Web && !bundle.files.contains("index.html")) refuse(site, "missing index.html in web bundle");
    return directories;
}

void validate_metadata(const BundleManifest& bundle, const Span& site) {
    if (kind_name(bundle.kind).empty()) refuse(site, "unknown bundle kind");
    if (!valid_project_id(bundle.id) || !valid_project_name(bundle.name) || !valid_project_version(bundle.version))
        refuse(site, "invalid bundle project id, name or version");
    if (bundle.language != "0.3") refuse_version(site, "bundle language '" + bundle.language + "', supported '0.3'");
    if ((bundle.kind == TargetKind::Desktop) != bundle.ui.has_value()) refuse(site, "desktop bundles require the embedded web bundle digest in 'ui'");
    if (bundle.kind == TargetKind::Web) {
        if (!bundle.csp || bundle.csp->find_first_not_of(' ') == std::string::npos ||
            !std::ranges::all_of(*bundle.csp, [](unsigned char c) { return c >= 32 && c <= 126; }))
            refuse(site, "missing or malformed web bundle content-security policy");
        if (bundle.routing && *bundle.routing != "hash" && *bundle.routing != "history")
            refuse(site, "bundle routing must be \"hash\" or \"history\"");
        if (bundle.base && (!bundle.base->starts_with('/') || !bundle.base->ends_with('/') ||
                            bundle.base->find_first_of("\"<>\\?#") != std::string::npos))
            refuse(site, "bundle base must be a URL path that starts and ends with '/'");
    } else if (bundle.csp || bundle.routing || bundle.base) {
        refuse(site, "bundle content-security policy, routing and base belong to the web bundle");
    }
    for (const auto& [axis, version] : bundle.requirements) {
        if (!identifier(axis) || version == 0) refuse(site, "invalid bundle requirement '" + axis + "'");
    }
    std::set<Capability> capabilities;
    for (const auto capability : bundle.capabilities) {
        const auto name = capability_name(capability);
        if (!target_capability(bundle.kind, name) || !capabilities.insert(capability).second)
            refuse(site, "invalid or repeated bundle capability '" + std::string(name) + "'");
    }
    for (const auto& [use, values] : bundle.origins) {
        if (use != "connect" && use != "load" && use != "style" && use != "external")
            refuse(site, "unknown bundle origin use '" + use + "'");
        std::set<std::string_view> seen;
        for (const auto& value : values) {
            if (!parse_origin(value) || !seen.insert(value).second) refuse(site, "invalid or repeated bundle origin '" + value + "'");
        }
    }
    for (const auto& [id, version] : bundle.contracts) {
        if (!identifier(id) || version == 0) refuse(site, "invalid bundle contract '" + id + "'");
    }
    validate_limits(bundle, site);
}

void keys(const Json& object, std::initializer_list<std::string_view> allowed, std::string_view what, const Span& site) {
    if (!object.is_object()) refuse(site, "expected a JSON object for " + std::string(what));
    for (const auto& [name, value] : object.fields()) {
        if (std::ranges::find(allowed, name) == allowed.end())
            refuse(site, "unknown " + std::string(what) + " field '" + name + "'");
    }
}

const Json::Array& array(const Json& value, std::string_view what, const Span& site) {
    if (!value.is_array()) refuse(site, "expected an array for " + std::string(what));
    return value.items();
}

const Json::Object& object(const Json& value, std::string_view what, const Span& site) {
    if (!value.is_object()) refuse(site, "expected an object for " + std::string(what));
    return value.fields();
}

std::string text(const Json& value, std::string_view what, const Span& site) {
    if (!value.is_string()) refuse(site, "expected a string for " + std::string(what));
    return value.string();
}

std::uint32_t number(const Json& value, std::string_view what, const Span& site, std::uint32_t minimum = 1) {
    const auto integer = value.integer(-1);
    if (integer < minimum || integer > std::numeric_limits<std::uint32_t>::max())
        refuse(site, "invalid integer for " + std::string(what));
    return static_cast<std::uint32_t>(integer);
}

bool boolean(const Json& value, std::string_view what, const Span& site) {
    if (!value.is_bool()) refuse(site, "expected a boolean for " + std::string(what));
    return value.boolean();
}

Sha256Digest digest(const Json& value, std::string_view what, const Span& site) {
    const auto parsed = Sha256Digest::parse(text(value, what, site));
    if (!parsed) refuse(site, "expected 64 lowercase SHA-256 digits for " + std::string(what));
    return *parsed;
}

BundleManifest decode(const Json& doc, const Span& site) {
    keys(doc, {"schema", "kind", "id", "name", "version", "language", "image", "files", "requires",
               "capabilities", "origins", "limits", "contracts", "ui", "csp", "dev", "routing", "base"}, "bundle", site);
    const auto schema = number(doc["schema"], "schema", site, 0);
    if (schema != 1) refuse_version(site, "unsupported bundle schema " + std::to_string(schema) + ", expected 1");
    const auto& image = doc["image"];
    keys(image, {"path", "digest"}, "image", site);
    BundleManifest bundle{read_kind(text(doc["kind"], "kind", site), site), text(doc["id"], "id", site),
                          text(doc["name"], "name", site), text(doc["version"], "version", site),
                          text(doc["language"], "language", site),
                          {text(image["path"], "image path", site), digest(image["digest"], "image digest", site)}};
    for (const auto& entry : array(doc["files"], "files", site)) {
        keys(entry, {"path", "sha256", "bytes", "media_type", "initial"}, "file row", site);
        auto path = text(entry["path"], "file path", site);
        BundleFile file{digest(entry["sha256"], "digest of '" + path + "'", site),
                        number(entry["bytes"], "byte length of '" + path + "'", site, 0),
                        text(entry["media_type"], "media type of '" + path + "'", site),
                        boolean(entry["initial"], "initial flag of '" + path + "'", site)};
        if (!bundle.files.emplace(path, std::move(file)).second) refuse(site, "duplicate bundle file '" + path + "'");
    }
    for (const auto& [axis, version] : object(doc["requires"], "requires", site))
        bundle.requirements.emplace(axis, number(version, "requirement '" + axis + "'", site));
    for (const auto& value : array(doc["capabilities"], "capabilities", site)) {
        const auto name = text(value, "capability", site);
        const auto capability = target_capability(bundle.kind, name);
        if (!capability) refuse(site, "bundle capability '" + name + "' is not allowed for this target");
        bundle.capabilities.push_back(*capability);
    }
    for (const auto& [use, values] : object(doc["origins"], "origins", site)) {
        auto& output = bundle.origins[use];
        for (const auto& value : array(values, "origins for '" + use + "'", site)) output.push_back(text(value, use, site));
    }
    for (const auto& [limit, value] : object(doc["limits"], "limits", site)) {
        if (!value.is_array()) {
            bundle.limits.emplace(limit, number(value, limit, site, 0));
        } else {
            if (value.size() != 2) refuse_limit(site, "expected two values for bundle limit '" + limit + "'");
            bundle.limits.emplace(limit, std::array{number(value[0], limit, site, 0), number(value[1], limit, site, 0)});
        }
    }
    for (const auto& entry : array(doc["contracts"], "contracts", site)) {
        keys(entry, {"id", "version"}, "contract", site);
        const auto id = text(entry["id"], "contract id", site);
        if (!bundle.contracts.emplace(id, number(entry["version"], "contract version", site)).second)
            refuse(site, "duplicate bundle contract '" + id + "'");
    }
    if (doc.contains("ui")) bundle.ui = digest(doc["ui"], "ui", site);
    if (doc.contains("csp")) bundle.csp = text(doc["csp"], "csp", site);
    if (doc.contains("dev")) bundle.dev = boolean(doc["dev"], "dev", site);
    if (doc.contains("routing")) bundle.routing = text(doc["routing"], "routing", site);
    if (doc.contains("base")) bundle.base = text(doc["base"], "base", site);
    return bundle;
}

Json encode(const BundleManifest& bundle) {
    Json::Array files, capabilities, contracts;
    Json::Object requirements, limits, origins;
    for (const auto& [path, file] : bundle.files) {
        files.emplace_back(Json{{"path", path}, {"sha256", file.sha256.hex()}, {"bytes", file.bytes},
                                {"media_type", file.media_type}, {"initial", file.initial}});
    }
    for (const auto& [axis, version] : bundle.requirements) requirements.emplace(axis, version);
    std::vector<std::string_view> groups;
    for (const auto cap : bundle.capabilities) groups.push_back(capability_name(cap));
    std::ranges::sort(groups);
    for (const auto name : groups) capabilities.emplace_back(name);
    for (const auto& [use, values] : bundle.origins) {
        auto sorted = values;
        std::ranges::sort(sorted);
        Json::Array entries;
        for (auto& item : sorted) entries.emplace_back(std::move(item));
        origins.emplace(use, std::move(entries));
    }
    for (const auto& [name, value] : bundle.limits) {
        if (const auto* scalar = std::get_if<std::uint32_t>(&value)) limits.emplace(name, *scalar);
        else {
            const auto& pair = std::get<std::array<std::uint32_t, 2>>(value);
            limits.emplace(name, Json::Array{pair[0], pair[1]});
        }
    }
    for (const auto& [id, version] : bundle.contracts) contracts.emplace_back(Json{{"id", id}, {"version", version}});
    Json doc{{"schema", 1}, {"kind", kind_name(bundle.kind)}, {"id", bundle.id}, {"name", bundle.name},
             {"version", bundle.version}, {"language", bundle.language},
             {"image", Json{{"path", bundle.image.path}, {"digest", bundle.image.digest.hex()}}},
             {"files", std::move(files)}, {"requires", std::move(requirements)}, {"origins", std::move(origins)},
             {"capabilities", std::move(capabilities)}, {"limits", std::move(limits)}, {"contracts", std::move(contracts)}};
    if (bundle.ui) doc["ui"] = bundle.ui->hex();
    if (bundle.csp) doc["csp"] = *bundle.csp;
    if (bundle.dev) doc["dev"] = *bundle.dev;
    if (bundle.routing) doc["routing"] = *bundle.routing;
    if (bundle.base) doc["base"] = *bundle.base;
    return doc;
}

// Json::dump uses a stream for numbers. Write the integer-only bundle
// schema independently of the process's C++ stream locale.
void canonical_json(const Json& value, std::string& output) {
    if (value.is_array()) {
        output += '[';
        bool first = true;
        for (const auto& item : value.items()) {
            if (!first) output += ',';
            first = false;
            canonical_json(item, output);
        }
        output += ']';
    } else if (value.is_object()) {
        output += '{';
        bool first = true;
        for (const auto& [key, item] : value.fields()) {
            if (!first) output += ',';
            first = false;
            output += Json(key).dump();
            output += ':';
            canonical_json(item, output);
        }
        output += '}';
    } else if (value.is_number()) {
        output += std::to_string(value.integer());
    } else {
        output += value.dump();
    }
}

struct FileContents {
    Sha256Digest digest;
    std::vector<std::uint8_t> bytes;
};

fs::path checked_path(const fs::path& root, std::string_view relative, const Span& site) {
    const auto local = path_from_text(relative);
    fs::path current = root;
    for (auto it = local.begin(); it != local.end(); ++it) {
        current /= *it;
        std::error_code error;
        const auto status = fs::symlink_status(current, error);
        const bool leaf = std::next(it) == local.end();
        if (fs::is_symlink(status)) refuse(site, "symbolic link in bundle path '" + std::string(relative) + "'");
#ifdef _WIN32
        // A junction is a link too, though the standard library gives it a
        // file type of its own; another reparse point is not a regular file.
        if (const auto reparse = detail::reparse_at(current); reparse && *reparse != detail::Reparse::none) {
            if (*reparse == detail::Reparse::link) refuse(site, "symbolic link in bundle path '" + std::string(relative) + "'");
            refuse(site, "missing or non-regular bundle file '" + std::string(relative) + "': " + detail::reparse_limitation);
        }
#endif
        if (error || (leaf ? !fs::is_regular_file(status) : !fs::is_directory(status)))
            refuse(site, "missing or non-regular bundle file '" + std::string(relative) + "'");
    }
    return current;
}

class BundleInput {
public:
    explicit BundleInput(const fs::path& path) {
#ifndef _WIN32
        const auto parent = detail::open_root(path.parent_path());
        file_ = detail::FileDescriptor(::openat(parent.get(), path.filename().c_str(),
                                               O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
        if (file_.get() < 0 || ::fstat(file_.get(), &before_) != 0 || !S_ISREG(before_.st_mode) || before_.st_size < 0)
            throw detail::ReadFailure("cannot open a regular file without following links");
        size = static_cast<std::uint64_t>(before_.st_size);
#else
        // `path` is canonical (checked_path), so the opened file must be
        // there: a junction or link swapped in on the way gives another
        // final path.
        file_ = detail::open_no_follow(path);
        const auto state = file_.valid() ? detail::state_of(file_.get()) : std::nullopt;
        if (state && state->reparse() == detail::Reparse::other) throw detail::ReadFailure(detail::reparse_limitation);
        if (!state || state->directory() || state->reparse() != detail::Reparse::none ||
            !detail::opened_at(file_.get(), path))
            throw detail::ReadFailure("cannot open a regular file without following links");
        before_ = *state;
        size = state->size;
#endif
    }

    bool read(std::span<std::uint8_t> output) {
#ifndef _WIN32
        while (!output.empty()) {
            const auto count = ::read(file_.get(), output.data(), output.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            output = output.subspan(static_cast<std::size_t>(count));
        }
        return true;
#else
        while (!output.empty()) {
            DWORD count = 0;
            const auto wanted = static_cast<DWORD>(std::min<std::size_t>(output.size(), 1u << 30));
            if (!::ReadFile(file_.get(), output.data(), wanted, &count, nullptr) || count == 0) return false;
            output = output.subspan(count);
        }
        return true;
#endif
    }

    bool unchanged() {
#ifndef _WIN32
        std::uint8_t extra;
        ssize_t next;
        do next = ::read(file_.get(), &extra, 1); while (next < 0 && errno == EINTR);
        struct stat after{};
        return next == 0 && ::fstat(file_.get(), &after) == 0 && detail::unchanged(before_, after);
#else
        std::uint8_t extra;
        DWORD next = 0;
        const auto after = ::ReadFile(file_.get(), &extra, 1, &next, nullptr) ? detail::state_of(file_.get()) : std::nullopt;
        return next == 0 && after && *after == before_;
#endif
    }

    std::uint64_t size;

private:
#ifndef _WIN32
    detail::FileDescriptor file_{-1};
    struct stat before_{};
#else
    detail::FileHandle file_;
    detail::FileState before_;
#endif
};

FileContents read_contents(const fs::path& root, std::string_view relative, std::uint64_t limit,
                           const BundleFile* expected, bool retain, const Span& site) try {
    const auto path = checked_path(root, relative, site);
    BundleInput input(path);
    const auto size = input.size;
    if (size > limit) refuse(site, "bundle file '" + std::string(relative) + "' exceeds W-1 (" + std::to_string(limit) + " bytes)");
    if (expected && size != expected->bytes) refuse(site, "byte length mismatch for bundle file '" + std::string(relative) + "'");

    util::Sha256 hash;
    std::vector<std::uint8_t> bytes;
    if (retain) bytes.resize(static_cast<std::size_t>(size));
    std::array<std::uint8_t, 64 * 1024> buffer;
    for (std::uint64_t offset = 0; offset < size;) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(size - offset, buffer.size()));
        auto* data = retain ? bytes.data() + static_cast<std::size_t>(offset) : buffer.data();
        if (!input.read(std::span(data, count)))
            refuse(site, "cannot read exact byte length of bundle file '" + std::string(relative) + "'");
        hash.update(std::span(data, count));
        offset += count;
    }
    if (!input.unchanged())
        refuse(site, "bundle file changed during reading: '" + std::string(relative) + "'");
    const auto pin = Sha256Digest::from_bytes(hash.finish());
    if (expected && expected->sha256 != pin) {
        refuse(site, "SHA-256 mismatch for bundle file '" + std::string(relative) + "': expected " +
                     std::string(expected->sha256.hex()) + ", found " + std::string(pin.hex()));
    }
    return {pin, std::move(bytes)};
} catch (const detail::ReadFailure& error) {
    refuse(site, "cannot read bundle file '" + std::string(relative) + "': " + error.what());
}

void verify_tree(const fs::path& root, const BundleManifest& manifest, const Span& site) {
    auto directories = validate_files(manifest, site);
    std::set<std::string, std::less<>> found;
    const auto limit = file_limit(manifest);
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const auto relative = path_text(it->path().lexically_relative(root));
        const auto status = it->symlink_status(error);
        if (error) break;
        if (fs::is_symlink(status)) refuse(site, "symbolic link in bundle tree: '" + relative + "'");
#ifdef _WIN32
        if (const auto reparse = detail::reparse_at(it->path()); reparse && *reparse != detail::Reparse::none) {
            if (*reparse == detail::Reparse::link) refuse(site, "symbolic link in bundle tree: '" + relative + "'");
            refuse(site, "non-regular entry in bundle: '" + relative + "': " + detail::reparse_limitation);
        }
#endif
        if (fs::is_directory(status)) {
            if (directories.erase(relative) != 1) refuse(site, "unlisted directory in bundle: '" + relative + "'");
            continue;
        }
        if (!fs::is_regular_file(status)) refuse(site, "non-regular entry in bundle: '" + relative + "'");
        if (relative == manifest_name) continue;
        const auto file = manifest.files.find(relative);
        if (file == manifest.files.end()) refuse(site, "unlisted file in bundle: '" + relative + "'");
        if (!found.insert(relative).second) refuse(site, "repeated file in bundle: '" + relative + "'");
        read_contents(root, relative, limit, &file->second, false, site);
    }
    if (error) refuse(site, "cannot read bundle directory (" + error.message() + ")");
    for (const auto& [path, file] : manifest.files) {
        if (!found.contains(path)) refuse(site, "missing bundle file '" + path + "'");
    }
}

} // namespace

std::string write_bundle_manifest(const BundleManifest& manifest, std::string_view file) {
    const Span site{std::string(file)};
    validate_metadata(manifest, site);
    validate_files(manifest, site);
    std::string result;
    canonical_json(encode(manifest), result);
    if (result.size() > default_file_limit) refuse(site, "bundle.json exceeds the manifest byte limit");
    return result;
}

BundleManifest parse_bundle_manifest(std::string_view bytes, std::string_view file) {
    const Span site{std::string(file)};
    if (bytes.size() > default_file_limit) refuse(site, "bundle.json exceeds the manifest byte limit");
    if (!valid_utf8(bytes)) refuse(site, "bundle.json is not UTF-8");
    Json doc;
    try {
        doc = Json::parse(bytes);
    } catch (const std::runtime_error& error) {
        refuse(site, "cannot read bundle.json: " + std::string(error.what()));
    }
    auto manifest = decode(doc, site);
    if (write_bundle_manifest(manifest, file) != bytes) refuse(site, "bundle.json is not canonical schema-1 JSON");
    return manifest;
}

VerifiedBundle::VerifiedBundle(fs::path directory, BundleManifest manifest, Sha256Digest digest)
    : directory_(std::move(directory)), manifest_(std::move(manifest)), digest_(digest) {}

std::vector<std::uint8_t> VerifiedBundle::read_file(std::string_view relative) const {
    const Span site{path_text(directory_ / manifest_name)};
    const auto found = manifest_.files.find(relative);
    if (found == manifest_.files.end()) refuse(site, "unlisted bundle file '" + std::string(relative) + "'");
    return read_contents(directory_, relative, file_limit(manifest_), &found->second, true, site).bytes;
}

VerifiedBundle verify_bundle_directory(const fs::path& directory, const BundleVerification& verification) {
    const Span site{path_text(directory / manifest_name)};
    std::error_code error;
    auto absolute = fs::absolute(directory, error);
    while (absolute.has_relative_path() && (absolute.filename().empty() || absolute.filename() == "."))
        absolute = absolute.parent_path();
    if (error || !fs::is_directory(fs::symlink_status(absolute, error)) || error)
        refuse(site, "bundle root must be an existing directory without links");
    const auto root = fs::canonical(absolute, error);
    if (error) refuse(site, "cannot resolve bundle directory");
    const auto input = read_contents(root, manifest_name, default_file_limit, nullptr, true, site);
    if (verification.manifest_digest && *verification.manifest_digest != input.digest) {
        refuse(site, "SHA-256 mismatch for bundle.json: expected " + std::string(verification.manifest_digest->hex()) +
                     ", found " + std::string(input.digest.hex()));
    }
    auto manifest = parse_bundle_manifest(std::string_view(reinterpret_cast<const char*>(input.bytes.data()), input.bytes.size()), site.file);
    if (verification.expected_kind && *verification.expected_kind != manifest.kind) {
        refuse(site, "bundle kind '" + std::string(kind_name(manifest.kind)) + "', expected '" +
                     std::string(kind_name(*verification.expected_kind)) + "'");
    }
    if (manifest.dev.value_or(false) && !verification.allow_dev) {
        refuse(site, "bundle was built by `neura dev`; build it with `neura build` to run, package or ship it");
    }
    if (verification.expected_id && *verification.expected_id != manifest.id) {
        refuse(site, "bundle id '" + manifest.id + "', expected '" + *verification.expected_id + "'");
    }
    for (const auto& [axis, required] : manifest.requirements) {
        const auto supported = verification.supported_requirements.find(axis);
        if (supported == verification.supported_requirements.end() || supported->second != required) {
            const auto found = supported == verification.supported_requirements.end() ? "unavailable" : std::to_string(supported->second);
            refuse_requirement(site, "bundle requirement '" + axis + "' needs version " + std::to_string(required) +
                         ", host has " + found);
        }
    }
    verify_tree(root, manifest, site);
    return VerifiedBundle(root, std::move(manifest), input.digest);
}

} // namespace neura::project

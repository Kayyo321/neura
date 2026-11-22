#include "app/project.hpp"

#include "cli/module_path.hpp"
#include "cli/tool.hpp"
#include "neura/version.hpp"
#include "project/files.hpp"
#include "project/toml.hpp"
#include "tooling/json.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include "platform/error.hpp"
#include "platform/file.hpp"
#include "platform/process.hpp"
#include "platform/self.hpp"
#include "platform/text.hpp"

#include <chrono>
#include <thread>
#endif

#ifndef _WIN32
extern char** environ;
#endif

// posix_spawn can start a child in another directory with glibc 2.29 and
// macOS 10.15; elsewhere the compiler is given an absolute source path.
// CreateProcessW takes the child's directory (W32).
#if defined(_WIN32) || defined(__APPLE__) || (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29)))
#define NEURA_SPAWN_CHDIR 1
#else
#define NEURA_SPAWN_CHDIR 0
#endif

namespace neura::app {
namespace {

using namespace project;
using tooling::Json;

// The versions of the host interfaces a bundle can require, by axis
// (Apps030.md, "Version axes"). The browser adapter and UI host are those of
// the browser runtime; the server host and the desktop host have their own.
constexpr std::pair<std::string_view, std::uint32_t> host_axes[] = {
    {"browser_adapter", 1}, {"ui_host", 1}, {"server_host", 1}, {"desktop_host", 1},
};

// The provider components of a server or desktop target and the version of
// each that this SDK installs. All three stay in the core component
// (owner decision, 2026-11-18).
constexpr std::string_view sdk_version = neura::version::core;

const std::set<std::string, std::less<>>& installed_providers() {
    static const std::set<std::string, std::less<>> providers = [] {
        std::set<std::string, std::less<>> found{"sqlite"};
#ifdef NEURA_HAS_POSTGRES_PROVIDER
        found.emplace("postgresql");
#endif
#ifdef NEURA_HAS_TLS_PROVIDER
        found.emplace("tls");
#endif
        return found;
    }();
    return providers;
}

// Every provider name this release defines, installed here or not.
constexpr std::string_view known_providers[] = {"postgresql", "sqlite", "tls"};

// The page modules of the browser runtime, dependencies first: a module's
// imports are renamed before its own digest is taken.
constexpr std::string_view page_modules[] = {"ui-wire.mjs", "dom.mjs", "nvm.js", "ui.mjs", "fetch.mjs", "boot.mjs"};

[[noreturn]] void fail_E1024(const Span& site, std::string message) {
    throw Diagnostic(site, std::move(message)).with_code("E1024");
}

[[noreturn]] void fail_E1027(const Span& site, std::string message) {
    throw Diagnostic(site, std::move(message)).with_code("E1027");
}

[[noreturn]] void fail_E1029(const Span& site, std::string message) {
    throw Diagnostic(site, std::move(message)).with_code("E1029");
}

[[noreturn]] void fail_E1031(const Span& site, std::string message) {
    throw Diagnostic(site, std::move(message)).with_code("E1031");
}

std::string kind_text(TargetKind kind) {
    switch (kind) {
    case TargetKind::Web: return "web";
    case TargetKind::Server: return "server";
    case TargetKind::Desktop: return "desktop";
    }
    return {};
}

std::string base64(std::span<const std::uint8_t> bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += alphabet[(n >> 6) & 63];
        out += alphabet[n & 63];
    }
    if (i + 1 == bytes.size()) {
        const std::uint32_t n = bytes[i] << 16;
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const std::uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += alphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string sri(std::string_view data) {
    util::Sha256 hash;
    hash.update(data);
    const auto digest = hash.finish();
    return "sha256-" + base64(digest);
}

std::string html_escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&#x27;"; break;
        default: out += c;
        }
    }
    return out;
}

// Canonical JSON: sorted keys, compact, integers only (as bundle.json).
void canonical(const Json& value, std::string& out) {
    if (value.is_array()) {
        out += '[';
        bool first = true;
        for (const auto& item : value.items()) {
            if (!first) out += ',';
            first = false;
            canonical(item, out);
        }
        out += ']';
    } else if (value.is_object()) {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : value.fields()) {
            if (!first) out += ',';
            first = false;
            out += Json(key).dump();
            out += ':';
            canonical(item, out);
        }
        out += '}';
    } else if (value.is_number()) {
        out += std::to_string(value.integer());
    } else {
        out += value.dump();
    }
}

std::string canonical(const Json& value) {
    std::string out;
    canonical(value, out);
    return out;
}

const std::map<std::string, std::string, std::less<>> media_types{
    {".html", "text/html"}, {".js", "text/javascript"}, {".mjs", "text/javascript"},
    {".wasm", "application/wasm"}, {".nvr", "application/octet-stream"}, {".json", "application/json"},
    {".css", "text/css"}, {".svg", "image/svg+xml"}, {".png", "image/png"}, {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".webp", "image/webp"}, {".avif", "image/avif"},
    {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".ttf", "font/ttf"},
    {".otf", "font/otf"}, {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mp3", "audio/mpeg"},
    {".ogg", "audio/ogg"}, {".wav", "audio/wav"}, {".vtt", "text/vtt"}, {".txt", "text/plain"},
    {".pdf", "application/pdf"}, {".sql", "application/sql"}, {".toml", "application/toml"},
};

std::string lower_extension(const fs::path& path) {
    auto ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; });
    return ext;
}

std::string media_type(std::string_view path) {
    const auto ext = lower_extension(detail::path_from_text(path));
    const auto found = media_types.find(ext);
    return found == media_types.end() ? "application/octet-stream" : found->second;
}

// A bundle being assembled in a staging directory.
class Assembly {
public:
    Assembly(fs::path root, std::uint64_t limit, Span site) : root_(std::move(root)), limit_(limit), site_(std::move(site)) {}

    void add(const std::string& path, std::string_view data, bool initial = true) {
        if (data.size() > limit_) {
            fail_E1029(site_, "bundle file '" + path + "' is " + std::to_string(data.size()) +
                                    " bytes, over W-1 (" + std::to_string(limit_) + " bytes)");
        }
        if (files_.contains(path)) return;
        write_file(root_ / detail::path_from_text(path), data);
        files_.emplace(path, BundleFile{sha256_of(data), data.size(), media_type(path), initial});
    }

    // Copies an existing bundle directory's files below `prefix`.
    void embed(const fs::path& directory, const std::string& prefix) {
        std::vector<fs::path> entries;
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) entries.push_back(entry.path());
        }
        std::ranges::sort(entries);
        for (const auto& path : entries) {
            add(prefix + detail::path_text(path.lexically_relative(directory)), read_file(path));
        }
    }

    Named<BundleFile>& files() { return files_; }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
    std::uint64_t limit_;
    Span site_;
    Named<BundleFile> files_;
};

std::string named(std::string_view name, std::string_view data) {
    const auto dot = name.find('.');
    return std::string(name.substr(0, dot)) + "." + std::string(sha256_of(data).hex()) +
           (dot == std::string_view::npos ? std::string{} : std::string(name.substr(dot)));
}

// --- NVR imports ------------------------------------------------------------------------------------------

struct ImageInfo {
    std::vector<std::pair<std::string, std::uint32_t>> imports;
    std::set<std::string> sections;
};

ImageInfo read_image(std::string_view image, const Span& site) {
    const auto bad = [&](const std::string& why) -> ImageInfo {
        fail_E1029(site, "the built image is not a valid NVR file: " + why);
    };
    const auto u32 = [&](std::size_t at) {
        std::uint32_t v = 0;
        std::memcpy(&v, image.data() + at, 4);
        return v;
    };
    const auto u64 = [&](std::size_t at) {
        std::uint64_t v = 0;
        std::memcpy(&v, image.data() + at, 8);
        return v;
    };
    if (image.size() < 24 || image.substr(0, 4) != "NEUR") return bad("no NEUR header");
    const auto count = u32(12);
    if (24 + 24ull * count > image.size()) return bad("truncated section directory");
    ImageInfo info;
    std::string_view table;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto at = 24 + 24 * static_cast<std::size_t>(i);
        const auto tag = std::string(image.substr(at, 4));
        const auto offset = u64(at + 8), size = u64(at + 16);
        if (offset + size > image.size()) return bad("section exceeds the file");
        info.sections.insert(tag);
        if (tag == "IMPT") table = image.substr(offset, size);
    }
    std::size_t at = 0;
    const auto next = [&]() -> std::uint32_t {
        if (at + 4 > table.size()) fail_E1029(site, "the built image has a truncated import table");
        std::uint32_t v = 0;
        std::memcpy(&v, table.data() + at, 4);
        at += 4;
        return v;
    };
    if (table.empty()) return info;
    const auto imports = next();
    for (std::uint32_t i = 0; i < imports; ++i) {
        const auto size = next();
        if (at + size > table.size()) fail_E1029(site, "the built image has a truncated import record");
        auto name = std::string(table.substr(at, size));
        at += size;
        const auto version = next();
        const auto parameters = next();
        at += 4ull * parameters + 8;
        info.imports.emplace_back(std::move(name), version);
    }
    return info;
}

// The capability group of a host service, or nullopt for the groups every
// target has (console, clock, entropy, process exit). "" marks a service no
// group of this release grants to a target of this kind.
std::optional<std::string> service_group(std::string_view name) {
    const auto starts = [&](std::string_view prefix) { return name.starts_with(prefix); };
    if (starts("neura.console.") || starts("neura.clock.") || name == "neura.entropy.fill" || name == "neura.process.exit")
        return std::nullopt;
    if (starts("neura.ui.") || starts("neura.history.")) return "ui";
    if (name == "neura.http.fetch") return "http_client";
    if (name == "neura.bridge.call") return "bridge";
    if (starts("neura.sql.")) return "sql";
    if (starts("neura.app.")) return "app";
    if (starts("neura.http.")) return "http_server";
    if (starts("neura.desktop.")) return "desktop";
    if (starts("neura.fs.")) return "fs";
    if (starts("neura.net.") || starts("neura.tls.")) return "net";
    if (starts("neura.env.")) return "env";
    return "";
}

// E1027: a host service whose group the target does not list. A web target
// may import only services of the web groups; for a server or desktop
// target, services outside the known groups (tasks, secrets, ...) are the
// host's own and pass.
void check_capabilities(const Target& target, const ImageInfo& image) {
    std::set<std::string, std::less<>> listed;
    for (const auto& cap : target.capabilities) listed.emplace(capability_name(cap.value));
    std::vector<std::string> refused;
    for (const auto& [name, version] : image.imports) {
        const auto group = service_group(name);
        if (!group) continue;
        if (group->empty()) {
            if (target.kind.value == TargetKind::Web) refused.push_back(name + " (no group a web target may list)");
            continue;
        }
        if (!listed.contains(*group)) {
            const bool allowed = target_capability(target.kind.value, *group).has_value();
            refused.push_back(name + " (group '" + *group + "'" + (allowed ? ", not listed" : ", not allowed for this kind") + ")");
        }
    }
    if (target.kind.value == TargetKind::Web) {
        for (const auto* tag : {"NATV", "CALB"}) {
            if (image.sections.contains(tag))
                refused.push_back(std::string("native code or callbacks (") + tag + ")");
        }
    }
    if (!refused.empty()) {
        std::string list;
        for (const auto& item : refused) list += (list.empty() ? "" : ", ") + item;
        fail_E1027(target.entry.span, "the entry's image uses host services this target does not declare: " + list);
    }
}

// --- Paths and compilation ------------------------------------------------------------------------------

std::vector<std::string> package_closure(const Manifest& manifest, const std::string& package) {
    std::vector<std::string> order;
    std::set<std::string> seen;
    std::vector<std::string> stack{package};
    while (!stack.empty()) {
        auto name = stack.back();
        stack.pop_back();
        if (!seen.insert(name).second) continue;
        order.push_back(name);
        const auto found = manifest.packages.find(name);
        if (found == manifest.packages.end()) continue;
        for (const auto& dep : found->second.depends) stack.push_back(dep.value);
    }
    return order;
}

fs::path entry_file(const Project& project, const Target& target) {
    const auto& root = project.resolved.packages.at(target.package.value).root;
    const auto file = root.value.parent_path() / module_path(target.entry.value);
    std::error_code error;
    const auto relative = file.lexically_relative(root.value);
    if (relative.empty() || *relative.begin() == ".." || !fs::is_regular_file(file, error)) {
        fail_E1024(target.entry.span, "entry module '" + target.entry.value + "' is not a file of package '" + target.package.value +
                 "': expected " + detail::path_text(file.lexically_relative(project.resolved.root)));
    }
    return file;
}

struct Compiled {
    std::string image;
    fs::path path;
};

Compiled compile(const Project& project, const Target& target, const BuildOptions& options, const fs::path& scratch,
                 bool native, const std::string& output_name) {
    const auto source = entry_file(project, target);
    std::vector<std::string> command{argument_path(options.neura), "build"};
    if (native) {
        // Desktop.md, "Writing the native child": the child lives as long as the application.
        for (const auto* arg : {"--native", "--deny-all", "--max-steps", "18446744073709551615", "--allow-clock"})
            command.emplace_back(arg);
    } else {
        command.emplace_back("--no-source-map");
    }
    if (options.no_optimize) command.emplace_back("--no-optimize");
    if (options.no_lift_hints) command.emplace_back("--no-lift-hints");
    if (auto problems = check_closure(project, {source}); !problems.empty()) {
        auto first = problems.front();
        for (std::size_t i = 1; i < problems.size(); ++i) first.also(problems[i]);
        throw first;
    }
    for (const auto& root : package_import_roots(project, target.package.value)) {
        command.emplace_back("-I");
        command.push_back(argument_path(root));
    }
    // The source is named relative to the project, as an import root sees it,
    // so the image records no absolute path. It is in generic form ('/'), so
    // an image built on Windows records the same path as one built on Linux.
    command.push_back(NEURA_SPAWN_CHDIR ? detail::path_text(source.lexically_relative(project.resolved.root))
                                        : source.string());
    const auto out = scratch / output_name;
    command.emplace_back("-o");
    command.push_back(argument_path(out));
    fs::create_directories(scratch);
    if (const int status = spawn_and_wait(command, project.resolved.root); status != 0) {
        fail_E1029(target.entry.span, "the compiler failed for target entry '" + target.entry.value +
                                             "' (status " + std::to_string(status) + ")");
    }
    return {read_file(out), out};
}

// --- Lock ------------------------------------------------------------------------------------------------

Sha256Digest component_digest(TargetKind kind, std::string_view name, std::string_view version) {
    return sha256_of("neura-component-v1\n" + kind_text(kind) + "\n" + std::string(name) + "\n" + std::string(version) + "\n");
}

// --- Web bundles ----------------------------------------------------------------------------------------

std::optional<fs::path> web_runtime_dir(const BuildOptions& options) {
    std::vector<fs::path> candidates;
    if (options.web_runtime) candidates.push_back(*options.web_runtime);
    if (const char* env = std::getenv("NEURA_WEB_RUNTIME"); env && *env) candidates.emplace_back(env);
    candidates.push_back(install_prefix() / "share" / "neura" / "web");
    for (const auto& dir : candidates) {
        std::error_code error;
        if (fs::is_regular_file(dir / "nvm.wasm", error) && fs::is_regular_file(dir / "nvm.mjs", error) &&
            fs::is_regular_file(dir / "boot.mjs", error))
            return dir;
        if (options.web_runtime && dir == *options.web_runtime) return dir; // reported missing below
    }
    return std::nullopt;
}

std::string join_origins(const std::vector<Located<Origin>>& origins) {
    std::vector<std::string> texts;
    for (const auto& o : origins) texts.push_back(o.value.text);
    std::ranges::sort(texts);
    texts.erase(std::unique(texts.begin(), texts.end()), texts.end());
    std::string out;
    for (const auto& t : texts) out += " " + t;
    return out;
}

std::vector<std::string> origin_texts(const std::vector<Located<Origin>>& origins) {
    std::vector<std::string> texts;
    for (const auto& o : origins) texts.push_back(o.value.text);
    std::ranges::sort(texts);
    texts.erase(std::unique(texts.begin(), texts.end()), texts.end());
    return texts;
}

std::uint64_t file_limit(const Target& target) {
    const auto found = target.limits.find("W-1");
    if (found == target.limits.end()) return 64ull * 1024 * 1024;
    return std::get<Limit>(found->second).value;
}

void copy_limits(const Target& target, BundleManifest& bundle) {
    for (const auto& [name, value] : target.limits) {
        if (const auto* one = std::get_if<Limit>(&value)) bundle.limits.emplace(name, one->value);
        else {
            const auto& two = std::get<std::array<Limit, 2>>(value);
            bundle.limits.emplace(name, std::array<std::uint32_t, 2>{two[0].value, two[1].value});
        }
    }
}

BundleManifest base_manifest(const Project& project, const Target& target) {
    const auto& identity = project.manifest.project;
    BundleManifest bundle{target.kind.value, identity.id.value, identity.name.value, identity.version.value,
                          identity.language.value, BundleImage{std::string{}, sha256_of({})}};
    for (const auto& cap : target.capabilities) {
        if (std::ranges::find(bundle.capabilities, cap.value) == bundle.capabilities.end())
            bundle.capabilities.push_back(cap.value);
    }
    copy_limits(target, bundle);
    return bundle;
}

#ifdef _WIN32
// Windows refuses to rename a directory while another program (an antivirus
// or an indexer scanning the new bundle, a browser fetching a page of the old
// one) has a file in it open. Those holds are short, so a rename is retried
// for about two seconds before it fails.
void rename_with_retries(const fs::path& from, const fs::path& to, std::error_code& error) {
    for (int attempt = 0;; ++attempt) {
        error.clear();
        fs::rename(from, to, error);
        const bool held = error && error.category() == std::system_category() &&
                          (error.value() == 5 /* ERROR_ACCESS_DENIED */ || error.value() == 32 /* ERROR_SHARING_VIOLATION */);
        if (!held || attempt == 40) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Removes a directory without following a link or junction in it; what
// cannot be removed now is left for the next build.
void remove_directory(const fs::path& path) {
    std::error_code error;
    platform::remove_tree(path, error);
}

// Where finish() moves the previous bundle: <out>.neura-old, or the first of
// <out>.neura-old-1 to -15 that is free. Leftovers of earlier builds are
// removed first; one that a program still holds is skipped, so a single open
// file cannot make every later build fail.
fs::path aside_for(const fs::path& out) {
    std::optional<fs::path> free;
    for (int index = 0; index < 16; ++index) {
        auto candidate = out;
        candidate += index == 0 ? std::string(".neura-old") : ".neura-old-" + std::to_string(index);
        remove_directory(candidate);
        std::error_code error;
        if (!free && fs::symlink_status(candidate, error).type() == fs::file_type::not_found) free = candidate;
    }
    if (free) return *free;
    auto old = out;
    old += ".neura-old";
    return old;
}
#endif

// Writes bundle.json, verifies the directory as a host would, and moves the
// staging directory into place.
BuildResult finish(Assembly& assembly, BundleManifest& bundle, const fs::path& out, const Span& site, bool dev) {
    bundle.files = assembly.files();
    const auto text = write_bundle_manifest(bundle, "bundle.json");
    write_file(assembly.root() / "bundle.json", text);
    BundleVerification verification;
    verification.supported_requirements = supported_requirements();
    verification.allow_dev = dev;
    auto verified = verify_bundle_directory(assembly.root(), verification);
    std::error_code error;
#ifdef _WIN32
    // The old bundle is moved aside first, so a file in it that is still
    // open can never leave a half-removed bundle at `out` (W32). When the new
    // bundle cannot be moved into place, the old one is moved back, so `out`
    // keeps the previous bundle (neura dev starts it again).
    const auto old = aside_for(out);
    fs::create_directories(out.parent_path());
    bool moved_aside = false;
    if (fs::exists(fs::symlink_status(out, error))) {
        rename_with_retries(out, old, error);
        if (error) {
            fail_E1029(site, "cannot move the previous bundle out of " + display_path(out) + ": " +
                                 platform::error_message(error));
        }
        moved_aside = true;
    }
    rename_with_retries(assembly.root(), out, error);
    if (error) {
        auto message = "cannot move the bundle into " + display_path(out) + ": " + platform::error_message(error);
        if (moved_aside) {
            std::error_code restore;
            rename_with_retries(old, out, restore);
            if (restore) {
                message += "; the previous bundle is in " + display_path(old) + " (" +
                           platform::error_message(restore) + ")";
            }
        }
        fail_E1029(site, message);
    }
    remove_directory(old);
#else
    fs::remove_all(out, error);
    fs::create_directories(out.parent_path());
    fs::rename(assembly.root(), out, error);
    if (error) fail_E1029(site, "cannot move the bundle into " + out.string() + ": " + error.message());
#endif
    return {out, verified.digest()};
}

fs::path staging_for(const fs::path& out) {
    auto staging = out;
    staging += ".neura-build";
#ifdef _WIN32
    remove_directory(staging);
#else
    std::error_code error;
    fs::remove_all(staging, error);
#endif
    fs::create_directories(staging);
    return staging;
}

BuildResult build_web(Project& project, const std::string& name, const Target& target, const BuildOptions& options,
                      const fs::path& out) {
    const auto& web = std::get<WebSettings>(target.settings);
    const auto runtime = web_runtime_dir(options);
    if (!runtime || !fs::is_regular_file(*runtime / "nvm.wasm")) {
        fail_E1031(target.kind.span, "the browser runtime target component is not installed: expected nvm.wasm, nvm.mjs and the page "
             "modules in " + display_path(runtime ? *runtime : install_prefix() / "share/neura/web") +
                 " (or name a directory with --web-runtime or NEURA_WEB_RUNTIME)");
    }
    const auto staging = staging_for(out);
    const auto scratch = out.parent_path() / (out.filename().string() + ".neura-image");
    const auto compiled = compile(project, target, options, scratch, false, "app.nvr");
    check_capabilities(target, read_image(compiled.image, target.entry.span));

    Assembly assembly(staging, file_limit(target), target.span);
    std::map<std::string, std::string> renamed;
    std::map<std::string, std::string> content;
    for (const auto* file : {"nvm.wasm", "nvm.mjs"}) {
        const auto data = read_file(*runtime / file);
        renamed[file] = named(file, data);
        content[file] = data;
        assembly.add(renamed[file], data);
    }
    static const std::regex import_pattern(R"(((?:from\s+|import\s*\(\s*))'\./([A-Za-z0-9_.-]+)')");
    for (const auto module : page_modules) {
        const auto text = read_file(*runtime / module);
        std::string rewritten;
        auto begin = std::sregex_iterator(text.begin(), text.end(), import_pattern);
        std::size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            const auto& match = *it;
            const auto target_name = match[2].str();
            if (!renamed.contains(target_name)) {
                fail_E1031(target.kind.span, "browser runtime module " + std::string(module) + " imports " +
                                                    target_name + ", which the runtime does not hold");
            }
            rewritten += text.substr(last, match.position() - last);
            rewritten += match[1].str() + "'./" + renamed[target_name] + "'";
            last = match.position() + match.length();
        }
        rewritten += text.substr(last);
        renamed[std::string(module)] = named(module, rewritten);
        content[std::string(module)] = rewritten;
        assembly.add(renamed[std::string(module)], rewritten);
    }
    const auto image_path = named("app.nvr", compiled.image);
    assembly.add(image_path, compiled.image);

    // Global stylesheets of the manifest first, then those the build listed
    // beside the image (docs/apps/Css.md), in their order.
    std::vector<std::string> links;
    const auto& resolved_target = project.resolved.targets[name];
    for (const auto& style : resolved_target.styles) {
        const auto data = read_file(style.value);
        const auto path = "styles/" + std::string(sha256_of(data).hex()) + ".css";
        assembly.add(path, data);
        links.push_back(path);
    }
    const auto listing = fs::path(compiled.path.string() + ".styles.json");
    if (fs::exists(listing)) {
        const auto styles = Json::parse(read_file(listing));
        if (styles["schema"].integer(0) != 1) fail_E1029(target.span, "unknown schema of " + detail::path_text(listing.filename()));
        std::set<std::string> linked;
        for (const auto& link : styles["stylesheets"].items()) linked.insert(link.string());
        for (const auto& entry : styles["files"].items()) {
            const auto path = entry["path"].string();
            const auto data = read_file(compiled.path.parent_path() / detail::path_from_text(path));
            if (std::string(sha256_of(data).hex()) != entry["sha256"].string())
                fail_E1029(target.span, "stylesheet output " + path + " does not match its listing");
            assembly.add(path, data, linked.contains(path));
        }
        for (const auto& link : styles["stylesheets"].items()) links.push_back(link.string());
    }
    // Package assets: every file of every asset root of the closure, by digest.
    for (const auto& package : package_closure(project.manifest, target.package.value)) {
        const auto found = project.resolved.packages.find(package);
        if (found == project.resolved.packages.end()) continue;
        for (const auto& root : found->second.assets) {
            std::vector<fs::path> entries;
            for (const auto& entry : fs::recursive_directory_iterator(root.value)) {
                if (entry.is_regular_file()) entries.push_back(entry.path());
            }
            std::ranges::sort(entries);
            for (const auto& file : entries) {
                const auto data = read_file(file);
                const auto ext = lower_extension(file);
                if (ext == ".nra") continue;
                assembly.add("assets/" + std::string(sha256_of(data).hex()) + ext, data, false);
            }
        }
    }
    fs::remove_all(scratch);

    const bool history = web.routing && web.routing->value == Routing::History;
    const std::string base = "/";
    const std::string prefix = history ? base : "./";
    std::string stylesheet_links;
    for (const auto& link : links) stylesheet_links += "<link rel=\"stylesheet\" href=\"" + html_escape(prefix + link) + "\">\n";

    bool http_client = false;
    for (const auto& cap : target.capabilities) http_client |= cap.value == Capability::HttpClient;
    const auto connect = origin_texts(web.origins.connect);
    Json boot{{"base", base},
              {"image", Json{{"digest", std::string(sha256_of(compiled.image).hex())}, {"path", image_path}}},
              {"routing", history ? "history" : "hash"},
              {"wasm", Json{{"digest", std::string(sha256_of(content["nvm.wasm"]).hex())}, {"path", renamed["nvm.wasm"]}}}};
    if (http_client) {
        Json::Array list;
        for (const auto& o : connect) list.emplace_back(o);
        boot["http"] = Json{{"origins", std::move(list)}};
    } else {
        boot["http"] = Json(nullptr);
    }
    Json::Object integrity;
    for (const auto* module : {"nvm.mjs", "ui-wire.mjs", "dom.mjs", "nvm.js", "ui.mjs", "fetch.mjs"})
        integrity.emplace(prefix + renamed[module], sri(content[module]));
    Json import_map{{"imports", Json::Object{}}, {"integrity", std::move(integrity)}};
    const auto import_text = canonical(import_map);
    const auto csp = "default-src 'none'; script-src 'self' 'wasm-unsafe-eval' '" + sri(import_text) +
                     "'; style-src 'self'" + join_origins(web.origins.style) + "; img-src 'self'" +
                     join_origins(web.origins.load) + "; font-src 'self'; connect-src 'self'" +
                     join_origins(web.origins.connect) + "; base-uri 'none'; form-action 'none'";
    std::string dev_script;
    if (options.dev_reload) {
        assembly.add("neura-dev.js", *options.dev_reload);
        dev_script = "<script src=\"" + prefix + "neura-dev.js\"></script>\n";
    }
    const auto boot_module = renamed["boot.mjs"];
    const auto page = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
                      "<meta http-equiv=\"Content-Security-Policy\" content=\"" + html_escape(csp) + "\">\n"
                      "<title>" + html_escape(project.manifest.project.name.value) + "</title>\n" +
                      stylesheet_links + "<script type=\"application/json\" id=\"neura-boot\">" + canonical(boot) +
                      "</script>\n<script type=\"importmap\">" + import_text + "</script>\n"
                      "<script type=\"module\" src=\"" + html_escape(prefix + boot_module) + "\" integrity=\"" +
                      sri(content["boot.mjs"]) + "\"></script>\n" + dev_script + "</head>\n<body>\n<div id=\"app\"></div>\n"
                      "<p id=\"neura-unsupported\" hidden>This application needs a browser with WebAssembly exception "
                      "handling: Chromium 156, Firefox 153 ESR, or the current Safari.</p>\n"
                      "<p id=\"neura-failure\" role=\"alert\" hidden></p>\n</body>\n</html>\n";
    assembly.add("index.html", page);
    if (history) assembly.add("404.html", page);

    auto bundle = base_manifest(project, target);
    bundle.image = {image_path, sha256_of(compiled.image)};
    bundle.requirements = {{"browser_adapter", 1}, {"ui_host", 1}};
    bundle.csp = csp;
    bundle.routing = history ? "history" : "hash";
    bundle.base = base;
    if (!web.origins.connect.empty()) bundle.origins["connect"] = connect;
    if (!web.origins.load.empty()) bundle.origins["load"] = origin_texts(web.origins.load);
    if (!web.origins.style.empty()) bundle.origins["style"] = origin_texts(web.origins.style);
    if (options.dev) bundle.dev = true;
    return finish(assembly, bundle, out, target.span, options.dev);
}

BuildResult build_server(Project& project, const std::string& name, const Target& target, const BuildOptions& options,
                         const fs::path& out) {
    const auto& server = std::get<ServerSettings>(target.settings);
    auto bundle = base_manifest(project, target);
    for (const auto& provider : server.providers) {
        if (!installed_providers().contains(provider.value)) {
            const bool known = std::ranges::find(known_providers, provider.value) != std::end(known_providers);
            std::string installed;
            for (const auto& p : installed_providers()) installed += (installed.empty() ? "" : ", ") + p;
            fail_E1031(provider.span, "provider '" + provider.value + "' is " +
                                             (known ? "not installed in this SDK" : "unknown") +
                                             "; installed: " + installed);
        }
        bundle.requirements.emplace("provider." + provider.value, 1);
    }
    for (const auto& [pool, settings] : server.sql) {
        if (!installed_providers().contains(settings.provider.value))
            fail_E1031(settings.provider.span, "SQL pool '" + pool + "' names provider '" + settings.provider.value +
                                                      "', which this SDK does not install");
    }
    bundle.requirements.emplace("server_host", 1);
    const auto staging = staging_for(out);
    const auto scratch = out.parent_path() / (out.filename().string() + ".neura-image");
    const auto compiled = compile(project, target, options, scratch, false, "app.nvr");
    fs::remove_all(scratch);
    check_capabilities(target, read_image(compiled.image, target.entry.span));
    Assembly assembly(staging, file_limit(target), target.span);
    const auto image_path = named("app.nvr", compiled.image);
    assembly.add(image_path, compiled.image);
    // Migrations keep their names: the host orders them by number (Sql.md).
    for (const auto& [pool, directory] : project.resolved.targets[name].migrations) {
        std::vector<fs::path> entries;
        for (const auto& entry : fs::directory_iterator(directory.value)) {
            if (entry.is_regular_file() && lower_extension(entry.path()) == ".sql") entries.push_back(entry.path());
        }
        std::ranges::sort(entries);
        for (const auto& file : entries)
            assembly.add("migrations/" + pool + "/" + detail::path_text(file.filename()), read_file(file));
    }
    // The host settings the manifest fixes, as `neura run` passes them.
    Json::Object settings;
    if (server.http.listen) settings.emplace("listen", server.http.listen->value);
    if (server.http.tls_listen) settings.emplace("tls_listen", server.http.tls_listen->value);
    if (server.http.admin_listen) settings.emplace("admin_listen", server.http.admin_listen->value);
    if (server.config.prefix) settings.emplace("config_env_prefix", server.config.prefix->value);
    Json::Object pools;
    for (const auto& [pool, settings_of_pool] : server.sql) {
        Json::Object entry{{"provider", settings_of_pool.provider.value}};
        if (settings_of_pool.migrations) entry.emplace("migrations", "migrations/" + pool);
        if (settings_of_pool.migrate)
            entry.emplace("migrate", settings_of_pool.migrate->value == MigrationMode::OnStart ? "on_start" : "manual");
        pools.emplace(pool, std::move(entry));
    }
    settings.emplace("sql", std::move(pools));
    settings.emplace("schema", 1);
    assembly.add("server.json", canonical(Json(std::move(settings))));
    if (options.dev) bundle.dev = true;
    bundle.image = {image_path, sha256_of(compiled.image)};
    return finish(assembly, bundle, out, target.span, options.dev);
}

BuildResult build_desktop(Project& project, const std::string& name, const Target& target, const BuildOptions& options,
                          const fs::path& out) {
    const auto& desktop = std::get<DesktopSettings>(target.settings);
    // The embedded web bundle is never marked `dev`: the desktop host refuses
    // one, and `neura dev` restarts the host after a build instead of
    // reloading the page.
    auto web_options = options;
    web_options.target = desktop.ui.value;
    web_options.dev = false;
    web_options.dev_reload.reset();
    if (options.dev) web_options.out = project.resolved.root / "dist" / "dev" / desktop.ui.value;
    else web_options.out.reset();
    const auto web = build_target(project, web_options);
    auto bundle = base_manifest(project, target);
    for (const auto& provider : desktop.providers) {
        if (!installed_providers().contains(provider.value))
            fail_E1031(provider.span, "provider '" + provider.value + "' is not installed in this SDK");
        bundle.requirements.emplace("provider." + provider.value, 1);
    }
    bundle.requirements.emplace("desktop_host", 1);
    const auto staging = staging_for(out);
    const auto scratch = out.parent_path() / (out.filename().string() + ".neura-image");
    // The native child is built for this system. A Windows child is a PE
    // file, which CreateProcess starts only by its .exe name (W31).
    const auto suffix = tool::executable_file("");
    const auto compiled = compile(project, target, options, scratch, true, "app-child" + suffix);
    fs::remove_all(scratch);
    Assembly assembly(staging, std::numeric_limits<std::uint64_t>::max(), target.span);
    assembly.embed(web.directory, "web/");
    const auto child = "bin/app-" + std::string(sha256_of(compiled.image).hex()) + suffix;
    assembly.add(child, compiled.image);
    fs::permissions(staging / child, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                         fs::perms::others_read | fs::perms::others_exec);
    std::vector<std::string> operations;
    for (const auto& op : desktop.operations) operations.push_back(op.value);
    std::ranges::sort(operations);
    operations.erase(std::unique(operations.begin(), operations.end()), operations.end());
    Json::Array ops;
    for (const auto& op : operations) ops.emplace_back(op);
    const auto registry = canonical(Json{{"operations", std::move(ops)}, {"schema", 1}});
    assembly.add("registry.json", registry);
    Json::Array capabilities, external;
    for (const auto& cap : target.capabilities) {
        if (cap.value == Capability::Desktop) {
            capabilities.emplace_back("desktop.clipboard_read");
            capabilities.emplace_back("desktop.clipboard_write");
        }
    }
    for (const auto& origin : origin_texts(desktop.origins.external)) external.emplace_back(origin);
    Json::Object limits;
    for (const auto& [limit, value] : target.limits) {
        if (limit.starts_with("D-")) {
            if (const auto* one = std::get_if<Limit>(&value)) limits.emplace(limit, one->value);
        }
    }
    Json::Object supported;
    for (const auto& [axis, version] : supported_requirements()) supported.emplace(axis, version);
    const auto web_manifest = read_file(web.directory / "bundle.json");
    Json description{{"capabilities", std::move(capabilities)},
                     {"child", Json{{"args", Json::Array{}}, {"executable", child}}},
                     {"externalOrigins", std::move(external)},
                     {"id", project.manifest.project.id.value},
                     {"kind", "desktop"},
                     {"limits", std::move(limits)},
                     {"registry", Json{{"path", "registry.json"}, {"sha256", std::string(sha256_of(registry).hex())}}},
                     {"schema", 1},
                     {"supportedRequirements", std::move(supported)},
                     {"version", project.manifest.project.version.value},
                     {"web", Json{{"directory", "web"}, {"manifestDigest", std::string(sha256_of(web_manifest).hex())}}}};
    assembly.add("desktop.json", canonical(description));
    bundle.image = {child, sha256_of(compiled.image)};
    bundle.ui = sha256_of(web_manifest);
    if (!desktop.origins.external.empty()) bundle.origins["external"] = origin_texts(desktop.origins.external);
    if (options.dev) bundle.dev = true;
    (void)name;
    return finish(assembly, bundle, out, target.span, options.dev);
}

} // namespace

// --- Public helpers --------------------------------------------------------------------------------------

Sha256Digest sha256_of(std::string_view bytes) {
    util::Sha256 hash;
    hash.update(bytes);
    return Sha256Digest::from_bytes(hash.finish());
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + display_path(path));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const fs::path& path, std::string_view bytes) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("cannot write " + display_path(path));
}

fs::path tool_directory() {
#ifdef _WIN32
    // The module's own final path, never the current directory, so no tool
    // or component is looked up where the user happens to stand.
    std::error_code error;
    const auto path = platform::executable_path(error);
    if (error || path.empty())
        throw std::runtime_error("cannot find the running executable: " + platform::error_message(error));
    return path.parent_path();
#else
#ifdef __linux__
    std::error_code error;
    auto path = fs::read_symlink("/proc/self/exe", error);
    if (!error) return path.parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) return fs::canonical(buffer.c_str()).parent_path();
#endif
    return fs::current_path();
#endif
}

fs::path install_prefix() { return tool_directory().parent_path(); }

Named<std::uint32_t> supported_requirements() {
    Named<std::uint32_t> result;
    for (const auto& [axis, version] : host_axes) result.emplace(axis, version);
    for (const auto& provider : installed_providers()) result.emplace("provider." + provider, 1);
    return result;
}

void print_diagnostic(std::ostream& err, const Diagnostic& diagnostic) {
    const auto& span = diagnostic.span;
    if (!span.file.empty()) {
        err << span.file;
        if (span.offset || span.length || span.line > 1 || span.column > 1) err << ':' << span.line << ':' << span.column;
        err << ": ";
    }
    err << "error";
    if (diagnostic.data() && !diagnostic.data()->code.empty()) err << '[' << diagnostic.data()->code << ']';
    err << ": " << diagnostic.what() << '\n';
    if (diagnostic.data()) {
        for (const auto& help : diagnostic.data()->helps) err << "  help: " << help << '\n';
        for (const auto& next : diagnostic.data()->more) print_diagnostic(err, next);
    }
}

std::string argument_path(const fs::path& path) {
#ifdef _WIN32
    return platform::native_utf8_path(path);
#else
    return path.string();
#endif
}

std::string display_path(const fs::path& path) {
#ifdef _WIN32
    return detail::path_text(path);
#else
    return path.string();
#endif
}

int spawn_and_wait(const std::vector<std::string>& arguments, const std::optional<fs::path>& directory) {
#ifdef _WIN32
    // CreateProcessW from the argument vector, never through cmd.exe, in a
    // kill-on-close job, so the child and whatever it starts end with this
    // process, as an interrupt ends a POSIX process group (W10, W32).
    std::error_code error;
    platform::Spawn spawn;
    try {
        spawn.program = platform::path_from_utf8(arguments.front());
    } catch (const std::system_error& invalid) {
        error = invalid.code();
    }
    spawn.arguments = arguments;
    if (directory) spawn.working_directory = *directory;
    spawn.job = platform::JobMode::kill_on_close;
    auto child = error ? platform::Process{} : platform::Process::start(spawn, error);
    if (!child) {
        std::cerr << "neura: cannot start " << arguments.front() << ": " << platform::error_message(error) << '\n';
        return 127;
    }
    const auto status = child.wait(std::nullopt, error);
    if (!status) {
        std::cerr << "neura: cannot wait for " << arguments.front() << ": " << platform::error_message(error) << '\n';
        return 1;
    }
    return status->shell_status();
#else
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
#if NEURA_SPAWN_CHDIR
    if (directory) posix_spawn_file_actions_addchdir_np(&actions, directory->c_str());
#else
    (void)directory;
#endif
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, arguments.front().c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        std::cerr << "neura: cannot start " << arguments.front() << ": " << std::strerror(error) << '\n';
        return 127;
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}

Project load_project(const fs::path& manifest_path) {
    const auto text = detail::path_text(manifest_path);
    std::string bytes;
    try {
        bytes = read_file(manifest_path);
    } catch (const std::runtime_error&) {
        throw Diagnostic(Span{text}, "cannot read the project manifest").with_code("E1020");
    }
    auto manifest = parse_manifest(bytes, text);
    auto resolved = resolve_project(manifest, manifest_path);
    return {std::move(manifest), std::move(resolved)};
}

std::optional<fs::path> find_manifest(const std::optional<std::string>& project, std::ostream& err) {
    if (project) {
        fs::path path(*project);
        std::error_code error;
        if (fs::is_directory(path, error)) path /= "neura.toml";
        if (!fs::is_regular_file(path, error)) {
            err << "neura: no project manifest at " << display_path(path) << '\n';
            return std::nullopt;
        }
        return path;
    }
    const auto here = fs::current_path() / "neura.toml";
    if (!fs::is_regular_file(here)) {
        err << "neura: no neura.toml in the current directory; name one with --project <manifest>\n";
        return std::nullopt;
    }
    return here;
}

void fill_components(Project& project) {
    auto& targets = project.resolved.lock_inputs.targets;
    targets.clear();
    for (const auto& [name, target] : project.manifest.targets) {
        auto& components = targets[name];
        const auto add = [&](std::string component, std::string version) {
            components.emplace(component, ResolvedComponent{version, component_digest(target.kind.value, component, version)});
        };
        switch (target.kind.value) {
        case TargetKind::Web:
            add("browser-runtime", std::string(sdk_version));
            break;
        case TargetKind::Server:
            add("server-host", std::string(sdk_version));
            for (const auto& p : std::get<ServerSettings>(target.settings).providers) add(p.value, std::string(sdk_version));
            break;
        case TargetKind::Desktop:
            add("desktop-engine", "45.3.3");
            for (const auto& p : std::get<DesktopSettings>(target.settings).providers) add(p.value, std::string(sdk_version));
            break;
        }
    }
}

void check_lock(Project& project, bool update, bool allow_missing) {
    fill_components(project);
    const auto path = project.resolved.root / "neura.lock";
    std::error_code error;
    const bool present = fs::is_regular_file(path, error);
    if (update || (allow_missing && !present)) {
        write_file(path, write_lock(make_lock(project.manifest, project.resolved.lock_inputs)));
        return;
    }
    if (!present) {
        validate_lock(nullptr, project.manifest, project.resolved.lock_inputs);
        return;
    }
    const auto lock = parse_lock(read_file(path), detail::path_text(path));
    validate_lock(&lock, project.manifest, project.resolved.lock_inputs);
}

void check_target(const Project& project, const std::string& name) {
    const auto found = project.manifest.targets.find(name);
    if (found == project.manifest.targets.end()) {
        std::string names;
        for (const auto& [target_name, target] : project.manifest.targets) names += (names.empty() ? "" : ", ") + target_name;
        throw Diagnostic(Span{detail::path_text(project.resolved.manifest)},
                         "the project has no target '" + name + "'" +
                             (names.empty() ? std::string{} : "; its targets are " + names))
            .with_code("E1023");
    }
    const auto& target = found->second;
    const std::vector<Text>* providers = nullptr;
    if (const auto* server = std::get_if<ServerSettings>(&target.settings)) providers = &server->providers;
    if (const auto* desktop = std::get_if<DesktopSettings>(&target.settings)) providers = &desktop->providers;
    if (providers) {
        for (const auto& provider : *providers) {
            if (installed_providers().contains(provider.value)) continue;
            const bool known = std::ranges::find(known_providers, provider.value) != std::end(known_providers);
            std::string installed;
            for (const auto& p : installed_providers()) installed += (installed.empty() ? "" : ", ") + p;
            fail_E1031(provider.span, "provider '" + provider.value + "' is " +
                                             (known ? "not installed in this SDK" : "unknown") +
                                             "; installed: " + installed);
        }
    }
    if (const auto* desktop = std::get_if<DesktopSettings>(&target.settings)) check_target(project, desktop->ui.value);
}

BuildResult build_target(Project& project, const BuildOptions& options) {
    const auto found = project.manifest.targets.find(options.target);
    if (found == project.manifest.targets.end()) {
        std::string names;
        for (const auto& [name, target] : project.manifest.targets) names += (names.empty() ? "" : ", ") + name;
        throw Diagnostic(Span{detail::path_text(project.resolved.manifest)},
                         "the project has no target '" + options.target + "'" +
                             (names.empty() ? std::string{} : "; its targets are " + names))
            .with_code("E1023");
    }
    if (options.source_map) {
        throw Diagnostic(found->second.span, "--source-map is not available for project builds in Neura 0.3.0")
            .with_code("E1029");
    }
#ifdef _WIN32
    if (found->second.kind.value == TargetKind::Desktop)
        fail_E1031(found->second.kind.span, windows_desktop_unavailable);
#endif
    const auto out = options.out ? fs::absolute(*options.out) : project.resolved.root / "dist" / options.target;
    switch (found->second.kind.value) {
    case TargetKind::Web: return build_web(project, found->first, found->second, options, out);
    case TargetKind::Server: return build_server(project, found->first, found->second, options, out);
    case TargetKind::Desktop: return build_desktop(project, found->first, found->second, options, out);
    }
    throw Diagnostic(found->second.span, "unknown target kind").with_code("E1023");
}

bool is_bundle_directory(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(path, error) && fs::exists(path / "bundle.json", error);
}

} // namespace neura::app

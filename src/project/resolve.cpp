#include "resolve.hpp"

#include "files.hpp"

#include "toml.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neura::project {
namespace {

namespace fs = std::filesystem;

using detail::path_from_text;
using detail::path_text;
using detail::ReadFailure;

bool within(const fs::path& path, const fs::path& root) {
    // Compare components, including the volume/root, rather than prefixes
    // of their string spellings.
    auto part = path.begin();
    for (const auto& base : root) {
        if (part == path.end() || *part != base) {
#ifdef _WIN32
            // Canonical Windows paths may still spell the same directory
            // with different case. Compare the actual ancestor identities.
            for (auto candidate = path; !candidate.empty(); candidate = candidate.parent_path()) {
                std::error_code error;
                if (fs::equivalent(candidate, root, error)) return true;
                if (candidate == candidate.root_path()) break;
            }
#endif
            return false;
        }
        ++part;
    }
    return true;
}

[[noreturn]] void outside(const Span& at, std::string message) {
    throw Diagnostic(at, std::move(message)).with_code("E1025");
}

[[noreturn]] void invalid_path(const Span& at, std::string message) {
    throw Diagnostic(at, std::move(message)).with_code("E1020");
}

[[noreturn]] void unresolved(const ExternalDependency& dependency, std::string message) {
    throw Diagnostic(dependency.span, std::move(message)).with_code("E1032")
        .label(dependency.path.span, "external dependency path");
}

bool missing(const std::error_code& error) {
    return error == std::errc::no_such_file_or_directory;
}

enum class EntryKind { File, Directory };

bool is_kind(fs::file_status status, EntryKind kind) {
    return kind == EntryKind::File ? fs::is_regular_file(status) : fs::is_directory(status);
}

std::string_view kind_name(EntryKind kind) {
    return kind == EntryKind::File ? "regular file" : "directory";
}

std::optional<fs::path> resolve_inside(const fs::path& root, const fs::path& path, const Span& at,
                                       EntryKind kind, bool optional, const ExternalDependency* dependency = nullptr) {
    const auto invalid = [&](std::string reason) {
        if (dependency) unresolved(*dependency, std::move(reason));
        invalid_path(at, std::move(reason));
    };
    if (path.empty() || path.has_root_path() || path_text(path).find('\0') != std::string::npos) {
        outside(at, "expected a project-relative path");
    }
    const auto normalized = path.lexically_normal();
    if (normalized.begin() != normalized.end() && *normalized.begin() == "..") {
        outside(at, "this path leaves the project directory");
    }
    auto current = root;
    std::error_code error;
    for (const auto& part : path) {
        if (part == ".." && current == root) outside(at, "this path leaves the project directory");
        // Only a directory has entries: "file/." and "file/" do not resolve.
        // realpath(3) says so on Linux, but macOS resolves "file/." to the
        // file, so the rule is checked here for every platform.
        if (current != root && !fs::is_directory(current, error)) {
            invalid("cannot resolve project path '" + path_text(path) + "'");
        }
        const auto next = current / part;
        current = fs::canonical(next, error);
        if (error) {
            if (optional && missing(error)) {
                // A dangling link is a real entry with an unavailable
                // destination. Only an absent directory entry is optional.
                const auto status = fs::symlink_status(next, error);
                if (status.type() == fs::file_type::not_found && (!error || missing(error))) return std::nullopt;
            }
            invalid("cannot resolve project path '" + path_text(path) + "'");
        }
        if (!within(current, root)) outside(at, "this path resolves outside the project directory");
    }
    const auto status = fs::status(current, error);
    if (error || !is_kind(status, kind)) {
        invalid("project path '" + path_text(path) + "' must name an existing " + std::string(kind_name(kind)));
    }
    return current;
}

ResolvedPath inside(const fs::path& root, const Text& path, EntryKind kind) {
    return {*resolve_inside(root, path_from_text(path.value), path.span, kind, false), path.span};
}

ResolvedPath external_path(const fs::path& root, const ExternalDependency& dependency) {
    const auto& declared = dependency.path;
    const auto path = path_from_text(declared.value);
    if (dependency.source == DependencySource::Archive) {
        return {*resolve_inside(root, path, declared.span, EntryKind::File, false, &dependency), declared.span};
    }
    if (path.empty() || path.has_root_path() || declared.value.find('\0') != std::string::npos) {
        outside(declared.span, "expected a manifest-relative external dependency directory");
    }
    std::error_code error;
    const auto directory = fs::canonical(root / path, error);
    if (error) unresolved(dependency, "cannot resolve external dependency '" + declared.value + "'");
    if (within(directory, root)) outside(declared.span, "this external dependency resolves inside the project directory");
    if (within(root, directory)) outside(declared.span, "the external dependency directory contains the project");
    if (!fs::is_directory(directory, error) || error) {
        unresolved(dependency, "external dependency '" + declared.value + "' must name an existing directory");
    }
    return {directory, declared.span};
}

struct FilePin {
    std::uint64_t size;
    Sha256Digest digest;
};

struct Child {
    std::string name;
    EntryKind kind;
};

std::string relative_name(std::string_view dir, std::string_view name) {
    if (dir.empty()) return std::string(name);
    return std::string(dir) + "/" + std::string(name);
}

constexpr std::string_view tree_domain = "neura-project-tree-v1\n";

#ifndef _WIN32

using detail::directory_flags;
using detail::FileDescriptor;
using detail::open_root;
using detail::subdirectory;
using detail::unchanged;

FilePin hash_file(const FileDescriptor& parent, const std::string& name) {
    struct stat status{};
    if (::fstatat(parent.get(), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(status.st_mode)) {
        throw ReadFailure("'" + name + "' is not a regular file");
    }
    const FileDescriptor file(::openat(parent.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    struct stat opened{};
    if (file.get() < 0 || ::fstat(file.get(), &opened) != 0 || !S_ISREG(opened.st_mode) ||
        status.st_dev != opened.st_dev || status.st_ino != opened.st_ino) {
        throw ReadFailure("cannot open regular file '" + name + "' without following links");
    }
    util::Sha256 hash;
    std::uint64_t size = 0;
    std::array<std::uint8_t, 65536> buffer;
    for (;;) {
        const auto count = ::read(file.get(), buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) continue;
            throw ReadFailure("cannot read '" + name + "'");
        }
        if (count == 0) break;
        const auto length = static_cast<std::size_t>(count);
        hash.update(std::span(buffer).first(length));
        size += length;
    }
    struct stat after{};
    if (::fstat(file.get(), &after) != 0 || after.st_size < 0 ||
        size != static_cast<std::uint64_t>(after.st_size) || !unchanged(opened, after)) {
        throw ReadFailure("'" + name + "' changed while it was read");
    }
    return {size, Sha256Digest::from_bytes(hash.finish())};
}

struct CloseDirectory {
    void operator()(DIR* directory) const { ::closedir(directory); }
};

class TreeReader {
public:
    TreeReader(fs::path root, const Span& at) : path_(std::move(root)), at_(at), root_(open_root(path_)) {}

    std::vector<Child> children(std::string_view relative) {
        auto fd = open_directory(relative);
        struct stat identity{};
        if (::fstat(fd.get(), &identity) != 0 || !seen_.emplace(identity.st_dev, identity.st_ino).second) {
            throw ReadFailure("dependency tree contains an unreadable or repeated directory");
        }
        std::unique_ptr<DIR, CloseDirectory> directory(::fdopendir(fd.get()));
        if (!directory) throw ReadFailure("cannot read the dependency directory");
        fd.release();
        std::vector<std::string> names;
        for (;;) {
            errno = 0;
            const auto* item = ::readdir(directory.get());
            if (!item) {
                if (errno != 0) throw ReadFailure("cannot read dependency directory '" + std::string(relative) + "'");
                break;
            }
            const std::string_view name(item->d_name);
            if (name != "." && name != "..") names.emplace_back(name);
        }
        std::ranges::sort(names);
        std::vector<Child> result;
        for (auto& name : names) {
            const auto rel = relative_name(relative, name);
            struct stat status{};
            if (::fstatat(::dirfd(directory.get()), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
                throw ReadFailure("cannot inspect dependency entry '" + rel + "'");
            }
            if (S_ISLNK(status.st_mode)) reject_link(rel);
            if (!S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode)) {
                throw ReadFailure("dependency entry '" + rel + "' is not a regular file or directory");
            }
            result.push_back({std::move(name), S_ISDIR(status.st_mode) ? EntryKind::Directory : EntryKind::File});
        }
        return result;
    }

    FilePin file(std::string_view dir, const std::string& name) const {
        return hash_file(open_directory(dir), name);
    }

private:
    FileDescriptor open_directory(std::string_view relative) const {
        FileDescriptor copy(::fcntl(root_.get(), F_DUPFD_CLOEXEC, 0));
        if (copy.get() < 0) throw ReadFailure("cannot hold the dependency root open");
        return subdirectory(std::move(copy), relative);
    }

    [[noreturn]] void reject_link(std::string_view rel) const {
        // Canonicalization here classifies the error. No file or directory
        // beyond the dependency root is opened for content.
        std::error_code error;
        const auto destination = fs::canonical(path_ / path_from_text(rel), error);
        if (!error && !within(destination, path_)) {
            outside(at_, "symbolic link '" + std::string(rel) + "' leaves the dependency directory");
        }
        throw ReadFailure("dependency entry '" + std::string(rel) + "' is a symbolic link");
    }

    fs::path path_;
    const Span& at_;
    FileDescriptor root_;
    std::set<std::pair<dev_t, ino_t>> seen_;
};

FilePin hash_file(const fs::path& path) {
    return hash_file(open_root(path.parent_path()), path_text(path.filename()));
}

#else

using detail::FileHandle;
using detail::FileState;
using detail::Reparse;

// Hashes an open file and refuses one that changed while it was read.
FilePin hash_open(const FileHandle& file, const FileState& before, const std::string& name) {
    util::Sha256 hash;
    std::uint64_t size = 0;
    std::array<std::uint8_t, 65536> buffer;
    for (;;) {
        DWORD count = 0;
        if (!::ReadFile(file.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
            throw ReadFailure("cannot read '" + name + "'");
        if (count == 0) break;
        hash.update(std::span(buffer).first(count));
        size += count;
    }
    const auto after = detail::state_of(file.get());
    if (!after || size != after->size || *after != before) throw ReadFailure("'" + name + "' changed while it was read");
    return {size, Sha256Digest::from_bytes(hash.finish())};
}

// The regular file at `path`, a canonical path: neither it nor any directory
// on the way may be a link, a junction or another reparse point.
FilePin hash_file_at(const fs::path& path, const std::string& name) {
    const auto file = detail::open_no_follow(path);
    const auto state = file.valid() ? detail::state_of(file.get()) : std::nullopt;
    if (state && state->reparse() == Reparse::other) {
        throw ReadFailure("'" + name + "' is not a regular file: " + detail::reparse_limitation);
    }
    if (!state || state->directory() || state->reparse() != Reparse::none) {
        throw ReadFailure("'" + name + "' is not a regular file");
    }
    if (!detail::opened_at(file.get(), path)) throw ReadFailure("cannot open regular file '" + name + "' without following links");
    return hash_open(file, *state, name);
}

FilePin hash_file(const fs::path& path) {
    return hash_file_at(path, path_text(path.filename()));
}

class TreeReader {
public:
    TreeReader(fs::path root, const Span& at) : path_(std::move(root)), at_(at), root_(open_directory("")) {}

    std::vector<Child> children(std::string_view relative) {
        const auto directory = open_directory(relative);
        const auto identity = detail::identity_of(directory.get());
        if (!identity || !seen_.insert(*identity).second) {
            throw ReadFailure("dependency tree contains an unreadable or repeated directory");
        }
        // The entries of the open directory itself, so none comes from a
        // directory that replaced it.
        std::vector<Child> result;
        std::vector<std::uint64_t> buffer(8192);
        for (auto kind = FileIdBothDirectoryRestartInfo;; kind = FileIdBothDirectoryInfo) {
            if (!::GetFileInformationByHandleEx(directory.get(), kind, buffer.data(),
                                                static_cast<DWORD>(buffer.size() * sizeof buffer[0]))) {
                if (::GetLastError() == ERROR_NO_MORE_FILES) break;
                throw ReadFailure("cannot read dependency directory '" + std::string(relative) + "'");
            }
            for (auto* at = reinterpret_cast<const std::uint8_t*>(buffer.data());;) {
                const auto& entry = *reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(at);
                const std::wstring_view wide(entry.FileName, entry.FileNameLength / sizeof(wchar_t));
                if (wide != L"." && wide != L"..") {
                    std::string name;
                    try {
                        name = path_text(fs::path(wide));
                    } catch (const std::exception&) {
                        throw ReadFailure("dependency tree contains a name without an unambiguous UTF-8 spelling");
                    }
                    // For a reparse point, EaSize holds its tag.
                    const auto reparse = detail::reparse_of(entry.FileAttributes, entry.EaSize);
                    if (reparse == Reparse::link) reject_link(relative_name(relative, name));
                    if (reparse == Reparse::other) {
                        throw ReadFailure("dependency entry '" + relative_name(relative, name) +
                                          "' is not a regular file or directory: " + detail::reparse_limitation);
                    }
                    if ((entry.FileAttributes & FILE_ATTRIBUTE_DEVICE) != 0) {
                        throw ReadFailure("dependency entry '" + relative_name(relative, name) +
                                          "' is not a regular file or directory");
                    }
                    const bool is_directory = (entry.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    result.push_back({std::move(name), is_directory ? EntryKind::Directory : EntryKind::File});
                }
                if (entry.NextEntryOffset == 0) break;
                at += entry.NextEntryOffset;
            }
        }
        std::ranges::sort(result, {}, &Child::name);
        return result;
    }

    FilePin file(std::string_view dir, const std::string& name) const {
        return hash_file_at(expected(relative_name(dir, name)), name);
    }

private:
    fs::path expected(std::string_view relative) const {
        return relative.empty() ? path_ : path_ / path_from_text(relative);
    }

    // A directory of the tree, opened where the canonical root and the names
    // read so far put it, with no link or junction on the way.
    FileHandle open_directory(std::string_view relative) const {
        const auto path = expected(relative);
        auto directory = detail::open_no_follow(path);
        const auto state = directory.valid() ? detail::state_of(directory.get()) : std::nullopt;
        if (state && state->reparse() == Reparse::other) {
            throw ReadFailure("dependency directory '" + std::string(relative.empty() ? "." : relative) +
                              "' is not a regular directory: " + detail::reparse_limitation);
        }
        if (!state || !state->directory() || state->reparse() != Reparse::none || !detail::opened_at(directory.get(), path)) {
            throw ReadFailure("cannot open directory '" + std::string(relative.empty() ? "." : relative) +
                              "' without following links");
        }
        return directory;
    }

    [[noreturn]] void reject_link(std::string_view rel) const {
        // Canonicalization here classifies the error. No file or directory
        // beyond the dependency root is opened for content.
        std::error_code error;
        const auto destination = fs::canonical(path_ / path_from_text(rel), error);
        if (!error && !within(destination, path_)) {
            outside(at_, "symbolic link '" + std::string(rel) + "' leaves the dependency directory");
        }
        throw ReadFailure("dependency entry '" + std::string(rel) + "' is a symbolic link");
    }

    fs::path path_;
    const Span& at_;
    FileHandle root_;
    std::set<detail::FileIdentity> seen_;
};

#endif

void hash_u64(util::Sha256& hash, std::uint64_t number) {
    std::array<std::uint8_t, 8> bytes;
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(number & 0xff);
        number >>= 8;
    }
    hash.update(bytes);
}

Sha256Digest hash_directory(const fs::path& root, const Span& at) {
    struct Entry {
        std::string path;
        std::optional<FilePin> file;
    };
    TreeReader reader(root, at);
    std::vector<Entry> entries;
    std::vector<std::string> pending{""};
    while (!pending.empty()) {
        auto dir = std::move(pending.back());
        pending.pop_back();
        for (const auto& child : reader.children(dir)) {
            auto path = relative_name(dir, child.name);
            if (!valid_utf8(child.name) || child.name.find('\\') != std::string::npos) {
                throw ReadFailure("dependency tree contains a name without an unambiguous UTF-8 spelling");
            }
            if (child.kind == EntryKind::Directory) {
                pending.push_back(path);
                entries.push_back({std::move(path), std::nullopt});
            } else {
                entries.push_back({std::move(path), reader.file(dir, child.name)});
            }
        }
    }

    std::ranges::sort(entries, {}, &Entry::path);
    util::Sha256 hash;
    hash.update(tree_domain);
    std::string_view previous;
    for (const auto& entry : entries) {
        if (entry.path == previous) throw ReadFailure("dependency tree contains the same path twice");
        previous = entry.path;
        hash.update(entry.file ? "F" : "D");
        hash_u64(hash, entry.path.size());
        hash.update(entry.path);
        if (entry.file) {
            hash_u64(hash, entry.file->size);
            hash.update(entry.file->digest.hex());
        }
    }
    return Sha256Digest::from_bytes(hash.finish());
}

void migrations(ResolvedTargetPaths& target, const fs::path& root, const Named<SqlPool>& pools) {
    for (const auto& [name, pool] : pools) {
        if (pool.migrations) target.migrations.emplace(name, inside(root, *pool.migrations, EntryKind::Directory));
    }
}

} // namespace

ResolvedProject resolve_project(const Manifest& manifest, const fs::path& manifest_path,
                                const std::optional<fs::path>& contracts_lock) {
    const auto& manifest_span = manifest.schema.span;
    const auto parent = manifest_path.has_parent_path() ? manifest_path.parent_path() : fs::path(".");
    std::error_code error;
    const auto root = fs::canonical(parent, error);
    if (error || !fs::is_directory(root, error) || error) {
        invalid_path(manifest_span, "cannot resolve the directory of the project manifest");
    }
    ResolvedProject result;
    result.root = root;
    result.manifest = *resolve_inside(root, manifest_path.filename(), manifest_span, EntryKind::File, false);

    for (const auto& [name, package] : manifest.packages) {
        ResolvedPackagePaths paths{inside(root, package.root, EntryKind::Directory), {}};
        for (const auto& asset : package.assets) paths.assets.push_back(inside(root, asset, EntryKind::Directory));
        result.packages.emplace(name, std::move(paths));
    }
    for (const auto& [name, target] : manifest.targets) {
        ResolvedTargetPaths paths;
        if (const auto* web = std::get_if<WebSettings>(&target.settings)) {
            for (const auto& style : web->styles) paths.styles.push_back(inside(root, style, EntryKind::File));
        } else if (const auto* server = std::get_if<ServerSettings>(&target.settings)) {
            migrations(paths, root, server->sql);
        } else if (const auto* desktop = std::get_if<DesktopSettings>(&target.settings)) {
            if (desktop->packaging.icon) paths.icon = inside(root, *desktop->packaging.icon, EntryKind::File);
            migrations(paths, root, desktop->sql);
        }
        result.targets.emplace(name, std::move(paths));
    }
    if (manifest.contracts_manifest) {
        result.contracts_manifest = inside(root, *manifest.contracts_manifest, EntryKind::File);
    }
    if (contracts_lock) {
        const auto& at = manifest.contracts_manifest ? manifest.contracts_manifest->span : manifest_span;
        if (const auto path = resolve_inside(root, *contracts_lock, at, EntryKind::File, true)) {
            try {
                result.lock_inputs.contracts_digest = hash_file(*path).digest;
            } catch (const ReadFailure& failure) {
                invalid_path(at, failure.what());
            }
            result.contracts_lock = ResolvedPath{*path, at};
        }
    }
    for (const auto& [name, dependency] : manifest.dependencies) {
        dependency_pin(name, dependency);
        auto path = external_path(root, dependency);
        try {
            const auto digest = dependency.source == DependencySource::Archive
                ? hash_file(path.value).digest : hash_directory(path.value, path.span);
            result.lock_inputs.dependencies.emplace(name, digest);
        } catch (const ReadFailure& failure) {
            unresolved(dependency, failure.what());
        }
        result.dependencies.emplace(name, std::move(path));
    }
    return result;
}

} // namespace neura::project

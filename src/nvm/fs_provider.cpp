#include "provider_internal.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>

#ifdef _WIN32
#include "platform/file.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <cstddef>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace neura::nvm::provider {

namespace {

// OpenMode ordinals in Std.Fs.File, and SeekOrigin ordinals.
enum OpenMode : std::int32_t { Read = 0, Write = 1, Append = 2, ReadWrite = 3, CreateNew = 4 };

// FileKind ordinals in Std.Fs.Fs.
enum FileKind : std::int32_t { RegularFile = 0, Directory = 1, OtherKind = 2 };

#ifdef _WIN32

// An absolute path as GetFullPathNameW gives it ("C:\a", "\\server\share\a")
// in its extended-length form ("\\?\C:\a", "\\?\UNC\server\share\a"), which
// is not limited to MAX_PATH and which Windows does not reinterpret.
std::wstring extended(std::wstring full) {
    if (!full.starts_with(L"\\\\")) return L"\\\\?\\" + full;
    auto share = full.substr(2);
    // A share's root names a directory only with its separator.
    if (std::count(share.begin(), share.end(), L'\\') == 1) share += L'\\';
    return L"\\\\?\\UNC\\" + share;
}

// The API text of a path that windows_path_error accepts, made absolute
// against the current directory: "." and ".." are applied lexically, as
// Windows applies them when it opens the path.
std::variant<std::wstring, std::uint32_t> api_path(std::string_view path) {
    const auto wide = platform::try_to_utf16(path);
    if (!wide) return host_error::invalid_input;
    std::wstring full(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetFullPathNameW(wide->c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length == 0) return host_error::from_win32(::GetLastError());
        if (length < full.size()) {
            full.resize(length);
            break;
        }
        full.resize(length);
    }
    const bool drive = full.size() >= 3 && full[1] == L':' && full[2] == L'\\';
    if (!drive && !full.starts_with(L"\\\\")) return host_error::invalid_input;
    return extended(std::move(full));
}

// The API text of a path from resolve_path, whose first component is the
// root ("C:" or "//server/share"). The components are used as resolved,
// without another pass through GetFullPathNameW, which would strip a trailing
// dot or space from a name the file system reported.
std::variant<std::wstring, std::uint32_t> api_path(const ResolvedPath& resolved) {
    if (resolved.components.empty()) return host_error::invalid_input;
    auto root = platform::try_to_utf16(resolved.components.front());
    if (!root) return host_error::invalid_input;
    std::wstring text;
    if (root->starts_with(L"//")) {
        std::replace(root->begin(), root->end(), L'/', L'\\');
        text = L"\\\\?\\UNC\\" + root->substr(2);
    } else {
        text = L"\\\\?\\" + *root;
    }
    for (std::size_t i = 1; i < resolved.components.size(); ++i) {
        const auto component = platform::try_to_utf16(resolved.components[i]);
        if (!component) return host_error::invalid_input;
        text += L'\\';
        text += *component;
    }
    if (resolved.components.size() == 1) text += L'\\';
    return text;
}

// Checks a path argument against the capability and returns the path to
// hand to the operating system, as extended-length API text, or an error
// code.
struct Checked {
    std::wstring path;
    std::uint32_t error = 0;
};

Checked check_path(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
                   const std::string& path, bool follow_last) {
    if (!capabilities.fs) return {{}, host_error::capability_denied};
    if (!usable_text(path)) return {{}, host_error::invalid_input};
    std::variant<std::wstring, std::uint32_t> api;
    if (prefixes.empty()) {
        // resolve_path applies the refusals of W09 itself; without prefixes
        // they are applied here.
        if (const auto refused = windows_path_error(path); refused != 0) return {{}, refused};
        api = api_path(path);
    } else {
        const auto resolved = resolve_path(path, follow_last);
        if (resolved.error != 0) return {{}, resolved.error};
        const bool allowed = std::any_of(prefixes.begin(), prefixes.end(),
                                         [&resolved](const ResolvedPath& prefix) { return resolved.within(prefix); });
        if (!allowed) return {{}, host_error::capability_denied};
        api = api_path(resolved);
    }
    if (const auto* error = std::get_if<std::uint32_t>(&api)) return {{}, *error};
    return {std::get<std::wstring>(std::move(api)), 0};
}

#else

// Checks a path argument against the capability and returns the path to
// hand to the operating system, or an error code.
struct Checked {
    std::string path;
    std::uint32_t error = 0;
};

Checked check_path(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
                   const std::string& path, bool follow_last) {
    if (!capabilities.fs) return {{}, host_error::capability_denied};
    if (!usable_text(path)) return {{}, host_error::invalid_input};
    if (prefixes.empty()) return {path, 0};
    const auto resolved = resolve_path(path, follow_last);
    if (resolved.error != 0) return {{}, resolved.error};
    for (const auto& prefix : prefixes) {
        if (resolved.within(prefix)) return {resolved.text(), 0};
    }
    return {{}, host_error::capability_denied};
}

#endif

} // namespace

#ifdef _WIN32

namespace {

// Files and directories are shared for reading, writing and deletion, so
// another program can rename or remove them while they are open, as on POSIX
// (W16).
constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

#ifndef FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE
#define FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE 0x00000040
#endif
#ifndef FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE
#define FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE 0x00000010
#endif
#ifndef FILE_SUPPORTS_POSIX_UNLINK_RENAME
#define FILE_SUPPORTS_POSIX_UNLINK_RENAME 0x00000400
#endif

// Whether a POSIX-semantics rename or delete failed because the volume (FAT,
// exFAT, some network file systems) or the system does not support it.
bool no_posix_semantics(DWORD error) {
    return error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION;
}

// Whether attributes describe a directory itself, not a link or junction to
// one, which is removed and renamed as an entry of its own.
bool real_directory(DWORD attributes) {
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

// The length of the root of an extended-length path: "\\?\C:\" or
// "\\?\UNC\server\share\".
std::size_t root_length(std::wstring_view path) {
    if (!path.starts_with(L"\\\\?\\UNC\\")) return std::min<std::size_t>(path.size(), 7);
    auto at = path.find(L'\\', 8);
    if (at == std::wstring_view::npos) return path.size();
    at = path.find(L'\\', at + 1);
    return at == std::wstring_view::npos ? path.size() : at + 1;
}

// The code for ERROR_PATH_NOT_FOUND, which Windows reports both for a missing
// directory on the path and for a component that is a file: POSIX answers
// ENOTDIR for the second, so the nearest existing ancestor decides.
std::uint32_t missing_entry(const std::wstring& path) {
    const auto root = root_length(path);
    std::wstring_view rest(path);
    while (rest.size() > root) {
        while (rest.size() > root && rest.back() == L'\\') rest.remove_suffix(1);
        const auto cut = rest.rfind(L'\\');
        if (cut == std::wstring_view::npos) break;
        rest = rest.substr(0, std::max(cut, root));
        const std::wstring ancestor(rest);
        const DWORD attributes = ::GetFileAttributesW(ancestor.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? host_error::not_found : host_error::not_a_directory;
        }
        const DWORD error = ::GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) break;
    }
    return host_error::not_found;
}

// The code for a failure to open, create or list `path`.
std::uint32_t path_failure(const std::wstring& path, DWORD error) {
    if (error == ERROR_PATH_NOT_FOUND) return missing_entry(path);
    return host_error::from_win32(error);
}

// The pattern that lists every entry of a directory.
std::wstring every_entry(const std::wstring& directory) {
    return directory.ends_with(L'\\') ? directory + L'*' : directory + L"\\*";
}

// Whether a directory has an entry other than "." and "..".
bool has_entries(const std::wstring& directory) {
    WIN32_FIND_DATAW entry{};
    const HANDLE find = ::FindFirstFileExW(every_entry(directory).c_str(), FindExInfoBasic, &entry,
                                           FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        const std::wstring_view name(entry.cFileName);
        found = name != L"." && name != L"..";
    } while (!found && ::FindNextFileW(find, &entry));
    ::FindClose(find);
    return found;
}

// Opens a directory entry itself, not what a link or junction leads to, for
// removing or renaming it.
platform::UniqueFile open_entry(const std::wstring& path) {
    return platform::UniqueFile(::CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, share_all,
                                              nullptr, OPEN_EXISTING,
                                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
}

// neura.fs.remove. POSIX semantics where the volume supports them: the name
// goes at once, also for a file that is open or read-only, as unlink(2)
// removes it. Elsewhere DeleteFileW or RemoveDirectoryW, with the read-only
// attribute cleared first (and restored if the removal fails).
std::uint32_t remove_entry(const std::wstring& path, bool directory) {
    auto entry = open_entry(path);
    if (!entry) return path_failure(path, ::GetLastError());
    FILE_BASIC_INFO basic{};
    if (!::GetFileInformationByHandleEx(entry.get(), FileBasicInfo, &basic, sizeof basic)) {
        return host_error::from_win32(::GetLastError());
    }
    const DWORD attributes = basic.FileAttributes;
    // A link or junction, even to a directory, is removed as a file, as
    // unlink(2) removes a symbolic link and rmdir(2) refuses one.
    if (directory && !real_directory(attributes)) return host_error::not_a_directory;
    if (!directory && real_directory(attributes)) return host_error::is_a_directory;
    FILE_DISPOSITION_INFO_EX disposition{};
    disposition.Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                        FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
    if (::SetFileInformationByHandle(entry.get(), FileDispositionInfoEx, &disposition, sizeof disposition)) return 0;
    const DWORD error = ::GetLastError();
    if (!no_posix_semantics(error)) return host_error::from_win32(error);
    entry.close();
    const bool read_only = (attributes & FILE_ATTRIBUTE_READONLY) != 0;
    if (read_only && !::SetFileAttributesW(path.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY))) {
        return host_error::from_win32(::GetLastError());
    }
    const bool removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? ::RemoveDirectoryW(path.c_str()) != 0
                                                                       : ::DeleteFileW(path.c_str()) != 0;
    if (removed) return 0;
    const DWORD failure = ::GetLastError();
    if (read_only) ::SetFileAttributesW(path.c_str(), attributes);
    return host_error::from_win32(failure);
}

// The code for a failed rename onto `to`.
std::uint32_t rename_failure(const std::wstring& to, DWORD error, bool source_directory) {
    if (error == ERROR_ACCESS_DENIED && !source_directory) {
        // POSIX answers EISDIR for a file renamed onto a directory; Windows
        // answers ERROR_ACCESS_DENIED.
        const DWORD target = ::GetFileAttributesW(to.c_str());
        if (target != INVALID_FILE_ATTRIBUTES && real_directory(target)) return host_error::is_a_directory;
    }
    // As for EEXIST on POSIX: the target is a directory that is not empty.
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return host_error::directory_not_empty;
    return path_failure(to, error);
}

// Whether the volume of an open entry supports POSIX-semantics rename and
// delete.
bool posix_volume(HANDLE handle) {
    DWORD flags = 0;
    return ::GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
           (flags & FILE_SUPPORTS_POSIX_UNLINK_RENAME) != 0;
}

// The final path of an open entry ("\\?\C:\..." or "\\?\UNC\..."), or
// nothing when the file system cannot report one.
std::optional<std::wstring> final_path(HANDLE handle) {
    std::wstring text(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetFinalPathNameByHandleW(handle, text.data(), static_cast<DWORD>(text.size()),
                                                         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) return std::nullopt;
        if (length < text.size()) {
            text.resize(length);
            return text;
        }
        text.resize(length);
    }
}

// The final path that `path` names, with links and junctions on the way to
// it followed but not one at its end: the final path of the entry itself when
// it can be opened, otherwise that of its parent directory with the last name
// appended. Nothing when neither can be opened.
std::optional<std::wstring> final_location(const std::wstring& path) {
    const platform::UniqueFile entry(::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr,
                                                   OPEN_EXISTING,
                                                   FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (entry) return final_path(entry.get());
    const auto root = root_length(path);
    std::wstring_view rest(path);
    while (rest.size() > root && rest.back() == L'\\') rest.remove_suffix(1);
    if (rest.size() <= root) return std::nullopt;
    const auto cut = rest.rfind(L'\\');
    if (cut == std::wstring_view::npos || cut + 1 < root) return std::nullopt;
    const std::wstring parent(rest.substr(0, std::max(cut, root)));
    const platform::UniqueFile directory(::CreateFileW(parent.c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr,
                                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!directory) return std::nullopt;
    auto location = final_path(directory.get());
    if (!location) return std::nullopt;
    if (!location->ends_with(L'\\')) *location += L'\\';
    *location += rest.substr(cut + 1);
    return location;
}

// Whether renaming the open directory `source` to `to` would move it into its
// own subtree, which rename(2) refuses with EINVAL. Windows answers
// ERROR_INVALID_PARAMETER or ERROR_ACCESS_DENIED instead, depending on how
// deep `to` lies, so the paths are compared after links and junctions are
// followed, ordinally without case as within() compares them. Renaming the
// directory to itself, or to another case of its own name, is not a move
// into its subtree.
bool into_own_subtree(HANDLE source, const std::wstring& to) {
    const auto directory = final_path(source);
    if (!directory || directory->ends_with(L'\\')) return false;
    const auto target = final_location(to);
    if (!target || target->size() <= directory->size() || (*target)[directory->size()] != L'\\') return false;
    return ::CompareStringOrdinal(directory->data(), static_cast<int>(directory->size()), target->data(),
                                  static_cast<int>(directory->size()), TRUE) == CSTR_EQUAL;
}

// neura.fs.rename. A POSIX-semantics rename where the volume supports it,
// which replaces a file (also one that is open or read-only) or an empty
// directory atomically, as rename(2) does. Elsewhere MoveFileExW, which
// cannot replace a directory: an empty target directory then answers
// unsupported. A directory renamed into its own subtree answers
// invalid_input, as EINVAL.
std::uint32_t rename_entry(const std::wstring& from, const std::wstring& to) {
    auto source = open_entry(from);
    if (!source) return path_failure(from, ::GetLastError());
    FILE_BASIC_INFO basic{};
    if (!::GetFileInformationByHandleEx(source.get(), FileBasicInfo, &basic, sizeof basic)) {
        return host_error::from_win32(::GetLastError());
    }
    const bool source_directory = real_directory(basic.FileAttributes);
    if (source_directory && into_own_subtree(source.get(), to)) return host_error::invalid_input;
    const std::size_t bytes = to.size() * sizeof(wchar_t);
    std::vector<std::byte> buffer(offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t));
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS |
                  FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(info->FileName, to.c_str(), bytes + sizeof(wchar_t));
    const auto size = static_cast<DWORD>(buffer.size());
    if (::SetFileInformationByHandle(source.get(), FileRenameInfoEx, info, size)) return 0;
    DWORD error = ::GetLastError();
    if (error == ERROR_INVALID_PARAMETER) {
        // Windows before version 1809 does not know IGNORE_READONLY_ATTRIBUTE.
        info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        if (::SetFileInformationByHandle(source.get(), FileRenameInfoEx, info, size)) return 0;
        error = ::GetLastError();
    }
    // Only a volume that lacks POSIX-semantics rename falls back; on one that
    // has it, the error is the answer.
    if (!no_posix_semantics(error) || posix_volume(source.get())) return rename_failure(to, error, source_directory);
    source.close();
    // MoveFileExW would replace a file with a directory, which rename(2)
    // refuses, so the kinds are compared first.
    const DWORD target = ::GetFileAttributesW(to.c_str());
    if (target != INVALID_FILE_ATTRIBUTES) {
        const bool target_directory = real_directory(target);
        if (source_directory && !target_directory) return host_error::not_a_directory;
        if (!source_directory && target_directory) return host_error::is_a_directory;
        if (source_directory) return has_entries(to) ? host_error::directory_not_empty : host_error::unsupported;
    }
    if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) return 0;
    return rename_failure(to, ::GetLastError(), source_directory);
}

// The modification time as seconds and nanoseconds since 1970-01-01, from a
// FILETIME count of 100-nanosecond intervals since 1601-01-01.
std::pair<std::int64_t, std::int32_t> unix_time(std::int64_t filetime) {
    constexpr std::int64_t ticks_per_second = 10'000'000;
    constexpr std::int64_t epoch = 116'444'736'000'000'000; // 1970-01-01 in FILETIME ticks
    const std::int64_t ticks = filetime - epoch;
    std::int64_t seconds = ticks / ticks_per_second;
    std::int64_t rest = ticks % ticks_per_second;
    if (rest < 0) {
        rest += ticks_per_second;
        --seconds;
    }
    return {seconds, static_cast<std::int32_t>(rest * 100)};
}

HostCompletion win32_failure() { return fail(host_error::from_win32(::GetLastError())); }

HostCompletion completion(std::uint32_t error) { return error == 0 ? HostCompletion::ok() : fail(error); }

} // namespace

void add_fs_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities,
                     const std::vector<ResolvedPath>& prefixes) {
    auto* files = &tables;
    const auto* granted = &capabilities;
    const auto* allowed = &prefixes;

    add(signature("neura.fs.open", {text(), int32()}, int64()), [=](const HostRequest& request) {
        const auto mode = as_int32(request.arguments.at(1));
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, mode != CreateNew);
        if (checked.error != 0) return fail(checked.error);
        DWORD access = 0;
        DWORD creation = OPEN_EXISTING;
        bool readable = false;
        bool writable = false;
        switch (mode) {
        case Read:
            access = GENERIC_READ;
            readable = true;
            break;
        case Write:
            // OPEN_ALWAYS and then truncation, as O_TRUNC truncates: the
            // file keeps its attributes, and a hidden or system file is not
            // refused as CREATE_ALWAYS refuses it.
            access = GENERIC_WRITE;
            creation = OPEN_ALWAYS;
            writable = true;
            break;
        case Append:
            // Without FILE_WRITE_DATA every write goes to the end, as with
            // O_APPEND.
            access = FILE_APPEND_DATA;
            creation = OPEN_ALWAYS;
            writable = true;
            break;
        case ReadWrite:
            access = GENERIC_READ | GENERIC_WRITE;
            readable = writable = true;
            break;
        case CreateNew:
            access = GENERIC_READ | GENERIC_WRITE;
            creation = CREATE_NEW;
            readable = writable = true;
            break;
        default:
            return fail(host_error::invalid_input);
        }
        if (files->files.size() >= max_open_ids) return fail(host_error::other);
        // No security attributes: the handle is not inherited by child
        // processes, as O_CLOEXEC keeps a descriptor from them.
        platform::UniqueFile file(::CreateFileW(checked.path.c_str(), access, share_all, nullptr, creation,
                                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_ACCESS_DENIED) {
                // A directory cannot be opened as a file, and CreateFileW
                // says so with ERROR_ACCESS_DENIED.
                const DWORD attributes = ::GetFileAttributesW(checked.path.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                    return fail(mode == CreateNew ? host_error::already_exists : host_error::is_a_directory);
                }
            }
            return fail(path_failure(checked.path, error));
        }
        if (mode == Write) {
            FILE_END_OF_FILE_INFO end{};
            if (!::SetFileInformationByHandle(file.get(), FileEndOfFileInfo, &end, sizeof end)) return win32_failure();
        }
        const auto id = files->next_file++;
        files->files[id] = {file.release(), readable, writable};
        return HostCompletion::ok(int64_value(id));
    });

    add(signature("neura.fs.read", {int64(), int32()}, byte_list()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto max = as_int32(request.arguments.at(1));
        if (found == files->files.end() || max < 1) return fail(host_error::invalid_input);
        if (!found->second.readable) return fail(host_error::permission_denied);
        std::string buffer(std::min(static_cast<std::size_t>(max), max_read_bytes), '\0');
        DWORD count = 0;
        if (!::ReadFile(found->second.descriptor, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
            const DWORD error = ::GetLastError();
            // The end of the file, or of a pipe whose writer is gone, reads
            // as no bytes, as on POSIX.
            if (error != ERROR_HANDLE_EOF && error != ERROR_BROKEN_PIPE) return fail(host_error::from_win32(error));
            count = 0;
        }
        buffer.resize(count);
        return HostCompletion::ok(bytes_value(std::move(buffer)));
    });

    add(signature("neura.fs.write", {int64(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto& bytes = request.arguments.at(1).bytes;
        if (found == files->files.end()) return fail(host_error::invalid_input);
        if (!found->second.writable) return fail(host_error::permission_denied);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), INT32_MAX));
        DWORD count = 0;
        if (!::WriteFile(found->second.descriptor, bytes.data(), size, &count, nullptr)) return win32_failure();
        return HostCompletion::ok(int32_value(static_cast<std::int32_t>(count)));
    });

    add(signature("neura.fs.seek", {int64(), int64(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto offset = as_int64(request.arguments.at(1));
        const auto origin = as_int32(request.arguments.at(2));
        if (found == files->files.end() || origin < 0 || origin > 2) return fail(host_error::invalid_input);
        const DWORD method = origin == 0 ? FILE_BEGIN : origin == 1 ? FILE_CURRENT : FILE_END;
        LARGE_INTEGER distance{};
        distance.QuadPart = offset;
        LARGE_INTEGER position{};
        // A position before the start fails with ERROR_NEGATIVE_SEEK, which
        // answers invalid_input as EINVAL does.
        if (!::SetFilePointerEx(found->second.descriptor, distance, &position, method)) return win32_failure();
        return HostCompletion::ok(int64_value(static_cast<std::int64_t>(position.QuadPart)));
    });

    add(signature("neura.fs.close", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        if (found == files->files.end()) return fail(host_error::invalid_input);
        const HANDLE handle = found->second.descriptor;
        // The ID is released even when close reports an error.
        files->files.erase(found);
        if (!::CloseHandle(handle)) return win32_failure();
        return HostCompletion::ok();
    });

    add(signature("neura.fs.stat", {text(), boolean()}, tuple({int32(), int64(), int64(), int32()})),
        [=](const HostRequest& request) {
            const bool follow = as_bool(request.arguments.at(1));
            const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, follow);
            if (checked.error != 0) return fail(checked.error);
            DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
            if (!follow) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
            const platform::UniqueFile entry(::CreateFileW(checked.path.c_str(), FILE_READ_ATTRIBUTES, share_all,
                                                           nullptr, OPEN_EXISTING, flags, nullptr));
            if (!entry) return fail(path_failure(checked.path, ::GetLastError()));
            FILE_BASIC_INFO basic{};
            FILE_STANDARD_INFO standard{};
            if (!::GetFileInformationByHandleEx(entry.get(), FileBasicInfo, &basic, sizeof basic) ||
                !::GetFileInformationByHandleEx(entry.get(), FileStandardInfo, &standard, sizeof standard)) {
                return win32_failure();
            }
            // Without following, every reparse point (a symbolic link, a
            // junction or any other) is Other (W16).
            FileKind kind = OtherKind;
            if (follow || (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                if (standard.Directory) {
                    kind = Directory;
                } else if (::GetFileType(entry.get()) == FILE_TYPE_DISK) {
                    kind = RegularFile;
                }
            }
            const auto [seconds, nanos] = unix_time(basic.LastWriteTime.QuadPart);
            return HostCompletion::ok(tuple_value({int32_value(kind),
                                                   int64_value(static_cast<std::int64_t>(standard.EndOfFile.QuadPart)),
                                                   int64_value(seconds), int32_value(nanos)}));
        });

    add(signature("neura.fs.list", {text()}, text_list()), [=](const HostRequest& request) {
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, true);
        if (checked.error != 0) return fail(checked.error);
        const auto pattern = every_entry(checked.path);
        WIN32_FIND_DATAW entry{};
        const HANDLE find = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch,
                                               nullptr, FIND_FIRST_EX_LARGE_FETCH);
        std::vector<std::string> names;
        bool invalid = false;
        if (find == INVALID_HANDLE_VALUE) {
            const DWORD error = ::GetLastError();
            // A drive's root may have no entries at all, not even "." and "..".
            if (error != ERROR_FILE_NOT_FOUND) return fail(path_failure(pattern, error));
        } else {
            do {
                const std::wstring_view name(entry.cFileName);
                if (name == L"." || name == L"..") continue;
                auto text = platform::try_to_utf8(name);
                if (!text) {
                    invalid = true;
                    continue;
                }
                names.push_back(std::move(*text));
            } while (::FindNextFileW(find, &entry));
            const DWORD error = ::GetLastError();
            ::FindClose(find);
            if (error != ERROR_NO_MORE_FILES) return fail(host_error::from_win32(error));
        }
        // A name that is not valid UTF-16 (an unpaired surrogate) cannot
        // become a Neura string (W07).
        if (invalid) return fail(host_error::unsupported);
        // std::string compares as unsigned bytes, which is UTF-8 byte order.
        std::sort(names.begin(), names.end());
        HostValue list;
        for (auto& name : names) list.elements.push_back(bytes_value(std::move(name)));
        return HostCompletion::ok(std::move(list));
    });

    add(signature("neura.fs.make_directory", {text()}, void_type()), [=](const HostRequest& request) {
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (checked.error != 0) return fail(checked.error);
        if (!::CreateDirectoryW(checked.path.c_str(), nullptr)) return fail(path_failure(checked.path, ::GetLastError()));
        return HostCompletion::ok();
    });

    add(signature("neura.fs.remove", {text(), boolean()}, void_type()), [=](const HostRequest& request) {
        const bool directory = as_bool(request.arguments.at(1));
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (checked.error != 0) return fail(checked.error);
        return completion(remove_entry(checked.path, directory));
    });

    add(signature("neura.fs.rename", {text(), text()}, void_type()), [=](const HostRequest& request) {
        const auto from = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (from.error != 0) return fail(from.error);
        const auto to = check_path(*granted, *allowed, request.arguments.at(1).bytes, false);
        if (to.error != 0) return fail(to.error);
        return completion(rename_entry(from.path, to.path));
    });
}

void close_files(StandardServices::Tables& tables) {
    for (const auto& [id, file] : tables.files) ::CloseHandle(file.descriptor);
    tables.files.clear();
    tables.next_file = 1;
}

#else

namespace {

int retry_open(const char* path, int flags) {
    for (;;) {
        const int descriptor = ::open(path, flags | O_CLOEXEC, 0666);
        if (descriptor >= 0 || errno != EINTR) return descriptor;
    }
}

HostCompletion errno_failure() { return fail(host_error::from_errno(errno)); }

} // namespace

void add_fs_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities,
                     const std::vector<ResolvedPath>& prefixes) {
    auto* files = &tables;
    const auto* granted = &capabilities;
    const auto* allowed = &prefixes;

    add(signature("neura.fs.open", {text(), int32()}, int64()), [=](const HostRequest& request) {
        const auto mode = as_int32(request.arguments.at(1));
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, mode != CreateNew);
        if (checked.error != 0) return fail(checked.error);
        int flags = 0;
        bool readable = false;
        bool writable = false;
        switch (mode) {
        case Read:
            flags = O_RDONLY;
            readable = true;
            break;
        case Write:
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            writable = true;
            break;
        case Append:
            flags = O_WRONLY | O_CREAT | O_APPEND;
            writable = true;
            break;
        case ReadWrite:
            flags = O_RDWR;
            readable = writable = true;
            break;
        case CreateNew:
            flags = O_RDWR | O_CREAT | O_EXCL;
            readable = writable = true;
            break;
        default:
            return fail(host_error::invalid_input);
        }
        if (files->files.size() >= max_open_ids) return fail(host_error::other);
        const int descriptor = retry_open(checked.path.c_str(), flags);
        if (descriptor < 0) return errno_failure();
        struct stat info {};
        if (::fstat(descriptor, &info) == 0 && S_ISDIR(info.st_mode)) {
            ::close(descriptor);
            return fail(host_error::is_a_directory);
        }
        const auto id = files->next_file++;
        files->files[id] = {descriptor, readable, writable};
        return HostCompletion::ok(int64_value(id));
    });

    add(signature("neura.fs.read", {int64(), int32()}, byte_list()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto max = as_int32(request.arguments.at(1));
        if (found == files->files.end() || max < 1) return fail(host_error::invalid_input);
        if (!found->second.readable) return fail(host_error::permission_denied);
        std::string buffer(std::min(static_cast<std::size_t>(max), max_read_bytes), '\0');
        for (;;) {
            const auto count = ::read(found->second.descriptor, buffer.data(), buffer.size());
            if (count >= 0) {
                buffer.resize(static_cast<std::size_t>(count));
                return HostCompletion::ok(bytes_value(std::move(buffer)));
            }
            if (errno != EINTR) return errno_failure();
        }
    });

    add(signature("neura.fs.write", {int64(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto& bytes = request.arguments.at(1).bytes;
        if (found == files->files.end()) return fail(host_error::invalid_input);
        if (!found->second.writable) return fail(host_error::permission_denied);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = std::min<std::size_t>(bytes.size(), INT32_MAX);
        for (;;) {
            const auto count = ::write(found->second.descriptor, bytes.data(), size);
            if (count >= 0) return HostCompletion::ok(int32_value(static_cast<std::int32_t>(count)));
            if (errno != EINTR) return errno_failure();
        }
    });

    add(signature("neura.fs.seek", {int64(), int64(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        const auto offset = as_int64(request.arguments.at(1));
        const auto origin = as_int32(request.arguments.at(2));
        if (found == files->files.end() || origin < 0 || origin > 2) return fail(host_error::invalid_input);
        const int whence = origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END;
        const auto position = ::lseek(found->second.descriptor, static_cast<off_t>(offset), whence);
        if (position < 0) return errno_failure();
        return HostCompletion::ok(int64_value(static_cast<std::int64_t>(position)));
    });

    add(signature("neura.fs.close", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->fs) return denied();
        const auto found = files->files.find(as_int64(request.arguments.at(0)));
        if (found == files->files.end()) return fail(host_error::invalid_input);
        const int descriptor = found->second.descriptor;
        // The ID is released even when close reports an error.
        files->files.erase(found);
        if (::close(descriptor) != 0 && errno != EINTR) return errno_failure();
        return HostCompletion::ok();
    });

    add(signature("neura.fs.stat", {text(), boolean()}, tuple({int32(), int64(), int64(), int32()})),
        [=](const HostRequest& request) {
            const bool follow = as_bool(request.arguments.at(1));
            const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, follow);
            if (checked.error != 0) return fail(checked.error);
            struct stat info {};
            const int status = follow ? ::stat(checked.path.c_str(), &info) : ::lstat(checked.path.c_str(), &info);
            if (status != 0) return errno_failure();
            const auto kind = S_ISREG(info.st_mode) ? RegularFile : S_ISDIR(info.st_mode) ? Directory : OtherKind;
#ifdef __APPLE__
            const auto modified = info.st_mtimespec;
#else
            const auto modified = info.st_mtim;
#endif
            return HostCompletion::ok(tuple_value({int32_value(kind), int64_value(static_cast<std::int64_t>(info.st_size)),
                                                   int64_value(static_cast<std::int64_t>(modified.tv_sec)),
                                                   int32_value(static_cast<std::int32_t>(modified.tv_nsec))}));
        });

    add(signature("neura.fs.list", {text()}, text_list()), [=](const HostRequest& request) {
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, true);
        if (checked.error != 0) return fail(checked.error);
        DIR* directory = ::opendir(checked.path.c_str());
        if (directory == nullptr) return errno_failure();
        std::vector<std::string> names;
        bool invalid = false;
        for (;;) {
            errno = 0;
            const auto* entry = ::readdir(directory);
            if (entry == nullptr) break;
            const std::string name(entry->d_name);
            if (name == "." || name == "..") continue;
            if (!nvr::valid_utf8(name)) invalid = true;
            names.push_back(name);
        }
        const int error = errno;
        ::closedir(directory);
        if (error != 0) return fail(host_error::from_errno(error));
        // A name that is not UTF-8 cannot become a Neura string.
        if (invalid) return fail(host_error::unsupported);
        // std::string compares as unsigned bytes, which is UTF-8 byte order.
        std::sort(names.begin(), names.end());
        HostValue list;
        for (auto& name : names) list.elements.push_back(bytes_value(std::move(name)));
        return HostCompletion::ok(std::move(list));
    });

    add(signature("neura.fs.make_directory", {text()}, void_type()), [=](const HostRequest& request) {
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (checked.error != 0) return fail(checked.error);
        if (::mkdir(checked.path.c_str(), 0777) != 0) return errno_failure();
        return HostCompletion::ok();
    });

    add(signature("neura.fs.remove", {text(), boolean()}, void_type()), [=](const HostRequest& request) {
        const bool directory = as_bool(request.arguments.at(1));
        const auto checked = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (checked.error != 0) return fail(checked.error);
        if (directory) {
            if (::rmdir(checked.path.c_str()) == 0) return HostCompletion::ok();
            // Some systems report a non-empty directory as EEXIST.
            return fail(errno == EEXIST ? host_error::directory_not_empty : host_error::from_errno(errno));
        }
        if (::unlink(checked.path.c_str()) == 0) return HostCompletion::ok();
        const int error = errno;
        struct stat info {};
        if (error == EPERM && ::lstat(checked.path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
            return fail(host_error::is_a_directory);
        }
        return fail(host_error::from_errno(error));
    });

    add(signature("neura.fs.rename", {text(), text()}, void_type()), [=](const HostRequest& request) {
        const auto from = check_path(*granted, *allowed, request.arguments.at(0).bytes, false);
        if (from.error != 0) return fail(from.error);
        const auto to = check_path(*granted, *allowed, request.arguments.at(1).bytes, false);
        if (to.error != 0) return fail(to.error);
        if (::rename(from.path.c_str(), to.path.c_str()) == 0) return HostCompletion::ok();
        return fail(errno == EEXIST ? host_error::directory_not_empty : host_error::from_errno(errno));
    });
}

void close_files(StandardServices::Tables& tables) {
    for (const auto& [id, file] : tables.files) ::close(file.descriptor);
    tables.files.clear();
    tables.next_file = 1;
}

#endif

} // namespace neura::nvm::provider

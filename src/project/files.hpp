#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#include "platform/windows.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <optional>
#endif

namespace neura::project::detail {
namespace fs = std::filesystem;

inline std::string path_text(const fs::path& path) {
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

inline fs::path path_from_text(std::string_view value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}

struct ReadFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#ifndef _WIN32

class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() { if (value_ >= 0) ::close(value_); }
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        std::swap(value_, other.value_);
        return *this;
    }
    int get() const { return value_; }
    int release() { return std::exchange(value_, -1); }
private:
    int value_;
};

constexpr int directory_flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;

inline FileDescriptor subdirectory(FileDescriptor current, std::string_view path) {
    while (!path.empty()) {
        const auto end = path.find('/');
        const auto name = std::string(path.substr(0, end));
        FileDescriptor next(::openat(current.get(), name.c_str(), directory_flags));
        if (next.get() < 0) throw ReadFailure("cannot open directory '" + name + "' without following links");
        current = std::move(next);
        if (end == std::string_view::npos) break;
        path.remove_prefix(end + 1);
    }
    return current;
}

inline FileDescriptor open_root(const fs::path& path) {
    FileDescriptor root(::open(path.root_path().c_str(), directory_flags));
    if (root.get() < 0) throw ReadFailure("cannot open the filesystem root");
    return subdirectory(std::move(root), path_text(path.relative_path()));
}

inline bool unchanged(const struct stat& before, const struct stat& after) {
#ifdef __APPLE__
    const auto before_mtime = before.st_mtimespec, after_mtime = after.st_mtimespec;
    const auto before_ctime = before.st_ctimespec, after_ctime = after.st_ctimespec;
#else
    const auto before_mtime = before.st_mtim, after_mtime = after.st_mtim;
    const auto before_ctime = before.st_ctim, after_ctime = after.st_ctim;
#endif
    return before.st_size == after.st_size &&
           before_mtime.tv_sec == after_mtime.tv_sec && before_mtime.tv_nsec == after_mtime.tv_nsec &&
           before_ctime.tv_sec == after_ctime.tv_sec && before_ctime.tv_nsec == after_ctime.tv_nsec;
}

#else

// The Windows counterparts of the descriptors above. Windows has no openat,
// so a reader opens each file by its full path without following a reparse
// point at it (FILE_FLAG_OPEN_REPARSE_POINT), and then checks where the
// system resolved the handle (GetFinalPathNameByHandleW): a link, junction or
// other reparse point anywhere on the way gives another final path. Files
// are opened with FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
// (W16), so a reader compares a file's state before and after it reads.

// Owns a file or directory HANDLE.
class FileHandle {
public:
    FileHandle() noexcept = default;
    explicit FileHandle(HANDLE value) noexcept : value_(value) {}
    ~FileHandle() { if (valid()) ::CloseHandle(value_); }
    FileHandle(FileHandle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    FileHandle& operator=(FileHandle&& other) noexcept {
        std::swap(value_, other.value_);
        return *this;
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    bool valid() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};

// A reparse point that names another file (a name surrogate: a symbolic link
// or a junction) is a link. Any other (a cloud-file placeholder, a
// deduplicated file) is neither a link nor a regular file to these readers.
enum class Reparse { none, link, other };

inline Reparse reparse_of(DWORD attributes, DWORD tag) {
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) return Reparse::none;
    return IsReparseTagNameSurrogate(tag) ? Reparse::link : Reparse::other;
}

// The reason a reader gives when it refuses a Reparse::other. A OneDrive file
// keeps its cloud reparse point even when its contents are on the disk, so
// this names that case.
inline constexpr const char* reparse_limitation =
    "it is a reparse point that is not a link (such as a OneDrive cloud file), which Neura 0.3.0 does not read on "
    "Windows";

// What an open file is, and what a reader compares before and after it reads
// (the POSIX readers compare size, mtime and ctime).
struct FileState {
    DWORD attributes = 0;
    DWORD tag = 0;
    std::int64_t last_write = 0;
    std::int64_t change = 0;
    std::uint64_t size = 0;

    bool directory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
    Reparse reparse() const { return reparse_of(attributes, tag); }
    bool operator==(const FileState&) const = default;
};

inline std::optional<FileState> state_of(HANDLE file) {
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!::GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof basic) ||
        !::GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof standard) ||
        !::GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof tag) || standard.EndOfFile.QuadPart < 0)
        return std::nullopt;
    return FileState{basic.FileAttributes, tag.ReparseTag, basic.LastWriteTime.QuadPart, basic.ChangeTime.QuadPart,
                     static_cast<std::uint64_t>(standard.EndOfFile.QuadPart)};
}

// Opens `path` itself for reading, never what a reparse point there names; a
// directory too (FILE_FLAG_BACKUP_SEMANTICS). Invalid when it cannot be
// opened.
inline FileHandle open_no_follow(const fs::path& path, DWORD access = GENERIC_READ) {
    return FileHandle(::CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
}

// What is at `path` itself; nullopt when it cannot be opened.
inline std::optional<Reparse> reparse_at(const fs::path& path) {
    const auto file = open_no_follow(path, FILE_READ_ATTRIBUTES);
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!file.valid() || !::GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &tag, sizeof tag))
        return std::nullopt;
    return reparse_of(tag.FileAttributes, tag.ReparseTag);
}

// The path the system resolved an open handle to, in the form
// std::filesystem::canonical gives (no \\?\ before a drive letter, \\server
// for \\?\UNC\server); nullopt when it has no such name.
inline std::optional<fs::path> final_path(HANDLE file) {
    std::wstring text(MAX_PATH, L'\0');
    for (;;) {
        const auto length = ::GetFinalPathNameByHandleW(file, text.data(), static_cast<DWORD>(text.size()),
                                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) return std::nullopt;
        if (length < text.size()) {
            text.resize(length);
            break;
        }
        text.resize(length);
    }
    const std::wstring_view view(text);
    if (view.starts_with(L"\\\\?\\UNC\\")) {
        text.erase(2, 6);
    } else if (view.size() >= 6 && view.starts_with(L"\\\\?\\") && view[5] == L':' &&
               ((view[4] >= L'A' && view[4] <= L'Z') || (view[4] >= L'a' && view[4] <= L'z'))) {
        text.erase(0, 4);
    }
    return fs::path(std::move(text));
}

// Whether an open file is at `expected`, a path in the form final_path gives.
inline bool opened_at(HANDLE file, const fs::path& expected) {
    const auto found = final_path(file);
    return found && *found == expected;
}

// A file's identity: the volume and the 128-bit file id (FileIdInfo), or
// the 64-bit index where the volume has no 128-bit ids.
struct FileIdentity {
    std::uint64_t volume = 0;
    std::array<std::uint8_t, 16> id{};
    auto operator<=>(const FileIdentity&) const = default;
};

inline std::optional<FileIdentity> identity_of(HANDLE file) {
    FileIdentity identity;
    FILE_ID_INFO info{};
    if (::GetFileInformationByHandleEx(file, FileIdInfo, &info, sizeof info)) {
        identity.volume = info.VolumeSerialNumber;
        static_assert(sizeof info.FileId.Identifier == 16);
        std::memcpy(identity.id.data(), info.FileId.Identifier, identity.id.size());
        return identity;
    }
    BY_HANDLE_FILE_INFORMATION basic{};
    if (!::GetFileInformationByHandle(file, &basic)) return std::nullopt;
    identity.volume = basic.dwVolumeSerialNumber;
    const std::uint64_t index = (std::uint64_t{basic.nFileIndexHigh} << 32) | basic.nFileIndexLow;
    std::memcpy(identity.id.data(), &index, sizeof index);
    return identity;
}

#endif

} // namespace neura::project::detail

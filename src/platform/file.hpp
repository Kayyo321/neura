#pragma once

// Files: owned handles, locks, atomic replacement, link-aware removal and
// opening, private directories and unique names.
//
// Windows handles are opened with FILE_SHARE_READ | FILE_SHARE_WRITE |
// FILE_SHARE_DELETE and are not inheritable (W16), so another Neura process
// can rename or remove a file that is open, as on POSIX. POSIX descriptors are
// opened with O_CLOEXEC.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace neura::platform {

#ifdef _WIN32
using native_file = void*; // HANDLE
#else
using native_file = int;
#endif

// INVALID_HANDLE_VALUE on Windows, -1 elsewhere. (A null HANDLE is also
// treated as invalid by UniqueFile.)
native_file invalid_file() noexcept;

// Owns a file, pipe or console handle and closes it on destruction.
class UniqueFile {
public:
    UniqueFile() noexcept;
    explicit UniqueFile(native_file file) noexcept;
    UniqueFile(UniqueFile&& other) noexcept;
    UniqueFile& operator=(UniqueFile&& other) noexcept;
    UniqueFile(const UniqueFile&) = delete;
    UniqueFile& operator=(const UniqueFile&) = delete;
    ~UniqueFile();

    bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }
    native_file get() const noexcept { return file_; }
    native_file release() noexcept;
    void reset(native_file file) noexcept;
    void close() noexcept;

private:
    native_file file_;
};

// Reads at most `size` bytes. Returns the count, 0 at end of input (a broken
// pipe on Windows counts as end of input) or -1 with `error` set.
std::ptrdiff_t read_some(native_file file, void* buffer, std::size_t size, std::error_code& error);

// Writes all bytes, retrying partial writes. Returns false with `error` set.
bool write_all(native_file file, const void* data, std::size_t size, std::error_code& error);

// Gives up ownership of `file` to a C stream opened with `mode` ("rb",
// "wb", ...). Returns nullptr with `error` set and closes the file on failure.
std::FILE* open_c_stream(UniqueFile&& file, const char* mode, std::error_code& error);

// ---------------------------------------------------------------------------
// Opening without following links.

enum class OpenAccess { read, write, read_write, append };

enum class OpenDisposition {
    open_existing,      // fail if missing
    open_or_create,     // create if missing, keep contents
    create_or_truncate, // create if missing, truncate if present
    create_new,         // fail if present (O_EXCL, CREATE_NEW)
};

// Opens a file and refuses a link at the final component: O_NOFOLLOW on
// POSIX; FILE_FLAG_OPEN_REPARSE_POINT on Windows, then a refusal when the
// opened file has FILE_ATTRIBUTE_REPARSE_POINT (symbolic links, junctions and
// every other reparse point). A refusal sets `error` to
// std::errc::too_many_symbolic_link_levels (ELOOP) on every host, so callers
// can say "it is a symbolic link". New files get mode 0666 & ~umask on POSIX.
UniqueFile open_no_follow(const std::filesystem::path& path, OpenAccess access, OpenDisposition disposition,
                          std::error_code& error);

// ---------------------------------------------------------------------------
// Links.

enum class LinkKind {
    none,          // not a link or reparse point (also returned with `error` set when the path cannot be read)
    symlink,       // a symbolic link (IO_REPARSE_TAG_SYMLINK on Windows)
    junction,      // a Windows mount point (IO_REPARSE_TAG_MOUNT_POINT); never on POSIX
    other_reparse, // any other Windows reparse point; never on POSIX
};

// What `path` itself is, without following it.
LinkKind link_kind(const std::filesystem::path& path, std::error_code& error);

// ---------------------------------------------------------------------------
// Locks.

enum class LockMode { shared, exclusive };
enum class LockWait { try_once, wait };

// An advisory whole-file lock: flock on POSIX; LockFileEx over the whole
// range on Windows, on a non-inheritable handle opened with FILE_SHARE_READ |
// FILE_SHARE_WRITE | FILE_SHARE_DELETE (Windows range locks are mandatory, so
// use lock files that nobody reads or writes through another handle). The
// lock ends with the object or the process.
class FileLock {
public:
    FileLock() noexcept = default;
    FileLock(FileLock&&) noexcept = default;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock();

    // Opens (and with `create`, creates) `path` without following a link at
    // it, and locks it. With LockWait::try_once a lock held elsewhere sets
    // `error` to std::errc::resource_unavailable_try_again on every host.
    static FileLock acquire(const std::filesystem::path& path, LockMode mode, LockWait wait, std::error_code& error,
                            bool create = true);

    bool locked() const noexcept { return file_.valid(); }
    explicit operator bool() const noexcept { return locked(); }
    LockMode mode() const noexcept { return mode_; }
    native_file handle() const noexcept { return file_.get(); }

    // Whether the locked file has since been removed: link count 0 on POSIX,
    // delete pending or link count 0 on Windows. A lease holder uses it to
    // see that its file was replaced.
    bool file_removed() const;

    void unlock() noexcept;

private:
    UniqueFile file_;
    LockMode mode_ = LockMode::shared;
};

// ---------------------------------------------------------------------------
// Replacement and removal.

// Renames `from` over `to`. POSIX: rename(2). Windows: a POSIX-semantics
// rename (FileRenameInfoEx REPLACE_IF_EXISTS | POSIX_SEMANTICS) where the
// volume supports it, so an open target is replaced as on POSIX; else
// MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH), retried a bounded number of
// times (about 2 s in all) while ERROR_SHARING_VIOLATION or
// ERROR_ACCESS_DENIED says another program (an antivirus or indexer) holds
// the file.
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error);

// Writes `bytes` to a new file beside `path` with a unique name (O_EXCL,
// CREATE_NEW), flushes it to the device (fsync, FlushFileBuffers) and
// replaces `path` with replace_file(). On failure the temporary is removed
// and `path` is unchanged. POSIX files get mode 0666 & ~umask.
bool write_file_atomically(const std::filesystem::path& path, std::string_view bytes, std::error_code& error);

// Removes `path` and, for a directory, everything under it. A symbolic link,
// junction or other reparse point is removed itself and never followed.
// Read-only files are removed (Windows clears the attribute or uses
// POSIX-semantics delete). Windows retries sharing violations a bounded
// number of times. A missing `path` is success. Returns the number of
// entries removed, or -1 with `error` set.
std::intmax_t remove_tree(const std::filesystem::path& path, std::error_code& error);

// ---------------------------------------------------------------------------
// Private directories.

// Creates the directory `path` (its parent must exist) readable only by the
// current user: mode 0700 on POSIX; on Windows a protected DACL that grants
// only the user's SID full access, with that SID as the owner. Fails with
// std::errc::file_exists when anything is already at `path`.
bool make_private_directory(const std::filesystem::path& path, std::error_code& error);

// Whether `path` is a directory, not a link, owned by the current user and
// accessible to nobody else: mode & 077 == 0 and st_uid == geteuid() on
// POSIX; on Windows not a reparse point, owner = user SID, and a protected
// DACL whose every entry is an allow entry for the user SID.
bool is_private_directory(const std::filesystem::path& path, std::error_code& error);

// `prefix` followed by 32 lowercase hexadecimal digits from the system's
// cryptographic generator (BCryptGenRandom, getrandom, getentropy). Throws
// std::system_error if the generator fails.
std::string unique_name(std::string_view prefix);

} // namespace neura::platform

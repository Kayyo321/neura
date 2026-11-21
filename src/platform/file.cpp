#include "file.hpp"

#include "internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "windows.hpp"

#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/random.h> // getrandom (Linux), getentropy (macOS)
#endif
#endif

namespace fs = std::filesystem;

namespace neura::platform {

#ifdef _WIN32
namespace {

using detail::Handle;
using detail::last_win32_error;
using detail::win32_error;

constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// Antivirus and indexer handles make a rename or delete fail for a moment.
// These bound how long an operation waits for them.
constexpr int retry_attempts = 12;

bool transient(DWORD code) noexcept {
    return code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_LOCK_VIOLATION;
}

void retry_pause(int attempt) {
    // 5, 10, 20, ... ms, at most 400 ms each: about 2.5 s over all attempts.
    const auto delay = std::min(5 << std::min(attempt, 6), 400);
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

// Errors of a file system that has no POSIX-semantics rename or delete.
bool unsupported(DWORD code) noexcept {
    return code == ERROR_INVALID_PARAMETER || code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_FUNCTION;
}

#ifndef FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE
#define FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE 0x00000040
#endif
#ifndef FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE
#define FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE 0x00000010
#endif

std::error_code loop_error() noexcept {
    return std::make_error_code(std::errc::too_many_symbolic_link_levels);
}

bool is_reparse_point(HANDLE file, std::error_code& error) {
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!::GetFileInformationByHandleEx(file, FileAttributeTagInfo, &info, sizeof info)) {
        error = last_win32_error();
        return false;
    }
    return (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

} // namespace

native_file invalid_file() noexcept {
    return INVALID_HANDLE_VALUE;
}

bool UniqueFile::valid() const noexcept {
    return file_ != nullptr && file_ != INVALID_HANDLE_VALUE;
}

void UniqueFile::close() noexcept {
    if (valid()) ::CloseHandle(file_);
    file_ = INVALID_HANDLE_VALUE;
}

std::ptrdiff_t read_some(native_file file, void* buffer, std::size_t size, std::error_code& error) {
    error.clear();
    DWORD got = 0;
    const DWORD want = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
    if (!::ReadFile(file, buffer, want, &got, nullptr)) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_BROKEN_PIPE || code == ERROR_HANDLE_EOF) return 0;
        error = win32_error(code);
        return -1;
    }
    return static_cast<std::ptrdiff_t>(got);
}

bool write_all(native_file file, const void* data, std::size_t size, std::error_code& error) {
    error.clear();
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        DWORD wrote = 0;
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
        if (!::WriteFile(file, bytes, want, &wrote, nullptr)) {
            error = last_win32_error();
            return false;
        }
        bytes += wrote;
        size -= wrote;
    }
    return true;
}

std::FILE* open_c_stream(UniqueFile&& file, const char* mode, std::error_code& error) {
    error.clear();
    const std::string_view text(mode);
    int flags = _O_BINARY;
    if (text.find('+') != std::string_view::npos) {
        flags |= _O_RDWR;
    } else if (text.starts_with('r')) {
        flags |= _O_RDONLY;
    } else {
        flags |= _O_WRONLY;
    }
    if (text.starts_with('a')) flags |= _O_APPEND;
    const int descriptor = ::_open_osfhandle(reinterpret_cast<intptr_t>(file.get()), flags);
    if (descriptor < 0) {
        error = std::error_code(errno, std::generic_category());
        file.close();
        return nullptr;
    }
    file.release();
    std::FILE* stream = ::_fdopen(descriptor, mode);
    if (stream == nullptr) {
        error = std::error_code(errno, std::generic_category());
        ::_close(descriptor);
    }
    return stream;
}

UniqueFile open_no_follow(const fs::path& path, OpenAccess access, OpenDisposition disposition,
                          std::error_code& error) {
    error.clear();
    DWORD rights = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    switch (access) {
    case OpenAccess::read: rights |= GENERIC_READ; break;
    case OpenAccess::write: rights |= GENERIC_WRITE; break;
    case OpenAccess::read_write: rights |= GENERIC_READ | GENERIC_WRITE; break;
    case OpenAccess::append: rights |= FILE_APPEND_DATA; break;
    }
    // CREATE_ALWAYS would supersede a link instead of refusing it, so a
    // truncating open is OPEN_ALWAYS followed by SetEndOfFile once the file
    // is known not to be a reparse point.
    DWORD creation = OPEN_EXISTING;
    switch (disposition) {
    case OpenDisposition::open_existing: creation = OPEN_EXISTING; break;
    case OpenDisposition::open_or_create: creation = OPEN_ALWAYS; break;
    case OpenDisposition::create_or_truncate: creation = OPEN_ALWAYS; break;
    case OpenDisposition::create_new: creation = CREATE_NEW; break;
    }
    const auto name = detail::api_path(path);
    UniqueFile file(::CreateFileW(name.c_str(), rights, share_all, nullptr, creation,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) {
        const DWORD code = ::GetLastError();
        // A directory reparse point (a junction) cannot be opened as a file;
        // say why.
        const DWORD attributes = ::GetFileAttributesW(name.c_str());
        if (code == ERROR_ACCESS_DENIED && attributes != INVALID_FILE_ATTRIBUTES) {
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                error = loop_error();
            } else if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
                error = std::make_error_code(std::errc::is_a_directory);
            } else {
                error = win32_error(code);
            }
        } else {
            error = win32_error(code);
        }
        return {};
    }
    const bool reparse = is_reparse_point(file.get(), error);
    if (error) return {};
    if (reparse) {
        error = loop_error();
        return {};
    }
    if (disposition == OpenDisposition::create_or_truncate) {
        // Truncating needs FILE_WRITE_DATA, which an append handle must not
        // have (it would let writes land before the end), so an append handle
        // truncates through a second handle to the same file object.
        Handle writer;
        HANDLE target = file.get();
        if (access == OpenAccess::append) {
            writer.reset(::ReOpenFile(file.get(), GENERIC_WRITE, share_all, 0));
            if (!writer) {
                error = last_win32_error();
                return {};
            }
            target = writer.get();
        }
        FILE_END_OF_FILE_INFO end{};
        if (!::SetFileInformationByHandle(target, FileEndOfFileInfo, &end, sizeof end)) {
            error = last_win32_error();
            return {};
        }
    }
    return file;
}

LinkKind link_kind(const fs::path& path, std::error_code& error) {
    error.clear();
    const auto name = detail::api_path(path);
    Handle file(::CreateFileW(name.c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!file) {
        error = last_win32_error();
        return LinkKind::none;
    }
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!::GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &info, sizeof info)) {
        error = last_win32_error();
        return LinkKind::none;
    }
    if ((info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) return LinkKind::none;
    if (info.ReparseTag == IO_REPARSE_TAG_SYMLINK) return LinkKind::symlink;
    if (info.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) return LinkKind::junction;
    return LinkKind::other_reparse;
}

FileLock FileLock::acquire(const fs::path& path, LockMode mode, LockWait wait, std::error_code& error,
                           bool create) {
    error.clear();
    const auto name = detail::api_path(path);
    const DWORD creation = create ? OPEN_ALWAYS : OPEN_EXISTING;
    UniqueFile file(::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, share_all, nullptr, creation,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file && ::GetLastError() == ERROR_ACCESS_DENIED) {
        // A read-only lock file can still be locked through a read handle.
        file.reset(::CreateFileW(name.c_str(), GENERIC_READ, share_all, nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    }
    if (!file) {
        error = last_win32_error();
        return {};
    }
    const bool reparse = is_reparse_point(file.get(), error);
    if (error) return {};
    if (reparse) {
        error = loop_error();
        return {};
    }
    DWORD flags = 0;
    if (mode == LockMode::exclusive) flags |= LOCKFILE_EXCLUSIVE_LOCK;
    if (wait == LockWait::try_once) flags |= LOCKFILE_FAIL_IMMEDIATELY;
    OVERLAPPED overlapped{};
    if (!::LockFileEx(file.get(), flags, 0, MAXDWORD, MAXDWORD, &overlapped)) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_LOCK_VIOLATION || code == ERROR_IO_PENDING) {
            error = std::make_error_code(std::errc::resource_unavailable_try_again);
        } else {
            error = win32_error(code);
        }
        return {};
    }
    FileLock lock;
    lock.file_ = std::move(file);
    lock.mode_ = mode;
    return lock;
}

bool FileLock::file_removed() const {
    if (!file_) return false;
    FILE_STANDARD_INFO info{};
    if (!::GetFileInformationByHandleEx(file_.get(), FileStandardInfo, &info, sizeof info)) return false;
    return info.DeletePending || info.NumberOfLinks == 0;
}

void FileLock::unlock() noexcept {
    if (!file_) return;
    OVERLAPPED overlapped{};
    ::UnlockFileEx(file_.get(), 0, MAXDWORD, MAXDWORD, &overlapped);
    file_.close();
}

namespace {

// Renames through FileRenameInfoEx with POSIX semantics, which replaces a
// target that is open with FILE_SHARE_DELETE. Returns the Win32 error.
DWORD posix_rename(const std::wstring& from, const std::wstring& to) {
    Handle source(::CreateFileW(from.c_str(), DELETE | SYNCHRONIZE, share_all, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!source) return ::GetLastError();
    const std::size_t bytes = to.size() * sizeof(wchar_t);
    std::vector<std::byte> buffer(offsetof(FILE_RENAME_INFO, FileName) + bytes + sizeof(wchar_t));
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(info->FileName, to.c_str(), bytes + sizeof(wchar_t));
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS |
                  FILE_RENAME_FLAG_IGNORE_READONLY_ATTRIBUTE;
    if (::SetFileInformationByHandle(source.get(), FileRenameInfoEx, info, static_cast<DWORD>(buffer.size()))) {
        return ERROR_SUCCESS;
    }
    DWORD code = ::GetLastError();
    if (code == ERROR_INVALID_PARAMETER) {
        // Systems before 1809 do not know IGNORE_READONLY_ATTRIBUTE.
        info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
        if (::SetFileInformationByHandle(source.get(), FileRenameInfoEx, info, static_cast<DWORD>(buffer.size()))) {
            return ERROR_SUCCESS;
        }
        code = ::GetLastError();
    }
    return code;
}

} // namespace

bool replace_file(const fs::path& from, const fs::path& to, std::error_code& error) {
    error.clear();
    const auto source = detail::api_path(from);
    const auto target = detail::api_path(to);
    bool posix = true;
    DWORD code = ERROR_SUCCESS;
    for (int attempt = 0; attempt < retry_attempts; ++attempt) {
        if (posix) {
            code = posix_rename(source, target);
            if (code == ERROR_SUCCESS) return true;
            if (unsupported(code)) {
                posix = false;
                --attempt;
                continue;
            }
        } else {
            if (::MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return true;
            }
            code = ::GetLastError();
        }
        if (!transient(code)) break;
        retry_pause(attempt);
    }
    error = win32_error(code);
    return false;
}

namespace {

bool flush_and_close(UniqueFile& file, std::error_code& error) {
    if (!::FlushFileBuffers(file.get())) {
        error = last_win32_error();
        return false;
    }
    file.close();
    return true;
}

void remove_file_quietly(const fs::path& path) {
    ::DeleteFileW(detail::api_path(path).c_str());
}

// Removes one entry (a file, an empty directory or a reparse point itself).
// POSIX semantics first, so a read-only or open file goes at once; then the
// classic calls with the read-only attribute cleared.
bool remove_entry(const std::wstring& name, DWORD attributes, std::error_code& error) {
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool posix = true;
    DWORD code = ERROR_SUCCESS;
    for (int attempt = 0; attempt < retry_attempts; ++attempt) {
        if (posix) {
            Handle file(::CreateFileW(name.c_str(), DELETE | FILE_READ_ATTRIBUTES, share_all, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            if (file) {
                FILE_DISPOSITION_INFO_EX info{};
                info.Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                             FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
                if (::SetFileInformationByHandle(file.get(), FileDispositionInfoEx, &info, sizeof info)) return true;
                code = ::GetLastError();
                if (unsupported(code)) {
                    posix = false;
                    --attempt;
                    continue;
                }
            } else {
                code = ::GetLastError();
            }
        } else {
            if (attributes & FILE_ATTRIBUTE_READONLY) {
                ::SetFileAttributesW(name.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
            }
            if (directory ? ::RemoveDirectoryW(name.c_str()) : ::DeleteFileW(name.c_str())) return true;
            code = ::GetLastError();
        }
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        if (!transient(code) && code != ERROR_DIR_NOT_EMPTY) break;
        retry_pause(attempt);
    }
    error = win32_error(code);
    return false;
}

std::intmax_t remove_below(const std::wstring& name, DWORD attributes, std::error_code& error) {
    std::intmax_t removed = 0;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        WIN32_FIND_DATAW entry{};
        const std::wstring pattern = name + L"\\*";
        HANDLE find = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr,
                                         FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            const DWORD code = ::GetLastError();
            if (code != ERROR_FILE_NOT_FOUND) {
                if (code == ERROR_PATH_NOT_FOUND) return 0;
                error = win32_error(code);
                return -1;
            }
        } else {
            do {
                const std::wstring_view child(entry.cFileName);
                if (child == L"." || child == L"..") continue;
                const auto count = remove_below(name + L"\\" + entry.cFileName, entry.dwFileAttributes, error);
                if (count < 0) {
                    ::FindClose(find);
                    return -1;
                }
                removed += count;
            } while (::FindNextFileW(find, &entry));
            const DWORD code = ::GetLastError();
            ::FindClose(find);
            if (code != ERROR_NO_MORE_FILES) {
                error = win32_error(code);
                return -1;
            }
        }
    }
    if (!remove_entry(name, attributes, error)) return -1;
    return removed + 1;
}

} // namespace

bool write_file_atomically(const fs::path& path, std::string_view bytes, std::error_code& error) {
    error.clear();
    const fs::path temporary =
        path.parent_path() / (L"." + path.filename().native() + L"." + to_utf16(unique_name("")) + L".tmp");
    UniqueFile file = open_no_follow(temporary, OpenAccess::write, OpenDisposition::create_new, error);
    if (!file) return false;
    if (!write_all(file.get(), bytes.data(), bytes.size(), error) || !flush_and_close(file, error) ||
        !replace_file(temporary, path, error)) {
        file.close();
        remove_file_quietly(temporary);
        return false;
    }
    return true;
}

std::intmax_t remove_tree(const fs::path& path, std::error_code& error) {
    error.clear();
    const auto name = detail::api_path(path);
    const DWORD attributes = ::GetFileAttributesW(name.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return 0;
        error = win32_error(code);
        return -1;
    }
    return remove_below(name, attributes, error);
}

namespace {

// The current user's SID and an ACL granting it everything, inherited by
// the directory's children.
struct PrivateAcl {
    std::string sid;
    std::vector<std::byte> acl;
};

bool private_acl(PrivateAcl& out, std::error_code& error) {
    out.sid = detail::current_user_sid();
    if (out.sid.empty()) {
        error = last_win32_error();
        return false;
    }
    auto* sid = reinterpret_cast<PSID>(out.sid.data());
    const DWORD size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD) + ::GetLengthSid(sid);
    out.acl.assign(size, std::byte{});
    auto* acl = reinterpret_cast<PACL>(out.acl.data());
    if (!::InitializeAcl(acl, size, ACL_REVISION) ||
        !::AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, FILE_ALL_ACCESS,
                                 sid)) {
        error = last_win32_error();
        return false;
    }
    return true;
}

} // namespace

bool make_private_directory(const fs::path& path, std::error_code& error) {
    error.clear();
    PrivateAcl acl;
    if (!private_acl(acl, error)) return false;
    SECURITY_DESCRIPTOR descriptor{};
    auto* sid = reinterpret_cast<PSID>(acl.sid.data());
    if (!::InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !::SetSecurityDescriptorOwner(&descriptor, sid, FALSE) ||
        !::SetSecurityDescriptorDacl(&descriptor, TRUE, reinterpret_cast<PACL>(acl.acl.data()), FALSE) ||
        !::SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
        error = last_win32_error();
        return false;
    }
    SECURITY_ATTRIBUTES attributes{sizeof attributes, &descriptor, FALSE};
    if (!::CreateDirectoryW(detail::api_path(path).c_str(), &attributes)) {
        const DWORD code = ::GetLastError();
        error = code == ERROR_ALREADY_EXISTS ? std::make_error_code(std::errc::file_exists) : win32_error(code);
        return false;
    }
    return true;
}

bool is_private_directory(const fs::path& path, std::error_code& error) {
    error.clear();
    Handle file(::CreateFileW(detail::api_path(path).c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES, share_all, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!file) {
        error = last_win32_error();
        return false;
    }
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!::GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &info, sizeof info)) {
        error = last_win32_error();
        return false;
    }
    if (!(info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return false;
    }
    const SECURITY_INFORMATION wanted = OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    DWORD size = 0;
    ::GetKernelObjectSecurity(file.get(), wanted, nullptr, 0, &size);
    if (size == 0) {
        error = last_win32_error();
        return false;
    }
    std::vector<std::byte> buffer(size);
    auto* descriptor = reinterpret_cast<PSECURITY_DESCRIPTOR>(buffer.data());
    if (!::GetKernelObjectSecurity(file.get(), wanted, descriptor, size, &size)) {
        error = last_win32_error();
        return false;
    }
    const std::string user = detail::current_user_sid();
    if (user.empty()) {
        error = last_win32_error();
        return false;
    }
    auto* user_sid = reinterpret_cast<PSID>(const_cast<char*>(user.data()));
    PSID owner = nullptr;
    BOOL defaulted = FALSE;
    if (!::GetSecurityDescriptorOwner(descriptor, &owner, &defaulted) || owner == nullptr ||
        !::EqualSid(owner, user_sid)) {
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!::GetSecurityDescriptorControl(descriptor, &control, &revision) || !(control & SE_DACL_PROTECTED)) {
        return false;
    }
    BOOL present = FALSE;
    PACL dacl = nullptr;
    if (!::GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present || dacl == nullptr) {
        return false; // no DACL grants everyone everything
    }
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
        void* raw = nullptr;
        if (!::GetAce(dacl, index, &raw)) return false;
        const auto* header = static_cast<const ACE_HEADER*>(raw);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return false;
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (!::EqualSid(reinterpret_cast<PSID>(&ace->SidStart), user_sid)) return false;
    }
    return true;
}

namespace {

void random_bytes(void* buffer, std::size_t size) {
    const NTSTATUS status = ::BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer), static_cast<ULONG>(size),
                                              BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        throw std::system_error(std::make_error_code(std::errc::io_error), "BCryptGenRandom failed");
    }
}

} // namespace

#else // POSIX

namespace {

std::error_code errno_error() noexcept {
    return {errno, std::system_category()};
}

void random_bytes(void* buffer, std::size_t size) {
    auto* bytes = static_cast<unsigned char*>(buffer);
#if defined(__linux__)
    while (size > 0) {
        const auto got = ::getrandom(bytes, size, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno_error(), "getrandom failed");
        }
        bytes += got;
        size -= static_cast<std::size_t>(got);
    }
#elif defined(__APPLE__)
    while (size > 0) {
        const std::size_t chunk = std::min<std::size_t>(size, 256);
        if (::getentropy(bytes, chunk) != 0) throw std::system_error(errno_error(), "getentropy failed");
        bytes += chunk;
        size -= chunk;
    }
#else
    const int random = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random < 0) throw std::system_error(errno_error(), "cannot open /dev/urandom");
    while (size > 0) {
        const auto got = ::read(random, bytes, size);
        if (got <= 0) {
            if (got < 0 && errno == EINTR) continue;
            const auto error = errno_error();
            ::close(random);
            throw std::system_error(error, "cannot read /dev/urandom");
        }
        bytes += got;
        size -= static_cast<std::size_t>(got);
    }
    ::close(random);
#endif
}

} // namespace

native_file invalid_file() noexcept {
    return -1;
}

bool UniqueFile::valid() const noexcept {
    return file_ >= 0;
}

void UniqueFile::close() noexcept {
    if (valid()) ::close(file_);
    file_ = -1;
}

std::ptrdiff_t read_some(native_file file, void* buffer, std::size_t size, std::error_code& error) {
    error.clear();
    for (;;) {
        const auto got = ::read(file, buffer, size);
        if (got >= 0) return got;
        if (errno == EINTR) continue;
        error = errno_error();
        return -1;
    }
}

bool write_all(native_file file, const void* data, std::size_t size, std::error_code& error) {
    error.clear();
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const auto wrote = ::write(file, bytes, size);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            error = errno_error();
            return false;
        }
        bytes += wrote;
        size -= static_cast<std::size_t>(wrote);
    }
    return true;
}

std::FILE* open_c_stream(UniqueFile&& file, const char* mode, std::error_code& error) {
    error.clear();
    std::FILE* stream = ::fdopen(file.get(), mode);
    if (stream == nullptr) {
        error = errno_error();
        file.close();
        return nullptr;
    }
    file.release();
    return stream;
}

UniqueFile open_no_follow(const fs::path& path, OpenAccess access, OpenDisposition disposition,
                          std::error_code& error) {
    error.clear();
    int flags = O_NOFOLLOW | O_CLOEXEC;
    switch (access) {
    case OpenAccess::read: flags |= O_RDONLY; break;
    case OpenAccess::write: flags |= O_WRONLY; break;
    case OpenAccess::read_write: flags |= O_RDWR; break;
    case OpenAccess::append: flags |= O_WRONLY | O_APPEND; break;
    }
    switch (disposition) {
    case OpenDisposition::open_existing: break;
    case OpenDisposition::open_or_create: flags |= O_CREAT; break;
    case OpenDisposition::create_or_truncate: flags |= O_CREAT | O_TRUNC; break;
    case OpenDisposition::create_new: flags |= O_CREAT | O_EXCL; break;
    }
    for (;;) {
        const int descriptor = ::open(path.c_str(), flags, 0666);
        if (descriptor >= 0) return UniqueFile(descriptor);
        if (errno == EINTR) continue;
        error = errno_error();
        return {};
    }
}

LinkKind link_kind(const fs::path& path, std::error_code& error) {
    error.clear();
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        error = errno_error();
        return LinkKind::none;
    }
    return S_ISLNK(info.st_mode) ? LinkKind::symlink : LinkKind::none;
}

FileLock FileLock::acquire(const fs::path& path, LockMode mode, LockWait wait, std::error_code& error,
                           bool create) {
    error.clear();
    const int create_flag = create ? O_CREAT : 0;
    int descriptor = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | create_flag, 0666);
    if (descriptor < 0 && (errno == EACCES || errno == EROFS)) {
        descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (descriptor < 0) {
        error = errno_error();
        return {};
    }
    UniqueFile file(descriptor);
    const int operation =
        (mode == LockMode::exclusive ? LOCK_EX : LOCK_SH) | (wait == LockWait::try_once ? LOCK_NB : 0);
    while (::flock(file.get(), operation) != 0) {
        if (errno == EINTR) continue;
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            error = std::make_error_code(std::errc::resource_unavailable_try_again);
        } else {
            error = errno_error();
        }
        return {};
    }
    FileLock lock;
    lock.file_ = std::move(file);
    lock.mode_ = mode;
    return lock;
}

bool FileLock::file_removed() const {
    if (!file_) return false;
    struct stat info {};
    if (::fstat(file_.get(), &info) != 0) return false;
    return info.st_nlink == 0;
}

void FileLock::unlock() noexcept {
    if (!file_) return;
    ::flock(file_.get(), LOCK_UN);
    file_.close();
}

bool replace_file(const fs::path& from, const fs::path& to, std::error_code& error) {
    error.clear();
    if (::rename(from.c_str(), to.c_str()) != 0) {
        error = errno_error();
        return false;
    }
    return true;
}

bool write_file_atomically(const fs::path& path, std::string_view bytes, std::error_code& error) {
    error.clear();
    const fs::path temporary = path.parent_path() / ("." + path.filename().native() + "." + unique_name("") + ".tmp");
    UniqueFile file = open_no_follow(temporary, OpenAccess::write, OpenDisposition::create_new, error);
    if (!file) return false;
    bool written = write_all(file.get(), bytes.data(), bytes.size(), error);
    if (written) {
        while (::fsync(file.get()) != 0) {
            if (errno == EINTR) continue;
            error = errno_error();
            written = false;
            break;
        }
    }
    if (written && ::close(file.release()) != 0) {
        error = errno_error();
        written = false;
    }
    if (written && replace_file(temporary, path, error)) return true;
    file.close();
    ::unlink(temporary.c_str());
    return false;
}

std::intmax_t remove_tree(const fs::path& path, std::error_code& error) {
    error.clear();
    // remove_all removes a symbolic link itself and never follows one.
    const auto removed = fs::remove_all(path, error);
    if (error) return -1;
    return static_cast<std::intmax_t>(removed);
}

bool make_private_directory(const fs::path& path, std::error_code& error) {
    error.clear();
    if (::mkdir(path.c_str(), 0700) != 0) {
        error = errno_error();
        return false;
    }
    return true;
}

bool is_private_directory(const fs::path& path, std::error_code& error) {
    error.clear();
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        error = errno_error();
        return false;
    }
    return S_ISDIR(info.st_mode) && info.st_uid == ::geteuid() && (info.st_mode & 077) == 0;
}

#endif

// ---------------------------------------------------------------------------
// Shared by both implementations.

UniqueFile::UniqueFile() noexcept : file_(invalid_file()) {}

UniqueFile::UniqueFile(native_file file) noexcept : file_(file) {}

UniqueFile::UniqueFile(UniqueFile&& other) noexcept : file_(other.release()) {}

UniqueFile& UniqueFile::operator=(UniqueFile&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
}

UniqueFile::~UniqueFile() {
    close();
}

native_file UniqueFile::release() noexcept {
    const native_file file = file_;
    file_ = invalid_file();
    return file;
}

void UniqueFile::reset(native_file file) noexcept {
    close();
    file_ = file;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        unlock();
        file_ = std::move(other.file_);
        mode_ = other.mode_;
    }
    return *this;
}

FileLock::~FileLock() {
    unlock();
}

std::string unique_name(std::string_view prefix) {
    std::array<unsigned char, 16> bytes{};
    random_bytes(bytes.data(), bytes.size());
    static constexpr char digits[] = "0123456789abcdef";
    std::string name(prefix);
    for (const unsigned char byte : bytes) {
        name += digits[byte >> 4];
        name += digits[byte & 0x0F];
    }
    return name;
}

} // namespace neura::platform

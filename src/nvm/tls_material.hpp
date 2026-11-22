#pragma once

// Authorize explicit TLS paths before opening them for reading, then validate
// the opened file and give OpenSSL bytes from that same handle.
#include "provider_internal.hpp"
#include "platform/file.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <iterator>
#ifdef _WIN32
#include "platform/text.hpp"
#include "platform/windows.hpp"
#include <cstring>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(F_GETPATH) || (defined(__FreeBSD__) && defined(F_KINFO))
#include <sys/param.h>
#endif
#if defined(__FreeBSD__) && defined(F_KINFO)
#include <sys/user.h>
#endif
#endif
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

namespace neura::nvm::provider::tls_material {

inline bool path_permitted(const ResolvedPath& path, const std::vector<ResolvedPath>& prefixes) {
    return std::any_of(prefixes.begin(), prefixes.end(), [&](const auto& prefix) { return path.within(prefix); });
}

// Split an already resolved absolute pathname without doing another lookup.
inline ResolvedPath resolved_name(std::string_view name) {
    ResolvedPath path;
    while (!name.empty()) {
        const auto end = name.find('/');
        if (end != 0) path.components.emplace_back(name.substr(0, end));
        if (end == std::string_view::npos) break;
        name.remove_prefix(end + 1);
    }
    return path;
}

#ifdef _WIN32

inline bool drive_path(std::wstring_view path) {
    if (path.size() < 3 || path[1] != L':' || path[2] != L'\\') return false;
    const auto drive = path[0];
    return (drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z');
}

inline std::uint32_t qualify_local_path(std::wstring& path) {
    std::wstring full(MAX_PATH, L'\0');
    for (;;) {
        const DWORD size = ::GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (!size) return host_error::from_win32(::GetLastError());
        if (size < full.size()) { full.resize(size); break; }
        full.resize(size);
    }
    if (!drive_path(full) || ::GetDriveTypeW(full.substr(0, 3).c_str()) == DRIVE_REMOTE)
        return host_error::invalid_input;
    path = std::move(full);
    return 0;
}

inline constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// Read the substitute name, which the filesystem actually follows. The
// optional print name can differ. The fixed layouts are defined by
// REPARSE_DATA_BUFFER for symbolic links and mount points.
inline std::uint32_t link_target(const std::wstring& path, std::wstring& target, bool& relative) {
    const platform::UniqueFile link(::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!link) return host_error::from_win32(::GetLastError());
    std::array<unsigned char, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> data;
    DWORD size = 0;
    if (!::DeviceIoControl(link.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0, data.data(),
                           static_cast<DWORD>(data.size()), &size, nullptr))
        return host_error::from_win32(::GetLastError());
    if (size < 8) return host_error::invalid_input;

    DWORD tag;
    std::memcpy(&tag, data.data(), sizeof tag);
    if (tag != IO_REPARSE_TAG_SYMLINK && tag != IO_REPARSE_TAG_MOUNT_POINT) return host_error::unsupported;
    const bool symbolic = tag == IO_REPARSE_TAG_SYMLINK;
    const std::size_t path_offset = symbolic ? 20 : 16;
    if (size < path_offset) return host_error::invalid_input;
    WORD offset, length;
    std::memcpy(&offset, data.data() + 8, sizeof offset);
    std::memcpy(&length, data.data() + 10, sizeof length);
    if (!length || length % sizeof(wchar_t) || offset % sizeof(wchar_t) ||
        path_offset + offset + length > size)
        return host_error::invalid_input;
    target.resize(length / sizeof(wchar_t));
    std::memcpy(target.data(), data.data() + path_offset + offset, length);
    DWORD flags = 0;
    if (symbolic) std::memcpy(&flags, data.data() + 16, sizeof flags);
    if (flags > 1) return host_error::invalid_input;
    relative = flags == 1;
    if (!relative) {
        if (target.starts_with(L"\\??\\") || target.starts_with(L"\\\\?\\")) target.erase(0, 4);
        if (!drive_path(target)) return host_error::invalid_input;
    } else if (target.front() == L'\\' || target.front() == L'/' || target.find(L':') != std::wstring::npos) {
        return host_error::invalid_input;
    }
    const auto text = platform::try_to_utf8(target);
    return text ? windows_path_error(*text) : host_error::invalid_input;
}

// Inspect path components before traversing a reparse point. This rejects
// UNC and device targets before reading attributes or bytes from them.
inline std::uint32_t resolve_local_path(std::wstring& path) {
    for (int links = 0; links <= 40; ++links) {
        if (const auto error = qualify_local_path(path)) return error;
        bool follow = false;
        for (std::size_t start = 3; start < path.size();) {
            const auto end = std::min(path.find(L'\\', start), path.size());
            const auto component = L"\\\\?\\" + path.substr(0, end);
            const auto attributes = ::GetFileAttributesW(component.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) return host_error::from_win32(::GetLastError());
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                std::wstring target;
                bool relative = false;
                if (const auto error = link_target(component, target, relative)) return error;
                if (relative) target.insert(0, path.substr(0, start));
                path = target + path.substr(end);
                follow = true;
                break;
            }
            start = end + 1;
        }
        if (!follow) return 0;
    }
    return host_error::other;
}

inline std::uint32_t windows_name_error(std::wstring name, const std::vector<ResolvedPath>& prefixes) {
    if (name.starts_with(L"\\\\?\\")) name.erase(0, 4);
    if (!drive_path(name)) return host_error::invalid_input;
    std::replace(name.begin(), name.end(), L'\\', L'/');
    const auto utf8 = platform::try_to_utf8(name);
    if (!utf8) return host_error::unsupported;
    return prefixes.empty() || path_permitted(resolved_name(*utf8), prefixes) ? 0 : host_error::capability_denied;
}

#endif

inline std::uint32_t opened_path_error(platform::native_file file, const std::vector<ResolvedPath>& prefixes) {
#ifdef _WIN32
    std::wstring name(512, L'\0');
    for (;;) {
        const auto size = ::GetFinalPathNameByHandleW(file, name.data(), static_cast<DWORD>(name.size()),
                                                     FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!size) return host_error::from_win32(::GetLastError());
        if (size < name.size()) { name.resize(size); break; }
        name.resize(size);
    }
    return windows_name_error(std::move(name), prefixes);
#else
    std::string name;
#if defined(__linux__)
    name.resize(512);
    const auto link = "/proc/self/fd/" + std::to_string(file);
    for (;;) {
        const auto size = ::readlink(link.c_str(), name.data(), name.size());
        // Without procfs the opened object cannot be checked against the prefixes.
        if (size < 0) return errno == ENOENT || errno == ENOTDIR || errno == EACCES || errno == EPERM
            ? host_error::unsupported : host_error::from_errno(errno);
        if (static_cast<std::size_t>(size) < name.size()) { name.resize(static_cast<std::size_t>(size)); break; }
        if (name.size() >= 1024 * 1024) return host_error::invalid_input;
        name.resize(name.size() * 2);
    }
    // An unlinked object has no authorizable pathname.
    if (name.ends_with(" (deleted)")) return host_error::capability_denied;
#elif defined(F_GETPATH)
    char buffer[MAXPATHLEN]{};
    if (::fcntl(file, F_GETPATH, buffer) != 0) return host_error::from_errno(errno);
    name.assign(buffer, std::find(std::begin(buffer), std::end(buffer), '\0'));
#elif defined(__FreeBSD__) && defined(F_KINFO)
    struct kinfo_file info{};
    info.kf_structsize = sizeof(info);
    if (::fcntl(file, F_KINFO, &info) != 0) return host_error::from_errno(errno);
    name.assign(info.kf_path, std::find(std::begin(info.kf_path), std::end(info.kf_path), '\0'));
#else
    (void)file;
    return host_error::unsupported;
#endif
    return name.starts_with('/') && path_permitted(resolved_name(name), prefixes) ?
        0 : host_error::capability_denied;
#endif
}

inline std::uint32_t read(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
                          std::string& path) {
    if (path.empty()) return 0;
    if (!capabilities.fs) return host_error::capability_denied;
    if (!usable_text(path)) return host_error::invalid_input;
#ifdef _WIN32
    const auto separator = [](char c) { return c == '\\' || c == '/'; };
    if (path.size() >= 2 && separator(path[0]) && separator(path[1])) return host_error::invalid_input;
    if (const auto error = windows_path_error(path)) return error;
    auto wide = platform::try_to_utf16(path);
    if (!wide) return host_error::invalid_input;
    if (const auto error = resolve_local_path(*wide)) return error;
    const auto absolute = L"\\\\?\\" + *wide;
    if (!prefixes.empty()) {
        std::wstring canonical(MAX_PATH, L'\0');
        for (;;) {
            const auto size = ::GetLongPathNameW(absolute.c_str(), canonical.data(), static_cast<DWORD>(canonical.size()));
            if (!size) return host_error::from_win32(::GetLastError());
            if (size < canonical.size()) { canonical.resize(size); break; }
            canonical.resize(size);
        }
        if (const auto error = windows_name_error(std::move(canonical), prefixes)) return error;
    }
    const platform::UniqueFile file(::CreateFileW(absolute.c_str(), GENERIC_READ, share_all, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file) return host_error::from_win32(::GetLastError());
    BY_HANDLE_FILE_INFORMATION info{};
    if (::GetFileType(file.get()) != FILE_TYPE_DISK) return host_error::invalid_input;
    if (!::GetFileInformationByHandle(file.get(), &info)) return host_error::from_win32(::GetLastError());
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return host_error::invalid_input;
    if (const auto error = opened_path_error(file.get(), prefixes)) return error;
#else
    std::string candidate = path;
    if (!prefixes.empty()) {
#if !defined(__linux__) && !defined(F_GETPATH) && !(defined(__FreeBSD__) && defined(F_KINFO))
        // No descriptor-to-path facility (for example OpenBSD): the opened
        // object cannot be checked against the prefixes.
        return host_error::unsupported;
#else
        const auto resolved = resolve_path(path, true);
        if (resolved.error) return resolved.error;
        if (!path_permitted(resolved, prefixes)) return host_error::capability_denied;
        candidate = resolved.text();
#endif
    }
    // Check the kind before opening, so a FIFO or device is never opened.
    // This also preserves lookup failures such as a trailing slash on a file.
    struct stat before{};
    if (::stat(path.c_str(), &before) != 0) return host_error::from_errno(errno);
    if (!S_ISREG(before.st_mode)) return host_error::invalid_input;
    const auto nofollow = prefixes.empty() ? 0 : O_NOFOLLOW;
    const platform::UniqueFile file(::open(candidate.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | nofollow));
    if (!file) return host_error::from_errno(errno);
    struct stat info{};
    if (::fstat(file.get(), &info) != 0) return host_error::from_errno(errno);
    if (!S_ISREG(info.st_mode)) return host_error::invalid_input;
    if (!prefixes.empty()) {
        if (info.st_nlink == 0) return host_error::capability_denied;
        if (const auto error = opened_path_error(file.get(), prefixes)) return error;
    }
#endif
    constexpr std::size_t limit = 16 * 1024 * 1024;
    std::string bytes;
    std::array<char, 8192> buffer;
    for (;;) {
        std::error_code error;
        const auto count = platform::read_some(file.get(), buffer.data(), buffer.size(), error);
        if (count < 0) {
#ifdef _WIN32
            return host_error::from_win32(static_cast<std::uint32_t>(error.value()));
#else
            return host_error::from_errno(error.value());
#endif
        }
        if (count == 0) break;
        if (static_cast<std::size_t>(count) > limit - bytes.size()) return host_error::invalid_input;
        bytes.append(buffer.data(), static_cast<std::size_t>(count));
    }
    if (bytes.empty()) return host_error::invalid_input;
    path = std::move(bytes);
    return 0;
}

inline bool trust(SSL_CTX* context, const std::string& bytes) {
    if (bytes.empty()) {
#ifdef _WIN32
        return SSL_CTX_load_verify_store(context, "org.openssl.winstore:") == 1;
#else
        return SSL_CTX_set_default_verify_paths(context) == 1;
#endif
    }
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(bytes.data(), static_cast<int>(bytes.size())), BIO_free);
    if (!bio) return false;
    auto no_password = [](char*, int, int, void*) { return 0; };
    auto* entries = PEM_X509_INFO_read_bio(bio.get(), nullptr, no_password, nullptr);
    if (!entries) return false;
    auto* store = SSL_CTX_get_cert_store(context);
    bool ok = true;
    int certificates = 0;
    for (int i = 0; i < sk_X509_INFO_num(entries); ++i) {
        const auto* entry = sk_X509_INFO_value(entries, i);
        if (entry->x509) { ++certificates; ok = X509_STORE_add_cert(store, entry->x509) == 1 && ok; }
        if (entry->crl) ok = X509_STORE_add_crl(store, entry->crl) == 1 && ok;
    }
    sk_X509_INFO_pop_free(entries, X509_INFO_free);
    return ok && certificates > 0;
}

inline bool identity(SSL_CTX* context, const std::string& certificate, const std::string& key) {
    std::unique_ptr<BIO, decltype(&BIO_free)> certs(BIO_new_mem_buf(certificate.data(), static_cast<int>(certificate.size())), BIO_free);
    std::unique_ptr<BIO, decltype(&BIO_free)> keys(BIO_new_mem_buf(key.data(), static_cast<int>(key.size())), BIO_free);
    if (!certs || !keys) return false;
    std::unique_ptr<X509, decltype(&X509_free)> leaf(PEM_read_bio_X509_AUX(certs.get(), nullptr, nullptr, nullptr), X509_free);
    if (!leaf || SSL_CTX_use_certificate(context, leaf.get()) != 1) return false;
    while (auto* next = PEM_read_bio_X509(certs.get(), nullptr, nullptr, nullptr)) {
        if (SSL_CTX_add_extra_chain_cert(context, next) != 1) { X509_free(next); return false; }
    }
    ERR_clear_error(); // The final PEM read reaches the end of the bundle.
    auto no_password = [](char*, int, int, void*) { return 0; };
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(
        PEM_read_bio_PrivateKey(keys.get(), nullptr, no_password, nullptr), EVP_PKEY_free);
    return private_key && SSL_CTX_use_PrivateKey(context, private_key.get()) == 1 && SSL_CTX_check_private_key(context) == 1;
}

inline bool client_names(SSL_CTX* context, const std::string& bytes) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(bytes.data(), static_cast<int>(bytes.size())), BIO_free);
    auto* names = sk_X509_NAME_new_null();
    if (!bio || !names) { sk_X509_NAME_free(names); return false; }
    while (auto* cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)) {
        auto* name = X509_NAME_dup(X509_get_subject_name(cert));
        X509_free(cert);
        if (!name || !sk_X509_NAME_push(names, name)) {
            X509_NAME_free(name); sk_X509_NAME_pop_free(names, X509_NAME_free); return false;
        }
    }
    ERR_clear_error();
    if (!sk_X509_NAME_num(names)) { sk_X509_NAME_free(names); return false; }
    SSL_CTX_set_client_CA_list(context, names);
    return true;
}
} // namespace neura::nvm::provider::tls_material

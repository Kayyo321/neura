#pragma once

// Explicit TLS paths are opened once. Authorize the opened object, then give
// OpenSSL its bytes; a later path replacement cannot change the material used.
#include "provider_internal.hpp"
#include "platform/file.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#ifdef _WIN32
#include "platform/text.hpp"
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

namespace neura::nvm::provider::tls_material {

inline std::uint32_t read(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
                          std::string& path) {
    if (path.empty()) return 0;
    if (!capabilities.fs) return host_error::capability_denied;
    if (!usable_text(path)) return host_error::invalid_input;
    std::string final;
#ifdef _WIN32
    if (const auto error = windows_path_error(path)) return error;
    const auto wide = platform::try_to_utf16(path);
    if (!wide) return host_error::invalid_input;
    platform::UniqueFile file(CreateFileW(wide->c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!file) return host_error::from_win32(GetLastError());
    if (GetFileType(file.get()) != FILE_TYPE_DISK) return host_error::invalid_input;
    std::wstring name(512, L'\0');
    for (;;) {
        const auto size = GetFinalPathNameByHandleW(file.get(), name.data(), static_cast<DWORD>(name.size()),
                                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!size) return host_error::from_win32(GetLastError());
        if (size < name.size()) { name.resize(size); break; }
        name.resize(static_cast<std::size_t>(size) + 1);
    }
    if (name.starts_with(L"\\\\?\\UNC\\")) name = L"//" + name.substr(8);
    else if (name.starts_with(L"\\\\?\\")) name.erase(0, 4);
    std::replace(name.begin(), name.end(), L'\\', L'/');
    const auto utf8 = platform::try_to_utf8(name);
    if (!utf8) return host_error::unsupported;
    final = *utf8;
#else
    platform::UniqueFile file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
    if (!file) return host_error::from_errno(errno);
    struct stat info{};
    if (::fstat(file.get(), &info) != 0) return host_error::from_errno(errno);
    if (!S_ISREG(info.st_mode)) return host_error::invalid_input;
#if defined(__linux__)
    std::string name(512, '\0');
    const auto link = "/proc/self/fd/" + std::to_string(file.get());
    for (;;) {
        const auto size = ::readlink(link.c_str(), name.data(), name.size());
        if (size < 0) return host_error::from_errno(errno);
        if (static_cast<std::size_t>(size) < name.size()) { name.resize(static_cast<std::size_t>(size)); break; }
        if (name.size() >= 1024 * 1024) return host_error::invalid_input;
        name.resize(name.size() * 2);
    }
    // An unlinked object has no authorizable pathname.
    if (name.ends_with(" (deleted)")) return host_error::capability_denied;
    final = std::move(name);
#elif defined(__APPLE__)
    char name[PATH_MAX];
    if (::fcntl(file.get(), F_GETPATH, name) != 0) return host_error::from_errno(errno);
    final = name;
#else
    if (!prefixes.empty()) return host_error::unsupported;
#endif
#endif
    if (!prefixes.empty()) {
        // Parse the canonical name reported for the handle without opening or
        // resolving it again. ResolvedPath::within applies Windows case rules.
        ResolvedPath actual;
        std::size_t start = 0;
#ifdef _WIN32
        if (final.starts_with("//")) {
            const auto share = final.find('/', final.find('/', 2) + 1);
            actual.components.push_back(final.substr(0, share));
            start = share == std::string::npos ? final.size() : share + 1;
        }
#endif
        while (start < final.size()) {
            const auto end = final.find('/', start);
            if (end != start) actual.components.push_back(final.substr(start, end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (std::none_of(prefixes.begin(), prefixes.end(), [&](const auto& prefix) { return actual.within(prefix); }))
            return host_error::capability_denied;
    }
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

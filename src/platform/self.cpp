#include "self.hpp"

#include "internal.hpp"

#include <string>

#ifdef _WIN32
#include "windows.hpp"
#else
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;

namespace neura::platform {

process_id current_process_id() noexcept {
#ifdef _WIN32
    return ::GetCurrentProcessId();
#else
    return static_cast<process_id>(::getpid());
#endif
}

std::string_view executable_suffix() noexcept {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

#ifdef _WIN32
namespace {

// GetFinalPathNameByHandleW returns \\?\C:\... or \\?\UNC\server\share\...;
// the prefix is dropped when the rest is an ordinary drive or UNC path short
// enough not to need it.
std::wstring without_extended_prefix(std::wstring path) {
    constexpr std::wstring_view unc = L"\\\\?\\UNC\\";
    constexpr std::wstring_view local = L"\\\\?\\";
    if (path.size() >= MAX_PATH) return path;
    if (path.starts_with(unc)) return L"\\\\" + path.substr(unc.size());
    if (path.starts_with(local) && path.size() >= local.size() + 2 && path[local.size() + 1] == L':') {
        return path.substr(local.size());
    }
    return path;
}

} // namespace

fs::path executable_path(std::error_code& error) {
    error.clear();
    std::wstring module(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (length == 0) {
            error = detail::last_win32_error();
            return {};
        }
        if (length < module.size()) {
            module.resize(length);
            break;
        }
        if (module.size() >= 32768) {
            error = detail::win32_error(ERROR_INSUFFICIENT_BUFFER);
            return {};
        }
        module.resize(module.size() * 2);
    }
    detail::Handle file(::CreateFileW(module.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!file) {
        error = detail::last_win32_error();
        return {};
    }
    std::wstring resolved(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetFinalPathNameByHandleW(
            file.get(), resolved.data(), static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) {
            error = detail::last_win32_error();
            return {};
        }
        if (length < resolved.size()) {
            resolved.resize(length);
            break;
        }
        resolved.resize(length + 1);
    }
    return fs::path(without_extended_prefix(std::move(resolved)));
}
#else
fs::path executable_path(std::error_code& error) {
    error.clear();
#if defined(__linux__)
    auto path = fs::canonical("/proc/self/exe", error);
    if (error) return {};
    return path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        error = std::make_error_code(std::errc::no_buffer_space);
        return {};
    }
    buffer.resize(std::strlen(buffer.c_str()));
    auto path = fs::canonical(buffer, error);
    if (error) return {};
    return path;
#else
    error = std::make_error_code(std::errc::not_supported);
    return {};
#endif
}
#endif

fs::path executable_path() {
    std::error_code error;
    auto path = executable_path(error);
    if (error) throw std::system_error(error, "cannot find the running executable");
    return path;
}

} // namespace neura::platform

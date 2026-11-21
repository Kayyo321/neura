#pragma once

// Helpers shared by the platform implementation files. Not for use outside
// src/platform.

#include "text.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include "windows.hpp"

namespace neura::platform::detail {

inline std::error_code win32_error(DWORD code) noexcept {
    return {static_cast<int>(code), std::system_category()};
}

inline std::error_code last_win32_error() noexcept {
    return win32_error(::GetLastError());
}

// Owns a kernel HANDLE (null and INVALID_HANDLE_VALUE are both empty).
class Handle {
public:
    Handle() noexcept = default;
    explicit Handle(HANDLE handle) noexcept : handle_(handle) {}
    Handle(Handle&& other) noexcept : handle_(other.release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }

    bool valid() const noexcept { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    explicit operator bool() const noexcept { return valid(); }
    HANDLE get() const noexcept { return handle_; }
    HANDLE release() noexcept {
        HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }
    void reset(HANDLE handle = nullptr) noexcept {
        if (valid()) ::CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

// The path as Windows API text. An absolute path gets the \\?\ (or
// \\?\UNC\) prefix so it is not limited to MAX_PATH and is not reinterpreted
// (no trailing-dot or device-name rewriting); a relative path is returned
// unchanged with '/' turned into '\\'.
std::wstring api_path(const std::filesystem::path& path);

// The token user SID of this process, as a self-relative buffer; empty on
// failure (with GetLastError set).
std::string current_user_sid();

} // namespace neura::platform::detail
#endif

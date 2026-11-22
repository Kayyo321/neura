#include "error.hpp"

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include "text.hpp"
#include "windows.hpp"
#endif

namespace neura::platform {

int last_error() noexcept {
#ifdef _WIN32
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

std::error_code system_error_code(int code) noexcept {
    return {code, std::system_category()};
}

std::error_code last_error_code() noexcept {
    return system_error_code(last_error());
}

std::string error_message(int code) {
#ifdef _WIN32
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(code), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message;
    if (length != 0 && buffer != nullptr) {
        std::wstring_view text(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' ||
                                 text.back() == L'.')) {
            text.remove_suffix(1);
        }
        message = try_to_utf8(text).value_or(std::string());
    }
    if (buffer != nullptr) ::LocalFree(buffer);
    if (message.empty()) message = "error " + std::to_string(static_cast<unsigned long>(code));
    return message;
#else
    // strerror is not thread-safe everywhere, and strerror_r has two
    // incompatible signatures; std::system_category().message() wraps the
    // right one.
    return std::system_category().message(code);
#endif
}

std::string error_message(const std::error_code& code) {
    if (code.category() == std::system_category()) return error_message(code.value());
    return code.message();
}

void throw_last_error(std::string_view what) {
    throw std::system_error(last_error_code(), std::string(what));
}

} // namespace neura::platform

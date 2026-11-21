#include "environment.hpp"

#include "internal.hpp"

#ifdef _WIN32
#include "windows.hpp"

#include <cstdlib>
#else
#include <cerrno>
#include <cstdlib>

extern char** environ;
#endif

namespace neura::platform {

std::optional<std::string> get_env(std::string_view name) {
    std::error_code error;
    auto value = get_env(name, error);
    if (error) throw std::system_error(error, "cannot read environment variable " + std::string(name));
    return value;
}

#ifdef _WIN32

namespace {

bool valid_name(const std::wstring& name) noexcept {
    return !name.empty() && name.find(L'=', 1) == std::wstring::npos && name.find(L'\0') == std::wstring::npos;
}

} // namespace

std::optional<std::string> get_env(std::string_view name, std::error_code& error) {
    error.clear();
    const auto wide = try_to_utf16(name);
    if (!wide) {
        error = invalid_text_error();
        return std::nullopt;
    }
    if (!valid_name(*wide)) return std::nullopt;
    std::wstring value(128, L'\0');
    for (;;) {
        ::SetLastError(ERROR_SUCCESS);
        const DWORD length = ::GetEnvironmentVariableW(wide->c_str(), value.data(), static_cast<DWORD>(value.size()));
        if (length == 0) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
            if (code != ERROR_SUCCESS) {
                error = detail::win32_error(code);
                return std::nullopt;
            }
            return std::string(); // set to the empty string
        }
        if (length < value.size()) {
            value.resize(length);
            break;
        }
        value.resize(length); // the size needed, terminator included
    }
    auto text = try_to_utf8(value);
    if (!text) {
        error = invalid_text_error();
        return std::nullopt;
    }
    return text;
}

bool set_env(std::string_view name, std::string_view value, std::error_code& error) {
    error.clear();
    const auto wide_name = try_to_utf16(name);
    const auto wide_value = try_to_utf16(value);
    if (!wide_name || !wide_value) {
        error = invalid_text_error();
        return false;
    }
    if (!valid_name(*wide_name) || wide_value->find(L'\0') != std::wstring::npos) {
        error = detail::win32_error(ERROR_INVALID_PARAMETER);
        return false;
    }
    // The C runtime keeps its own copy for getenv. It cannot hold an empty
    // value: _wputenv_s(name, L"") removes the variable from both copies, so
    // the system's copy is set afterwards.
    ::_wputenv_s(wide_name->c_str(), wide_value->c_str());
    if (!::SetEnvironmentVariableW(wide_name->c_str(), wide_value->c_str())) {
        error = detail::last_win32_error();
        return false;
    }
    return true;
}

bool unset_env(std::string_view name, std::error_code& error) {
    error.clear();
    const auto wide = try_to_utf16(name);
    if (!wide) {
        error = invalid_text_error();
        return false;
    }
    if (!valid_name(*wide)) {
        error = detail::win32_error(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (!::SetEnvironmentVariableW(wide->c_str(), nullptr) && ::GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
        error = detail::last_win32_error();
        return false;
    }
    ::_wputenv_s(wide->c_str(), L"");
    return true;
}

std::vector<std::pair<std::string, std::string>> environment() {
    wchar_t* block = ::GetEnvironmentStringsW();
    if (block == nullptr) return {};
    std::vector<std::pair<std::string, std::string>> variables;
    try {
        for (const wchar_t* entry = block; *entry != L'\0';) {
            const std::wstring_view text(entry);
            entry += text.size() + 1;
            if (text.front() == L'=') continue; // "=C:=C:\dir" and the like
            const auto equals = text.find(L'=');
            const auto name = text.substr(0, equals);
            const auto value = equals == std::wstring_view::npos ? std::wstring_view() : text.substr(equals + 1);
            variables.emplace_back(to_utf8(name), to_utf8(value));
        }
    } catch (...) {
        ::FreeEnvironmentStringsW(block);
        throw;
    }
    ::FreeEnvironmentStringsW(block);
    return variables;
}

#else // POSIX

std::optional<std::string> get_env(std::string_view name, std::error_code& error) {
    error.clear();
    const char* value = std::getenv(std::string(name).c_str());
    if (value == nullptr) return std::nullopt;
    return std::string(value);
}

bool set_env(std::string_view name, std::string_view value, std::error_code& error) {
    error.clear();
    if (::setenv(std::string(name).c_str(), std::string(value).c_str(), 1) != 0) {
        error = {errno, std::system_category()};
        return false;
    }
    return true;
}

bool unset_env(std::string_view name, std::error_code& error) {
    error.clear();
    if (::unsetenv(std::string(name).c_str()) != 0) {
        error = {errno, std::system_category()};
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, std::string>> environment() {
    std::vector<std::pair<std::string, std::string>> variables;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view text(*entry);
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) {
            variables.emplace_back(std::string(text), std::string());
        } else {
            variables.emplace_back(std::string(text.substr(0, equals)), std::string(text.substr(equals + 1)));
        }
    }
    return variables;
}

#endif

} // namespace neura::platform

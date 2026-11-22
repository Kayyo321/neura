#include "dirs.hpp"

#include "internal.hpp"

#ifdef _WIN32
#include "windows.hpp"

#include <knownfolders.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <vector>
#endif

namespace fs = std::filesystem;

namespace neura::platform {

#ifdef _WIN32

namespace {

fs::path known_folder(REFKNOWNFOLDERID folder, std::error_code& error) {
    error.clear();
    PWSTR text = nullptr;
    const HRESULT result = ::SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &text);
    if (FAILED(result)) {
        if (text) ::CoTaskMemFree(text);
        error = std::error_code(HRESULT_CODE(result) != 0 ? HRESULT_CODE(result) : ERROR_PATH_NOT_FOUND,
                                std::system_category());
        return {};
    }
    fs::path path(text);
    ::CoTaskMemFree(text);
    return path;
}

using TempPath2 = DWORD(WINAPI*)(DWORD, LPWSTR);

} // namespace

fs::path local_app_data(std::error_code& error) {
    return known_folder(FOLDERID_LocalAppData, error);
}

fs::path home_directory(std::error_code& error) {
    return known_folder(FOLDERID_Profile, error);
}

fs::path temp_directory(std::error_code& error) {
    error.clear();
    // GetTempPath2W is newer than the oldest supported Windows 10, so it is
    // looked up instead of imported.
    static const auto temp_path2 = reinterpret_cast<TempPath2>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetTempPath2W")));
    wchar_t buffer[MAX_PATH + 2];
    const DWORD length = temp_path2 ? temp_path2(MAX_PATH + 2, buffer) : ::GetTempPathW(MAX_PATH + 2, buffer);
    if (length == 0 || length > MAX_PATH + 1) {
        error = detail::last_win32_error();
        return {};
    }
    std::wstring text(buffer, length);
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    return fs::path(text);
}

#else // POSIX

fs::path local_app_data(std::error_code& error) {
    error = std::make_error_code(std::errc::not_supported);
    return {};
}

fs::path home_directory(std::error_code& error) {
    error.clear();
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') return fs::path(home);
    long size = ::sysconf(_SC_GETPW_R_SIZE_MAX);
    std::vector<char> buffer(size > 0 ? static_cast<std::size_t>(size) : 16384);
    passwd entry{};
    passwd* found = nullptr;
    const int result = ::getpwuid_r(::geteuid(), &entry, buffer.data(), buffer.size(), &found);
    if (result != 0 || found == nullptr || found->pw_dir == nullptr) {
        error = std::error_code(result != 0 ? result : ENOENT, std::system_category());
        return {};
    }
    return fs::path(found->pw_dir);
}

fs::path temp_directory(std::error_code& error) {
    error.clear();
    auto path = fs::temp_directory_path(error);
    if (error) return {};
    // TMPDIR=/tmp/ names the same directory as /tmp.
    while (path.has_relative_path() && !path.has_filename()) path = path.parent_path();
    return path;
}

#endif

} // namespace neura::platform

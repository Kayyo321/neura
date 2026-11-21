#include "dynlib.hpp"

#include "error.hpp"
#include "internal.hpp"

#ifdef _WIN32
#include "environment.hpp"
#include "self.hpp"
#include "windows.hpp"

#include <psapi.h>

#include <algorithm>
#include <filesystem>
#else
#include <dlfcn.h>
#endif

namespace neura::platform {

Library::Library(Library&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

Library& Library::operator=(Library&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Library::~Library() {
    close();
}

#ifdef _WIN32

namespace fs = std::filesystem;

namespace {

constexpr DWORD beside_dependencies = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;

bool names_path(std::string_view text) noexcept {
    if (text.find_first_of("/\\") != std::string_view::npos) return true;
    return text.size() >= 2 && text[1] == ':' &&
           ((text[0] >= 'A' && text[0] <= 'Z') || (text[0] >= 'a' && text[0] <= 'z'));
}

std::string load_error(std::string_view text, DWORD code) {
    return std::string(text) + ": " + error_message(static_cast<int>(code));
}

// LoadLibrary adds ".dll" to a name without an extension; a trailing '.'
// asks for the file exactly as named.
std::wstring exact_file(const fs::path& path) {
    std::wstring text = path.native();
    if (!path.has_extension()) text += L'.';
    return text;
}

// The directories a bare name is looked for in (W19): the application
// directory, System32, then each absolute PATH entry. Relative entries
// would make the current directory a search location, so they are skipped.
std::vector<fs::path> search_directories() {
    std::vector<fs::path> directories;
    std::error_code error;
    const auto self = executable_path(error);
    if (!error) directories.push_back(self.parent_path());
    wchar_t system[MAX_PATH];
    const UINT length = ::GetSystemDirectoryW(system, MAX_PATH);
    if (length > 0 && length < MAX_PATH) directories.emplace_back(std::wstring(system, length));
    const auto path = get_env("PATH", error);
    if (path) {
        const auto wide = try_to_utf16(*path).value_or(std::wstring());
        std::size_t start = 0;
        while (start <= wide.size()) {
            const auto end = std::min(wide.find(L';', start), wide.size());
            std::wstring entry = wide.substr(start, end - start);
            if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"') {
                entry = entry.substr(1, entry.size() - 2);
            }
            const fs::path directory(entry);
            if (!entry.empty() && directory.is_absolute()) directories.push_back(directory);
            start = end + 1;
        }
    }
    return directories;
}

} // namespace

Library Library::open(std::string_view text, LibrarySearch search, std::string& error) {
    error.clear();
    const auto wide = try_to_utf16(text);
    if (!wide || wide->empty() || wide->find(L'\0') != std::wstring::npos) {
        error = std::string(text) + ": not a valid library name";
        return {};
    }
    if (names_path(text)) {
        std::error_code failure;
        const fs::path absolute = fs::absolute(fs::path(*wide).make_preferred(), failure);
        if (failure) {
            error = std::string(text) + ": " + error_message(failure);
            return {};
        }
        HMODULE module = ::LoadLibraryExW(exact_file(absolute).c_str(), nullptr, beside_dependencies);
        if (module == nullptr) {
            error = load_error(text, ::GetLastError());
            return {};
        }
        return Library(module);
    }
    if (search == LibrarySearch::system) {
        HMODULE module = ::LoadLibraryExW(wide->c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr) {
            error = load_error(text, ::GetLastError());
            return {};
        }
        return Library(module);
    }
    std::vector<std::wstring> names{*wide};
    if (!fs::path(*wide).has_extension()) names.push_back(*wide + L".dll");
    for (const auto& directory : search_directories()) {
        for (const auto& name : names) {
            const fs::path candidate = directory / name;
            std::error_code ignored;
            if (!fs::is_regular_file(candidate, ignored)) continue;
            HMODULE module = ::LoadLibraryExW(exact_file(candidate).c_str(), nullptr, beside_dependencies);
            if (module == nullptr) {
                error = load_error(text, ::GetLastError());
                return {};
            }
            return Library(module);
        }
    }
    error = load_error(text, ERROR_MOD_NOT_FOUND);
    return {};
}

void* Library::symbol(std::string_view name, std::string* error) const {
    if (handle_ == nullptr) {
        if (error) *error = std::string(name) + ": no library is open";
        return nullptr;
    }
    void* address = module_symbol(handle_, name);
    if (address == nullptr && error) {
        *error = std::string(name) + ": " + error_message(static_cast<int>(::GetLastError()));
    }
    return address;
}

void Library::close() noexcept {
    if (handle_ != nullptr) ::FreeLibrary(static_cast<HMODULE>(handle_));
    handle_ = nullptr;
}

void* module_symbol(void* module, std::string_view name) noexcept {
    try {
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(module), std::string(name).c_str()));
    } catch (...) {
        return nullptr;
    }
}

std::vector<void*> loaded_modules() {
    std::vector<HMODULE> modules(256);
    for (;;) {
        DWORD needed = 0;
        const DWORD bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
        if (!::EnumProcessModulesEx(::GetCurrentProcess(), modules.data(), bytes, &needed, LIST_MODULES_DEFAULT)) {
            return {};
        }
        if (needed <= bytes) {
            modules.resize(needed / sizeof(HMODULE));
            break;
        }
        modules.resize(needed / sizeof(HMODULE) + 16);
    }
    return std::vector<void*>(modules.begin(), modules.end());
}

#else // POSIX

Library Library::open(std::string_view text, LibrarySearch, std::string& error) {
    error.clear();
    void* handle = ::dlopen(std::string(text).c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* message = ::dlerror();
        error = message ? message : std::string(text) + ": cannot load library";
        return {};
    }
    return Library(handle);
}

void* Library::symbol(std::string_view name, std::string* error) const {
    if (handle_ == nullptr) {
        if (error) *error = std::string(name) + ": no library is open";
        return nullptr;
    }
    ::dlerror();
    void* address = ::dlsym(handle_, std::string(name).c_str());
    if (address == nullptr && error) {
        const char* message = ::dlerror();
        *error = message ? message : std::string(name) + ": symbol not found";
    }
    return address;
}

void Library::close() noexcept {
    if (handle_ != nullptr) ::dlclose(handle_);
    handle_ = nullptr;
}

void* module_symbol(void* module, std::string_view name) noexcept {
    try {
        return ::dlsym(module, std::string(name).c_str());
    } catch (...) {
        return nullptr;
    }
}

std::vector<void*> loaded_modules() {
    return {};
}

#endif

} // namespace neura::platform

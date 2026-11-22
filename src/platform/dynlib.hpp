#pragma once

// Dynamic libraries: dlopen, or LoadLibraryExW with the search rules of W19.
// Tools also call SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
// at start, so no implicit load searches the current directory or PATH.

#include <string>
#include <string_view>
#include <vector>

namespace neura::platform {

enum class LibrarySearch {
    // Std.DynamicForeign rules (W19). On Windows text with '/', '\\' or a
    // drive prefix is a path and loads that file (LoadLibraryExW with
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS, so
    // its own dependencies are found beside it); a bare name is looked for in
    // the application directory, System32, then each PATH entry, each by full
    // path, and ".dll" is tried after the name itself when it has no
    // extension. Any extension is accepted. POSIX: dlopen(text, RTLD_NOW |
    // RTLD_LOCAL).
    foreign,
    // A library of the operating system (vulkan-1.dll): System32 only on
    // Windows; dlopen(text, RTLD_NOW | RTLD_LOCAL) on POSIX.
    system,
};

class Library {
public:
    Library() noexcept = default;
    Library(Library&& other) noexcept;
    Library& operator=(Library&& other) noexcept;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    ~Library();

    // Loads `text` (UTF-8). On failure returns a closed Library and sets
    // `error` to the loader's message (dlerror(), or the Windows message
    // naming the file).
    static Library open(std::string_view text, LibrarySearch search, std::string& error);

    bool is_open() const noexcept { return handle_ != nullptr; }
    explicit operator bool() const noexcept { return is_open(); }
    // The HMODULE or dlopen handle.
    void* native_handle() const noexcept { return handle_; }

    // The address of `name`, or nullptr (with `error` set when given).
    void* symbol(std::string_view name, std::string* error = nullptr) const;

    void close() noexcept;

private:
    explicit Library(void* handle) noexcept : handle_(handle) {}
    void* handle_ = nullptr;
};

// The address of `name` in a module handle that this Library does not own
// (an entry of loaded_modules(), or RTLD_DEFAULT on POSIX).
void* module_symbol(void* module, std::string_view name) noexcept;

// The modules loaded in this process, in EnumProcessModulesEx order (the
// executable first). Empty on POSIX, where dlsym(RTLD_DEFAULT) searches them.
std::vector<void*> loaded_modules();

} // namespace neura::platform

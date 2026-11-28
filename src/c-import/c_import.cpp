#include "c_import.hpp"

#include "libclang_loader.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>

#ifdef __APPLE__
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

#ifdef _WIN32
#include "platform/windows.hpp"

#include <cstddef>
#include <cwchar>
#include <fstream>
#endif

namespace neura {
namespace {

namespace fs = std::filesystem;

// The text of a path as libclang takes it, and as generated C and Neura
// source and messages hold it: UTF-8 on every host. On Windows libclang reads
// file names as UTF-8, while path::string() would use the process code page.
std::string utf8_text(const fs::path& path) {
#ifdef _WIN32
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
#else
    return path.string();
#endif
}

// The same in generic form, with '/' separators.
std::string utf8_generic(const fs::path& path) {
#ifdef _WIN32
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
#else
    return path.generic_string();
#endif
}

// A path from UTF-8 text, such as a file name libclang reports.
fs::path utf8_path(const std::string& text) {
#ifdef _WIN32
    return fs::path(std::u8string(text.begin(), text.end()));
#else
    return fs::path(text);
#endif
}

// Names a Neura program cannot use for a declaration: the keywords and the
// built-in type names. A C name that collides gets a trailing underscore.
constexpr auto reserved = std::to_array<std::string_view>({
    "let", "const", "export", "override", "func", "module", "class", "import", "new", "destroy", "if", "else",
    "while", "do", "for", "in", "break", "continue", "return", "true", "false", "as", "is", "nil", "enum",
    "match", "and", "or", "interface", "implements", "promote", "static", "singleton", "not", "typeof", "this", "int", "int8",
    "int16", "int32", "int64", "uint", "uint8", "uint16", "uint32", "uint64", "float16", "bfloat16",
    "float32", "float64", "float", "bool", "string", "void", "unit", "host", "callback", "init",
});

std::string neura_name(const std::string& name) {
    return std::ranges::find(reserved, name) != reserved.end() ? name + "_" : name;
}

// The process's libclang, loaded on the first C import.
NeuraLibclang& libclang(const CImportOptions& options, const Span& site) {
    static std::unique_ptr<NeuraLibclang> loaded;
    static std::string failure;
    if (loaded) return *loaded;
    auto library = std::make_unique<NeuraLibclang>();
    if (!neura_libclang_load(library.get(), options.libclang.empty() ? nullptr : options.libclang.c_str())) {
        failure = library->error;
#ifdef _WIN32
        throw Diagnostic(site, "C imports need libclang, which could not be loaded: " + failure +
                                   "; install LLVM (winget install LLVM.LLVM) or the C++ Clang tools of Visual "
                                   "Studio, or name libclang.dll with --libclang or the NEURA_LIBCLANG "
                                   "environment variable")
            .with_code("E0708");
#else
        throw Diagnostic(site, "C imports need libclang, which could not be loaded: " + failure +
                                   "; install libclang (for example the libclang1-19 package), or name the "
                                   "library with --libclang or the NEURA_LIBCLANG environment variable")
            .with_code("E0708");
#endif
    }
    loaded = std::move(library);
    return *loaded;
}

#ifdef __APPLE__
// What `xcrun --sdk macosx --show-sdk-path` prints, run without a shell; empty
// if xcrun is missing, fails, or prints nothing.
std::string xcrun_sdk_path() {
    int pipe_ends[2];
    if (::pipe(pipe_ends) != 0) return {};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&actions, pipe_ends[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_ends[1]);
    char program[] = "xcrun";
    char sdk_flag[] = "--sdk";
    char sdk_name[] = "macosx";
    char query[] = "--show-sdk-path";
    char* argv[] = {program, sdk_flag, sdk_name, query, nullptr};
    pid_t child = 0;
    const int spawned = ::posix_spawnp(&child, "xcrun", &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_ends[1]);
    std::string output;
    if (spawned == 0) {
        char buffer[512];
        for (;;) {
            const auto count = ::read(pipe_ends[0], buffer, sizeof buffer);
            if (count <= 0) break;
            output.append(buffer, static_cast<std::size_t>(count));
            if (output.size() > 4096) break;
        }
    }
    ::close(pipe_ends[0]);
    if (spawned != 0) return {};
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    return output;
}

// The macOS SDK that holds the C headers (ForeignInterop.md, C imports): the
// SDKROOT environment variable, then the SDKs of the developer directory and
// of the Command Line Tools, and only then `xcrun`.
fs::path macos_sdk() {
    static const fs::path sdk = [] {
        const auto usable = [](const fs::path& candidate) {
            std::error_code ignored;
            return !candidate.empty() && fs::is_regular_file(candidate / "usr/include/stdlib.h", ignored);
        };
        const auto environment = [](const char* name) {
            const char* value = std::getenv(name);
            return value != nullptr ? std::string(value) : std::string{};
        };
        constexpr auto platform_sdk = "Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
        std::vector<fs::path> candidates;
        if (const auto root = environment("SDKROOT"); !root.empty()) candidates.emplace_back(root);
        if (const auto developer = environment("DEVELOPER_DIR"); !developer.empty())
            candidates.push_back(fs::path(developer) / platform_sdk);
        candidates.emplace_back("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk");
        candidates.push_back(fs::path("/Applications/Xcode.app/Contents/Developer") / platform_sdk);
        for (const auto& candidate : candidates)
            if (usable(candidate)) return candidate;
        if (const auto printed = xcrun_sdk_path(); usable(printed)) return fs::path(printed);
        return fs::path{};
    }();
    return sdk;
}
#endif

#ifdef _WIN32
// The value of an environment variable, read from the UTF-16 environment;
// empty when it is not set.
std::wstring environment_value(const wchar_t* name) {
    const wchar_t* value = ::_wgetenv(name);
    return value != nullptr ? std::wstring(value) : std::wstring{};
}

// Whether a directory names a fixed location: a drive root (C:\ or C:/) or a
// UNC path. A relative entry, or a drive-relative one such as C:include,
// would depend on the current directory.
bool fixed_directory(const std::wstring& text) {
    const auto separator = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    if (text.size() >= 2 && separator(text[0]) && separator(text[1])) return true;
    const bool drive = text.size() >= 3 && ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z'));
    return drive && text[1] == L':' && separator(text[2]);
}

// The directories of a list such as INCLUDE: separated by ';', surrounding
// quotes removed, empty and relative entries left out.
void append_directory_list(std::vector<fs::path>& directories, const std::wstring& list) {
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto end = std::min(list.find(L';', start), list.size());
        auto entry = list.substr(start, end - start);
        if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"') entry = entry.substr(1, entry.size() - 2);
        if (fixed_directory(entry)) directories.emplace_back(entry);
        start = end + 1;
    }
}

// The first line `vswhere` prints for `arguments`, run without a shell and
// without a window, with only its output pipe and NUL inherited; empty when
// vswhere is missing, fails, prints nothing or more than 64 KiB, or runs for
// longer than 30 seconds (it is then ended), so that a vswhere that hangs,
// as during an update of the Visual Studio installer, cannot hang an import.
// vswhere is installed with Visual Studio 2017 and later at
// %ProgramFiles(x86)%\Microsoft Visual Studio\Installer.
//
// This is raw Win32 rather than platform::run_capture because neura_compiler
// does not link neura_platform.
std::wstring vswhere(const std::wstring& arguments) {
    constexpr ULONGLONG timeout_ms = 30000;
    constexpr std::size_t cap = 65536;
    const auto root = environment_value(L"ProgramFiles(x86)");
    std::error_code ignored;
    if (!fixed_directory(root)) return {};
    const auto program = fs::path(root) / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
    if (!fs::is_regular_file(program, ignored)) return {};
    std::wstring command = L"\"" + program.native() + L"\" " + arguments;

    SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!::CreatePipe(&read_end, &write_end, &inheritable, 0)) return {};
    ::SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE null = ::CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                OPEN_EXISTING, 0, nullptr);
    PROCESS_INFORMATION process{};
    BOOL started = FALSE;
    SIZE_T size = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<std::byte> storage(size);
    const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (null != INVALID_HANDLE_VALUE && size > 0 && ::InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
        HANDLE inherited[] = {write_end, null};
        if (::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited,
                                        nullptr, nullptr)) {
            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof startup;
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            startup.StartupInfo.hStdInput = null;
            startup.StartupInfo.hStdOutput = write_end;
            startup.StartupInfo.hStdError = null;
            startup.lpAttributeList = attributes;
            started = ::CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, TRUE,
                                       EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                                       &startup.StartupInfo, &process);
        }
        ::DeleteProcThreadAttributeList(attributes);
    }
    // Only the child holds the write end now.
    ::CloseHandle(write_end);
    if (null != INVALID_HANDLE_VALUE) ::CloseHandle(null);
    std::string output;
    DWORD status = 1;
    if (started) {
        // Drain the pipe while waiting, so that vswhere never blocks on a full
        // pipe, and read only what PeekNamedPipe reports, so that no read
        // blocks past the deadline. The pipe is drained once more after the
        // exit; then PeekNamedPipe fails with ERROR_BROKEN_PIPE.
        const auto deadline = ::GetTickCount64() + timeout_ms;
        bool exited = false;
        bool failed = false;
        for (;;) {
            DWORD available = 0;
            if (!::PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr)) available = 0;
            if (available > 0) {
                char buffer[512];
                DWORD count = 0;
                if (!::ReadFile(read_end, buffer, std::min<DWORD>(available, sizeof buffer), &count, nullptr)) {
                    failed = true;
                    break;
                }
                if (output.size() + count > cap) {
                    failed = true;
                    break;
                }
                output.append(buffer, count);
                continue;
            }
            if (exited) break;
            const auto now = ::GetTickCount64();
            if (now >= deadline) {
                failed = true;
                break;
            }
            exited = ::WaitForSingleObject(process.hProcess, static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 20))) ==
                     WAIT_OBJECT_0;
        }
        if (!exited) {
            ::TerminateProcess(process.hProcess, 1);
            ::WaitForSingleObject(process.hProcess, 5000);
        } else if (!failed) {
            ::GetExitCodeProcess(process.hProcess, &status);
        }
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    }
    ::CloseHandle(read_end);
    if (status != 0) return {};
    output.resize(std::min(output.find_first_of("\r\n"), output.size()));
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, output.data(),
                                             static_cast<int>(output.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring line(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, output.data(), static_cast<int>(output.size()), line.data(),
                          length);
    return line;
}

// The C headers of the MSVC tools: the default tools version of the newest
// Visual Studio 2017 or later that has them.
std::vector<fs::path> msvc_include_directories() {
    const auto installation =
        vswhere(L"-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
                L"-property installationPath -utf8");
    if (!fixed_directory(installation)) return {};
    const fs::path vc = fs::path(installation) / L"VC";
    std::ifstream file(vc / L"Auxiliary" / L"Build" / L"Microsoft.VCToolsVersion.default.txt", std::ios::binary);
    std::string version;
    std::getline(file, version);
    while (!version.empty() && (version.back() == '\r' || version.back() == ' ' || version.back() == '\t'))
        version.pop_back();
    if (version.empty() || version.find_first_not_of("0123456789.") != std::string::npos) return {};
    const auto include = vc / L"Tools" / L"MSVC" / fs::path(version) / L"include";
    std::error_code ignored;
    if (!fs::is_directory(include, ignored)) return {};
    return {include};
}

// The numbers of a version such as 10.0.26100.0, or nothing for another name.
std::optional<std::vector<unsigned long>> version_numbers(const std::wstring& name) {
    std::vector<unsigned long> numbers;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find(L'.', start), name.size());
        if (end == start || end - start > 9) return std::nullopt;
        unsigned long number = 0;
        for (auto i = start; i < end; ++i) {
            if (name[i] < L'0' || name[i] > L'9') return std::nullopt;
            number = number * 10 + static_cast<unsigned long>(name[i] - L'0');
        }
        numbers.push_back(number);
        start = end + 1;
    }
    return numbers;
}

// The C headers of the newest Windows 10 or 11 SDK that has the Universal C
// Runtime's (ucrt), from the installation root the SDK records in the
// registry: its ucrt, shared, um, winrt, and cppwinrt directories.
std::vector<fs::path> windows_sdk_include_directories() {
    wchar_t root[1024];
    DWORD size = sizeof root;
    if (::RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10",
                       RRF_RT_REG_SZ, nullptr, root, &size) != ERROR_SUCCESS ||
        !fixed_directory(root))
        return {};
    std::error_code error;
    std::optional<std::vector<unsigned long>> newest;
    fs::path chosen;
    for (auto entry = fs::directory_iterator(fs::path(root) / L"Include", error);
         !error && entry != fs::directory_iterator(); entry.increment(error)) {
        const auto numbers = version_numbers(entry->path().filename().native());
        std::error_code ignored;
        if (!numbers || numbers->front() != 10 || !fs::is_regular_file(entry->path() / L"ucrt" / L"stdlib.h", ignored))
            continue;
        if (!newest || *numbers > *newest) {
            newest = numbers;
            chosen = entry->path();
        }
    }
    std::vector<fs::path> directories;
    if (chosen.empty()) return directories;
    for (const auto* name : {L"ucrt", L"shared", L"um", L"winrt", L"cppwinrt"}) {
        std::error_code ignored;
        if (fs::is_directory(chosen / name, ignored)) directories.push_back(chosen / name);
    }
    return directories;
}
#endif

// libclang finds its builtin headers (stddef.h, stdarg.h) relative to where it
// is installed; Debian's layout needs the directory named.
std::string clang_resource_dir(const NeuraLibclang& lib) {
    const auto major = std::to_string(lib.major);
#ifdef _WIN32
    // An LLVM or Visual Studio installation keeps libclang.dll in bin and the
    // builtin headers in lib\clang\<version>, which LLVM 16 and later name by
    // the major version and older releases by the full one (15.0.7). Take the
    // directory of the major version, else the highest of that major version
    // (of any, when the version is unknown) that has the headers.
    if (lib.path[0] == '\0') return {};
    const auto clang = utf8_path(lib.path).parent_path().parent_path() / L"lib" / L"clang";
    std::error_code ignored;
    if (lib.major != 0 && fs::exists(clang / major / L"include" / L"stddef.h", ignored)) return utf8_text(clang / major);
    std::optional<std::vector<unsigned long>> newest;
    fs::path chosen;
    std::error_code error;
    for (auto entry = fs::directory_iterator(clang, error); !error && entry != fs::directory_iterator();
         entry.increment(error)) {
        const auto numbers = version_numbers(entry->path().filename().native());
        if (!numbers || (lib.major != 0 && numbers->front() != static_cast<unsigned long>(lib.major)) ||
            !fs::exists(entry->path() / L"include" / L"stddef.h", ignored))
            continue;
        if (!newest || *numbers > *newest) {
            newest = numbers;
            chosen = entry->path();
        }
    }
    return chosen.empty() ? std::string{} : utf8_text(chosen);
#else
    std::vector<fs::path> candidates{fs::path("/usr/lib/llvm-" + major) / "lib/clang" / major,
                                     fs::path("/usr/lib/clang") / major, fs::path("/usr/local/lib/clang") / major};
    if (lib.path[0] != '\0') candidates.push_back(fs::path(lib.path).parent_path() / "clang" / major);
    for (const auto& candidate : candidates) {
        std::error_code ignored;
        if (fs::exists(candidate / "include/stddef.h", ignored)) return candidate.string();
    }
#ifdef __APPLE__
    // Apple's toolchains name the directory by their own version, which an
    // older Xcode writes in full (15.0.0). Take the highest one that has the
    // builtin headers, beside the libclang that was loaded.
    if (lib.path[0] != '\0') {
        std::error_code ignored;
        std::vector<fs::path> found;
        for (const auto& entry : fs::directory_iterator(fs::path(lib.path).parent_path() / "clang", ignored)) {
            if (fs::exists(entry.path() / "include/stddef.h", ignored)) found.push_back(entry.path());
        }
        std::ranges::sort(found, [](const fs::path& left, const fs::path& right) {
            return left.filename().string() < right.filename().string();
        });
        if (!found.empty()) return found.back().string();
    }
#endif
    return {};
#endif
}

// Whether `text` ends with `suffix`, which is not longer. Windows file names
// do not depend on case, so there ASCII letters compare without it.
bool same_suffix(const std::string& text, const std::string& suffix) {
    const auto start = text.size() - suffix.size();
#ifdef _WIN32
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (std::size_t i = 0; i < suffix.size(); ++i)
        if (lower(text[start + i]) != lower(suffix[i])) return false;
    return true;
#else
    return text.compare(start, suffix.size(), suffix) == 0;
#endif
}

// Whether an included file named `name` can hold declarations of the public
// header that includes it (ForeignInterop.md, C imports): its name begins
// with an underscore, as in Apple's and glibc's C libraries. On Windows a name
// that begins with corecrt_ or vcruntime_ counts too: the Universal C Runtime
// keeps the declarations of stdlib.h, stdio.h, string.h and the other
// standard headers in such files (corecrt_malloc.h, corecrt_search.h,
// corecrt_wstdio.h), and the Visual C++ runtime those of string.h's memory
// and search functions, memcpy, memset and strchr among them, in
// vcruntime_string.h. corecrt.h and vcruntime.h themselves, which every one
// of these headers includes, do not count.
bool declaration_file(const std::string& name) {
#ifdef _WIN32
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    for (const std::string_view prefix : {std::string_view("corecrt_"), std::string_view("vcruntime_")})
        if (name.size() > prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), name.begin(), [&](char a, char b) { return a == lower(b); }))
            return true;
#endif
    return name.rfind('_', 0) == 0;
}

#ifdef _WIN32
// Whether `file`, a file name libclang reports, is a header of the Universal
// C Runtime: one in a ucrt directory among the system header directories.
bool ucrt_file(const std::string& file) {
    if (file.empty()) return false;
    const auto directory = utf8_path(file).parent_path();
    return ::_wcsicmp(directory.filename().c_str(), L"ucrt") == 0 && is_system_header_directory(directory);
}
#endif

class Importer {
public:
    Importer(NeuraLibclang& lib, fs::path header, bool linked, std::string module_name, const CImportOptions& options,
             const Span& site)
        : lib_(lib), header_(std::move(header)), linked_(linked), module_name_(std::move(module_name)),
          options_(options), site_(site) {}

    CImportResult run() {
        arguments_ = {"-x", "c", "-std=gnu11"};
        if (!is_system_header_directory(header_.parent_path()))
            arguments_.push_back("-I" + utf8_text(header_.parent_path()));
        for (const auto& dir : options_.include_dirs) arguments_.push_back("-I" + dir);
        for (const auto& define : options_.defines) arguments_.push_back("-D" + define);
        if (const auto resource = resource_dir(); !resource.empty()) {
            arguments_.push_back("-resource-dir");
            arguments_.push_back(resource);
        }
#ifdef __APPLE__
        // The C headers are in the SDK, which libclang does not look for.
        if (const auto sdk = macos_sdk(); !sdk.empty()) {
            arguments_.push_back("-isysroot");
            arguments_.push_back(sdk.string());
        }
#endif
#ifdef _WIN32
        // The headers of the MSVC tools and the Windows SDK are read from the
        // directories the header search used (system_header_directories),
        // after libclang's builtin headers, rather than from those libclang
        // would find on its own.
        if (const auto system = system_header_directories(); !system.empty()) {
            arguments_.push_back("-nostdlibinc");
            for (const auto& directory : system) {
                arguments_.push_back("-idirafter");
                arguments_.push_back(utf8_text(directory));
            }
        }
#endif
        index_ = lib_.clang_createIndex(0, 0);
        unit_ = parse(utf8_text(header_), nullptr);
        const auto root = lib_.clang_getTranslationUnitCursor(unit_);
        // The #include directives, which say what files hold this header's declarations.
        visit(root, [&](CXCursor cursor) {
            if (lib_.clang_getCursorKind(cursor) == CXCursor_InclusionDirective)
                inclusions_.emplace_back(file_of(cursor), text(lib_.clang_getCursorSpelling(cursor)));
        });
        // First pass: the typedef names of records, preferred over tags.
        visit(root, [&](CXCursor cursor) {
            if (lib_.clang_getCursorKind(cursor) == CXCursor_TypedefDecl && in_header(cursor)) {
                const auto underlying = lib_.clang_getCanonicalType(lib_.clang_getTypedefDeclUnderlyingType(cursor));
                if (underlying.kind == CXType_Record) {
                    const auto declaration = lib_.clang_getTypeDeclaration(underlying);
                    typedef_names_.emplace(key(declaration), text(lib_.clang_getCursorSpelling(cursor)));
                }
            }
        });
        visit(root, [&](CXCursor cursor) { declaration(cursor); });
        macros();
        lib_.clang_disposeTranslationUnit(unit_);
        lib_.clang_disposeIndex(index_);
        return finish();
    }

private:
    struct Scalar {
        std::string carrier;  // A ValueLayout constant and MemoryLayout.Carrier variant.
        std::string type;     // The Neura type.
        long long size = 0;
        long long align = 0;
    };

    struct Mapped {
        enum class Kind { Void, Scalar, Pointer, Struct, Unsupported } kind = Kind::Unsupported;
        Scalar scalar;
        std::string record;  // A struct's Neura class name.
        std::string reason;
        bool c_string = false;
    };

    struct Field {
        std::string name;
        Mapped type;
        long long offset = 0;
        long long size = 0;
        long long count = -1;  // The element count of an array field, or -1.
    };

    struct Record {
        std::string name;
        bool supported = false;
        std::string reason;
        std::vector<Field> fields;
        long long size = 0;
    };

    // --- libclang helpers ---------------------------------------------------

    std::string text(CXString string) {
        const char* chars = lib_.clang_getCString(string);
        std::string result = chars == nullptr ? std::string{} : std::string(chars);
        lib_.clang_disposeString(string);
        return result;
    }

    template <typename Visit>
    void visit(CXCursor parent, Visit visit_child) {
        lib_.clang_visitChildren(
            parent,
            [](CXCursor cursor, CXCursor, CXClientData data) {
                (*static_cast<Visit*>(data))(cursor);
                return CXChildVisit_Continue;
            },
            &visit_child);
    }

    std::string file_of(CXCursor cursor) {
        CXFile file = nullptr;
        unsigned line = 0, column = 0, offset = 0;
        lib_.clang_getSpellingLocation(lib_.clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        if (file == nullptr) return {};
        return text(lib_.clang_getFileName(file));
    }

    // Whether `file` is the imported header, or a header whose name begins
    // with an underscore that the imported header includes, directly or
    // through other such headers (ForeignInterop.md, C imports). C libraries
    // keep the declarations of a public header in such files. libclang names
    // an included file only as the directive spelled it, so a directive
    // matches the file whose path ends with that spelling.
    bool is_header_file(const std::string& file, std::set<std::string>& visiting) {
        if (const auto known = header_files_.find(file); known != header_files_.end()) return known->second;
        std::error_code ignored;
        const auto path = utf8_path(file);
        // The same file under another spelling counts, such as through a
        // symbolic link in an SDK path.
        if (fs::weakly_canonical(path, ignored) == fs::weakly_canonical(header_, ignored) ||
            fs::equivalent(path, header_, ignored))
            return header_files_[file] = true;
        if (!declaration_file(utf8_text(path.filename()))) return header_files_[file] = false;
        if (!visiting.insert(file).second) return false;
        const auto text_path = utf8_generic(path);
        bool found = false;
        for (const auto& [includer, spelled] : inclusions_) {
            fs::path tail;
            for (const auto& part : utf8_path(spelled).lexically_normal())
                if (part != ".." && part != ".") tail /= part;
            const auto wanted = "/" + utf8_generic(tail);
            if (tail.empty() || text_path.size() < wanted.size() || !same_suffix(text_path, wanted))
                continue;
            if (is_header_file(includer, visiting)) {
                found = true;
                break;
            }
        }
        visiting.erase(file);
        // A result reached while another file is being judged may rest on a
        // cycle guard; only the outermost judgement is kept.
        if (visiting.empty()) header_files_[file] = found;
        return found;
    }

    bool in_header(CXCursor cursor) {
        const auto file = file_of(cursor);
        if (file.empty()) return false;
        std::set<std::string> visiting;
        return is_header_file(file, visiting);
    }

    std::string key(CXCursor cursor) {
        CXFile file = nullptr;
        unsigned line = 0, column = 0, offset = 0;
        lib_.clang_getSpellingLocation(lib_.clang_getCursorLocation(cursor), &file, &line, &column, &offset);
        return (file == nullptr ? std::string{} : text(lib_.clang_getFileName(file))) + ":" + std::to_string(offset) +
               ":" + text(lib_.clang_getCursorSpelling(cursor));
    }

    std::string resource_dir() const { return clang_resource_dir(lib_); }

    CXTranslationUnit parse(const std::string& file, CXUnsavedFile* unsaved, bool report = true) {
        std::vector<const char*> argv;
        for (const auto& argument : arguments_) argv.push_back(argument.c_str());
        CXTranslationUnit unit = nullptr;
        const auto error = lib_.clang_parseTranslationUnit2(index_, file.c_str(), argv.data(),
                                                            static_cast<int>(argv.size()), unsaved, unsaved ? 1 : 0,
                                                            NEURA_LIBCLANG_HEADER_PARSE_OPTIONS, &unit);
        if (error != CXError_Success || unit == nullptr)
            throw Diagnostic(site_, "cannot parse C header " + utf8_text(header_)).with_code("E0708");
        if (report) {
            for (unsigned i = 0; i < lib_.clang_getNumDiagnostics(unit); ++i) {
                auto diagnostic = lib_.clang_getDiagnostic(unit, i);
                const auto severity = lib_.clang_getDiagnosticSeverity(diagnostic);
                if (severity >= CXDiagnostic_Error) {
                    const auto message =
                        text(lib_.clang_formatDiagnostic(diagnostic, lib_.clang_defaultDiagnosticDisplayOptions()));
                    lib_.clang_disposeDiagnostic(diagnostic);
                    lib_.clang_disposeTranslationUnit(unit);
                    throw Diagnostic(site_, "C import: " + message).with_code("E0708");
                }
                lib_.clang_disposeDiagnostic(diagnostic);
            }
        }
        return unit;
    }

    // --- Types ----------------------------------------------------------------

    static Mapped unsupported(std::string reason) {
        Mapped mapped;
        mapped.reason = std::move(reason);
        return mapped;
    }

    Mapped scalar(std::string carrier, std::string type, CXType ctype) {
        Mapped mapped;
        mapped.kind = Mapped::Kind::Scalar;
        mapped.scalar = {std::move(carrier), std::move(type), lib_.clang_Type_getSizeOf(ctype),
                         lib_.clang_Type_getAlignOf(ctype)};
        return mapped;
    }

    Mapped integer(CXType ctype, bool is_signed) {
        switch (lib_.clang_Type_getSizeOf(ctype)) {
        case 1: return is_signed ? scalar("Int8", "int8", ctype) : scalar("UInt8", "uint8", ctype);
        case 2: return is_signed ? scalar("Int16", "int16", ctype) : scalar("UInt16", "uint16", ctype);
        case 4: return is_signed ? scalar("Int32", "int32", ctype) : scalar("UInt32", "uint32", ctype);
        case 8: return is_signed ? scalar("Int64", "int64", ctype) : scalar("UInt64", "uint64", ctype);
        default: return unsupported("an integer type of " + std::to_string(lib_.clang_Type_getSizeOf(ctype)) +
                                    " bytes");
        }
    }

    Mapped map(CXType type) {
        const auto canonical = lib_.clang_getCanonicalType(type);
        switch (canonical.kind) {
        case CXType_Void: {
            Mapped mapped;
            mapped.kind = Mapped::Kind::Void;
            return mapped;
        }
        case CXType_Bool: return scalar("Bool", "bool", canonical);
        case CXType_Char_S:
        case CXType_SChar:
        case CXType_Short:
        case CXType_Int:
        case CXType_Long:
        case CXType_LongLong: return integer(canonical, true);
        // wchar_t is an unsigned 16-bit type on Windows (ForeignInterop.md,
        // C imports); a signed one elsewhere.
#ifdef _WIN32
        case CXType_WChar: return integer(canonical, false);
#else
        case CXType_WChar: return integer(canonical, true);
#endif
        case CXType_Char_U:
        case CXType_UChar:
        case CXType_UShort:
        case CXType_UInt:
        case CXType_ULong:
        case CXType_ULongLong:
        case CXType_Char16:
        case CXType_Char32: return integer(canonical, false);
        case CXType_Float: return scalar("Float32", "float32", canonical);
        case CXType_Double: return scalar("Float64", "float64", canonical);
        case CXType_BlockPointer:
            // A block is an object, not a code address: a function stub
            // cannot stand in for one.
            return unsupported("a block pointer");
        case CXType_Pointer: {
            Mapped mapped;
            mapped.kind = Mapped::Kind::Pointer;
            const auto pointee = lib_.clang_getPointeeType(canonical);
            const auto pointee_kind = lib_.clang_getCanonicalType(pointee).kind;
            mapped.c_string = (pointee_kind == CXType_Char_S || pointee_kind == CXType_Char_U) &&
                              lib_.clang_isConstQualifiedType(pointee);
            return mapped;
        }
        case CXType_Enum:
            return map(lib_.clang_getEnumDeclIntegerType(lib_.clang_getTypeDeclaration(canonical)));
        case CXType_Record: {
            const auto declaration = lib_.clang_getTypeDeclaration(canonical);
            if (lib_.clang_getCursorKind(declaration) == CXCursor_UnionDecl)
                return unsupported("a union passed by value");
            const auto& found = record(declaration);
            if (!found.supported) return unsupported("struct " + found.name + ": " + found.reason);
            Mapped mapped;
            mapped.kind = Mapped::Kind::Struct;
            mapped.record = found.name;
            return mapped;
        }
        case CXType_LongDouble: return unsupported("long double");
        case CXType_Complex: return unsupported("_Complex");
        case CXType_Int128:
        case CXType_UInt128: return unsupported("a 128-bit integer");
        default: return unsupported("the C type " + text(lib_.clang_getTypeSpelling(type)));
        }
    }

    std::string record_name(CXCursor declaration) {
        if (const auto named = typedef_names_.find(key(declaration)); named != typedef_names_.end())
            return named->second;
        return text(lib_.clang_getCursorSpelling(declaration));
    }

    const Record& record(CXCursor declaration) {
        const auto definition = lib_.clang_getCursorDefinition(declaration);
        const auto cursor = lib_.clang_Cursor_isNull(definition) ? declaration : definition;
        const auto id = key(cursor);
        if (const auto found = records_.find(id); found != records_.end()) return found->second;
        auto& result = records_[id];
        result.name = neura_name(record_name(cursor));
        if (lib_.clang_Cursor_isNull(definition)) {
            result.reason = "its definition is not visible";
            return result;
        }
        if (result.name.empty() || lib_.clang_Cursor_isAnonymous(cursor)) {
            result.reason = "an anonymous struct";
            return result;
        }
        const auto type = lib_.clang_getCursorType(cursor);
        result.size = lib_.clang_Type_getSizeOf(type);
        std::vector<Field> fields;
        std::string reason;
        long long position = 0;
        long long alignment = 1;
        visit(cursor, [&](CXCursor child) {
            if (!reason.empty() || lib_.clang_getCursorKind(child) != CXCursor_FieldDecl) return;
            Field field;
            field.name = neura_name(text(lib_.clang_getCursorSpelling(child)));
            if (lib_.clang_Cursor_isBitField(child)) {
                reason = "bit-field " + field.name;
                return;
            }
            const auto field_type = lib_.clang_getCanonicalType(lib_.clang_getCursorType(child));
            auto element_type = field_type;
            if (field_type.kind == CXType_ConstantArray) {
                field.count = lib_.clang_getArraySize(field_type);
                element_type = lib_.clang_getArrayElementType(field_type);
            } else if (field_type.kind == CXType_IncompleteArray) {
                reason = "flexible array member " + field.name;
                return;
            }
            field.type = map(element_type);
            if (field.type.kind == Mapped::Kind::Unsupported || field.type.kind == Mapped::Kind::Void) {
                reason = "field " + field.name + " has " + field.type.reason;
                return;
            }
            if (field.count >= 0 && field.type.kind == Mapped::Kind::Struct) {
                reason = "field " + field.name + " is an array of structs";
                return;
            }
            field.offset = lib_.clang_Cursor_getOffsetOfField(child) / 8;
            field.size = lib_.clang_Type_getSizeOf(element_type);
            const auto field_align = lib_.clang_Type_getAlignOf(element_type);
            position = (position + field_align - 1) / field_align * field_align;
            if (position != field.offset) {
                reason = "a packed or explicitly aligned layout";
                return;
            }
            position += field.size * (field.count >= 0 ? field.count : 1);
            alignment = std::max(alignment, field_align);
            fields.push_back(std::move(field));
        });
        auto& stored = records_[id];
        if (reason.empty() && fields.empty()) reason = "an empty struct";
        if (reason.empty() && (position + alignment - 1) / alignment * alignment != stored.size)
            reason = "a packed or explicitly aligned layout";
        stored.reason = reason;
        stored.supported = reason.empty();
        stored.fields = std::move(fields);
        if (stored.supported) emit_record(stored);
        return stored;
    }

    // --- Neura source ---------------------------------------------------------

    static std::string layout_of(const Mapped& type) {
        switch (type.kind) {
        case Mapped::Kind::Scalar:
            if (type.scalar.align == type.scalar.size) return "c_value_layout." + type.scalar.carrier;
            return "c_layout.value_sized(c_layout.Carrier." + type.scalar.carrier + ", " +
                   std::to_string(type.scalar.size) + ", " + std::to_string(type.scalar.align) + ")";
        case Mapped::Kind::Pointer: return "c_value_layout.Address";
        case Mapped::Kind::Struct: return type.record + ".layout()";
        default: return {};
        }
    }

    static std::string neura_type(const Mapped& type) {
        switch (type.kind) {
        case Mapped::Kind::Scalar: return type.scalar.type;
        case Mapped::Kind::Pointer: return "c_segment.MemorySegment";
        case Mapped::Kind::Struct: return type.record;
        default: return "void";
        }
    }

    static std::string default_value(const Mapped& type) {
        if (type.kind == Mapped::Kind::Scalar) {
            if (type.scalar.type == "bool") return "false";
            if (type.scalar.type == "float32" || type.scalar.type == "float64") return "0.0";
            return "0";
        }
        return "nil";
    }

    // The MemorySegment accessor suffix of a carrier: Int32 is int32, UInt8 is uint8.
    static std::string accessor(const std::string& carrier) {
        std::string name;
        for (const char c : carrier) name += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        return name;
    }

    void emit_record(const Record& record) {
        std::ostringstream out;
        out << "    /// @brief The C struct " << record.name << " (" << record.size << " bytes).\n";
        out << "    export let " << record.name << " = class (\n";
        for (const auto& field : record.fields) {
            if (field.count >= 0) {
                out << "        export let " << field.name << ": " << neura_type(field.type) << "[list] = new;\n";
            } else {
                out << "        export let " << field.name << ": " << neura_type(field.type) << " = "
                    << default_value(field.type) << ";\n";
            }
        }
        out << "\n        /// @brief The struct's memory layout.\n";
        out << "        export static let layout = func -> c_layout.MemoryLayout => c_layout.struct_layout(";
        for (std::size_t i = 0; i < record.fields.size(); ++i) {
            const auto& field = record.fields[i];
            out << (i > 0 ? ", " : "");
            if (field.count >= 0)
                out << "c_layout.sequence_layout(" << field.count << ", " << layout_of(field.type) << ")";
            else
                out << layout_of(field.type);
            out << ".with_name(\"" << field.name << "\")";
        }
        out << ");\n\n";
        out << "        /// @brief A value with the given fields.\n";
        out << "        export static let of = func(";
        bool first = true;
        for (const auto& field : record.fields) {
            out << (first ? "" : ", ") << field.name << "_value: " << neura_type(field.type)
                << (field.count >= 0 ? "[list]" : "");
            first = false;
        }
        out << ") -> " << record.name << " {\n";
        out << "            let value = new " << record.name << "();\n";
        for (const auto& field : record.fields)
            out << "            value." << field.name << " = " << field.name << "_value;\n";
        out << "            return value;\n        };\n\n";
        out << "        /// @brief The struct stored at the start of `segment`.\n";
        out << "        export static let read = func(segment: c_segment.MemorySegment) -> " << record.name << " {\n";
        out << "            let value = new " << record.name << "();\n";
        for (const auto& field : record.fields) {
            if (field.count >= 0) {
                out << "            for (let i: int64 = 0; i < " << field.count << "; i++) {\n";
                out << "                value." << field.name << ".append(" << read_at(field.type, std::to_string(field.offset) + " + i * " + std::to_string(field.size)) << ");\n";
                out << "            }\n";
            } else {
                out << "            value." << field.name << " = " << read_at(field.type, std::to_string(field.offset))
                    << ";\n";
            }
        }
        out << "            return value;\n        };\n\n";
        out << "        /// @brief Stores the struct at the start of `segment`; a nil field or a missing array element is zero.\n";
        out << "        export let write_to = func(segment: c_segment.MemorySegment) {\n";
        out << "            segment.as_slice(0, " << record.size << ").fill(0);\n";
        for (const auto& field : record.fields) {
            const auto offset = std::to_string(field.offset);
            if (field.count >= 0) {
                out << "            for (let i = 0; i < " << field.count << " && i < " << field.name << ".length; i++) {\n";
                out << "                " << write_at(field.type, offset + " + (i as int64) * " + std::to_string(field.size), field.name + "[i]") << "\n";
                out << "            }\n";
            } else {
                out << "            " << write_at(field.type, offset, field.name) << "\n";
            }
        }
        out << "        };\n    );\n\n";
        records_source_ += out.str();
        declared(record.name);
    }

    static std::string read_at(const Mapped& type, const std::string& offset) {
        switch (type.kind) {
        case Mapped::Kind::Scalar: return "segment.get_" + accessor(type.scalar.carrier) + "(" + offset + ")";
        case Mapped::Kind::Pointer: return "segment.get_address(" + offset + ")";
        case Mapped::Kind::Struct:
            return type.record + ".read(segment.as_slice(" + offset + ", " + type.record + ".layout().byte_size()))";
        default: return {};
        }
    }

    static std::string write_at(const Mapped& type, const std::string& offset, const std::string& value) {
        switch (type.kind) {
        case Mapped::Kind::Scalar:
            return "segment.set_" + accessor(type.scalar.carrier) + "(" + offset + ", " + value + ");";
        case Mapped::Kind::Pointer:
            return "if (" + value + " != nil) {\n                segment.set_address(" + offset + ", " + value +
                   ");\n            }";
        case Mapped::Kind::Struct:
            return "if (" + value + " != nil) {\n                " + value + ".write_to(segment.as_slice(" + offset +
                   ", " + type.record + ".layout().byte_size()));\n            }";
        default: return {};
        }
    }

    static std::string result_of(const Mapped& type, const std::string& value) {
        if (type.kind == Mapped::Kind::Pointer) return "c_value.as_segment(" + value + ")";
        const auto& t = type.scalar.type;
        if (t == "bool") return "c_value.as_bool(" + value + ")";
        if (t == "int32") return "c_value.as_int32(" + value + ")";
        if (t == "int64") return "c_value.as_int64(" + value + ")";
        if (t == "uint64") return "c_value.as_uint64(" + value + ")";
        if (t == "float32") return "c_value.as_float32(" + value + ")";
        if (t == "float64") return "c_value.as_float64(" + value + ")";
        return "c_value.as_int64(" + value + ") as " + t;
    }

    bool declared(const std::string& name) { return !names_.insert(name).second; }

    // --- Declarations ---------------------------------------------------------

    void declaration(CXCursor cursor) {
        if (!in_header(cursor)) {
            // Say where a function went, if it is asked for: a declaration
            // that comes from an included header is not part of this import.
            if (lib_.clang_getCursorKind(cursor) == CXCursor_FunctionDecl) {
                const auto where = file_of(cursor);
                if (!where.empty())
                    elsewhere_.emplace(neura_name(text(lib_.clang_getCursorSpelling(cursor))),
                                       "declared in " + utf8_generic(utf8_path(where).filename()) +
                                           ", not in the imported header " + utf8_generic(header_.filename()));
            }
            return;
        }
        switch (lib_.clang_getCursorKind(cursor)) {
        case CXCursor_FunctionDecl: function(cursor); break;
        case CXCursor_StructDecl:
            if (lib_.clang_isCursorDefinition(cursor)) {
                const auto& found = record(cursor);
                if (!found.supported && !found.name.empty()) unsupported_.emplace(found.name, found.reason);
            }
            break;
        case CXCursor_TypedefDecl: {
            const auto underlying = lib_.clang_getCanonicalType(lib_.clang_getTypedefDeclUnderlyingType(cursor));
            if (underlying.kind == CXType_Record &&
                lib_.clang_getCursorKind(lib_.clang_getTypeDeclaration(underlying)) == CXCursor_StructDecl) {
                const auto& found = record(lib_.clang_getTypeDeclaration(underlying));
                if (!found.supported && !found.name.empty()) unsupported_.emplace(found.name, found.reason);
            }
            break;
        }
        case CXCursor_EnumDecl: enumeration(cursor); break;
        case CXCursor_VarDecl: global(cursor); break;
        case CXCursor_MacroDefinition:
            if (!lib_.clang_Cursor_isMacroBuiltin(cursor)) {
                const auto name = text(lib_.clang_getCursorSpelling(cursor));
                if (lib_.clang_Cursor_isMacroFunctionLike(cursor)) {
                    unsupported_.emplace(neura_name(name), "a function-like macro");
                } else {
                    // A macro defined as nothing, such as an include guard,
                    // has only its name as a token.
                    CXToken* tokens = nullptr;
                    unsigned count = 0;
                    lib_.clang_tokenize(unit_, lib_.clang_getCursorExtent(cursor), &tokens, &count);
                    if (tokens != nullptr) lib_.clang_disposeTokens(unit_, tokens, count);
                    if (count > 1) macro_names_.push_back(name);
                }
            }
            break;
        default: break;
        }
    }

    void enumeration(CXCursor cursor) {
        const auto integer_type = map(lib_.clang_getEnumDeclIntegerType(cursor));
        visit(cursor, [&](CXCursor child) {
            if (lib_.clang_getCursorKind(child) != CXCursor_EnumConstantDecl) return;
            const auto name = neura_name(text(lib_.clang_getCursorSpelling(child)));
            if (declared(name)) return;
            const bool is_unsigned = integer_type.scalar.type.starts_with("u");
            const auto value = is_unsigned ? std::to_string(lib_.clang_getEnumConstantDeclUnsignedValue(child))
                                           : std::to_string(lib_.clang_getEnumConstantDeclValue(child));
            constants_ << "    /// @brief The C enumerator " << name << ".\n";
            constants_ << "    export const " << name << ": " << integer_type.scalar.type << " = " << value << ";\n";
        });
    }

    std::string symbol(const std::string& name, bool inline_wrapper = false) {
#ifdef _WIN32
        // GetProcAddress searches only the selected module. Keep unlinked
        // declarations on the default lookup even when a wrapper needs a DLL.
        if (!linked_ && !inline_wrapper) return "c_lookup.default_lookup().find_or_trap(\"" + name + "\")";
#else
        (void)inline_wrapper;
#endif
        symbols_.insert(name);
        return "neura_library().find_or_trap(\"" + name + "\")";
    }

    void global(CXCursor cursor) {
        const auto storage = lib_.clang_Cursor_getStorageClass(cursor);
        if (storage == CX_SC_Static) return;
        const auto c_name = text(lib_.clang_getCursorSpelling(cursor));
        const auto name = neura_name(c_name);
        const auto type = lib_.clang_getCursorType(cursor);
        const auto size = lib_.clang_Type_getSizeOf(type);
        if (declared(name + "_segment")) return;
        std::ostringstream out;
        out << "    /// @brief The memory of the C global " << c_name << ".\n";
        out << "    export let " << name << "_segment = func -> c_segment.MemorySegment => " << symbol(c_name)
            << ".reinterpret(" << std::max<long long>(size, 0) << ");\n";
        const auto mapped = map(type);
        if (mapped.kind == Mapped::Kind::Scalar || mapped.kind == Mapped::Kind::Pointer ||
            mapped.kind == Mapped::Kind::Struct) {
            const auto t = neura_type(mapped);
            out << "    /// @brief Reads the C global " << c_name << ".\n";
            out << "    export let get_" << name << " = func -> " << t << " {\n";
            out << "        let segment = " << name << "_segment();\n";
            out << "        return " << read_at(mapped, "0") << ";\n    };\n";
            if (!lib_.clang_isConstQualifiedType(type)) {
                out << "    /// @brief Writes the C global " << c_name << ".\n";
                out << "    export let set_" << name << " = func(value: " << t << ") {\n";
                out << "        let segment = " << name << "_segment();\n";
                out << "        " << write_at(mapped, "0", "value") << "\n    };\n";
            }
        }
        functions_ << out.str() << "\n";
    }

    void function(CXCursor cursor) {
        const auto c_name = text(lib_.clang_getCursorSpelling(cursor));
        const auto name = neura_name(c_name);
        if (functions_seen_.contains(c_name)) return;
        functions_seen_.insert(c_name);
        const auto type = lib_.clang_getCursorType(cursor);
        const bool variadic = lib_.clang_isFunctionTypeVariadic(type) != 0;
        const auto result = map(lib_.clang_getResultType(type));
        if (result.kind == Mapped::Kind::Unsupported) {
            unsupported_.emplace(name, "its result is " + result.reason);
            return;
        }
        std::vector<std::pair<std::string, Mapped>> parameters;
        const auto count = lib_.clang_Cursor_getNumArguments(cursor);
        for (int i = 0; i < count; ++i) {
            const auto argument = lib_.clang_Cursor_getArgument(cursor, static_cast<unsigned>(i));
            auto parameter = neura_name(text(lib_.clang_getCursorSpelling(argument)));
            if (parameter.empty()) parameter = "arg" + std::to_string(i);
            auto mapped = map(lib_.clang_getCursorType(argument));
            if (mapped.kind == Mapped::Kind::Unsupported || mapped.kind == Mapped::Kind::Void) {
                unsupported_.emplace(name, "parameter " + parameter + " is " + mapped.reason);
                return;
            }
            if (variadic && mapped.kind == Mapped::Kind::Struct) {
                unsupported_.emplace(name, "a variadic function with a struct parameter");
                return;
            }
            parameters.emplace_back(parameter, std::move(mapped));
        }
        if (declared(name)) {
            unsupported_.emplace(name, "the name is already declared");
            return;
        }
        // An inline function has no external definition: call an exported
        // wrapper compiled into the image's native library.
        std::string target = c_name;
        const bool inline_wrapper = lib_.clang_Cursor_isFunctionInlined(cursor) != 0;
        if (inline_wrapper) {
            if (variadic) {
#ifdef _WIN32
                // The Universal C Runtime defines printf, scanf and the rest
                // of their families inline in its headers, and ucrtbase.dll
                // does not export them.
                if (ucrt_file(file_of(cursor))) {
                    unsupported_.emplace(name, "a variadic inline function of the Universal C Runtime, which does "
                                               "not export it; use Std.Io");
                    return;
                }
#endif
                unsupported_.emplace(name, "a variadic inline function");
                return;
            }
            target = "neura_inline_" + c_name;
            shim_ << text(lib_.clang_getTypeSpelling(lib_.clang_getResultType(type))) << " " << target << "(";
            for (int i = 0; i < count; ++i) {
                const auto argument = lib_.clang_Cursor_getArgument(cursor, static_cast<unsigned>(i));
                shim_ << (i > 0 ? ", " : "") << text(lib_.clang_getTypeSpelling(lib_.clang_getCursorType(argument)))
                      << " a" << i;
            }
            shim_ << (count == 0 ? "void" : "") << ") {\n    "
                  << (result.kind == Mapped::Kind::Void ? "" : "return ") << c_name << "(";
            for (int i = 0; i < count; ++i) shim_ << (i > 0 ? ", " : "") << "a" << i;
            shim_ << ");\n}\n";
        }
        std::string descriptor = result.kind == Mapped::Kind::Void ? "c_descriptor.of_void(" : "c_descriptor.of(" + layout_of(result);
        for (std::size_t i = 0; i < parameters.size(); ++i)
            descriptor += (i == 0 && result.kind == Mapped::Kind::Void ? "" : ", ") + layout_of(parameters[i].second);
        descriptor += ")";

        const auto lookup = symbol(target, inline_wrapper);
        std::ostringstream out;
        const auto result_type = neura_type(result);
        if (variadic) {
            out << "    /// @brief Calls the variadic C function " << c_name
                << ". Each further argument is an int when it fits, a long long otherwise, a double, an int for a "
                   "bool, or an address.\n";
            out << "    export let " << name << " = func(";
            for (const auto& [parameter, mapped] : parameters) out << parameter << ": " << neura_type(mapped) << ", ";
            out << "neura_rest: ...) -> " << result_type << " {\n";
            out << "        let neura_arguments: c_variadic.Argument[list] = new;\n";
            for (const auto& [parameter, mapped] : parameters) out << "        neura_arguments.append(" << pack(mapped, parameter) << ");\n";
            out << "        for (let neura_argument in neura_rest) {\n            neura_arguments.append(neura_argument);\n        }\n";
            const auto call = "c_method.MethodHandle.call_variadic(" + lookup + ", " + descriptor + ", neura_arguments)";
            if (result.kind == Mapped::Kind::Void) out << "        " << call << ";\n";
            else out << "        return " << result_of(result, call) << ";\n";
            out << "    };\n\n";
            functions_ << out.str();
            return;
        }
        out << "    let neura_handle_" << name << ": c_method.MethodHandle = nil;\n";
        out << "    let neura_call_" << name << " = func -> c_method.MethodHandle {\n";
        out << "        if (neura_handle_" << name << " == nil) {\n";
        out << "            neura_handle_" << name << " = c_linker.native_linker().downcall_handle(" << lookup
            << ",\n                " << descriptor << ");\n";
        out << "        }\n        return neura_handle_" << name << ";\n    };\n";
        const bool has_string = std::ranges::any_of(parameters, [](const auto& p) { return p.second.c_string; });
        out << "    /// @brief Calls the C function " << c_name << ".";
        if (has_string) out << " A `string` overload passes NUL-terminated copies of `const char*` arguments.";
        out << "\n";
        for (int variant = 0; variant < (has_string ? 2 : 1); ++variant) {
            out << (variant == 0 ? "    export let " + name + " = func(" : "    or func(");
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                const auto& [parameter, mapped] = parameters[i];
                out << (i > 0 ? ", " : "") << parameter << ": "
                    << (variant == 1 && mapped.c_string ? "string" : neura_type(mapped));
            }
            out << ") -> " << result_type << " {\n";
            body(out, name, parameters, result);
            out << "    }" << (variant == 0 && has_string ? "\n" : ";\n\n");
        }
        functions_ << out.str();
    }

    static std::string pack(const Mapped& type, const std::string& value) {
        if (type.kind == Mapped::Kind::Pointer) return "c_variadic.Argument.Object(" + value + ")";
        const auto& t = type.scalar.type;
        if (t == "bool") return "c_variadic.Argument.Boolean(" + value + ")";
        if (t == "float32") return "c_variadic.Argument.Floating32(" + value + ")";
        if (t == "float64") return "c_variadic.Argument.Floating(" + value + ")";
        if (t.starts_with("u")) return "c_variadic.Argument.Unsigned(" + value + " as uint64)";
        return "c_variadic.Argument.Signed(" + value + " as int64)";
    }

    static void body(std::ostringstream& out, const std::string& name,
                     const std::vector<std::pair<std::string, Mapped>>& parameters, const Mapped& result) {
        const bool structs = result.kind == Mapped::Kind::Struct ||
                             std::ranges::any_of(parameters, [](const auto& p) { return p.second.kind == Mapped::Kind::Struct; });
        std::string arguments;
        if (structs) out << "        let neura_arena = c_arena.of_confined();\n";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const auto& [parameter, mapped] = parameters[i];
            arguments += (i > 0 ? ", " : "");
            if (mapped.kind == Mapped::Kind::Struct) {
                out << "        let neura_" << parameter << " = neura_arena.allocate(" << mapped.record << ".layout());\n";
                out << "        if (" << parameter << " != nil) {\n            " << parameter << ".write_to(neura_" << parameter << ");\n        }\n";
                arguments += "neura_" + parameter;
            } else {
                arguments += parameter;
            }
        }
        const auto call = "neura_call_" + name + "()." + (structs ? "invoke_with(neura_arena" + std::string(parameters.empty() ? "" : ", ") : "invoke(") + arguments + ")";
        if (result.kind == Mapped::Kind::Void) {
            out << "        " << call << ";\n";
            if (structs) out << "        neura_arena.close();\n";
        } else if (result.kind == Mapped::Kind::Struct) {
            out << "        let neura_result = " << result.record << ".read(c_value.as_segment(" << call << "));\n";
            out << "        neura_arena.close();\n        return neura_result;\n";
        } else if (structs) {
            out << "        let neura_result = " << result_of(result, call) << ";\n";
            out << "        neura_arena.close();\n        return neura_result;\n";
        } else {
            out << "        return " << result_of(result, call) << ";\n";
        }
    }

    // --- Macros ---------------------------------------------------------------

    void macros() {
        if (macro_names_.empty()) return;
        // Evaluate each object-like macro as the initializer of a variable in
        // a translation unit that includes the header.
        std::string source = "#include \"" + utf8_generic(fs::absolute(header_)) + "\"\n";
        for (const auto& name : macro_names_)
            source += "static const __auto_type neura_macro_" + name + " = (" + name + ");\n";
        const std::string file = utf8_text(header_.parent_path() / "neura-macros.c");
        CXUnsavedFile unsaved{file.c_str(), source.c_str(), static_cast<unsigned long>(source.size())};
        auto unit = parse(file, &unsaved, false);
        std::map<std::string, CXCursor> variables;
        visit(lib_.clang_getTranslationUnitCursor(unit), [&](CXCursor cursor) {
            if (lib_.clang_getCursorKind(cursor) != CXCursor_VarDecl) return;
            const auto name = text(lib_.clang_getCursorSpelling(cursor));
            if (name.starts_with("neura_macro_")) variables.emplace(name.substr(12), cursor);
        });
        for (const auto& c_name : macro_names_) {
            const auto name = neura_name(c_name);
            const auto found = variables.find(c_name);
            if (found == variables.end()) {
                unsupported_.emplace(name, "a macro that is not a constant expression");
                continue;
            }
            const auto type = lib_.clang_getCanonicalType(lib_.clang_getCursorType(found->second));
            auto result = lib_.clang_Cursor_Evaluate(found->second);
            std::optional<std::string> declaration;
            switch (result == nullptr ? CXEval_UnExposed : lib_.clang_EvalResult_getKind(result)) {
            case CXEval_Int: {
                const auto mapped = map(type);
                if (mapped.kind != Mapped::Kind::Scalar || mapped.scalar.type == "bool") break;
                const bool is_unsigned = lib_.clang_EvalResult_isUnsignedInt(result) != 0;
                const auto value = is_unsigned ? std::to_string(lib_.clang_EvalResult_getAsUnsigned(result))
                                               : std::to_string(lib_.clang_EvalResult_getAsLongLong(result));
                declaration = mapped.scalar.type + " = " + value;
                break;
            }
            case CXEval_Float: {
                const bool single = type.kind == CXType_Float;
                const double value = lib_.clang_EvalResult_getAsDouble(result);
                declaration = float_constant(value, single);
                break;
            }
            case CXEval_StrLiteral: {
                const char* chars = lib_.clang_EvalResult_getAsStr(result);
                if (chars != nullptr) {
                    if (auto literal = string_literal(chars)) declaration = "string = " + *literal;
                }
                break;
            }
            default: break;
            }
            if (!declaration) {
                // A string macro initializes a char pointer, which does not
                // evaluate; read its string literal instead.
                std::optional<std::string> literal;
                std::function<void(CXCursor)> find = [&](CXCursor node) {
                    if (literal) return;
                    if (lib_.clang_getCursorKind(node) == CXCursor_StringLiteral) {
                        literal = decode_c_string(text(lib_.clang_getCursorSpelling(node)));
                        return;
                    }
                    visit(node, find);
                };
                find(found->second);
                if (literal) {
                    if (auto neura_literal = string_literal(*literal)) declaration = "string = " + *neura_literal;
                }
            }
            if (result != nullptr) lib_.clang_EvalResult_dispose(result);
            if (!declaration) {
                unsupported_.emplace(name, "a macro that is not an integer, floating, or string constant");
                continue;
            }
            if (declared(name)) continue;
            constants_ << "    /// @brief The C macro " << c_name << ".\n";
            constants_ << "    export const " << name << ": " << *declaration << ";\n";
        }
        lib_.clang_disposeTranslationUnit(unit);
    }

    // The bytes of a simple C string literal token, or nothing for a prefix,
    // an octal or hex escape, or concatenated literals.
    static std::optional<std::string> decode_c_string(const std::string& token) {
        if (token.size() < 2 || token.front() != '"' || token.back() != '"') return std::nullopt;
        std::string result;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            char c = token[i];
            if (c == '"') return std::nullopt;
            if (c == '\\') {
                if (i + 2 >= token.size()) return std::nullopt;
                switch (token[++i]) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '\'': c = '\''; break;
                default: return std::nullopt;
                }
            }
            result += c;
        }
        return result;
    }

    static std::string float_constant(double value, bool single) {
        std::array<char, 64> buffer{};
        const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        std::string text(buffer.data(), error == std::errc{} ? end : buffer.data());
        const bool plain = !text.empty() && text.find_first_of("eEni") == std::string::npos;
        if (plain && text.find('.') == std::string::npos) text += ".0";
        if (single) {
            if (plain) return "float32 = " + text;
            std::uint32_t bits = 0;
            const auto narrowed = static_cast<float>(value);
            std::memcpy(&bits, &narrowed, sizeof bits);
            return "float32 = c_float32.from_bits(" + std::to_string(bits) + ")";
        }
        if (plain) return "float64 = " + text;
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        return "float64 = c_float64.from_bits(" + std::to_string(bits) + ")";
    }

    // A Neura string literal for UTF-8 text, or nothing when it holds a
    // control character a literal cannot spell.
    static std::optional<std::string> string_literal(std::string_view text) {
        std::string literal = "\"";
        for (const char c : text) {
            const auto byte = static_cast<unsigned char>(c);
            if (c == '"') literal += "\\\"";
            else if (c == '\\') literal += "\\\\";
            else if (c == '\n') literal += "\\n";
            else if (c == '\t') literal += "\\t";
            else if (c == '\r') literal += "\\r";
            else if (byte < 0x20 || byte == 0x7f) return std::nullopt;
            else literal += c;
        }
        return literal + "\"";
    }

    // --- Assembly ------------------------------------------------------------

    CImportResult finish() {
        CImportResult result;
        result.module_name = module_name_;
        if (!shim_.str().empty())
            result.shim = "#include \"" + utf8_generic(fs::absolute(header_)) + "\"\n" + shim_.str();
        result.unsupported = unsupported_;
        result.symbols.assign(symbols_.begin(), symbols_.end());
        const bool embedded = linked_ || !result.shim.empty();
        std::ostringstream out;
        out << "// Generated by neura from the C header " << utf8_generic(header_.filename()) << ". Do not edit.\n";
        for (const auto& [name, reason] : unsupported_) out << "// Not imported: " << name << ": " << reason << "\n";
        out << "let c_arena = import:Std.DynamicForeign.Arena;\n"
               "let c_descriptor = import:Std.DynamicForeign.FunctionDescriptor;\n"
               "let c_float32 = import:Std.Core.Float32;\n"
               "let c_float64 = import:Std.Core.Float64;\n"
               "let c_layout = import:Std.DynamicForeign.MemoryLayout;\n"
               "let c_linker = import:Std.DynamicForeign.Linker;\n"
               "let c_lookup = import:Std.DynamicForeign.SymbolLookup;\n"
               "let c_method = import:Std.DynamicForeign.MethodHandle;\n"
               "let c_segment = import:Std.DynamicForeign.MemorySegment;\n"
               "let c_value = import:Std.DynamicForeign.ForeignValue;\n"
               "let c_value_layout = import:Std.DynamicForeign.ValueLayout;\n"
               "let c_variadic = import:Std.Core.Variadic;\n\n";
        out << "/// @brief The declarations of the C header " << utf8_generic(header_.filename())
            << " (ForeignInterop.md, C imports). Calls need the native capability.\n";
        out << "export let " << module_name_ << " = module (\n";
        out << "    let neura_lookup: c_lookup.SymbolLookup = nil;\n";
        out << "    let neura_library = func -> c_lookup.SymbolLookup {\n";
        out << "        if (neura_lookup == nil) {\n";
        out << "            neura_lookup = "
            << (embedded ? "c_lookup.embedded().or_else(c_lookup.default_lookup())" : "c_lookup.default_lookup()")
            << ";\n        }\n        return neura_lookup;\n    };\n\n";
        out << constants_.str() << (constants_.str().empty() ? "" : "\n");
        out << records_source_;
        out << functions_.str();
        out << ");\n";
        result.source = out.str();
        for (const auto& [name, reason] : elsewhere_)
            if (!names_.contains(name)) result.unsupported.emplace(name, reason);
        return result;
    }

    NeuraLibclang& lib_;
    fs::path header_;
    bool linked_;
    std::string module_name_;
    const CImportOptions& options_;
    Span site_;
    std::vector<std::string> arguments_;
    CXIndex index_ = nullptr;
    CXTranslationUnit unit_ = nullptr;
    std::map<std::string, std::string> typedef_names_;
    std::map<std::string, Record> records_;
    std::set<std::string> names_;
    std::set<std::string> functions_seen_;
    std::set<std::string> symbols_;
    std::vector<std::string> macro_names_;
    std::map<std::string, std::string> unsupported_;
    // Functions declared in headers the imported one includes, with where.
    std::map<std::string, std::string> elsewhere_;
    // Each #include directive as (the including file, the name as spelled),
    // and the files already judged to be part of the header.
    std::vector<std::pair<std::string, std::string>> inclusions_;
    std::map<std::string, bool> header_files_;
    std::ostringstream constants_;
    std::ostringstream functions_;
    std::ostringstream shim_;
    std::string records_source_;
};

} // namespace

std::vector<std::filesystem::path> system_header_directories() {
#ifdef __APPLE__
    // The C headers are in the SDK; there is no /usr/include.
    std::vector<fs::path> directories{"/usr/local/include"};
    if (const auto sdk = macos_sdk(); !sdk.empty()) directories.push_back(sdk / "usr/include");
    return directories;
#elif defined(_WIN32)
    // The C headers of the MSVC tools and the Windows SDK, found once per
    // process. A developer environment of Visual Studio names them in
    // INCLUDE (and EXTERNAL_INCLUDE), which clang honors too; without it they
    // are found as clang finds them.
    static const std::vector<fs::path> directories = [] {
        std::vector<fs::path> found;
        append_directory_list(found, environment_value(L"INCLUDE"));
        if (!found.empty()) {
            append_directory_list(found, environment_value(L"EXTERNAL_INCLUDE"));
            return found;
        }
        found = msvc_include_directories();
        for (auto& directory : windows_sdk_include_directories()) found.push_back(std::move(directory));
        return found;
    }();
    return directories;
#else
    return {"/usr/local/include", "/usr/include/x86_64-linux-gnu", "/usr/include/aarch64-linux-gnu", "/usr/include"};
#endif
}

bool is_system_header_directory(const std::filesystem::path& directory) {
#ifdef __APPLE__
    // clang drops a -I directory that is also a system directory. Naming the
    // SDK's usr/include with -I would put it ahead of the builtin headers.
    std::error_code ignored;
    const auto canonical = fs::weakly_canonical(directory, ignored);
    for (const auto& system : system_header_directories())
        if (canonical == fs::weakly_canonical(system, ignored)) return true;
#elif defined(_WIN32)
    // The header search found the header there, and the import names the
    // directory with -idirafter. Compared as files, so that the case of a
    // name and the spelling of the separators do not matter.
    for (const auto& system : system_header_directories()) {
        std::error_code ignored;
        if (fs::equivalent(directory, system, ignored)) return true;
    }
#else
    (void)directory;
#endif
    return false;
}

LibclangStatus probe_libclang(const CImportOptions& options) {
    LibclangStatus status;
    NeuraLibclang library{};
    if (!neura_libclang_load(&library, options.libclang.empty() ? nullptr : options.libclang.c_str())) {
        status.error = library.error;
        return status;
    }
    status.loaded = true;
    status.path = library.path;
    status.major = library.major;
    status.resource_dir = clang_resource_dir(library);
    neura_libclang_unload(&library);
    return status;
}

CImportResult import_c_header(const std::filesystem::path& header, bool linked, const std::string& module_name,
                              const CImportOptions& options, const Span& site) {
    auto& lib = libclang(options, site);
    return Importer(lib, header, linked, module_name, options, site).run();
}

} // namespace neura

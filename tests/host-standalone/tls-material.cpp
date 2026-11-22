#include "nvm/tls_material.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#ifdef __linux__
#include <cstdarg>
#include <sys/syscall.h>
#endif

using namespace neura;
using namespace neura::nvm;
using namespace neura::nvm::provider;
namespace fs = std::filesystem;

#ifdef __linux__
namespace {
bool proc_available = true;
int proc_queries = 0;
std::string watched_path;
int watched_opens = 0;
// Opens that read a FIFO (not O_PATH handles) and reopens through /proc.
int fifo_reads = 0;
int reopens = 0;
// Simulates openat2 missing (ENOSYS), as before Linux 5.6.
bool openat2_missing = false;
// After the check's stat of this path, replace `swapped` with a link to `swap_target`.
std::string swap_after_stat;
fs::path swapped, swap_target;
}
extern "C" int __real_open(const char* name, int flags, ...);
extern "C" int __wrap_open(const char* name, int flags, ...) {
    if (watched_path == name) ++watched_opens;
    if (std::string_view(name).starts_with("/proc/self/fd/")) ++reopens;
    int opened = 0;
    if (!(flags & O_CREAT) && (flags & O_TMPFILE) != O_TMPFILE) {
        opened = __real_open(name, flags);
    } else {
        std::va_list arguments;
        va_start(arguments, flags);
        const auto mode = va_arg(arguments, int);
        va_end(arguments);
        opened = __real_open(name, flags, mode);
    }
    struct stat info{};
    if (opened >= 0 && (flags & O_PATH) == 0 && ::fstat(opened, &info) == 0 && S_ISFIFO(info.st_mode)) ++fifo_reads;
    return opened;
}
extern "C" long __real_syscall(long number, ...);
extern "C" long __wrap_syscall(long number, ...) {
    std::va_list arguments;
    va_start(arguments, number);
    long values[6];
    for (auto& value : values) value = va_arg(arguments, long);
    va_end(arguments);
    if (number == SYS_openat2) {
        if (watched_path == reinterpret_cast<const char*>(values[1])) ++watched_opens;
        if (openat2_missing) {
            errno = ENOSYS;
            return -1;
        }
    }
    return __real_syscall(number, values[0], values[1], values[2], values[3], values[4], values[5]);
}
extern "C" int __real_stat(const char* name, struct stat* info);
extern "C" int __wrap_stat(const char* name, struct stat* info) {
    const auto result = __real_stat(name, info);
    if (!swap_after_stat.empty() && swap_after_stat == name) {
        swap_after_stat.clear();
        fs::rename(swapped, fs::path(swapped) += ".checked");
        fs::create_directory_symlink(swap_target, swapped);
    }
    return result;
}
extern "C" ssize_t __real_readlink(const char* name, char* bytes, size_t size);
extern "C" ssize_t __wrap_readlink(const char* name, char* bytes, size_t size) {
    if (std::string_view(name).starts_with("/proc/self/fd/")) {
        ++proc_queries;
        if (!proc_available) {
            errno = ENOENT;
            return -1;
        }
    }
    return __real_readlink(name, bytes, size);
}
#endif

std::string path_text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

int main() {
    const auto root = fs::canonical(fs::temp_directory_path()) / platform::unique_name("neura-tls-material-");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{root};
    assert(fs::create_directory(root));
    const auto allowed = root / "allowed";
    assert(fs::create_directory(allowed));
    const auto fixture = allowed / "material.pem";
    const auto outside = root / "outside.pem";
    constexpr std::string_view material = "credential fixture\n";
    for (const auto& file : {fixture, outside}) {
        std::ofstream stream(file, std::ios::binary);
        stream << material;
        assert(stream.good());
    }

    Capabilities capabilities;
    capabilities.fs = true;
    const auto prefix = resolve_path(path_text(allowed), true);
    assert(!prefix.error);
    const std::vector<ResolvedPath> prefixes{prefix};

    // An unrestricted credential read behaves the same without procfs.
#ifdef __linux__
    proc_available = false;
#endif
    auto path = path_text(fixture);
    assert(tls_material::read(capabilities, {}, path) == 0 && path == material);
#ifdef __linux__
    assert(proc_queries == 0);
    path = path_text(fixture);
    assert(tls_material::read(capabilities, prefixes, path) == host_error::unsupported);
    assert(proc_queries == 1);
    proc_available = true;
#endif

#if defined(_WIN32) || defined(__linux__) || defined(F_GETPATH) || (defined(__FreeBSD__) && defined(F_KINFO))
    path = path_text(fixture);
#ifdef __linux__
    // Positive control: the wrapper sees the permitted open.
    watched_path = path;
#endif
    assert(tls_material::read(capabilities, prefixes, path) == 0 && path == material);
#ifdef __linux__
    assert(watched_opens == 1);
    watched_opens = 0;
#endif
    path = path_text(outside);
#ifdef __linux__
    watched_path = path;
#endif
    assert(tls_material::read(capabilities, prefixes, path) == host_error::capability_denied);
#ifdef __linux__
    assert(watched_opens == 0);
    watched_path.clear();
#endif

    // Independently check the opened-object guard using a harmless file.
    std::error_code error;
    const auto opened = platform::open_no_follow(outside, platform::OpenAccess::read,
        platform::OpenDisposition::open_existing, error);
    assert(opened);
    assert(tls_material::opened_path_error(opened.get(), prefixes) == host_error::capability_denied);
#else
    path = path_text(fixture);
    assert(tls_material::read(capabilities, prefixes, path) == host_error::unsupported);
#endif

#ifdef __linux__
    // Classify this path by the grant before any file-kind check on a handle.
    const auto fifo = root / "outside.fifo";
    assert(::mkfifo(fifo.c_str(), 0600) == 0);
    path = path_text(fifo);
    const auto queries = proc_queries;
    assert(tls_material::read(capabilities, prefixes, path) == host_error::capability_denied);
    assert(proc_queries == queries);

    // A FIFO inside the prefix, or with no prefixes, is refused before any open.
    const auto inside_fifo = allowed / "inside.fifo";
    assert(::mkfifo(inside_fifo.c_str(), 0600) == 0);
    for (const auto* granted : {&prefixes, static_cast<const std::vector<ResolvedPath>*>(nullptr)}) {
        path = path_text(inside_fifo);
        watched_path = path;
        watched_opens = 0;
        assert(tls_material::read(capabilities, granted ? *granted : std::vector<ResolvedPath>{}, path) ==
               host_error::invalid_input);
        assert(watched_opens == 0);
    }
    watched_path.clear();

    const auto symlink = allowed / "link.pem";
    fs::create_symlink(fixture.filename(), symlink);
    path = path_text(symlink);
    assert(tls_material::read(capabilities, prefixes, path) == 0 && path == material);

    // A directory inside the prefix swapped for a link to an outside one
    // after the path was checked cannot make the open follow it. With
    // openat2 the open refuses the link; without it, the O_PATH handle is
    // checked before anything is opened for reading. Either way the outside
    // FIFO is never opened and an outside file is never opened for reading.
    const auto directory = allowed / "sub";
    assert(fs::create_directory(directory));
    {
        std::ofstream stream(directory / "credential.pem", std::ios::binary);
        stream << material;
    }
    const auto fifo_directory = root / "elsewhere-fifo";
    const auto file_directory = root / "elsewhere-file";
    assert(fs::create_directory(fifo_directory) && fs::create_directory(file_directory));
    assert(::mkfifo((fifo_directory / "credential.pem").c_str(), 0600) == 0);
    {
        std::ofstream stream(file_directory / "credential.pem", std::ios::binary);
        stream << material;
    }
    struct Race {
        fs::path target;
        bool without_openat2;
        std::uint32_t expected;
    };
    for (const auto& race : {Race{fifo_directory, false, host_error::capability_denied},
                             Race{file_directory, false, host_error::capability_denied},
                             Race{fifo_directory, true, host_error::invalid_input},
                             Race{file_directory, true, host_error::capability_denied}}) {
        path = path_text(directory / "credential.pem");
        swap_after_stat = path;
        swapped = directory;
        swap_target = race.target;
        openat2_missing = race.without_openat2;
        fifo_reads = 0;
        reopens = 0;
        const auto result = tls_material::read(capabilities, prefixes, path);
        assert(swap_after_stat.empty()); // The swap happened between the check and the open.
        assert(fifo_reads == 0);
        assert(result == race.expected && reopens == 0);
        fs::remove(directory);
        fs::rename(fs::path(directory) += ".checked", directory);
    }
    openat2_missing = false;
    // Without openat2 an unchanged path still loads through the O_PATH handle.
    for (const bool without_openat2 : {false, true}) {
        openat2_missing = without_openat2;
        path = path_text(directory / "credential.pem");
        reopens = 0;
        assert(tls_material::read(capabilities, prefixes, path) == 0 && path == material);
        assert(reopens == 1);
    }
    openat2_missing = false;
#endif
#ifdef _WIN32
    for (const char* forbidden : {R"(\\example.invalid\credentials\root.pem)", "//example.invalid/credentials/root.pem",
                                  R"(\\.\NUL)", R"(\\?\C:\root.pem)", "NUL.pem"}) {
        path = forbidden;
        assert(tls_material::read(capabilities, {}, path) == host_error::invalid_input);
        assert(tls_material::read(capabilities, prefixes, path) == host_error::invalid_input);
    }
#endif
}

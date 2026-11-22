#include "nvm/tls_material.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#ifdef __linux__
#include <cstdarg>
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
}
extern "C" int __real_open(const char* name, int flags, ...);
extern "C" int __wrap_open(const char* name, int flags, ...) {
    if (watched_path == name) ++watched_opens;
    if (!(flags & O_CREAT) && (flags & O_TMPFILE) != O_TMPFILE) return __real_open(name, flags);
    std::va_list arguments;
    va_start(arguments, flags);
    const auto mode = va_arg(arguments, int);
    va_end(arguments);
    return __real_open(name, flags, mode);
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

#include "system.hpp"

#include "cli/install_layout.hpp"
#include "cli/tool.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__linux__)
#include <link.h>
#include <sys/random.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif

namespace neura::service {

void UniqueFd::reset(int fd) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

void set_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void set_nonblocking(int fd, bool nonblocking) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0) return;
    ::fcntl(fd, F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

// --- Messages ----------------------------------------------------------------

bool MessageReader::fill(int fd) {
    while (!ready_) {
        const auto have = buffer_.size();
        std::string chunk(need_ - have, '\0');
        iovec io{chunk.data(), chunk.size()};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 8)];
        msghdr header{};
        header.msg_iov = &io;
        header.msg_iovlen = 1;
        header.msg_control = control;
        header.msg_controllen = sizeof control;
        int flags = 0;
#ifdef MSG_CMSG_CLOEXEC
        flags |= MSG_CMSG_CLOEXEC;
#endif
        const auto count = ::recvmsg(fd, &header, flags | MSG_DONTWAIT);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            throw TransportError(std::string("the connection failed: ") + std::strerror(errno));
        }
        for (auto* message = CMSG_FIRSTHDR(&header); message; message = CMSG_NXTHDR(&header, message)) {
            if (message->cmsg_level != SOL_SOCKET || message->cmsg_type != SCM_RIGHTS) continue;
            const auto n = (message->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int* fds = reinterpret_cast<const int*>(CMSG_DATA(message));
            for (std::size_t i = 0; i < n; ++i) {
                set_cloexec(fds[i]);
                descriptors_.emplace_back(fds[i]);
            }
        }
        if (header.msg_flags & MSG_CTRUNC) {
            descriptors_.clear();
            throw ProtocolError("a message carried more descriptors than any message may");
        }
        if (count == 0) {
            if (buffer_.empty() && descriptors_.empty()) return false;
            throw TransportError("the connection ended inside a message");
        }
        buffer_.append(chunk.data(), static_cast<std::size_t>(count));
        if (buffer_.size() < need_) continue;
        if (need_ == 4) {
            std::array<std::uint8_t, 4> length{};
            std::memcpy(length.data(), buffer_.data(), 4);
            need_ = 4 + std::size_t{announced_length(length)};
            continue;
        }
        const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(buffer_.data()), buffer_.size());
        auto message = decode(bytes, descriptors_.size());
        ready_ = Received{std::move(message), std::move(descriptors_)};
        descriptors_.clear();
        buffer_.clear();
        need_ = 4;
    }
    return true;
}

std::optional<Received> MessageReader::take() {
    auto out = std::move(ready_);
    ready_.reset();
    return out;
}

namespace {

int remaining_ms(std::optional<Clock::time_point> deadline) {
    if (!deadline) return -1;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, 1 << 30));
}

} // namespace

void send_message(int fd, const Message& message, const std::vector<int>& descriptors,
                  std::optional<Clock::time_point> deadline) {
    const auto bytes = encode(message);
    std::size_t sent = 0;
    bool first = true;
    while (sent < bytes.size()) {
        iovec io{const_cast<char*>(bytes.data() + sent), bytes.size() - sent};
        msghdr header{};
        header.msg_iov = &io;
        header.msg_iovlen = 1;
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 8)];
        if (first && !descriptors.empty()) {
            if (descriptors.size() > 8) throw TransportError("too many descriptors for one message");
            header.msg_control = control;
            header.msg_controllen = CMSG_SPACE(sizeof(int) * descriptors.size());
            auto* cmsg = CMSG_FIRSTHDR(&header);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int) * descriptors.size());
            std::memcpy(CMSG_DATA(cmsg), descriptors.data(), sizeof(int) * descriptors.size());
        }
        int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const auto count = ::sendmsg(fd, &header, flags);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd poll_fd{fd, POLLOUT, 0};
                const int ready = ::poll(&poll_fd, 1, remaining_ms(deadline));
                if (ready == 0) throw TransportError("the peer did not accept the message in time");
                continue;
            }
            throw TransportError(std::string("cannot send: ") + std::strerror(errno));
        }
        first = false;
        sent += static_cast<std::size_t>(count);
    }
}

std::optional<Received> receive_message(int fd, std::optional<Clock::time_point> deadline) {
    MessageReader reader;
    for (;;) {
        if (!reader.fill(fd)) return std::nullopt;
        if (auto message = reader.take()) return message;
        pollfd poll_fd{fd, POLLIN, 0};
        const int ready = ::poll(&poll_fd, 1, remaining_ms(deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) throw TransportError("no reply in time");
    }
}

std::optional<std::uint32_t> peer_uid(int fd) {
#if defined(SO_PEERCRED)
    ucred credentials{};
    socklen_t length = sizeof credentials;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) return std::nullopt;
    return credentials.uid;
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (::getpeereid(fd, &uid, &gid) != 0) return std::nullopt;
    return uid;
#endif
}

void random_bytes(std::span<std::uint8_t> out) {
    std::size_t done = 0;
#if defined(__linux__)
    while (done < out.size()) {
        const auto count = ::getrandom(out.data() + done, out.size() - done, 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        done += static_cast<std::size_t>(count);
    }
#elif defined(__APPLE__)
    ::arc4random_buf(out.data(), out.size());
    done = out.size();
#endif
    if (done < out.size()) throw TransportError("no random bytes are available");
}

// --- The build ------------------------------------------------------------------

namespace {

#if defined(__linux__)
struct BuildIdSearch {
    std::uintptr_t address;
    std::string id;
};

int find_build_id(dl_phdr_info* info, std::size_t, void* data) {
    auto* search = static_cast<BuildIdSearch*>(data);
    bool contains = false;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD) continue;
        const auto start = info->dlpi_addr + segment.p_vaddr;
        if (search->address >= start && search->address < start + segment.p_memsz) contains = true;
    }
    if (!contains) return 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_NOTE) continue;
        const auto* at = reinterpret_cast<const unsigned char*>(info->dlpi_addr + segment.p_vaddr);
        const auto* end = at + segment.p_memsz;
        while (at + sizeof(ElfW(Nhdr)) <= end) {
            const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(at);
            const auto* name = at + sizeof(ElfW(Nhdr));
            const auto* desc = name + ((note->n_namesz + 3) & ~3u);
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
                search->id = hex(std::span(desc, note->n_descsz));
                return 1;
            }
            at = desc + ((note->n_descsz + 3) & ~3u);
        }
    }
    return 1;
}
#endif

} // namespace

std::string own_build() {
    static const std::string build = [] {
        std::string identity;
        const void* address = reinterpret_cast<const void*>(&own_build);
#if defined(__linux__)
        BuildIdSearch search{reinterpret_cast<std::uintptr_t>(address), {}};
        dl_iterate_phdr(find_build_id, &search);
        if (!search.id.empty()) identity = "build-id " + search.id;
#endif
        if (identity.empty()) {
            Dl_info info{};
            struct stat status{};
            if (::dladdr(address, &info) != 0 && info.dli_fname && ::stat(info.dli_fname, &status) == 0) {
                std::ostringstream text;
                text << "file " << status.st_dev << ':' << status.st_ino << ':' << status.st_size << ':'
                     << status.st_mtime;
                identity = text.str();
            } else {
                identity = "unknown";
            }
        }
#ifdef NEURA_SERVICE_TEST_BUILD
        // A test supervisor of another build (tests/service, case 11).
        identity += " " NEURA_SERVICE_TEST_BUILD;
#endif
        return neura::tool::version_line() + "; " + identity;
    }();
    return build;
}

// --- Files ----------------------------------------------------------------------

namespace {

std::string short_digest(std::string_view text) { return util::sha256_hex(text).substr(0, 16); }

} // namespace

std::string endpoint_of(const fs::path& root, std::string_view generation) {
    std::string text = root.string();
    text += '\0';
    text += generation;
    return short_digest(text);
}

Installation installation_of(const fs::path& executable) {
    Installation installation;
    std::error_code error;
    auto real = fs::canonical(executable, error);
    if (error) real = executable;
    installation.bin = real.parent_path();
    const auto detection = lifecycle::detect_install(real);
    if (detection.kind == lifecycle::Detection::Kind::Installed) {
        installation.root = fs::canonical(detection.root, error);
        if (error) installation.root = detection.root;
        installation.generation = detection.running_generation;
        installation.installed = true;
        // The lifecycle's own rule (docs/design/Lifecycle030.md): a schema
        // 1 marker and a core component manifest in the running generation,
        // which only a 0.3 archive placed by a 0.2 updater has.
        installation.unmigrated = lifecycle::needs_migration(detection);
    } else {
        installation.root = installation.bin;
        installation.generation = "dev";
    }
    installation.id = short_digest(installation.root.string());
    return installation;
}

Installation installation_at_root(const fs::path& root) {
    Installation installation;
    std::error_code error;
    installation.root = fs::canonical(root, error);
    if (error) installation.root = root;
    installation.generation = fs::read_symlink(installation.root / "current", error).filename().string();
    installation.bin = installation.root / "versions" / installation.generation / "bin";
    installation.installed = true;
    installation.id = short_digest(installation.root.string());
    return installation;
}

bool private_directory(const fs::path& directory) {
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0) return false;
    return S_ISDIR(status.st_mode) && status.st_uid == ::geteuid() && (status.st_mode & 0777) == 0700;
}

void make_private_directory(const fs::path& directory) {
    if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        throw TransportError("cannot create " + directory.string() + ": " + std::strerror(errno));
    }
    if (!private_directory(directory)) {
        throw TransportError(directory.string() + " is not a directory of this user with mode 0700");
    }
}

fs::path run_directory(bool create) {
    const auto uid = ::geteuid();
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg == '/') {
        if (private_directory(xdg)) return fs::path(xdg);
    }
    const char* tmp = std::getenv("TMPDIR");
    fs::path base = tmp && *tmp == '/' ? fs::path(tmp) : fs::path("/tmp");
    const auto directory = base / ("neura-" + std::to_string(uid));
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0) {
        if (!create) return directory;
        make_private_directory(directory);
        return directory;
    }
    if (!private_directory(directory)) {
        throw TransportError(directory.string() + " is refused: it must be a directory of this user with mode 0700");
    }
    return directory;
}

Endpoint endpoint_files(const fs::path& run, const fs::path& root, std::string_view generation) {
    Endpoint endpoint;
    endpoint.directory = run / "neura" / endpoint_of(root, generation);
    endpoint.socket = endpoint.directory / "s";
    endpoint.lock = endpoint.directory / "lock";
    endpoint.start = endpoint.directory / "start";
    if (endpoint.socket.native().size() >= sizeof(sockaddr_un::sun_path)) {
        throw TransportError("the socket path " + endpoint.socket.string() + " is longer than this system allows (" +
                             std::to_string(sizeof(sockaddr_un::sun_path) - 1) + " bytes); the service is unavailable");
    }
    return endpoint;
}

fs::path state_root(const Installation& installation) {
    fs::path base;
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state == '/') {
        base = state;
    } else if (const char* home = std::getenv("HOME"); home && *home == '/') {
        base = fs::path(home) / ".local/state";
    } else {
        base = fs::path("/tmp") / ("neura-state-" + std::to_string(::geteuid()));
    }
    return base / "neura" / installation.id;
}

fs::path state_directory(const Installation& installation, std::string_view generation) {
    return state_root(installation) / std::string(generation);
}

void write_atomically(const fs::path& path, std::string_view text) {
    auto temporary = path;
    temporary += ".tmp" + std::to_string(::getpid());
    {
        const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) throw TransportError("cannot write " + path.string() + ": " + std::strerror(errno));
        UniqueFd owned(fd);
        std::size_t done = 0;
        while (done < text.size()) {
            const auto count = ::write(fd, text.data() + done, text.size() - done);
            if (count < 0) {
                if (errno == EINTR) continue;
                ::unlink(temporary.c_str());
                throw TransportError("cannot write " + path.string() + ": " + std::strerror(errno));
            }
            done += static_cast<std::size_t>(count);
        }
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        throw TransportError("cannot write " + path.string() + ": " + std::strerror(errno));
    }
}

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

std::vector<std::string> live_generations(const Installation& installation) {
    std::vector<std::string> generations;
    if (installation.installed) {
        const auto detection = lifecycle::detect_install(installation.bin / "neura");
        if (detection.kind == lifecycle::Detection::Kind::Installed) generations = detection.marker.generations;
    }
    if (std::ranges::find(generations, installation.generation) == generations.end()) {
        generations.push_back(installation.generation);
    }
    return generations;
}

std::vector<std::string> recorded_generations(const Installation& installation) {
    std::vector<std::string> generations;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(state_root(installation), error)) {
        if (entry.is_directory(error)) generations.push_back(entry.path().filename().string());
    }
    std::ranges::sort(generations);
    return generations;
}

char** process_environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

void clear_environment() {
#if defined(__linux__)
    ::clearenv();
#else
    std::vector<std::string> names;
    for (char** entry = process_environment(); entry && *entry; ++entry) {
        const std::string_view text(*entry);
        names.emplace_back(text.substr(0, text.find('=')));
    }
    for (const auto& name : names) ::unsetenv(name.c_str());
#endif
}

// --- Process settings -----------------------------------------------------------

int resource_of(std::uint8_t resource) {
    switch (static_cast<ResourceKind>(resource)) {
    case ResourceKind::Nofile: return RLIMIT_NOFILE;
    case ResourceKind::Stack: return RLIMIT_STACK;
    case ResourceKind::Core: return RLIMIT_CORE;
    case ResourceKind::Fsize: return RLIMIT_FSIZE;
    case ResourceKind::As: return RLIMIT_AS;
    case ResourceKind::Cpu: return RLIMIT_CPU;
    }
    return -1;
}

std::uint64_t signal_bit(int signal) { return signal > 0 && signal <= 64 ? std::uint64_t{1} << (signal - 1) : 0; }

void capture_process_settings(Envelope& envelope) {
    const auto mask = ::umask(0);
    ::umask(mask);
    envelope.umask = static_cast<std::uint32_t>(mask);
    envelope.resource_limits.clear();
    for (std::uint8_t resource = 1; resource <= 6; ++resource) {
        rlimit limit{};
        if (::getrlimit(resource_of(resource), &limit) != 0) continue;
        const auto value = [](rlim_t v) { return v == RLIM_INFINITY ? unlimited : static_cast<std::uint64_t>(v); };
        envelope.resource_limits.push_back({resource, value(limit.rlim_cur), value(limit.rlim_max)});
    }
    errno = 0;
    const int nice_value = ::getpriority(PRIO_PROCESS, 0);
    envelope.nice = errno == 0 ? nice_value : 0;
    envelope.ignored_signals = 0;
    for (int signal = 1; signal < NSIG && signal <= 64; ++signal) {
        struct sigaction action{};
        if (::sigaction(signal, nullptr, &action) == 0 && action.sa_handler == SIG_IGN) {
            envelope.ignored_signals |= signal_bit(signal);
        }
    }
    sigset_t blocked;
    sigemptyset(&blocked);
    ::sigprocmask(SIG_BLOCK, nullptr, &blocked);
    envelope.blocked_signals = 0;
    for (int signal = 1; signal < NSIG && signal <= 64; ++signal) {
        if (sigismember(&blocked, signal) == 1) envelope.blocked_signals |= signal_bit(signal);
    }
}

std::uint64_t clock_ns(Clock::time_point point) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(point.time_since_epoch()).count());
}

Clock::time_point clock_at(std::uint64_t ns) {
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(ns)));
}

std::string milliseconds(std::uint64_t ns) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", static_cast<double>(ns) / 1e6);
    return text;
}

} // namespace neura::service

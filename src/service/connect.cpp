// Finding, starting and greeting a supervisor (docs/design/Service030.md,
// "Files", "Starting, retiring and login start"), for the client of a
// launch and for the `neura runtime` commands.

#include "service.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include "cli/tool.hpp"
#include "platform/error.hpp"
#include "platform/file.hpp"
#include "platform/process.hpp"
#include "platform/socket.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace neura::service {

using Millis = std::chrono::milliseconds;

// --- Connecting -------------------------------------------------------------------

namespace {

#ifdef _WIN32
UniqueFd connect_to(const fs::path& socket_path) {
    if (platform::ensure_sockets()) return {};
    const SOCKET socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket == INVALID_SOCKET) return {};
    UniqueFd fd(static_cast<Descriptor>(socket), UniqueFd::Kind::socket);
    set_cloexec(fd.get());
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto text = platform::native_utf8_path(socket_path);
    if (text.size() >= sizeof address.sun_path) return {};
    std::memcpy(address.sun_path, text.data(), text.size());
    if (::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) return {};
    if (!platform::set_nonblocking(static_cast<platform::native_socket>(socket))) return {};
    return fd;
}
#else
UniqueFd connect_to(const fs::path& socket_path) {
    int type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif
    UniqueFd fd(::socket(AF_UNIX, type, 0));
    if (!fd) return {};
    set_cloexec(fd.get());
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof address.sun_path - 1);
    if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) return {};
#ifdef SO_NOSIGPIPE
    const int one = 1;
    ::setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return fd;
}
#endif

Message hello_message(const Installation& installation, Purpose purpose) {
    Message hello;
    hello.kind = Kind::Hello;
    hello.sequence = 1;
    hello.add_u16(Tag::ProtocolLow, protocol_major)
        .add_u16(Tag::ProtocolHigh, protocol_major)
        .add(Tag::Build, own_build())
        .add(Tag::Installation, installation.id)
        .add(Tag::Generation, installation.generation)
        .add_u8(Tag::Purpose, static_cast<std::uint8_t>(purpose));
    return hello;
}

// The identity of the peer: this user, this installation. Throws NotRun
// with the identity reason, which `auto` does not fall back from.
std::optional<Session> greet(UniqueFd fd, const Installation& installation, Purpose purpose,
                             Clock::time_point deadline) {
#ifdef _WIN32
    const auto peer = peer_identity(fd.get());
    if (!peer.known || !peer.same_user) {
        throw NotRun{"the service's socket belongs to another user; nothing was run", false, "identity"};
    }
    // An elevated and a normal process of one user share the run directory,
    // but not a service: the supervisor runs programs at its own level.
    if (peer.integrity != 0) {
        throw NotRun{std::string("the service runs at a ") + (peer.integrity < 0 ? "lower" : "higher") +
                         " integrity level than this process (an elevated and a normal process of one user do not "
                         "share it); nothing was run",
                     true, "identity"};
    }
#else
    const auto uid = peer_uid(fd.get());
    if (!uid || *uid != ::geteuid()) {
        throw NotRun{"the service's socket belongs to another user; nothing was run", false, "identity"};
    }
#endif
    try {
        send_message(fd.get(), hello_message(installation, purpose), {}, deadline);
        auto welcome = receive_message(fd.get(), deadline);
        if (!welcome) return std::nullopt;
        if (welcome->message.kind == Kind::Rejected) {
            const auto reason = std::string(welcome->message.get(Tag::Reason));
            throw NotRun{std::string(welcome->message.get(Tag::Sentence)) + "; nothing was run",
                         reason != "identity" && reason != "protocol", reason};
        }
        if (welcome->message.kind != Kind::Welcome) throw ProtocolError("expected Welcome");
        if (welcome->message.get(Tag::Installation) != installation.id) {
            throw NotRun{"the supervisor that answered serves another installation; nothing was run", false, "identity"};
        }
        Session session;
        session.fd = std::move(fd);
        session.welcome = std::move(welcome->message);
        session.sequence = 1;
        return session;
    } catch (const TransportError&) {
        return std::nullopt;
    } catch (const ProtocolError& error) {
        throw NotRun{std::string("the supervisor sent a malformed message (") + error.what() + "); nothing was run",
                     false, "protocol"};
    }
}

#ifdef _WIN32
// Starts `neurad --foreground --root ROOT --on-demand` with no console, in a
// process group of its own and outside the caller's job, so that it outlives
// the terminal and the command that started it (W29); in the caller's job
// when that job does not allow breaking away. Its input is NUL, its output
// and errors are appended to its log.
bool start_supervisor(const Installation& installation, const fs::path& log, bool on_demand) {
    std::error_code error;
    auto output = platform::open_no_follow(log, platform::OpenAccess::append, platform::OpenDisposition::open_or_create,
                                           error);
    if (!output) return false;
    platform::Spawn spawn;
    spawn.program = installation.bin / tool::executable_file("neurad");
    spawn.arguments = {platform::native_utf8_path(spawn.program), "--foreground", "--root",
                       platform::native_utf8_path(installation.root)};
    if (on_demand) spawn.arguments.emplace_back("--on-demand");
    spawn.input = platform::Stdio::null();
    spawn.output = platform::Stdio::from(output.get());
    spawn.error = platform::Stdio::from(output.get());
    spawn.detached = true;
    spawn.breakaway = true;
    auto process = platform::Process::start(spawn, error);
    if (!process) {
        spawn.breakaway = false;
        process = platform::Process::start(spawn, error);
    }
    return static_cast<bool>(process);
}
#else
// Starts `neurad --foreground --root ROOT --on-demand` in a new session,
// its streams on /dev/null and its log.
bool start_supervisor(const Installation& installation, const fs::path& log, bool on_demand) {
    const auto neurad = installation.bin / "neurad";
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t none;
    sigemptyset(&none);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &all);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_SETSID
    flags |= POSIX_SPAWN_SETSID;
#endif
    posix_spawnattr_setflags(&attributes, flags);
    std::vector<std::string> arguments{neurad.string(), "--foreground", "--root", installation.root.string()};
    if (on_demand) arguments.emplace_back("--on-demand");
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, neurad.c_str(), &actions, &attributes, argv.data(), process_environment());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    return error == 0;
}
#endif

} // namespace

Session connect_service(const Installation& installation, std::string_view generation, Purpose purpose,
                        StartMode start_mode) {
    const bool start = start_mode != StartMode::No;
    fs::path run;
    Endpoint endpoint;
    try {
        run = run_directory(start);
        endpoint = endpoint_files(run, installation.root, generation);
    } catch (const TransportError& error) {
        throw NotRun{error.what(), true, "unavailable"};
    }
#ifdef _WIN32
    const auto check_private = [&](const fs::path& directory) {
        std::error_code error;
        if (fs::symlink_status(directory, error).type() != fs::file_type::not_found && !error &&
            !private_directory(directory)) {
            throw NotRun{platform::utf8_path(directory) +
                             " is not a directory of this user that only this user can open; nothing was run",
                         false, "identity"};
        }
    };
    // <run>, or the private directory under %TEMP% that holds the endpoint.
    const auto parent = endpoint.directory.parent_path();
    check_private(parent);
#else
    const auto check_private = [&](const fs::path& directory) {
        struct stat status{};
        if (::lstat(directory.c_str(), &status) == 0 && !private_directory(directory)) {
            throw NotRun{directory.string() + " is not a directory of this user with mode 0700; nothing was run", false,
                         "identity"};
        }
    };
    check_private(run / "neura");
#endif
    check_private(endpoint.directory);
    const auto attempt = [&](Millis wait) -> std::optional<Session> {
        auto fd = connect_to(endpoint.socket);
        if (!fd) return std::nullopt;
        return greet(std::move(fd), installation, purpose, Clock::now() + wait);
    };
    if (auto session = attempt(Millis(250))) return std::move(*session);
    if (!start) throw NotRun{"no supervisor is running", true, "not-running"};
#ifdef _WIN32
    const auto state = state_directory(installation, generation);
    const auto log = state / "log";
    try {
        make_private_directory(parent);
        make_private_directory(endpoint.directory);
        // Private before the log is opened in it.
        make_state_root(installation);
    } catch (const TransportError& error) {
        throw NotRun{std::string(error.what()) + "; nothing was run", false, "identity"};
    }
    std::error_code error;
    fs::create_directories(state, error);
    // Clients that arrive meanwhile wait on the start lock and find the
    // supervisor started.
    platform::FileLock lock;
    const auto deadline = Clock::now() + Millis(2000);
    for (;;) {
        lock = platform::FileLock::acquire(endpoint.start, platform::LockMode::exclusive, platform::LockWait::try_once,
                                           error);
        if (lock || error != std::errc::resource_unavailable_try_again || Clock::now() >= deadline) break;
        std::this_thread::sleep_for(Millis(5));
    }
    if (auto session = attempt(Millis(250))) return std::move(*session);
    if (!lock) {
        throw NotRun{"the service start lock could not be acquired; see " + platform::utf8_path(log), true,
                     "start-failed"};
    }
    if (!start_supervisor(installation, log, start_mode == StartMode::OnDemand)) {
        throw NotRun{"the service could not be started; see " + platform::utf8_path(log), true, "start-failed"};
    }
    while (Clock::now() < deadline) {
        if (auto session = attempt(Millis(250))) return std::move(*session);
        std::this_thread::sleep_for(Millis(5));
    }
    throw NotRun{"the service did not start within 2 seconds; see " + platform::utf8_path(log), true, "start-timeout"};
#else
    try {
        make_private_directory(run / "neura");
        make_private_directory(endpoint.directory);
    } catch (const TransportError& error) {
        throw NotRun{std::string(error.what()) + "; nothing was run", false, "identity"};
    }
    const auto state = state_directory(installation, generation);
    std::error_code error;
    fs::create_directories(state, error);
    const auto log = state / "log";
    // Clients that arrive meanwhile wait on the start lock and find the
    // supervisor started.
    UniqueFd lock(::open(endpoint.start.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
    const auto deadline = Clock::now() + Millis(2000);
    if (lock) {
        while (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0 && Clock::now() < deadline) {
            std::this_thread::sleep_for(Millis(5));
        }
    }
    if (auto session = attempt(Millis(250))) return std::move(*session);
    if (!start_supervisor(installation, log, start_mode == StartMode::OnDemand)) {
        throw NotRun{"the service could not be started; see " + log.string(), true, "start-failed"};
    }
    while (Clock::now() < deadline) {
        if (auto session = attempt(Millis(250))) return std::move(*session);
        std::this_thread::sleep_for(Millis(5));
    }
    throw NotRun{"the service did not start within 2 seconds; see " + log.string(), true, "start-timeout"};
#endif
}

} // namespace neura::service

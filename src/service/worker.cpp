// The worker of the runtime service (docs/design/Service030.md, "The worker"):
// a process started by a supervisor before it knows its application, given
// one launch, and ended with it.

#include "service.hpp"

#include "cli/tool.hpp"
#include "platform/text.hpp"

#include "cli/commands.hpp"
#include "nvm/environment.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include "platform/console.hpp"
#include "platform/environment.hpp"
#include "platform/error.hpp"
#include "platform/socket.hpp"
#include "platform/windows.hpp"

#include <fcntl.h>
#include <io.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if defined(__APPLE__)
#include <sys/event.h>
#endif
#endif

namespace neura::service {

namespace {

[[noreturn]] void refuse(const std::string& message) {
    std::fprintf(stderr, "neura: neurad --worker: %s\n", message.c_str());
    std::exit(1);
}

// The launch, once the worker has applied its settings: reports that it
// started, runs the image with the code and the messages of a direct run,
// and reports how the run ended.
int run_launch(Descriptor channel, const Envelope& envelope, const std::uint8_t* image) {
    Message started;
    started.kind = Kind::WorkerStarted;
    started.add(Tag::Time, named_value("worker-started", clock_ns(Clock::now())));
    send_message(channel, started);

    // The run, with the code and the messages of a direct run.
    auto options = options_of(envelope);
    const auto client_start = clock_at(envelope.client_clock);
    runner::RunOptions::Ending ending;
    Terminal terminal = Terminal::LoadFailed;
    int exit_status = 1;
    try {
        exit_status = cli::run_image(options, std::span(image, envelope.image_size), platform::path_from_utf8(envelope.image_path),
                                     options.tier_stats ? std::optional(client_start) : std::nullopt, &ending);
        terminal = ending.trapped ? Terminal::Trapped : Terminal::Exited;
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
        terminal = ending.started ? Terminal::Exited : Terminal::LoadFailed;
    }
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
    Message finished;
    finished.kind = Kind::WorkerFinished;
    finished.add_u8(Tag::Terminal, static_cast<std::uint8_t>(terminal))
        .add_i64(Tag::ExitStatus, exit_status)
        .add(Tag::Time, named_value("loaded", ending.loaded ? clock_ns(ending.loaded_at) : 0))
        .add(Tag::Time, named_value("run-started", ending.started ? clock_ns(ending.started_at) : 0))
        .add(Tag::Time, named_value("exit", clock_ns(Clock::now())));
    if (ending.trapped) finished.add(Tag::TrapClass, ending.trap_class);
    try {
        send_message(channel, finished);
    } catch (const std::exception&) {
        // The supervisor is gone; the worker ends with it.
    }
    return exit_status;
}

#ifdef _WIN32

HANDLE handle_of(Descriptor descriptor) { return reinterpret_cast<HANDLE>(descriptor); }

// The path of an open directory, without the \\?\ prefix of a final path.
std::optional<fs::path> directory_path(HANDLE directory) {
    std::wstring text(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetFinalPathNameByHandleW(directory, text.data(), static_cast<DWORD>(text.size()),
                                                         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) return std::nullopt;
        if (length < text.size()) {
            text.resize(length);
            break;
        }
        text.resize(length + 1);
    }
    if (text.starts_with(L"\\\\?\\UNC\\")) return fs::path(L"\\\\" + text.substr(8));
    if (text.starts_with(L"\\\\?\\")) return fs::path(text.substr(4));
    return fs::path(text);
}

// Installs `handle` as C runtime descriptor `fd` and as the standard handle
// `which`, so that the C runtime, the C++ streams and code that asks for
// GetStdHandle see the same stream.
void install_stream(UniqueFd& handle, int fd, DWORD which) {
    const int temporary = ::_open_osfhandle(static_cast<std::intptr_t>(handle.get()), fd == 0 ? _O_RDONLY : _O_WRONLY);
    if (temporary < 0) refuse("cannot install a stream");
    handle.release();
    if (::_dup2(temporary, fd) != 0) refuse("cannot install a stream");
    ::_close(temporary);
    ::SetStdHandle(which, reinterpret_cast<HANDLE>(::_get_osfhandle(fd)));
}

#else

// Moves `fd` to a descriptor at or above `floor`, close-on-exec.
int lift(int fd, int floor) {
    const int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, floor);
    if (moved < 0) refuse(std::string("cannot move a descriptor: ") + std::strerror(errno));
    return moved;
}

// Closes every descriptor at or above `first`, except `keep`.
void close_from(int first, int keep) {
#if defined(__linux__)
    const char* listing = "/proc/self/fd";
#else
    const char* listing = "/dev/fd";
#endif
    std::vector<int> open;
    if (DIR* directory = ::opendir(listing)) {
        const int own = ::dirfd(directory);
        while (const dirent* entry = ::readdir(directory)) {
            const int fd = std::atoi(entry->d_name);
            if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9' && fd >= first && fd != own && fd != keep) open.push_back(fd);
        }
        ::closedir(directory);
    } else {
        rlimit limit{};
        ::getrlimit(RLIMIT_NOFILE, &limit);
        const auto top = std::min<rlim_t>(limit.rlim_cur, 65536);
        for (int fd = first; fd < static_cast<int>(top); ++fd) {
            if (fd != keep) open.push_back(fd);
        }
    }
    for (const int fd : open) ::close(fd);
}

#if defined(__APPLE__)
// macOS has no parent-death signal: a thread waits for the supervisor to
// exit and ends the worker with it. Untested on this milestone's machines.
void watch_supervisor(pid_t supervisor) {
    std::thread([supervisor] {
        const int queue = ::kqueue();
        if (queue < 0) return;
        struct kevent change;
        EV_SET(&change, supervisor, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        struct kevent event;
        if (::kevent(queue, &change, 1, &event, 1, nullptr) >= 0 || ::getppid() != supervisor) ::kill(::getpid(), SIGKILL);
    }).detach();
}
#endif

#endif

} // namespace

#ifdef _WIN32

int worker_main(const std::vector<std::string>& arguments) {
    // Nothing reads the cached runtime controls until the caller's
    // environment replaces the supervisor's.
    nvm::hold_environment();
    std::optional<Descriptor> channel;
    std::string build;
    unsigned long long supervisor = 0;
    bool never_ready = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= arguments.size()) refuse("expected a value after " + argument);
            return arguments[++i];
        };
        if (argument == "--channel") channel = static_cast<Descriptor>(std::strtoull(value().c_str(), nullptr, 10));
        else if (argument == "--build") build = value();
        else if (argument == "--supervisor") supervisor = std::strtoull(value().c_str(), nullptr, 10);
        else if (argument == "--test-never-ready") never_ready = true;
        else refuse("unknown argument " + argument);
    }
    if (!channel || build.empty() || supervisor == 0) {
        refuse("this is the worker of the runtime service; it is started by neurad and cannot be run by hand");
    }
    // The channel is a socket the supervisor let this process inherit
    // (PROC_THREAD_ATTRIBUTE_HANDLE_LIST). A layered service provider that is
    // not IFS-compatible keeps an inherited socket from working.
    platform::ensure_sockets();
    int type = 0;
    int length = sizeof type;
    if (::getsockopt(static_cast<SOCKET>(*channel), SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length) != 0 ||
        type != SOCK_STREAM) {
        refuse("the channel from the supervisor is not a socket this process can use (" +
               platform::last_socket_error().message() +
               "); a layered service provider that is not IFS-compatible keeps inherited sockets from working");
    }
    if (build != own_build()) {
        refuse("the supervisor is from another build (" + build + "); this worker is " + own_build());
    }
    // The lease of its generation, held until it exits, so that an
    // application that runs holds the generation it runs from (docs/Install.md,
    // "Generations in use").
    {
        bool removed = false;
        [[maybe_unused]] const auto lease = tool::take_lease(tool::executable_path(""), removed);
        if (removed) refuse("its generation was removed");
    }
    // The worker runs in a kill-on-close job that only its supervisor holds,
    // so it ends with the supervisor; one that is already gone is refused.
    {
        const HANDLE parent = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(supervisor));
        const bool alive = parent != nullptr && ::WaitForSingleObject(parent, 0) == WAIT_TIMEOUT;
        if (parent != nullptr) ::CloseHandle(parent);
        if (!alive) refuse("the supervisor that started this worker is gone");
    }
    set_cloexec(*channel);
    // A test of the supervisor's ready timeout (Service030.md, case 44).
    while (never_ready) ::Sleep(INFINITE);
    try {
        Message ready;
        ready.kind = Kind::WorkerReady;
        ready.add(Tag::Build, own_build()).add_u64(Tag::Pid, static_cast<std::uint64_t>(::GetCurrentProcessId()));
        send_message(*channel, ready);
        auto assignment = receive_message(*channel, std::nullopt);
        if (!assignment) return 0;
        if (assignment->message.kind != Kind::WorkerAssign) refuse("expected an assignment");
        const auto envelope = read_envelope(assignment->message);
        auto& fds = assignment->descriptors;

        // 1. The caller's environment, whole, before anything reads it.
        clear_environment();
        for (const auto& entry : envelope.environment) {
            const auto equal = entry.find('=', 1);
            std::error_code error;
            if (equal == std::string::npos) continue;
            platform::set_env(entry.substr(0, equal), entry.substr(equal + 1), error);
        }
        nvm::bind_environment();

        // 2. The priority class, which stands in for the nice value (W29);
        //    Windows has no umask and no resource limits of that kind.
        if (envelope.nice != 0) ::SetPriorityClass(::GetCurrentProcess(), static_cast<DWORD>(envelope.nice));

        // 3. The caller's working directory, by the path of the directory the
        //    caller sent: Windows cannot enter a directory by its handle.
        const auto directory = directory_path(handle_of(fds[1].get()));
        if (!directory || !::SetCurrentDirectoryW(directory->c_str())) {
            refuse("cannot enter the working directory: " + platform::error_message(static_cast<int>(::GetLastError())));
        }
        fds[1].reset();

        // 4. The worker's job is its application group: stop and the
        //    forwarded signals end the program and whatever it starts.

        // 5. The caller's streams as 0, 1 and 2; the channel stays where it
        //    is, and nothing else is inherited by what the program starts. A
        //    console handle works only in a process attached to its console:
        //    the worker attaches to the caller's, and leaves its events to
        //    the caller, which forwards them.
        const bool console = std::any_of(fds.begin() + 2, fds.end(), [](const UniqueFd& stream) {
            return ::GetFileType(handle_of(stream.get())) == FILE_TYPE_CHAR;
        });
        if (console && assignment->message.has(Tag::Pid) &&
            ::AttachConsole(static_cast<DWORD>(assignment->message.u64(Tag::Pid)))) {
            platform::ignore_console_events();
        }
        install_stream(fds[2], 0, STD_INPUT_HANDLE);
        install_stream(fds[3], 1, STD_OUTPUT_HANDLE);
        install_stream(fds[4], 2, STD_ERROR_HANDLE);
        platform::set_binary_stdio();
        platform::install_console_streams();

        // 6. Windows has no signal dispositions or mask to carry.

        // 7. The image bytes, read-only: the section is a read-only duplicate
        //    of the supervisor's.
        const std::uint8_t* image = nullptr;
        if (envelope.image_size > 0) {
            void* mapped = ::MapViewOfFile(handle_of(fds[0].get()), FILE_MAP_READ, 0, 0,
                                           static_cast<SIZE_T>(envelope.image_size));
            if (mapped == nullptr) {
                refuse("cannot map the image: " + platform::error_message(static_cast<int>(::GetLastError())));
            }
            image = static_cast<const std::uint8_t*>(mapped);
        }
        fds[0].reset();
        const int status = run_launch(*channel, envelope, image);
        // A socket that a process leaves open at its exit is reset, and a
        // reset discards what the supervisor has not read yet: the channel
        // is closed in order, after the report of the run.
        ::shutdown(static_cast<SOCKET>(*channel), SD_SEND);
        ::closesocket(static_cast<SOCKET>(*channel));
        return status;
    } catch (const std::exception& error) {
        refuse(error.what());
    }
}

WorkerProcess spawn_worker(const fs::path& neurad, const std::string& build, const std::vector<std::string>* environment,
                           const std::vector<ResourceLimit>* limits, bool never_ready) {
    // Windows has no resource limits of that kind to start a worker with.
    (void)limits;
    std::error_code error;
    const auto pair = platform::socket_pair(error);
    if (error) throw TransportError("cannot create a worker channel: " + platform::error_message(error));
    UniqueFd own(static_cast<Descriptor>(pair[0]), UniqueFd::Kind::socket);
    UniqueFd theirs(static_cast<Descriptor>(pair[1]), UniqueFd::Kind::socket);

    platform::Spawn spawn;
    spawn.program = neurad;
    spawn.arguments = {platform::native_utf8_path(neurad),
                       "--worker",
                       "--channel",
                       std::to_string(theirs.get()),
                       "--build",
                       build,
                       "--supervisor",
                       std::to_string(::GetCurrentProcessId())};
    if (never_ready) spawn.arguments.emplace_back("--test-never-ready");
    if (environment) {
        std::vector<std::pair<std::string, std::string>> variables;
        for (const auto& entry : *environment) {
            const auto equal = entry.find('=', 1);
            if (equal == std::string::npos) continue;
            variables.emplace_back(entry.substr(0, equal), entry.substr(equal + 1));
        }
        spawn.environment = std::move(variables);
    }
    spawn.input = platform::Stdio::null();
    spawn.output = platform::Stdio::null();
    spawn.error = platform::Stdio::null();
    spawn.inherit = {reinterpret_cast<platform::native_file>(theirs.get())};
    // A worker holds no directory busy until its launch names one: a
    // process's working directory cannot be removed on Windows.
    {
        wchar_t system[MAX_PATH + 1];
        const UINT length = ::GetSystemDirectoryW(system, MAX_PATH + 1);
        if (length != 0 && length <= MAX_PATH) spawn.working_directory = fs::path(std::wstring(system, length));
    }
    // No console: a worker gets the caller's streams with its launch.
    spawn.detached = true;
    spawn.job = platform::JobMode::kill_on_close;
    auto process = platform::Process::start(spawn, error);
    if (!process) {
        throw TransportError("cannot start " + platform::utf8_path(neurad) + ": " + platform::error_message(error));
    }
    WorkerProcess worker;
    worker.pid = static_cast<int>(process.pid());
    worker.channel = std::move(own);
    worker.spawned = Clock::now();
    worker.process = std::move(process);
    return worker;
}

#else

int worker_main(const std::vector<std::string>& arguments) {
    // Nothing reads the cached runtime controls until the caller's
    // environment replaces the supervisor's.
    nvm::hold_environment();
    int channel = -1;
    std::string build;
    long supervisor = -1;
    bool never_ready = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= arguments.size()) refuse("expected a value after " + argument);
            return arguments[++i];
        };
        if (argument == "--channel") channel = std::atoi(value().c_str());
        else if (argument == "--build") build = value();
        else if (argument == "--supervisor") supervisor = std::atol(value().c_str());
        else if (argument == "--test-never-ready") never_ready = true;
        else refuse("unknown argument " + argument);
    }
    struct stat status{};
    if (channel < 0 || build.empty() || supervisor <= 0 || ::fstat(channel, &status) != 0 || !S_ISSOCK(status.st_mode)) {
        refuse("this is the worker of the runtime service; it is started by neurad and cannot be run by hand");
    }
    if (build != own_build()) {
        refuse("the supervisor is from another build (" + build + "); this worker is " + own_build());
    }
    // The lease of its generation, held until it exits, so that an
    // application that runs holds the generation it runs from (docs/Install.md,
    // "Generations in use").
    int lease = -1;
    {
        bool removed = false;
        lease = tool::take_lease(tool::executable_path("").c_str(), removed);
        if (removed) refuse("its generation was removed");
    }
#if defined(__linux__)
    ::prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
    if (::getppid() != static_cast<pid_t>(supervisor)) refuse("the supervisor that started this worker is gone");
#if defined(__APPLE__)
    watch_supervisor(static_cast<pid_t>(supervisor));
#endif
    set_cloexec(channel);
    // A test of the supervisor's ready timeout (Service030.md, case 44).
    while (never_ready) ::pause();
    try {
        Message ready;
        ready.kind = Kind::WorkerReady;
        ready.add(Tag::Build, own_build()).add_u64(Tag::Pid, static_cast<std::uint64_t>(::getpid()));
        send_message(channel, ready);
        auto assignment = receive_message(channel, std::nullopt);
        if (!assignment) return 0;
        if (assignment->message.kind != Kind::WorkerAssign) refuse("expected an assignment");
        const auto envelope = read_envelope(assignment->message);
        auto& fds = assignment->descriptors;

        // 1. The caller's environment, whole, before anything reads it.
        clear_environment();
        for (const auto& entry : envelope.environment) ::putenv(::strdup(entry.c_str()));
        nvm::bind_environment();

        // 2. The umask, the limits and the nice value.
        ::umask(static_cast<mode_t>(envelope.umask));
        for (const auto& limit : envelope.resource_limits) {
            rlimit own{};
            const int resource = resource_of(limit.resource);
            if (::getrlimit(resource, &own) != 0) continue;
            const auto value = [](std::uint64_t v) { return v == unlimited ? RLIM_INFINITY : static_cast<rlim_t>(v); };
            rlimit wanted{value(limit.soft), value(limit.hard)};
            // A hard limit is lowered to the caller's, never raised.
            if (own.rlim_max != RLIM_INFINITY && (wanted.rlim_max == RLIM_INFINITY || wanted.rlim_max > own.rlim_max)) {
                wanted.rlim_max = own.rlim_max;
            }
            ::setrlimit(resource, &wanted);
        }
        errno = 0;
        const int nice_value = ::getpriority(PRIO_PROCESS, 0);
        if (errno == 0 && nice_value != envelope.nice) ::setpriority(PRIO_PROCESS, 0, static_cast<int>(envelope.nice));

        // 3. The caller's working directory.
        if (::fchdir(fds[1].get()) != 0) refuse(std::string("cannot enter the working directory: ") + std::strerror(errno));

        // 4. A process group of its own, so that stop and the forwarded
        //    signals reach the program and whatever it starts.
        ::setpgid(0, 0);

        // 5. The caller's streams as 0, 1 and 2; the channel as 3 and the
        //    image as 4 until it is mapped; nothing else.
        const int high_channel = lift(channel, 100);
        const int high_image = lift(fds[0].get(), 100);
        const int high_streams[3] = {lift(fds[2].get(), 100), lift(fds[3].get(), 100), lift(fds[4].get(), 100)};
        if (lease >= 0) {
            const int high_lease = lift(lease, 100);
            ::close(lease);
            lease = high_lease;
        }
        for (auto& fd : fds) fd.reset();
        for (int stream = 0; stream < 3; ++stream) {
            if (::dup2(high_streams[stream], stream) < 0) refuse("cannot install a stream");
        }
        ::dup2(high_channel, 3);
        ::dup2(high_image, 4);
        set_cloexec(3);
        set_cloexec(4);
        close_from(5, lease);
        channel = 3;

        // 6. The signal dispositions and mask a direct run would inherit.
        for (int signal = 1; signal < NSIG && signal <= 64; ++signal) {
            if (signal == SIGKILL || signal == SIGSTOP) continue;
            struct sigaction action{};
            action.sa_handler = (envelope.ignored_signals & signal_bit(signal)) ? SIG_IGN : SIG_DFL;
            sigemptyset(&action.sa_mask);
            ::sigaction(signal, &action, nullptr);
        }
        sigset_t mask;
        sigemptyset(&mask);
        for (int signal = 1; signal < NSIG && signal <= 64; ++signal) {
            if (envelope.blocked_signals & signal_bit(signal)) sigaddset(&mask, signal);
        }
        ::sigprocmask(SIG_SETMASK, &mask, nullptr);

        // 7. The image bytes, read-only.
        const std::uint8_t* image = nullptr;
        if (envelope.image_size > 0) {
            void* mapped = ::mmap(nullptr, envelope.image_size, PROT_READ, MAP_PRIVATE, 4, 0);
            if (mapped == MAP_FAILED) refuse(std::string("cannot map the image: ") + std::strerror(errno));
            image = static_cast<const std::uint8_t*>(mapped);
        }
        ::close(4);

        return run_launch(channel, envelope, image);
    } catch (const std::exception& error) {
        refuse(error.what());
    }
}

WorkerProcess spawn_worker(const fs::path& neurad, const std::string& build, const std::vector<std::string>* environment,
                           const std::vector<ResourceLimit>* limits, bool never_ready) {
    int pair[2];
    int type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif
    if (::socketpair(AF_UNIX, type, 0, pair) != 0) {
        throw TransportError(std::string("cannot create a worker channel: ") + std::strerror(errno));
    }
    UniqueFd own(pair[0]);
    UniqueFd theirs(pair[1]);
    set_cloexec(own.get());
    set_cloexec(theirs.get());

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, theirs.get(), 3);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t none;
    sigemptyset(&none);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &all);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    std::vector<std::string> arguments{neurad.string(), "--worker",       "--channel", "3",
                                       "--build",       build,            "--supervisor",
                                       std::to_string(::getpid())};
    if (never_ready) arguments.emplace_back("--test-never-ready");
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::vector<std::string> entries;
    std::vector<char*> envp;
    if (environment) {
        entries = *environment;
        for (auto& entry : entries) envp.push_back(entry.data());
        envp.push_back(nullptr);
    }

    // posix_spawn has no limit attribute: the supervisor, which has one
    // thread, sets the caller's limits on itself around the spawn.
    std::vector<std::pair<int, rlimit>> saved;
    if (limits) {
        for (const auto& limit : *limits) {
            const int resource = resource_of(limit.resource);
            rlimit current{};
            if (::getrlimit(resource, &current) != 0) continue;
            rlimit wanted = current;
            wanted.rlim_cur = limit.soft == unlimited ? RLIM_INFINITY : static_cast<rlim_t>(limit.soft);
            if (wanted.rlim_cur == current.rlim_cur) continue;
            if (::setrlimit(resource, &wanted) == 0) saved.emplace_back(resource, current);
        }
    }
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, neurad.c_str(), &actions, &attributes, argv.data(),
                                    environment ? envp.data() : process_environment());
    for (const auto& [resource, previous] : saved) ::setrlimit(resource, &previous);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error != 0) throw TransportError("cannot start " + neurad.string() + ": " + std::strerror(error));
    WorkerProcess worker;
    worker.pid = pid;
    worker.channel = std::move(own);
    worker.spawned = Clock::now();
    return worker;
}

#endif

} // namespace neura::service

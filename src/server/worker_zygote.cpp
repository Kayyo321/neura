#include "server/worker_zygote.hpp"

#ifdef _WIN32
#include <iostream>

namespace neura::server {

// Windows has no fork, and cloning a process that uses the C runtime and
// Winsock is unsupported: workers start with exec (W27).
bool zygote_supported() { return false; }

int run_worker_zygote(const WorkerOptions& options) {
    (void)options;
    std::cerr << "neura-server-host: the worker zygote needs Linux\n";
    return 70;
}

std::unique_ptr<ZygoteClient> ZygoteClient::start(const std::filesystem::path& executable,
                                                  const std::vector<std::string>& arguments, std::string& problem) {
    (void)executable;
    (void)arguments;
    problem = "the worker zygote needs Linux";
    return nullptr;
}

ZygoteClient::~ZygoteClient() { stop(); }

// No ZygoteClient exists on Windows; 0 names no worker.
worker_pid ZygoteClient::fork_worker(int channel, unsigned char flags) {
    (void)channel;
    (void)flags;
    failed_ = true;
    return 0;
}

void ZygoteClient::stop() { control_ = -1; }

} // namespace neura::server
#else
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

extern char** environ;

namespace neura::server {
namespace {

#ifdef __linux__
void flush_output() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
}

bool send_all(int descriptor, const void* data, std::size_t size) {
    for (;;) {
        const auto sent = ::send(descriptor, data, size, MSG_NOSIGNAL);
        if (sent == static_cast<ssize_t>(size)) return true;
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }
}
#endif // The two helpers serve the Linux zygote only.

// Waits up to `timeout` for `size` bytes, one message of the control channel.
bool receive_exact(int descriptor, void* data, std::size_t size, std::chrono::milliseconds timeout) {
    pollfd entry{descriptor, POLLIN, 0};
    for (;;) {
        const int ready = ::poll(&entry, 1, static_cast<int>(timeout.count()));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        const auto got = ::recv(descriptor, data, size, 0);
        if (got < 0 && errno == EINTR) continue;
        return got == static_cast<ssize_t>(size);
    }
}

} // namespace

bool zygote_supported() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

int run_worker_zygote(const WorkerOptions& options) {
#ifdef __linux__
    constexpr int control = worker_channel_descriptor;
    ::prctl(PR_SET_NAME, "neura-zygote", 0, 0, 0);
    std::optional<PreparedImage> prepared;
    try {
        prepared.emplace(prepare_image(options));
    } catch (const std::exception& error) {
        std::cerr << "neura-server-host: worker zygote: " << error.what() << '\n';
        return 70;
    }
    const unsigned char loaded = 'R';
    if (!send_all(control, &loaded, 1)) return 70;
    for (;;) {
        unsigned char flags = 0;
        iovec io{&flags, 1};
        alignas(cmsghdr) char space[CMSG_SPACE(sizeof(int))];
        msghdr message{};
        message.msg_iov = &io;
        message.msg_iovlen = 1;
        message.msg_control = space;
        message.msg_controllen = sizeof space;
        const auto got = ::recvmsg(control, &message, MSG_CMSG_CLOEXEC);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return got == 0 ? 0 : 70; // The host closed the channel, or it failed.
        int channel = -1;
        for (cmsghdr* entry = CMSG_FIRSTHDR(&message); entry != nullptr; entry = CMSG_NXTHDR(&message, entry)) {
            if (entry->cmsg_level == SOL_SOCKET && entry->cmsg_type == SCM_RIGHTS &&
                entry->cmsg_len == CMSG_LEN(sizeof(int))) {
                std::memcpy(&channel, CMSG_DATA(entry), sizeof(int));
            }
        }
        std::int32_t reply = -EBADF;
        if (channel >= 0) {
            flush_output();
            // CLONE_PARENT: the worker is a child of the host, not of the
            // zygote. With no new stack the call returns twice, as fork does.
            // The zygote has one thread and holds no lock here.
            const long child = ::syscall(SYS_clone, CLONE_PARENT | SIGCHLD, nullptr, nullptr, nullptr, nullptr);
            if (child == 0) {
                // dup2 onto descriptor 3 closes the control channel.
                ::dup2(channel, worker_channel_descriptor);
                ::close(channel);
                ::prctl(PR_SET_NAME, "neura-worker", 0, 0, 0);
                auto worker_options = options;
                worker_options.refuse_assign = (flags & ZygoteClient::refuse_assign_flag) != 0;
                int status = 1;
                try {
                    status = serve_assignment(worker_options, *prepared);
                } catch (const std::exception& error) {
                    std::cerr << "neura-server-host: " << error.what() << '\n';
                }
                flush_output();
                ::_exit(status);
            }
            reply = child < 0 ? -errno : static_cast<std::int32_t>(child);
            ::close(channel);
        }
        unsigned char bytes[4];
        for (int i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(static_cast<std::uint32_t>(reply) >> (8 * i));
        if (!send_all(control, bytes, sizeof bytes)) return 70;
    }
#else
    (void)options;
    std::cerr << "neura-server-host: the worker zygote needs Linux\n";
    return 70;
#endif
}

std::unique_ptr<ZygoteClient> ZygoteClient::start(const std::filesystem::path& executable,
                                                  const std::vector<std::string>& arguments, std::string& problem) {
#ifdef __linux__
    int pair[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) {
        problem = std::string("socketpair: ") + std::strerror(errno);
        return nullptr;
    }
    // dup2 onto the control descriptor clears close-on-exec, except when the
    // descriptor is already that number.
    if (pair[1] == worker_channel_descriptor) {
        const int moved = ::fcntl(pair[1], F_DUPFD_CLOEXEC, worker_channel_descriptor + 1);
        ::close(pair[1]);
        pair[1] = moved;
    }
    std::vector<std::string> words{executable.string(), "--zygote"};
    words.insert(words.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& word : words) argv.push_back(word.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pair[1], worker_channel_descriptor);
    // The image's console output goes to the host's standard error.
    posix_spawn_file_actions_adddup2(&actions, 2, 1);
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pair[1]);
    if (error != 0) {
        ::close(pair[0]);
        problem = std::string("posix_spawn: ") + std::strerror(error);
        return nullptr;
    }
    std::unique_ptr<ZygoteClient> client(new ZygoteClient(pid, pair[0]));
    unsigned char loaded = 0;
    if (!receive_exact(pair[0], &loaded, 1, std::chrono::seconds(60)) || loaded != 'R') {
        problem = "the zygote did not load the image";
        client->stop();
        return nullptr;
    }
    return client;
#else
    (void)executable;
    (void)arguments;
    problem = "the worker zygote needs Linux";
    return nullptr;
#endif
}

ZygoteClient::~ZygoteClient() { stop(); }

pid_t ZygoteClient::fork_worker(int channel, unsigned char flags) {
    std::lock_guard lock(mutex_);
    if (control_ < 0) return -1;
    iovec io{&flags, 1};
    alignas(cmsghdr) char space[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = space;
    message.msg_controllen = sizeof space;
    cmsghdr* entry = CMSG_FIRSTHDR(&message);
    entry->cmsg_level = SOL_SOCKET;
    entry->cmsg_type = SCM_RIGHTS;
    entry->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(entry), &channel, sizeof(int));
    ssize_t sent;
    do {
        sent = ::sendmsg(control_, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    unsigned char bytes[4];
    if (sent != 1 || !receive_exact(control_, bytes, sizeof bytes, std::chrono::seconds(10))) {
        failed_ = true;
        return -1;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[i]) << (8 * i);
    const auto reply = static_cast<std::int32_t>(value);
    // A negative reply is the errno of a failed fork; the zygote itself is
    // still usable.
    return reply > 0 ? static_cast<pid_t>(reply) : -1;
}

void ZygoteClient::stop() {
    std::lock_guard lock(mutex_);
    if (control_ >= 0) {
        ::close(control_);
        control_ = -1;
    }
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
        pid_ = -1;
    }
}

} // namespace neura::server
#endif

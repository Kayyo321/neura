// Linux-only regression for timed and overlapping reads after zero-copy sends.
// Registered with CTest as host-zerocopy-readiness; links with --wrap=recvmsg.
#include "nvm/provider_internal.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <sys/mman.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

#if defined(__linux__) && defined(SO_ZEROCOPY) && defined(MSG_ZEROCOPY)
namespace {
int synthetic_notices;
}

// Loopback coalesces successive notices. Leave a real completion queued
// while simulating a reproducible backlog ahead of it on this same socket.
extern "C" ssize_t __real_recvmsg(int socket, msghdr* message, int flags);
extern "C" ssize_t __wrap_recvmsg(int socket, msghdr* message, int flags) {
    if ((flags & MSG_ERRQUEUE) != 0 && synthetic_notices > 0) {
        --synthetic_notices;
        return 0;
    }
    return __real_recvmsg(socket, message, flags);
}
#endif

int main() {
#if defined(__linux__) && defined(SO_ZEROCOPY) && defined(MSG_ZEROCOPY)
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    assert(::listen(listener, 1) == 0);
    socklen_t size = sizeof address;
    assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    const int writer = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(writer >= 0);
    assert(::connect(writer, reinterpret_cast<sockaddr*>(&address), size) == 0);
    const int reader = ::accept(listener, nullptr, nullptr);
    assert(reader >= 0);
    const int enabled = 1;
    // SO_ZEROCOPY exists on supported Linux hosts: failure is a failure.
    assert(::setsockopt(writer, SOL_SOCKET, SO_ZEROCOPY, &enabled, sizeof enabled) == 0);
    const auto queue_completion = [&] {
        constexpr std::size_t length = 64 * 1024;
        void* staging = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(staging != MAP_FAILED);
        std::memset(staging, 0x5a, length);
        const auto sent = ::send(writer, staging, length, MSG_ZEROCOPY | MSG_DONTWAIT | MSG_NOSIGNAL);
        assert(sent > 0);
        ::munmap(staging, length);
        std::string bytes(static_cast<std::size_t>(sent), '\0');
        std::size_t received = 0;
        while (received < bytes.size()) {
            pollfd readable{reader, POLLIN, 0};
            assert(::poll(&readable, 1, 1000) > 0);
            const auto count = ::recv(reader, bytes.data() + received, bytes.size() - received, MSG_DONTWAIT);
            assert(count > 0);
            received += static_cast<std::size_t>(count);
        }
        assert(bytes == std::string(bytes.size(), '\x5a'));
        pollfd notification{writer, POLLIN, 0};
        assert(::poll(&notification, 1, 1000) > 0);
        assert(notification.revents == POLLERR);
        synthetic_notices = 129;
    };
    using namespace neura::nvm;
    using namespace neura::nvm::provider;
    StandardServices::Tables table;
    table.sockets[1] = {writer};
    Capabilities capabilities;
    capabilities.net = true;
    FunctionService::Handler invoke_read;
    FunctionService::Begin begin_read;
    const Adder add = [](HostSignature, FunctionService::Handler) {};
    const OverlapAdder overlap = [&](HostSignature signature, FunctionService::Handler invoke,
                                     FunctionService::Begin begin) {
        if (signature.name == "neura.net.read") {
            invoke_read = std::move(invoke);
            begin_read = std::move(begin);
        }
    };
    add_net_services(add, overlap, table, capabilities);
    assert(invoke_read && begin_read);
    // Let a regressed blocking recv fail eventually instead of hanging the test.
    const timeval guard{1, 0};
    assert(::setsockopt(writer, SOL_SOCKET, SO_RCVTIMEO, &guard, sizeof guard) == 0);
    queue_completion();
    const auto start = std::chrono::steady_clock::now();
    const auto timed = invoke_read(HostRequest{0, 0, {int64_value(1), int32_value(16), int64_value(40'000'000)}});
    const auto elapsed = std::chrono::steady_clock::now() - start;
    assert(!timed.success && timed.value.bits == host_error::timed_out);
    assert(elapsed >= std::chrono::milliseconds(30) && elapsed < std::chrono::milliseconds(750));
    assert(synthetic_notices == 0);

    queue_completion();
    auto pending = begin_read(HostRequest{0, 0, {int64_value(1), int32_value(16), int64_value(0)}});
    assert(std::holds_alternative<PendingOperation>(pending));
    auto& operation = std::get<PendingOperation>(pending);
    assert(!operation.attempt(false));
    assert(!operation.attempt(false));
    assert(synthetic_notices == 0);
    const auto interest = std::get<PosixWait>(operation.wait_source).current();
    pollfd blocked{static_cast<int>(interest.descriptor), POLLIN, 0};
    const auto parked = std::chrono::steady_clock::now();
    assert(::poll(&blocked, 1, 40) == 0);
    assert(std::chrono::steady_clock::now() - parked >= std::chrono::milliseconds(30));
    assert(!operation.attempt(false));
    const auto cancelled = operation.attempt(true);
    assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    ::close(reader);
    ::close(writer);
    ::close(listener);
    std::cout << "PASS: timed and overlapping reads drain more than 64 zero-copy notices and remain deadline-bound\n";
#else
    std::cout << "SKIP: requires Linux SO_ZEROCOPY and MSG_ZEROCOPY\n";
    return 77;
#endif
}

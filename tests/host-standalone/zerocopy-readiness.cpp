// Linux-only focused regression of a real pending net.read after a
// zero-copy completion. See the receipt for the small-link build command.
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

// Unrelated resources are never constructed by this focused harness.
namespace neura::nvm::provider {
void TlsDeleter::operator()(TlsStream*) const { std::abort(); }
void UpcallDeleter::operator()(Upcall*) const { std::abort(); }
std::optional<std::uint32_t> set_socket_options(NativeSocket, const HostValue&, const HostValue&) { std::abort(); }
}

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
    assert((notification.revents & POLLERR) != 0);
    using namespace neura::nvm;
    using namespace neura::nvm::provider;
    StandardServices::Tables table;
    table.sockets[1] = {writer};
    Capabilities capabilities;
    capabilities.net = true;
    FunctionService::Begin begin_read;
    const Adder add = [](HostSignature, FunctionService::Handler) {};
    const OverlapAdder overlap = [&](HostSignature signature, FunctionService::Handler, FunctionService::Begin begin) {
        if (signature.name == "neura.net.read") begin_read = std::move(begin);
    };
    add_net_services(add, overlap, table, capabilities);
    assert(begin_read);
    auto pending = begin_read(HostRequest{0, 0, {int64_value(1), int32_value(16), int64_value(0)}});
    assert(std::holds_alternative<PendingOperation>(pending));
    auto& operation = std::get<PendingOperation>(pending);
    const auto interest = std::get<PosixWait>(operation.wait_source).current();
    pollfd blocked{static_cast<int>(interest.descriptor), POLLIN, 0};
    const auto start = std::chrono::steady_clock::now();
    assert(::poll(&blocked, 1, 40) == 0);
    assert(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(30));
    assert(!operation.attempt(false));
    const auto cancelled = operation.attempt(true);
    assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    ::close(reader);
    ::close(writer);
    ::close(listener);
    std::cout << "PASS: zero-copy completion drained; blocked-read readiness sleeps\n";
#else
    std::cout << "SKIP: requires Linux SO_ZEROCOPY and MSG_ZEROCOPY\n";
    return 77;
#endif
}

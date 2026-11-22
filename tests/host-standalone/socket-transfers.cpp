// Focused loopback provider tests, independent of the compiler and full suite.
// See the worker receipt for the exact small-link build command.
#include "nvm/provider_internal.hpp"
#include "nvm/driver.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>

// These unrelated resources/services are never constructed by this harness.
namespace neura::nvm::provider {
void TlsDeleter::operator()(TlsStream*) const { std::abort(); }
void UpcallDeleter::operator()(Upcall*) const { std::abort(); }
}
namespace neura::nvm {
std::optional<IoBackend> available_io_backend(std::int32_t) { return std::nullopt; }
}

using namespace neura::nvm;
using namespace neura::nvm::provider;

struct TcpPair {
    int socket = -1, peer = -1;
    TcpPair() {
        const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(listener >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
        assert(::listen(listener, 1) == 0);
        socklen_t size = sizeof address;
        assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        socket = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(socket >= 0);
        assert(::connect(socket, reinterpret_cast<sockaddr*>(&address), size) == 0);
        peer = ::accept(listener, nullptr, nullptr);
        assert(peer >= 0);
        ::close(listener);
    }
    ~TcpPair() { ::close(socket); ::close(peer); }
};

struct Fixture {
    StandardServices::Tables table;
    Capabilities capabilities;
    std::map<std::string, FunctionService::Begin> begin;
    Fixture() {
        capabilities.net = true;
        const Adder add = [](HostSignature, FunctionService::Handler) {};
        const OverlapAdder overlap = [&](HostSignature signature, FunctionService::Handler, FunctionService::Begin fn) {
            begin[signature.name] = std::move(fn);
        };
        add_socket_services(add, overlap, table, capabilities);
    }
    auto start(std::string name, std::vector<HostValue> args) {
        const auto start = std::chrono::steady_clock::now();
        auto result = begin.at(name)(HostRequest{0, 0, std::move(args)});
        assert(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200));
        return result;
    }
};

std::string read_exact(int fd, std::size_t size) {
    std::string bytes(size, '\0');
    std::size_t total = 0;
    while (total < size) {
        pollfd ready{fd, POLLIN, 0};
        assert(::poll(&ready, 1, 1000) > 0);
        const auto got = ::recv(fd, bytes.data() + total, size - total, MSG_DONTWAIT);
        assert(got > 0);
        total += static_cast<std::size_t>(got);
    }
    return bytes;
}

std::size_t fill(int fd) {
    int small = 4096;
    assert(::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    const std::string bytes(64 * 1024, 'f');
    std::size_t total = 0;
    // TCP ACKs may open a little space after the first EAGAIN. Continue
    // until the peer receive window has closed and backpressure is stable.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto sent = ::send(fd, bytes.data(), bytes.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) total += static_cast<std::size_t>(sent);
        else { assert(errno == EAGAIN || errno == EWOULDBLOCK); ::usleep(1000); }
    }
    return total;
}

void quiet_source_and_backpressure() {
    Fixture f;
    TcpPair source, target;
    f.table.sockets[1] = {source.socket};
    f.table.sockets[2] = {target.socket};
    {
        auto pending = f.start("neura.net.splice", {int64_value(1), int64_value(2), int64_value(10)});
        assert(std::holds_alternative<PendingOperation>(pending));
        auto& op = std::get<PendingOperation>(pending);
        assert(std::get<PosixWait>(op.wait_source).current().events == PosixWait::Events::Read);
        const auto cancelled = op.attempt(true);
        assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    }
    const auto filler = fill(target.socket);
    const std::string message = "source bytes survive backpressure and cancellation";
    assert(::send(source.peer, message.data(), message.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(message.size()));
    {
        auto pending = f.start("neura.net.splice", {int64_value(1), int64_value(2), int64_value(message.size())});
        assert(std::holds_alternative<PendingOperation>(pending));
        auto& op = std::get<PendingOperation>(pending);
        assert(std::get<PosixWait>(op.wait_source).current().events == PosixWait::Events::Write);
        char peek[128]{};
        assert(::recv(source.socket, peek, sizeof peek, MSG_PEEK | MSG_DONTWAIT) == static_cast<ssize_t>(message.size()));
        assert(std::string(peek, message.size()) == message);
        const auto cancelled = op.attempt(true);
        assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    }
    assert(read_exact(target.peer, filler) == std::string(filler, 'f'));
    {
        auto relayed = f.start("neura.net.splice", {int64_value(1), int64_value(2), int64_value(message.size())});
        if (auto* op = std::get_if<PendingOperation>(&relayed)) {
            pollfd ready{target.socket, POLLOUT, 0};
            assert(::poll(&ready, 1, 1000) > 0);
            const auto done = op->attempt(false);
            assert(done && done->success && done->value.bits == message.size());
        } else {
            const auto& done = std::get<HostCompletion>(relayed);
            assert(done.success && done.value.bits == message.size());
        }
        assert(read_exact(target.peer, message.size()) == message);
        char byte;
        assert(::recv(source.socket, &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);
    }
    // Close the public ID before cancellation. The held descriptor remains
    // valid, while the operation rejects the closed ID rather than reuse it.
    auto pending = f.start("neura.net.splice", {int64_value(1), int64_value(2), int64_value(1)});
    const auto held = std::get<PosixWait>(std::get<PendingOperation>(pending).wait_source).current().descriptor;
    f.table.sockets.erase(1);
    ::close(source.socket);
    source.socket = -1;
    assert(::fcntl(static_cast<int>(held), F_GETFD) >= 0);
    const auto closed = std::get<PendingOperation>(pending).attempt(true);
    assert(closed && !closed->success && closed->value.bits == host_error::invalid_input);
    std::cout << "PASS: quiet-source relay, backpressure, cancellation, exact source/count accounting, closed ID\n";
}

void zero_copy_backpressure() {
    Fixture f;
    TcpPair stream;
    f.table.sockets[1] = {stream.socket};
    const auto filler = fill(stream.socket);
    {
        auto pending = f.start("neura.net.send_zerocopy", {int64_value(1), bytes_value("must remain unsent")});
        assert(std::holds_alternative<PendingOperation>(pending));
        const auto cancelled = std::get<PendingOperation>(pending).attempt(true);
        assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    }
    assert(read_exact(stream.peer, filler) == std::string(filler, 'f'));
    // A partial send reports precisely the prefix delivered to the peer.
    const std::string payload(2 * 1024 * 1024, 'z');
    auto result = f.start("neura.net.send_zerocopy", {int64_value(1), bytes_value(payload)});
    std::optional<HostCompletion> answer;
    if (auto* done = std::get_if<HostCompletion>(&result)) answer = *done;
    else {
        pollfd ready{stream.socket, POLLOUT, 0};
        assert(::poll(&ready, 1, 1000) > 0);
        answer = std::get<PendingOperation>(result).attempt(false);
    }
    assert(answer && answer->success && answer->value.bits > 0 && answer->value.bits < payload.size());
    assert(read_exact(stream.peer, answer->value.bits) == payload.substr(0, answer->value.bits));
    pollfd notice{stream.socket, POLLIN, 0};
    assert(::poll(&notice, 1, 1000) > 0 && (notice.revents & POLLERR) != 0);
    // Empty send enters the same transfer setup without adding a notice.
    const auto empty = f.start("neura.net.send_zerocopy", {int64_value(1), bytes_value("")});
    assert(std::get<HostCompletion>(empty).success);
    notice.revents = 0;
    assert(::poll(&notice, 1, 20) == 0);
    std::cout << "PASS: non-reading zero-copy peer, cancellation, exact partial-send count\n";
}

void segmented_counts() {
    Fixture f;
    const int receiver = ::socket(AF_INET, SOCK_DGRAM, 0);
    const int sender = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(receiver >= 0 && sender >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    socklen_t size = sizeof address;
    assert(::getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    f.table.sockets[1] = {sender, false, true};
    auto result = f.start("neura.net.udp.send_segmented", {int64_value(1), bytes_value("127.0.0.1"),
                          int32_value(ntohs(address.sin_port)), bytes_value("abcde"), int32_value(2)});
    const auto& done = std::get<HostCompletion>(result);
    assert(done.success && done.value.bits == 5);
    assert(read_exact(receiver, 2) == "ab");
    assert(read_exact(receiver, 2) == "cd");
    assert(read_exact(receiver, 1) == "e");
    ::close(receiver);
    ::close(sender);
    std::cout << "PASS: segmented UDP lengths and payload/count accounting\n";
}

int main() {
    quiet_source_and_backpressure();
    zero_copy_backpressure();
    segmented_counts();
}

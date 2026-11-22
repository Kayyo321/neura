// Focused loopback provider tests, independent of the compiler and full suite.
// Registered with CTest as host-socket-transfers.
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
#include <cstdio>
#include <iostream>
#include <thread>

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
    FunctionService::Begin begin_transfer;
    FunctionService::Handler invoke_transfer;
    FunctionService::Handler invoke_file;
    Fixture() {
        capabilities.net = true;
        capabilities.fs = true;
        const Adder add = [](HostSignature, FunctionService::Handler) {};
        const OverlapAdder overlap = [&](HostSignature signature, FunctionService::Handler invoke,
                                         FunctionService::Begin fn) {
            if (signature.name == "neura.net.send_zerocopy" && signature.version == 3) {
                begin_transfer = std::move(fn);
                invoke_transfer = std::move(invoke);
            } else {
                if (signature.name == "neura.net.send_file") invoke_file = invoke;
                begin[signature.name] = std::move(fn);
            }
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

void zero_copy_deadline_after_a_prefix() {
    Fixture f;
    TcpPair stream;
    f.table.sockets[1] = {stream.socket};
    const auto seed = f.begin_transfer(HostRequest{0, 0, {int64_value(1), bytes_value("prefix"), int64_value(0)}});
    const auto& prefix = std::get<HostCompletion>(seed);
    assert(prefix.success && prefix.value.elements[0].bits == 6);
    assert(read_exact(stream.peer, 6) == "prefix");

    const auto filler = fill(stream.socket);
    const auto before = std::chrono::steady_clock::now();
    const auto expiry = std::chrono::duration_cast<std::chrono::nanoseconds>(
        (before + std::chrono::milliseconds(100)).time_since_epoch()).count();
    const auto expired = f.invoke_transfer(HostRequest{1, 0, {
        int64_value(1), bytes_value("unsent"), int64_value(expiry),
    }});
    const auto waited = std::chrono::steady_clock::now() - before;
    assert(!expired.success && expired.value.bits == host_error::timed_out);
    assert(waited >= std::chrono::milliseconds(90) && waited < std::chrono::seconds(2));
    assert(read_exact(stream.peer, filler) == std::string(filler, 'f'));
    const auto past = f.begin_transfer(HostRequest{2, 0, {
        int64_value(1), bytes_value("must still be unsent"), int64_value(expiry),
    }});
    const auto& refused = std::get<HostCompletion>(past);
    assert(!refused.success && refused.value.bits == host_error::timed_out);
    char byte;
    assert(::recv(stream.peer, &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);

    const auto continued = f.invoke_transfer(HostRequest{3, 0, {
        int64_value(1), bytes_value("suffix"), prefix.value.elements[1],
    }});
    assert(continued.success && continued.value.elements[0].bits == 6);
    assert(continued.value.elements[1].bits == prefix.value.elements[1].bits);
    assert(read_exact(stream.peer, 6) == "suffix");
    std::cout << "PASS: zero-copy continuation token and absolute synchronous backpressure timeout\n";
}

void raw_read_leaves_the_scheduler_available() {
    Fixture f;
    f.capabilities.raw = true;
    const int receiver = ::socket(AF_INET, SOCK_DGRAM, 0);
    const int sender = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(receiver >= 0 && sender >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(receiver, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    socklen_t size = sizeof address;
    assert(::getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    // Inject a loopback datagram descriptor under a raw ID so this readiness
    // test can run without process privileges.
    f.table.sockets[1] = {receiver, false, true, false, true};
    {
        auto pending = f.start("neura.net.raw.receive_from", {int64_value(1), int32_value(4), int64_value(0)});
        auto& op = std::get<PendingOperation>(pending);
        assert(op.deadline == PendingOperation::no_deadline && !op.attempt(false));
        const auto cancelled = op.attempt(true);
        assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    }
    auto pending = f.start("neura.net.raw.receive_from", {int64_value(1), int32_value(4), int64_value(1000000000)});
    auto& op = std::get<PendingOperation>(pending);
    assert(op.deadline == 1000000000 && op.expire().value.bits == host_error::timed_out);
    assert(::sendto(sender, "probe", 5, 0, reinterpret_cast<sockaddr*>(&address), size) == 5);
    pollfd ready{receiver, POLLIN, 0};
    assert(::poll(&ready, 1, 1000) > 0);
    const auto done = op.attempt(false);
    assert(done && done->success && done->value.elements[0].bytes == "prob");
    assert(done->value.elements[1].bytes == "127.0.0.1" && done->value.elements[2].bits == 0);
    ::close(receiver);
    ::close(sender);
    std::cout << "PASS: raw receive readiness, cancellation, deadline and truncation\n";
}

// A Unix-domain stream with a small send buffer gives deterministic
// backpressure for the bounded file copy.
void file_transfer_is_bounded() {
    Fixture f;
    FILE* file = ::tmpfile();
    assert(file != nullptr);
    const int fd = ::fileno(file);
    std::string content(3 * 1024 * 1024 + 5, '\0');
    for (std::size_t index = 0; index < content.size(); ++index) content[index] = static_cast<char>('a' + index % 26);
    assert(::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size()));
    assert(::lseek(fd, 17, SEEK_SET) == 17);
    f.table.files[1] = {fd, true, false};
    int stream[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
    f.table.sockets[1] = {stream[0]};
    const auto args = [&](std::int64_t offset, std::int64_t count) {
        return std::vector{int64_value(1), int64_value(1), int64_value(offset), int64_value(count)};
    };

    // Backpressure before any progress parks on writability under the
    // 30-second transfer deadline and expires with TimedOut.
    const auto filler = fill(stream[0]);
    {
        auto started = f.start("neura.net.send_file", args(123, 1000));
        auto& operation = std::get<PendingOperation>(started);
        assert(operation.deadline == 30'000'000'000);
        const auto interest = std::get<PosixWait>(operation.wait_source).current();
        assert(interest.descriptor >= 0 && interest.events == PosixWait::Events::Write);
        assert(!operation.attempt(false));
        const auto expired = operation.expire();
        assert(!expired.success && expired.value.bits == host_error::timed_out);
        const auto cancelled = operation.attempt(true);
        assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    }
    assert(read_exact(stream[1], filler) == std::string(filler, 'f'));
    char byte;
    assert(::recv(stream[1], &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);

    // The first attempt sends what the socket accepts and parks. Expiry or
    // cancellation then reports exactly that on-wire prefix, never the bytes
    // read from the file but still buffered.
    for (const bool cancel : {false, true}) {
        auto started = f.start("neura.net.send_file", args(123, 1024 * 1024));
        auto& operation = std::get<PendingOperation>(started);
        pollfd readable{stream[1], POLLIN, 0};
        assert(::poll(&readable, 1, 0) == 1);
        assert(!operation.attempt(false));
        const auto done = cancel ? operation.attempt(true) : std::optional(operation.expire());
        assert(done && done->success && done->value.bits > 0 && done->value.bits < 1024 * 1024);
        const auto accepted = static_cast<std::size_t>(done->value.bits);
        assert(read_exact(stream[1], accepted) == content.substr(123, accepted));
        assert(::poll(&readable, 1, 50) == 0);
    }
    assert(::lseek(fd, 0, SEEK_CUR) == 17);

    // Synchronous callers wait on readiness: a slow reader receives every
    // byte across many backpressured attempts and the 1 MiB attempt budget.
    {
        std::string received;
        std::thread reader([&] { received = read_exact(stream[1], content.size() - 9); });
        const auto sent = f.invoke_file(HostRequest{0, 0, args(9, static_cast<std::int64_t>(content.size()))});
        reader.join();
        assert(sent.success && sent.value.bits == content.size() - 9);
        assert(received == content.substr(9));
    }

    // A count past the end of the file stops there; zero sends nothing.
    auto sent = f.invoke_file(HostRequest{0, 0, args(static_cast<std::int64_t>(content.size()) - 32, 256)});
    assert(sent.success && sent.value.bits == 32);
    assert(read_exact(stream[1], 32) == content.substr(content.size() - 32));
    sent = f.invoke_file(HostRequest{0, 0, args(static_cast<std::int64_t>(content.size()) + 10, 256)});
    assert(sent.success && sent.value.bits == 0);
    sent = f.invoke_file(HostRequest{0, 0, args(0, 0)});
    assert(sent.success && sent.value.bits == 0);
    assert(::lseek(fd, 0, SEEK_CUR) == 17);

    // Closing the file ID while the transfer is parked ends it with the
    // prefix already accepted; without progress it is InvalidInput.
    const auto refill = fill(stream[0]);
    {
        auto started = f.start("neura.net.send_file", args(0, 1000));
        auto& operation = std::get<PendingOperation>(started);
        auto closed = f.table.files.extract(1);
        const auto done = operation.attempt(false);
        assert(done && !done->success && done->value.bits == host_error::invalid_input);
        f.table.files.insert(std::move(closed));
    }
    assert(read_exact(stream[1], refill) == std::string(refill, 'f'));

    // Rights and arguments.
    f.capabilities.fs = false;
    sent = f.invoke_file(HostRequest{0, 0, args(0, 1)});
    assert(!sent.success);
    f.capabilities.fs = true;
    sent = f.invoke_file(HostRequest{0, 0, args(-1, 1)});
    assert(!sent.success && sent.value.bits == host_error::invalid_input);
    f.table.files[1].readable = false;
    sent = f.invoke_file(HostRequest{0, 0, args(0, 1)});
    assert(!sent.success && sent.value.bits == host_error::invalid_input);

    ::close(stream[0]);
    ::close(stream[1]);
    ::fclose(file);
    std::cout << "PASS: bounded file transfer: deadline, cancellation and expiry prefixes, readiness, EOF, position\n";
}

int main() {
    quiet_source_and_backpressure();
    zero_copy_backpressure();
    zero_copy_deadline_after_a_prefix();
    segmented_counts();
    raw_read_leaves_the_scheduler_available();
    file_transfer_is_bounded();
}

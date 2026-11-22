// Focused loopback provider tests, independent of the compiler and full suite.
// Registered with CTest as host-socket-transfers.
#include "nvm/provider_internal.hpp"
#include "nvm/driver.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/sendfile.h>
#endif
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
    FunctionService::Handler invoke_file_prefix;
    Fixture() {
        capabilities.net = true;
        capabilities.fs = true;
        const Adder add = [](HostSignature, FunctionService::Handler) {};
        const OverlapAdder overlap = [&](HostSignature signature, FunctionService::Handler invoke,
                                         FunctionService::Begin fn) {
            if (signature.name == "neura.net.send_zerocopy" && signature.version == 3) {
                begin_transfer = std::move(fn);
                invoke_transfer = std::move(invoke);
            } else if (signature.name == "neura.net.send_file" && signature.version == 3) {
                // Version 3 sends a prefix; versions 1 and 2 the whole range.
                invoke_file_prefix = std::move(invoke);
                begin["neura.net.send_file#3"] = std::move(fn);
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
// backpressure. Versions 1 and 2 keep the 0.3.0 shape: the whole range, under
// one 30-second deadline for the call. On Linux the bytes move by sendfile(2).
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

// The scheduler path: a transfer that yields after its 1 MiB attempt budget
// while the socket is still writable must be attempted again at once by
// every readiness backend, and a second request completes meanwhile.
void file_transfer_resumes_under_the_scheduler() {
    FILE* file = ::tmpfile();
    assert(file != nullptr);
    const int fd = ::fileno(file);
    std::string content(5 * 1024 * 1024 + 3, '\0');
    for (std::size_t index = 0; index < content.size(); ++index) content[index] = static_cast<char>('A' + index % 23);
    assert(::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size()));
    for (const auto backend : {IoBackend::Poll, IoBackend::Epoll}) {
        Fixture f;
        f.table.files[1] = {fd, true, false};
        int stream[2] = {-1, -1};
        assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
        // A large buffer and a fast reader keep the socket writable, so
        // only the attempt budget makes the transfer yield.
        int large = 8 * 1024 * 1024;
        ::setsockopt(stream[0], SOL_SOCKET, SO_SNDBUF, &large, sizeof large);
        f.table.sockets[1] = {stream[0]};
        std::string received;
        std::thread reader([&] { received = read_exact(stream[1], content.size()); });
        SteadyTaskClock clock;
        PendingRequests pending(clock, backend);
        HostRequest request{1, 0, {int64_value(1), int64_value(1), int64_value(0),
                                   int64_value(static_cast<std::int64_t>(content.size()))}};
        request.now = clock.now();
        pending.add(1, f.begin.at("neura.net.send_file")(request), true);
        pending.add(2, HostCompletion::ok(int64_value(42)), false);
        std::optional<HostCompletion> transferred;
        bool other = false;
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!transferred && std::chrono::steady_clock::now() < limit) {
            for (auto& done : pending.wait(clock.now() + 100'000'000)) {
                if (done.sequence == 2) other = true;
                else transferred = done.completion;
            }
        }
        reader.join();
        assert(other && transferred && transferred->success && transferred->value.bits == content.size());
        assert(received == content);
        ::close(stream[0]);
        ::close(stream[1]);
    }
    ::fclose(file);
    std::cout << "PASS: file transfer resumes after its attempt budget under poll and epoll\n";
}

class ManualClock final : public TaskClock {
public:
    std::int64_t now() override { return time; }
    void wait_until(std::int64_t deadline) override { time = deadline; }
    bool virtual_time() const override { return true; }
    std::int64_t time = 0;
};

// Version 3, as TcpSocket.send_file drives it: each call returns the prefix
// it sent once the socket would block, and waits only while nothing is
// accepted, so its 30-second deadline is a no-progress deadline. A slow
// reader that keeps draining completes a transfer lasting far longer than
// the window; a stalled one times out, with the count known to the caller.
void file_prefix_transfer_has_an_idle_deadline() {
    Fixture f;
    FILE* file = ::tmpfile();
    assert(file != nullptr);
    const int fd = ::fileno(file);
    std::string content(2 * 1024 * 1024 + 7, '\0');
    for (std::size_t index = 0; index < content.size(); ++index) content[index] = static_cast<char>('a' + index % 19);
    assert(::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size()));
    assert(::lseek(fd, 5, SEEK_SET) == 5);
    f.table.files[1] = {fd, true, false};
    int stream[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
    int small = 4096;
    assert(::setsockopt(stream[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small) == 0);
    f.table.sockets[1] = {stream[0]};
    const auto& begin = f.begin.at("neura.net.send_file#3");
    const auto request = [&](std::int64_t offset, std::int64_t count, std::int64_t now) {
        HostRequest result{0, 0, {int64_value(1), int64_value(1), int64_value(offset), int64_value(count)}};
        result.now = now;
        return result;
    };

    // With room, a call completes at once with the prefix the socket took.
    auto first = begin(request(100, 1024 * 1024, 0));
    const auto& prefix = std::get<HostCompletion>(first);
    assert(prefix.success && prefix.value.bits > 0 && prefix.value.bits < 1024 * 1024);
    std::string received = read_exact(stream[1], prefix.value.bits);
    assert(received == content.substr(100, received.size()));

    // A slow reader: every 20 virtual seconds it drains what is queued, so
    // each call makes progress inside its window although the transfer
    // lasts for many windows.
    ManualClock clock;
    PendingRequests pending(clock, IoBackend::Poll);
    const auto total_wanted = static_cast<std::int64_t>(content.size()) - 100;
    std::int64_t total = static_cast<std::int64_t>(received.size());
    std::uint64_t sequence = 1;
    int calls = 0;
    while (total < total_wanted) {
        auto started = begin(request(100 + total, total_wanted - total, clock.now()));
        ++calls;
        if (auto* done = std::get_if<HostCompletion>(&started)) {
            // The reader has not run: the next call finds the socket full.
            assert(done->success && done->value.bits > 0);
            total += static_cast<std::int64_t>(done->value.bits);
            continue;
        }
        assert(std::get<PendingOperation>(started).deadline == clock.now() + 30'000'000'000);
        pending.add(sequence, std::move(started), true);
        clock.time += 20'000'000'000;
        // Nothing was read yet: the call is still waiting, not expired.
        assert(pending.poll().empty());
        // The reader drains; the call then completes with its prefix.
        std::optional<HostCompletion> answer;
        for (int round = 0; round < 1000 && !answer; ++round) {
            char chunk[4096];
            const auto got = ::recv(stream[1], chunk, sizeof chunk, MSG_DONTWAIT);
            if (got > 0) received.append(chunk, static_cast<std::size_t>(got));
            for (auto& done : pending.poll()) answer = done.completion;
        }
        assert(answer && answer->success && answer->value.bits > 0);
        total += static_cast<std::int64_t>(answer->value.bits);
        ++sequence;
    }
    assert(total == total_wanted);
    while (static_cast<std::int64_t>(received.size()) < total) {
        received += read_exact(stream[1], static_cast<std::size_t>(total) - received.size());
    }
    assert(received == content.substr(100));
    assert(calls > 2 && clock.time > 60'000'000'000);
    // At the end of the file a call returns 0.
    auto end = begin(request(static_cast<std::int64_t>(content.size()), 10, clock.now()));
    assert(std::get<HostCompletion>(end).success && std::get<HostCompletion>(end).value.bits == 0);

    // A stalled reader: after a prefix, a call that sends nothing for the
    // whole window ends with TimedOut, and the caller still holds the count.
    const auto filler = fill(stream[0]);
    auto stalled = begin(request(0, 1000, clock.now()));
    auto& operation = std::get<PendingOperation>(stalled);
    assert(!operation.attempt(false));
    pending.add(sequence, std::move(stalled), true);
    clock.time += 29'999'999'999;
    assert(pending.poll().empty());
    clock.time += 1;
    const auto expired = pending.poll();
    assert(expired.size() == 1 && !expired.front().completion.success &&
           expired.front().completion.value.bits == host_error::timed_out);
    // Cancellation of a waiting call carries no count either: none was sent.
    auto waiting = begin(request(0, 1000, clock.now()));
    const auto cancelled = std::get<PendingOperation>(waiting).attempt(true);
    assert(cancelled && !cancelled->success && cancelled->value.bits == host_error::cancelled);
    assert(read_exact(stream[1], filler) == std::string(filler, 'f'));
    char byte;
    assert(::recv(stream[1], &byte, 1, MSG_DONTWAIT) < 0 && errno == EAGAIN);
    assert(::lseek(fd, 0, SEEK_CUR) == 5);
    // The synchronous form returns a prefix too.
    const auto sync = f.invoke_file_prefix(HostRequest{0, 0, {int64_value(1), int64_value(1), int64_value(0),
                                                              int64_value(static_cast<std::int64_t>(content.size()))}});
    assert(sync.success && sync.value.bits > 0 && sync.value.bits < content.size());
    assert(read_exact(stream[1], sync.value.bits) == content.substr(0, sync.value.bits));

    ::close(stream[0]);
    ::close(stream[1]);
    ::fclose(file);
    std::cout << "PASS: prefix file transfer: progress resets the 30 s window, stall times out, EOF, cancel\n";
}

// Linux refuses sendfile(2) from some files (EINVAL); /proc/self/cmdline is
// one that pread still reads. The transfer then copies through its buffer.
void file_transfer_falls_back_to_a_copy() {
    Fixture f;
    const int fd = ::open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    std::string expected(4096, '\0');
    const auto length = ::pread(fd, expected.data(), expected.size(), 0);
    assert(length > 1);
    expected.resize(static_cast<std::size_t>(length));
    int stream[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
    off_t position = 0;
    assert(::sendfile(stream[0], fd, &position, 16) < 0 && errno == EINVAL);
    f.table.files[1] = {fd, true, false};
    f.table.sockets[1] = {stream[0]};
    auto sent = f.invoke_file(HostRequest{0, 0, {int64_value(1), int64_value(1), int64_value(0), int64_value(4096)}});
    assert(sent.success && sent.value.bits == expected.size());
    assert(read_exact(stream[1], expected.size()) == expected);
    sent = f.invoke_file_prefix(HostRequest{0, 0, {int64_value(1), int64_value(1), int64_value(1), int64_value(4096)}});
    assert(sent.success && sent.value.bits == expected.size() - 1);
    assert(read_exact(stream[1], expected.size() - 1) == expected.substr(1));
    ::close(stream[0]);
    ::close(stream[1]);
    ::close(fd);
    std::cout << "PASS: file transfer copies through its buffer when sendfile refuses the file\n";
}

// sendfile(2) has no MSG_NOSIGNAL: a transfer to a peer that has gone gets
// ConnectionReset, and the process survives SIGPIPE's default action. Any
// SIGPIPE the transfer raised is consumed, and one already pending is kept.
void file_transfer_to_a_gone_peer_survives_sigpipe() {
    const auto previous = ::signal(SIGPIPE, SIG_DFL);
    Fixture f;
    FILE* file = ::tmpfile();
    assert(file != nullptr);
    const int fd = ::fileno(file);
    const std::string content(4096, 'p');
    assert(::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size()));
    f.table.files[1] = {fd, true, false};
    const auto transfer = [&](bool prefix) {
        const auto& invoke = prefix ? f.invoke_file_prefix : f.invoke_file;
        return invoke(HostRequest{0, 0, {int64_value(1), int64_value(1), int64_value(0), int64_value(4096)}});
    };
    const auto pending_sigpipe = [] {
        sigset_t pending;
        assert(::sigpending(&pending) == 0);
        return sigismember(&pending, SIGPIPE) == 1;
    };
    // A local stream whose peer is closed fails every write with EPIPE.
    {
        int stream[2] = {-1, -1};
        assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
        ::close(stream[1]);
        f.table.sockets[1] = {stream[0]};
        for (const bool prefix : {false, true}) {
            const auto sent = transfer(prefix);
            assert(!sent.success && sent.value.bits == host_error::connection_reset);
        }
        assert(!pending_sigpipe());
        f.table.sockets.erase(1);
        ::close(stream[0]);
    }
    // A TCP peer that resets the connection: the first write may report the
    // reset itself, every later one EPIPE.
    {
        TcpPair pair;
        linger abort{1, 0};
        assert(::setsockopt(pair.peer, SOL_SOCKET, SO_LINGER, &abort, sizeof abort) == 0);
        ::close(pair.peer);
        pair.peer = ::socket(AF_INET, SOCK_STREAM, 0); // Closed by ~TcpPair.
        pollfd reset{pair.socket, POLLIN, 0};
        assert(::poll(&reset, 1, 1000) == 1);
        f.table.sockets[1] = {pair.socket};
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto sent = transfer(attempt % 2 == 1);
            assert(!sent.success && sent.value.bits == host_error::connection_reset);
        }
        assert(!pending_sigpipe());
        f.table.sockets.erase(1);
    }
    // A SIGPIPE pending before the transfer is still pending after it.
    {
        sigset_t blocked, saved;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        assert(::pthread_sigmask(SIG_BLOCK, &blocked, &saved) == 0);
        assert(::raise(SIGPIPE) == 0 && pending_sigpipe());
        int stream[2] = {-1, -1};
        assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
        ::close(stream[1]);
        f.table.sockets[1] = {stream[0]};
        const auto sent = transfer(false);
        assert(!sent.success && sent.value.bits == host_error::connection_reset);
        assert(pending_sigpipe());
        timespec zero{};
        assert(::sigtimedwait(&blocked, nullptr, &zero) == SIGPIPE);
        assert(::pthread_sigmask(SIG_SETMASK, &saved, nullptr) == 0);
        f.table.sockets.erase(1);
        ::close(stream[0]);
    }
    ::fclose(file);
    ::signal(SIGPIPE, previous);
    std::cout << "PASS: a file transfer to a gone peer reports ConnectionReset and survives SIGPIPE\n";
}

int main() {
    quiet_source_and_backpressure();
    zero_copy_backpressure();
    zero_copy_deadline_after_a_prefix();
    segmented_counts();
    raw_read_leaves_the_scheduler_available();
    file_transfer_is_bounded();
    file_transfer_resumes_under_the_scheduler();
    file_prefix_transfer_has_an_idle_deadline();
#ifdef __linux__
    file_transfer_falls_back_to_a_copy();
#endif
    file_transfer_to_a_gone_peer_survives_sigpipe();
}

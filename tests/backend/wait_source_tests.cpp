#include "nvm/driver.hpp"
#include "nvm/providers.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/io_uring.h>
#include <sys/syscall.h>
#endif
#endif

namespace {

using namespace neura::nvm;
using namespace std::chrono_literals;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

class Clock final : public TaskClock {
public:
    std::int64_t now() override { return time; }
    void wait_until(std::int64_t deadline) override { time = std::max(time, deadline); }
    bool virtual_time() const override { return true; }

    std::int64_t time = 0;
};

class LiveClock final : public TaskClock {
public:
    std::int64_t now() override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_).count();
    }
    void wait_until(std::int64_t deadline) override { std::this_thread::sleep_until(start_ + std::chrono::nanoseconds(deadline)); }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

HostCompletion value(std::uint64_t bits) { return HostCompletion::ok(HostValue{bits, {}, {}}); }

void completed(const std::vector<PendingRequests::Done>& done, std::uint64_t sequence, std::uint64_t bits,
               bool cancelled = false) {
    check(done.size() == 1 && done[0].sequence == sequence && done[0].completion.success &&
          done[0].completion.value.bits == bits && done[0].cancelled == cancelled, "wrong completion");
}

PendingOperation with_source(WaitSource wait_source) {
    PendingOperation operation;
    operation.wait_source = std::move(wait_source);
    return operation;
}

void polled_and_external() {
    Clock clock;
    PendingRequests pending(clock);
    int attempts = 0;
    PendingOperation polled;
    polled.attempt = [&](bool) -> std::optional<HostCompletion> {
        if (++attempts == 2) return value(7);
        return std::nullopt;
    };
    pending.add(1, std::move(polled), false);
    check(pending.poll().empty() && attempts == 1, "default wait missed a check");
    completed(pending.poll(), 1, 7);

    const auto signal = std::make_shared<ExternalSignal>();
    auto external = with_source(ExternalWait{signal});
    attempts = 0;
    external.attempt = [&](bool) -> std::optional<HostCompletion> {
        ++attempts;
        if (attempts == 2) {
            // Readiness received during an unsuccessful attempt survives for the next check.
            signal->signal();
        }
        if (attempts == 3) return value(8);
        return std::nullopt;
    };
    pending.add(2, std::move(external), false);
    check(pending.poll().empty() && attempts == 0, "unsignaled external wait ran");
    signal->signal();
    signal->signal();
    check(pending.poll().empty() && attempts == 1, "external signal missed its check");
    check(pending.poll().empty() && attempts == 1, "unsuccessful attempt did not consume coalesced signals");
    signal->signal();
    check(pending.poll().empty() && attempts == 2, "external operation did not rearm");
    completed(pending.poll(), 2, 8);
    check(pending.poll().empty() && pending.empty(), "external operation delivered twice");

    std::vector<std::uint64_t> seen;
    const auto second = std::make_shared<ExternalSignal>();
    auto first_wait = with_source(ExternalWait{std::make_shared<ExternalSignal>()});
    const auto first = std::get<ExternalWait>(first_wait.wait_source).signal;
    first_wait.attempt = [&](bool) {
        seen.push_back(10);
        second->signal();
        return value(10);
    };
    auto second_wait = with_source(ExternalWait{second});
    second_wait.attempt = [&](bool) {
        seen.push_back(20);
        return value(20);
    };
    pending.add(20, std::move(second_wait), false);
    pending.add(10, std::move(first_wait), false);
    first->signal();
    completed(pending.poll(), 10, 10);
    completed(pending.poll(), 20, 20);
    check(seen == std::vector<std::uint64_t>{10, 20}, "external wake during delivery affected the same snapshot");
}

void external_cancel_and_deadline() {
    Clock clock;
    PendingRequests pending(clock);
    for (const bool ready_before_cancel : {true, false}) {
        const auto signal = std::make_shared<ExternalSignal>();
        auto operation = with_source(ExternalWait{signal});
        int attempts = 0;
        operation.attempt = [&](bool cancelled) -> std::optional<HostCompletion> {
            ++attempts;
            check(cancelled != ready_before_cancel, "wrong attempt won the external cancellation race");
            if (cancelled) return std::nullopt;
            return value(2);
        };
        pending.add(9, std::move(operation), true);
        if (ready_before_cancel) signal->signal();
        pending.cancel(9);
        signal->signal();
        pending.cancel(9);
        auto done = pending.poll();
        if (ready_before_cancel) {
            completed(done, 9, 2, true);
        } else {
            check(done.size() == 1 && done[0].cancelled && !done[0].completion.success &&
                  done[0].completion.value.bits == host_error::cancelled, "external cancellation did not settle");
        }
        check(attempts == 1 && pending.poll().empty(), "late signal or duplicate cancel retried an answered operation");
    }

    auto operation = with_source(ExternalWait{std::make_shared<ExternalSignal>()});
    operation.deadline = 25;
    operation.attempt = [&](bool cancelled) -> std::optional<HostCompletion> {
        check(!cancelled && clock.time == 25, "unsignaled external operation ran before its deadline");
        return std::nullopt;
    };
    operation.expire = [] { return value(25); };
    pending.add(4, std::move(operation), false);
    check(pending.poll().empty() && clock.time == 0, "embedding poll waited for its deadline");
    clock.time = 25;
    completed(pending.poll(), 4, 25);
}

// PendingOperation::wake_at: a provider timer makes the driver attempt
// again without readiness, never expires the operation, and never moves a
// virtual clock.
void provider_wake_ups() {
    Clock clock;
    clock.time = 1'000;
    PendingRequests pending(clock);
    const auto signal = std::make_shared<ExternalSignal>();
    auto timed = with_source(ExternalWait{signal});
    int attempts = 0;
    std::int64_t wake = PendingOperation::no_deadline;
    timed.wake_at = [&](std::int64_t) { return wake; };
    timed.attempt = [&](bool) -> std::optional<HostCompletion> {
        if (++attempts == 2) return value(30);
        return std::nullopt;
    };
    pending.add(30, std::move(timed), false);
    check(pending.poll().empty() && attempts == 0, "an unset wake-up attempted the operation");
    wake = 2'000;
    check(pending.poll().empty() && attempts == 0, "a future wake-up attempted the operation");
    wake = 1'000;
    check(pending.poll().empty() && attempts == 1, "a due wake-up did not attempt the operation");
    completed(pending.wait(std::nullopt), 30, 30);
    check(clock.time == 1'000, "a wake-up moved the virtual clock");

    // A wake-up that keeps moving never expires the operation; the deadline does.
    auto bounded = with_source(ExternalWait{std::make_shared<ExternalSignal>()});
    int woken = 0;
    bool expired = false;
    bounded.deadline = 5'000;
    bounded.wake_at = [](std::int64_t now) { return now; };
    bounded.attempt = [&](bool) -> std::optional<HostCompletion> {
        ++woken;
        return std::nullopt;
    };
    bounded.expire = [&] {
        expired = true;
        return value(31);
    };
    pending.add(31, std::move(bounded), false);
    check(pending.poll().empty() && woken == 1 && !expired, "a wake-up expired the operation");
    completed(pending.wait(std::nullopt), 31, 31);
    check(expired && clock.time == 5'000, "the deadline did not expire a woken operation");
}

// BackgroundWork: run on every full check, never on a cancellation's own
// check, and its wake-up bounds the native wait without delivering anything.
void background_work() {
    struct Counting final : BackgroundWork {
        int runs = 0;
        std::int64_t wake = PendingOperation::no_deadline;
        std::vector<PosixWait::Interest> interests() override { return {}; }
        std::int64_t wake_at(std::int64_t) override { return wake; }
        void run() override { ++runs; }
    } work;
    Clock clock;
    PendingRequests pending(clock);
    pending.add_background(&work);
    pending.add_background(&work);
    check(pending.poll().empty() && work.runs == 1, "background work did not run once per check");
    auto cancellable = with_source(ExternalWait{std::make_shared<ExternalSignal>()});
    cancellable.attempt = [](bool cancelled) -> std::optional<HostCompletion> {
        if (cancelled) return value(50);
        return std::nullopt;
    };
    pending.add(50, std::move(cancellable), true);
    pending.cancel(50);
    check(work.runs == 1, "a cancellation's own check ran background work");
    completed(pending.poll(), 50, 50, true);
    check(work.runs == 2, "background work missed a check");
}

#ifdef _WIN32

class ManualEvent {
public:
    ManualEvent() : handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) { check(handle != nullptr, "CreateEvent failed"); }
    ~ManualEvent() { ::CloseHandle(handle); }
    ManualEvent(const ManualEvent&) = delete;
    ManualEvent& operator=(const ManualEvent&) = delete;

    HANDLE handle;
};

void native_events() {
    LiveClock clock;
    PendingRequests pending(clock);
    std::array<ManualEvent, MAXIMUM_WAIT_OBJECTS + 2> events;
    const auto add = [&](std::size_t index, std::uint64_t sequence) {
        auto operation = with_source(WindowsWait{events[index].handle});
        operation.attempt = [sequence](bool) { return value(sequence); };
        pending.add(sequence, std::move(operation), false);
    };
    for (std::size_t index = 0; index < events.size(); ++index) add(index, index);
    add(0, events.size());
    check(pending.poll().empty(), "unsignaled Win32 wait ran");
    check(::SetEvent(events.back().handle) != 0 && ::SetEvent(events.front().handle) != 0, "SetEvent failed");
    const auto done = pending.poll();
    check(done.size() == 3 && done[0].sequence == 0 && done[1].sequence == events.size() - 1 &&
          done[2].sequence == events.size(), "Windows waits lost a shared handle or readiness beyond one wait set");
    check(pending.poll().empty(), "Win32 operation delivered twice");
}

std::vector<PendingRequests::Done> wait_for_some(PendingRequests& pending) {
    for (;;) {
        auto done = pending.wait(std::nullopt);
        if (!done.empty()) return done;
    }
}

// More handles than one WaitForMultipleObjects takes: a blocking wait still
// wakes for a handle beyond the first set, and a deadline still ends it.
void many_handles() {
    LiveClock clock;
    PendingRequests pending(clock);
    std::array<ManualEvent, MAXIMUM_WAIT_OBJECTS + 6> events;
    for (std::size_t index = 0; index < events.size(); ++index) {
        auto operation = with_source(WindowsWait{events[index].handle});
        operation.attempt = [index](bool) { return value(index); };
        pending.add(index, std::move(operation), false);
    }
    {
        std::jthread setter([&] {
            std::this_thread::sleep_for(5ms);
            ::SetEvent(events[MAXIMUM_WAIT_OBJECTS + 3].handle);
        });
        completed(wait_for_some(pending), MAXIMUM_WAIT_OBJECTS + 3, MAXIMUM_WAIT_OBJECTS + 3);
    }
    auto timeout = with_source(WindowsWait{events[MAXIMUM_WAIT_OBJECTS + 4].handle});
    const auto deadline = clock.now() + 3'000'000;
    timeout.deadline = deadline;
    timeout.attempt = [&](bool) -> std::optional<HostCompletion> {
        check(clock.now() >= deadline, "Win32 attempt ran before readiness or deadline");
        return std::nullopt;
    };
    timeout.expire = [] { return value(1000); };
    pending.add(1000, std::move(timeout), false);
    completed(wait_for_some(pending), 1000, 1000);
}

class Winsock {
public:
    Winsock() {
        WSADATA data{};
        check(::WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup failed");
    }
    ~Winsock() { ::WSACleanup(); }
    Winsock(const Winsock&) = delete;
    Winsock& operator=(const Winsock&) = delete;
};

// A connected pair of loopback TCP sockets.
class SocketPair {
public:
    SocketPair() {
        const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        check(listener != INVALID_SOCKET, "socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof address;
        const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0 &&
                               ::listen(listener, 1) == 0 &&
                               ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0;
        writer_ = listening ? ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) : INVALID_SOCKET;
        const bool connected =
            writer_ != INVALID_SOCKET && ::connect(writer_, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0;
        reader_ = connected ? ::accept(listener, nullptr, nullptr) : INVALID_SOCKET;
        ::closesocket(listener);
        if (reader_ == INVALID_SOCKET) {
            if (writer_ != INVALID_SOCKET) ::closesocket(writer_);
            throw std::runtime_error("loopback socket pair failed");
        }
    }
    ~SocketPair() {
        ::closesocket(reader_);
        ::closesocket(writer_);
    }
    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;

    std::intptr_t reader() const { return static_cast<std::intptr_t>(reader_); }
    std::intptr_t writer() const { return static_cast<std::intptr_t>(writer_); }
    void send(char byte) const { check(::send(writer_, &byte, 1, 0) == 1, "socket send failed"); }
    void await_readable() const {
        WSAPOLLFD entry{reader_, POLLRDNORM, 0};
        check(::WSAPoll(&entry, 1, 5000) == 1, "sent byte did not arrive");
    }
    void receive(char expected) const {
        char byte = 0;
        check(::recv(reader_, &byte, 1, 0) == 1 && byte == expected, "socket not readable or wrong byte");
    }

private:
    SOCKET reader_ = INVALID_SOCKET;
    SOCKET writer_ = INVALID_SOCKET;
};

// A PosixWait names a SOCKET on Windows and is waited for with WSAPoll, alone
// or together with handles.
void native_sockets() {
    const Winsock winsock;
    LiveClock clock;
    PendingRequests pending(clock);
    SocketPair first, second;
    const auto reader = [&](const SocketPair& pair, char byte) {
        auto operation = with_source(PosixWait{{pair.reader(), PosixWait::Events::Read}});
        operation.attempt = [&pair, byte](bool) { pair.receive(byte); return value(static_cast<unsigned char>(byte)); };
        return operation;
    };
    pending.add(20, reader(first, 'a'), false);
    pending.add(10, reader(second, 'b'), false);
    check(pending.poll().empty(), "unsignaled socket read ran");
    first.send('a');
    second.send('b');
    // Loopback delivery is not synchronous with send(): both bytes are there
    // before the check, which then finds both sockets ready together.
    first.await_readable();
    second.await_readable();
    const auto done = pending.poll();
    check(done.size() == 2 && done[0].sequence == 10 && done[0].completion.value.bits == 'b' &&
              done[1].sequence == 20 && done[1].completion.value.bits == 'a',
          "joint socket readiness was not delivered in order");

    // A writable socket is ready at once; the interest can switch sockets.
    bool reading = true;
    auto switching = with_source(PosixWait{{}, [&] {
        return reading ? PosixWait::Interest{first.reader(), PosixWait::Events::Read}
                       : PosixWait::Interest{second.writer(), PosixWait::Events::ReadWrite};
    }});
    switching.attempt = [&](bool) -> std::optional<HostCompletion> {
        if (!reading) return value(3);
        first.receive('c');
        reading = false;
        return std::nullopt;
    };
    pending.add(3, std::move(switching), false);
    first.send('c');
    const auto switched = wait_for_some(pending);
    check(!reading, "dynamic socket wait did not switch interest");
    completed(switched, 3, 3);

    auto timeout = with_source(PosixWait{{first.reader(), PosixWait::Events::Read}});
    const auto deadline = clock.now() + 3'000'000;
    timeout.deadline = deadline;
    timeout.attempt = [&](bool) -> std::optional<HostCompletion> {
        check(clock.now() >= deadline, "socket attempt ran before readiness or deadline");
        return std::nullopt;
    };
    timeout.expire = [] { return value(4); };
    pending.add(4, std::move(timeout), false);
    completed(wait_for_some(pending), 4, 4);

    {
        pending.add(5, reader(second, 'd'), false);
        std::jthread writer([&] {
            std::this_thread::sleep_for(5ms);
            second.send('d');
        });
        completed(wait_for_some(pending), 5, 'd');
    }

    // Sockets and handles in one wait: each wakes it, and neither is taken
    // for the other. Past one wait set of handles too.
    std::array<ManualEvent, MAXIMUM_WAIT_OBJECTS + 1> events;
    for (std::size_t index = 0; index < events.size(); ++index) {
        auto operation = with_source(WindowsWait{events[index].handle});
        operation.attempt = [index](bool) { return value(100 + index); };
        pending.add(100 + index, std::move(operation), false);
    }
    pending.add(6, reader(first, 'e'), false);
    {
        std::jthread setter([&] {
            std::this_thread::sleep_for(5ms);
            ::SetEvent(events.back().handle);
        });
        completed(wait_for_some(pending), 100 + events.size() - 1, 100 + events.size() - 1);
    }
    {
        std::jthread writer([&] {
            std::this_thread::sleep_for(5ms);
            first.send('e');
        });
        completed(wait_for_some(pending), 6, 'e');
    }
    check(pending.poll().empty(), "a socket or handle wait delivered twice");
}

// A socket that is not open is ready at once, as POLLNVAL makes it after
// poll(2): alone (WSAPoll fails the call), next to an open socket (WSAPoll
// neither counts it nor ends its wait) and next to a handle.
void closed_sockets() {
    const Winsock winsock;
    LiveClock clock;
    PendingRequests pending(clock);
    SocketPair open;
    // Created after the pair, so no socket of the test gets its value.
    const SOCKET doomed = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    check(doomed != INVALID_SOCKET, "socket failed");
    check(::closesocket(doomed) == 0, "closesocket failed");
    const auto closed = static_cast<std::intptr_t>(doomed);
    // A deadline instead of a wait without end if the closed socket is
    // missed; the attempt must come long before it.
    const auto closed_wait = [&](std::uint64_t sequence) {
        auto operation = with_source(PosixWait{{closed, PosixWait::Events::Read}});
        const auto added = clock.now();
        operation.deadline = added + 3'000'000'000;
        operation.attempt = [&clock, added, sequence](bool) {
            check(clock.now() - added < 1'000'000'000, "a closed socket was not ready at once");
            return value(sequence);
        };
        operation.expire = [] { return value(0); };
        return operation;
    };

    pending.add(1, closed_wait(1), false);
    completed(wait_for_some(pending), 1, 1);

    auto waiting = with_source(PosixWait{{open.reader(), PosixWait::Events::Read}});
    waiting.attempt = [&open](bool) { open.receive('x'); return value('x'); };
    pending.add(2, std::move(waiting), false);
    pending.add(3, closed_wait(3), false);
    completed(wait_for_some(pending), 3, 3);
    check(pending.poll().empty(), "an open socket was taken as ready next to a closed one");

    const ManualEvent event;
    auto handle = with_source(WindowsWait{event.handle});
    handle.attempt = [](bool) { return value(5); };
    pending.add(5, std::move(handle), false);
    pending.add(4, closed_wait(4), false);
    completed(wait_for_some(pending), 4, 4);
    check(pending.poll().empty(), "a handle or open socket was taken as ready next to a closed socket");

    open.send('x');
    open.await_readable();
    completed(wait_for_some(pending), 2, 'x');
    check(::SetEvent(event.handle) != 0, "SetEvent failed");
    completed(wait_for_some(pending), 5, 5);
}

#else

class Pipe {
public:
    Pipe() {
        check(::pipe(fds_) == 0, "pipe failed");
        if (::fcntl(fds_[0], F_SETFL, O_NONBLOCK) != 0) {
            ::close(fds_[0]);
            ::close(fds_[1]);
            throw std::runtime_error("fcntl failed");
        }
    }
    ~Pipe() { ::close(fds_[0]); ::close(fds_[1]); }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    int reader() const { return fds_[0]; }
    int writer() const { return fds_[1]; }
    void send(char byte) const { check(::write(writer(), &byte, 1) == 1, "pipe write failed"); }
    void receive(char expected) const {
        char byte = 0;
        check(::read(reader(), &byte, 1) == 1 && byte == expected, "pipe not readable or wrong byte");
    }

private:
    int fds_[2];
};

void native_events() {
    LiveClock clock;
    PendingRequests pending(clock);
    Pipe first, second;
    const auto reader = [&](const Pipe& pipe, char byte) {
        auto operation = with_source(PosixWait{{pipe.reader(), PosixWait::Events::Read}});
        operation.attempt = [&pipe, byte](bool) { pipe.receive(byte); return value(static_cast<unsigned char>(byte)); };
        return operation;
    };
    pending.add(20, reader(first, 'a'), false);
    pending.add(10, reader(second, 'b'), false);
    check(pending.poll().empty(), "unsignaled POSIX read ran");
    first.send('a');
    second.send('b');
    const auto done = pending.poll();
    check(done.size() == 2 && done[0].sequence == 10 && done[0].completion.value.bits == 'b' &&
          done[1].sequence == 20 && done[1].completion.value.bits == 'a', "joint readiness was not delivered in order");

    bool reading = true;
    auto switching = with_source(PosixWait{{}, [&] {
        return reading ? PosixWait::Interest{first.reader(), PosixWait::Events::Read}
                       : PosixWait::Interest{second.writer(), PosixWait::Events::ReadWrite};
    }});
    switching.attempt = [&](bool) -> std::optional<HostCompletion> {
        if (!reading) return value(3);
        first.receive('c');
        reading = false;
        return std::nullopt;
    };
    pending.add(3, std::move(switching), false);
    first.send('c');
    check(pending.poll().empty() && !reading, "dynamic wait did not switch interest");
    completed(pending.poll(), 3, 3);

    auto timeout = with_source(PosixWait{{first.reader(), PosixWait::Events::Read}});
    const auto deadline = clock.now() + 3'000'000;
    timeout.deadline = deadline;
    timeout.attempt = [&](bool) -> std::optional<HostCompletion> {
        check(clock.now() >= deadline, "POSIX attempt ran before readiness or deadline");
        return std::nullopt;
    };
    timeout.expire = [] { return value(4); };
    pending.add(4, std::move(timeout), false);
    while (!pending.empty()) {
        auto expired = pending.wait(std::nullopt);
        if (!expired.empty()) completed(expired, 4, 4);
    }

    auto ready = reader(second, 'd');
    pending.add(5, std::move(ready), false);
    std::jthread writer([&] {
        std::this_thread::sleep_for(5ms);
        second.send('d');
    });
    while (!pending.empty()) {
        auto awakened = pending.wait(std::nullopt);
        if (!awakened.empty()) completed(awakened, 5, 'd');
    }
}

// One descriptor named by two pending operations and by background work in
// the same wait (a QUIC connection's socket): every backend wakes all of them.
void shared_descriptor() {
    LiveClock clock;
    PendingRequests pending(clock);
    Pipe shared;
    struct Watching final : BackgroundWork {
        int descriptor = -1;
        int runs = 0;
        std::vector<PosixWait::Interest> interests() override {
            return {PosixWait::Interest{descriptor, PosixWait::Events::Read}};
        }
        std::int64_t wake_at(std::int64_t) override { return PendingOperation::no_deadline; }
        void run() override { ++runs; }
    } work;
    work.descriptor = shared.reader();
    pending.add_background(&work);
    bool readable = false;
    for (const std::uint64_t sequence : {70, 71}) {
        auto operation = with_source(PosixWait{{shared.reader(), PosixWait::Events::Read}});
        operation.attempt = [&, sequence](bool) -> std::optional<HostCompletion> {
            if (!readable) return std::nullopt;
            return value(sequence);
        };
        pending.add(sequence, std::move(operation), false);
    }
    check(pending.poll().empty(), "operations on an idle shared descriptor completed");
    readable = true;
    shared.send('s');
    std::vector<PendingRequests::Done> done;
    while (done.size() < 2) {
        for (auto& answer : pending.wait(std::nullopt)) done.push_back(answer);
    }
    check(done.size() == 2 && done[0].sequence == 70 && done[1].sequence == 71 && work.runs >= 2,
          "a shared descriptor did not wake both operations and the background work");
}

// The no-spin guard: a wake-up that always answers "now" while its attempts
// never finish is backed off, so the driver does not busy-wait.
void wake_up_guard() {
    LiveClock clock;
    PendingRequests pending(clock);
    Pipe quiet;
    auto stuck = with_source(PosixWait{{quiet.reader(), PosixWait::Events::Read}});
    int attempts = 0;
    stuck.wake_at = [](std::int64_t now) { return now; };
    stuck.attempt = [&](bool cancelled) -> std::optional<HostCompletion> {
        ++attempts;
        if (cancelled) return value(60);
        return std::nullopt;
    };
    pending.add(60, std::move(stuck), true);
    const auto end = clock.now() + 300'000'000;
    int returns = 0;
    while (clock.now() < end) {
        static_cast<void>(pending.wait(end));
        ++returns;
    }
    check(attempts < 40 && returns < 40, "a wake-up that never finishes made the driver spin");
    pending.cancel(60);
    completed(pending.poll(), 60, 60, true);
}

// A live clock: the driver sleeps until the wake-up, not until readiness,
// and does not spin while it waits.
void live_wake_up() {
    LiveClock clock;
    PendingRequests pending(clock);
    Pipe quiet;
    auto timer = with_source(PosixWait{{quiet.reader(), PosixWait::Events::Read}});
    const auto due = clock.now() + 20'000'000;
    int attempts = 0;
    timer.wake_at = [due](std::int64_t) { return due; };
    timer.attempt = [&](bool) -> std::optional<HostCompletion> {
        ++attempts;
        if (clock.now() >= due) return value(40);
        return std::nullopt;
    };
    pending.add(40, std::move(timer), false);
    std::vector<PendingRequests::Done> done;
    while (done.empty()) done = pending.wait(std::nullopt);
    completed(done, 40, 40);
    check(clock.now() >= due, "the wake-up was attempted early");
    check(attempts <= 3, "the driver spun before the wake-up");
}

#endif

} // namespace

int main() {
    try {
        // NEURA_IO_BACKEND selects the backend these waits use; a requested
        // backend this kernel has must be the one in use, or the run would
        // test poll(2) twice.
        const auto backend = neura::nvm::io_backend();
        std::cout << "driver backend: " << neura::nvm::io_backend_name(backend) << '\n';
#ifdef _WIN32
        // Windows ignores NEURA_IO_BACKEND (W14).
        polled_and_external();
        external_cancel_and_deadline();
        native_events();
        many_handles();
        native_sockets();
        closed_sockets();
        provider_wake_ups();
        background_work();
#else
#ifdef __linux__
        if (backend == IoBackend::IoUring) {
            // Without io_uring the documented behaviour is the fallback to
            // poll(2) (docs/runtime/NVM.md), so the same waits must still hold.
            io_uring_params parameters{};
            const auto descriptor = ::syscall(__NR_io_uring_setup, 2, &parameters);
            if (descriptor < 0) {
                std::cout << "io_uring: fallback path, because io_uring_setup failed on this host ("
                          << std::strerror(errno) << "); checking the waits through poll(2)\n";
            } else {
                std::cout << "io_uring: strict path, because io_uring_setup succeeded\n";
                ::close(static_cast<int>(descriptor));
            }
        }
#endif
        polled_and_external();
        external_cancel_and_deadline();
        native_events();
        provider_wake_ups();
        background_work();
        live_wake_up();
        wake_up_guard();
        shared_descriptor();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

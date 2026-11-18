#include "nvm/driver.hpp"
#include "nvm/providers.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
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

#endif

} // namespace

int main() {
    try {
        // NEURA_IO_BACKEND selects the backend these waits use; a requested
        // backend this kernel has must be the one in use, or the run would
        // test poll(2) twice.
        const auto backend = neura::nvm::io_backend();
        std::cout << "driver backend: " << neura::nvm::io_backend_name(backend) << '\n';
        if (const char* wanted = std::getenv("NEURA_IO_BACKEND");
            wanted && std::string_view(wanted) != neura::nvm::io_backend_name(backend)) {
            // A container may forbid io_uring (kernel.io_uring_disabled, or
            // a seccomp filter); the driver then keeps poll(2).
            if (std::string_view(wanted) == "io_uring") {
                std::cout << "skipped: io_uring is not available here\n";
                return 0;
            }
            throw std::runtime_error(std::string("NEURA_IO_BACKEND=") + wanted + " was not selected");
        }
        polled_and_external();
        external_cancel_and_deadline();
        native_events();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

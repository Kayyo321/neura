#include "driver.hpp"

#include "providers.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <system_error>
#else
#include <cerrno>
#include <poll.h>
#endif

namespace neura::nvm {
namespace {

constexpr std::int64_t none = PendingOperation::no_deadline;
constexpr int retry_ms = 1;

// Round up so the clock has reached the deadline when the wait returns.
int milliseconds_until(std::int64_t deadline, std::int64_t now) {
    if (deadline <= now) return 0;
    const auto nanos = static_cast<std::uint64_t>(deadline) - static_cast<std::uint64_t>(now);
    const auto milliseconds = nanos / 1'000'000 + (nanos % 1'000'000 != 0 ? 1 : 0);
    return static_cast<int>(std::min<std::uint64_t>(milliseconds, INT_MAX));
}

void pause_briefly() { std::this_thread::sleep_for(std::chrono::milliseconds(retry_ms)); }

bool waits_natively(const WaitSource& source) {
    if (const auto* posix = std::get_if<PosixWait>(&source)) return posix->current().descriptor >= 0;
    return std::holds_alternative<WindowsWait>(source);
}

#ifdef _WIN32

void check_wait_result(DWORD result, DWORD count) {
    if (result == WAIT_FAILED) {
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(), "waiting for host handles");
    }
    if (result != WAIT_TIMEOUT && result >= WAIT_OBJECT_0 + count) {
        throw std::logic_error("a host wait needs a level-signaled Windows handle");
    }
}

class NativeWaits {
public:
    std::size_t add(PosixWait::Interest) {
        throw std::logic_error("a POSIX descriptor wait needs poll(2)");
    }

    std::size_t add(WindowsWait source) {
        const auto [at, inserted] = slots_.emplace(source.handle, handles_.size());
        if (inserted) handles_.push_back(source.handle);
        return at->second;
    }

    void wait(int timeout_ms) {
        if (handles_.empty()) return;
        const auto max_handles = static_cast<std::size_t>(MAXIMUM_WAIT_OBJECTS);
        if (handles_.size() > max_handles) {
            // The kernel wait accepts a bounded set. Cap the first batch's
            // wait so activity elsewhere is still observed promptly.
            timeout_ms = timeout_ms < 0 ? retry_ms : std::min(timeout_ms, retry_ms);
        }
        const auto count = static_cast<DWORD>(std::min(handles_.size(), max_handles));
        const DWORD timeout = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);
        const DWORD first = ::WaitForMultipleObjects(count, handles_.data(), FALSE, timeout);
        check_wait_result(first, count);

        ready_.resize(handles_.size());
        for (std::size_t index = 0; index < handles_.size(); ++index) {
            if (first != WAIT_TIMEOUT && index == first - WAIT_OBJECT_0) {
                ready_[index] = true;
                continue;
            }
            const DWORD result = ::WaitForSingleObject(handles_[index], 0);
            check_wait_result(result, 1);
            ready_[index] = result == WAIT_OBJECT_0;
        }
    }

    bool ready(std::size_t index) const { return ready_[index]; }

private:
    // Each handle occurs once, even if more than one request waits for it.
    std::map<HANDLE, std::size_t> slots_;
    std::vector<HANDLE> handles_;
    std::vector<bool> ready_;
};

#else

short poll_events(PosixWait::Events events) {
    switch (events) {
    case PosixWait::Events::Read: return POLLIN;
    case PosixWait::Events::Write: return POLLOUT;
    case PosixWait::Events::ReadWrite: return POLLIN | POLLOUT;
    }
    throw std::logic_error("invalid POSIX wait events");
}

class NativeWaits {
public:
    std::size_t add(PosixWait::Interest source) {
        const auto index = descriptors_.size();
        descriptors_.push_back({source.descriptor, poll_events(source.events), 0});
        return index;
    }

    std::size_t add(WindowsWait) {
        throw std::logic_error("a Windows handle wait needs Win32");
    }

    void wait(int timeout_ms) {
        if (descriptors_.empty()) return;
        const int count = ::poll(descriptors_.data(), static_cast<nfds_t>(descriptors_.size()), timeout_ms);
        if (count < 0) {
            if (errno != EINTR) throw std::runtime_error("poll failed while waiting for host requests");
            for (auto& descriptor : descriptors_) descriptor.revents = 0;
        }
    }

    bool ready(std::size_t index) const { return descriptors_[index].revents != 0; }

private:
    std::vector<pollfd> descriptors_;
};

#endif

} // namespace

void PendingRequests::add(std::uint64_t sequence, std::variant<HostCompletion, PendingOperation> started,
                          bool cancellable) {
    Entry entry;
    entry.cancellable = cancellable;
    if (auto* completion = std::get_if<HostCompletion>(&started)) {
        // A completion inside begin() is still delivered at a scheduling point.
        entry.answer = std::move(*completion);
    } else {
        entry.operation = std::move(std::get<PendingOperation>(started));
        if (!entry.operation.attempt) throw std::logic_error("a pending host operation has no attempt function");
        if (entry.operation.deadline != none && !entry.operation.expire) {
            throw std::logic_error("a pending host operation with a deadline has no expire function");
        }
        const auto& source = entry.operation.wait_source;
        if (const auto* external = std::get_if<ExternalWait>(&source); external && !external->signal) {
            throw std::logic_error("an external wait needs a signal token");
        }
        if (const auto* windows = std::get_if<WindowsWait>(&source); windows && !windows->handle) {
            throw std::logic_error("a Windows wait needs a handle");
        }
    }
    if (!entries_.emplace(sequence, std::move(entry)).second) {
        throw std::logic_error("host request " + std::to_string(sequence) + " started twice");
    }
}

void PendingRequests::cancel(std::uint64_t sequence) {
    const auto found = entries_.find(sequence);
    if (found == entries_.end()) return;
    auto& entry = found->second;
    entry.cancelled = true;
    // A finished operation keeps its answer. A noncancellable one runs to its end.
    if (entry.answer || !entry.cancellable) return;
    check(0, sequence);
    if (entry.answer) return;
    auto answer = entry.operation.attempt(true);
    entry.answer = answer ? std::move(*answer) : HostCompletion::failed(HostValue{host_error::cancelled, {}, {}});
}

void PendingRequests::check(int timeout_ms, std::optional<std::uint64_t> only) {
    struct Check {
        Entry* entry;
        std::optional<std::size_t> native_index{};
        ExternalSignal* external = nullptr;
        bool ready = false;
    };

    NativeWaits native;
    std::vector<Check> checks;
    for (auto& [sequence, entry] : entries_) {
        if (entry.answer || (only && *only != sequence)) continue;
        auto& check = checks.emplace_back(Check{&entry});
        const auto& source = entry.operation.wait_source;
        if (const auto* posix = std::get_if<PosixWait>(&source)) {
            const auto current = posix->current();
            if (current.descriptor >= 0) check.native_index = native.add(current);
        } else if (const auto* windows = std::get_if<WindowsWait>(&source)) {
            check.native_index = native.add(*windows);
        } else if (const auto* external = std::get_if<ExternalWait>(&source)) {
            check.external = external->signal.get();
        }
    }
    native.wait(timeout_ms);
    for (auto& check : checks) {
        check.ready = check.native_index ? native.ready(*check.native_index) : !check.external || check.external->take();
    }

    std::optional<std::int64_t> now;
    for (const auto& check : checks) {
        auto& entry = *check.entry;
        const auto& operation = entry.operation;
        if (!now && operation.deadline != none) now = clock_.now();
        const bool due = operation.deadline != none && operation.deadline <= *now;
        if (!check.ready && !due) continue;
        auto answer = operation.attempt(false);
        if (!answer && due) answer = operation.expire();
        if (answer) entry.answer = std::move(*answer);
    }
}

std::vector<PendingRequests::Done> PendingRequests::collect() {
    std::vector<Done> done;
    for (auto at = entries_.begin(); at != entries_.end();) {
        if (at->second.answer) {
            done.push_back({at->first, std::move(*at->second.answer), at->second.cancelled});
            at = entries_.erase(at);
        } else {
            ++at;
        }
    }
    return done;
}

std::vector<PendingRequests::Done> PendingRequests::poll() {
    check(0);
    return collect();
}

std::vector<PendingRequests::Done> PendingRequests::wait(std::optional<std::int64_t> clock_deadline) {
    check(0);
    if (auto done = collect(); !done.empty()) return done;
    auto earliest = clock_deadline.value_or(none);
    bool has_native_wait = false;
    bool retry_soon = false;
    for (const auto& [sequence, entry] : entries_) {
        const auto& operation = entry.operation;
        earliest = std::min(earliest, operation.deadline);
        if (waits_natively(operation.wait_source)) {
            has_native_wait = true;
        } else if (std::holds_alternative<ExternalWait>(operation.wait_source) || operation.deadline == none) {
            retry_soon = true;
        }
    }
    if (clock_.virtual_time()) {
        if (earliest != none) {
            clock_.wait_until(earliest);
        } else if (has_native_wait) {
            check(retry_soon ? retry_ms : -1);
        } else if (retry_soon) {
            pause_briefly();
        }
        check(0);
        return collect();
    }

    int timeout_ms = earliest == none ? -1 : milliseconds_until(earliest, clock_.now());
    if (retry_soon) timeout_ms = timeout_ms < 0 ? retry_ms : std::min(timeout_ms, retry_ms);
    if (has_native_wait) {
        check(timeout_ms);
    } else {
        if (earliest != none && !retry_soon) {
            clock_.wait_until(earliest);
        } else if (retry_soon && timeout_ms != 0) {
            pause_briefly();
        }
        check(0);
    }
    return collect();
}

} // namespace neura::nvm

#include "driver.hpp"

#include "providers.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <poll.h>
#endif

namespace neura::nvm {

std::vector<std::uint8_t> host_modes_for(const nvr::Image& image, const Bindings& bindings) {
    std::vector<std::uint8_t> modes;
    if (!image.transferable_layouts) return modes;
    modes.reserve(bindings.services.size());
    for (const auto* service : bindings.services) {
        modes.push_back(service == nullptr || !service->overlapping() ? 0 : service->cancellable() ? 2 : 1);
    }
    return modes;
}

namespace {

constexpr std::int64_t none = PendingOperation::no_deadline;

// Milliseconds to wait for `nanos` nanoseconds: rounded up, so that the clock
// has reached the deadline when the wait returns.
int milliseconds_for(std::int64_t nanos) {
    if (nanos <= 0) return 0;
    const auto milliseconds = nanos / 1'000'000 + (nanos % 1'000'000 != 0 ? 1 : 0);
    return static_cast<int>(std::min<std::int64_t>(milliseconds, INT_MAX));
}

void pause_briefly() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

} // namespace

void PendingRequests::add(std::uint64_t sequence, std::variant<HostCompletion, PendingOperation> started,
                          bool cancellable) {
    Entry entry;
    entry.cancellable = cancellable;
    if (auto* completion = std::get_if<HostCompletion>(&started)) {
        // It finished inside begin(); it is still delivered at a scheduling
        // point, like every other answer.
        entry.answer = std::move(*completion);
    } else {
        entry.operation = std::move(std::get<PendingOperation>(started));
        if (!entry.operation.attempt) throw std::logic_error("a pending host operation has no attempt function");
        if (entry.operation.deadline != none && !entry.operation.expire) {
            throw std::logic_error("a pending host operation with a deadline has no expire function");
        }
    }
    if (!entries_.emplace(sequence, std::move(entry)).second) {
        throw std::logic_error("host request " + std::to_string(sequence) + " started twice");
    }
}

int PendingRequests::descriptor_of(const Entry& entry) const {
    return entry.operation.current_descriptor ? entry.operation.current_descriptor() : entry.operation.descriptor;
}

void PendingRequests::cancel(std::uint64_t sequence) {
    const auto found = entries_.find(sequence);
    if (found == entries_.end()) return;
    auto& entry = found->second;
    entry.cancelled = true;
    // Exactly one answer: one that exists already stays, and an operation
    // that cannot be cancelled runs to its own end.
    if (entry.answer || !entry.cancellable) return;
    // The operation may have finished before the cancel request was
    // processed. Then its completion wins.
    check(0, sequence);
    if (entry.answer) return;
    auto answer = entry.operation.attempt(true);
    entry.answer = answer ? std::move(*answer) : HostCompletion::failed(HostValue{host_error::cancelled, {}, {}});
}

void PendingRequests::check(int timeout_ms, std::optional<std::uint64_t> only) {
    std::int64_t now = 0;
    bool clocked = false;
    const auto read_clock = [&] {
        if (!clocked) {
            now = clock_.now();
            clocked = true;
        }
        return now;
    };
#ifndef _WIN32
    std::vector<pollfd> descriptors;
    std::vector<std::map<std::uint64_t, Entry>::iterator> owners;
    for (auto at = entries_.begin(); at != entries_.end(); ++at) {
        if (at->second.answer || (only && *only != at->first)) continue;
        const auto descriptor = descriptor_of(at->second);
        if (descriptor < 0) continue;
        short events = 0;
        if (at->second.operation.readable) events |= POLLIN;
        if (at->second.operation.writable) events |= POLLOUT;
        if (events == 0) events = POLLIN;
        descriptors.push_back({descriptor, events, 0});
        owners.push_back(at);
    }
    if (!descriptors.empty()) {
        const int ready = ::poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), timeout_ms);
        if (ready < 0) {
            if (errno != EINTR) throw std::runtime_error("poll failed while waiting for host requests");
            for (auto& entry : descriptors) entry.revents = 0;
        }
    }
    std::map<std::uint64_t, bool> fired;
    for (std::size_t i = 0; i < owners.size(); ++i) fired[owners[i]->first] = descriptors[i].revents != 0;
#else
    for (const auto& [sequence, entry] : entries_) {
        if (!entry.answer && (!only || *only == sequence) && descriptor_of(entry) >= 0) {
            throw std::logic_error("a descriptor wait needs poll(2), which this platform does not provide");
        }
    }
    std::map<std::uint64_t, bool> fired;
    (void)timeout_ms;
#endif
    for (auto& [sequence, entry] : entries_) {
        if (entry.answer || (only && *only != sequence)) continue;
        const auto& operation = entry.operation;
        const bool waits_for_descriptor = fired.contains(sequence);
        const bool due = operation.deadline != none && operation.deadline <= read_clock();
        if (waits_for_descriptor && !fired[sequence] && !due) continue;
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
    bool has_descriptor = false;
    bool attempt_driven = false;
    for (const auto& [sequence, entry] : entries_) {
        earliest = std::min(earliest, entry.operation.deadline);
        if (descriptor_of(entry) >= 0) {
            has_descriptor = true;
        } else if (entry.operation.deadline == none) {
            // Nothing says when it can finish; ask again soon.
            attempt_driven = true;
        }
    }
    const bool virtual_time = clock_.virtual_time();
    if (virtual_time) {
        if (earliest != none) {
            clock_.wait_until(earliest);
        } else if (has_descriptor) {
            check(attempt_driven ? 1 : -1);
        } else if (attempt_driven) {
            pause_briefly();
        }
        check(0);
        return collect();
    }
    int timeout_ms = -1;
    if (earliest != none) timeout_ms = milliseconds_for(earliest - clock_.now());
    if (attempt_driven) timeout_ms = timeout_ms < 0 ? 1 : std::min(timeout_ms, 1);
    if (has_descriptor) {
        check(timeout_ms);
    } else {
        if (earliest != none && !attempt_driven) {
            clock_.wait_until(earliest);
        } else if (attempt_driven) {
            pause_briefly();
        }
        check(0);
    }
    return collect();
}

Event drive(Execution& execution, const Bindings& bindings, TaskClock& clock, DriveHooks& hooks) {
    PendingRequests pending(clock);
    const auto deliver = [&](const Event& at, std::vector<PendingRequests::Done>& done) {
        for (auto& answer : done) {
            if (hooks.delivered) hooks.delivered(at.tick, answer.sequence, answer.cancelled, answer.completion);
            execution.deliver(answer.sequence, answer.completion);
        }
    };
    const auto read_clock = [&](const Event& at) -> std::size_t {
        const auto now = clock.now();
        const auto expired = execution.advance_clock(now);
        if (expired != 0 && hooks.clock_read) hooks.clock_read(at.tick, now);
        return expired;
    };
    for (;;) {
        const auto event = execution.advance();
        switch (event.kind) {
        case Event::Kind::HostRequest: {
            if (!event.overlapping) {
                execution.complete(hooks.serve(event));
                break;
            }
            auto request = event.request;
            request.now = clock.now();
            auto& service = *bindings.services.at(request.import);
            const bool cancellable = service.cancellable();
            if (hooks.issued) hooks.issued(event, cancellable);
            pending.add(request.sequence, service.begin(request), cancellable);
            break;
        }
        case Event::Kind::Poll: {
            auto done = pending.poll();
            deliver(event, done);
            if (event.timer) read_clock(event);
            break;
        }
        case Event::Kind::Waiting: {
            if (pending.empty() && !event.timer) {
                throw std::logic_error("the run waits for a host request this driver never started");
            }
            for (;;) {
                auto done = pending.wait(event.timer ? std::optional(event.deadline) : std::nullopt);
                deliver(event, done);
                const auto expired = event.timer ? read_clock(event) : 0;
                if (!done.empty() || expired != 0) break;
            }
            break;
        }
        case Event::Kind::Cancel:
            pending.cancel(event.sequence);
            break;
        case Event::Kind::Completed:
        case Event::Kind::Trapped:
            return event;
        }
    }
}

} // namespace neura::nvm

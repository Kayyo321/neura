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
#include <cstdlib>
#include <cstring>
#include <map>
#include <poll.h>
#include <string_view>
#include <unistd.h>
#ifdef __linux__
#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#endif
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

#ifdef __linux__
// The epoll(7) backend: one epoll instance per driver thread, kept between
// checks. Each check registers the descriptors it waits for with the events
// it wants and removes the rest. Readiness is level-triggered, exactly as
// poll(2) reports it; only how the kernel is asked differs. The driver
// applies the ready operations in request order whichever backend reported
// them (docs/runtime/NVM.md, "Waiting for host requests").
class EpollSet {
public:
    EpollSet() : descriptor_(::epoll_create1(EPOLL_CLOEXEC)) {}
    ~EpollSet() {
        if (descriptor_ >= 0) ::close(descriptor_);
    }
    EpollSet(const EpollSet&) = delete;
    EpollSet& operator=(const EpollSet&) = delete;
    bool usable() const { return descriptor_ >= 0; }

    // Fills `revents` of each entry; false when epoll cannot serve this set
    // (a descriptor it refuses, such as a regular file), and poll(2) must.
    bool wait(std::vector<pollfd>& entries, int timeout_ms) {
        std::map<int, std::uint32_t> wanted;
        for (const auto& entry : entries) {
            if (entry.fd < 0) continue;
            wanted[entry.fd] |= (entry.events & POLLIN ? EPOLLIN : 0u) | (entry.events & POLLOUT ? EPOLLOUT : 0u);
        }
        for (auto at = registered_.begin(); at != registered_.end();) {
            if (wanted.contains(at->first)) {
                ++at;
                continue;
            }
            ::epoll_ctl(descriptor_, EPOLL_CTL_DEL, at->first, nullptr); // It may have been closed: no check.
            at = registered_.erase(at);
        }
        for (const auto& [fd, events] : wanted) {
            epoll_event event{};
            event.events = events;
            event.data.fd = fd;
            const auto found = registered_.find(fd);
            int result = 0;
            if (found == registered_.end()) {
                result = ::epoll_ctl(descriptor_, EPOLL_CTL_ADD, fd, &event);
                if (result < 0 && errno == EEXIST) result = ::epoll_ctl(descriptor_, EPOLL_CTL_MOD, fd, &event);
            } else {
                // A descriptor closed and reopened under the same number is
                // no longer in the set: add it again.
                result = ::epoll_ctl(descriptor_, EPOLL_CTL_MOD, fd, &event);
                if (result < 0 && errno == ENOENT) result = ::epoll_ctl(descriptor_, EPOLL_CTL_ADD, fd, &event);
            }
            if (result < 0) {
                registered_.erase(fd);
                return false;
            }
            registered_[fd] = events;
        }
        std::vector<epoll_event> events(std::max<std::size_t>(wanted.size(), 1));
        const int count = ::epoll_wait(descriptor_, events.data(), static_cast<int>(events.size()), timeout_ms);
        for (auto& entry : entries) entry.revents = 0;
        if (count < 0) {
            if (errno != EINTR) throw std::runtime_error("epoll_wait failed while waiting for host requests");
            return true;
        }
        std::map<int, std::uint32_t> ready;
        for (int i = 0; i < count; ++i) ready[events[i].data.fd] |= events[i].events;
        for (auto& entry : entries) {
            const auto found = ready.find(entry.fd);
            if (found == ready.end()) continue;
            const auto got = found->second;
            entry.revents = static_cast<short>((got & EPOLLIN ? POLLIN : 0) | (got & EPOLLOUT ? POLLOUT : 0) |
                                               (got & EPOLLERR ? POLLERR : 0) | (got & EPOLLHUP ? POLLHUP : 0));
        }
        return true;
    }

private:
    int descriptor_;
    std::map<int, std::uint32_t> registered_;
};
#endif

#ifdef __linux__
// The io_uring backend: one ring per driver thread. Each check submits a
// one-shot IORING_OP_POLL_ADD per descriptor and, for a bounded wait, an
// IORING_OP_TIMEOUT; waits for the first completion; then removes the polls
// and the timeout that did not complete and reaps every completion, so the
// ring is empty again before the check returns. Raw system calls: no
// liburing. Readiness is reported as poll(2) would report it.
class UringSet {
public:
    UringSet() {
        io_uring_params parameters{};
        descriptor_ = static_cast<int>(::syscall(__NR_io_uring_setup, entries, &parameters));
        if (descriptor_ < 0) return;
        const auto sq_bytes = parameters.sq_off.array + parameters.sq_entries * sizeof(std::uint32_t);
        const auto cq_bytes = parameters.cq_off.cqes + parameters.cq_entries * sizeof(io_uring_cqe);
        const bool single = (parameters.features & IORING_FEAT_SINGLE_MMAP) != 0;
        sq_bytes_ = single ? std::max(sq_bytes, cq_bytes) : sq_bytes;
        cq_bytes_ = single ? 0 : cq_bytes;
        sq_ring_ = ::mmap(nullptr, sq_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, descriptor_,
                          IORING_OFF_SQ_RING);
        if (sq_ring_ == MAP_FAILED) {
            close_all();
            return;
        }
        cq_ring_ = single ? sq_ring_
                          : ::mmap(nullptr, cq_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
                                   descriptor_, IORING_OFF_CQ_RING);
        if (cq_ring_ == MAP_FAILED) {
            close_all();
            return;
        }
        sqes_bytes_ = parameters.sq_entries * sizeof(io_uring_sqe);
        sqes_ = static_cast<io_uring_sqe*>(::mmap(nullptr, sqes_bytes_, PROT_READ | PROT_WRITE,
                                                  MAP_SHARED | MAP_POPULATE, descriptor_, IORING_OFF_SQES));
        if (sqes_ == MAP_FAILED) {
            close_all();
            return;
        }
        auto* sq = static_cast<char*>(sq_ring_);
        sq_head_ = reinterpret_cast<unsigned*>(sq + parameters.sq_off.head);
        sq_tail_ = reinterpret_cast<unsigned*>(sq + parameters.sq_off.tail);
        sq_mask_ = *reinterpret_cast<unsigned*>(sq + parameters.sq_off.ring_mask);
        sq_array_ = reinterpret_cast<unsigned*>(sq + parameters.sq_off.array);
        sq_entries_ = parameters.sq_entries;
        auto* cq = static_cast<char*>(cq_ring_);
        cq_head_ = reinterpret_cast<unsigned*>(cq + parameters.cq_off.head);
        cq_tail_ = reinterpret_cast<unsigned*>(cq + parameters.cq_off.tail);
        cq_mask_ = *reinterpret_cast<unsigned*>(cq + parameters.cq_off.ring_mask);
        cqes_ = reinterpret_cast<io_uring_cqe*>(cq + parameters.cq_off.cqes);
        usable_ = true;
    }
    ~UringSet() { close_all(); }
    UringSet(const UringSet&) = delete;
    UringSet& operator=(const UringSet&) = delete;
    bool usable() const { return usable_; }

    // Fills `revents`; false when the ring cannot take this many polls, and
    // poll(2) must serve the check.
    bool wait(std::vector<pollfd>& entries, int timeout_ms) {
        // Polls, a timeout, then as many removals: within one submission.
        if (2 * entries.size() + 2 > sq_entries_) return false;
        for (auto& entry : entries) entry.revents = 0;
        std::vector<bool> done(entries.size(), false);
        std::size_t outstanding = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].fd < 0) {
                done[i] = true;
                continue;
            }
            auto& sqe = next_sqe();
            sqe.opcode = IORING_OP_POLL_ADD;
            sqe.fd = entries[i].fd;
            sqe.poll32_events = static_cast<std::uint32_t>(entries[i].events);
            sqe.user_data = i + 1;
            ++outstanding;
        }
        __kernel_timespec span{};
        bool timer = false;
        if (timeout_ms >= 0) {
            span.tv_sec = timeout_ms / 1000;
            span.tv_nsec = static_cast<long long>(timeout_ms % 1000) * 1'000'000;
            auto& sqe = next_sqe();
            sqe.opcode = IORING_OP_TIMEOUT;
            sqe.fd = -1;
            sqe.addr = reinterpret_cast<std::uint64_t>(&span);
            sqe.len = 1;
            sqe.user_data = timeout_tag;
            timer = true;
            ++outstanding;
        }
        if (outstanding == 0) return true;
        bool ready = false;
        bool timer_done = !timer;
        const auto reap = [&] {
            unsigned head = *cq_head_;
            const unsigned tail = __atomic_load_n(cq_tail_, __ATOMIC_ACQUIRE);
            for (; head != tail; ++head) {
                const auto& cqe = cqes_[head & cq_mask_];
                if (cqe.user_data == timeout_tag) {
                    timer_done = true;
                    --outstanding;
                } else if (cqe.user_data >= 1 && cqe.user_data <= entries.size()) {
                    const auto index = static_cast<std::size_t>(cqe.user_data - 1);
                    if (!done[index]) {
                        done[index] = true;
                        --outstanding;
                        if (cqe.res > 0) {
                            entries[index].revents = static_cast<short>(cqe.res);
                            ready = true;
                        } else if (cqe.res != -ECANCELED) {
                            entries[index].revents = POLLERR;
                            ready = true;
                        }
                    }
                }
                // Removal completions carry remove_tag and are not counted.
            }
            __atomic_store_n(cq_head_, head, __ATOMIC_RELEASE);
        };
        if (!enter(pending_submissions_, 1)) return false;
        reap();
        while (!ready && !timer_done) {
            if (!enter(0, 1)) return false;
            reap();
        }
        // Remove what did not complete, then reap until nothing is left.
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (done[i]) continue;
            auto& sqe = next_sqe();
            sqe.opcode = IORING_OP_POLL_REMOVE;
            sqe.fd = -1;
            sqe.addr = i + 1;
            sqe.user_data = remove_tag;
        }
        if (!timer_done) {
            auto& sqe = next_sqe();
            sqe.opcode = IORING_OP_TIMEOUT_REMOVE;
            sqe.fd = -1;
            sqe.addr = timeout_tag;
            sqe.user_data = remove_tag;
        }
        if (pending_submissions_ != 0 && !enter(pending_submissions_, 0)) return false;
        while (outstanding != 0) {
            if (!enter(0, 1)) return false;
            reap();
        }
        // The removals' own completions.
        reap();
        return true;
    }

private:
    static constexpr unsigned entries = 256;
    static constexpr std::uint64_t timeout_tag = ~std::uint64_t{0};
    static constexpr std::uint64_t remove_tag = ~std::uint64_t{0} - 1;

    io_uring_sqe& next_sqe() {
        const unsigned tail = *sq_tail_;
        const unsigned index = tail & sq_mask_;
        auto& sqe = sqes_[index];
        std::memset(&sqe, 0, sizeof sqe);
        sq_array_[index] = index;
        __atomic_store_n(sq_tail_, tail + 1, __ATOMIC_RELEASE);
        ++pending_submissions_;
        return sqe;
    }

    bool enter(unsigned submit, unsigned wait) {
        for (;;) {
            const long result = ::syscall(__NR_io_uring_enter, descriptor_, submit, wait,
                                          wait != 0 ? IORING_ENTER_GETEVENTS : 0, nullptr, 0);
            if (result >= 0) {
                pending_submissions_ -= std::min<unsigned>(pending_submissions_, static_cast<unsigned>(result));
                submit -= std::min<unsigned>(submit, static_cast<unsigned>(result));
                if (submit == 0) return true;
                wait = 0;
                continue;
            }
            if (errno == EINTR) {
                if (submit == 0) return true; // The caller checks the ring again.
                continue;
            }
            usable_ = false;
            return false;
        }
    }

    void close_all() {
        if (sqes_ && sqes_ != MAP_FAILED) ::munmap(sqes_, sqes_bytes_);
        if (cq_ring_ && cq_ring_ != MAP_FAILED && cq_ring_ != sq_ring_) ::munmap(cq_ring_, cq_bytes_);
        if (sq_ring_ && sq_ring_ != MAP_FAILED) ::munmap(sq_ring_, sq_bytes_);
        if (descriptor_ >= 0) ::close(descriptor_);
        sqes_ = nullptr;
        cq_ring_ = sq_ring_ = nullptr;
        descriptor_ = -1;
        usable_ = false;
    }

    int descriptor_ = -1;
    bool usable_ = false;
    void* sq_ring_ = nullptr;
    void* cq_ring_ = nullptr;
    io_uring_sqe* sqes_ = nullptr;
    std::size_t sq_bytes_ = 0;
    std::size_t cq_bytes_ = 0;
    std::size_t sqes_bytes_ = 0;
    unsigned* sq_head_ = nullptr;
    unsigned* sq_tail_ = nullptr;
    unsigned* sq_array_ = nullptr;
    unsigned sq_mask_ = 0;
    unsigned sq_entries_ = 0;
    unsigned* cq_head_ = nullptr;
    unsigned* cq_tail_ = nullptr;
    unsigned cq_mask_ = 0;
    io_uring_cqe* cqes_ = nullptr;
    unsigned pending_submissions_ = 0;
};
#endif

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
#ifdef __linux__
        if (io_backend() == IoBackend::Epoll) {
            thread_local EpollSet set;
            if (set.usable() && set.wait(descriptors_, timeout_ms)) return;
        } else if (io_backend() == IoBackend::IoUring) {
            thread_local UringSet ring;
            if (ring.usable() && ring.wait(descriptors_, timeout_ms)) return;
        }
#endif
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

IoBackend io_backend() {
#if defined(__linux__)
    static const IoBackend chosen = [] {
        const char* text = std::getenv("NEURA_IO_BACKEND");
        if (text && std::string_view(text) == "epoll") {
            const int probe = ::epoll_create1(EPOLL_CLOEXEC);
            if (probe >= 0) {
                ::close(probe);
                return IoBackend::Epoll;
            }
        }
        if (text && std::string_view(text) == "io_uring") {
            // A kernel without io_uring, or with it disabled
            // (kernel.io_uring_disabled), keeps poll.
            io_uring_params parameters{};
            const long probe = ::syscall(__NR_io_uring_setup, 2, &parameters);
            if (probe >= 0) {
                ::close(static_cast<int>(probe));
                return IoBackend::IoUring;
            }
        }
        return IoBackend::Poll;
    }();
    return chosen;
#elif defined(_WIN32)
    return IoBackend::WaitForMultipleObjects;
#else
    return IoBackend::Poll;
#endif
}

const char* io_backend_name(IoBackend backend) {
    switch (backend) {
    case IoBackend::Poll: return "poll";
    case IoBackend::Epoll: return "epoll";
    case IoBackend::IoUring: return "io_uring";
    case IoBackend::WaitForMultipleObjects: return "WaitForMultipleObjects";
    }
    return "poll";
}

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

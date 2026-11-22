#include "driver.hpp"

#include "providers.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include "platform/windows.hpp"

#include <map>
#include <system_error>

// WSAPoll. neura_wait_source_tests compiles this file without the platform
// library, which links Winsock for the rest of the NVM.
#pragma comment(lib, "ws2_32.lib")
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <poll.h>
#include <string_view>
#include <unistd.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <fcntl.h>
#include <sys/event.h>
#define NEURA_HAS_KQUEUE 1
#endif
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

// Called by the thread pool when a handle beyond the first wait set is
// signaled: wakes the wait through the fan-in event.
VOID CALLBACK wake_fan_in(PVOID event, BOOLEAN) { ::SetEvent(static_cast<HANDLE>(event)); }

// The Windows waits (W14): sockets of PosixWait interests through WSAPoll,
// handles of WindowsWait sources through WaitForMultipleObjects. A wait with
// both alternates between them in slices of retry_ms. Readiness is
// level-triggered, as poll(2) reports it.
class NativeWaits {
public:
    // A PosixWait names a SOCKET on Windows.
    std::size_t add(PosixWait::Interest source) {
        const auto socket = static_cast<SOCKET>(source.descriptor);
        const short wanted = source.events == PosixWait::Events::Read    ? POLLRDNORM
                             : source.events == PosixWait::Events::Write ? POLLWRNORM
                                                                         : POLLRDNORM | POLLWRNORM;
        // Each socket occurs once in the poll set, asked for every event
        // that a request waits for.
        const auto [at, inserted] = socket_slots_.emplace(socket, sockets_.size());
        if (inserted) sockets_.push_back(WSAPOLLFD{socket, 0, 0});
        sockets_[at->second].events = static_cast<short>(sockets_[at->second].events | wanted);
        slots_.push_back({false, at->second, wanted});
        return slots_.size() - 1;
    }

    std::size_t add(WindowsWait source) {
        // Each handle occurs once, even if more than one request waits for it.
        const auto [at, inserted] = handle_slots_.emplace(source.handle, handles_.size());
        if (inserted) handles_.push_back(source.handle);
        slots_.push_back({true, at->second, 0});
        return slots_.size() - 1;
    }

    void wait(int timeout_ms, IoBackend) {
        socket_ready_.assign(sockets_.size(), 0);
        handle_ready_.assign(handles_.size(), false);
        if (handles_.empty()) {
            if (!sockets_.empty()) poll_sockets(timeout_ms);
            return;
        }
        if (sockets_.empty()) {
            wait_handles(timeout_ms);
            return;
        }
        // Neither wait can include the other's objects without changing the
        // sockets (WSAEventSelect makes them nonblocking and edge-triggered),
        // so they take turns.
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            if (poll_sockets(0)) {
                wait_handles(0);
                return;
            }
            int slice = retry_ms;
            if (timeout_ms >= 0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count();
                if (elapsed >= timeout_ms) {
                    wait_handles(0);
                    return;
                }
                slice = static_cast<int>(std::min<std::int64_t>(slice, timeout_ms - elapsed));
            }
            if (wait_handles(slice)) {
                poll_sockets(0);
                return;
            }
        }
    }

    bool ready(std::size_t index) const {
        const auto& slot = slots_[index];
        if (slot.handle) return handle_ready_[slot.index];
        // An error, a hang-up or an invalid socket is ready too: the attempt
        // reports it, as after poll(2).
        constexpr short failures = POLLERR | POLLHUP | POLLNVAL;
        return (socket_ready_[slot.index] & (slot.events | failures)) != 0;
    }

private:
    struct Slot {
        bool handle = false;
        std::size_t index = 0;
        short events = 0;
    };

    // Fills socket_ready_; true when any socket is ready. WSAPoll differs
    // from poll(2) for a socket that is not open: alone, it fails the whole
    // call with WSAENOTSOCK, and next to open sockets it reports POLLNVAL
    // without counting it or ending the wait. A check without waiting comes
    // first, so such a socket makes the wait ready at once, as POLLNVAL does
    // after poll(2).
    bool poll_sockets(int timeout_ms) {
        if (poll_sockets_once(0) || timeout_ms == 0) return any_socket_ready();
        return poll_sockets_once(timeout_ms);
    }

    // One WSAPoll. Readiness is any reported event, POLLNVAL included, not
    // WSAPoll's count, which leaves POLLNVAL out.
    bool poll_sockets_once(int timeout_ms) {
        for (auto& entry : sockets_) entry.revents = 0;
        const int count = ::WSAPoll(sockets_.data(), static_cast<ULONG>(sockets_.size()), timeout_ms);
        if (count == SOCKET_ERROR) {
            const int error = ::WSAGetLastError();
            for (auto& entry : sockets_) entry.revents = 0;
            if (error == WSAENOTSOCK) {
                find_invalid_sockets();
            } else if (error != WSAEINTR) {
                throw std::system_error(error, std::system_category(), "WSAPoll failed while waiting for host requests");
            }
        }
        for (std::size_t index = 0; index < sockets_.size(); ++index) socket_ready_[index] = sockets_[index].revents;
        return any_socket_ready();
    }

    // After WSAENOTSOCK: polls each socket alone, without waiting, to mark
    // the ones that are not open with POLLNVAL and report the others.
    void find_invalid_sockets() {
        for (auto& entry : sockets_) {
            WSAPOLLFD alone{entry.fd, entry.events, 0};
            if (::WSAPoll(&alone, 1, 0) != SOCKET_ERROR) {
                entry.revents = alone.revents;
                continue;
            }
            const int error = ::WSAGetLastError();
            if (error == WSAENOTSOCK) {
                entry.revents = POLLNVAL;
            } else if (error != WSAEINTR) {
                throw std::system_error(error, std::system_category(), "WSAPoll failed while waiting for host requests");
            }
        }
    }

    bool any_socket_ready() const {
        for (const short events : socket_ready_) {
            if (events != 0) return true;
        }
        return false;
    }

    // Waits for any handle, then sees which handles are signaled.
    bool wait_handles(int timeout_ms) {
        if (timeout_ms != 0) {
            const auto max_handles = static_cast<std::size_t>(MAXIMUM_WAIT_OBJECTS);
            const DWORD timeout = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);
            DWORD result = WAIT_FAILED;
            if (handles_.size() <= max_handles) {
                const auto count = static_cast<DWORD>(handles_.size());
                result = ::WaitForMultipleObjects(count, handles_.data(), FALSE, timeout);
                check_wait_result(result, count);
            } else {
                result = wait_fanned_in(timeout);
            }
            if (result == WAIT_TIMEOUT) return false;
        }
        bool any = false;
        for (std::size_t index = 0; index < handles_.size(); ++index) {
            const DWORD result = ::WaitForSingleObject(handles_[index], 0);
            check_wait_result(result, 1);
            handle_ready_[index] = result == WAIT_OBJECT_0;
            any = any || handle_ready_[index];
        }
        return any;
    }

    // WaitForMultipleObjects takes at most MAXIMUM_WAIT_OBJECTS handles: the
    // first ones minus one are waited for directly, and the rest through
    // thread-pool waits that signal one event, the last handle of the set.
    DWORD wait_fanned_in(DWORD timeout) {
        const std::size_t direct = MAXIMUM_WAIT_OBJECTS - 1;
        const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category(),
                                    "creating the event for host handles");
        }
        std::vector<HANDLE> registered;
        registered.reserve(handles_.size() - direct);
        const auto unregister = [&] {
            // Waits for running callbacks, so none touches the event after
            // it is closed.
            for (const HANDLE wait : registered) ::UnregisterWaitEx(wait, INVALID_HANDLE_VALUE);
            ::CloseHandle(event);
        };
        for (std::size_t index = direct; index < handles_.size(); ++index) {
            HANDLE wait = nullptr;
            if (!::RegisterWaitForSingleObject(&wait, handles_[index], wake_fan_in, event, INFINITE,
                                               WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD)) {
                const auto error = static_cast<int>(::GetLastError());
                unregister();
                throw std::system_error(error, std::system_category(), "waiting for host handles");
            }
            registered.push_back(wait);
        }
        std::vector<HANDLE> set(handles_.begin(), handles_.begin() + static_cast<std::ptrdiff_t>(direct));
        set.push_back(event);
        const auto count = static_cast<DWORD>(set.size());
        const DWORD result = ::WaitForMultipleObjects(count, set.data(), FALSE, timeout);
        const auto error = static_cast<int>(::GetLastError());
        unregister();
        if (result == WAIT_FAILED) throw std::system_error(error, std::system_category(), "waiting for host handles");
        check_wait_result(result, count);
        return result;
    }

    std::vector<Slot> slots_;
    std::map<SOCKET, std::size_t> socket_slots_;
    std::vector<WSAPOLLFD> sockets_;
    std::vector<short> socket_ready_;
    std::map<HANDLE, std::size_t> handle_slots_;
    std::vector<HANDLE> handles_;
    std::vector<bool> handle_ready_;
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

#ifdef NEURA_HAS_KQUEUE
// Register only this wait's descriptors. Readiness (including EOF) still
// goes to the caller in its original order, including duplicate interests.
bool wait_kqueue(std::vector<pollfd>& descriptors, int timeout_ms) {
    const int queue = ::kqueue();
    if (queue < 0) return false;
    ::fcntl(queue, F_SETFD, FD_CLOEXEC);
    std::vector<struct kevent> interests;
    for (const auto& item : descriptors) {
        struct kevent event;
        if ((item.events & POLLIN) != 0) {
            EV_SET(&event, item.fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
            interests.push_back(event);
        }
        if ((item.events & POLLOUT) != 0) {
            EV_SET(&event, item.fd, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
            interests.push_back(event);
        }
    }
    if (interests.empty()) {
        ::close(queue);
        return false;
    }
    std::vector<struct kevent> ready(interests.size());
    timespec timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1'000'000L};
    const int found = ::kevent(queue, interests.data(), static_cast<int>(interests.size()), ready.data(),
                               static_cast<int>(ready.size()), timeout_ms < 0 ? nullptr : &timeout);
    ::close(queue);
    if (found < 0) return false;
    for (int index = 0; index < found; ++index) {
        if ((ready[index].flags & EV_ERROR) != 0) return false;
    }
    for (int index = 0; index < found; ++index) {
        auto& event = ready[index];
        const short flag = event.filter == EVFILT_WRITE ? POLLOUT : POLLIN;
        for (auto& item : descriptors) {
            if (static_cast<std::uintptr_t>(item.fd) == event.ident && (item.events & flag) != 0) {
                item.revents |= flag;
            }
        }
    }
    return true;
}
#endif

class NativeWaits {
public:
    std::size_t add(PosixWait::Interest source) {
        const auto index = descriptors_.size();
        descriptors_.push_back({static_cast<int>(source.descriptor), poll_events(source.events), 0});
        return index;
    }

    std::size_t add(WindowsWait) {
        throw std::logic_error("a Windows handle wait needs Win32");
    }

    void wait(int timeout_ms, [[maybe_unused]] IoBackend backend) {
        if (descriptors_.empty()) return;
#ifdef __linux__
        if (backend == IoBackend::Epoll) {
            thread_local EpollSet set;
            if (set.usable() && set.wait(descriptors_, timeout_ms)) return;
        } else if (backend == IoBackend::IoUring) {
            thread_local UringSet ring;
            if (ring.usable() && ring.wait(descriptors_, timeout_ms)) return;
        }
#endif
#ifdef NEURA_HAS_KQUEUE
        if (backend == IoBackend::Kqueue && wait_kqueue(descriptors_, timeout_ms)) return;
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
#ifdef _WIN32
    return IoBackend::WaitForMultipleObjects;
#elif defined(__linux__)
    const char* text = std::getenv("NEURA_IO_BACKEND");
    if (text && std::string_view(text) == "epoll") return IoBackend::Epoll;
    if (text && std::string_view(text) == "io_uring") return IoBackend::IoUring;
#endif
    return IoBackend::Poll;
}

void PendingRequests::apply_service_selection(const HostRequest& request, const HostSignature& signature,
                                              const HostCompletion& completion) {
    if (!completion.success || signature.name != "neura.runtime.event_loop" || signature.version != 1) return;
    switch (request.arguments.at(0).bits) {
    case 1: backend_ = IoBackend::IoUring; break;
    case 2: backend_ = IoBackend::Epoll; break;
    case 3: backend_ = IoBackend::Kqueue; break;
    case 4:
#ifdef _WIN32
        backend_ = IoBackend::WaitForMultipleObjects;
#else
        backend_ = IoBackend::Poll;
#endif
        break;
    }
}

const char* io_backend_name(IoBackend backend) {
    switch (backend) {
    case IoBackend::Poll: return "poll";
    case IoBackend::Epoll: return "epoll";
    case IoBackend::IoUring: return "io_uring";
    case IoBackend::Kqueue: return "kqueue";
    // WSAPoll for sockets and WaitForMultipleObjects for handles (W14).
    case IoBackend::WaitForMultipleObjects: return "wsapoll+waitformultipleobjects";
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
    native.wait(timeout_ms, backend_);
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

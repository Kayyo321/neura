#include "client_socket.hpp"
#include "platform/socket.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neura::nvm::provider::client_socket {
std::uint32_t error_code(int error) {
#ifdef _WIN32
    return host_error::from_wsa(error);
#else
    return host_error::from_errno(error);
#endif
}

int wait(NativeSocket descriptor, short events, Clock::time_point deadline, std::uint32_t& error,
         const std::atomic<bool>* cancelled) {
    for (;;) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            return -1;
        }
        const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count();
        if (left <= 0) {
            error = host_error::timed_out;
            return 0;
        }
        const auto timeout = cancelled ? std::min(timeout_milliseconds(left), 50) : timeout_milliseconds(left);
        platform::PollItem item{descriptor, events, 0};
        const auto ready = platform::poll({&item, 1}, timeout);
        if (ready > 0) return ready;
        if (ready < 0 && !platform::last_socket_error().interrupted()) {
            error = error_code(platform::last_socket_error().code);
            return -1;
        }
    }
}

namespace {
bool prepare_socket(NativeSocket descriptor) {
    if (!platform::set_no_inherit(descriptor) || !platform::set_nonblocking(descriptor)) return false;
#ifdef SO_NOSIGPIPE
    int one = 1;
    return ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) == 0;
#else
    return true;
#endif
}
}

NativeSocket connect(const std::string& host, std::int32_t port, Clock::time_point deadline, std::uint32_t& error,
                     const std::atomic<bool>* cancelled) {
    if (const auto start = platform::ensure_sockets()) {
        error = error_code(start.value());
        return invalid_native_socket;
    }
    if (cancelled && cancelled->load()) {
        error = host_error::cancelled;
        return invalid_native_socket;
    }
    if (Clock::now() >= deadline) {
        error = host_error::timed_out;
        return invalid_native_socket;
    }
#ifdef NEURA_NUMERIC_ADDRESSES_ONLY
    // Match the fully static launcher's existing numeric/localhost contract.
    // This branch must not reference getaddrinfo or its libc NSS plugins.
    struct NumericAddress {
        addrinfo info{};
        sockaddr_storage storage{};
    };
    std::vector<std::unique_ptr<NumericAddress>> addresses;
    const std::vector<std::string> candidates =
        host == "localhost" ? std::vector<std::string>{"127.0.0.1", "::1"} : std::vector<std::string>{host};
    addrinfo* first = nullptr;
    addrinfo** tail = &first;
    for (const auto& candidate : candidates) {
        auto node = std::make_unique<NumericAddress>();
        auto* v4 = reinterpret_cast<sockaddr_in*>(&node->storage);
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&node->storage);
        if (::inet_pton(AF_INET, candidate.c_str(), &v4->sin_addr) == 1) {
            v4->sin_family = AF_INET;
            v4->sin_port = htons(static_cast<std::uint16_t>(port));
            node->info.ai_family = AF_INET;
            node->info.ai_addrlen = sizeof(sockaddr_in);
        } else if (::inet_pton(AF_INET6, candidate.c_str(), &v6->sin6_addr) == 1) {
            v6->sin6_family = AF_INET6;
            v6->sin6_port = htons(static_cast<std::uint16_t>(port));
            node->info.ai_family = AF_INET6;
            node->info.ai_addrlen = sizeof(sockaddr_in6);
        } else {
            error = host_error::unsupported;
            return invalid_native_socket;
        }
        node->info.ai_socktype = SOCK_STREAM;
        node->info.ai_addr = reinterpret_cast<sockaddr*>(&node->storage);
        *tail = &node->info;
        tail = &node->info.ai_next;
        addresses.push_back(std::move(node));
    }
    const auto* found = first;
#else
    struct Resolution {
        std::mutex mutex;
        std::condition_variable ready;
        bool done = false;
        int error = 0;
        addrinfo* addresses = nullptr;
        ~Resolution() {
            if (addresses) ::freeaddrinfo(addresses);
        }
    };
    // Heap storage avoids inherited locked mutexes after fork. A stuck system
    // resolver retains one worker, and later calls fail promptly instead of
    // accumulating threads. A child gets a fresh slot on its first call.
    struct ResolverSlot {
#ifdef _WIN32
        const std::uint64_t owner = GetCurrentProcessId();
#else
        const std::uint64_t owner = static_cast<std::uint64_t>(::getpid());
#endif
        std::atomic<bool> busy{false};
        std::mutex mutex;
        std::condition_variable available;
    };
    static std::atomic<ResolverSlot*> current{new ResolverSlot};
    auto* slot = current.load();
#ifndef _WIN32
    if (slot->owner != static_cast<std::uint64_t>(::getpid())) {
        auto replacement = std::make_unique<ResolverSlot>();
        if (current.compare_exchange_strong(slot, replacement.get())) slot = replacement.release();
        // The inherited slot belongs to a vanished parent worker. Do not
        // destroy any parent-owned synchronization state in the child.
    }
#endif
    // Allocate before claiming the process-wide slot: an allocation failure
    // must not leave every later resolver caller waiting on a vanished job.
    auto resolution = std::make_shared<Resolution>();
    const auto dns_deadline = std::min(deadline, Clock::now() + std::chrono::seconds(30));
    // Queue ordinary callers behind the one shared resolver job. Admission is
    // bounded by the calling provider (fetch has sixteen workers), and each
    // waiter keeps its own original deadline and cancellation state.
    std::unique_lock slot_lock(slot->mutex);
    for (;;) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            return invalid_native_socket;
        }
        if (Clock::now() >= dns_deadline) {
            error = host_error::timed_out;
            return invalid_native_socket;
        }
        bool idle = false;
        if (slot->busy.compare_exchange_strong(idle, true)) break;
        slot->available.wait_until(slot_lock, std::min(dns_deadline, Clock::now() + std::chrono::milliseconds(50)));
    }
    slot_lock.unlock();
    auto* acquired = slot;
    try {
        std::thread([resolution, acquired, host, service = std::to_string(port)] {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* found = nullptr;
            const auto result = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
            {
                std::lock_guard lock(resolution->mutex);
                resolution->addresses = found;
                resolution->error = result;
                resolution->done = true;
            }
            acquired->busy.store(false);
            acquired->available.notify_all();
            resolution->ready.notify_one();
        }).detach();
    } catch (...) {
        acquired->busy.store(false);
        acquired->available.notify_all();
        error = host_error::other;
        return invalid_native_socket;
    }
    std::unique_lock lock(resolution->mutex);
    while (!resolution->done) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            return invalid_native_socket;
        }
        if (Clock::now() >= dns_deadline) {
            error = host_error::timed_out;
            return invalid_native_socket;
        }
        resolution->ready.wait_until(lock, std::min(dns_deadline, Clock::now() + std::chrono::milliseconds(50)));
    }
    if (resolution->error != 0) {
        error = host_error::not_found;
        return invalid_native_socket;
    }
    const auto* found = resolution->addresses;
#endif
    error = host_error::connection_refused;
    for (const auto* entry = found; entry; entry = entry->ai_next) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            break;
        }
        if (Clock::now() >= deadline) {
            error = host_error::timed_out;
            break;
        }
        const auto descriptor = ::socket(entry->ai_family, SOCK_STREAM, 0);
        if (descriptor == invalid_native_socket) {
            error = error_code(platform::last_socket_error().code);
            continue;
        }
        if (prepare_socket(descriptor)) {
            if (::connect(descriptor, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) == 0) return descriptor;
            const auto problem = platform::last_socket_error();
            error = error_code(problem.code);
            if (problem.would_block()) {
                const int ready = wait(descriptor, platform::poll_out, deadline, error, cancelled);
                int pending_error = 0;
#ifdef _WIN32
                int length = sizeof pending_error;
                auto* result = reinterpret_cast<char*>(&pending_error);
#else
                socklen_t length = sizeof pending_error;
                auto* result = &pending_error;
#endif
                if (ready > 0 && ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, result, &length) == 0 &&
                    pending_error == 0)
                    return descriptor;
                if (ready > 0) error = error_code(pending_error ? pending_error : platform::last_socket_error().code);
            }
        } else {
            error = error_code(platform::last_socket_error().code);
        }
        platform::close_socket(descriptor);
        if (error == host_error::timed_out || error == host_error::cancelled) break;
    }
    return invalid_native_socket;
}

} // namespace neura::nvm::provider::client_socket

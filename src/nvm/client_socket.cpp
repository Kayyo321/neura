#include "client_socket.hpp"
#include "platform/socket.hpp"
#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
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

struct NumericAddress {
    addrinfo info{};
    sockaddr_storage storage{};
};

// An IPv6 zone: a decimal index, or on POSIX an interface name.
bool zone_index(const std::string& zone, std::uint32_t& index) {
    if (zone.empty()) return false;
    if (zone.find_first_not_of("0123456789") == std::string::npos) {
        if (zone.size() > 10) return false;
        const auto value = std::stoull(zone);
        if (value > 0xffffffffULL) return false;
        index = static_cast<std::uint32_t>(value);
        return true;
    }
#ifdef _WIN32
    return false;
#else
    index = ::if_nametoindex(zone.c_str());
    return index != 0;
#endif
}

// Numeric hosts are parsed here, never by getaddrinfo, so that no part of a
// caller's deadline is spent in the system resolver on them.
bool numeric_address(NumericAddress& address, const char* host, std::int32_t port) {
    auto* v4 = reinterpret_cast<sockaddr_in*>(&address.storage);
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&address.storage);
    const std::string text(host);
    const auto percent = text.find('%');
    if (percent != std::string::npos) {
        std::uint32_t scope = 0;
        if (::inet_pton(AF_INET6, text.substr(0, percent).c_str(), &v6->sin6_addr) != 1 ||
            !zone_index(text.substr(percent + 1), scope))
            return false;
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(static_cast<std::uint16_t>(port));
        v6->sin6_scope_id = scope;
        address.info.ai_family = AF_INET6;
        address.info.ai_addrlen = sizeof(sockaddr_in6);
    } else if (::inet_pton(AF_INET, host, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons(static_cast<std::uint16_t>(port));
        address.info.ai_family = AF_INET;
        address.info.ai_addrlen = sizeof(sockaddr_in);
    } else if (::inet_pton(AF_INET6, host, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(static_cast<std::uint16_t>(port));
        address.info.ai_family = AF_INET6;
        address.info.ai_addrlen = sizeof(sockaddr_in6);
    } else {
        return false;
    }
    address.info.ai_socktype = SOCK_STREAM;
    address.info.ai_addr = reinterpret_cast<sockaddr*>(&address.storage);
    return true;
}

#ifndef NEURA_NUMERIC_ADDRESSES_ONLY
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

struct ResolverPool {
    std::mutex mutex;
    std::condition_variable available;
    unsigned active = 0;

    void release() {
        {
            std::lock_guard lock(mutex);
            --active;
        }
        available.notify_one();
    }
};

constinit std::atomic<ResolverPool*> current_pool{nullptr};
#ifndef _WIN32
static_assert(decltype(current_pool)::is_always_lock_free);
void reset_resolver_in_child() {
    // Parent-owned synchronization state belongs to vanished threads.
    // A child allocates a fresh pool on its first named lookup.
    current_pool.store(nullptr, std::memory_order_relaxed);
}
const int resolver_atfork_status = ::pthread_atfork(nullptr, nullptr, reset_resolver_in_child);
#endif

ResolverPool* resolver_pool() {
#ifndef _WIN32
    if (resolver_atfork_status != 0) return nullptr;
#endif
    auto* pool = current_pool.load();
    if (pool) return pool;
    auto replacement = std::make_unique<ResolverPool>();
    if (current_pool.compare_exchange_strong(pool, replacement.get())) return replacement.release();
    return pool;
}

std::shared_ptr<Resolution> resolve(const std::string& host, std::int32_t port, Clock::time_point deadline,
                                    std::uint32_t& error, const std::atomic<bool>* cancelled) {
    constexpr unsigned max_jobs = 4;
    auto* pool = resolver_pool();
    if (!pool) {
        error = host_error::other;
        return {};
    }
    // Allocate before claiming a slot, and keep the result alive independently
    // from the caller when the platform resolver cannot be interrupted.
    auto resolution = std::make_shared<Resolution>();
    const auto dns_deadline = std::min(deadline, Clock::now() + std::chrono::seconds(30));
    // A saturated pool queues callers under their own deadline. Numeric
    // addresses and localhost bypass the pool entirely.
    std::unique_lock slot_lock(pool->mutex);
    for (;;) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            return {};
        }
        if (Clock::now() >= dns_deadline) {
            error = host_error::timed_out;
            return {};
        }
        if (pool->active < max_jobs) {
            ++pool->active;
            break;
        }
        pool->available.wait_until(slot_lock, std::min(dns_deadline, Clock::now() + std::chrono::milliseconds(50)));
    }
    slot_lock.unlock();
    try {
        std::thread([resolution, pool, host, service = std::to_string(port)] {
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
            pool->release();
            resolution->ready.notify_one();
        }).detach();
    } catch (...) {
        pool->release();
        error = host_error::other;
        return {};
    }
    std::unique_lock lock(resolution->mutex);
    while (!resolution->done) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            return {};
        }
        if (Clock::now() >= dns_deadline) {
            error = host_error::timed_out;
            return {};
        }
        resolution->ready.wait_until(lock, std::min(dns_deadline, Clock::now() + std::chrono::milliseconds(50)));
    }
    if (cancelled && cancelled->load()) {
        error = host_error::cancelled;
        return {};
    }
    if (Clock::now() >= dns_deadline) {
        error = host_error::timed_out;
        return {};
    }
    if (resolution->error != 0) {
        error = host_error::not_found;
        return {};
    }
    return resolution;
}
#endif
}

NativeSocket connect(const std::string& host, std::int32_t port, Clock::time_point deadline, std::uint32_t& error,
                     const std::atomic<bool>* cancelled, const AddressFilter& allowed_address) {
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
    NumericAddress ip, local_v6;
    const addrinfo* found = nullptr;
#ifndef NEURA_NUMERIC_ADDRESSES_ONLY
    std::shared_ptr<Resolution> resolution;
#endif
    if (host == "localhost") {
        numeric_address(ip, "127.0.0.1", port);
        numeric_address(local_v6, "::1", port);
        ip.info.ai_next = &local_v6.info;
        found = &ip.info;
    } else if (numeric_address(ip, host.c_str(), port)) {
        found = &ip.info;
    } else {
#ifdef NEURA_NUMERIC_ADDRESSES_ONLY
        // The fully static launcher must avoid libc NSS dependencies.
        error = host_error::unsupported;
        return invalid_native_socket;
#else
        resolution = resolve(host, port, deadline, error, cancelled);
        if (!resolution) return invalid_native_socket;
        found = resolution->addresses;
#endif
    }
    error = host_error::connection_refused;
    bool rejected_address = false, allowed_candidate = false;
    for (const auto* entry = found; entry; entry = entry->ai_next) {
        if (cancelled && cancelled->load()) {
            error = host_error::cancelled;
            break;
        }
        if (Clock::now() >= deadline) {
            error = host_error::timed_out;
            break;
        }
        if (allowed_address) {
            char numeric[INET6_ADDRSTRLEN];
            const void* address = nullptr;
            if (entry->ai_family == AF_INET && entry->ai_addrlen >= sizeof(sockaddr_in)) {
                address = &reinterpret_cast<const sockaddr_in*>(entry->ai_addr)->sin_addr;
            } else if (entry->ai_family == AF_INET6 && entry->ai_addrlen >= sizeof(sockaddr_in6)) {
                address = &reinterpret_cast<const sockaddr_in6*>(entry->ai_addr)->sin6_addr;
            }
            if (!address || !::inet_ntop(entry->ai_family, address, numeric, sizeof numeric) ||
                !allowed_address(numeric)) {
                rejected_address = true;
                continue;
            }
            allowed_candidate = true;
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
    if (rejected_address && !allowed_candidate && error == host_error::connection_refused)
        error = host_error::permission_denied;
    return invalid_native_socket;
}

} // namespace neura::nvm::provider::client_socket

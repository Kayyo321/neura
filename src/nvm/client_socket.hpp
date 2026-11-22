#pragma once

#include "provider_internal.hpp"
#include <atomic>
#include <chrono>

namespace neura::nvm::provider::client_socket {
using Clock = std::chrono::steady_clock;
std::uint32_t error_code(int error);
// Nonblocking connect and bounded shared DNS worker. Cancellation never transfers
// a socket to another owner: the calling worker closes it on its next poll.
NativeSocket connect(const std::string& host, std::int32_t port, Clock::time_point deadline,
                     std::uint32_t& error, const std::atomic<bool>* cancelled = nullptr);
int wait(NativeSocket descriptor, short events, Clock::time_point deadline,
         std::uint32_t& error, const std::atomic<bool>* cancelled = nullptr);
} // namespace neura::nvm::provider::client_socket

#pragma once

#include "provider_internal.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>

namespace neura::nvm::provider::client_socket {
using Clock = std::chrono::steady_clock;
using AddressFilter = std::function<bool(std::string_view)>;
std::uint32_t error_code(int error);
// Nonblocking connect and bounded shared DNS worker. Cancellation never transfers
// a socket to another owner: the calling worker closes it on its next poll.
// With allowed_address, each candidate address (numeric text) is checked before
// its connect attempt; when every candidate is refused the error is
// permission_denied.
NativeSocket connect(const std::string& host, std::int32_t port, Clock::time_point deadline,
                     std::uint32_t& error, const std::atomic<bool>* cancelled = nullptr,
                     const AddressFilter& allowed_address = {});
int wait(NativeSocket descriptor, short events, Clock::time_point deadline,
         std::uint32_t& error, const std::atomic<bool>* cancelled = nullptr);
} // namespace neura::nvm::provider::client_socket

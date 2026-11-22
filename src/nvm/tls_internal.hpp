#pragma once

#include <chrono>
#include <cstdint>

namespace neura::nvm::tls_internal {

// The default bound of a TLS transfer, and of the handshake of a connection
// that neura.net.tls.accept accepted with timeout zero.
inline constexpr std::int64_t default_transfer_timeout = 30'000'000'000LL;

// The deadline of the handshake on a connection neura.net.tls.accept has
// accepted (tls_provider.cpp). A positive `timeout` bounds the wait for a
// client and the handshake together, so the handshake keeps
// `accept_deadline`. Zero waits for a client without limit, but a connected
// peer that stalls the handshake is still bounded: by `bound` nanoseconds
// from `now`, saturating at the clock's end. Only a `bound` of zero or less
// leaves the handshake unbounded.
template <class TimePoint>
TimePoint accepted_handshake_deadline(std::int64_t timeout, TimePoint accept_deadline, TimePoint now,
                                      std::int64_t bound) {
    if (timeout != 0) return accept_deadline;
    const auto limit = TimePoint::max();
    const auto duration =
        std::chrono::duration_cast<typename TimePoint::duration>(std::chrono::nanoseconds(bound));
    return bound > 0 && duration < limit - now ? now + duration : limit;
}

// The `bound` the provider passes: default_transfer_timeout, unless a test
// shortens it to observe the bound without waiting 30 seconds. A value of
// zero or less restores the default.
std::int64_t accept_handshake_bound();
void set_accept_handshake_bound_for_testing(std::int64_t nanos);

} // namespace neura::nvm::tls_internal

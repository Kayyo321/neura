#pragma once

#include <algorithm>
#include <cstdint>

namespace neura::nvm::dtls_internal {

// The wait reported by neura.net.dtls.engine.exchange after OpenSSL asks to
// retry a write (tls_provider.cpp). `retries` counts the consecutive retry
// answers before this one in the same operation and `drained` says whether
// this call returned outbound packets. The first retry after draining output
// is immediate, since the drain freed BIO capacity. A repeated retry waits on
// the socket instead of spinning: 1 ms doubling to 64 ms, never past `limit`
// (the next protocol timer or the operation deadline), and never zero.
inline std::int64_t write_retry_wait(int retries, bool drained, std::int64_t limit) {
    if (retries == 0 && drained) return 0;
    const auto backoff = std::int64_t{1'000'000} << std::clamp(retries, 0, 6);
    return std::max<std::int64_t>(1, std::min(limit, backoff));
}

} // namespace neura::nvm::dtls_internal

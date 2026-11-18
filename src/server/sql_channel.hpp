#pragma once

// SQL calls over the request-worker channel (docs/design/Backend030.md,
// "Message bodies", Call operation 2). The worker forwards each neura.sql.*
// request to the host, which runs it on a connection of its pools and
// answers with a Return; the worker holds no connection of its own.
//
// Call body, operation 2: uint32 call identifier, uint32 operation (2),
// uint32 SQL operation (nvm::SqlOperation), uint32 argument count, and that
// many encoded values.
// Return body: uint32 call identifier, uint32 transport error (0), uint8
// success, one encoded value (the success payload or the uint32 error
// code), text detail.
// An encoded value is uint64 bits, a text of bytes, uint32 element count and
// that many encoded values, nested at most 8 deep. All integers are
// little-endian; a text is a uint32 byte count and the bytes.

#include "nvm/sql_services.hpp"
#include "server/worker_messages.hpp"

#include <optional>
#include <span>
#include <vector>

namespace neura::server::worker {

inline constexpr std::uint32_t sql_call_operation = 2;

struct SqlCall {
    std::uint32_t call = 0;
    nvm::SqlOperation operation = nvm::SqlOperation::Pool;
    std::vector<nvm::HostValue> arguments;
};

std::vector<std::uint8_t> encode_sql_call(const SqlCall& call);
// Nothing when the body is not an SQL call or is malformed.
std::optional<SqlCall> decode_sql_call(std::span<const std::uint8_t> body);

std::vector<std::uint8_t> encode_sql_return(std::uint32_t call, const nvm::SqlAnswer& answer);
std::optional<nvm::SqlAnswer> decode_sql_return(std::uint32_t call, std::span<const std::uint8_t> body);

// The worker's side: each call is one Call and its Return, within the
// request's deadline. A failed channel answers connection_reset (11), a
// call or answer above the channel's frame limit too_large (16).
class ChannelSql final : public nvm::SqlExchange {
public:
    ChannelSql(Channel& channel, Clock::time_point deadline) : channel_(channel), deadline_(deadline) {}
    nvm::SqlAnswer call(nvm::SqlOperation operation, const std::vector<nvm::HostValue>& arguments) override;

private:
    Channel& channel_;
    Clock::time_point deadline_;
    std::uint32_t calls_ = 0x10000; // Apart from the configuration calls' numbers.
};

} // namespace neura::server::worker

#include "server/request_worker.hpp"

#include "nvm/app_services.hpp"
#include "nvr/format.hpp"
#include "runner/runner.hpp"
#include "server/json_line.hpp"
#include "server/worker_messages.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace neura::server {
namespace {

using worker::Channel;
using worker::Clock;
using worker::FrameKind;

constexpr std::uint32_t config_call = 1;

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

// The worker's side of its one request: everything arrived before the image
// started, so reads never wait; responses are sent as they are produced.
class ChannelExchange final : public nvm::ServerExchange {
public:
    ChannelExchange(Channel& channel, nvm::ServerRequest request, std::string body, Clock::time_point deadline)
        : channel_(channel), request_(std::move(request)), body_(std::move(body)), deadline_(deadline) {}

    std::optional<nvm::ServerRequest> next() override { return request_; }

    std::string read_body(std::size_t max) override {
        const auto count = std::min(max, body_.size() - offset_);
        auto chunk = body_.substr(offset_, count);
        offset_ += count;
        return chunk;
    }

    std::uint32_t respond(std::uint32_t status, const nvm::HeaderPairs& headers, std::string_view body,
                          bool complete) override {
        const auto head = worker::encode_head({status, headers});
        if (channel_.send(FrameKind::Head, head, deadline_) != Channel::Status::Ok) return failure();
        if (const auto code = write_body(body); code != 0) return code;
        return complete ? close_body() : 0;
    }

    std::uint32_t write_body(std::string_view bytes) override {
        while (!bytes.empty()) {
            const auto size = std::min(bytes.size(), worker::max_body_chunk_bytes);
            const auto chunk = bytes_of(bytes.substr(0, size));
            if (channel_.send(FrameKind::BodyChunk, chunk, deadline_) != Channel::Status::Ok) return failure();
            bytes.remove_prefix(size);
        }
        return 0;
    }

    std::uint32_t close_body() override {
        if (channel_.send(FrameKind::BodyEnd, {}, deadline_) != Channel::Status::Ok ||
            channel_.send(FrameKind::Done, {}, deadline_) != Channel::Status::Ok) {
            return failure();
        }
        return 0;
    }

    // Call body: uint32 call identifier, uint32 operation (1, configuration),
    // text key. Return body: uint32 call identifier, uint32 error code,
    // uint8 found, text value.
    std::optional<std::string> config(std::string_view key) override {
        const auto call = ++calls_;
        std::vector<std::uint8_t> body;
        const auto put = [&](std::uint32_t value) {
            for (int i = 0; i < 4; ++i) body.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        };
        put(call);
        put(config_call);
        put(static_cast<std::uint32_t>(key.size()));
        body.insert(body.end(), key.begin(), key.end());
        if (channel_.send(FrameKind::Call, body, deadline_) != Channel::Status::Ok) return std::nullopt;
        for (;;) {
            auto frame = channel_.receive(deadline_);
            if (!frame) return std::nullopt;
            if (frame->kind == FrameKind::Cancel) continue;
            if (frame->kind != FrameKind::Return) return std::nullopt;
            const auto& in = frame->body;
            const auto get = [&](std::size_t at) {
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[at + i]) << (8 * i);
                return value;
            };
            if (in.size() < 13 || get(0) != call || get(4) != 0) return std::nullopt;
            const auto size = get(9);
            if (in.size() != 13 + std::size_t{size}) return std::nullopt;
            if (in[8] == 0) return std::nullopt;
            return std::string(reinterpret_cast<const char*>(in.data()) + 13, size);
        }
    }

    void log(std::uint32_t level, std::string_view message, const nvm::HeaderPairs& fields) override {
        std::cerr << json_log_line(level, request_.request_id, message, fields) << std::flush;
    }

private:
    std::uint32_t failure() const {
        return channel_.status() == Channel::Status::TimedOut ? nvm::host_error::timed_out
                                                              : nvm::host_error::connection_reset;
    }

    Channel& channel_;
    nvm::ServerRequest request_;
    std::string body_;
    std::size_t offset_ = 0;
    Clock::time_point deadline_;
    std::uint32_t calls_ = 0;
};

// The host's deadline, a Unix time, as a point on the steady clock.
Clock::time_point local_deadline(std::int64_t unix_nanos) {
    const auto now_unix = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto left = std::max<std::int64_t>(0, unix_nanos - now_unix);
    return Clock::now() + std::chrono::nanoseconds(left);
}

} // namespace

int run_request_worker(const WorkerOptions& options) {
    // Load and verify before greeting: a ready worker has a bound image.
    const auto bytes = runner::read_file(options.image);
    util::Sha256 digest;
    digest.update(std::span<const std::uint8_t>(bytes));
    const auto image_digest = digest.finish();
    const auto image = nvr::load(bytes);

    auto capabilities = options.capabilities;
    capabilities.http_server = true;
    capabilities.app = true;
    capabilities.clock = true;
    capabilities.entropy = true;
    capabilities.native = false;

    Channel channel(worker_channel_descriptor, worker::PeerRole::Worker, worker::server_identity(image_digest));
    worker::Hello hello;
    hello.role = worker::PeerRole::Worker;
    hello.identity = worker::server_identity(image_digest);
    // A ready worker waits as long as the host keeps it.
    const auto forever = Clock::time_point::max();
    if (channel.greet(hello, forever) != Channel::Status::Ok) return 70;

    auto assign = channel.receive(forever);
    if (!assign) return channel.status() == Channel::Status::Closed ? 0 : 70;
    auto request = worker::decode_assign(assign->body);
    if (!request) return 70;
    const auto deadline = local_deadline(request->deadline);
    std::string body;
    for (;;) {
        auto frame = channel.receive(deadline);
        if (!frame) return 70;
        if (frame->kind == FrameKind::BodyEnd) break;
        if (frame->kind == FrameKind::Cancel) return 70;
        body.append(frame->body.begin(), frame->body.end());
    }

    ChannelExchange exchange(channel, *request, std::move(body), deadline);
    runner::Host host(capabilities, &exchange, options.response_bytes);
    runner::RunOptions run_options;
    if (options.record_directory) {
        run_options.record = *options.record_directory / (request->request_id + ".trace");
    }
    const std::vector<std::string> arguments;
    return runner::run(image, host, arguments, run_options, options.image.string(), bytes.size());
}

} // namespace neura::server

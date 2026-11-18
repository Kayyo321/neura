#include "server/request_worker.hpp"

#include "jit/jit.hpp"
#include "nvm/app_services.hpp"
#include "nvr/format.hpp"
#include "runner/runner.hpp"
#include "server/json_line.hpp"
#include "server/sql_channel.hpp"
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
constexpr std::uint32_t metric_call = 3; // 2 is SQL (the sql-providers branch)

std::vector<std::uint8_t> bytes_of(std::string_view text) {
    return {text.begin(), text.end()};
}

// The worker's side of its one request. The request body arrives as
// BodyChunk frames while the image runs: reads wait for the next chunk, and
// chunks that arrive while the worker waits for a Return, or while a send
// waits for the host, are queued.
class ChannelExchange final : public nvm::ServerExchange {
public:
    ChannelExchange(Channel& channel, nvm::ServerRequest request, Clock::time_point deadline)
        : channel_(channel), request_(std::move(request)), deadline_(deadline) {}

    std::optional<nvm::ServerRequest> next() override { return request_; }

    std::uint32_t read_body(std::size_t max, std::string& out) override {
        while (pending_.size() == offset_ && !body_ended_) {
            if (!take_frame()) break;
        }
        if (pending_.size() == offset_ && body_broken_) return failure();
        const auto count = std::min(max, pending_.size() - offset_);
        out = pending_.substr(offset_, count);
        offset_ += count;
        if (offset_ == pending_.size()) {
            pending_.clear();
            offset_ = 0;
        }
        return 0;
    }

    std::uint32_t respond(std::uint32_t status, const nvm::HeaderPairs& headers, std::string_view body,
                          bool complete) override {
        worker::ResponseHead response{status, headers, complete ? static_cast<std::int64_t>(body.size()) : -1};
        const auto head = worker::encode_head(response);
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
        std::vector<std::uint8_t> body;
        put(body, static_cast<std::uint32_t>(key.size()));
        body.insert(body.end(), key.begin(), key.end());
        const auto in = call(config_call, body);
        if (!in || in->size() < 5) return std::nullopt;
        const auto size = get(*in, 1);
        if (in->size() != 5 + std::size_t{size} || (*in)[0] == 0) return std::nullopt;
        return std::string(reinterpret_cast<const char*>(in->data()) + 5, size);
    }

    // Call body: uint32 call identifier, uint32 operation (3, metric),
    // uint32 kind, int64 value, text name. Return body: uint32 call
    // identifier, uint32 error code.
    std::uint32_t metric(std::string_view name, std::uint32_t kind, std::int64_t value) override {
        std::vector<std::uint8_t> body;
        put(body, kind);
        for (int i = 0; i < 8; ++i) body.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * i)));
        put(body, static_cast<std::uint32_t>(name.size()));
        body.insert(body.end(), name.begin(), name.end());
        std::uint32_t code = 0;
        const auto in = call(metric_call, body, &code);
        if (!in) return code != 0 ? code : failure();
        return 0;
    }

    void log(std::uint32_t level, std::string_view message, const nvm::HeaderPairs& fields) override {
        std::cerr << json_log_line(level, request_.request_id, message, fields) << std::flush;
    }

    // An SQL call (operation 2): the Return body after its identifier and
    // transport error, or nothing with the code to report.
    std::optional<std::vector<std::uint8_t>> sql_call(const std::vector<std::uint8_t>& arguments,
                                                      std::uint32_t& code) {
        auto answer = call(worker::sql_call_operation, arguments, &code);
        if (!answer && code == 0) code = failure();
        return answer;
    }

    // Reads and discards what is left of the request body, so the host is
    // never blocked sending it to a worker that has finished.
    void drain() {
        while (!body_ended_ && take_frame()) {
            pending_.clear();
            offset_ = 0;
        }
    }

private:
    static void put(std::vector<std::uint8_t>& out, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    static std::uint32_t get(const std::vector<std::uint8_t>& in, std::size_t at) {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[at + i]) << (8 * i);
        return value;
    }

    // Sends a Call and waits for its Return; the result is the Return body
    // after its identifier and error code, or nothing (with the code, when
    // the host answered one).
    std::optional<std::vector<std::uint8_t>> call(std::uint32_t operation, const std::vector<std::uint8_t>& arguments,
                                                  std::uint32_t* error = nullptr) {
        const auto id = ++calls_;
        std::vector<std::uint8_t> body;
        put(body, id);
        put(body, operation);
        body.insert(body.end(), arguments.begin(), arguments.end());
        if (channel_.send(FrameKind::Call, body, deadline_) != Channel::Status::Ok) return std::nullopt;
        for (;;) {
            auto frame = channel_.receive(deadline_);
            if (!frame) return std::nullopt;
            if (frame->kind == FrameKind::Cancel) continue;
            if (frame->kind == FrameKind::BodyChunk || frame->kind == FrameKind::BodyEnd) {
                queue(*frame);
                continue;
            }
            if (frame->kind != FrameKind::Return) return std::nullopt;
            const auto& in = frame->body;
            if (in.size() < 8 || get(in, 0) != id) return std::nullopt;
            if (const auto code = get(in, 4); code != 0) {
                if (error) *error = code;
                return std::nullopt;
            }
            return std::vector<std::uint8_t>(in.begin() + 8, in.end());
        }
    }

    // Receives the next request frame into the queue; false at the end of
    // the channel or on a frame out of place.
    bool take_frame() {
        auto frame = channel_.receive(deadline_);
        if (!frame || (frame->kind != FrameKind::Cancel && frame->kind != FrameKind::BodyChunk &&
                       frame->kind != FrameKind::BodyEnd)) {
            body_ended_ = true;
            body_broken_ = true;
            return false;
        }
        if (frame->kind == FrameKind::Cancel) return true;
        queue(*frame);
        return true;
    }

    void queue(const worker::Frame& frame) {
        if (frame.kind == FrameKind::BodyEnd) {
            body_ended_ = true;
            return;
        }
        pending_.append(frame.body.begin(), frame.body.end());
    }

    std::uint32_t failure() const {
        switch (channel_.status()) {
        case Channel::Status::TimedOut: return nvm::host_error::timed_out;
        case Channel::Status::Closed: return nvm::host_error::unexpected_end;
        default: return nvm::host_error::connection_reset;
        }
    }

    Channel& channel_;
    nvm::ServerRequest request_;
    std::string pending_;
    std::size_t offset_ = 0;
    bool body_ended_ = false;
    bool body_broken_ = false;
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
    // The image runs at once; the request body follows as it reads it.
    channel.read_while_sending(true);
    ChannelExchange exchange(channel, *request, deadline);
    // SQL calls go to the host's pools (granted with --allow-sql), through
    // the exchange, which keeps the request body frames that arrive meanwhile.
    worker::ChannelSql sql([&exchange](const std::vector<std::uint8_t>& arguments, std::uint32_t& code) {
        return exchange.sql_call(arguments, code);
    });
    runner::Host host(capabilities, &exchange, options.response_bytes, &sql);
    runner::RunOptions run_options;
    if (options.max_steps) run_options.limits.max_steps = *options.max_steps;
    if (options.tier) run_options.limits.tier = *options.tier;
    if (options.jit && !jit::parse_mode(*options.jit, run_options.limits.jit)) return 70;
    if (options.record_directory) {
        run_options.record = *options.record_directory / (request->request_id + ".trace");
    }
    const std::vector<std::string> arguments;
    const int status = runner::run(image, host, arguments, run_options, options.image.string(), bytes.size());
    exchange.drain();
    return status;
}

} // namespace neura::server

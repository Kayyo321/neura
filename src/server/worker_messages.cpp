#include "server/worker_messages.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neura::server::worker {
namespace {

class Writer {
public:
    void u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void i64(std::int64_t value) {
        const auto bits = static_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    void text(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void pairs(const nvm::HeaderPairs& pairs) {
        u32(static_cast<std::uint32_t>(pairs.size()));
        for (const auto& [name, value] : pairs) {
            text(name);
            text(value);
        }
    }
    std::vector<std::uint8_t> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> input) : input_(input) {}
    bool u32(std::uint32_t& value) {
        if (input_.size() < 4) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(input_[i]) << (8 * i);
        input_ = input_.subspan(4);
        return true;
    }
    bool i64(std::int64_t& value) {
        if (input_.size() < 8) return false;
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) bits |= static_cast<std::uint64_t>(input_[i]) << (8 * i);
        value = static_cast<std::int64_t>(bits);
        input_ = input_.subspan(8);
        return true;
    }
    bool text(std::string& value) {
        std::uint32_t size = 0;
        if (!u32(size) || input_.size() < size) return false;
        value.assign(reinterpret_cast<const char*>(input_.data()), size);
        input_ = input_.subspan(size);
        return true;
    }
    bool pairs(nvm::HeaderPairs& pairs) {
        std::uint32_t count = 0;
        // Each pair takes at least eight bytes, which bounds the count.
        if (!u32(count) || count > input_.size() / 8) return false;
        pairs.resize(count);
        for (auto& [name, value] : pairs) {
            if (!text(name) || !text(value)) return false;
        }
        return true;
    }
    bool done() const { return input_.empty(); }

private:
    std::span<const std::uint8_t> input_;
};

} // namespace

std::vector<std::uint8_t> encode_assign(const nvm::ServerRequest& request) {
    Writer out;
    out.u32(request.kind);
    out.text(request.request_id);
    out.i64(request.deadline);
    out.text(request.peer);
    out.text(request.scheme);
    out.text(request.method);
    out.text(request.target);
    out.pairs(request.headers);
    out.i64(request.body_length);
    out.u32(0);
    return std::move(out.bytes);
}

std::optional<nvm::ServerRequest> decode_assign(std::span<const std::uint8_t> body) {
    Reader in(body);
    nvm::ServerRequest request;
    std::uint32_t leases = 0;
    if (!in.u32(request.kind) || !in.text(request.request_id) || !in.i64(request.deadline) ||
        !in.text(request.peer) || !in.text(request.scheme) || !in.text(request.method) ||
        !in.text(request.target) || !in.pairs(request.headers) || !in.i64(request.body_length) ||
        !in.u32(leases) || leases != 0 || !in.done()) {
        return std::nullopt;
    }
    if (request.kind != nvm::ServerRequest::request && request.kind != nvm::ServerRequest::startup_check) {
        return std::nullopt;
    }
    return request;
}

std::vector<std::uint8_t> encode_head(const ResponseHead& head) {
    Writer out;
    out.u32(head.status);
    out.pairs(head.headers);
    out.i64(head.body_length);
    return std::move(out.bytes);
}

std::optional<ResponseHead> decode_head(std::span<const std::uint8_t> body) {
    Reader in(body);
    ResponseHead head;
    if (!in.u32(head.status) || !in.pairs(head.headers) || !in.i64(head.body_length) || head.body_length < -1 ||
        !in.done()) {
        return std::nullopt;
    }
    return head;
}

WorkerIdentity server_identity(const ImageDigest& image) {
    WorkerIdentity identity;
    identity.image = image;
    identity.providers = {{"neura.app", 1}, {"neura.http.server", 1}, {"neura.sql", 1}};
    return identity;
}

namespace {

int wait_milliseconds(Clock::time_point deadline) {
    const auto left = deadline - Clock::now();
    if (left <= Clock::duration::zero()) return 0;
    const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(left).count();
    return milliseconds > INT_MAX ? INT_MAX : static_cast<int>(milliseconds);
}

// The socket calls of each host. POSIX passes MSG_DONTWAIT on every call
// as well (see the constructor). Windows has no MSG_DONTWAIT and needs none,
// since the constructor makes the socket non-blocking; Winsock reports
// errors through WSAGetLastError, and there is no SIGPIPE.
#ifdef _WIN32
int poll_one(pollfd& entry, int timeout) { return ::WSAPoll(&entry, 1, timeout); }
int send_now(platform::native_socket descriptor, const std::uint8_t* data, std::size_t size) {
    const auto count = static_cast<int>(std::min<std::size_t>(size, INT_MAX));
    return ::send(descriptor, reinterpret_cast<const char*>(data), count, 0);
}
int receive_now(platform::native_socket descriptor, std::uint8_t* data, std::size_t size) {
    const auto count = static_cast<int>(std::min<std::size_t>(size, INT_MAX));
    return ::recv(descriptor, reinterpret_cast<char*>(data), count, 0);
}
bool interrupted() { return platform::last_socket_error().interrupted(); }
bool would_block() { return platform::last_socket_error().would_block(); }
#else
int poll_one(pollfd& entry, int timeout) { return ::poll(&entry, 1, timeout); }
ssize_t send_now(int descriptor, const std::uint8_t* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    return ::send(descriptor, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
#else
    return ::send(descriptor, data, size, MSG_DONTWAIT);
#endif
}
ssize_t receive_now(int descriptor, std::uint8_t* data, std::size_t size) {
    return ::recv(descriptor, data, size, MSG_DONTWAIT);
}
bool interrupted() { return errno == EINTR; }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }
#endif

// Polls one entry until the deadline: 1 ready, 0 timed out, -1 failed.
int poll_until(pollfd& entry, Clock::time_point deadline) {
    for (;;) {
        entry.revents = 0;
        const int ready = poll_one(entry, wait_milliseconds(deadline));
        if (ready > 0) return (entry.revents & POLLNVAL) ? -1 : 1;
        if (ready == 0) return 0;
        if (!interrupted()) return -1;
    }
}

// Waits for `events` until the deadline: 1 ready, 0 timed out, -1 failed.
int wait_for(platform::native_socket descriptor, short events, Clock::time_point deadline) {
    for (;;) {
        pollfd entry{descriptor, events, 0};
        const int ready = poll_one(entry, wait_milliseconds(deadline));
        if (ready > 0) return (entry.revents & POLLNVAL) ? -1 : 1;
        if (ready == 0) return 0;
        if (!interrupted()) return -1;
    }
}

Direction outbound(PeerRole role) {
    return role == PeerRole::Host ? Direction::HostToWorker : Direction::WorkerToHost;
}

Direction inbound(PeerRole role) {
    return role == PeerRole::Host ? Direction::WorkerToHost : Direction::HostToWorker;
}

} // namespace

Channel::Channel(platform::native_socket descriptor, PeerRole role, WorkerIdentity expected)
    : descriptor_(descriptor), role_(role), sequence_(std::move(expected)), decoder_(inbound(role)) {
    // send(2) and recv(2) below must never block: a full socket waits in
    // poll, with the deadline, and the worker reads meanwhile. MSG_DONTWAIT
    // alone is not enough, because macOS honours it on recv(2) but not on
    // send(2), where a blocking AF_UNIX socket still sleeps until the peer
    // reads: a worker streaming its response before it reads the body and a
    // host sending that body then each sleep in send(2), past both deadlines.
#ifdef _WIN32
    // A worker's channel is the first socket it uses, so Winsock starts here.
    if (platform::ensure_sockets() || !platform::set_nonblocking(descriptor_)) status_ = Status::Failed;
#else
    if (const int flags = ::fcntl(descriptor_, F_GETFL); flags >= 0 && !(flags & O_NONBLOCK)) {
        (void)::fcntl(descriptor_, F_SETFL, flags | O_NONBLOCK);
    }
#endif
}

Channel::~Channel() {
#ifdef _WIN32
    if (descriptor_ != platform::invalid_socket) (void)platform::close_socket(descriptor_);
#else
    if (descriptor_ >= 0) ::close(descriptor_);
#endif
}

Channel::Status Channel::send(FrameKind kind, std::span<const std::uint8_t> body, Clock::time_point deadline) {
    if (status_ != Status::Ok) return status_;
    const FrameHeader header{kind, body.size()};
    if (sequence_.accept(outbound(role_), header, kind == FrameKind::Hello ? body : std::span<const std::uint8_t>{})
            .error != ProtocolError::None) {
        return status_ = Status::Failed;
    }
    const auto encoded = encode_header(outbound(role_), header);
    if (encoded.error != FrameError::None) return status_ = Status::Failed;
    std::vector<std::uint8_t> wire(encoded.bytes.begin(), encoded.bytes.end());
    wire.insert(wire.end(), body.begin(), body.end());
    std::size_t offset = 0;
    while (offset < wire.size()) {
        // Never block inside send(2): a full socket waits in poll below, with
        // the deadline, and the worker reads what the host sends meanwhile.
        const auto written = send_now(descriptor_, wire.data() + offset, wire.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && interrupted()) continue;
        if (written < 0 && would_block()) {
            if (read_while_sending_ && !peer_closed_) {
                pollfd entry{descriptor_, POLLOUT | POLLIN, 0};
                const int ready = poll_until(entry, deadline);
                if (ready == 0) return status_ = Status::TimedOut;
                if (ready < 0) return status_ = Status::Failed;
                if (entry.revents & POLLIN) {
                    std::uint8_t chunk[64 * 1024];
                    const auto got = receive_now(descriptor_, chunk, sizeof chunk);
                    if (got > 0) buffer_.insert(buffer_.end(), chunk, chunk + got);
                    else if (got == 0) peer_closed_ = true;
                }
                continue;
            }
            const int ready = wait_for(descriptor_, POLLOUT, deadline);
            if (ready == 0) return status_ = Status::TimedOut;
            if (ready < 0) return status_ = Status::Failed;
            continue;
        }
        return status_ = Status::Failed;
    }
    return Status::Ok;
}

Channel::Status Channel::fill(Clock::time_point deadline, bool wait) {
    if (offset_ == buffer_.size()) {
        buffer_.clear();
        offset_ = 0;
    }
    if (peer_closed_) return Status::Closed;
    std::uint8_t chunk[64 * 1024];
    for (;;) {
        const auto got = receive_now(descriptor_, chunk, sizeof chunk);
        if (got > 0) {
            buffer_.insert(buffer_.end(), chunk, chunk + got);
            return Status::Ok;
        }
        if (got == 0) return Status::Closed;
        if (interrupted()) continue;
        if (!would_block()) return Status::Failed;
        if (!wait) return Status::TimedOut;
        const int ready = wait_for(descriptor_, POLLIN, deadline);
        if (ready == 0) return Status::TimedOut;
        if (ready < 0) return Status::Failed;
    }
}

std::optional<Frame> Channel::receive(Clock::time_point deadline) { return receive(deadline, true); }

std::optional<Frame> Channel::receive_ready() { return receive(Clock::now(), false); }

std::optional<Frame> Channel::receive(Clock::time_point deadline, bool wait) {
    if (status_ != Status::Ok) return std::nullopt;
    // A frame partly decoded when a nonblocking receive stopped is kept in
    // `partial_` until the rest arrives.
    Frame frame = std::move(partial_);
    partial_ = Frame{FrameKind::Hello, {}};
    for (;;) {
        const std::span<const std::uint8_t> unread(buffer_.data() + offset_, buffer_.size() - offset_);
        const auto result = decoder_.consume(unread);
        offset_ += result.consumed;
        switch (result.event) {
        case FrameEvent::HeaderReady:
            frame.kind = decoder_.header().kind;
            frame.body.reserve(decoder_.header().body_bytes);
            continue;
        case FrameEvent::BodyData:
            frame.body.insert(frame.body.end(), result.body_data.begin(), result.body_data.end());
            continue;
        case FrameEvent::Complete: {
            const auto header = decoder_.header();
            decoder_ = FrameDecoder(inbound(role_));
            if (sequence_.accept(inbound(role_), header,
                                 header.kind == FrameKind::Hello ? std::span<const std::uint8_t>(frame.body)
                                                                 : std::span<const std::uint8_t>{})
                    .error != ProtocolError::None) {
                status_ = Status::Failed;
                return std::nullopt;
            }
            return frame;
        }
        case FrameEvent::Rejected:
            sequence_.reject_frame(result.error);
            status_ = Status::Failed;
            return std::nullopt;
        case FrameEvent::Closed:
            status_ = Status::Closed;
            return std::nullopt;
        case FrameEvent::NeedInput: {
            const auto filled = fill(deadline, wait);
            if (!wait && filled == Status::TimedOut) {
                partial_ = std::move(frame);
                return std::nullopt;
            }
            if (filled == Status::Closed) {
                const auto end = decoder_.finish_input();
                if (end.event == FrameEvent::Rejected) {
                    sequence_.reject_frame(end.error);
                    status_ = Status::Failed;
                } else {
                    status_ = Status::Closed;
                }
                return std::nullopt;
            }
            if (filled != Status::Ok) {
                status_ = filled;
                return std::nullopt;
            }
            continue;
        }
        }
    }
}

Channel::Status Channel::greet(const Hello& hello, Clock::time_point deadline) {
    const auto encoded = encode_hello(hello);
    if (encoded.error != ProtocolError::None) return status_ = Status::Failed;
    if (send(FrameKind::Hello, encoded.bytes, deadline) != Status::Ok) return status_;
    const auto frame = receive(deadline);
    if (!frame) return status_;
    if (frame->kind != FrameKind::Hello) return status_ = Status::Failed;
    return Status::Ok;
}

} // namespace neura::server::worker

// HTTP/2 for the server host (docs/design/Backend030.md, "HTTP/2 and
// decision 9"), framed by the nghttp2 library the server component lock pins.
//
// One connection thread owns the nghttp2 session and the transport; no other
// thread calls nghttp2. Each request stream is served by the same Responder
// the HTTP/1.1 loop calls, on a thread of its own. The two sides share the
// stream state below under one mutex: the connection thread wakes a responder
// through its condition variable (and, while the responder also waits on a
// descriptor, a pipe), and a responder wakes the connection thread through
// the connection's pipe, which the connection's read also waits on.

#include "server/http2_connection.hpp"
#include "server/http_socket.hpp"

#include <nghttp2/nghttp2.h>

#include <algorithm>
#include <array>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace neura::server::http {
namespace {

using Clock = ConnectionClock;
using Deadline = Clock::time_point;
using namespace std::chrono_literals;

// How long the connection thread waits for input before it looks at the
// drain flag and the idle deadline again.
constexpr auto tick = 250ms;
// Frames are gathered up to this size before one write to the transport.
constexpr std::size_t send_batch_bytes = 64 * 1024;
// A response head with a known body of at most this size waits for its body,
// so a small buffered response leaves in one write (as in HTTP/1.1).
constexpr std::uint64_t coalesce_bytes = 64 * 1024;
// The HPACK dynamic table, in both directions (SETTINGS_HEADER_TABLE_SIZE).
constexpr std::uint32_t header_table_bytes = 4096;
// The initial stream window the host advertises (the protocol default).
constexpr std::int64_t stream_window = 65535;

// A non-blocking pipe whose read end a poll can wait on.
class WakePipe {
public:
    WakePipe() {
        int ends[2];
#ifdef __linux__
        if (::pipe2(ends, O_NONBLOCK | O_CLOEXEC) != 0) return;
#else
        if (::pipe(ends) != 0) return;
        for (const int end : ends) {
            ::fcntl(end, F_SETFL, ::fcntl(end, F_GETFL) | O_NONBLOCK);
            ::fcntl(end, F_SETFD, FD_CLOEXEC);
        }
#endif
        read_ = ends[0];
        write_ = ends[1];
    }
    WakePipe(const WakePipe&) = delete;
    WakePipe& operator=(const WakePipe&) = delete;
    ~WakePipe() {
        if (read_ >= 0) ::close(read_);
        if (write_ >= 0) ::close(write_);
    }

    bool valid() const { return read_ >= 0; }
    int descriptor() const { return read_; }
    // A full pipe is already readable, so a failed write loses nothing.
    void signal() const {
        const char byte = 1;
        if (write_ >= 0) (void)!::write(write_, &byte, 1);
    }
    void drain() const {
        std::array<char, 64> bytes;
        while (read_ >= 0 && ::read(read_, bytes.data(), bytes.size()) > 0) {}
    }

private:
    int read_ = -1;
    int write_ = -1;
};

char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool token_char(char c) {
    constexpr std::string_view extra = "!#$%&'*+-.^_`|~";
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           extra.find(c) != std::string_view::npos;
}

// The rules ConnectionWriter applies to response fields.
bool valid_fields(const std::vector<Header>& headers) {
    for (const auto& header : headers) {
        if (header.name.empty()) return false;
        for (const char c : header.name) if (!token_char(c)) return false;
        for (const char c : header.value) {
            const auto byte = static_cast<unsigned char>(c);
            if ((byte < 0x20 && byte != '\t') || byte == 0x7f) return false;
        }
    }
    return true;
}

// Fields HTTP/2 forbids in a message (RFC 9113, 8.2.2) and the length the
// connection writes itself. The names are lowercase.
bool connection_field(std::string_view name) {
    return name == "content-length" || name == "transfer-encoding" || name == "connection" ||
           name == "keep-alive" || name == "proxy-connection" || name == "upgrade" || name == "te";
}

// One request stream. `head`, `authority`, `cookies` and the sizes are
// written by the connection thread before the responder starts and only read
// afterwards; the fields after them are shared under Connection::mutex_.
struct Stream {
    explicit Stream(std::int32_t number) : id(number) {}

    const std::int32_t id;
    RequestHead head;
    std::string authority;
    std::vector<std::string> cookies;
    std::size_t header_list_bytes = 0;
    bool too_large = false;
    Deadline deadline{};
    bool responder = false; // A responder thread serves it (not answered by the connection).

    // The request body.
    std::string inbound;          // Received and not yet read by the responder.
    std::uint64_t received = 0;   // Body bytes received in all.
    bool inbound_end = false;     // END_STREAM arrived.
    bool body_rejected = false;   // The body passed S-6.
    std::size_t to_consume = 0;   // Read by the responder, not yet reported to nghttp2.

    // The stream's state in the session.
    bool reset = false;      // Closed before its response ended, or the connection ended.
    bool closed = false;     // nghttp2 has closed it.
    bool end_sent = false;   // A frame with END_STREAM was serialized.
    bool end_flushed = false; // ... and written to the transport.

    // The response.
    bool head_ready = false;
    bool head_submitted = false;
    bool body_follows = false;
    int status = 0;
    std::vector<Header> fields;
    std::string outbound;          // Written by the responder, not yet framed.
    std::uint64_t appended = 0;    // Bytes the responder wrote.
    std::uint64_t taken = 0;       // Bytes framed into DATA.
    std::uint64_t flushed = 0;     // Bytes written to the transport.
    bool outbound_end = false;
    bool deferred = false;         // The data source answered DEFERRED.
    bool abort = false;            // A started response did not finish: RST_STREAM.
    bool rst_submitted = false;
    std::function<void()> cancel;

    std::condition_variable changed;
    std::optional<WakePipe> pipe; // Created when the responder waits on a descriptor too.

    std::thread thread;
    bool responder_done = false;
};

class Connection {
public:
    Connection(ConnectionStream& io, const Responder& respond, const ConnectionLimits& limits)
        : io_(io), respond_(respond), limits_(limits) {
        nghttp2_session_callbacks* callbacks = nullptr;
        nghttp2_option* option = nullptr;
        if (nghttp2_session_callbacks_new(&callbacks) != 0) return;
        if (nghttp2_option_new(&option) != 0) {
            nghttp2_session_callbacks_del(callbacks);
            return;
        }
        nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers);
        nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv);
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
        nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, on_frame_send);
        // The responder's reads open the window (nghttp2_session_consume),
        // so a stream buffers at most its window, not S-6.
        nghttp2_option_set_no_auto_window_update(option, 1);
        nghttp2_option_set_max_deflate_dynamic_table_size(option, header_table_bytes);
        if (nghttp2_session_server_new2(&session_, callbacks, this, option) != 0) session_ = nullptr;
        nghttp2_option_del(option);
        nghttp2_session_callbacks_del(callbacks);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection() {
        if (session_) nghttp2_session_del(session_);
    }

    ConnectionEnd run(std::string_view received) {
        if (!session_ || !wake_.valid()) return ConnectionEnd::StreamFailure;
        const std::uint32_t streams = std::max<std::uint32_t>(1, limits_.http2.max_concurrent_streams);
        const nghttp2_settings_entry settings[] = {
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, streams},
            {NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, header_table_bytes},
            {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, static_cast<std::uint32_t>(limits_.request.head_bytes)},
        };
        if (nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, settings, std::size(settings)) != 0) {
            return ConnectionEnd::StreamFailure;
        }
        // The connection window covers every stream's window, so one stream
        // whose responder reads slowly does not stall the others' uploads.
        const auto window = std::min<std::int64_t>(INT32_MAX, stream_window * streams);
        (void)nghttp2_session_set_local_window_size(session_, NGHTTP2_FLAG_NONE, 0, static_cast<std::int32_t>(window));

        ConnectionEnd end = ConnectionEnd::PeerClosed;
        if (!received.empty() && !receive(received, end)) {
            (void)flush();
            shutdown();
            return end;
        }
        std::array<char, 16 * 1024> buffer;
        auto idle_since = Clock::now();
        for (;;) {
            process();
            if (!flush()) {
                end = ConnectionEnd::StreamFailure;
                break;
            }
            after_flush();
            if (limits_.draining && limits_.draining->load() && !goaway_sent_) {
                // Streams up to the last one received still finish.
                (void)nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE, nghttp2_session_get_last_proc_stream_id(session_),
                                            NGHTTP2_NO_ERROR, nullptr, 0);
                goaway_sent_ = true;
                continue;
            }
            const bool active = !streams_.empty();
            if (!active && !nghttp2_session_want_read(session_) && !nghttp2_session_want_write(session_)) {
                end = goaway_sent_ ? ConnectionEnd::ResponseClosed : ConnectionEnd::PeerClosed;
                break;
            }
            const auto now = Clock::now();
            if (active) {
                idle_since = now;
            } else if (now - idle_since >= limits_.head_timeout) {
                // Idle for the HTTP/1.1 keep-alive time: GOAWAY and close.
                if (!goaway_sent_) {
                    (void)nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE,
                                                nghttp2_session_get_last_proc_stream_id(session_), NGHTTP2_NO_ERROR,
                                                nullptr, 0);
                    goaway_sent_ = true;
                }
                (void)flush();
                end = ConnectionEnd::HeadTimeout;
                break;
            }
            auto deadline = now + tick;
            if (!active) deadline = std::min(deadline, idle_since + limits_.head_timeout);
            const auto input = io_.read(buffer, deadline, wake_.descriptor());
            if (input.status == StreamStatus::Ready) {
                if (input.size == 0 || input.size > buffer.size()) {
                    end = ConnectionEnd::StreamFailure;
                    break;
                }
                if (!receive({buffer.data(), input.size}, end)) {
                    (void)flush();
                    break;
                }
            } else if (input.status == StreamStatus::Woken) {
                wake_.drain();
            } else if (input.status == StreamStatus::End) {
                end = ConnectionEnd::PeerClosed;
                break;
            } else if (input.status == StreamStatus::Failed) {
                end = ConnectionEnd::StreamFailure;
                break;
            }
        }
        shutdown();
        return end;
    }

private:
    friend class StreamBody;
    friend class StreamWriter;

    static Connection& of(void* user) { return *static_cast<Connection*>(user); }

    Stream* find(std::int32_t id) {
        const auto found = streams_.find(id);
        return found == streams_.end() ? nullptr : found->second.get();
    }

    // Under mutex_.
    static void notify(Stream& stream) {
        stream.changed.notify_all();
        if (stream.pipe) stream.pipe->signal();
    }

    // Under mutex_: tells the work behind a stream that the client gave it up.
    static void give_up(Stream& stream) {
        if (stream.reset) return;
        stream.reset = true;
        if (stream.cancel) stream.cancel();
        notify(stream);
    }

    bool receive(std::string_view bytes, ConnectionEnd& end) {
        const auto result = nghttp2_session_mem_recv2(session_, reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                                      bytes.size());
        if (result >= 0) return true;
        end = result == NGHTTP2_ERR_BAD_CLIENT_MAGIC ? ConnectionEnd::RejectedRequest : ConnectionEnd::StreamFailure;
        return false;
    }

    // Serializes what the session has to send and writes it. A write that
    // the client does not take within S-8 fails the connection.
    bool flush() {
        std::string batch;
        for (;;) {
            const std::uint8_t* data = nullptr;
            const auto size = nghttp2_session_mem_send2(session_, &data);
            if (size < 0) return false;
            if (size == 0) break;
            batch.append(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
            if (batch.size() >= send_batch_bytes) {
                if (io_.write_all(batch, Clock::now() + limits_.request_timeout) != StreamStatus::Ready) return false;
                batch.clear();
            }
        }
        return batch.empty() || io_.write_all(batch, Clock::now() + limits_.request_timeout) == StreamStatus::Ready;
    }

    // Applies what the responders asked for since the last turn.
    void process() {
        struct Submit {
            std::int32_t id;
            int status;
            std::vector<Header> fields;
            Stream* stream;
        };
        std::vector<std::pair<std::int32_t, std::size_t>> consume;
        std::vector<Submit> submits;
        std::vector<std::int32_t> resumes;
        std::vector<std::int32_t> aborts;
        {
            std::lock_guard lock(mutex_);
            consume.swap(consume_);
            for (auto& [id, pointer] : streams_) {
                auto& stream = *pointer;
                if (stream.to_consume != 0) {
                    consume.emplace_back(id, stream.to_consume);
                    stream.to_consume = 0;
                }
                if (stream.closed) continue;
                if (stream.head_ready && !stream.head_submitted) {
                    stream.head_submitted = true;
                    submits.push_back({id, stream.status, std::move(stream.fields), stream.body_follows ? &stream : nullptr});
                } else if (stream.head_submitted && stream.deferred && (!stream.outbound.empty() || stream.outbound_end)) {
                    stream.deferred = false;
                    resumes.push_back(id);
                }
                if (stream.abort && !stream.rst_submitted) {
                    stream.rst_submitted = true;
                    aborts.push_back(id);
                }
            }
        }
        for (const auto& [id, size] : consume) (void)nghttp2_session_consume(session_, id, size);
        for (auto& submit : submits) {
            const auto status = std::to_string(submit.status);
            std::vector<nghttp2_nv> fields;
            fields.reserve(submit.fields.size() + 1);
            const auto field = [](std::string_view name, std::string_view value) {
                return nghttp2_nv{reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data())),
                                  reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())), name.size(),
                                  value.size(), NGHTTP2_NV_FLAG_NONE};
            };
            fields.push_back(field(":status", status));
            for (const auto& header : submit.fields) fields.push_back(field(header.name, header.value));
            nghttp2_data_provider2 provider{};
            provider.source.ptr = submit.stream;
            provider.read_callback = read_body;
            (void)nghttp2_submit_response2(session_, submit.id, fields.data(), fields.size(),
                                           submit.stream ? &provider : nullptr);
        }
        for (const auto id : resumes) (void)nghttp2_session_resume_data(session_, id);
        for (const auto id : aborts) (void)nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, id, NGHTTP2_INTERNAL_ERROR);
    }

    // After a flush: the bytes framed so far are on the transport. Streams
    // whose responder has returned and which the session has closed end
    // here, and their responses count as written.
    void after_flush() {
        std::vector<std::shared_ptr<Stream>> finished;
        {
            std::lock_guard lock(mutex_);
            for (auto entry = streams_.begin(); entry != streams_.end();) {
                auto& stream = *entry->second;
                if (stream.flushed != stream.taken || (stream.end_sent && !stream.end_flushed)) {
                    stream.flushed = stream.taken;
                    stream.end_flushed = stream.end_sent;
                    notify(stream);
                }
                if (stream.responder && stream.responder_done && stream.closed) {
                    finished.push_back(std::move(entry->second));
                    entry = streams_.erase(entry);
                } else {
                    ++entry;
                }
            }
        }
        for (auto& stream : finished) {
            if (stream->thread.joinable()) stream->thread.join();
            if (limits_.response_finished) limits_.response_finished();
        }
    }

    // The connection ends: every stream still served is given up, and every
    // responder has returned before this does.
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            for (auto& [id, stream] : streams_) give_up(*stream);
        }
        for (auto& [id, stream] : streams_) {
            if (stream->thread.joinable()) stream->thread.join();
            if (stream->responder && limits_.response_finished) limits_.response_finished();
        }
        streams_.clear();
    }

    // A response the connection gives itself, without a responder.
    void answer(std::int32_t id, int status) {
        auto text = std::to_string(status);
        nghttp2_nv field{reinterpret_cast<std::uint8_t*>(const_cast<char*>(":status")),
                         reinterpret_cast<std::uint8_t*>(text.data()), 7, text.size(), NGHTTP2_NV_FLAG_NONE};
        (void)nghttp2_submit_response2(session_, id, &field, 1, nullptr);
    }

    // The end of a request's head: checked as HTTP/1.1 checks it, then given
    // to a responder thread, answered, or refused.
    void begin(Stream& stream, bool end_stream) {
        const auto id = stream.id;
        if (stream.too_large) return answer(id, 431);
        auto& head = stream.head;
        // Cookie crumbs (RFC 9113, 8.2.3) become the one field HTTP/1.1 sends.
        if (!stream.cookies.empty()) {
            std::string joined;
            for (const auto& cookie : stream.cookies) {
                if (!joined.empty()) joined += "; ";
                joined += cookie;
            }
            head.headers.push_back({"cookie", std::move(joined)});
        }
        // :authority stands for Host; both must agree.
        const auto host = std::ranges::find_if(head.headers, [](const Header& field) { return field.name == "host"; });
        if (!stream.authority.empty()) {
            if (host == head.headers.end()) head.headers.insert(head.headers.begin(), {"host", stream.authority});
            else if (host->value != stream.authority) return answer(id, 400);
        }
        if (const int status = check_request_head(head, limits_.request); status != 0) return answer(id, status);
        if (head.framing != BodyFraming::ContentLength) head.framing = end_stream ? BodyFraming::None : BodyFraming::Chunked;
        const auto shared = streams_.at(id);
        {
            std::lock_guard lock(mutex_);
            // Responders that still run count against the limit, so streams
            // reset at once cannot pile up threads (rapid reset).
            if (running_ >= std::max<std::uint32_t>(1, limits_.http2.max_concurrent_streams)) {
                (void)nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, id, NGHTTP2_REFUSED_STREAM);
                return;
            }
            ++running_;
            stream.responder = true;
            stream.inbound_end = end_stream;
            stream.deadline = Clock::now() + limits_.request_timeout;
        }
        if (head.expect_continue && !end_stream) {
            nghttp2_nv field{reinterpret_cast<std::uint8_t*>(const_cast<char*>(":status")),
                             reinterpret_cast<std::uint8_t*>(const_cast<char*>("100")), 7, 3, NGHTTP2_NV_FLAG_NONE};
            (void)nghttp2_submit_headers(session_, NGHTTP2_FLAG_NONE, id, nullptr, &field, 1, nullptr);
        }
        try {
            stream.thread = std::thread([this, shared] { serve(*shared); });
        } catch (const std::system_error&) {
            std::lock_guard lock(mutex_);
            --running_;
            stream.responder = false;
            (void)nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, id, NGHTTP2_REFUSED_STREAM);
        }
    }

    void serve(Stream& stream);

    // nghttp2 callbacks, all on the connection thread.

    static int on_begin_headers(nghttp2_session*, const nghttp2_frame* frame, void* user) {
        if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
        auto& connection = of(user);
        connection.streams_[frame->hd.stream_id] = std::make_shared<Stream>(frame->hd.stream_id);
        return 0;
    }

    static int on_header(nghttp2_session*, const nghttp2_frame* frame, const std::uint8_t* name_bytes,
                         std::size_t name_size, const std::uint8_t* value_bytes, std::size_t value_size, std::uint8_t,
                         void* user) {
        // Trailer fields are discarded, as the HTTP/1.1 decoder discards them.
        if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
        auto* stream = of(user).find(frame->hd.stream_id);
        if (!stream || stream->too_large) return 0;
        // S-5 measures the decoded list as SETTINGS_MAX_HEADER_LIST_SIZE does
        // (RFC 9113, 6.5.2): name, value and 32 bytes per field.
        stream->header_list_bytes += name_size + value_size + 32;
        if (stream->header_list_bytes > of(user).limits_.request.head_bytes) {
            stream->too_large = true;
            return 0;
        }
        const std::string_view name(reinterpret_cast<const char*>(name_bytes), name_size);
        const std::string_view value(reinterpret_cast<const char*>(value_bytes), value_size);
        auto& head = stream->head;
        if (name == ":method") head.method = value;
        else if (name == ":path") head.target = value;
        else if (name == ":authority") stream->authority = value;
        else if (name.starts_with(':')) {} // :scheme; nghttp2 has checked the pseudo-header fields.
        else if (name == "cookie") stream->cookies.emplace_back(value);
        else head.headers.push_back({std::string(name), std::string(value)});
        return 0;
    }

    static int on_frame_recv(nghttp2_session*, const nghttp2_frame* frame, void* user) {
        auto& connection = of(user);
        if (frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA) return 0;
        auto* stream = connection.find(frame->hd.stream_id);
        if (!stream) return 0;
        const bool end_stream = (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0;
        if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
            connection.begin(*stream, end_stream);
            return 0;
        }
        if (end_stream) {
            std::lock_guard lock(connection.mutex_);
            stream->inbound_end = true;
            notify(*stream);
        }
        return 0;
    }

    static int on_data_chunk_recv(nghttp2_session*, std::uint8_t, std::int32_t id, const std::uint8_t* data,
                                  std::size_t size, void* user) {
        auto& connection = of(user);
        auto* stream = connection.find(id);
        std::lock_guard lock(connection.mutex_);
        if (!stream || !stream->responder || stream->responder_done || stream->reset || stream->body_rejected) {
            // Nobody reads it: it is consumed at once.
            connection.consume_.emplace_back(id, size);
            return 0;
        }
        stream->received += size;
        if (stream->received > connection.limits_.request.body_bytes) {
            // S-6: the responder's next read answers Rejected (413).
            stream->body_rejected = true;
            connection.consume_.emplace_back(id, size + stream->inbound.size());
            stream->inbound.clear();
        } else {
            stream->inbound.append(reinterpret_cast<const char*>(data), size);
        }
        notify(*stream);
        return 0;
    }

    static int on_stream_close(nghttp2_session*, std::int32_t id, std::uint32_t, void* user) {
        auto& connection = of(user);
        const auto found = connection.streams_.find(id);
        if (found == connection.streams_.end()) return 0;
        auto& stream = *found->second;
        {
            std::lock_guard lock(connection.mutex_);
            stream.closed = true;
            // Closed before its response ended: the client reset it.
            if (!stream.end_sent) {
                give_up(stream);
                if (!stream.inbound.empty()) {
                    connection.consume_.emplace_back(id, stream.inbound.size());
                    stream.inbound.clear();
                }
            }
            notify(stream);
        }
        if (!stream.responder) connection.streams_.erase(found);
        return 0;
    }

    static int on_frame_send(nghttp2_session* session, const nghttp2_frame* frame, void* user) {
        if ((frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA) ||
            !(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
            return 0;
        }
        auto& connection = of(user);
        const auto id = frame->hd.stream_id;
        if (auto* stream = connection.find(id)) {
            std::lock_guard lock(connection.mutex_);
            stream->end_sent = true;
        }
        // A complete response before the whole request: the client is asked
        // to stop sending (RFC 9113, 8.1), and the rest is discarded.
        if (nghttp2_session_get_stream_remote_close(session, id) == 0) {
            (void)nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE, id, NGHTTP2_NO_ERROR);
        }
        return 0;
    }

    static nghttp2_ssize read_body(nghttp2_session*, std::int32_t, std::uint8_t* output, std::size_t size,
                                   std::uint32_t* flags, nghttp2_data_source* source, void* user) {
        auto& connection = of(user);
        auto& stream = *static_cast<Stream*>(source->ptr);
        std::lock_guard lock(connection.mutex_);
        const auto count = std::min(size, stream.outbound.size());
        std::memcpy(output, stream.outbound.data(), count);
        stream.outbound.erase(0, count);
        stream.taken += count;
        if (stream.outbound.empty() && stream.outbound_end) {
            *flags |= NGHTTP2_DATA_FLAG_EOF;
            return static_cast<nghttp2_ssize>(count);
        }
        if (count == 0) {
            stream.deferred = true;
            return NGHTTP2_ERR_DEFERRED;
        }
        return static_cast<nghttp2_ssize>(count);
    }

    ConnectionStream& io_;
    const Responder& respond_;
    const ConnectionLimits& limits_;
    nghttp2_session* session_ = nullptr;
    WakePipe wake_;
    bool goaway_sent_ = false;
    // The map is changed only on the connection thread; the shared fields of
    // its streams, consume_ and running_ are guarded by mutex_.
    std::map<std::int32_t, std::shared_ptr<Stream>> streams_;
    std::mutex mutex_;
    std::vector<std::pair<std::int32_t, std::size_t>> consume_;
    std::uint32_t running_ = 0;
};

// The body of one stream, as the responder reads it.
class StreamBody final : public RequestBody {
public:
    StreamBody(Connection& connection, Stream& stream) : connection_(connection), stream_(stream) {}

    Piece read() override {
        if (ended_) return last_;
        std::unique_lock lock(connection_.mutex_);
        for (;;) {
            if (stream_.reset) return end({Status::Failed});
            if (stream_.body_rejected) return end({Status::Rejected, {}, 413});
            if (!stream_.inbound.empty()) {
                current_.clear();
                current_.swap(stream_.inbound);
                stream_.to_consume += current_.size();
                connection_.wake_.signal();
                return {Status::Data, current_};
            }
            if (stream_.inbound_end) return end({Status::End});
            if (Clock::now() >= stream_.deadline) return end({Status::TimedOut});
            if (wake_ < 0) {
                stream_.changed.wait_until(lock, stream_.deadline);
                continue;
            }
            if (!stream_.pipe) stream_.pipe.emplace();
            if (!stream_.pipe->valid()) return end({Status::Failed});
            pollfd entries[2] = {{stream_.pipe->descriptor(), POLLIN, 0}, {wake_, POLLIN, 0}};
            lock.unlock();
            const int ready = socket_io::poll_until(std::span(entries, 2), stream_.deadline);
            lock.lock();
            stream_.pipe->drain();
            if (ready > 0 && entries[0].revents == 0 && entries[1].revents != 0) return {Status::Woken};
        }
    }

    void wake_on(int descriptor) override { wake_ = descriptor; }
    bool ended() const { return ended_; }
    Status outcome() const { return last_.status; }
    int rejection() const { return last_.status_code; }

private:
    Piece end(Piece piece) {
        ended_ = true;
        last_ = piece;
        return piece;
    }

    Connection& connection_;
    Stream& stream_;
    std::string current_;
    int wake_ = -1;
    bool ended_ = false;
    Piece last_{Status::End};
};

// The response of one stream: a HEADERS frame, then DATA frames as far as
// the client's flow-control window allows. write() returns once its bytes
// are on the transport, finish() once END_STREAM is.
class StreamWriter final : public ResponseWriter {
public:
    StreamWriter(Connection& connection, Stream& stream, bool head_method)
        : connection_(connection), stream_(stream), head_method_(head_method) {}

    StreamStatus start(int status, const std::vector<Header>& headers,
                       std::optional<std::uint64_t> length) override {
        if (started_) return fail();
        started_ = true;
        const bool no_content = status == 204 || status == 304;
        if (status < 200 || status > 599 || !valid_fields(headers) ||
            ((no_content || status == 205) && length && *length != 0)) {
            // As in HTTP/1.1: answered 500 in its place.
            invalid_ = true;
            (void)submit(500, {}, false, true);
            return fail();
        }
        length_ = length;
        suppress_ = head_method_ || no_content;
        std::vector<Header> fields;
        fields.reserve(headers.size() + 1);
        for (const auto& header : headers) {
            Header field{header.name, header.value};
            std::ranges::transform(field.name, field.name.begin(), ascii_lower);
            if (connection_field(field.name)) continue;
            fields.push_back(std::move(field));
        }
        if (length && !no_content) fields.push_back({"content-length", std::to_string(*length)});
        const bool body = !suppress_ && !(length && *length == 0);
        hold_ = body && length && *length <= coalesce_bytes;
        if (!submit(status, std::move(fields), body, !hold_)) return fail();
        return StreamStatus::Ready;
    }

    StreamStatus write(std::string_view bytes) override {
        if (!started_ || finished_ || failed_) return fail();
        if (length_ && bytes.size() > *length_ - written_) return fail();
        written_ += bytes.size();
        if (suppress_ || bytes.empty()) return StreamStatus::Ready;
        std::unique_lock lock(connection_.mutex_);
        if (stream_.reset) return fail();
        stream_.outbound.append(bytes);
        stream_.appended += bytes.size();
        // A held head leaves with its whole body and END_STREAM at finish().
        if (hold_) return StreamStatus::Ready;
        connection_.wake_.signal();
        return result(wait(lock, [&] { return stream_.flushed >= stream_.appended; }));
    }

    StreamStatus finish() override {
        if (!started_ || finished_ || failed_) return fail();
        if (length_ && written_ != *length_) return fail();
        finished_ = true;
        std::unique_lock lock(connection_.mutex_);
        if (stream_.reset) return fail();
        stream_.outbound_end = true;
        stream_.head_ready = true;
        connection_.wake_.signal();
        return result(wait(lock, [&] { return stream_.end_flushed; }));
    }

    bool started() const override { return started_; }

    void on_cancel(std::function<void()> cancel) override {
        std::lock_guard lock(connection_.mutex_);
        stream_.cancel = std::move(cancel);
        if (stream_.reset && stream_.cancel) stream_.cancel();
    }

    // A response with no body that the connection gives in the responder's
    // place; it does not wait for the transport.
    bool answer(int status) {
        if (started_) return false;
        started_ = true;
        finished_ = true;
        return submit(status, {}, false, true);
    }

    bool complete() const { return finished_ && !failed_; }
    bool invalid() const { return invalid_; }

private:
    bool submit(int status, std::vector<Header> fields, bool body, bool ready) {
        std::lock_guard lock(connection_.mutex_);
        if (stream_.reset) return false;
        stream_.status = status;
        stream_.fields = std::move(fields);
        stream_.body_follows = body;
        if (!body) stream_.outbound_end = true;
        if (ready) {
            stream_.head_ready = true;
            connection_.wake_.signal();
        }
        return true;
    }

    template <typename Done>
    StreamStatus wait(std::unique_lock<std::mutex>& lock, Done done) {
        while (!done()) {
            if (stream_.reset) return StreamStatus::Failed;
            if (stream_.changed.wait_until(lock, stream_.deadline) == std::cv_status::timeout && !done()) {
                return stream_.reset ? StreamStatus::Failed : StreamStatus::TimedOut;
            }
        }
        return StreamStatus::Ready;
    }

    StreamStatus fail(StreamStatus status = StreamStatus::Failed) {
        failed_ = true;
        return status;
    }
    StreamStatus result(StreamStatus status) { return status == StreamStatus::Ready ? status : fail(status); }

    Connection& connection_;
    Stream& stream_;
    bool head_method_;
    bool started_ = false;
    bool finished_ = false;
    bool failed_ = false;
    bool invalid_ = false;
    bool suppress_ = false;
    bool hold_ = false;
    std::optional<std::uint64_t> length_;
    std::uint64_t written_ = 0;
};

// One stream's responder, on its own thread. What follows the responder
// mirrors serve_connection: an unstarted response is answered with the
// body's rejection, 504 or 500; a started one that did not finish is reset.
void Connection::serve(Stream& stream) {
    StreamBody body(*this, stream);
    StreamWriter writer(*this, stream, stream.head.method == "HEAD");
    try {
        respond_(stream.head, body, writer, stream.deadline);
    } catch (...) {
        // A responder failure is a 500 below, as for any unanswered request.
    }
    writer.on_cancel({});
    bool reset = false;
    {
        std::lock_guard lock(mutex_);
        reset = stream.reset;
    }
    if (!writer.started() && !reset) {
        int status = 0;
        if (body.ended() && body.outcome() == RequestBody::Status::Rejected) status = body.rejection();
        else if (body.ended() && body.outcome() == RequestBody::Status::TimedOut) status = 504;
        else if (!(body.ended() && body.outcome() == RequestBody::Status::Failed)) {
            status = Clock::now() >= stream.deadline ? 504 : 500;
        }
        if (status != 0) (void)writer.answer(status);
    }
    std::lock_guard lock(mutex_);
    if (writer.started() && !writer.complete() && !writer.invalid()) stream.abort = true;
    // An unanswered stream whose body failed is reset as well.
    if (!writer.started() && !stream.reset) stream.abort = true;
    stream.responder_done = true;
    --running_;
    wake_.signal();
}

// Replays bytes already read before the stream's own.
class ReplayStream final : public ConnectionStream {
public:
    ReplayStream(ConnectionStream& inner, std::string replay) : inner_(inner), replay_(std::move(replay)) {}

    StreamRead read(std::span<char> output, Deadline deadline) override { return read(output, deadline, -1); }
    StreamRead read(std::span<char> output, Deadline deadline, int wake) override {
        if (offset_ < replay_.size()) {
            const auto count = std::min(output.size(), replay_.size() - offset_);
            std::memcpy(output.data(), replay_.data() + offset_, count);
            offset_ += count;
            return {StreamStatus::Ready, count};
        }
        return inner_.read(output, deadline, wake);
    }
    StreamStatus write_all(std::string_view input, Deadline deadline) override {
        return inner_.write_all(input, deadline);
    }
    void close_after_response() override { inner_.close_after_response(); }

private:
    ConnectionStream& inner_;
    std::string replay_;
    std::size_t offset_ = 0;
};

} // namespace

ConnectionEnd serve_http2_connection(ConnectionStream& stream, const Responder& respond,
                                     const ConnectionLimits& limits, std::string_view received) {
    Connection connection(stream, respond, limits);
    return connection.run(received);
}

ConnectionEnd serve_cleartext_connection(ConnectionStream& stream, const Responder& respond,
                                         const ConnectionLimits& limits) {
    std::string received;
    std::array<char, 16 * 1024> buffer;
    const auto deadline = Clock::now() + limits.head_timeout;
    // Bytes arrive until they leave the preface or contain all of it.
    while (received.size() < http2_client_preface.size() &&
           http2_client_preface.starts_with(received)) {
        const auto input = stream.read(buffer, deadline);
        if (input.status != StreamStatus::Ready) {
            if (!received.empty()) break; // The HTTP/1.1 decoder reports the truncated head.
            if (input.status == StreamStatus::End) return ConnectionEnd::PeerClosed;
            if (input.status == StreamStatus::TimedOut) return ConnectionEnd::HeadTimeout;
            return ConnectionEnd::StreamFailure;
        }
        received.append(buffer.data(), input.size);
    }
    if (received.starts_with(http2_client_preface)) return serve_http2_connection(stream, respond, limits, received);
    ReplayStream replay(stream, std::move(received));
    return serve_connection(replay, respond, limits);
}

} // namespace neura::server::http

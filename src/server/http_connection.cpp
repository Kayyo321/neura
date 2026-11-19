#include "server/http_connection.hpp"

#include <array>

namespace neura::server::http {
namespace {

constexpr std::string_view continue_reply = "HTTP/1.1 100 Continue\r\n\r\n";

std::string_view reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 415: return "Unsupported Media Type";
    case 417: return "Expectation Failed";
    case 422: return "Unprocessable Content";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default: return {};
    }
}

bool token_char(char c) {
    constexpr std::string_view extra = "!#$%&'*+-.^_`|~";
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           extra.find(c) != std::string_view::npos;
}

bool same_name(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

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

// Framing fields belong to the connection.
bool framing_field(std::string_view name) {
    return same_name(name, "content-length") || same_name(name, "transfer-encoding") || same_name(name, "connection");
}

StreamStatus write_response(ConnectionStream& stream, int status, std::string_view body,
                            bool head_method, bool close, ConnectionClock::time_point deadline,
                            const std::vector<Header>& headers = {}) {
    const bool no_content = status == 204 || status == 304;
    std::string head = "HTTP/1.1 " + std::to_string(status) + " ";
    head += reason_phrase(status);
    head += "\r\n";
    for (const auto& header : headers) {
        if (framing_field(header.name)) continue;
        head += header.name;
        head += ": ";
        head += header.value;
        head += "\r\n";
    }
    if (!no_content) head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    if (close) head += "Connection: close\r\n";
    head += "\r\n";

    const auto result = stream.write_all(head, deadline);
    if (result != StreamStatus::Ready || head_method || no_content || body.empty()) return result;
    return stream.write_all(body, deadline);
}

ConnectionEnd close_with_status(ConnectionStream& stream, int status, ConnectionEnd end) {
    // A rejection or pre-response timeout has its own bounded write window.
    const auto deadline = ConnectionClock::now() + std::chrono::seconds{1};
    if (write_response(stream, status, {}, false, true, deadline) != StreamStatus::Ready) {
        return ConnectionEnd::StreamFailure;
    }
    stream.close_after_response();
    return end;
}

ConnectionEnd write_failure(StreamStatus status) {
    return status == StreamStatus::TimedOut ? ConnectionEnd::RequestTimeout : ConnectionEnd::StreamFailure;
}

// The body of one request, decoded from the connection as it is read.
class ConnectionBody final : public RequestBody {
public:
    ConnectionBody(ConnectionStream& stream, RequestDecoder& decoder, std::span<char> buffer,
                   std::string_view& unread, ConnectionClock::time_point deadline)
        : stream_(stream), decoder_(decoder), buffer_(buffer), unread_(unread), deadline_(deadline) {}

    Piece read() override {
        if (ended_) return last_;
        for (;;) {
            if (ConnectionClock::now() >= deadline_) return end({Status::TimedOut});
            const auto result = decoder_.consume(unread_);
            unread_.remove_prefix(result.consumed);
            switch (result.event) {
            case ReadEvent::Complete:
                return end({Status::End});
            case ReadEvent::BodyData:
                if (result.body_data.empty()) continue;
                return {Status::Data, result.body_data};
            case ReadEvent::Rejected:
                return end({Status::Rejected, {}, result.status});
            case ReadEvent::NeedInput: {
                const auto read = stream_.read(buffer_, deadline_, wake_);
                switch (read.status) {
                case StreamStatus::Woken:
                    return {Status::Woken};
                case StreamStatus::Ready:
                    if (read.size == 0 || read.size > buffer_.size()) return end({Status::Failed});
                    unread_ = {buffer_.data(), read.size};
                    continue;
                case StreamStatus::End:
                    (void)decoder_.finish_input();
                    truncated_ = true;
                    return end({Status::Failed});
                case StreamStatus::TimedOut:
                    return end({Status::TimedOut});
                case StreamStatus::Failed:
                    return end({Status::Failed});
                }
                return end({Status::Failed});
            }
            default:
                return end({Status::Failed});
            }
        }
    }

    void wake_on(int descriptor) override { wake_ = descriptor; }
    bool ended() const { return ended_; }
    Status outcome() const { return last_.status; }
    int rejection() const { return last_.status_code; }
    bool truncated() const { return truncated_; }

private:
    Piece end(Piece piece) {
        ended_ = true;
        last_ = piece;
        return piece;
    }

    ConnectionStream& stream_;
    RequestDecoder& decoder_;
    std::span<char> buffer_;
    std::string_view& unread_;
    ConnectionClock::time_point deadline_;
    bool ended_ = false;
    bool truncated_ = false;
    int wake_ = -1;
    Piece last_{Status::End};
};

std::string hex(std::size_t value) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    do {
        text.insert(text.begin(), digits[value % 16]);
        value /= 16;
    } while (value != 0);
    return text;
}

// The response of one request, written to the connection as it is produced.
class ConnectionWriter final : public ResponseWriter {
public:
    ConnectionWriter(ConnectionStream& stream, bool head_method, bool close, ConnectionClock::time_point deadline)
        : stream_(stream), head_method_(head_method), close_(close), deadline_(deadline) {}

    StreamStatus start(int status, const std::vector<Header>& headers,
                       std::optional<std::uint64_t> length) override {
        if (started_) return fail();
        started_ = true;
        // A response that starts at or past the deadline is the host's 504
        // for a request past S-8: like close_with_status's, it has its own
        // bounded write window, or it would never leave and the client would
        // see the connection close with no answer. The host's deadline is
        // 50 ms before this one, which a loaded machine can overrun.
        if (ConnectionClock::now() >= deadline_) deadline_ = ConnectionClock::now() + std::chrono::seconds{1};
        const bool no_content = status == 204 || status == 304;
        if (status < 200 || status > 599 || !valid_fields(headers) ||
            ((no_content || status == 205) && length && *length != 0)) {
            invalid_ = true;
            (void)close_with_status(stream_, 500, ConnectionEnd::ResponseClosed);
            return fail();
        }
        length_ = length;
        suppress_ = head_method_ || no_content;
        chunked_ = !no_content && !length;
        std::string head = "HTTP/1.1 " + std::to_string(status) + " ";
        head += reason_phrase(status);
        head += "\r\n";
        for (const auto& header : headers) {
            if (framing_field(header.name)) continue;
            head += header.name;
            head += ": ";
            head += header.value;
            head += "\r\n";
        }
        if (chunked_) head += "Transfer-Encoding: chunked\r\n";
        else if (!no_content) head += "Content-Length: " + std::to_string(*length) + "\r\n";
        if (close_) head += "Connection: close\r\n";
        head += "\r\n";
        // A head with a known, small body is held back and sent with it, so
        // a buffered response leaves in one write.
        if (!chunked_ && !suppress_ && *length_ != 0 && *length_ <= coalesce_bytes) {
            pending_ = std::move(head);
            return StreamStatus::Ready;
        }
        return result(stream_.write_all(head, deadline_));
    }

    StreamStatus write(std::string_view bytes) override {
        if (!started_ || finished_ || failed_) return fail();
        if (length_ && bytes.size() > *length_ - written_) return fail();
        written_ += bytes.size();
        if (suppress_ || bytes.empty()) return StreamStatus::Ready;
        if (!pending_.empty()) {
            pending_.append(bytes);
            if (written_ < *length_) return StreamStatus::Ready;
            const auto status = stream_.write_all(pending_, deadline_);
            pending_.clear();
            return result(status);
        }
        if (!chunked_) return result(stream_.write_all(bytes, deadline_));
        std::string frame = hex(bytes.size());
        frame += "\r\n";
        frame.append(bytes);
        frame += "\r\n";
        return result(stream_.write_all(frame, deadline_));
    }

    StreamStatus finish() override {
        if (!started_ || finished_ || failed_) return fail();
        if (length_ && written_ != *length_) return fail();
        finished_ = true;
        if (!pending_.empty()) {
            const auto status = stream_.write_all(pending_, deadline_);
            pending_.clear();
            if (status != StreamStatus::Ready) return fail(status);
        }
        if (chunked_ && !suppress_) return result(stream_.write_all("0\r\n\r\n", deadline_));
        return StreamStatus::Ready;
    }

    bool started() const override { return started_; }
    bool finished() const { return finished_ && !failed_; }
    bool failed() const { return failed_; }
    bool invalid() const { return invalid_; }
    StreamStatus failure() const { return failure_; }

private:
    StreamStatus fail(StreamStatus status = StreamStatus::Failed) {
        failed_ = true;
        failure_ = status;
        return status;
    }
    StreamStatus result(StreamStatus status) { return status == StreamStatus::Ready ? status : fail(status); }

    ConnectionStream& stream_;
    bool head_method_;
    bool close_;
    ConnectionClock::time_point deadline_;
    bool started_ = false;
    bool finished_ = false;
    bool failed_ = false;
    bool invalid_ = false;
    bool suppress_ = false;
    bool chunked_ = false;
    static constexpr std::uint64_t coalesce_bytes = 64 * 1024;
    StreamStatus failure_ = StreamStatus::Ready;
    std::string pending_;
    std::optional<std::uint64_t> length_;
    std::uint64_t written_ = 0;
};

} // namespace

Responder buffered(BufferedResponder respond, std::size_t response_bytes) {
    return [respond = std::move(respond), response_bytes](const RequestHead& head, RequestBody& body,
                                                          ResponseWriter& writer,
                                                          ConnectionClock::time_point deadline) {
        std::string content;
        for (;;) {
            const auto piece = body.read();
            if (piece.status == RequestBody::Status::Data) {
                content.append(piece.data);
                continue;
            }
            if (piece.status == RequestBody::Status::End) break;
            if (piece.status == RequestBody::Status::Woken) continue;
            return;
        }
        const auto response = respond(head, content);
        // Unstarted: answered 504 past the deadline, and 500 otherwise.
        if (ConnectionClock::now() >= deadline || response.body.size() > response_bytes) return;
        if (writer.start(response.status, response.headers, response.body.size()) != StreamStatus::Ready) return;
        if (writer.write(response.body) != StreamStatus::Ready) return;
        (void)writer.finish();
    };
}

ConnectionEnd serve_connection(ConnectionStream& stream, const BufferedResponder& respond,
                               const ConnectionLimits& limits) {
    return serve_connection(stream, buffered(respond, limits.response_bytes), limits);
}

ConnectionEnd serve_connection(ConnectionStream& stream, const Responder& respond, const ConnectionLimits& limits) {
    std::array<char, 16 * 1024> buffer;
    std::string_view unread;
    for (;;) {
        RequestDecoder decoder(limits.request);
        auto deadline = ConnectionClock::now() + limits.head_timeout;

        // The head.
        for (bool head_ready = false; !head_ready;) {
            if (ConnectionClock::now() >= deadline) return ConnectionEnd::HeadTimeout;
            const auto result = decoder.consume(unread);
            unread.remove_prefix(result.consumed);
            switch (result.event) {
            case ReadEvent::HeadReady:
                head_ready = true;
                break;
            case ReadEvent::NeedInput: {
                const auto read = stream.read(buffer, deadline);
                switch (read.status) {
                case StreamStatus::Ready:
                    if (read.size == 0 || read.size > buffer.size()) return ConnectionEnd::StreamFailure;
                    unread = {buffer.data(), read.size};
                    break;
                case StreamStatus::End:
                    return decoder.finish_input().event == ReadEvent::Closed
                        ? ConnectionEnd::PeerClosed : ConnectionEnd::TruncatedRequest;
                case StreamStatus::TimedOut:
                    return ConnectionEnd::HeadTimeout;
                case StreamStatus::Failed:
                case StreamStatus::Woken:
                    return ConnectionEnd::StreamFailure;
                }
                break;
            }
            case ReadEvent::Rejected:
                return close_with_status(stream, result.status, ConnectionEnd::RejectedRequest);
            default:
                return ConnectionEnd::StreamFailure;
            }
        }

        // S-8 runs from the end of the head.
        deadline = ConnectionClock::now() + limits.request_timeout;
        const auto& head = decoder.head();
        if (head.expect_continue) {
            const auto sent = stream.write_all(continue_reply, deadline);
            if (sent != StreamStatus::Ready) return write_failure(sent);
        }

        ConnectionBody body(stream, decoder, buffer, unread, deadline);
        ConnectionWriter writer(stream, head.method == "HEAD", head.connection_close, deadline);
        {
            struct Finished {
                const std::function<void()>& notify;
                ~Finished() {
                    if (notify) notify();
                }
            } finished{limits.response_finished};
            respond(head, body, writer, deadline);

            if (!writer.started()) {
                if (body.ended()) {
                    switch (body.outcome()) {
                    case RequestBody::Status::Rejected:
                        return close_with_status(stream, body.rejection(), ConnectionEnd::RejectedRequest);
                    case RequestBody::Status::TimedOut:
                        return close_with_status(stream, 504, ConnectionEnd::RequestTimeout);
                    case RequestBody::Status::Failed:
                        return body.truncated() ? ConnectionEnd::TruncatedRequest : ConnectionEnd::StreamFailure;
                    default:
                        break;
                    }
                }
                if (ConnectionClock::now() >= deadline) {
                    return close_with_status(stream, 504, ConnectionEnd::RequestTimeout);
                }
                return close_with_status(stream, 500, ConnectionEnd::ResponseClosed);
            }
            if (writer.invalid()) return ConnectionEnd::ResponseClosed;
            if (writer.failed() || !writer.finished()) {
                // A response cut short: the client sees an incomplete body.
                if (writer.failure() == StreamStatus::TimedOut || ConnectionClock::now() >= deadline) {
                    return ConnectionEnd::RequestTimeout;
                }
                return ConnectionEnd::StreamFailure;
            }
        }

        // What the responder left of the body is read and discarded; a body
        // that cannot be read to its end closes the connection.
        body.wake_on(-1);
        while (!body.ended()) (void)body.read();
        if (body.outcome() != RequestBody::Status::End) {
            stream.close_after_response();
            return ConnectionEnd::ResponseClosed;
        }
        if (head.connection_close) {
            stream.close_after_response();
            return ConnectionEnd::ResponseClosed;
        }
    }
}

} // namespace neura::server::http

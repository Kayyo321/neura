#include "server/http_connection.hpp"

#include <array>

namespace neura::server::http {
namespace {

constexpr std::string_view continue_reply = "HTTP/1.1 100 Continue\r\n\r\n";

std::string_view reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 413: return "Content Too Large";
    case 417: return "Expectation Failed";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default: return {};
    }
}

StreamStatus write_response(ConnectionStream& stream, int status, std::string_view body,
                            bool head_method, bool close, ConnectionClock::time_point deadline) {
    const bool no_content = status == 204 || status == 304;
    std::string head = "HTTP/1.1 " + std::to_string(status) + " ";
    head += reason_phrase(status);
    head += "\r\n";
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

} // namespace

ConnectionEnd serve_connection(ConnectionStream& stream, const BufferedResponder& respond,
                               const ConnectionLimits& limits) {
    std::array<char, 16 * 1024> buffer;
    std::string_view unread;
    for (;;) {
        RequestDecoder decoder(limits.request);
        std::string body;
        bool reading_head = true;
        auto deadline = ConnectionClock::now() + limits.head_timeout;

        for (;;) {
            if (ConnectionClock::now() >= deadline) {
                if (reading_head) return ConnectionEnd::HeadTimeout;
                return close_with_status(stream, 504, ConnectionEnd::RequestTimeout);
            }
            const auto result = decoder.consume(unread);
            unread.remove_prefix(result.consumed);
            if (result.event == ReadEvent::Complete) break;

            switch (result.event) {
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
                    if (reading_head) return ConnectionEnd::HeadTimeout;
                    return close_with_status(stream, 504, ConnectionEnd::RequestTimeout);
                case StreamStatus::Failed:
                    return ConnectionEnd::StreamFailure;
                }
                break;
            }
            case ReadEvent::HeadReady:
                reading_head = false;
                deadline = ConnectionClock::now() + limits.request_timeout;
                if (decoder.head().expect_continue) {
                    const auto sent = stream.write_all(continue_reply, deadline);
                    if (sent != StreamStatus::Ready) return write_failure(sent);
                }
                break;
            case ReadEvent::BodyData:
                body.append(result.body_data);
                break;
            case ReadEvent::Rejected:
                return close_with_status(stream, result.status, ConnectionEnd::RejectedRequest);
            default:
                return ConnectionEnd::StreamFailure;
            }
        }

        const auto& head = decoder.head();
        const auto response = respond(head, body);
        if (ConnectionClock::now() >= deadline) return close_with_status(stream, 504, ConnectionEnd::RequestTimeout);
        const bool no_content = response.status == 204 || response.status == 205 || response.status == 304;
        if (response.status < 200 || response.status > 599 || response.body.size() > limits.response_bytes ||
            (no_content && !response.body.empty())) {
            return close_with_status(stream, 500, ConnectionEnd::ResponseClosed);
        }
        const auto sent = write_response(stream, response.status, response.body, head.method == "HEAD",
                                         head.connection_close, deadline);
        if (sent != StreamStatus::Ready) return write_failure(sent);
        if (head.connection_close) {
            stream.close_after_response();
            return ConnectionEnd::ResponseClosed;
        }
    }
}

} // namespace neura::server::http

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
        struct Finished {
            const std::function<void()>& notify;
            ~Finished() {
                if (notify) notify();
            }
        } finished{limits.response_finished};
        const auto response = respond(head, body);
        if (ConnectionClock::now() >= deadline) return close_with_status(stream, 504, ConnectionEnd::RequestTimeout);
        const bool no_content = response.status == 204 || response.status == 205 || response.status == 304;
        if (response.status < 200 || response.status > 599 || response.body.size() > limits.response_bytes ||
            (no_content && !response.body.empty()) || !valid_fields(response.headers)) {
            return close_with_status(stream, 500, ConnectionEnd::ResponseClosed);
        }
        const auto sent = write_response(stream, response.status, response.body, head.method == "HEAD",
                                         head.connection_close, deadline, response.headers);
        if (sent != StreamStatus::Ready) return write_failure(sent);
        if (head.connection_close) {
            stream.close_after_response();
            return ConnectionEnd::ResponseClosed;
        }
    }
}

} // namespace neura::server::http

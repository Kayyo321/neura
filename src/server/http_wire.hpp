#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neura::server::http {

struct RequestLimits {
    // S-5 also bounds each chunk-size line, aggregate chunk extensions and the
    // trailer section, as separate budgets. Excess chunk metadata is a 413.
    std::size_t head_bytes = 16 * 1024;
    std::uint64_t body_bytes = 1024 * 1024; // Apps030 S-6, after decoding.
};

enum class BodyFraming { None, ContentLength, Chunked };

struct Header {
    std::string name;
    std::string value;
};

struct RequestHead {
    std::string method;
    std::string target;
    std::vector<Header> headers; // Lowercase names. Equal lengths are canonicalized.
    BodyFraming framing = BodyFraming::None;
    std::uint64_t content_length = 0; // Defined for ContentLength framing.
    bool expect_continue = false;
    bool connection_close = false;
};

enum class ReadEvent {
    NeedInput,
    HeadReady,
    BodyData,
    Complete,
    Rejected,   // Send status, then close.
    Closed,     // EOF between requests.
    Incomplete, // EOF in a request. Discard the request and close.
};

struct ReadResult {
    ReadEvent event;
    std::size_t consumed = 0;
    int status = 0;              // Defined for Rejected.
    std::string_view body_data;  // BodyData borrows from the current input only.
};

// One decoder per request. Hand any unconsumed input back to the same decoder
// until Complete, then to a new decoder if keeping the connection alive.
class RequestDecoder {
public:
    explicit RequestDecoder(RequestLimits limits = {});

    // HeadReady stops at the end of the head, allowing a 100 response before
    // decoding body bytes. Complete can consume zero bytes. BodyData is tentative
    // until Complete, especially for chunked framing where S-6 can fail later.
    ReadResult consume(std::string_view input);
    ReadResult finish_input(); // Permanent EOF, after feeding all buffered input.
    const RequestHead& head() const { return head_; } // Available from HeadReady.

private:
    enum class State {
        RequestLine,
        Headers,
        FixedBody,
        ChunkSize,
        ChunkBody,
        ChunkEndCR,
        ChunkEndLF,
        Trailers,
        Complete,
        Rejected,
        Closed,
        Incomplete,
    };

    int parse_request_line();
    int parse_header();
    int validate_head();
    int parse_chunk_size();
    int parse_trailer();
    ReadResult reject(int status, std::size_t consumed);

    RequestLimits limits_;
    RequestHead head_;
    State state_ = State::RequestLine;
    int status_ = 0;
    std::string line_;
    bool saw_cr_ = false;
    std::size_t head_bytes_ = 0;
    std::size_t chunk_line_bytes_ = 0;
    std::size_t chunk_ext_bytes_ = 0;
    std::size_t trailer_bytes_ = 0;
    std::size_t trailer_fields_ = 0;
    std::uint64_t remaining_ = 0;
    std::uint64_t decoded_bytes_ = 0;
};

// The head checks of RequestDecoder for a head that arrived in another
// framing (HTTP/2): at most 100 fields (431), exactly one valid `host`
// (400), a known method (501), an origin-form or "*" target (400),
// Content-Length (400; over S-6, 413) and Expect (417). Returns 0 or the
// status to answer. On success a Content-Length sets `framing` to
// ContentLength and `content_length`, repeated equal lengths become one
// field, and `expect_continue` is set. Names must already be lowercase;
// HTTP/2 framing has refused Transfer-Encoding, Connection and Upgrade.
int check_request_head(RequestHead& head, const RequestLimits& limits);

} // namespace neura::server::http

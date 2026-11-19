#include "server/http_wire.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

using namespace neura::server::http;
using std::string;
using std::string_view;

constexpr string_view get_head = "GET /parts/7?query=hello%20world HTTP/1.1\r\nHost: example.test\r\n";
constexpr string_view post_head = "POST /parts HTTP/1.1\r\nHost: example.test\r\n";
constexpr string_view next_request = "HEAD /parts/8 HTTP/1.1\r\nHost: example.test\r\n\r\n";

void check(bool condition, string_view message) {
    if (!condition) throw std::runtime_error(string(message));
}

string post(string_view fields, string_view body = {}) {
    return string(post_head) + string(fields) + "\r\n" + string(body);
}

string chunked(string_view body) {
    return post("Transfer-Encoding: chunked\r\n", body);
}

struct Decoded {
    RequestHead head;
    string body;
    std::size_t consumed = 0;
    ReadEvent event = ReadEvent::NeedInput;
    int status = 0;
    bool head_ready = false;
};

void feed(RequestDecoder& decoder, Decoded& decoded, string_view input) {
    do {
        const auto result = decoder.consume(input);
        check(result.consumed <= input.size(), "decoder consumed beyond the available data");
        if (result.event == ReadEvent::BodyData) {
            check(decoded.head_ready, "the decoder returned body data before the head");
            check(!result.body_data.empty() && result.body_data.size() <= result.consumed &&
                  result.body_data == input.substr(result.consumed - result.body_data.size(), result.body_data.size()),
                  "body slice does not match the consumed input");
            decoded.body += result.body_data;
        } else {
            check(result.body_data.empty(), "non-body event contains a body slice");
        }
        input.remove_prefix(result.consumed);
        decoded.consumed += result.consumed;
        decoded.event = result.event;
        decoded.status = result.status;
        if (result.event == ReadEvent::HeadReady) {
            check(!decoded.head_ready, "the decoder returned the head twice");
            decoded.head = decoder.head();
            decoded.head_ready = true;
        } else if (result.event != ReadEvent::BodyData) {
            if (result.event == ReadEvent::NeedInput) check(input.empty(), "NeedInput left unconsumed bytes");
            return;
        }
    } while (true);
}

Decoded at_split(string_view input, std::size_t split, RequestLimits limits = {}) {
    RequestDecoder decoder(limits);
    Decoded decoded;
    feed(decoder, decoded, input.substr(0, split));
    if (decoded.event == ReadEvent::NeedInput) feed(decoder, decoded, input.substr(split));
    return decoded;
}

Decoded accepted(string_view wire, string_view expected_body, RequestLimits limits = {}) {
    const string input = string(wire) + string(next_request);
    for (std::size_t split = 0; split <= input.size(); ++split) {
        const auto decoded = at_split(input, split, limits);
        check(decoded.event == ReadEvent::Complete && decoded.head_ready, "valid request was not complete at split " + std::to_string(split));
        check(decoded.consumed == wire.size(), "decoder consumed bytes outside the first request");
        check(decoded.body == expected_body, "wrong decoded body at split " + std::to_string(split));
    }

    RequestDecoder decoder(limits);
    Decoded incremental;
    for (std::size_t index = 0; index < wire.size(); ++index) {
        feed(decoder, incremental, string_view(input).substr(index, 1));
        const auto expected = index == wire.size() - 1 ? ReadEvent::Complete : ReadEvent::NeedInput;
        check(incremental.event == expected, "unexpected event for one-byte reads");
    }
    check(incremental.body == expected_body && incremental.consumed == wire.size(), "one-byte reads changed the request");
    check(decoder.consume(next_request).consumed == 0, "completed decoder took pipelined input");
    RequestDecoder next;
    Decoded following;
    feed(next, following, string_view(input).substr(incremental.consumed));
    check(following.event == ReadEvent::Complete && following.head.method == "HEAD" && following.body.empty(),
          "pipelined request could not be decoded independently");
    return incremental;
}

void rejected(string_view wire, int status, bool head_ready = false, RequestLimits limits = {}) {
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        const auto decoded = at_split(wire, split, limits);
        check(decoded.event == ReadEvent::Rejected && decoded.status == status && decoded.head_ready == head_ready,
              "expected rejection " + std::to_string(status) + " at split " + std::to_string(split) +
              ", received status " + std::to_string(decoded.status));
    }
    RequestDecoder decoder(limits);
    Decoded decoded;
    feed(decoder, decoded, wire);
    const auto result = decoder.consume(next_request);
    check(result.event == ReadEvent::Rejected && result.status == status && result.consumed == 0,
          "rejection did not stop the connection");
    check(decoder.finish_input().event == ReadEvent::Rejected, "EOF overwrote a rejection");
}

void request_heads() {
    const auto request = accepted(string(get_head) + "X-MiXeD:\t value\t\r\nX-Mixed: second\r\n\r\n", "");
    check(request.head.method == "GET" && request.head.target == "/parts/7?query=hello%20world", "request line changed");
    check(request.head.framing == BodyFraming::None && !request.head.connection_close, "invalid default framing");
    check(request.head.headers.size() == 3 && request.head.headers[1].name == "x-mixed" &&
          request.head.headers[1].value == "value" && request.head.headers[2].value == "second", "header order or OWS changed");

    check(accepted("OPTIONS * HTTP/1.1\r\nHost: [::ffff:192.0.2.1]:8080\r\n\r\n", "").head.method == "OPTIONS",
          "OPTIONS asterisk-form");
    const auto proxy = accepted("GET http://[2001:db8::1]:80/index?x=1 HTTP/1.1\r\nHost: example.test\r\n\r\n", "");
    check(proxy.head.headers.front().value == "[2001:db8::1]:80", "absolute-form must supply the effective Host");
    for (string_view host : {"[::]", "[1:2:3:4:5:6:7:8]", "[v1.Test:host]", "example.test:0"}) {
        accepted("GET / HTTP/1.1\r\nHost: " + string(host) + "\r\n\r\n", "");
    }
    const auto closing = accepted(string(get_head) + "Connection: Keep-Alive, ClOsE\r\n\r\n", "");
    check(closing.head.connection_close, "connection close option was lost");

    rejected("GET / HTTP/1.1\r\n\r\n", 400);
    rejected(string(get_head) + "HOST: example.test\r\n\r\n", 400);
    for (string_view host : {"", "bad host", "[::1", "[:::1]", "[1:2:3:4:5:6:7:8:9]", "[::ffff:300.0.0.1]", "example.test:abc"}) {
        rejected("GET / HTTP/1.1\r\nHost: " + string(host) + "\r\n\r\n", 400);
    }
    rejected(string(get_head) + " folded\r\n\r\n", 400);
    rejected(string(get_head) + "\tfolded\r\n\r\n", 400);
    rejected(string(get_head) + "Field : value\r\n\r\n", 400);
    rejected(string(get_head) + "Field: invalid\x7f\r\n\r\n", 400);
    rejected("GET / HTTP/1.1\nHost: example.test\n\n", 400);
    rejected("GET  / HTTP/1.1\r\nHost: example.test\r\n\r\n", 400);
    rejected("GET / HTTP/1.0\r\nHost: example.test\r\n\r\n", 505);
    rejected("GET /bad%2 HTTP/1.1\r\nHost: example.test\r\n\r\n", 400);
    rejected("GET /#fragment HTTP/1.1\r\nHost: example.test\r\n\r\n", 400);
    rejected("GET * HTTP/1.1\r\nHost: example.test\r\n\r\n", 400);
    rejected("UNKNOWN / HTTP/1.1\r\nHost: example.test\r\n\r\n", 501);
    rejected("CONNECT example.test:443 HTTP/1.1\r\nHost: example.test:443\r\n\r\n", 501);
    rejected(string(get_head) + "Upgrade: websocket\r\n\r\n", 501);
    rejected(string(get_head) + "Connection: upgrade\r\n\r\n", 501);
}

void lengths_and_limits() {
    const std::string binary{"a\0b\n", 4};
    const auto fixed = accepted(post("Content-Length: 4\r\n", binary), binary, {1024, 4});
    check(fixed.head.framing == BodyFraming::ContentLength && fixed.head.content_length == 4, "fixed framing not advertised");
    const auto zero = accepted(post("Content-Length: 000\r\n"), "", {1024, 0});
    check(zero.head.framing == BodyFraming::ContentLength && zero.head.content_length == 0 &&
          zero.head.headers[1].value == "0", "zero length was not normalized");

    const auto equivalent = accepted(post("Content-Length: 04, 004\r\nX-Key: v\r\ncontent-length: 4\r\n", binary), binary);
    check(equivalent.head.headers.size() == 3 && equivalent.head.headers[1].name == "content-length" &&
          equivalent.head.headers[1].value == "4" && equivalent.head.headers[2].name == "x-key", "equal lengths were not canonicalized");
    for (string_view value : {"", "-3", "+3", "3,", "3,,3", "1 2", "4, 5"}) {
        rejected(post("Content-Length: " + string(value) + "\r\n"), 400);
    }
    rejected(post("Content-Length: 3\r\nContent-Length: 4\r\n"), 400);
    rejected(post("Content-Length: 5\r\n"), 413, false, {1024, 4});
    rejected(post("Content-Length: 18446744073709551616\r\n"), 413, false,
             {1024, std::numeric_limits<std::uint64_t>::max()});
    rejected(post("Content-Length: 1\r\nTransfer-Encoding: chunked\r\n"), 400);
    rejected(post("Transfer-Encoding: gzip\r\nContent-Length: 999999999999999999999999\r\n"), 400);

    const string head = string(get_head) + "\r\n";
    accepted(head, "", {head.size(), 0});
    rejected(head, 431, false, {head.size() - 1, 0});
    const std::size_t defaults = RequestLimits{}.head_bytes;
    const string prefix = string(get_head) + "X: ";
    const string max_head = prefix + string(defaults - prefix.size() - 4, 'x') + "\r\n\r\n";
    const auto exact = at_split(max_head + string(next_request), max_head.size() - 1);
    check(exact.event == ReadEvent::Complete && exact.consumed == defaults, "default S-5 must admit exactly 16 KiB");
    const auto over_limit = at_split(max_head.substr(0, max_head.size() - 4) + "x\r\n\r\n", max_head.size() - 1);
    check(over_limit.event == ReadEvent::Rejected && over_limit.status == 431, "default S-5 must reject 16 KiB plus one byte");
    string fields(get_head);
    for (int i = 0; i < 99; ++i) fields += "X:\r\n";
    check(accepted(fields + "\r\n", "").head.headers.size() == 100, "100 fields should be allowed");
    rejected(fields + "X:\r\n\r\n", 431);
}

void expect_continue() {
    const string request = post("Content-Length: 4\r\nExpect: 100-ConTinue\r\n", "data");
    RequestDecoder decoder;
    const auto head = decoder.consume(request + string(next_request));
    check(head.event == ReadEvent::HeadReady && head.consumed == request.size() - 4 &&
          head.body_data.empty() && decoder.head().expect_continue, "100 response must be possible before any body byte");
    const auto body = decoder.consume(string_view(request).substr(head.consumed));
    check(body.event == ReadEvent::BodyData && body.body_data == "data", "body unread after 100 response");
    check(decoder.finish_input().event == ReadEvent::Complete, "EOF at a fixed boundary must complete");
    check(!accepted(post("Content-Length: 0\r\nExpect: 100-continue\r\n"), "").head.expect_continue,
          "100 response must not be needed without a body");
    check(accepted(post("Transfer-Encoding: chunked\r\nExpect: 100-continue\r\n", "0\r\n\r\n"), "").head.expect_continue,
          "100 response is needed before receiving chunks");
    check(accepted(post("Content-Length: 1\r\nExpect: , 100-continue, 100-continue\r\n", "x"), "x").head.expect_continue,
          "valid expectation list should indicate continue");
    rejected(post("Content-Length: 4\r\nExpect: other\r\n"), 417);
    rejected(post("Content-Length: 4\r\nExpect: 100-continue, other\r\n"), 417);
    rejected(post("Content-Length: 5\r\nExpect: 100-continue\r\n"), 413, false, {1024, 4});
}

void chunk_framing() {
    const string binary{"b\0d\r\n", 5};
    const string wire = post("Transfer-Encoding: cHuNkEd\r\n",
        "2 ; x= \"quoted, value; \\\"ok\" ; flag\r\nab\r\n5\r\n" + binary + "\r\n000;last\r\nX-Trailer: yes\r\n\r\n");
    const auto decoded = accepted(wire, "ab" + binary, {1024, 7});
    check(decoded.head.framing == BodyFraming::Chunked && decoded.head.headers.size() == 2,
          "chunked framing was lost or trailers entered the head");
    accepted(chunked("0\r\nContent-Length: 3\r\nHost: trailer.test\r\n\r\n"), "", {1024, 0});
    accepted(chunked("A\r\n0123456789\r\n0\r\n\r\n"), "0123456789", {1024, 10});
    accepted(post("Transfer-Encoding: , chunked, \r\n", "0\r\n\r\n"), "");
    string short_chunks;
    for (int i = 0; i < 100; ++i) short_chunks += "1\r\na\r\n";
    accepted(chunked(short_chunks + "0\r\n\r\n"), string(100, 'a'), {100, 100});

    for (string_view coding : {"gzip", "deflate, chunked", "other; param=\"quoted, separator\", chunked"}) {
        rejected(post("Transfer-Encoding: " + string(coding) + "\r\n"), 501);
    }
    for (string_view coding : {"", "chunked, gzip", "chunked, chunked", "chunked; p=value", "gzip; p", "gzip; =value", "gzip; p=\""}) {
        rejected(post("Transfer-Encoding: " + string(coding) + "\r\n"), 400);
    }
    rejected(post("Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n"), 400);
    rejected(post("Transfer-Encoding: gzip\r\nTransfer-Encoding: chunked\r\n"), 501);
    for (string_view framing : {"\r\n", "x\r\n", "+1\r\n", "1 \r\n", "1;\r\n", "1;x=\"unterminated\r\n",
                                "1\r\naX", "1\r\na\rX", "0\r\n folded\r\n", "0\r\nTrailer field: invalid\r\n"}) {
        rejected(chunked(framing), 400, true);
    }
    const auto over_limit = at_split(chunked("3\r\nabc\r\n2\r\n"), 0, {1024, 4});
    check(over_limit.event == ReadEvent::Rejected && over_limit.status == 413 && over_limit.head_ready &&
          over_limit.body == "abc", "an oversized next chunk must be refused before emitting its body");
    rejected(chunked("10000000000000000\r\n"), 413, true, {1024, std::numeric_limits<std::uint64_t>::max()});
    rejected(chunked("0;" + string(100, 'x') + "\r\n\r\n"), 413, true, {100, 0});
    const string extended_chunk = "1;" + string(49, 'x') + "\r\na\r\n";
    accepted(chunked(extended_chunk + extended_chunk + "0\r\n\r\n"), "aa", {100, 2});
    rejected(chunked(extended_chunk + extended_chunk + "0;x\r\n\r\n"), 413, true, {100, 2});
    rejected(chunked("0\r\nX: " + string(100, 'x') + "\r\n\r\n"), 413, true, {100, 0});
    string trailer_lines;
    for (int i = 0; i < 100; ++i) trailer_lines += "X:\r\n";
    accepted(chunked("0\r\n" + trailer_lines + "\r\n"), "");
    rejected(chunked("0\r\n" + trailer_lines + "X:\r\n\r\n"), 413, true);

    const RequestLimits defaults;
    const string max_body(static_cast<std::size_t>(defaults.body_bytes), 'x');
    const auto at_limit = at_split(chunked("100000\r\n" + max_body + "\r\n0\r\n\r\n"), 64);
    check(at_limit.event == ReadEvent::Complete && at_limit.body == max_body, "default S-6 must admit exactly 1 MiB");
    rejected(chunked("100001\r\n"), 413, true);
}

void eof() {
    RequestDecoder closed;
    check(closed.finish_input().event == ReadEvent::Closed, "idle EOF must close cleanly");
    check(closed.consume(next_request).consumed == 0, "closed decoder took data");

    for (const auto& truncated : {
         string("G"), string(get_head) + "\r", post("Content-Length: 4\r\n", "abc"),
         chunked(""), chunked("2\r"), chunked("2\r\na"), chunked("2\r\nab"), chunked("2\r\nab\r"),
         chunked("2\r\nab\r\n"), chunked("0\r\n"), chunked("0\r\nX: a\r\n"), chunked("0\r\n\r")}) {
        for (std::size_t split = 0; split <= truncated.size(); ++split) {
            RequestDecoder decoder;
            Decoded decoded;
            feed(decoder, decoded, string_view(truncated).substr(0, split));
            check(decoded.event == ReadEvent::NeedInput, "truncated prefix terminated before EOF");
            feed(decoder, decoded, string_view(truncated).substr(split));
            check(decoded.event == ReadEvent::NeedInput && decoder.finish_input().event == ReadEvent::Incomplete,
                  "truncated request must be incomplete at EOF");
            check(decoder.consume(next_request).consumed == 0, "incomplete decoder took another request");
        }
    }
}

} // namespace

int main() {
    constexpr std::pair<string_view, void (*)()> suites[] = {
        {"request heads", request_heads},
        {"lengths and limits", lengths_and_limits},
        {"100-continue", expect_continue},
        {"chunk framing", chunk_framing},
        {"EOF", eof},
    };
    for (const auto& [name, run] : suites) {
        try {
            run();
        } catch (const std::exception& error) {
            std::cerr << name << ": " << error.what() << '\n';
            return 1;
        }
    }
}

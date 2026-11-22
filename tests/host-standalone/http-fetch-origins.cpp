// The origin list of an http_client grant (docs/runtime/NVM.md, "HTTP client
// origins"). The resolver and connect of this executable are wrapped: names
// resolve to fixed answers, and the one simulated public address connects to
// this process's loopback listener. No test reaches the public network.
#include "nvm/http_fetch_provider.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <thread>

using namespace neura::nvm;
using namespace neura::nvm::provider;

namespace {
std::atomic<int> changing_answers{0};
std::atomic<int> connects{0};
constexpr std::uint32_t public_address = 0x5db8d822; // 93.184.216.34
}

extern "C" int __real_getaddrinfo(const char*, const char*, const addrinfo*, addrinfo**);
extern "C" int __wrap_getaddrinfo(const char* host, const char* port, const addrinfo* hints, addrinfo** output) {
    if (hints && (hints->ai_flags & AI_NUMERICHOST) != 0) return __real_getaddrinfo(host, port, hints, output);
    const std::string_view name(host);
    const char* numeric = nullptr;
    if (name == "public.test") numeric = "93.184.216.34";
    if (name == "local.test") numeric = "127.0.0.1";
    if (name == "mapped.test") numeric = "::ffff:127.0.0.1";
    if (name == "metadata.test") numeric = "169.254.169.254";
    if (name == "cgnat.test") numeric = "100.64.0.1";
    if (name == "unique-local.test") numeric = "fc00::1";
    if (name == "link-local.test") numeric = "fe80::1";
    if (name == "unspecified.test") numeric = "::";
    if (name == "private.test") numeric = "10.8.0.1";
    // The first answer is public, every later one private.
    if (name == "changing.test") numeric = changing_answers++ == 0 ? "93.184.216.34" : "10.8.0.1";
    assert(numeric);
    addrinfo numeric_hints{};
    if (hints) numeric_hints = *hints;
    numeric_hints.ai_flags |= AI_NUMERICHOST;
    return __real_getaddrinfo(numeric, port, &numeric_hints, output);
}

extern "C" int __real_connect(int, const sockaddr*, socklen_t);
extern "C" int __wrap_connect(int fd, const sockaddr* address, socklen_t length) {
    // Only the public address and IPv4 loopback may ever be attempted.
    assert(address->sa_family == AF_INET && length == sizeof(sockaddr_in));
    auto local = *reinterpret_cast<const sockaddr_in*>(address);
    const auto ip = ntohl(local.sin_addr.s_addr);
    assert(ip == public_address || ip == INADDR_LOOPBACK);
    ++connects;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return __real_connect(fd, reinterpret_cast<const sockaddr*>(&local), length);
}

int main() {
    assert(valid_http_client_origin("https://api.example"));
    assert(valid_http_client_origin("http://[::1]:8080/"));
    assert(!valid_http_client_origin("https://api.example/path"));
    assert(!valid_http_client_origin("ws://api.example"));
    assert(!valid_http_client_origin("https://*.example"));
    assert(!valid_http_client_origin("api.example"));

    const auto listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in bind_address{};
    bind_address.sin_family = AF_INET;
    bind_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&bind_address), sizeof bind_address) == 0);
    assert(::listen(listener, 4) == 0);
    socklen_t length = sizeof bind_address;
    assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&bind_address), &length) == 0);
    const auto port = std::to_string(ntohs(bind_address.sin_port));
    const auto origin = [&](std::string_view host) { return "http://" + std::string(host) + ":" + port; };
    const auto fetch = [](const std::string& url, std::optional<std::vector<std::string>> origins,
                          unsigned version = 2) {
        Capabilities grant;
        grant.http_client = true;
        grant.http_client_origins = std::move(origins);
        FunctionService::Handler call;
        add_http_fetch_services(
            [&](HostSignature signature, FunctionService::Handler invoke, FunctionService::Begin) {
                if (signature.version == version) call = std::move(invoke);
            },
            grant);
        assert(call);
        return call(HostRequest{0, 0,
                                {
                                    bytes_value("GET"),
                                    bytes_value(url),
                                    {},
                                    bytes_value(""),
                                    int64_value(2000000000),
                                    scalar(0),
                                }});
    };
    // Serves one response per accepted connection, `count` connections.
    const auto serve = [&](std::vector<std::string> responses) {
        return std::jthread([listener, responses = std::move(responses)] {
            for (const auto& response : responses) {
                pollfd readable{listener, POLLIN, 0};
                assert(::poll(&readable, 1, 2500) == 1);
                const auto stream = ::accept(listener, nullptr, nullptr);
                assert(stream >= 0);
                char block[1024];
                std::string head;
                while (head.find("\r\n\r\n") == std::string::npos && head.size() < 4096) {
                    readable = {stream, POLLIN, 0};
                    assert(::poll(&readable, 1, 2500) == 1);
                    const auto count = ::recv(stream, block, sizeof block, 0);
                    assert(count > 0);
                    head.append(block, count);
                }
                assert(::send(stream, response.data(), response.size(), MSG_NOSIGNAL) ==
                       static_cast<ssize_t>(response.size()));
                ::close(stream);
            }
        });
    };
    const std::string ok = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    const auto permit = [&](const std::string& host, std::optional<std::vector<std::string>> origins) {
        const auto previous_connects = connects.load();
        const auto server = serve({ok});
        const auto done = fetch(origin(host), std::move(origins));
        assert(done.success && done.value.elements.size() == 4);
        assert(done.value.elements[0].bits == 200 && done.value.elements[2].bytes == "ok");
        assert(done.value.elements[3].bytes.empty());
        assert(connects == previous_connects + 1);
    };
    // A denial is status 0 with the refused origin, and opens no connection.
    const auto deny = [&](const std::string& host, std::vector<std::string> allowed) {
        const auto previous_connects = connects.load();
        const auto done = fetch(origin(host), std::move(allowed));
        assert(done.success && done.value.elements[0].bits == 0);
        assert(done.value.elements[3].bytes == origin(host) && connects == previous_connects);
    };

    // A listed name with a public answer; names match without case.
    permit("public.test", std::vector{origin("PUBLIC.test")});
    // A listed name with a non-global answer needs the numeric origin too.
    deny("local.test", {origin("local.test")});
    permit("local.test", std::vector{origin("local.test"), origin("127.0.0.1")});
    // The numeric origin must have the same scheme and port.
    deny("local.test", {origin("local.test"), "https://127.0.0.1:" + port, "http://127.0.0.1:1"});
    // A numeric origin allows exactly its address.
    permit("127.0.0.1", std::vector{origin("127.0.0.1")});
    deny("127.0.0.1", {origin("127.0.0.2")});
    // `localhost` is a name: its loopback addresses need numeric origins.
    deny("localhost", {origin("localhost")});
    // No list keeps the unscoped grant; an empty list allows nothing.
    permit("local.test", std::nullopt);
    deny("127.0.0.1", {});
    deny("public.test", {});
    // An unlisted origin is refused before any lookup.
    deny("public.test", {origin("other.test")});
    // IPv4-mapped loopback is loopback.
    deny("mapped.test", {origin("mapped.test")});
    // Every non-global range named by the rule, metadata addresses included.
    for (const auto* host : {"metadata.test", "cgnat.test", "unique-local.test", "link-local.test",
                             "unspecified.test", "private.test"})
        deny(host, {origin(host)});
    // Version 1 has no origin field: the denial is error 14.
    const auto legacy = fetch(origin("local.test"), std::vector{origin("local.test")}, 1);
    assert(!legacy.success && legacy.value.bits == host_error::capability_denied);

    // A redirect to an unlisted origin is refused at that hop.
    {
        const auto previous_connects = connects.load();
        const auto server = serve({"HTTP/1.1 302 Found\r\nLocation: " + origin("unlisted.test") +
                                   "/private\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"});
        const auto done = fetch(origin("127.0.0.1"), std::vector{origin("127.0.0.1")});
        assert(done.success && done.value.elements[0].bits == 0);
        assert(done.value.elements[3].bytes == origin("unlisted.test"));
        assert(connects == previous_connects + 1);
    }
    // Within one request, a same-origin redirect resolves the name again;
    // the second, private answer is refused at the second connect.
    {
        const auto previous_connects = connects.load();
        const auto server = serve({"HTTP/1.1 302 Found\r\nLocation: /again\r\nContent-Length: 0\r\n"
                                   "Connection: close\r\n\r\n"});
        const auto done = fetch(origin("changing.test"), std::vector{origin("changing.test")});
        assert(changing_answers == 2);
        assert(done.success && done.value.elements[0].bits == 0);
        assert(done.value.elements[3].bytes == origin("changing.test"));
        assert(connects == previous_connects + 1);
    }
    // A list that does not parse allows nothing.
    {
        const auto previous_connects = connects.load();
        const auto done = fetch(origin("127.0.0.1"), std::vector<std::string>{"not an origin", origin("127.0.0.1")});
        assert(done.success && done.value.elements[0].bits == 0 && connects == previous_connects);
    }
    ::close(listener);
}

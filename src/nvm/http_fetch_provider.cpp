#include "http_fetch_provider.hpp"
#include "client_socket.hpp"
#include "platform/socket.hpp"
#include "sigpipe_guard.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#ifdef NEURA_NVM_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif
#ifdef NEURA_NVM_HTTP2
#include <nghttp2/nghttp2.h>
#endif

namespace neura::nvm::provider {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t body_limit = 8 * 1024 * 1024;
constexpr std::size_t head_limit = 16384;
constexpr std::uint32_t too_large = 16, busy = 17, unknown = 18;
// Fetch codes after the shared table in providers.hpp, whose 19 to 22 are
// the SQL provider's (docs/runtime/NVM.md, "HTTP client services").
constexpr std::uint32_t redirect_downgrade = 23, redirect_body = 24;
[[maybe_unused]] constexpr std::uint32_t not_processed = 25;
constexpr std::int64_t default_timeout = 30000000000LL;
struct Failure {
    std::uint32_t code;
    std::string origin{};
};
[[noreturn]] void reject(std::uint32_t code) {
    throw Failure{code};
}
HostCompletion fetch_error(std::uint32_t code, bool detailed, std::string origin = {}) {
    // A host error carries only a uint32. Version 2 reports an origin outside
    // the grant as a reply with status 0, no fields or body, and the origin;
    // version 1 reports it as error 14 (CapabilityDenied).
    if (detailed && !origin.empty())
        return HostCompletion::ok(
            tuple_value({scalar(0), HostValue{}, HostValue{}, bytes_value(std::move(origin))}));
    return fail(code);
}
std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}
bool token(std::string_view text) {
    if (text.empty()) return false;
    for (const unsigned char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos))
            return false;
    return true;
}
bool field_value(std::string_view text) {
    for (const unsigned char c : text)
        if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}
std::uint64_t number(std::string_view text, int base, std::uint32_t error = unknown) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) reject(error);
    return value;
}
// HTTP field bytes are Latin-1 at this boundary, while HostValue text is
// UTF-8. Never publish arbitrary peer bytes as an unchecked text completion.
std::string header_text(std::string_view bytes) {
    std::string text;
    for (const unsigned char c : bytes) {
        if (c < 128)
            text += static_cast<char>(c);
        else {
            text += static_cast<char>(0xc0 | (c >> 6));
            text += static_cast<char>(0x80 | (c & 63));
        }
    }
    return text;
}
std::string header_bytes(std::string_view text) {
    std::string bytes;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 128)
            bytes += static_cast<char>(first);
        else {
            if ((first != 0xc2 && first != 0xc3) || i + 1 == text.size()) reject(host_error::invalid_input);
            const auto second = static_cast<unsigned char>(text[++i]);
            if (second < 0x80 || second > 0xbf) reject(host_error::invalid_input);
            bytes += static_cast<char>(((first & 3) << 6) | (second & 63));
        }
    }
    return bytes;
}
std::string normalize_path(std::string input) {
    std::string output;
    const auto parent = [&] {
        const auto slash = output.rfind('/');
        output.erase(slash == std::string::npos ? 0 : slash);
    };
    while (!input.empty()) {
        if (input.starts_with("../"))
            input.erase(0, 3);
        else if (input.starts_with("./"))
            input.erase(0, 2);
        else if (input.starts_with("/./"))
            input.erase(0, 2);
        else if (input == "/.")
            input = "/";
        else if (input.starts_with("/../")) {
            input.erase(0, 3);
            parent();
        } else if (input == "/..") {
            input = "/";
            parent();
        } else if (input == "." || input == "..")
            input.clear();
        else {
            const auto next = input.find('/', input.front() == '/' ? 1 : 0);
            output += input.substr(0, next);
            input.erase(0, next == std::string::npos ? input.size() : next);
        }
    }
    return output.empty() ? "/" : output;
}
bool has_scheme(std::string_view reference) {
    const auto colon = reference.find(':');
    if (colon == std::string_view::npos || colon > reference.find_first_of("/?#") || colon == 0) return false;
    const auto alpha = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    if (!alpha(reference.front())) return false;
    for (const unsigned char c : reference.substr(1, colon - 1))
        if (!alpha(c) && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') return false;
    return true;
}
// A strict RFC 9112 chunk-extension parser: token names and token or quoted
// values, with optional BWS around delimiters. No obs-fold or bare controls.
void chunk_extensions(std::string_view text) {
    while (!text.empty()) {
        text = trim(text);
        if (text.empty() || text.front() != ';') reject(unknown);
        text.remove_prefix(1);
        text = trim(text);
        std::size_t size = 0;
        while (size < text.size() && token(text.substr(size, 1))) ++size;
        if (size == 0) reject(unknown);
        text.remove_prefix(size);
        text = trim(text);
        if (!text.empty() && text.front() == '=') {
            text.remove_prefix(1);
            text = trim(text);
            if (text.empty()) reject(unknown);
            if (text.front() == '"') {
                text.remove_prefix(1);
                bool ended = false;
                while (!text.empty()) {
                    const auto c = static_cast<unsigned char>(text.front());
                    text.remove_prefix(1);
                    if (c == '"') {
                        ended = true;
                        break;
                    }
                    if (c == '\\') {
                        if (text.empty()) reject(unknown);
                        const auto escaped = static_cast<unsigned char>(text.front());
                        text.remove_prefix(1);
                        if ((escaped < 32 && escaped != '\t') || escaped == 127) reject(unknown);
                    } else if ((c < 32 && c != '\t') || c == 127)
                        reject(unknown);
                }
                if (!ended) reject(unknown);
            } else {
                size = 0;
                while (size < text.size() && token(text.substr(size, 1))) ++size;
                if (!size) reject(unknown);
                text.remove_prefix(size);
            }
        }
        text = trim(text);
        if (!text.empty() && text.front() != ';') reject(unknown);
    }
}
struct Url {
    bool secure = false;
    std::string host, authority, target;
    std::int32_t port = 0;
    std::string origin() const;
};

struct Address {
    std::array<unsigned char, 16> bytes{};
    int family = AF_UNSPEC;

    explicit Address(const char* text) {
        if (::inet_pton(AF_INET, text, bytes.data()) == 1) family = AF_INET;
        else if (::inet_pton(AF_INET6, text, bytes.data()) == 1) family = AF_INET6;
    }

    bool operator==(const Address&) const = default;

    // An IPv4-mapped IPv6 address connects to its IPv4 address.
    Address unmapped() const {
        if (family != AF_INET6 || !std::all_of(bytes.begin(), bytes.begin() + 10, [](auto b) { return b == 0; }) ||
            bytes[10] != 0xff || bytes[11] != 0xff)
            return *this;
        Address result("");
        result.family = AF_INET;
        std::copy(bytes.begin() + 12, bytes.end(), result.bytes.begin());
        return result;
    }
};

// The host without the trailing dot of an absolute name. `1.2.3.4.` is the
// address 1.2.3.4 both in its origin and in the origin filter, so a listed
// numeric origin still allows exactly that address.
std::string bare_host(const std::string& host) {
    return host.size() > 1 && host.back() == '.' ? host.substr(0, host.size() - 1) : host;
}

std::string Url::origin() const {
    const auto bare = bare_host(host);
    auto name = lower(bare);
    const Address address(bare.c_str());
    if (address.family != AF_UNSPEC) {
        char numeric[INET6_ADDRSTRLEN];
        if (::inet_ntop(address.family, address.bytes.data(), numeric, sizeof numeric)) name = numeric;
        if (address.family == AF_INET6) name = "[" + name + "]";
    }
    return std::string(secure ? "https://" : "http://") + name + ":" + std::to_string(port);
}

bool global_ipv4(const unsigned char* bytes) {
    const auto address = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
                         (std::uint32_t(bytes[2]) << 8) | bytes[3];
    // IANA IPv4 Special-Purpose Address Registry, including globally
    // reachable anycast exceptions to the IETF assignments block.
    if (address == 0xc0000009 || address == 0xc000000a) return true;
    constexpr std::pair<std::uint32_t, unsigned> nonglobal[] = {
        {0x00000000, 8}, {0x0a000000, 8}, {0x64400000, 10}, {0x7f000000, 8},
        {0xa9fe0000, 16}, {0xac100000, 12}, {0xc0000000, 24}, {0xc0000200, 24},
        {0xc0586300, 24}, {0xc0a80000, 16}, {0xc6120000, 15}, {0xc6336400, 24},
        {0xcb007100, 24}, {0xe0000000, 3},
    };
    return std::ranges::none_of(nonglobal, [address](const auto& block) {
        const auto [network, prefix] = block;
        return (address >> (32 - prefix)) == (network >> (32 - prefix));
    });
}

bool global_address(const Address& address) {
    const auto& bytes = address.bytes;
    if (address.family == AF_INET) return global_ipv4(bytes.data());
    if (address.family != AF_INET6) return false;
    if (std::all_of(bytes.begin(), bytes.begin() + 10, [](auto byte) { return byte == 0; }) &&
        bytes[10] == 0xff && bytes[11] == 0xff) return global_ipv4(bytes.data() + 12);
    const auto prefix = [&](const char* text, unsigned length) {
        const Address block(text);
        const auto whole = length / 8;
        const auto remaining = length % 8;
        return std::equal(bytes.begin(), bytes.begin() + whole, block.bytes.begin()) &&
               (remaining == 0 || (bytes[whole] >> (8 - remaining)) == (block.bytes[whole] >> (8 - remaining)));
    };
    if (prefix("64:ff9b::", 96)) return global_ipv4(bytes.data() + 12);
    // The global unicast block also contains IANA special-purpose ranges.
    if (!prefix("2000::", 3) || prefix("2001:db8::", 32) || prefix("2002::", 16) || prefix("3fff::", 20))
        return false;
    if (!prefix("2001::", 23)) return true;
    return prefix("2001:1::1", 128) || prefix("2001:1::2", 128) || prefix("2001:1::3", 128) ||
           prefix("2001:3::", 32) || prefix("2001:4:112::", 48) ||
           prefix("2001:20::", 28) || prefix("2001:30::", 28);
}

Url parse_url(std::string_view text) {
    if (text.size() > 8192) reject(too_large);
    for (const unsigned char c : text)
        if (c <= 32 || c >= 127 || c == '\\') reject(host_error::invalid_input);
    const auto separator = text.find("://");
    if (separator == std::string_view::npos) reject(host_error::invalid_input);
    const auto scheme = lower(text.substr(0, separator));
    if (scheme != "http" && scheme != "https") reject(host_error::unsupported);
    Url url;
    url.secure = scheme == "https";
    url.port = url.secure ? 443 : 80;
    text.remove_prefix(separator + 3);
    const auto end = text.find_first_of("/?#");
    const auto authority = text.substr(0, end);
    if (authority.empty() || authority.find('@') != std::string_view::npos) reject(host_error::invalid_input);
    std::string_view host = authority, port;
    if (host.front() == '[') {
        const auto close = host.find(']');
        if (close == std::string_view::npos) reject(host_error::invalid_input);
        const auto remainder = host.substr(close + 1);
        host = host.substr(1, close - 1);
        unsigned char raw[16];
        if (::inet_pton(AF_INET6, std::string(host).c_str(), raw) != 1) reject(host_error::invalid_input);
        if (!remainder.empty()) {
            if (remainder.front() != ':') reject(host_error::invalid_input);
            port = remainder.substr(1);
            if (port.empty()) reject(host_error::invalid_input);
        }
    } else {
        const auto colon = host.find(':');
        if (colon != std::string_view::npos) {
            port = host.substr(colon + 1);
            host = host.substr(0, colon);
            if (port.empty()) reject(host_error::invalid_input);
        }
        for (const unsigned char c : host)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
                reject(host_error::invalid_input);
    }
    if (host.empty()) reject(host_error::invalid_input);
    if (!port.empty()) {
        const auto value = number(port, 10, host_error::invalid_input);
        if (value == 0 || value > 65535) reject(host_error::invalid_input);
        url.port = static_cast<std::int32_t>(value);
    }
    url.host = std::string(host);
    url.authority = std::string(authority);
    auto target = end == std::string_view::npos ? std::string_view() : text.substr(end);
    target = target.substr(0, target.find('#'));
    url.target = target.empty() || target.front() != '/' ? "/" + std::string(target) : std::string(target);
    const auto query = url.target.find('?');
    url.target =
        normalize_path(url.target.substr(0, query)) + (query == std::string::npos ? "" : url.target.substr(query));
    return url;
}

using Origins = std::optional<std::vector<Url>>;

Url parse_origin(std::string_view text) {
    auto url = parse_url(text);
    const auto authority_end = text.find_first_of("/?#", text.find("://") + 3);
    if (authority_end != std::string_view::npos && text.substr(authority_end) != "/") reject(host_error::invalid_input);
    return url;
}

Origins parse_origins(const std::optional<std::vector<std::string>>& configured) {
    if (!configured) return std::nullopt;
    Origins origins(std::in_place);
    for (const auto& text : *configured) origins->push_back(parse_origin(text));
    return origins;
}

[[noreturn]] void deny_origin(const Url& url) {
    throw Failure{host_error::capability_denied, url.origin()};
}

client_socket::AddressFilter origin_filter(const Url& url, const Origins& origins) {
    if (!origins) return {};
    const auto origin = url.origin();
    if (std::ranges::none_of(*origins, [&](const auto& allowed) { return allowed.origin() == origin; }))
        deny_origin(url);
    // docs/runtime/NVM.md, "HTTP client origins": a listed numeric origin
    // allows exactly its address. A listed name allows its global addresses,
    // and another address only when a numeric origin with the same scheme
    // and port lists it. Each connect attempt checks its own candidate.
    const auto literal = Address(bare_host(url.host).c_str()).unmapped();
    return [&url, &origins, literal](std::string_view numeric) {
        const auto candidate = Address(std::string(numeric).c_str()).unmapped();
        if (candidate.family == AF_UNSPEC) return false;
        if (literal.family != AF_UNSPEC) return candidate == literal;
        if (global_address(candidate)) return true;
        return std::ranges::any_of(*origins, [&](const auto& allowed) {
            const auto listed = Address(bare_host(allowed.host).c_str()).unmapped();
            return allowed.secure == url.secure && allowed.port == url.port && listed.family != AF_UNSPEC &&
                   candidate == listed;
        });
    };
}
std::string redirected(const Url& base, std::string_view location) {
    if (has_scheme(location)) return std::string(location);
    const auto origin = std::string(base.secure ? "https://" : "http://") + base.authority;
    if (location.empty()) return origin + base.target;
    if (location.starts_with("//")) return std::string(base.secure ? "https:" : "http:") + std::string(location);
    if (location.starts_with('/')) return origin + std::string(location);
    if (location.starts_with('?')) return origin + base.target.substr(0, base.target.find('?')) + std::string(location);
    if (location.starts_with('#')) return origin + base.target + std::string(location);
    const auto path = base.target.substr(0, base.target.find('?'));
    return origin + path.substr(0, path.rfind('/') + 1) + std::string(location);
}
using Headers = std::vector<std::pair<std::string, std::string>>;
struct Request {
    std::string method, url, body;
    Headers headers;
    std::int64_t timeout = 0;
};
Request parse_request(const HostRequest& host) {
    const auto& args = host.arguments;
    Request result{args.at(0).bytes, args.at(1).bytes, args.at(3).bytes, {}, as_int64(args.at(4))};
    if (!token(result.method) || result.timeout < 0 || args.at(5).bits > 2) reject(host_error::invalid_input);
    const auto method = lower(result.method);
    if (method == "connect" || method == "trace" || method == "track") reject(host_error::invalid_input);
    if (method == "delete" || method == "get" || method == "head" || method == "options" || method == "post" ||
        method == "put")
        for (auto& c : result.method)
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if ((result.method == "GET" || result.method == "HEAD") && !result.body.empty()) reject(host_error::invalid_input);
    if (result.body.size() > body_limit) reject(too_large);
    const auto& fields = args.at(2).elements;
    if (fields.size() % 2 || fields.size() > 200) reject(host_error::invalid_input);
    std::size_t total = 0;
    for (std::size_t i = 0; i < fields.size(); i += 2) {
        const auto name = lower(fields[i].bytes);
        const auto& value = fields[i + 1].bytes;
        if (!token(name) || !field_value(value) || trim(value).size() != value.size() || name == "host" ||
            name == "content-length" || name == "transfer-encoding" || name == "connection" || name == "expect" ||
            name == "upgrade" || name == "proxy-connection" || name == "proxy-authorization" || name == "te" ||
            name == "trailer" || name == "keep-alive" || name == "http2-settings")
            reject(host_error::invalid_input);
        total += name.size() + value.size() + 4;
        if (total > head_limit) reject(too_large);
        result.headers.emplace_back(name, header_bytes(value));
    }
    (void)parse_url(result.url);
    return result;
}
struct Response {
    std::uint32_t status = 0;
    Headers headers;
    std::string body;
};

// TLS BIO writes may raise SIGPIPE: the fetch worker writes under
// SocketWrites (sigpipe_guard.hpp), a mask local to that thread.
class Connection {
    NativeSocket socket_ = invalid_native_socket;
    Clock::time_point deadline_;
    const std::atomic<bool>& cancelled_;
#ifdef NEURA_NVM_TLS
    SSL_CTX* context_ = nullptr;
    SSL* ssl_ = nullptr;
#endif
    void wait(short events) {
        std::uint32_t error = 0;
        if (client_socket::wait(socket_, events, deadline_, error, &cancelled_) <= 0) reject(error);
    }

  public:
    bool h2 = false;
    Connection(const Url& url, Clock::time_point deadline, const std::atomic<bool>& cancelled,
               const Origins& origins)
        : deadline_(deadline), cancelled_(cancelled) {
        if (url.secure) {
#ifndef NEURA_NVM_TLS
            reject(host_error::unsupported);
#endif
        }
        std::uint32_t error = 0;
        auto filter = origin_filter(url, origins);
        bool permitted_address = false;
        const client_socket::AddressFilter observe = !filter ? client_socket::AddressFilter{} :
            client_socket::AddressFilter([&](std::string_view numeric) {
                const bool allowed = filter(numeric);
                permitted_address |= allowed;
                return allowed;
            });
        socket_ = client_socket::connect(url.host, url.port, deadline, error, &cancelled, observe);
        if (socket_ == invalid_native_socket) {
            if (filter && !permitted_address && error == host_error::permission_denied) deny_origin(url);
            reject(error);
        }
        try {
#ifdef NEURA_NVM_TLS
            if (url.secure) {
                context_ = SSL_CTX_new(TLS_client_method());
                if (!context_ || !SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION) ||
                    !SSL_CTX_set_default_verify_paths(context_))
                    reject(host_error::permission_denied);
                SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
                ssl_ = SSL_new(context_);
                if (!ssl_ || static_cast<std::uint64_t>(socket_) > INT_MAX ||
                    !SSL_set_fd(ssl_, static_cast<int>(socket_)))
                    reject(host_error::other);
                X509_VERIFY_PARAM_set_hostflags(SSL_get0_param(ssl_), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
                unsigned char address[16];
                const bool numeric = ::inet_pton(AF_INET, url.host.c_str(), address) == 1 ||
                                     ::inet_pton(AF_INET6, url.host.c_str(), address) == 1;
                if (numeric ? !X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl_), url.host.c_str())
                            : !(SSL_set_tlsext_host_name(ssl_, url.host.c_str()) &&
                                SSL_set1_dnsname(ssl_, url.host.c_str())))
                    reject(host_error::invalid_input);
#ifdef NEURA_NVM_HTTP2
                const std::string alpn("\x02h2\x08http/1.1", 12);
#else
                const std::string alpn("\x08http/1.1", 9);
#endif
                if (SSL_set_alpn_protos(ssl_, reinterpret_cast<const unsigned char*>(alpn.data()), alpn.size()) != 0)
                    reject(host_error::other);
                for (;;) {
                    check();
                    ERR_clear_error();
                    const int result = SSL_connect(ssl_);
                    if (result > 0) break;
                    tls_retry(result);
                }
                const unsigned char* protocol = nullptr;
                unsigned int length = 0;
                SSL_get0_alpn_selected(ssl_, &protocol, &length);
                h2 = length == 2 && std::memcmp(protocol, "h2", 2) == 0;
            }
#endif
        } catch (...) {
            close();
            throw;
        }
    }
    Connection(const Connection&) = delete;
    ~Connection() { close(); }
    void close() {
#ifdef NEURA_NVM_TLS
        if (ssl_) {
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (context_) {
            SSL_CTX_free(context_);
            context_ = nullptr;
        }
#endif
        if (socket_ != invalid_native_socket) {
            platform::close_socket(socket_);
            socket_ = invalid_native_socket;
        }
    }
    void check() const {
        if (cancelled_.load()) reject(host_error::cancelled);
        if (Clock::now() >= deadline_) reject(host_error::timed_out);
    }
#ifdef NEURA_NVM_TLS
    void tls_retry(int result) {
        const auto reason = SSL_get_error(ssl_, result);
        if (reason == SSL_ERROR_WANT_READ)
            wait(platform::poll_in);
        else if (reason == SSL_ERROR_WANT_WRITE)
            wait(platform::poll_out);
        else if (SSL_get_verify_result(ssl_) != X509_V_OK)
            reject(host_error::permission_denied);
        else
            reject(unknown);
    }
#endif
    void write(std::string_view bytes) {
        while (!bytes.empty()) {
            check();
            int count;
#ifdef NEURA_NVM_TLS
            if (ssl_) {
                ERR_clear_error();
                count = SSL_write(ssl_, bytes.data(), static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX)));
                if (count <= 0) {
                    tls_retry(count);
                    continue;
                }
            } else
#endif
            {
#ifdef MSG_NOSIGNAL
                constexpr int flags = MSG_NOSIGNAL;
#else
                constexpr int flags = 0;
#endif
                count = static_cast<int>(::send(socket_, bytes.data(),
                                                static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX)), flags));
                if (count <= 0) {
                    const auto error = platform::last_socket_error();
                    if (error.interrupted()) continue;
                    if (error.would_block()) {
                        wait(platform::poll_out);
                        continue;
                    }
                    reject(unknown);
                }
            }
            bytes.remove_prefix(static_cast<std::size_t>(count));
        }
    }
    std::string read() {
        std::array<char, 16384> buffer;
        for (;;) {
            check();
            int count;
#ifdef NEURA_NVM_TLS
            if (ssl_) {
                ERR_clear_error();
                count = SSL_read(ssl_, buffer.data(), static_cast<int>(buffer.size()));
                if (count <= 0) {
                    if (SSL_get_error(ssl_, count) == SSL_ERROR_ZERO_RETURN) return {};
                    tls_retry(count);
                    continue;
                }
            } else
#endif
            {
                count = static_cast<int>(::recv(socket_, buffer.data(), buffer.size(), 0));
                if (count == 0) return {};
                if (count < 0) {
                    const auto error = platform::last_socket_error();
                    if (error.interrupted()) continue;
                    if (error.would_block()) {
                        wait(platform::poll_in);
                        continue;
                    }
                    reject(unknown);
                }
            }
            return std::string(buffer.data(), static_cast<std::size_t>(count));
        }
    }
};

class Input {
    Connection& connection_;
    std::string pending_;

  public:
    explicit Input(Connection& connection) : connection_(connection) {}
    std::string line(std::size_t& budget) {
        for (;;) {
            const auto end = pending_.find("\r\n");
            if (end != std::string::npos) {
                if (end + 2 > budget) reject(too_large);
                std::string result = pending_.substr(0, end);
                pending_.erase(0, end + 2);
                budget -= end + 2;
                if (result.find_first_of("\r\n") != std::string::npos) reject(unknown);
                return result;
            }
            if (pending_.size() >= budget) reject(too_large);
            const auto more = connection_.read();
            if (more.empty()) reject(unknown);
            pending_ += more;
        }
    }
    void take(std::uint64_t count, std::string& output) {
        if (count > body_limit - output.size()) reject(too_large);
        while (count) {
            if (pending_.empty()) {
                pending_ = connection_.read();
                if (pending_.empty()) reject(unknown);
            }
            const auto size = std::min<std::uint64_t>(count, pending_.size());
            output.append(pending_, 0, size);
            pending_.erase(0, size);
            count -= size;
        }
    }
    void to_end(std::string& output) {
        for (;;) {
            if (pending_.size() > body_limit - output.size()) reject(too_large);
            output += pending_;
            pending_ = connection_.read();
            if (pending_.empty()) return;
        }
    }
};
void validate_response_fields(const Headers& headers, std::optional<std::uint64_t>& length, bool& chunked,
                              bool expects_body = true) {
    for (const auto& [name, value] : headers) {
        if (name == "content-length") {
            if (length) reject(unknown);
            length = number(trim(value), 10);
        }
        if (name == "transfer-encoding") {
            if (chunked || lower(trim(value)) != "chunked") reject(unknown);
            chunked = true;
        }
    }
    if (length && chunked) reject(unknown);
    if (expects_body && length && *length > body_limit) reject(too_large);
}
Response http1(Connection& connection, const Url& url, const Request& request) {
    std::string head =
        request.method + " " + url.target + " HTTP/1.1\r\nHost: " + url.authority + "\r\nConnection: close\r\n";
    for (const auto& [name, value] : request.headers) head += name + ": " + value + "\r\n";
    head += "Content-Length: " + std::to_string(request.body.size()) + "\r\n\r\n";
    if (head.size() > head_limit) reject(too_large);
    connection.write(head);
    connection.write(request.body);
    Input input(connection);
    Response response;
    std::optional<std::uint64_t> length;
    bool chunked = false;
    for (int interim = 0;; ++interim) {
        if (interim > 8) reject(unknown);
        std::size_t budget = head_limit;
        const auto status = input.line(budget);
        if (status.size() < 12 || (!status.starts_with("HTTP/1.1 ") && !status.starts_with("HTTP/1.0 ")) ||
            (status.size() > 12 && status[12] != ' ') || !field_value(status))
            reject(unknown);
        response.status = static_cast<std::uint32_t>(number(std::string_view(status).substr(9, 3), 10));
        if (response.status < 100 || response.status > 599 || response.status == 101) reject(unknown);
        response.headers.clear();
        for (;;) {
            const auto line = input.line(budget);
            if (line.empty()) break;
            const auto colon = line.find(':');
            if (colon == std::string::npos || !token(std::string_view(line).substr(0, colon)) ||
                !field_value(std::string_view(line).substr(colon + 1)))
                reject(unknown);
            if (response.headers.size() >= 100) reject(too_large);
            response.headers.emplace_back(lower(std::string_view(line).substr(0, colon)),
                                          trim(std::string_view(line).substr(colon + 1)));
        }
        length.reset();
        chunked = false;
        validate_response_fields(response.headers, length, chunked,
                                 request.method != "HEAD" && response.status >= 200 && response.status != 204 &&
                                     response.status != 304);
        if (response.status == 204 && (length || chunked)) reject(unknown);
        if (response.status >= 200) break;
        if (chunked || length) reject(unknown);
    }
    if (request.method == "HEAD" || response.status == 204 || response.status == 304) return response;
    if (chunked) {
        for (;;) {
            std::size_t line_budget = head_limit;
            const auto line = input.line(line_budget);
            const auto semi = line.find(';');
            auto digits = std::string_view(line).substr(0, semi);
            if (semi != std::string::npos)
                while (!digits.empty() && (digits.back() == ' ' || digits.back() == '\t')) digits.remove_suffix(1);
            const auto count = number(digits, 16);
            if (semi != std::string::npos) chunk_extensions(std::string_view(line).substr(semi));
            if (count == 0) break;
            input.take(count, response.body);
            std::string ending;
            input.take(2, ending);
            if (ending != "\r\n") reject(unknown);
        }
        std::size_t trailer_budget = head_limit;
        for (std::size_t fields = 0;; ++fields) {
            const auto line = input.line(trailer_budget);
            if (line.empty()) break;
            const auto colon = line.find(':');
            if (fields >= 100 || colon == std::string::npos || !token(std::string_view(line).substr(0, colon)) ||
                !field_value(std::string_view(line).substr(colon + 1)))
                reject(unknown);
            const auto name = lower(std::string_view(line).substr(0, colon));
            if (name == "content-length" || name == "transfer-encoding" || name == "host") reject(unknown);
        }
    } else if (length)
        input.take(*length, response.body);
    else
        input.to_end(response.body);
    return response;
}

#ifdef NEURA_NVM_HTTP2
class H2 {
    nghttp2_session* session_ = nullptr;
    const Request& request_;
    std::size_t offset_ = 0, header_bytes_ = 0;
    Headers block_;
    std::uint32_t block_status_ = 0, failure_ = 0;
    bool response_seen_ = false, final_head_ = false, closed_ = false;
    Response response_;
    static H2& of(void* user) { return *static_cast<H2*>(user); }
    template <class F> int callback(F function) {
        try {
            function();
            return 0;
        } catch (const Failure& failure) {
            failure_ = failure.code;
        } catch (...) {
            failure_ = host_error::other;
        }
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    static int header(nghttp2_session*, const nghttp2_frame* frame, const std::uint8_t* name, std::size_t name_size,
                      const std::uint8_t* value, std::size_t value_size, std::uint8_t, void* user) {
        auto& self = of(user);
        return self.callback([&] {
            if (frame->hd.stream_id != 1) reject(unknown);
            const std::string_view n(reinterpret_cast<const char*>(name), name_size);
            const std::string_view v(reinterpret_cast<const char*>(value), value_size);
            self.header_bytes_ += n.size() + v.size() + 4;
            if (self.header_bytes_ > head_limit || self.block_.size() >= 100) reject(too_large);
            if (n == ":status") {
                if (self.final_head_ || self.block_status_ || v.size() != 3) reject(unknown);
                self.block_status_ = static_cast<std::uint32_t>(number(v, 10));
                if (self.block_status_ < 100 || self.block_status_ > 599) reject(unknown);
            } else {
                if (!token(n) || lower(n) != n || !field_value(v) || n == "connection" || n == "transfer-encoding" ||
                    n == "upgrade" || n == "proxy-connection" || n == "keep-alive")
                    reject(unknown);
                self.block_.emplace_back(n, v);
            }
        });
    }
    static int frame(nghttp2_session*, const nghttp2_frame* frame, void* user) {
        auto& self = of(user);
        return self.callback([&] {
            // RFC 9113, 6.8: a GOAWAY whose last stream ID is below 1 states
            // that the peer did not process stream 1, whatever its error code.
            if (frame->hd.type == NGHTTP2_GOAWAY) {
                if (frame->goaway.last_stream_id < 1) reject(self.response_seen_ ? unknown : not_processed);
                if (frame->goaway.error_code != NGHTTP2_NO_ERROR) reject(unknown);
            }
            if (frame->hd.type != NGHTTP2_HEADERS || frame->hd.stream_id != 1) return;
            if (self.final_head_) {
                for (const auto& [name, value] : self.block_) {
                    (void)value;
                    if (name == "content-length") reject(unknown);
                }
            } else {
                if (!self.block_status_) reject(unknown);
                self.response_seen_ = true;
                std::optional<std::uint64_t> length;
                bool chunked = false;
                validate_response_fields(self.block_, length, chunked,
                                         self.request_.method != "HEAD" && self.block_status_ >= 200 &&
                                             self.block_status_ != 204 && self.block_status_ != 304);
                if (self.block_status_ == 204 && length) reject(unknown);
                if (self.block_status_ < 200) {
                    if (self.block_status_ == 101 || length) reject(unknown);
                } else {
                    self.response_.status = self.block_status_;
                    self.response_.headers = self.block_;
                    self.final_head_ = true;
                }
            }
            self.block_.clear();
            self.block_status_ = 0;
            self.header_bytes_ = 0;
        });
    }
    static int data(nghttp2_session*, std::uint8_t, std::int32_t stream, const std::uint8_t* data, std::size_t size,
                    void* user) {
        auto& self = of(user);
        return self.callback([&] {
            if (stream != 1 || !self.final_head_ || self.request_.method == "HEAD" || self.response_.status == 204 ||
                self.response_.status == 304)
                reject(unknown);
            if (size > body_limit - self.response_.body.size()) reject(too_large);
            self.response_.body.append(reinterpret_cast<const char*>(data), size);
        });
    }
    static int closed(nghttp2_session*, std::int32_t stream, std::uint32_t error, void* user) {
        auto& self = of(user);
        if (stream == 1) {
            self.closed_ = true;
            // REFUSED_STREAM before any response is "not processed" (RFC 9113, 8.7).
            if (error && !self.failure_)
                self.failure_ = error == NGHTTP2_REFUSED_STREAM && !self.response_seen_ ? not_processed : unknown;
        }
        return 0;
    }
    static int invalid(nghttp2_session*, const nghttp2_frame*, int, void* user) {
        of(user).failure_ = unknown;
        return 0;
    }
    static nghttp2_ssize body(nghttp2_session*, std::int32_t, std::uint8_t* output, std::size_t size,
                              std::uint32_t* flags, nghttp2_data_source*, void* user) {
        auto& self = of(user);
        const auto count = std::min(size, self.request_.body.size() - self.offset_);
        std::memcpy(output, self.request_.body.data() + self.offset_, count);
        self.offset_ += count;
        if (self.offset_ == self.request_.body.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
        return static_cast<nghttp2_ssize>(count);
    }

  public:
    explicit H2(const Request& request) : request_(request) {}
    ~H2() {
        if (session_) nghttp2_session_del(session_);
    }
    Response run(Connection& connection, const Url& url) {
        nghttp2_session_callbacks* callbacks = nullptr;
        if (nghttp2_session_callbacks_new(&callbacks) != 0) reject(host_error::other);
        nghttp2_session_callbacks_set_on_header_callback(callbacks, header);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, frame);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, data);
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, closed);
        nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(callbacks, invalid);
        const auto made = nghttp2_session_client_new(&session_, callbacks, this);
        nghttp2_session_callbacks_del(callbacks);
        if (made != 0) reject(host_error::other);
        const nghttp2_settings_entry settings[] = {{NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
                                                   {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, head_limit}};
        if (nghttp2_submit_settings(session_, 0, settings, 2) != 0) reject(host_error::other);
        Headers fields{{":method", request_.method},
                       {":scheme", url.secure ? "https" : "http"},
                       {":authority", url.authority},
                       {":path", url.target}};
        fields.insert(fields.end(), request_.headers.begin(), request_.headers.end());
        fields.emplace_back("content-length", std::to_string(request_.body.size()));
        std::vector<nghttp2_nv> values;
        for (auto& [name, value] : fields)
            values.push_back({reinterpret_cast<std::uint8_t*>(name.data()),
                              reinterpret_cast<std::uint8_t*>(value.data()), name.size(), value.size(),
                              NGHTTP2_NV_FLAG_NONE});
        nghttp2_data_provider2 source{};
        source.read_callback = body;
        if (nghttp2_submit_request2(session_, nullptr, values.data(), values.size(),
                                    request_.body.empty() ? nullptr : &source, nullptr) != 1)
            reject(host_error::other);
        while (!closed_) {
            connection.check();
            // One write per turn: separate small writes (two WINDOW_UPDATE
            // frames, say) would wait on Nagle and delayed acknowledgement.
            std::string output;
            for (;;) {
                const std::uint8_t* bytes = nullptr;
                const auto size = nghttp2_session_mem_send2(session_, &bytes);
                if (size < 0) reject(failure_ ? failure_ : unknown);
                if (size == 0) break;
                output.append(reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size));
            }
            if (!output.empty()) connection.write(output);
            if (failure_) reject(failure_);
            const auto bytes = connection.read();
            if (bytes.empty()) reject(unknown);
            const auto consumed =
                nghttp2_session_mem_recv2(session_, reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
            if (failure_) reject(failure_);
            if (consumed < 0 || static_cast<std::size_t>(consumed) != bytes.size()) reject(unknown);
        }
        if (failure_) reject(failure_);
        if (!final_head_) reject(unknown);
        std::optional<std::uint64_t> length;
        bool chunked = false;
        validate_response_fields(response_.headers, length, chunked,
                                 request_.method != "HEAD" && response_.status != 204 && response_.status != 304);
        if (request_.method != "HEAD" && response_.status != 204 && response_.status != 304 && length &&
            *length != response_.body.size())
            reject(unknown);
        return std::move(response_);
    }
};
#endif

HostCompletion exchange(Request request, Clock::time_point deadline, const std::atomic<bool>& cancelled,
                        const Origins& origins) {
    [[maybe_unused]] SocketWrites protection;
    std::set<std::pair<std::string, std::string>> visited;
    for (int redirects = 0;; ++redirects) {
        const auto url = parse_url(request.url);
        if (redirects > 10 || !visited.emplace(request.method, url.origin() + url.target).second) reject(unknown);
        Connection connection(url, deadline, cancelled, origins);
        Response response;
#ifdef NEURA_NVM_HTTP2
        if (connection.h2)
            response = H2(request).run(connection, url);
        else
#endif
            response = http1(connection, url, request);
        std::optional<std::string_view> location;
        if (response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 ||
            response.status == 308) {
            for (const auto& [name, value] : response.headers) {
                if (name == "location") {
                    if (location) reject(unknown);
                    location = value;
                }
            }
        }
        if (location) {
            // Redirect policy (docs/runtime/NVM.md, "HTTP client services"):
            // never HTTPS to HTTP; across origins keep only the safe fields
            // and never resend a body (307, 308, or 301 and 302 other than
            // POST would). The next hop is checked against the origin list.
            const auto next = redirected(url, *location);
            const auto next_url = parse_url(next);
            const bool to_get = (response.status == 303 && request.method != "HEAD") ||
                                ((response.status == 301 || response.status == 302) && request.method == "POST");
            if (url.secure && !next_url.secure) reject(redirect_downgrade);
            if (next_url.origin() != url.origin()) {
                if (!to_get && !request.body.empty()) reject(redirect_body);
                std::erase_if(request.headers, [](const auto& field) {
                    return field.first != "accept" && field.first != "accept-language" &&
                           field.first != "content-language";
                });
            }
            if (to_get) {
                request.method = "GET";
                request.body.clear();
                std::erase_if(request.headers, [](const auto& field) {
                    return field.first == "content-type" || field.first == "content-encoding";
                });
            }
            request.url = next;
            continue;
        }
        HostValue headers;
        for (auto& [name, value] : response.headers) {
            headers.elements.push_back(bytes_value(std::move(name)));
            headers.elements.push_back(bytes_value(header_text(value)));
        }
        return HostCompletion::ok(
            tuple_value({scalar(response.status), std::move(headers), bytes_value(std::move(response.body))}));
    }
}
struct FetchState {
    std::mutex mutex;
    std::condition_variable ready;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> finished{false};
    std::shared_ptr<ExternalSignal> signal = std::make_shared<ExternalSignal>();
    std::optional<HostCompletion> answer;
    Clock::time_point deadline;
};
std::uint64_t process_id() {
#ifdef _WIN32
    return ::GetCurrentProcessId();
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}
struct FetchWorkers {
    struct Worker {
        std::shared_ptr<FetchState> state;
        std::thread thread;
    };
    const std::uint64_t owner = process_id();
    std::mutex mutex;
    std::array<Worker, 16> slots;
};
std::atomic<FetchWorkers*> workers{nullptr};
std::atomic<bool> stopping_workers{false};

void stop_fetch_workers() {
    stopping_workers.store(true);
    auto* pool = workers.load();
    if (!pool || pool->owner != process_id()) return;
    std::lock_guard lock(pool->mutex);
    for (auto& slot : pool->slots)
        if (slot.state) slot.state->cancelled.store(true);
    for (auto& slot : pool->slots) {
        if (!slot.thread.joinable()) continue;
        slot.thread.join();
        slot.state.reset();
    }
}

FetchWorkers& fetch_workers() {
    // At process exit, cancel and join every live fetch before the TLS
    // library's own exit handler runs. Handlers run in reverse order of
    // registration, so initialize the TLS library (which registers its
    // cleanup) before registering this one. A fork child inherits both.
    static const bool registered = [] {
#ifdef NEURA_NVM_TLS
        if (OPENSSL_init_ssl(0, nullptr) != 1) return false;
#endif
        return std::atexit(stop_fetch_workers) == 0;
    }();
    if (!registered) reject(host_error::other);
    auto* pool = workers.load();
    // A fork child must leave its parent's mutex and threads alone.
    if (!pool || pool->owner != process_id()) {
        auto replacement = std::make_unique<FetchWorkers>();
        if (workers.compare_exchange_strong(pool, replacement.get())) pool = replacement.release();
    }
    return *pool;
}
std::shared_ptr<FetchState> start_fetch(const HostRequest& host, bool allowed, bool detailed,
                                        const std::shared_ptr<const Origins>& origins) {
    if (!allowed) reject(host_error::capability_denied);
    const auto started = Clock::now();
    auto request = parse_request(host);
    auto state = std::make_shared<FetchState>();
    const auto duration = std::chrono::duration_cast<Clock::duration>(
        std::chrono::nanoseconds(request.timeout ? request.timeout : default_timeout));
    state->deadline = duration < Clock::time_point::max() - started ? started + duration : Clock::time_point::max();
    auto& pool = fetch_workers();
    std::lock_guard lock(pool.mutex);
    if (stopping_workers.load()) reject(host_error::cancelled);
    FetchWorkers::Worker* available = nullptr;
    for (auto& slot : pool.slots) {
        if (slot.state && slot.state->finished.load()) {
            slot.thread.join();
            slot.state.reset();
        }
        if (!available && !slot.state) available = &slot;
    }
    if (!available) reject(busy);
    try {
        available->thread = std::thread([state, request = std::move(request), origins, detailed]() mutable {
            HostCompletion answer;
            try {
                answer = exchange(std::move(request), state->deadline, state->cancelled, *origins);
                if (detailed) answer.value.elements.push_back(bytes_value(""));
            } catch (const Failure& failure) {
                answer = fetch_error(failure.code, detailed, failure.origin);
            } catch (...) {
                answer = fetch_error(host_error::other, detailed);
            }
            {
                std::lock_guard lock(state->mutex);
                state->answer = std::move(answer);
            }
            state->finished.store(true);
            state->ready.notify_one();
            state->signal->signal();
        });
        available->state = state;
    } catch (...) {
        reject(host_error::other);
    }
    return state;
}
struct FetchOwner {
    std::shared_ptr<FetchState> state;
    explicit FetchOwner(std::shared_ptr<FetchState> value) : state(std::move(value)) {}
    ~FetchOwner() { state->cancelled.store(true); }
};
} // namespace

void add_http_fetch_services(const OverlapAdder& add, const Capabilities& capabilities) {
    const auto allowed = capabilities.http_client;
    // A list that does not parse allows no origin. Option and manifest
    // readers validate it first with valid_http_client_origin.
    auto parsed = std::make_shared<Origins>();
    try {
        *parsed = parse_origins(capabilities.http_client_origins);
    } catch (const Failure&) {
        parsed->emplace();
    }
    const std::shared_ptr<const Origins> configured = std::move(parsed);
    for (const bool detailed : {false, true}) {
        auto sig = signature("neura.http.fetch", {text(), text(), text_list(), byte_list(), int64(), uint32()},
                                   tuple({uint32(), text_list(), byte_list()}));
        if (detailed) {
            sig.version = 2;
            sig.success = tuple({uint32(), text_list(), byte_list(), text()});
        }
        const FunctionService::Handler invoke = [allowed, configured, detailed](const HostRequest& request) {
            try {
                const auto state = start_fetch(request, allowed, detailed, configured);
                std::unique_lock lock(state->mutex);
                if (!state->ready.wait_until(lock, state->deadline, [&] { return state->answer.has_value(); })) {
                    state->cancelled.store(true);
                    return fetch_error(host_error::timed_out, detailed);
                }
                return std::move(*state->answer);
            } catch (const Failure& failure) {
                return fetch_error(failure.code, detailed, failure.origin);
            } catch (...) {
                return fetch_error(host_error::other, detailed);
            }
        };
        const FunctionService::Begin begin =
            [allowed, configured, detailed](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
            try {
                const auto state = start_fetch(request, allowed, detailed, configured);
                std::shared_ptr<FetchOwner> owner;
                try {
                    owner = std::make_shared<FetchOwner>(state);
                } catch (...) {
                    state->cancelled.store(true);
                    throw;
                }
                PendingOperation pending;
                pending.wait_source = ExternalWait{state->signal};
                const auto left = std::max<std::int64_t>(
                    1, std::chrono::duration_cast<std::chrono::nanoseconds>(state->deadline - Clock::now()).count());
                pending.deadline = deadline_after(request.now, left);
                pending.attempt = [owner, detailed](bool cancelled) -> std::optional<HostCompletion> {
                    auto& state = *owner->state;
                    std::lock_guard lock(state.mutex);
                    if (state.answer) return std::move(*state.answer);
                    if (cancelled) {
                        state.cancelled.store(true);
                        return cancelled_answer();
                    }
                    return std::nullopt;
                };
                pending.expire = [owner, detailed] {
                    owner->state->cancelled.store(true);
                    return fetch_error(host_error::timed_out, detailed);
                };
                return pending;
            } catch (const Failure& failure) {
                return fetch_error(failure.code, detailed, failure.origin);
            } catch (...) {
                return fetch_error(host_error::other, detailed);
            }
        };
        add(sig, invoke, begin);
    }
}

bool valid_http_client_origin(std::string_view text) {
    try {
        (void)parse_origin(text);
        return true;
    } catch (const Failure&) {
        return false;
    }
}
} // namespace neura::nvm::provider

// Focused loopback mDNS admission/window regression. Includes the provider
// to test the pure subnet policy alongside real socket ancillary delivery.
#include "nvm/socket_provider.cpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

using namespace neura::nvm;
using namespace neura::nvm::provider;

in_addr address_of(const char* text) {
    in_addr result{};
    assert(::inet_pton(AF_INET, text, &result) == 1);
    return result;
}

void subnet_policy() {
    MdnsLinks links;
    links.subnets = {{1, address_of("192.0.2.1").s_addr, address_of("255.255.255.0").s_addr},
                     {2, address_of("198.51.100.1").s_addr, address_of("255.255.255.0").s_addr},
                     {1, address_of("203.0.113.1").s_addr, address_of("255.255.255.0").s_addr}};
    assert(links.allows(address_of("192.0.2.55"), 1));
    assert(links.allows(address_of("203.0.113.55"), 1));
    assert(!links.allows(address_of("198.51.100.55"), 1));
    assert(links.allows(address_of("198.51.100.55"), 2));
    assert(!links.allows(address_of("192.0.3.55"), 1));
    assert(!links.allows(address_of("192.0.2.55"), 0));
    assert(!links.allows(address_of("0.0.0.0"), 1));
    assert(!links.allows(address_of("224.0.0.251"), 1));
    assert(!links.allows(address_of("255.255.255.255"), 1));
    MdnsLinks actual;
    assert(!actual.load());
    bool loopback = false;
    for (const auto& subnet : actual.subnets) {
        if (subnet.address == address_of("127.0.0.1").s_addr) {
            assert(actual.allows(address_of("127.0.0.1"), subnet.interface_index));
            loopback = true;
        }
    }
    assert(loopback);
    std::cout << "PASS: ingress subnet policy, overlay aliases, wrong interface, off-link and invalid sources\n";
}

// Returns false when the host cannot provide loopback multicast on 5353.
bool exchange_and_window() {
    const ScopedSocket responder{::socket(AF_INET, SOCK_DGRAM, 0)};
    const ScopedSocket noise{::socket(AF_INET, SOCK_DGRAM, 0)};
    assert(responder.descriptor >= 0 && noise.descriptor >= 0);
    const int enabled = 1;
    assert(set_native_option(responder.descriptor, SOL_SOCKET, SO_REUSEADDR, enabled));
#ifdef SO_REUSEPORT
    assert(set_native_option(responder.descriptor, SOL_SOCKET, SO_REUSEPORT, enabled));
#endif
    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(5353);
    group.sin_addr = address_of("224.0.0.251");
    const ip_mreq membership{group.sin_addr, address_of("127.0.0.1")};
    // The port can be held exclusively by a system responder, and some
    // sandboxes have no loopback multicast: report SKIP, never a pass.
    if (::bind(responder.descriptor, reinterpret_cast<sockaddr*>(&group), sizeof group) != 0 ||
        !set_native_option(responder.descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, membership)) {
        std::cout << "SKIP: loopback multicast on 224.0.0.251:5353 is unavailable: " << std::strerror(errno) << "\n";
        return false;
    }
    assert(set_native_option(responder.descriptor, IPPROTO_IP, IP_MULTICAST_IF, membership.imr_interface));
    assert(set_native_option(noise.descriptor, IPPROTO_IP, IP_MULTICAST_IF, membership.imr_interface));
    const std::string query("\0\0\0\0\0\1\0\0\0\0\0\0\x04h4md\x05local\0\0\1\0\1", 28);
    auto response = query;
    response[2] = static_cast<char>(0x84);
    auto wrong_port = response;
    wrong_port[3] = 1;
    auto wrong_destination = response;
    wrong_destination[3] = 2;
    auto unicast = group;
    unicast.sin_addr = membership.imr_interface;
    auto ephemeral = unicast;
    ephemeral.sin_port = 0;
    assert(::bind(noise.descriptor, reinterpret_cast<sockaddr*>(&ephemeral), sizeof ephemeral) == 0);
    const auto send = [](int fd, const std::string& bytes, const sockaddr_in& to) {
        assert(::sendto(fd, bytes.data(), bytes.size(), MSG_DONTWAIT,
                        reinterpret_cast<const sockaddr*>(&to), sizeof to) == static_cast<ssize_t>(bytes.size()));
    };
    bool answered = false;
    std::jthread worker([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            pollfd ready{responder.descriptor, POLLIN, 0};
            if (::poll(&ready, 1, 10) <= 0) continue;
            std::array<char, 128> data{};
            const auto got = ::recv(responder.descriptor, data.data(), data.size(), MSG_DONTWAIT);
            if (got < 0 || std::string_view(data.data(), static_cast<std::size_t>(got)) != query) continue;
            send(noise.descriptor, wrong_port, group);
            send(responder.descriptor, wrong_destination, unicast);
            send(responder.descriptor, response, group);
            answered = true;
            return;
        }
    });
    const auto answer = multicast_exchange(query, 150'000'000, "127.0.0.1");
    worker.request_stop();
    worker.join();
    assert(answered && answer.success);
    const auto contains = [&](const std::string& message) {
        return std::any_of(answer.value.elements.begin(), answer.value.elements.end(),
                           [&](const auto& item) { return item.bytes == message; });
    };
    assert(contains(response));
    assert(!contains(wrong_port));
    assert(!contains(wrong_destination));
    for (const auto window : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{5'000'000'001}, INT64_MAX}) {
        const auto result = multicast_exchange(query, window, "127.0.0.1");
        assert(!result.success && result.value.bits == host_error::invalid_input);
    }
    std::cout << "PASS: real loopback reply content, wrong port/destination rejection, zero/negative/>5s window rejection\n";
    return true;
}

int main() {
    subnet_policy();
    if (!exchange_and_window()) return 77;
}

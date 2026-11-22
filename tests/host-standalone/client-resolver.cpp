// The resolver for this executable handles only synthetic names and loopback.
#include "nvm/client_socket.hpp"
#include "platform/socket.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <thread>

using namespace neura::nvm::provider;
using namespace std::chrono_literals;

namespace {
std::atomic<unsigned> stalled{0};
std::atomic<bool> release_lookup{false};
std::atomic<unsigned> numeric_lookups{0};
}

extern "C" int __real_getaddrinfo(const char*, const char*, const addrinfo*, addrinfo**);
extern "C" int __wrap_getaddrinfo(const char* host, const char* service, const addrinfo* hints, addrinfo** result) {
    if ((hints->ai_flags & AI_NUMERICHOST) != 0) {
        ++numeric_lookups;
        return __real_getaddrinfo(host, service, hints, result);
    }
    if (std::string_view(host) == "stall.test") {
        ++stalled;
        while (!release_lookup.load()) std::this_thread::sleep_for(1ms);
        --stalled;
        return EAI_NONAME;
    }
    assert(std::string_view(host) == "loopback.test");
    addrinfo numeric = *hints;
    numeric.ai_flags |= AI_NUMERICHOST;
    return __real_getaddrinfo("127.0.0.1", service, &numeric, result);
}

int main() {
    const auto listener = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof local) == 0);
    assert(::listen(listener, 8) == 0);
    socklen_t length = sizeof local;
    assert(::getsockname(listener, reinterpret_cast<sockaddr*>(&local), &length) == 0);
    const auto port = ntohs(local.sin_port);
    const auto lookup = [=](const char* host, std::chrono::milliseconds timeout, std::uint32_t& error) {
        return client_socket::connect(host, port, client_socket::Clock::now() + timeout, error);
    };
    const auto success = [&](const char* host) {
        std::uint32_t error = 0;
        const auto opened = lookup(host, 1s, error);
        assert(opened != invalid_native_socket);
        ::close(opened);
    };
    const auto start_stalled = [&] {
        std::uint32_t error = 0;
        assert(lookup("stall.test", 150ms, error) == invalid_native_socket);
        assert(error == neura::nvm::host_error::timed_out);
    };

    start_stalled();
    assert(stalled.load() == 1);
    success("loopback.test");
    for (unsigned i = 1; i < 4; ++i) start_stalled();
    assert(stalled.load() == 4);
    success("127.0.0.1");
    success("localhost");
    // Numeric hosts, a zone included, never enter getaddrinfo, so a stalled
    // resolver cannot spend their deadline. Nothing listens on ::1 here.
    {
        std::uint32_t refused = 0;
        const auto before = client_socket::Clock::now();
        assert(lookup("::1%lo", 1s, refused) == invalid_native_socket);
        assert(lookup("::1%1", 1s, refused) == invalid_native_socket);
        assert(refused != neura::nvm::host_error::timed_out);
        assert(client_socket::Clock::now() - before < 500ms);
    }
    assert(numeric_lookups.load() == 0);
    std::uint32_t error = 0;
    const auto before = client_socket::Clock::now();
    assert(lookup("loopback.test", 30ms, error) == invalid_native_socket);
    assert(error == neura::nvm::host_error::timed_out);
    assert(client_socket::Clock::now() - before < 500ms);
    assert(stalled.load() == 4);

    const auto child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        ::alarm(3);
        success("loopback.test");
        ::_exit(0);
    }
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    release_lookup.store(true);
    const auto released_by = client_socket::Clock::now() + 1s;
    while (stalled.load() != 0 && client_socket::Clock::now() < released_by) std::this_thread::sleep_for(1ms);
    assert(stalled.load() == 0);
    success("loopback.test");
    ::close(listener);
}

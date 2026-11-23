// Link with -Wl,--wrap=getaddrinfo. The real provider enters a deliberately
// stalled system resolver; its caller must return before that worker does.
#include "nvm/providers.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

using namespace neura::nvm;
using namespace std::chrono_literals;
namespace {
std::atomic<bool> entered{false};
std::atomic<bool> release{false};
std::atomic<bool> finished{false};
HostValue text(std::string bytes) { return {0, std::move(bytes), {}}; }
HostValue number(std::uint64_t value) { return {value, {}, {}}; }
bool bounded_connect() {
    StringSink out, err;
    StringSource in("");
    Capabilities capabilities;
    capabilities.net = true;
    StandardServices services(out, err, in, capabilities);
    HostRegistry registry;
    services.register_all(registry);
    auto* connect = registry.find("neura.net.tls.connect", 1);
    const auto started = std::chrono::steady_clock::now();
    const auto answer = connect->invoke(HostRequest{0, 0, {text("deadline.test"), number(443), number(20'000'000),
        text("deadline.test"), HostValue{}, text("")}});
    return !answer.success && answer.value.bits == host_error::timed_out &&
           std::chrono::steady_clock::now() - started < 500ms;
}
// Under load the caller can pass its 20 ms deadline before it starts a
// resolver worker, which is a correct timeout that never reaches
// getaddrinfo, or the worker can start after the caller has returned. Every
// call must time out promptly; at least one worker must then be seen blocked
// in the stalled resolver, so the timeout came from not waiting for it.
enum class Stall { reached, escaped, never_entered };
Stall stalled_lookup() {
    entered = false;
    const auto limit = std::chrono::steady_clock::now() + 5s;
    do {
        if (!bounded_connect()) return Stall::escaped;
        const auto settle = std::chrono::steady_clock::now() + 200ms;
        while (!entered.load() && std::chrono::steady_clock::now() < settle) std::this_thread::sleep_for(1ms);
    } while (!entered.load() && std::chrono::steady_clock::now() < limit);
    return entered.load() ? Stall::reached : Stall::never_entered;
}
}
extern "C" int __real_getaddrinfo(const char*, const char*, const addrinfo*, addrinfo**);
extern "C" int __wrap_getaddrinfo(const char* host, const char* service, const addrinfo* hints, addrinfo** output) {
    if (std::strcmp(host, "deadline.test") != 0) return __real_getaddrinfo(host, service, hints, output);
    entered = true;
    while (!release.load()) std::this_thread::sleep_for(1ms);
    finished = true;
    return EAI_AGAIN;
}
int main() {
    const auto fail = [](const char* reason) { std::cerr << reason << '\n'; return 1; };
    const auto first = stalled_lookup();
    if (first == Stall::escaped || finished) return fail("DNS escaped its caller deadline");
    if (first == Stall::never_entered) return fail("lookup never entered the system resolver");
    if (!bounded_connect() || finished) return fail("busy resolver did not fail promptly");
    // An outstanding parent worker must not leave the forked child holding
    // a permanently busy resolver slot or a mutex owned by a vanished thread.
    const auto child = ::fork();
    if (child < 0) return fail("fork failed");
    if (child == 0) {
        if (stalled_lookup() != Stall::reached) _exit(1);
        release = true;
        while (!finished) std::this_thread::sleep_for(1ms);
        _exit(0);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
        release = true;
        return fail("child inherited the parent's blocked resolver");
    }
    release = true;
    while (!finished) std::this_thread::sleep_for(1ms);
    std::cout << "PASS DNS deadline, bounded worker count, fork recovery\n";
}

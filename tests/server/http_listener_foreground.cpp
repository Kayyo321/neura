#include "server/http_listener.hpp"

#include <cerrno>
#include <charconv>
#include <csignal>
#include <future>
#include <iostream>
#include <netinet/in.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace neura::server::http;
using namespace std::chrono_literals;

volatile std::sig_atomic_t reload_requested = 0;
volatile std::sig_atomic_t stop_requested = 0;

void signal_handler(int signal) {
    if (signal == SIGHUP) reload_requested = 1;
    else stop_requested = 1;
}

sigset_t install_handlers() {
    struct sigaction action{};
    action.sa_handler = signal_handler;
    ::sigemptyset(&action.sa_mask);
    sigset_t controls;
    ::sigemptyset(&controls);
    for (int signal : {SIGHUP, SIGINT, SIGTERM}) {
        if (::sigaction(signal, &action, nullptr) != 0) {
            throw std::system_error(errno, std::generic_category(), "install fixture signal handler");
        }
        ::sigaddset(&controls, signal);
    }
    return controls;
}

class BlockSignals {
public:
    explicit BlockSignals(const sigset_t& mask) {
        const int error = ::pthread_sigmask(SIG_BLOCK, &mask, &previous_);
        if (error) throw std::system_error(error, std::generic_category(), "block fixture signals");
    }
    ~BlockSignals() { ::pthread_sigmask(SIG_SETMASK, &previous_, nullptr); }

private:
    sigset_t previous_;
};

template<typename Number>
Number parse_number(std::string_view input) {
    Number value = 0;
    const auto [ptr, error] = std::from_chars(input.data(), input.data() + input.size(), value);
    if (error != std::errc{} || ptr != input.data() + input.size()) throw std::invalid_argument("invalid fixture number");
    return value;
}

struct Active {
    std::jthread thread;
    std::future<ServeResult> result;
};

void reap_finished(std::vector<Active>& active) {
    for (auto entry = active.begin(); entry != active.end();) {
        if (entry->result.wait_for(0ms) == std::future_status::ready) {
            entry->result.get();
            entry = active.erase(entry);
        } else {
            ++entry;
        }
    }
}

struct TestSocket {
    TestSocket() : descriptor(::socket(AF_INET, SOCK_STREAM, 0)) {
        if (descriptor < 0) throw std::runtime_error("could not make fixture socket");
    }
    TestSocket(const TestSocket&) = delete;
    TestSocket& operator=(const TestSocket&) = delete;
    ~TestSocket() { ::close(descriptor); }
    int descriptor;
};

sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

void check_ownership() {
    const ListenEndpoint endpoint{"127.0.0.1", 0, std::nullopt};
    const std::span one_endpoint(&endpoint, 1);
    ListenerSet occupied(one_endpoint);
    const auto first_port = [&] {
        ListenerSet temporary(one_endpoint);
        return temporary.endpoints()[0].port;
    }();
    const ListenEndpoint transaction[] = {
        {"127.0.0.1", first_port, std::nullopt}, {"127.0.0.1", occupied.endpoints()[0].port, std::nullopt},
    };
    bool bind_failed = false;
    try {
        ListenerSet incomplete(transaction);
    } catch (const std::system_error& error) {
        bind_failed = error.code().value() == EADDRINUSE;
    }
    const TestSocket reusable;
    const auto reusable_address = loopback(first_port);
    // This descriptor has no SO_REUSEADDR, so even an unlistening leaked bind would fail.
    if (!bind_failed || ::bind(reusable.descriptor, reinterpret_cast<const sockaddr*>(&reusable_address),
                               sizeof reusable_address) != 0) throw std::runtime_error("failed start retained a listener descriptor");

    ListenerLimits limits;
    limits.connections = 1;
    ListenerSet listener(one_endpoint, limits);
    if (listener.accept_until(ConnectionClock::now() + 5ms).status != AcceptStatus::TimedOut) {
        throw std::runtime_error("idle listener did not wait to its deadline");
    }
    const auto address = loopback(listener.endpoints()[0].port);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const TestSocket peer;
        if (::connect(peer.descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
            throw std::runtime_error("could not connect ownership test peer");
        }
        auto accepted = listener.accept_until(ConnectionClock::now() + 1s);
        if (accepted.status != AcceptStatus::Accepted || !accepted.connection) {
            throw std::runtime_error("dropping an undispatched socket retained its S-4 slot");
        }
        accepted.connection.reset();
        char byte;
        if (::recv(peer.descriptor, &byte, 1, 0) != 0) throw std::runtime_error("undispatched socket did not close");
    }
}

int run(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--ownership") {
        check_ownership();
        return 0;
    }
    if (argc < 5 || (argc - 2) % 3 != 0) throw std::invalid_argument("expected cap and endpoint triples");
    ListenerLimits limits;
    limits.connections = parse_number<std::size_t>(argv[1]);
    std::vector<ListenEndpoint> endpoints;
    for (int index = 2; index < argc; index += 3) {
        ListenEndpoint endpoint{"127.0.0.1", parse_number<std::uint16_t>(argv[index]), std::nullopt};
        if (std::string_view(argv[index + 1]) != "-" || std::string_view(argv[index + 2]) != "-") {
            endpoint.tls = TlsFiles{argv[index + 1], argv[index + 2]};
        }
        endpoints.push_back(std::move(endpoint));
    }

    const auto controls = install_handlers();
    std::vector<Active> active;
    ListenerSet listeners(endpoints, limits);
    std::cout << "READY";
    for (const auto& endpoint : listeners.endpoints()) std::cout << ' ' << endpoint.port;
    std::cout << std::endl;

    while (!stop_requested) {
        bool reload = false;
        {
            const BlockSignals blocked(controls);
            reload = reload_requested != 0;
            reload_requested = 0;
        }
        if (reload) {
            try {
                listeners.reload_tls();
                std::cout << "RELOADED" << std::endl;
            } catch (const std::exception&) {
                std::cout << "RELOAD_FAILED" << std::endl;
            }
        }
        reap_finished(active);
        auto accepted = listeners.accept_until(ConnectionClock::now() + 20ms);
        if (stop_requested) break;
        if (!accepted.connection) continue;
        if (accepted.connection->peer_address() != "127.0.0.1") throw std::runtime_error("wrong fixture peer address");
        std::packaged_task task([connection = std::move(*accepted.connection)]() mutable {
            const bool secure = connection.secure();
            const auto served = std::move(connection).serve([](const RequestHead&, std::string_view) {
                return BufferedResponse{200, "fixed"};
            });
            if (secure != served.tls.has_value()) throw std::runtime_error("incorrect fixture transport");
            return served;
        });
        auto result = task.get_future();
        // Let the foreground thread own all signal notifications.
        const BlockSignals blocked(controls);
        active.push_back({std::jthread(std::move(task)), std::move(result)});
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

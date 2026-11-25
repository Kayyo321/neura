// The QUIC provider of lane net-a milestone A1-0 (src/nvm/quic_provider.cpp),
// driven through the real PendingRequests driver: both peers in one process
// over loopback. Registered with CTest as host-quic-provider in TLS builds;
// argv[1] is the pinned openssl command, used to make a certificate.
//
// - A stream with a write pending (blocked by the peer's flow-control
//   window) refuses a second write with Busy, and still accepts a read.
// - A pending read whose connection another caller closes completes with
//   NotFound, the closed-handle code.
// - A closed connection's ID stays refused after a new connection reused
//   its slot; the new ID has the same slot and a new generation.
// - Live connections are serviced on every driver check (BackgroundWork):
//   data a completed write accepted reaches the peer while only the peer's
//   read is pending.
#include "nvm/provider_internal.hpp"
#include "nvm/driver.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unistd.h>

using namespace neura::nvm;
using namespace neura::nvm::provider;
namespace fs = std::filesystem;

namespace {

class LiveClock final : public TaskClock {
public:
    std::int64_t now() override {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start_).count();
    }
    void wait_until(std::int64_t deadline) override {
        std::this_thread::sleep_until(start_ + std::chrono::nanoseconds(deadline));
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

constexpr std::int64_t seconds = 1'000'000'000;
constexpr std::int64_t slot_mask = (std::int64_t{1} << 24) - 1;

HostValue text(std::string value) { return HostValue{0, std::move(value), {}}; }
HostValue texts(std::vector<std::string> values) {
    HostValue list;
    for (auto& value : values) list.elements.push_back(text(std::move(value)));
    return list;
}
HostValue number(std::int64_t value) { return scalar(static_cast<std::uint64_t>(value)); }

struct Fixture {
    StandardServices::Tables tables;
    Capabilities capabilities;
    std::vector<ResolvedPath> prefixes;
    std::map<std::string, FunctionService::Handler> call;
    std::map<std::string, FunctionService::Begin> begin;
    BackgroundWork* background = nullptr;
    LiveClock clock;
    PendingRequests pending{clock};
    std::uint64_t sequence = 0;
    std::map<std::uint64_t, HostCompletion> done;

    Fixture() {
        capabilities.net = true;
        capabilities.fs = true;
        const QuicAdder add = [&](HostSignature signature, FunctionService::Handler handler, BackgroundWork* work) {
            call[signature.name] = std::move(handler);
            background = work;
        };
        const QuicOverlapAdder overlap = [&](HostSignature signature, FunctionService::Handler handler,
                                             FunctionService::Begin start, BackgroundWork* work) {
            call[signature.name] = std::move(handler);
            begin[signature.name] = std::move(start);
            background = work;
        };
        add_quic_services(add, overlap, tables, capabilities, prefixes);
        assert(background != nullptr);
        pending.add_background(background);
    }
    ~Fixture() { close_quic(tables); }

    HostCompletion now(const std::string& name, std::vector<HostValue> args) {
        return call.at("neura.net.quic." + name)(HostRequest{0, 0, std::move(args)});
    }

    // Starts an overlapping request; returns its sequence number.
    std::uint64_t start(const std::string& name, std::vector<HostValue> args) {
        const auto id = sequence++;
        HostRequest request{id, 0, std::move(args)};
        request.now = clock.now();
        pending.add(id, begin.at("neura.net.quic." + name)(request), true);
        return id;
    }

    // Runs driver checks until every listed request has completed.
    void until(std::initializer_list<std::uint64_t> wanted) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        const auto complete = [&] {
            for (const auto id : wanted) if (!done.contains(id)) return false;
            return true;
        };
        while (!complete()) {
            assert(std::chrono::steady_clock::now() < deadline && "driver did not complete the requests");
            for (auto& answer : pending.wait(std::nullopt)) done.emplace(answer.sequence, answer.completion);
        }
    }

    // How many times the driver's wait returned within `milliseconds` while
    // nothing completes: a pending operation that made the driver spin would
    // give thousands.
    int wakeups(int milliseconds) {
        const auto end = clock.now() + std::int64_t{milliseconds} * 1'000'000;
        int count = 0;
        while (clock.now() < end) {
            for (auto& answer : pending.wait(end)) done.emplace(answer.sequence, answer.completion);
            ++count;
        }
        return count;
    }

    HostCompletion take(std::uint64_t id) {
        until({id});
        auto answer = done.at(id);
        done.erase(id);
        return answer;
    }
};

void expect_code(const HostCompletion& completion, std::uint32_t code, const char* what) {
    if (completion.success || completion.value.bits != code) {
        std::cerr << what << ": expected error " << code << ", got "
                  << (completion.success ? std::string("success") : std::to_string(completion.value.bits)) << '\n';
        std::exit(1);
    }
}

std::int64_t first_int(const HostCompletion& completion) {
    assert(completion.success);
    return as_int64(completion.value.elements.at(0));
}

} // namespace

int main(int argc, char** argv) {
    // Registered only in builds with the TLS provider, which serves QUIC.
    assert(argc == 2);
    const auto dir = fs::temp_directory_path() / ("neura-quic-provider-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const auto cert = (dir / "cert.pem").string();
    const auto key = (dir / "key.pem").string();
    const auto command = std::string(argv[1]) + " req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes "
                         "-days 2 -subj /CN=localhost -addext subjectAltName=DNS:localhost -keyout '" + key +
                         "' -out '" + cert + "' >/dev/null 2>&1";
    assert(std::system(command.c_str()) == 0);
    {
        Fixture f;
        const auto listened = f.now("listen", {text("127.0.0.1"), int32_value(0), text(cert), text(key),
                                               texts({"spike"})});
        assert(listened.success);
        const auto endpoint = as_int64(listened.value.elements.at(0));
        const auto port = as_int32(listened.value.elements.at(1));
        const auto connect_args = [&] {
            return std::vector<HostValue>{text("127.0.0.1"), int32_value(port), number(10 * seconds),
                                          text("localhost"), texts({"spike"}), text(cert)};
        };
        const auto accepting = f.start("accept", {number(endpoint), number(10 * seconds)});
        const auto quiet_accept = f.wakeups(300);
        std::cout << "  (accept with no client: " << quiet_accept << " driver wakeups in 300 ms)\n";
        assert(quiet_accept < 40 && f.done.empty());
        const auto connecting = f.start("connect", connect_args());
        const auto client = first_int(f.take(connecting));
        const auto server = first_int(f.take(accepting));

        // A stream the client opens and writes to, accepted by the server.
        const auto opened = f.now("open_stream", {number(client), scalar(1)});
        assert(opened.success);
        const auto stream = as_int64(opened.value);
        assert(f.take(f.start("stream_write", {number(client), number(stream), text("x"), scalar(0),
                                                number(10 * seconds)})).success);
        // Only the server's accept is pending now: the write above was
        // accepted into OpenSSL's buffer, and servicing sends it.
        const auto accepted = f.take(f.start("accept_stream", {number(server), number(10 * seconds)}));
        assert(accepted.success && as_int64(accepted.value.elements.at(0)) == stream);
        std::cout << "PASS a completed write reaches the peer while only the peer waits\n";

        // Check 1 of A1-0 at the driver: an accept_stream and reads on two
        // streams are pending at once; the client then makes all three ready
        // before the driver checks again, and one check completes all three.
        const auto second_open = f.now("open_stream", {number(client), scalar(1)});
        assert(second_open.success);
        const auto second = as_int64(second_open.value);
        assert(f.take(f.start("stream_write", {number(client), number(second), text("y"), scalar(0),
                                                number(10 * seconds)})).success);
        assert(f.take(f.start("accept_stream", {number(server), number(10 * seconds)})).success);
        for (const auto id : {stream, second}) {
            const auto first_bytes = f.take(f.start("stream_read", {number(server), number(id), int32_value(16),
                                                                    number(10 * seconds)}));
            assert(first_bytes.success);
        }
        const auto waiting_accept = f.start("accept_stream", {number(server), number(10 * seconds)});
        const auto waiting_first = f.start("stream_read", {number(server), number(stream), int32_value(16),
                                                           number(10 * seconds)});
        const auto waiting_second = f.start("stream_read", {number(server), number(second), int32_value(16),
                                                            number(10 * seconds)});
        for (auto& answer : f.pending.poll()) f.done.emplace(answer.sequence, answer.completion);
        assert(f.done.empty() && "nothing is ready before the client writes");
        const auto client_write = [&](std::int64_t id, const char* bytes) {
            HostRequest request{900, 0, {number(client), number(id), text(bytes), scalar(0), number(seconds)}};
            const auto started = f.begin.at("neura.net.quic.stream_write")(request);
            assert(std::holds_alternative<HostCompletion>(started) && std::get<HostCompletion>(started).success);
        };
        client_write(stream, "1");
        client_write(second, "2");
        const auto third_open = f.now("open_stream", {number(client), scalar(1)});
        assert(third_open.success);
        client_write(as_int64(third_open.value), "3");
        // The client flushes what it buffered (a new stream's first data is sent
        // by event processing, which a driver check would run), and the
        // datagrams reach the server's socket before the next check.
        for (int i = 0; i < 3; ++i) {
            f.background->run();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        const auto one_pass = f.pending.poll();
        if (one_pass.size() != 3) {
            std::cerr << "one driver check completed " << one_pass.size() << " of the 3 pending requests:";
            for (const auto& answer : one_pass) {
                std::cerr << ' ' << (answer.sequence == waiting_accept ? "accept" : answer.sequence == waiting_first
                    ? "first" : "second");
            }
            std::cerr << '\n';
            return 1;
        }
        for (const auto& answer : one_pass) {
            assert(answer.completion.success);
            assert(answer.sequence == waiting_accept || answer.sequence == waiting_first ||
                   answer.sequence == waiting_second);
        }
        std::cout << "PASS accept_stream and two reads pending together complete in one driver check\n";

        // An idle connection with a read pending on each side is quiet.
        const auto idle_server = f.start("stream_read", {number(server), number(second), int32_value(16),
                                                         number(10 * seconds)});
        const auto idle_client = f.start("stream_read", {number(client), number(second), int32_value(16),
                                                         number(10 * seconds)});
        const auto quiet_idle = f.wakeups(300);
        std::cout << "  (idle connection, a read pending on each side: " << quiet_idle << " wakeups in 300 ms)\n";
        assert(quiet_idle < 40 && !f.done.contains(idle_server) && !f.done.contains(idle_client));
        f.pending.cancel(idle_server);
        f.pending.cancel(idle_client);
        expect_code(f.take(idle_server), host_error::cancelled, "cancelled idle read");
        expect_code(f.take(idle_client), host_error::cancelled, "cancelled idle read");

        // A write larger than the peer's stream window stays pending; a second
        // write on that stream is refused at once, a read is not.
        const std::string big(2 * 1024 * 1024, 'b');
        const auto blocked = f.start("stream_write", {number(client), number(stream), text(big), scalar(0),
                                                      number(10 * seconds)});
        for (auto& answer : f.pending.poll()) f.done.emplace(answer.sequence, answer.completion);
        assert(!f.done.contains(blocked));
        expect_code(std::get<HostCompletion>(f.begin.at("neura.net.quic.stream_write")(
                        HostRequest{999, 0, {number(client), number(stream), text("y"), scalar(0), number(seconds)}})),
                    host_error::busy, "second write on a stream with a write pending");
        const auto client_read = f.start("stream_read", {number(client), number(stream), int32_value(16),
                                                         number(10 * seconds)});
        // While the peer's window fills, acknowledgments wake the driver; once
        // it is full and acknowledged, the blocked write is quiet.
        const auto filling = f.wakeups(1000);
        const auto quiet_blocked = f.wakeups(300);
        std::cout << "  (write blocked by flow control: " << filling << " wakeups while the window filled, then "
                  << quiet_blocked << " in 300 ms)\n";
        assert(quiet_blocked < 40 && !f.done.contains(blocked));
        std::size_t received = 0;
        while (received < big.size()) {
            const auto chunk = f.take(f.start("stream_read", {number(server), number(stream), int32_value(1 << 20),
                                                              number(10 * seconds)}));
            if (!chunk.success) {
                std::cerr << "server read failed with " << chunk.value.bits << " after " << received << " bytes\n";
                return 1;
            }
            received += chunk.value.elements.at(0).bytes.size();
        }
        const auto written = f.take(blocked);
        assert(written.success && as_int32(written.value.elements.at(0)) == static_cast<std::int32_t>(big.size()));
        std::cout << "PASS one pending write per stream; a read in the other direction runs meanwhile\n";

        // A pending read whose connection is closed by another caller.
        const auto orphan = f.start("stream_read", {number(server), number(stream), int32_value(16),
                                                    number(10 * seconds)});
        assert(f.now("close", {number(server), number(9), text("closing"), number(2 * seconds)}).success);
        expect_code(f.take(orphan), host_error::not_found, "pending read of a closed connection");
        std::cout << "PASS a pending read ends when another caller closes its connection\n";

        // The client's own read ends once the peer is gone (idle or close),
        // or is cancelled here; either way it is answered.
        f.pending.cancel(client_read);
        const auto client_read_answer = f.take(client_read);
        std::cout << "  (the client's read ended: " << (client_read_answer.success ? "success" : "error " +
                     std::to_string(client_read_answer.value.bits)) << ")\n";

        // A closed ID never reaches the object that reuses its slot.
        const auto closed_client = f.now("close", {number(client), number(0), text(""), number(2 * seconds)});
        if (!closed_client.success) {
            std::cerr << "client close failed with " << closed_client.value.bits << '\n';
            return 1;
        }
        const auto reconnect = f.start("connect", connect_args());
        const auto again = first_int(f.take(reconnect));
        assert((again & slot_mask) == (client & slot_mask) && again != client);
        expect_code(f.now("open_stream", {number(client), scalar(1)}), host_error::not_found,
                    "stale connection ID after its slot was reused");
        assert(f.now("open_stream", {number(again), scalar(1)}).success);
        std::cout << "PASS a reused slot gets a new generation; the stale ID is refused\n";
        assert(f.now("close", {number(endpoint), number(0), text(""), number(2 * seconds)}).success);

        // A client that sends its first flight and stops: the endpoint's
        // pending accept stays quiet instead of spinning.
        const auto second_listen = f.now("listen", {text("127.0.0.1"), int32_value(0), text(cert), text(key),
                                                    texts({"spike"})});
        assert(second_listen.success);
        const auto second_endpoint = as_int64(second_listen.value.elements.at(0));
        const auto second_port = as_int32(second_listen.value.elements.at(1));
        const auto stalled_accept = f.start("accept", {number(second_endpoint), number(10 * seconds)});
        {
            HostRequest stalled{901, 0, {text("127.0.0.1"), int32_value(second_port), number(10 * seconds),
                                         text("localhost"), texts({"spike"}), text(cert)}};
            stalled.now = f.clock.now();
            // Never added to the driver: the client's Initial is sent and nothing follows.
            auto never_driven = f.begin.at("neura.net.quic.connect")(stalled);
            assert(std::holds_alternative<PendingOperation>(never_driven));
            const auto quiet_handshake = f.wakeups(300);
            std::cout << "  (accept with a stalled client handshake: " << quiet_handshake << " wakeups in 300 ms)\n";
            assert(quiet_handshake < 40 && !f.done.contains(stalled_accept));

            // A good client meanwhile is accepted although the stalled one is
            // still in its handshake (RULINGS-06, bug 2).
            const auto good = f.start("connect", {text("127.0.0.1"), int32_value(second_port), number(10 * seconds),
                                                   text("localhost"), texts({"spike"}), text(cert)});
            const auto good_client = first_int(f.take(good));
            const auto good_server = first_int(f.take(stalled_accept));
            const auto good_stream = f.now("open_stream", {number(good_client), scalar(1)});
            assert(good_stream.success);
            assert(f.take(f.start("stream_write", {number(good_client), number(as_int64(good_stream.value)), text("g"),
                                                   scalar(0), number(10 * seconds)})).success);
            const auto arrived = f.take(f.start("accept_stream", {number(good_server), number(10 * seconds)}));
            assert(arrived.success && as_int64(arrived.value.elements.at(0)) == as_int64(good_stream.value));
            std::cout << "PASS a good client is accepted while a stalled client is still in its handshake\n";

            // Closing the endpoint while one of its connections is closing and
            // an accept is pending (RULINGS-06, bug 1): the closing connection
            // keeps the shared socket until it ends, the accept is refused.
            const auto closing = f.start("close", {number(good_server), number(3), text("bye"), number(2 * seconds)});
            const auto waiting = f.start("accept", {number(second_endpoint), number(10 * seconds)});
            assert(f.now("close", {number(second_endpoint), number(0), text(""), number(2 * seconds)}).success);
            expect_code(f.take(waiting), host_error::not_found, "accept pending when its endpoint closed");
            const auto ended = f.take(closing);
            assert(ended.success);
            assert(f.now("close", {number(good_client), number(0), text(""), number(2 * seconds)}).success);
            std::cout << "PASS closing an endpoint under a closing connection and a pending accept\n";
        }
    }
    fs::remove_all(dir);
    return 0;
}

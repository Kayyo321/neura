// The supervisor of the runtime service (docs/design/Service030.md): one
// per user, installation and generation. A single-threaded loop over the
// control socket, the client connections, the channels of its workers and
// its child processes. It runs nothing of an image itself.

#include "service.hpp"

#include "tooling/json.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <set>

#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace neura::service {

namespace {

using tooling::Json;
using Millis = std::chrono::milliseconds;

// Timers, from the note; a test may shorten them through the supervisor's
// own NEURA_TEST_SERVICE_TIMERS (name=milliseconds,...), and make the next
// worker never report ready (never-ready=1).
struct Timers {
    Millis reservation{30'000};
    Millis ready{2'000};
    Millis idle{15 * 60'000};
    Millis grace{10'000};
    bool never_ready = false;
    // Answer as another build (case 11), and close a client's connection
    // right after Committed (case 18).
    bool other_build = false;
    bool drop_after_commit = false;

    void read() {
        const char* text = std::getenv("NEURA_TEST_SERVICE_TIMERS");
        if (!text) return;
        std::string_view rest(text);
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const auto item = rest.substr(0, comma);
            const auto equal = item.find('=');
            if (equal != std::string_view::npos) {
                const auto name = item.substr(0, equal);
                const Millis value{std::atoll(std::string(item.substr(equal + 1)).c_str())};
                if (name == "reservation") reservation = value;
                else if (name == "ready") ready = value;
                else if (name == "idle") idle = value;
                else if (name == "grace") grace = value;
                else if (name == "never-ready") never_ready = value.count() != 0;
                else if (name == "other-build") other_build = value.count() != 0;
                else if (name == "drop-after-commit") drop_after_commit = value.count() != 0;
            }
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
};

int signal_pipe[2] = {-1, -1};

extern "C" void on_signal(int signal) {
    const int saved = errno;
    const char byte = static_cast<char>(signal);
    [[maybe_unused]] const auto written = ::write(signal_pipe[1], &byte, 1);
    errno = saved;
}

std::int64_t unix_now() { return static_cast<std::int64_t>(std::time(nullptr)); }

struct CacheEntry {
    std::array<std::uint8_t, 32> digest{};
    std::uint64_t size = 0;
    UniqueFd memory;
    std::uint64_t last_used = 0;
    int users = 0;
};

struct Application {
    std::uint64_t number = 0;
    std::string id;
    RequestId request{};
    Token token{};
    Clock::time_point prepared_at{};
    std::int64_t created = 0;
    std::int64_t ended = 0;
    Envelope envelope;
    Terminal state = Terminal::Prepared;
    LaunchPath path = LaunchPath::Warm;
    int exit_status = 0;
    int signal = 0;
    std::string trap_class;
    // The connection of the client that prepared it, while it lasts.
    int connection = -1;
    bool image_ready = false;
    bool image_reserved = false;
    bool committed = false;
    bool commit_replied = false;
    std::uint64_t commit_sequence = 0;
    // The descriptors of Commit: working directory, input, output, error.
    std::vector<UniqueFd> streams;
    int worker = -1;  // pid
    // A worker that was ready before the commit ran it.
    bool used_ready = false;
    bool reported = false;
    Terminal reported_state = Terminal::Exited;
    bool stop_requested = false;
    Terminal stop_state = Terminal::Stopped;
    std::optional<Clock::time_point> kill_at;
    std::vector<std::pair<std::string, std::uint64_t>> times;
    // Stop requests waiting for the terminal state: connection, sequence.
    std::vector<std::pair<int, std::uint64_t>> stop_waiters;
};

struct Worker {
    WorkerProcess process;
    MessageReader reader;
    std::vector<std::string> environment;
    std::uint64_t application = 0;  // 0 while it waits in the pool
    bool fresh = false;
    bool channel_open = true;
};

struct Connection {
    UniqueFd fd;
    MessageReader reader;
    bool greeted = false;
    Purpose purpose = Purpose::Control;
    std::string build;
    std::string generation;
    Clock::time_point opened{};
};

class Supervisor {
public:
    Supervisor(const fs::path& self, Installation installation, bool on_demand)
        : self_(self), installation_(std::move(installation)), on_demand_(on_demand) {}

    int run();

private:
    // Setup and records.
    void open_state();
    void log(const std::string& line);
    std::string record_text(const Application& application) const;
    void save(const Application& application);
    void forget(const Application& application);
    void prune_records();
    void prune_stored();
    bool active_generation() const;

    // The loop.
    void accept_connections();
    void serve(int key);
    void handle(int key, Received received);
    void close_connection(int key);
    void reject(int key, std::uint64_t sequence, Reason reason, const std::string& sentence, bool close = false);
    void reply(int key, Message message);
    void handle_worker(int pid);
    void reap();
    void timers();
    Clock::time_point next_deadline() const;

    // Requests.
    void hello(int key, const Message& message);
    void prepare(int key, const Message& message);
    void image(int key, Received& received);
    void commit(int key, Received& received);
    void cancel(int key, const Message& message);
    void forward_signal(int key, const Message& message);
    void query(int key, const Message& message);
    void list(int key, const Message& message);
    void stop(int key, const Message& message);
    void shutdown(int key, const Message& message);
    void retire(int key, const Message& message);
    void cache_clear(int key, const Message& message);
    Message listing() const;

    // Applications and workers.
    Application* by_token(std::span<const std::uint8_t> token);
    Application* by_id(std::string_view id);
    std::size_t live_count() const;
    std::size_t pending_count() const;
    void release(Application& application);
    void assign(Application& application);
    void start_worker(Application* for_application);
    void replenish();
    void finish(Application& application, Terminal state, int status, int signal);
    void terminate(Application& application, Terminal state, Millis grace);
    void send_outcome(Application& application);
    void stopped_reply(Application& application);
    CacheEntry* cached(const std::array<std::uint8_t, 32>& digest);
    std::uint64_t cache_bytes() const;
    std::uint64_t reserved_bytes() const;
    bool make_room(std::uint64_t size);
    void use_entry(const std::array<std::uint8_t, 32>& digest, int change);
    bool can_exit() const;

    fs::path self_;
    Installation installation_;
    bool on_demand_;
    Timers timers_;
    std::string build_;
    std::string worker_build_;
    Epoch epoch_{};
    SupervisorState state_ = SupervisorState::Accepting;
    bool retired_ = false;
    Endpoint endpoint_;
    UniqueFd lock_;
    UniqueFd listener_;
    fs::path state_dir_;
    fs::path records_;
    fs::path log_path_;
    std::uint64_t issued_ = 0;
    std::uint64_t clock_ = 0;
    std::map<int, Connection> connections_;
    int next_connection_ = 1;
    // Connections accepted since the start, shown by Listing so that a test
    // can prove that a command made none.
    std::uint64_t accepted_ = 0;
    // Tokens of released reservations, newest last.
    std::deque<Token> released_tokens_;
    bool released(std::span<const std::uint8_t> token) const {
        if (token.size() != 32) return false;
        return std::ranges::any_of(released_tokens_, [&](const Token& known) {
            return std::equal(token.begin(), token.end(), known.begin());
        });
    }
    std::map<std::uint64_t, Application> applications_;
    // Terminal applications, oldest first, for the record limit.
    std::deque<std::uint64_t> terminal_order_;
    std::map<int, Worker> workers_;
    std::list<CacheEntry> cache_;
    Clock::time_point idle_since_ = Clock::now();
    bool stop_ = false;
    bool ready_retry_ = true;
    std::vector<std::string> own_environment_;
};

// --- Setup ----------------------------------------------------------------------

bool Supervisor::active_generation() const {
    if (!installation_.installed) return true;
    std::error_code error;
    const auto target = fs::read_symlink(installation_.root / "current", error);
    if (error) return true;
    return target.filename() == installation_.generation;
}

void Supervisor::log(const std::string& line) {
    std::error_code error;
    if (fs::exists(log_path_, error) && fs::file_size(log_path_, error) > (1u << 20)) {
        auto rotated = log_path_;
        rotated += ".1";
        fs::rename(log_path_, rotated, error);
    }
    if (std::FILE* file = std::fopen(log_path_.c_str(), "a")) {
        char stamp[32];
        const auto now = std::time(nullptr);
        std::tm parts{};
        ::gmtime_r(&now, &parts);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &parts);
        std::fprintf(file, "%s %s\n", stamp, line.c_str());
        std::fclose(file);
    }
}

void Supervisor::open_state() {
    state_dir_ = state_directory(installation_, installation_.generation);
    records_ = state_dir_ / "records";
    std::error_code error;
    fs::create_directories(records_, error);
    if (error) throw TransportError("cannot create " + records_.string() + ": " + error.message());
    ::chmod(state_root(installation_).c_str(), 0700);
    ::chmod(state_dir_.c_str(), 0700);
    ::chmod(records_.c_str(), 0700);
    log_path_ = state_dir_ / "log";
}

std::string Supervisor::record_text(const Application& application) const {
    Json::Object times;
    const auto start = application.envelope.client_clock;
    for (const auto& [name, ns] : application.times) {
        times[name] = Json(static_cast<std::int64_t>(ns >= start ? ns - start : 0));
    }
    Json::Array mode;
    for (const auto& entry : application.envelope.mode) mode.emplace_back(entry);
    Json::Object limits;
    for (const auto& [name, value] : application.envelope.limits) limits[name] = Json(static_cast<std::int64_t>(value));
    Json::Object record{
        {"schema", Json(1)},
        {"id", Json(application.id)},
        {"epoch", Json(hex(epoch_))},
        {"number", Json(static_cast<std::int64_t>(application.number))},
        {"request", Json(hex(application.request))},
        {"state", Json(std::string(name(application.state)))},
        {"image", Json(application.envelope.image_path)},
        {"digest", Json(hex(application.envelope.digest))},
        {"imageBytes", Json(static_cast<std::int64_t>(application.envelope.image_size))},
        {"arguments", Json(static_cast<std::int64_t>(application.envelope.arguments.size()))},
        {"mode", Json(mode)},
        {"limits", Json(limits)},
        {"lifetime", Json(application.envelope.lifetime == Lifetime::Detached ? "detached" : "foreground")},
        {"path", Json(std::string(name(application.committed && !application.used_ready ? LaunchPath::Fresh
                                                                                          : application.path)))},
        {"created", Json(application.created)},
        {"times", Json(times)},
    };
    if (!is_live(application.state)) {
        record["status"] = Json(application.exit_status);
        record["ended"] = Json(application.ended);
        if (!application.trap_class.empty()) record["trapClass"] = Json(application.trap_class);
        if (application.signal != 0) record["signal"] = Json(application.signal);
    }
    return Json(record).dump();
}

void Supervisor::save(const Application& application) {
    try {
        write_atomically(records_ / application.id, record_text(application) + "\n");
    } catch (const std::exception& error) {
        log(std::string("cannot write a record: ") + error.what());
    }
}

void Supervisor::forget(const Application& application) {
    std::error_code error;
    fs::remove(records_ / application.id, error);
}

void Supervisor::prune_stored() {
    // The records and epoch files of earlier supervisors of this generation
    // keep the same bounds: terminal records 24 hours and 1,024 at most,
    // live ones (now unknown) always, and an epoch file while a record of
    // its epoch remains or for 24 hours.
    const auto now = unix_now();
    std::vector<std::pair<std::int64_t, fs::path>> terminal;
    std::set<std::string> epochs_in_use;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(records_, error)) {
        const auto text = read_text(entry.path());
        if (!text) continue;
        try {
            auto record = Json::parse(*text);
            const auto state = terminal_named(record["state"].string());
            epochs_in_use.insert(record["epoch"].string());
            if (state && !is_live(*state)) terminal.emplace_back(record["ended"].integer(), entry.path());
        } catch (const std::exception&) {
            // A record that does not parse was cut short: it is dropped.
            fs::remove(entry.path(), error);
        }
    }
    std::ranges::sort(terminal);
    const auto excess = terminal.size() > max_terminal_records ? terminal.size() - max_terminal_records : 0;
    for (std::size_t i = 0; i < terminal.size(); ++i) {
        if (i < excess || now - terminal[i].first > static_cast<std::int64_t>(record_lifetime_seconds)) {
            fs::remove(terminal[i].second, error);
        }
    }
    for (const auto& entry : fs::directory_iterator(state_dir_, error)) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with("epoch-") || epochs_in_use.contains(name.substr(6))) continue;
        struct stat status{};
        if (::stat(entry.path().c_str(), &status) == 0 &&
            now - static_cast<std::int64_t>(status.st_mtime) > static_cast<std::int64_t>(record_lifetime_seconds)) {
            fs::remove(entry.path(), error);
        }
    }
}

void Supervisor::prune_records() {
    // Terminal records live 24 hours and at most 1,024 are kept; live ones
    // are never removed.
    const auto now = unix_now();
    while (!terminal_order_.empty()) {
        const auto number = terminal_order_.front();
        const auto found = applications_.find(number);
        if (found == applications_.end()) {
            terminal_order_.pop_front();
            continue;
        }
        const bool too_many = terminal_order_.size() > max_terminal_records;
        const bool too_old = now - found->second.ended > static_cast<std::int64_t>(record_lifetime_seconds);
        if (!too_many && !too_old) break;
        forget(found->second);
        applications_.erase(found);
        terminal_order_.pop_front();
    }
}

// --- Connections ------------------------------------------------------------------

void Supervisor::reply(int key, Message message) {
    const auto found = connections_.find(key);
    if (found == connections_.end()) return;
    try {
        send_message(found->second.fd.get(), message, {}, Clock::now() + Millis(1000));
    } catch (const std::exception& error) {
        log(std::string("a reply was not delivered: ") + error.what());
        close_connection(key);
    }
}

void Supervisor::reject(int key, std::uint64_t sequence, Reason reason, const std::string& sentence, bool close) {
    Message message;
    message.kind = Kind::Rejected;
    message.sequence = sequence;
    message.add(Tag::Reason, name(reason)).add(Tag::Sentence, sentence).add_u8(Tag::NotStarted, 1);
    reply(key, std::move(message));
    if (close) close_connection(key);
}

void Supervisor::accept_connections() {
    for (;;) {
        const int fd = ::accept(listener_.get(), nullptr, nullptr);
        if (fd < 0) return;
        UniqueFd owned(fd);
        set_cloexec(fd);
        // A supervisor accepts a connection only from its own user.
        const auto uid = peer_uid(fd);
        if (!uid || *uid != ::geteuid()) {
            log("refused a connection of another user");
            continue;
        }
        Connection connection;
        connection.fd = std::move(owned);
        connection.opened = Clock::now();
        connections_.emplace(next_connection_++, std::move(connection));
        ++accepted_;
    }
}

void Supervisor::close_connection(int key) {
    const auto found = connections_.find(key);
    if (found == connections_.end()) return;
    connections_.erase(found);
    for (auto& [number, application] : applications_) {
        if (application.connection != key) continue;
        application.connection = -1;
        if (!application.committed && is_live(application.state)) {
            // A reservation whose connection closes is released.
            release(application);
        } else if (application.envelope.lifetime == Lifetime::Foreground && is_live(application.state)) {
            // The foreground client went away: end the application.
            terminate(application, Terminal::ClientLost, timers_.grace);
        }
    }
    std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
    if (connections_.empty()) idle_since_ = Clock::now();
}

void Supervisor::serve(int key) {
    auto found = connections_.find(key);
    if (found == connections_.end()) return;
    auto& connection = found->second;
    try {
        if (!connection.reader.fill(connection.fd.get())) {
            close_connection(key);
            return;
        }
        while (auto received = connection.reader.take()) {
            handle(key, std::move(*received));
            if (!connections_.contains(key)) return;
            if (!connection.reader.fill(connection.fd.get())) {
                close_connection(key);
                return;
            }
        }
    } catch (const ProtocolError& error) {
        // One Rejected with reason protocol, then the connection closes.
        log(std::string("a malformed message: ") + error.what());
        reject(key, 0, Reason::Protocol, std::string("malformed message: ") + error.what(), true);
    } catch (const TransportError&) {
        close_connection(key);
    }
}

void Supervisor::handle(int key, Received received) {
    auto& connection = connections_.at(key);
    const auto& message = received.message;
    if (!connection.greeted) {
        if (message.kind != Kind::Hello) throw ProtocolError("the first message must be Hello");
        hello(key, message);
        return;
    }
    switch (message.kind) {
    case Kind::Prepare: prepare(key, message); break;
    case Kind::Image: image(key, received); break;
    case Kind::Commit: commit(key, received); break;
    case Kind::Cancel: cancel(key, message); break;
    case Kind::Signal: forward_signal(key, message); break;
    case Kind::Query: query(key, message); break;
    case Kind::List: list(key, message); break;
    case Kind::Stop: stop(key, message); break;
    case Kind::Shutdown: shutdown(key, message); break;
    case Kind::Retire: retire(key, message); break;
    case Kind::CacheClear: cache_clear(key, message); break;
    default: throw ProtocolError(std::string("a client does not send ") + std::string(name(message.kind)));
    }
}

void Supervisor::hello(int key, const Message& message) {
    auto& connection = connections_.at(key);
    const auto low = message.u16(Tag::ProtocolLow);
    const auto high = message.u16(Tag::ProtocolHigh);
    if (message.get(Tag::Installation) != installation_.id) {
        reject(key, message.sequence, Reason::Identity, "this supervisor serves another installation", true);
        return;
    }
    if (low > protocol_major || high < protocol_major) {
        reject(key, message.sequence, Reason::UnsupportedProtocol,
               "no common protocol major: this supervisor speaks " + std::string(protocol_text), true);
        return;
    }
    const auto purpose = message.u8(Tag::Purpose);
    if (purpose != 1 && purpose != 2) throw ProtocolError("unknown purpose");
    connection.greeted = true;
    connection.purpose = static_cast<Purpose>(purpose);
    connection.build = std::string(message.get(Tag::Build));
    connection.generation = std::string(message.get(Tag::Generation));
    Message welcome;
    welcome.kind = Kind::Welcome;
    welcome.sequence = message.sequence;
    welcome.add_u16(Tag::Protocol, protocol_major)
        .add(Tag::Epoch, std::string_view(reinterpret_cast<const char*>(epoch_.data()), epoch_.size()))
        .add(Tag::Build, build_)
        .add(Tag::Generation, installation_.generation)
        .add(Tag::Installation, installation_.id)
        .add_u8(Tag::State, static_cast<std::uint8_t>(retired_ && state_ == SupervisorState::Accepting
                                                          ? SupervisorState::Retiring
                                                          : state_))
        .add(Tag::Limit, named_value("applications", max_applications))
        .add(Tag::Limit, named_value("pending", max_pending))
        .add(Tag::Limit, named_value("cache-bytes", max_cache_bytes))
        .add(Tag::Limit, named_value("message-bytes", max_message_bytes))
        .add_u64(Tag::Pid, static_cast<std::uint64_t>(::getpid()));
    reply(key, std::move(welcome));
}

// --- Launches ---------------------------------------------------------------------

std::size_t Supervisor::live_count() const {
    return static_cast<std::size_t>(std::ranges::count_if(
        applications_, [](const auto& entry) { return is_live(entry.second.state); }));
}

std::size_t Supervisor::pending_count() const {
    return static_cast<std::size_t>(std::ranges::count_if(applications_, [](const auto& entry) {
        return entry.second.state == Terminal::Prepared && !entry.second.committed;
    }));
}

CacheEntry* Supervisor::cached(const std::array<std::uint8_t, 32>& digest) {
    for (auto& entry : cache_) {
        if (entry.digest == digest) return &entry;
    }
    return nullptr;
}

std::uint64_t Supervisor::cache_bytes() const {
    std::uint64_t total = 0;
    for (const auto& entry : cache_) total += entry.size;
    return total;
}

std::uint64_t Supervisor::reserved_bytes() const {
    std::uint64_t total = 0;
    for (const auto& [number, application] : applications_) {
        if (application.image_reserved && !application.image_ready) total += application.envelope.image_size;
    }
    return total;
}

bool Supervisor::make_room(std::uint64_t size) {
    // The least recently used free entry goes first; an entry in use never.
    while (cache_bytes() + reserved_bytes() + size > max_cache_bytes) {
        auto victim = cache_.end();
        for (auto entry = cache_.begin(); entry != cache_.end(); ++entry) {
            if (entry->users == 0 && (victim == cache_.end() || entry->last_used < victim->last_used)) victim = entry;
        }
        if (victim == cache_.end()) return false;
        cache_.erase(victim);
    }
    return true;
}

void Supervisor::use_entry(const std::array<std::uint8_t, 32>& digest, int change) {
    if (auto* entry = cached(digest)) {
        entry->users += change;
        entry->last_used = ++clock_;
    }
}

void Supervisor::prepare(int key, const Message& message) {
    auto& connection = connections_.at(key);
    if (connection.build != build_ || connection.generation != installation_.generation) {
        reject(key, message.sequence, Reason::Identity,
               "the supervisor is " + build_ + " of generation " + installation_.generation + ", the client is " +
                   connection.build + " of generation " + connection.generation);
        return;
    }
    if (!retired_ && !active_generation()) {
        retired_ = true;
        log("retired: current no longer names this generation");
        replenish();
    }
    if (state_ == SupervisorState::Draining) {
        reject(key, message.sequence, Reason::Draining, "the service is shutting down");
        return;
    }
    const auto request = message.get(Tag::RequestId);
    if (request.size() != 16) throw ProtocolError("a request ID is 16 bytes");
    Envelope envelope = read_envelope(message);
    if (live_count() >= max_applications) {
        reject(key, message.sequence, Reason::AdmissionApplications,
               std::to_string(max_applications) + " applications are already held");
        return;
    }
    if (pending_count() >= max_pending) {
        reject(key, message.sequence, Reason::AdmissionPending,
               std::to_string(max_pending) + " prepared launches are already held");
        return;
    }
    if (envelope.image_size > max_cache_bytes) {
        reject(key, message.sequence, Reason::ImageTooLarge,
               "the image of " + std::to_string(envelope.image_size) + " bytes is larger than the cache limit of " +
                   std::to_string(max_cache_bytes));
        return;
    }
    auto* entry = cached(envelope.digest);
    if (!entry && !make_room(envelope.image_size)) {
        reject(key, message.sequence, Reason::AdmissionCache,
               "the image does not fit beside the images that running applications use");
        return;
    }
    const auto fit = reuse(envelope, own_environment_);
    if (!fit.fresh_possible) {
        reject(key, message.sequence, Reason::NotReproducible, fit.reason);
        return;
    }
    Application application;
    application.number = ++issued_;
    application.id = application_id(installation_.generation, epoch_, application.number);
    std::copy(request.begin(), request.end(), application.request.begin());
    random_bytes(application.token);
    application.prepared_at = Clock::now();
    application.created = unix_now();
    application.envelope = std::move(envelope);
    application.path = fit.warm ? LaunchPath::Warm : LaunchPath::Fresh;
    application.connection = key;
    application.times.emplace_back("accepted", clock_ns(Clock::now()));
    if (entry) {
        application.image_ready = true;
        entry->users += 1;
        entry->last_used = ++clock_;
    } else {
        application.image_reserved = true;
    }
    try {
        write_atomically(state_dir_ / ("epoch-" + hex(epoch_)), std::to_string(issued_) + "\n");
    } catch (const std::exception& error) {
        log(std::string("cannot write the epoch file: ") + error.what());
    }
    Message prepared;
    prepared.kind = Kind::Prepared;
    prepared.sequence = message.sequence;
    prepared.add(Tag::Token, std::string_view(reinterpret_cast<const char*>(application.token.data()), 32))
        .add_u32(Tag::ValidMilliseconds, static_cast<std::uint32_t>(timers_.reservation.count()))
        .add_u8(Tag::ImageNeeded, application.image_ready ? 0 : 1)
        .add_u8(Tag::Path, static_cast<std::uint8_t>(application.path))
        .add(Tag::ApplicationId, application.id);
    const auto number = application.number;
    applications_.emplace(number, std::move(application));
    save(applications_.at(number));
    reply(key, std::move(prepared));
}

Application* Supervisor::by_token(std::span<const std::uint8_t> token) {
    if (token.size() != 32) return nullptr;
    for (auto& [number, application] : applications_) {
        if (std::equal(token.begin(), token.end(), application.token.begin())) return &application;
    }
    return nullptr;
}

Application* Supervisor::by_id(std::string_view id) {
    for (auto& [number, application] : applications_) {
        if (application.id == id) return &application;
    }
    return nullptr;
}

void Supervisor::release(Application& application) {
    // The token stays known for the epoch, so that a late Commit is
    // token-expired rather than token-unknown (bounded: the oldest go).
    released_tokens_.push_back(application.token);
    if (released_tokens_.size() > 4096) released_tokens_.pop_front();
    if (application.image_ready) use_entry(application.envelope.digest, -1);
    application.streams.clear();
    forget(application);
    // Marked for removal by the caller's erase.
    application.number = 0;
    application.state = Terminal::Expired;
}

namespace {

std::span<const std::uint8_t> token_of(const Message& message) {
    const auto token = message.get(Tag::Token);
    return {reinterpret_cast<const std::uint8_t*>(token.data()), token.size()};
}

} // namespace

void Supervisor::image(int key, Received& received) {
    const auto& message = received.message;
    auto* application = by_token(token_of(message));
    if (!application) {
        if (released(token_of(message))) {
            reject(key, message.sequence, Reason::TokenExpired, "the reservation expired or was released");
            return;
        }
        reject(key, message.sequence, Reason::TokenUnknown, "this supervisor did not issue the token");
        return;
    }
    if (Clock::now() - application->prepared_at > timers_.reservation) {
        reject(key, message.sequence, Reason::TokenExpired, "the reservation expired");
        release(*application);
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
        return;
    }
    if (!application->image_ready) {
        // Copy the bytes into memory of the supervisor's own, hashing as
        // they are copied; the client's object is never used afterwards.
        const auto size = application->envelope.image_size;
        const int source = received.descriptors.front().get();
        int memory = -1;
#if defined(__linux__)
        memory = ::memfd_create("neura-image", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#endif
        if (memory < 0) {
            char pattern[] = "/tmp/neura-image-XXXXXX";
            memory = ::mkstemp(pattern);
            if (memory >= 0) ::unlink(pattern);
        }
        if (memory < 0) {
            reject(key, message.sequence, Reason::AdmissionCache, "the service cannot hold the image");
            release(*application);
            std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
            return;
        }
        UniqueFd owned(memory);
        util::Sha256 digest;
        std::uint64_t copied = 0;
        std::vector<std::uint8_t> buffer(1 << 20);
        bool failed = false;
        for (;;) {
            const auto count = ::pread(source, buffer.data(), buffer.size(), static_cast<off_t>(copied));
            if (count < 0) {
                if (errno == EINTR) continue;
                failed = true;
                break;
            }
            if (count == 0) break;
            copied += static_cast<std::uint64_t>(count);
            if (copied > size) break;
            digest.update(std::span(buffer.data(), static_cast<std::size_t>(count)));
            std::size_t done = 0;
            while (done < static_cast<std::size_t>(count)) {
                const auto written = ::write(memory, buffer.data() + done, static_cast<std::size_t>(count) - done);
                if (written < 0) {
                    if (errno == EINTR) continue;
                    failed = true;
                    break;
                }
                done += static_cast<std::size_t>(written);
            }
            if (failed) break;
        }
        if (failed || copied != size || digest.finish() != application->envelope.digest) {
            reject(key, message.sequence, Reason::ImageMismatch,
                   "the bytes read are not of the size and digest the launch names; the image changed while it was "
                   "launched");
            release(*application);
            std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
            return;
        }
#if defined(__linux__)
        // The bytes are never written again, by the supervisor or a worker.
        ::fcntl(memory, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL);
#endif
        // Another launch may have brought the same bytes meanwhile.
        if (!cached(application->envelope.digest)) {
            CacheEntry entry;
            entry.digest = application->envelope.digest;
            entry.size = size;
            entry.memory = std::move(owned);
            entry.last_used = ++clock_;
            cache_.push_back(std::move(entry));
        }
        application->image_reserved = false;
        application->image_ready = true;
        use_entry(application->envelope.digest, 1);
    }
    Message prepared;
    prepared.kind = Kind::Prepared;
    prepared.sequence = message.sequence;
    prepared.add(Tag::Token, message.get(Tag::Token))
        .add_u32(Tag::ValidMilliseconds, static_cast<std::uint32_t>(timers_.reservation.count()))
        .add_u8(Tag::ImageNeeded, 0)
        .add_u8(Tag::Path, static_cast<std::uint8_t>(application->path))
        .add(Tag::ApplicationId, application->id);
    reply(key, std::move(prepared));
}

void Supervisor::commit(int key, Received& received) {
    const auto& message = received.message;
    auto* application = by_token(token_of(message));
    if (!application) {
        if (released(token_of(message))) {
            reject(key, message.sequence, Reason::TokenExpired, "the reservation expired or was released before the commit");
            return;
        }
        reject(key, message.sequence, Reason::TokenUnknown, "this supervisor did not issue the token in this epoch");
        return;
    }
    if (application->committed) {
        // The same Committed, and nothing starts.
        if (application->commit_replied) {
            Message committed;
            committed.kind = Kind::Committed;
            committed.sequence = message.sequence;
            committed.add(Tag::ApplicationId, application->id);
            reply(key, std::move(committed));
        }
        return;
    }
    if (Clock::now() - application->prepared_at > timers_.reservation) {
        reject(key, message.sequence, Reason::TokenExpired, "the reservation expired before the commit");
        release(*application);
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
        return;
    }
    if (!application->image_ready) {
        reject(key, message.sequence, Reason::ImageMismatch, "the image bytes were not sent");
        release(*application);
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
        return;
    }
    application->committed = true;
    application->commit_sequence = message.sequence;
    application->connection = key;
    application->streams = std::move(received.descriptors);
    application->state = Terminal::Starting;
    application->times.emplace_back("committed", clock_ns(Clock::now()));
    save(*application);
    assign(*application);
}

void Supervisor::start_worker(Application* for_application) {
    Worker worker;
    const bool fresh = for_application && for_application->path == LaunchPath::Fresh;
    try {
        if (fresh) {
            worker.process = spawn_worker(self_, worker_build_, &for_application->envelope.environment,
                                          &for_application->envelope.resource_limits);
            worker.environment = for_application->envelope.environment;
        } else {
            worker.process = spawn_worker(self_, worker_build_, nullptr, nullptr, timers_.never_ready && ready_retry_);
            worker.environment = own_environment_;
        }
    } catch (const std::exception& error) {
        log(std::string("cannot start a worker: ") + error.what());
        return;
    }
    worker.fresh = fresh;
    worker.application = for_application ? for_application->number : 0;
    if (for_application) for_application->worker = worker.process.pid;
    set_nonblocking(worker.process.channel.get(), true);
    workers_.emplace(worker.process.pid, std::move(worker));
}

void Supervisor::assign(Application& application) {
    // A ready worker, when the launch can use one; otherwise one started for it.
    if (application.worker < 0 && application.path == LaunchPath::Warm) {
        for (auto& [pid, worker] : workers_) {
            if (worker.application == 0 && !worker.fresh && worker.process.ready) {
                worker.application = application.number;
                application.worker = pid;
                application.used_ready = true;
                break;
            }
        }
        if (application.worker < 0) {
            for (auto& [pid, worker] : workers_) {
                if (worker.application == 0 && !worker.fresh) {
                    worker.application = application.number;
                    application.worker = pid;
                    break;
                }
            }
        }
    }
    if (application.worker < 0) start_worker(&application);
    if (application.worker < 0) {
        if (application.commit_replied) return;
        reject(application.connection, application.commit_sequence, Reason::WorkerUnavailable,
               "no worker could be started");
        release(application);
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
        return;
    }
    auto& worker = workers_.at(application.worker);
    if (!worker.process.ready) return;  // assigned when it reports ready
    const auto* entry = cached(application.envelope.digest);
    Message assignment;
    assignment.kind = Kind::WorkerAssign;
    assignment.add_u8(Tag::Descriptors, 5).add(Tag::ApplicationId, application.id);
    put_envelope(assignment, application.envelope);
    std::vector<int> descriptors{entry ? entry->memory.get() : -1};
    for (const auto& stream : application.streams) descriptors.push_back(stream.get());
    try {
        if (descriptors.front() < 0) throw TransportError("the image left the cache");
        send_message(worker.process.channel.get(), assignment, descriptors, Clock::now() + Millis(1000));
    } catch (const std::exception& error) {
        log(std::string("cannot assign a worker: ") + error.what());
        ::kill(worker.process.pid, SIGKILL);
        return;
    }
    application.streams.clear();
    application.times.emplace_back("assigned", clock_ns(Clock::now()));
    save(application);
    Message committed;
    committed.kind = Kind::Committed;
    committed.sequence = application.commit_sequence;
    committed.add(Tag::ApplicationId, application.id)
        .add_u8(Tag::Path, static_cast<std::uint8_t>(application.used_ready ? LaunchPath::Warm : LaunchPath::Fresh));
    application.commit_replied = true;
    if (application.connection >= 0) reply(application.connection, std::move(committed));
    if (timers_.drop_after_commit && application.connection >= 0) {
        // A test of a connection lost after the commit: the application
        // runs on, and its client must find its outcome by asking.
        const int key = application.connection;
        application.connection = -1;
        connections_.erase(key);
    }
    replenish();
}

void Supervisor::replenish() {
    // One ready worker, unless retired, draining or at the application limit.
    std::size_t waiting = 0;
    for (auto& [pid, worker] : workers_) {
        if (worker.application == 0 && !worker.fresh) ++waiting;
    }
    if (retired_ || state_ != SupervisorState::Accepting || live_count() >= max_applications) {
        for (auto& [pid, worker] : workers_) {
            if (worker.application == 0 && !worker.fresh) ::kill(pid, SIGKILL);
        }
        return;
    }
    if (waiting < ready_workers) start_worker(nullptr);
}

void Supervisor::cancel(int key, const Message& message) {
    auto* application = by_token(token_of(message));
    if (application && !application->committed) {
        release(*application);
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
    }
    (void)key;
}

void Supervisor::forward_signal(int key, const Message& message) {
    auto* application = by_id(message.get(Tag::ApplicationId));
    if (!application || !is_live(application->state) || application->worker < 0) return;
    int number = 0;
    switch (static_cast<ForwardedSignal>(message.u8(Tag::SignalNumber))) {
    case ForwardedSignal::Int: number = SIGINT; break;
    case ForwardedSignal::Term: number = SIGTERM; break;
    case ForwardedSignal::Hup: number = SIGHUP; break;
    case ForwardedSignal::Quit: number = SIGQUIT; break;
    case ForwardedSignal::Winch: number = SIGWINCH; break;
    default: throw ProtocolError("unknown signal");
    }
    ::killpg(application->worker, number);
    (void)key;
}

void Supervisor::terminate(Application& application, Terminal state, Millis grace) {
    if (!is_live(application.state)) return;
    if (!application.committed || application.worker < 0) {
        release(application);
        return;
    }
    if (!application.stop_requested) {
        application.stop_requested = true;
        application.stop_state = state;
        application.kill_at = Clock::now() + grace;
        ::killpg(application.worker, SIGTERM);
        ::kill(application.worker, SIGTERM);
    }
}

// --- Workers ------------------------------------------------------------------------

void Supervisor::handle_worker(int pid) {
    auto found = workers_.find(pid);
    if (found == workers_.end()) return;
    auto& worker = found->second;
    try {
        if (!worker.reader.fill(worker.process.channel.get())) {
            worker.channel_open = false;
            return;
        }
        while (auto received = worker.reader.take()) {
            const auto& message = received->message;
            Application* application = nullptr;
            if (worker.application != 0) {
                const auto at = applications_.find(worker.application);
                if (at != applications_.end()) application = &at->second;
            }
            if (message.kind == Kind::WorkerReady) {
                if (message.get(Tag::Build) != worker_build_) throw ProtocolError("a worker of another build");
                worker.process.ready = true;
                ready_retry_ = true;
                if (application) assign(*application);
            } else if (message.kind == Kind::WorkerStarted && application) {
                application->state = Terminal::Running;
                for (const auto value : message.all(Tag::Time)) application->times.push_back(read_named_value(value));
                save(*application);
            } else if (message.kind == Kind::WorkerFinished && application) {
                application->reported = true;
                application->reported_state = static_cast<Terminal>(message.u8(Tag::Terminal));
                application->exit_status = static_cast<int>(message.i64(Tag::ExitStatus));
                if (message.has(Tag::TrapClass)) application->trap_class = std::string(message.get(Tag::TrapClass));
                for (const auto value : message.all(Tag::Time)) {
                    auto entry = read_named_value(value);
                    if (entry.second != 0) application->times.push_back(entry);
                }
            }
            if (!worker.reader.fill(worker.process.channel.get())) {
                worker.channel_open = false;
                return;
            }
        }
    } catch (const std::exception& error) {
        log(std::string("a worker's channel failed: ") + error.what());
        worker.channel_open = false;
        ::kill(pid, SIGKILL);
    }
}

void Supervisor::finish(Application& application, Terminal state, int status, int signal) {
    application.state = state;
    application.exit_status = status;
    application.signal = signal;
    application.ended = unix_now();
    application.worker = -1;
    application.times.emplace_back("ended", clock_ns(Clock::now()));
    use_entry(application.envelope.digest, -1);
    save(application);
    terminal_order_.push_back(application.number);
    send_outcome(application);
    stopped_reply(application);
    prune_records();
}

void Supervisor::send_outcome(Application& application) {
    if (application.connection < 0) return;
    Message outcome;
    outcome.kind = Kind::Outcome;
    outcome.add(Tag::ApplicationId, application.id)
        .add_u8(Tag::Terminal, static_cast<std::uint8_t>(application.state))
        .add_i64(Tag::ExitStatus, application.exit_status)
        .add(Tag::Record, record_text(application));
    if (application.signal != 0) outcome.add_u8(Tag::SignalNumber, static_cast<std::uint8_t>(application.signal));
    if (!application.trap_class.empty()) outcome.add(Tag::TrapClass, application.trap_class);
    const auto start = application.envelope.client_clock;
    for (const auto& [name, ns] : application.times) outcome.add(Tag::Time, named_value(name, ns >= start ? ns - start : 0));
    reply(application.connection, std::move(outcome));
}

void Supervisor::stopped_reply(Application& application) {
    for (const auto& [key, sequence] : application.stop_waiters) {
        Message status;
        status.kind = Kind::Status;
        status.sequence = sequence;
        status.add_u8(Tag::Terminal, static_cast<std::uint8_t>(application.state)).add(Tag::Record, record_text(application));
        reply(key, std::move(status));
    }
    application.stop_waiters.clear();
}

void Supervisor::reap() {
    for (;;) {
        int status = 0;
        const pid_t pid = ::waitpid(-1, &status, WNOHANG);
        if (pid <= 0) return;
        const auto found = workers_.find(pid);
        if (found == workers_.end()) continue;
        // Read what the worker said before it ended.
        handle_worker(pid);
        auto& worker = found->second;
        const auto number = worker.application;
        const bool was_ready = worker.process.ready;
        workers_.erase(found);
        // A worker that ended before it reported ready: the pool tries once
        // more on the next launch, not at once.
        if (!was_ready) ready_retry_ = false;
        if (number == 0) continue;
        const auto at = applications_.find(number);
        if (at == applications_.end()) continue;
        auto& application = at->second;
        if (!application.commit_replied) {
            // The worker ended before it was given the launch: nothing ran.
            if (application.connection >= 0) {
                reject(application.connection, application.commit_sequence, Reason::WorkerUnavailable,
                       "the worker ended before it reported ready; nothing was run");
            }
            release(application);
            std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
            continue;
        }
        const bool signaled = WIFSIGNALED(status);
        const int signal = signaled ? WTERMSIG(status) : 0;
        if (application.reported) {
            finish(application, application.reported_state, application.exit_status, 0);
        } else if (application.stop_requested) {
            finish(application, application.stop_state, signaled ? 128 + signal : WEXITSTATUS(status), signal);
        } else if (signaled && signal != SIGKILL) {
            finish(application, Terminal::Signaled, 128 + signal, signal);
        } else {
            finish(application, Terminal::WorkerLost, signaled ? 128 + signal : WEXITSTATUS(status), signal);
        }
    }
}

// --- Control ----------------------------------------------------------------------

Message Supervisor::listing() const {
    Message message;
    message.kind = Kind::Listing;
    std::size_t ready = 0;
    for (const auto& [pid, worker] : workers_) {
        if (worker.application == 0 && worker.process.ready) ++ready;
    }
    const auto state = retired_ && state_ == SupervisorState::Accepting ? SupervisorState::Retiring : state_;
    message.add_u8(Tag::State, static_cast<std::uint8_t>(state))
        .add_u32(Tag::ApplicationCount, static_cast<std::uint32_t>(live_count()))
        .add_u64(Tag::CacheBytes, cache_bytes())
        .add_u32(Tag::ReadyWorkers, static_cast<std::uint32_t>(ready))
        .add(Tag::Epoch, std::string_view(reinterpret_cast<const char*>(epoch_.data()), epoch_.size()))
        .add(Tag::Generation, installation_.generation)
        .add(Tag::Build, build_)
        .add_u64(Tag::Pid, static_cast<std::uint64_t>(::getpid()))
        .add(Tag::Limit, named_value("connections-accepted", accepted_));
    for (const auto& [number, application] : applications_) message.add(Tag::Record, record_text(application));
    return message;
}

void Supervisor::list(int key, const Message& message) {
    auto out = listing();
    out.sequence = message.sequence;
    reply(key, std::move(out));
}

void Supervisor::query(int key, const Message& message) {
    Message status;
    status.kind = Kind::Status;
    status.sequence = message.sequence;
    const Application* found = nullptr;
    if (const auto* request = message.find(Tag::RequestId)) {
        for (const auto& [number, application] : applications_) {
            if (request->bytes.size() == 16 &&
                std::memcmp(request->bytes.data(), application.request.data(), 16) == 0) {
                found = &application;
            }
        }
    } else if (const auto* id = message.find(Tag::ApplicationId)) {
        for (const auto& [number, application] : applications_) {
            if (application.id == id->bytes) found = &application;
        }
        if (!found) {
            const auto parsed = parse_application_id(id->bytes);
            const bool ours = parsed && parsed->generation == installation_.generation &&
                              parsed->epoch == hex(std::span(epoch_).first(4));
            status.add_u8(Tag::Terminal, static_cast<std::uint8_t>(
                                             ours && parsed->number <= issued_ ? Terminal::Expired : Terminal::Unknown));
            reply(key, std::move(status));
            return;
        }
    }
    if (found) {
        status.add_u8(Tag::Terminal, static_cast<std::uint8_t>(found->state)).add(Tag::Record, record_text(*found));
    } else {
        status.add_u8(Tag::Terminal, static_cast<std::uint8_t>(Terminal::Unknown));
    }
    reply(key, std::move(status));
}

void Supervisor::stop(int key, const Message& message) {
    auto* application = by_id(message.get(Tag::ApplicationId));
    if (!application) {
        query(key, message);
        return;
    }
    if (!is_live(application->state)) {
        Message status;
        status.kind = Kind::Status;
        status.sequence = message.sequence;
        status.add_u8(Tag::Terminal, static_cast<std::uint8_t>(application->state))
            .add(Tag::Record, record_text(*application));
        reply(key, std::move(status));
        return;
    }
    if (!application->committed) {
        release(*application);
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
        Message status;
        status.kind = Kind::Status;
        status.sequence = message.sequence;
        status.add_u8(Tag::Terminal, static_cast<std::uint8_t>(Terminal::Expired));
        reply(key, std::move(status));
        return;
    }
    application->stop_waiters.emplace_back(key, message.sequence);
    terminate(*application, Terminal::Stopped, Millis(message.u32(Tag::GraceMilliseconds)));
}

void Supervisor::shutdown(int key, const Message& message) {
    state_ = SupervisorState::Draining;
    log("draining: shutdown requested");
    replenish();
    if (message.u8(Tag::Terminate) != 0) {
        for (auto& [number, application] : applications_) {
            if (is_live(application.state)) terminate(application, Terminal::Stopped, timers_.grace);
        }
        std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
    }
    auto out = listing();
    out.sequence = message.sequence;
    reply(key, std::move(out));
}

void Supervisor::retire(int key, const Message& message) {
    retired_ = true;
    log("retired by an update");
    replenish();
    auto out = listing();
    out.sequence = message.sequence;
    reply(key, std::move(out));
}

void Supervisor::cache_clear(int key, const Message& message) {
    // Every cached image no running application uses.
    cache_.remove_if([](const CacheEntry& entry) { return entry.users == 0; });
    auto out = listing();
    out.sequence = message.sequence;
    reply(key, std::move(out));
}

// --- The loop -------------------------------------------------------------------------

bool Supervisor::can_exit() const {
    const bool empty = live_count() == 0;
    if (state_ == SupervisorState::Draining) return empty;
    if (retired_) return empty && connections_.empty();
    return false;
}

Clock::time_point Supervisor::next_deadline() const {
    auto next = Clock::now() + Millis(60'000);
    for (const auto& [number, application] : applications_) {
        if (application.state == Terminal::Prepared && !application.committed) {
            next = std::min(next, application.prepared_at + timers_.reservation);
        }
        if (application.kill_at) next = std::min(next, *application.kill_at);
    }
    for (const auto& [pid, worker] : workers_) {
        if (!worker.process.ready) next = std::min(next, worker.process.spawned + timers_.ready);
    }
    if (on_demand_) next = std::min(next, idle_since_ + timers_.idle);
    return next;
}

void Supervisor::timers() {
    const auto now = Clock::now();
    for (auto& [number, application] : applications_) {
        if (application.state == Terminal::Prepared && !application.committed &&
            now - application.prepared_at >= timers_.reservation) {
            release(application);
        }
        if (application.kill_at && now >= *application.kill_at && application.worker > 0) {
            ::killpg(application.worker, SIGKILL);
            ::kill(application.worker, SIGKILL);
            application.kill_at.reset();
        }
    }
    std::erase_if(applications_, [](const auto& entry) { return entry.second.number == 0; });
    for (auto& [pid, worker] : workers_) {
        if (!worker.process.ready && now - worker.process.spawned >= timers_.ready) {
            log("a worker did not report ready in time; it is killed");
            ::kill(pid, SIGKILL);
            worker.process.spawned = now + Millis(60'000);
        }
    }
    if (on_demand_ && live_count() == 0 && connections_.empty() && now - idle_since_ >= timers_.idle) {
        log("idle: no application and no connection; exiting");
        stop_ = true;
    }
}

int Supervisor::run() {
    // Nothing of the process that started it: no directory it could hold
    // busy, and no descriptor above 2 it inherited.
    if (::chdir("/") != 0) log_path_.clear();
    {
        std::vector<int> inherited;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator("/proc/self/fd", error)) {
            const int fd = std::atoi(entry.path().filename().c_str());
            if (fd > 2) inherited.push_back(fd);
        }
        for (const int fd : inherited) ::close(fd);
    }
    timers_.read();
    build_ = own_build();
    // The workers are of this build; only the supervisor's answers differ.
    worker_build_ = build_;
    if (timers_.other_build) build_ += " (a test of another build)";
    for (char** entry = process_environment(); entry && *entry; ++entry) own_environment_.emplace_back(*entry);
    random_bytes(epoch_);
    const auto run = run_directory(true);
    make_private_directory(run / "neura");
    endpoint_ = endpoint_files(run, installation_.root, installation_.generation);
    make_private_directory(endpoint_.directory);
    open_state();

    // The lock decides which supervisor owns the endpoint; a socket file
    // found by the holder of the lock is stale.
    const int lock = ::open(endpoint_.lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0) throw TransportError("cannot open " + endpoint_.lock.string() + ": " + std::strerror(errno));
    lock_.reset(lock);
    if (::flock(lock, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "neura: neurad: a supervisor of this installation and generation is already running\n");
        return 1;
    }
    ::unlink(endpoint_.socket.c_str());
    int type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif
    const int listener = ::socket(AF_UNIX, type, 0);
    if (listener < 0) throw TransportError(std::string("cannot create the socket: ") + std::strerror(errno));
    listener_.reset(listener);
    set_cloexec(listener);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, endpoint_.socket.c_str(), sizeof address.sun_path - 1);
    const auto mask = ::umask(0177);
    const int bound = ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address);
    ::umask(mask);
    if (bound != 0) throw TransportError("cannot bind " + endpoint_.socket.string() + ": " + std::strerror(errno));
    ::chmod(endpoint_.socket.c_str(), 0600);
    if (::listen(listener, 64) != 0) throw TransportError(std::string("cannot listen: ") + std::strerror(errno));
    set_nonblocking(listener, true);

    if (::pipe(signal_pipe) != 0) throw TransportError("cannot create the signal pipe");
    set_cloexec(signal_pipe[0]);
    set_cloexec(signal_pipe[1]);
    set_nonblocking(signal_pipe[0], true);
    set_nonblocking(signal_pipe[1], true);
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    ::sigaction(SIGCHLD, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    prune_stored();
    retired_ = !active_generation();
    log("started: pid " + std::to_string(::getpid()) + ", epoch " + hex(epoch_) + ", generation " +
        installation_.generation + (retired_ ? ", retired" : "") + (on_demand_ ? ", on demand" : ""));
    replenish();

    while (!stop_) {
        std::vector<pollfd> polled;
        std::vector<std::pair<char, int>> owners;
        polled.push_back({listener_.get(), POLLIN, 0});
        owners.emplace_back('l', 0);
        polled.push_back({signal_pipe[0], POLLIN, 0});
        owners.emplace_back('s', 0);
        for (const auto& [key, connection] : connections_) {
            polled.push_back({connection.fd.get(), POLLIN, 0});
            owners.emplace_back('c', key);
        }
        for (const auto& [pid, worker] : workers_) {
            if (!worker.channel_open) continue;
            polled.push_back({worker.process.channel.get(), POLLIN, 0});
            owners.emplace_back('w', pid);
        }
        const auto wait = std::chrono::duration_cast<Millis>(next_deadline() - Clock::now()).count();
        const int ready = ::poll(polled.data(), polled.size(), static_cast<int>(std::clamp<long long>(wait, 0, 60'000)));
        if (ready < 0 && errno != EINTR) throw TransportError(std::string("poll failed: ") + std::strerror(errno));
        for (std::size_t i = 0; ready > 0 && i < polled.size(); ++i) {
            if (polled[i].revents == 0) continue;
            const auto [kind, key] = owners[i];
            if (kind == 'l') {
                accept_connections();
            } else if (kind == 's') {
                char signals[64];
                const auto count = ::read(signal_pipe[0], signals, sizeof signals);
                for (ssize_t j = 0; j < count; ++j) {
                    if (signals[j] == SIGTERM || signals[j] == SIGINT || signals[j] == SIGHUP) {
                        state_ = SupervisorState::Draining;
                        log("draining: signal " + std::to_string(signals[j]));
                        replenish();
                    }
                }
                reap();
            } else if (kind == 'c') {
                serve(key);
            } else if (kind == 'w') {
                handle_worker(key);
            }
        }
        reap();
        // Idle is measured from the end of the last application or
        // connection, whichever came later.
        if (live_count() > 0 || !connections_.empty()) idle_since_ = Clock::now();
        timers();
        if (can_exit()) stop_ = true;
    }
    log("exiting");
    for (auto& [pid, worker] : workers_) ::kill(pid, SIGKILL);
    // Live records of a supervisor that exits are kept for `inspect`;
    // its readers report them as unknown.
    ::unlink(endpoint_.socket.c_str());
    return 0;
}

} // namespace

int supervisor_main(const std::vector<std::string>& arguments, const fs::path& self) {
    std::optional<fs::path> root;
    bool on_demand = false;
    bool foreground = false;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == "--foreground") {
            foreground = true;
        } else if (argument == "--root" && i + 1 < arguments.size()) {
            root = fs::path(arguments[++i]);
        } else if (argument == "--on-demand") {
            on_demand = true;
        } else {
            std::fprintf(stderr, "neura: neurad: unknown argument %s\nusage: neurad --foreground --root ROOT\n",
                         argument.c_str());
            return 1;
        }
    }
    if (!foreground || !root) {
        std::fprintf(stderr, "usage: neurad --foreground --root ROOT\n");
        return 1;
    }
    auto installation = installation_of(self);
    std::error_code error;
    const auto real_root = fs::canonical(*root, error);
    if (error || real_root != installation.root) {
        std::fprintf(stderr, "neura: neurad: %s is not the root of this neurad's installation (%s)\n", root->c_str(),
                     installation.root.c_str());
        return 1;
    }
    if (installation.unmigrated) {
        std::fprintf(stderr, "neura: runtime: finish the 0.3.0 install first: run \"neura update --migrate-install\"\n");
        return status_not_run;
    }
    try {
        Supervisor supervisor(self, std::move(installation), on_demand);
        return supervisor.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "neura: neurad: %s\n", error.what());
        return 1;
    }
}

} // namespace neura::service

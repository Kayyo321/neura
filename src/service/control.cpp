// The `neura runtime` commands (docs/design/Service030.md, "Commands"):
// start, status, ps, inspect, stop, shutdown and cache clear. Only start
// starts a supervisor; the others answer from the supervisors that are
// running and from the records of those that are gone.

#include "service.hpp"

#include "cli/lifecycle.hpp"
#include "cli/tool.hpp"
#include "tooling/json.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include "platform/file.hpp"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace neura::service {

namespace {

using Millis = std::chrono::milliseconds;
using cli::RuntimeError;
using tooling::Json;

[[noreturn]] void usage_error(const std::string& message) { throw RuntimeError(message, 1); }

// A supervisor that answered, with what it listed.
struct Running {
    std::string generation;
    Session session;
    Message listing;
};

std::optional<Running> running(const Installation& installation, const std::string& generation) {
    try {
        auto session = connect_service(installation, generation, Purpose::Control, StartMode::No);
        Message list;
        list.kind = Kind::List;
        list.sequence = ++session.sequence;
        send_message(session.fd.get(), list, {}, Clock::now() + Millis(1000));
        auto reply = receive_message(session.fd.get(), Clock::now() + Millis(1000));
        if (!reply || reply->message.kind != Kind::Listing) return std::nullopt;
        return Running{generation, std::move(session), std::move(reply->message)};
    } catch (const NotRun& why) {
        if (why.reason == "identity" || why.reason == "protocol") throw RuntimeError(why.message, status_not_run);
        return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string epoch_of(const Message& message) {
    const auto bytes = message.get(Tag::Epoch);
    return hex(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

// Every generation that may have a supervisor or records.
std::vector<std::string> generations_of(const Installation& installation) {
    auto generations = live_generations(installation);
    for (const auto& recorded : recorded_generations(installation)) {
        if (std::ranges::find(generations, recorded) == generations.end()) generations.push_back(recorded);
    }
    return generations;
}

// The records a generation's state directory keeps. A live record whose
// supervisor is not the one running (`epoch`, or none) is reported unknown.
std::vector<Json> stored_records(const Installation& installation, const std::string& generation,
                                 const std::string& running_epoch) {
    std::vector<Json> records;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(state_directory(installation, generation) / "records", error)) {
        if (entry.path().filename().string().find(".tmp") != std::string::npos) continue;
        const auto text = read_text(entry.path());
        if (!text) continue;
        try {
            auto record = Json::parse(*text);
            const auto state = terminal_named(record["state"].string());
            if (!state) continue;
            if (record["epoch"].string() == running_epoch) continue;
            if (is_live(*state)) record["state"] = Json("unknown");
            records.push_back(std::move(record));
        } catch (const std::exception&) {
        }
    }
    return records;
}

std::uint64_t highest_issued(const Installation& installation, const std::string& generation,
                             const std::string& epoch_prefix) {
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(state_directory(installation, generation), error)) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with("epoch-") || name.compare(6, epoch_prefix.size(), epoch_prefix) != 0) continue;
        if (const auto text = read_text(entry.path())) {
            std::uint64_t value = 0;
            std::from_chars(text->data(), text->data() + text->size(), value);
            return value;
        }
    }
    return 0;
}

std::string text_of(const Json& value) {
    if (value.is_string()) return value.string();
    if (value.is_null()) return "-";
    return value.dump();
}

void print_records(const std::vector<Json>& records) {
    std::cout << "ID                                       STATE        STATUS  IMAGE\n";
    for (const auto& record : records) {
        auto id = record["id"].string();
        auto state = record["state"].string();
        char line[512];
        std::snprintf(line, sizeof line, "%-40s %-12s %-7s %s\n", id.c_str(), state.c_str(),
                      record.contains("status") ? text_of(record["status"]).c_str() : "-",
                      record["image"].string().c_str());
        std::cout << line;
    }
}

bool flag(std::vector<std::string>& arguments, std::string_view name) {
    const auto found = std::ranges::find(arguments, name);
    if (found == arguments.end()) return false;
    arguments.erase(found);
    return true;
}

std::optional<std::string> option(std::vector<std::string>& arguments, std::string_view name) {
    for (auto at = arguments.begin(); at != arguments.end(); ++at) {
        if (at->starts_with(std::string(name) + "=")) {
            auto value = at->substr(name.size() + 1);
            arguments.erase(at);
            return value;
        }
    }
    return std::nullopt;
}

std::uint64_t seconds_of(const std::string& text, std::string_view option_name) {
    double value = 0;
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    if (text.empty() || !end || *end != '\0' || value < 0 || value > 86400) {
        usage_error(std::string(option_name) + " expects a number of seconds");
    }
    return static_cast<std::uint64_t>(value * 1000);
}

void no_more(const std::vector<std::string>& arguments, std::string_view command) {
    if (!arguments.empty()) usage_error("unexpected argument for neura runtime " + std::string(command) + ": " + arguments.front());
}

// --- Commands -----------------------------------------------------------------------------

int start(const Installation& installation, std::vector<std::string> arguments) {
    no_more(arguments, "start");
    if (installation.unmigrated) {
        throw RuntimeError("finish the 0.3.0 install first: run \"neura update --migrate-install\"", status_not_run);
    }
    try {
        auto session = connect_service(installation, installation.generation, Purpose::Control, StartMode::Resident);
        std::cout << "the service of generation " << installation.generation << " is running (epoch "
                  << epoch_of(session.welcome).substr(0, 8) << ")\n";
        return 0;
    } catch (const NotRun& why) {
        throw RuntimeError(why.message, status_not_run);
    }
}

int status(const Installation& installation, std::vector<std::string> arguments) {
    const bool json = flag(arguments, "--json");
    no_more(arguments, "status");
    std::error_code error;
    const bool installed = fs::exists(installation.bin / tool::executable_file("neurad"), error);
    const bool login = login_registered(installation);
    Json::Array supervisors;
    for (const auto& generation : live_generations(installation)) {
        Json::Object entry{{"generation", Json(generation)}};
        if (installed) {
            if (auto found = running(installation, generation)) {
                const auto& listing = found->listing;
                entry["running"] = Json(true);
                entry["state"] = Json(std::string(name(static_cast<SupervisorState>(listing.u8(Tag::State)))));
                entry["epoch"] = Json(epoch_of(listing));
                entry["readyWorkers"] = Json(static_cast<std::int64_t>(listing.u32(Tag::ReadyWorkers)));
                entry["applications"] = Json(static_cast<std::int64_t>(listing.u32(Tag::ApplicationCount)));
                entry["cacheBytes"] = Json(static_cast<std::int64_t>(listing.u64(Tag::CacheBytes)));
                entry["build"] = Json(std::string(listing.get(Tag::Build)));
            } else {
                entry["running"] = Json(false);
            }
        } else {
            entry["running"] = Json(false);
        }
        supervisors.emplace_back(entry);
    }
    if (json) {
        Json::Object object{{"schema", Json(1)},
                            {"installed", Json(installed)},
                            {"protocol", Json(std::string(protocol_text))},
                            {"installation", Json(installation.id)},
                            {"generation", Json(installation.generation)},
                            {"loginStart", Json(login)},
                            {"unmigrated", Json(installation.unmigrated)},
                            {"supervisors", Json(supervisors)}};
        std::cout << Json(object).dump() << '\n';
        return 0;
    }
    std::cout << "service component: " << (installed ? "installed (protocol " + std::string(protocol_text) + ")"
                                                     : std::string("not installed"))
              << '\n';
    std::cout << "login start: " << (login ? "registered" : "not registered") << '\n';
    if (installation.unmigrated) std::cout << "installation: not migrated; run \"neura update --migrate-install\"\n";
    for (const auto& entry : supervisors) {
        std::cout << "generation " << entry["generation"].string() << ": ";
        if (!entry["running"].boolean()) {
            std::cout << "not running\n";
            continue;
        }
        std::cout << entry["state"].string() << ", epoch " << entry["epoch"].string().substr(0, 8) << ", "
                  << entry["applications"].integer() << " applications, " << entry["readyWorkers"].integer()
                  << " ready workers, " << entry["cacheBytes"].integer() << " cache bytes\n";
    }
    return 0;
}

int ps(const Installation& installation, std::vector<std::string> arguments) {
    const bool json = flag(arguments, "--json");
    const bool all = flag(arguments, "--all");
    no_more(arguments, "ps");
    std::vector<Json> records;
    for (const auto& generation : generations_of(installation)) {
        std::string epoch;
        if (auto found = running(installation, generation)) {
            epoch = epoch_of(found->listing);
            for (const auto value : found->listing.all(Tag::Record)) {
                try {
                    records.push_back(Json::parse(value));
                } catch (const std::exception&) {
                }
            }
        }
        for (auto& record : stored_records(installation, generation, epoch)) records.push_back(std::move(record));
    }
    std::erase_if(records, [&](const Json& record) {
        const auto state = terminal_named(record["state"].string());
        return !all && (!state || !is_live(*state));
    });
    std::ranges::sort(records, [](const Json& a, const Json& b) {
        return std::pair(a["created"].integer(), a["id"].string()) < std::pair(b["created"].integer(), b["id"].string());
    });
    if (json) {
        Json::Array array(records.begin(), records.end());
        std::cout << Json(Json::Object{{"schema", Json(1)}, {"applications", Json(array)}}).dump() << '\n';
    } else {
        print_records(records);
    }
    return 0;
}

// The record of one application: from its supervisor, or its files.
struct Found {
    std::optional<Json> record;
    Terminal state = Terminal::Unknown;
    std::optional<Running> supervisor;
};

Found find_application(const Installation& installation, const std::string& id) {
    const auto parsed = parse_application_id(id);
    const auto generations = generations_of(installation);
    if (!parsed || std::ranges::find(generations, parsed->generation) == generations.end()) {
        usage_error(id + " is not an application of this installation");
    }
    Found found;
    std::string running_epoch;
    if (auto supervisor = running(installation, parsed->generation)) {
        running_epoch = epoch_of(supervisor->listing);
        if (running_epoch.starts_with(parsed->epoch)) {
            for (const auto value : supervisor->listing.all(Tag::Record)) {
                try {
                    auto record = Json::parse(value);
                    if (record["id"].string() == id) {
                        found.state = terminal_named(record["state"].string()).value_or(Terminal::Unknown);
                        found.record = std::move(record);
                    }
                } catch (const std::exception&) {
                }
            }
            if (!found.record) found.state = parsed->number <= highest_issued(installation, parsed->generation, parsed->epoch)
                                                 ? Terminal::Expired
                                                 : Terminal::Unknown;
            found.supervisor = std::move(supervisor);
            return found;
        }
    }
    for (auto& record : stored_records(installation, parsed->generation, running_epoch)) {
        if (record["id"].string() == id) {
            found.state = terminal_named(record["state"].string()).value_or(Terminal::Unknown);
            found.record = std::move(record);
            return found;
        }
    }
    const auto highest = highest_issued(installation, parsed->generation, parsed->epoch);
    found.state = parsed->number <= highest ? Terminal::Expired : Terminal::Unknown;
    if (found.state == Terminal::Unknown) usage_error(id + " is not an application of this installation");
    return found;
}

int inspect(const Installation& installation, std::vector<std::string> arguments) {
    const bool json = flag(arguments, "--json");
    if (arguments.size() != 1) usage_error("usage: neura runtime inspect ID [--json]");
    const auto found = find_application(installation, arguments.front());
    if (!found.record) {
        if (json) {
            std::cout << Json(Json::Object{{"schema", Json(1)}, {"id", Json(arguments.front())},
                                           {"state", Json(std::string(name(found.state)))}})
                             .dump()
                      << '\n';
        } else {
            std::cout << arguments.front() << ": " << name(found.state) << '\n';
        }
        return 1;
    }
    if (json) {
        auto record = *found.record;
        std::cout << record.dump() << '\n';
        return 0;
    }
    const auto& record = *found.record;
    std::cout << "id: " << record["id"].string() << "\nstate: " << record["state"].string() << '\n';
    if (record.contains("status")) std::cout << "status: " << text_of(record["status"]) << '\n';
    if (record.contains("trapClass")) std::cout << "trap: " << record["trapClass"].string() << '\n';
    if (record.contains("signal")) std::cout << "signal: " << text_of(record["signal"]) << '\n';
    std::cout << "image: " << record["image"].string() << "\ndigest: " << record["digest"].string()
              << "\narguments: " << text_of(record["arguments"]) << "\nlifetime: " << record["lifetime"].string()
              << "\npath: " << record["path"].string() << "\nmode:";
    for (const auto& entry : record["mode"].items()) std::cout << ' ' << entry.string();
    std::cout << "\nlimits:";
    for (const auto& [limit, value] : record["limits"].fields()) std::cout << ' ' << limit << '=' << text_of(value);
    std::cout << "\ntimes (ms from the client's start):";
    for (const auto& [phase, value] : record["times"].fields()) {
        std::cout << ' ' << phase << '=' << milliseconds(static_cast<std::uint64_t>(value.integer()));
    }
    std::cout << '\n';
    return 0;
}

int stop(const Installation& installation, std::vector<std::string> arguments) {
    const auto grace_text = option(arguments, "--grace");
    const auto grace = grace_text ? seconds_of(*grace_text, "--grace") : 10'000;
    if (arguments.size() != 1) usage_error("usage: neura runtime stop ID [--grace=SECONDS]");
    const auto& id = arguments.front();
    auto found = find_application(installation, id);
    if (found.state == Terminal::Expired) {
        std::cout << id << ": expired\n";
        return 1;
    }
    if (!found.supervisor || !is_live(found.state)) {
        // Already terminal, or its supervisor is gone and with it its worker.
        std::cout << id << ": " << name(found.state) << '\n';
        return 0;
    }
    auto& session = found.supervisor->session;
    Message request;
    request.kind = Kind::Stop;
    request.sequence = ++session.sequence;
    request.add(Tag::ApplicationId, id).add_u32(Tag::GraceMilliseconds, static_cast<std::uint32_t>(grace));
    try {
        send_message(session.fd.get(), request, {}, Clock::now() + Millis(1000));
        auto reply = receive_message(session.fd.get(), Clock::now() + Millis(grace + 10'000));
        if (!reply || reply->message.kind != Kind::Status) throw TransportError("no answer");
        std::cout << id << ": " << name(static_cast<Terminal>(reply->message.u8(Tag::Terminal))) << '\n';
        return 0;
    } catch (const std::exception& error) {
        throw RuntimeError(std::string("the service did not confirm the stop: ") + error.what(), status_not_run);
    }
}

// Whether a supervisor holds the lock of `generation`'s endpoint, which it
// does from its start to its exit, whether or not it answers.
bool supervisor_holds_lock(const Installation& installation, const std::string& generation) {
    try {
        const auto endpoint = endpoint_files(run_directory(false), installation.root, generation);
#ifdef _WIN32
        std::error_code error;
        const auto lock = platform::FileLock::acquire(endpoint.lock, platform::LockMode::shared,
                                                      platform::LockWait::try_once, error, false);
        return !lock && error == std::errc::resource_unavailable_try_again;
#else
        UniqueFd fd(::open(endpoint.lock.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        if (!fd) return false;
        return ::flock(fd.get(), LOCK_SH | LOCK_NB) != 0 && errno == EWOULDBLOCK;
#endif
    } catch (const std::exception&) {
        return false;
    }
}

// Sends Shutdown to the supervisors of `generations` that answer and waits,
// until `timeout`, for them to exit. Returns the generations whose
// supervisor still runs; `waiting` receives those that were sent Shutdown.
std::vector<std::string> shut_down(const Installation& installation, const std::vector<std::string>& generations,
                                   bool terminate, Millis timeout, std::vector<std::string>& waiting) {
    for (const auto& generation : generations) {
        auto found = running(installation, generation);
        if (!found) continue;
        Message request;
        request.kind = Kind::Shutdown;
        request.sequence = ++found->session.sequence;
        request.add_u8(Tag::Terminate, terminate ? 1 : 0);
        try {
            send_message(found->session.fd.get(), request, {}, Clock::now() + Millis(1000));
            receive_message(found->session.fd.get(), Clock::now() + Millis(1000));
        } catch (const std::exception&) {
        }
        waiting.push_back(generation);
    }
    const auto deadline = Clock::now() + timeout;
    std::vector<std::string> still;
    for (;;) {
        still.clear();
        for (const auto& generation : waiting) {
            if (running(installation, generation)) still.push_back(generation);
        }
        if (still.empty() || Clock::now() >= deadline) break;
        std::this_thread::sleep_for(Millis(50));
    }
    return still;
}

int shutdown(const Installation& installation, std::vector<std::string> arguments) {
    const bool terminate = flag(arguments, "--terminate");
    const auto only = option(arguments, "--generation");
    const auto timeout_text = option(arguments, "--timeout");
    const auto timeout = Millis(timeout_text ? seconds_of(*timeout_text, "--timeout") : 60'000);
    no_more(arguments, "shutdown");
    std::vector<std::string> generations;
    for (const auto& generation : live_generations(installation)) {
        if (!only || *only == generation) generations.push_back(generation);
    }
    std::vector<std::string> waiting;
    const auto still = shut_down(installation, generations, terminate, timeout, waiting);
    if (still.empty()) {
        std::cout << (waiting.empty() ? "no supervisor is running\n" : "the service has shut down\n");
        return 0;
    }
    std::vector<Json> records;
    for (const auto& generation : still) {
        if (auto found = running(installation, generation)) {
            for (const auto value : found->listing.all(Tag::Record)) {
                auto record = Json::parse(value);
                const auto state = terminal_named(record["state"].string());
                if (state && is_live(*state)) records.push_back(std::move(record));
            }
        }
    }
    std::cerr << "neura: runtime: the service is draining; these applications are still running:\n";
    print_records(records);
    return 1;
}

int cache_clear(const Installation& installation, std::vector<std::string> arguments) {
    no_more(arguments, "cache clear");
    for (const auto& generation : live_generations(installation)) {
        auto found = running(installation, generation);
        if (!found) continue;
        Message request;
        request.kind = Kind::CacheClear;
        request.sequence = ++found->session.sequence;
        send_message(found->session.fd.get(), request, {}, Clock::now() + Millis(1000));
        auto reply = receive_message(found->session.fd.get(), Clock::now() + Millis(1000));
        if (reply && reply->message.kind == Kind::Listing) {
            std::cout << "generation " << generation << ": " << reply->message.u64(Tag::CacheBytes)
                      << " cache bytes in use\n";
        }
    }
    return 0;
}

// --- The lifecycle's side ----------------------------------------------------------------

// Every generation of the installation at `root` that may have a supervisor:
// the directories in versions/ and those with records.
std::vector<std::string> generations_at(const Installation& installation) {
    std::vector<std::string> generations;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(installation.root / "versions", error)) {
        const auto name = entry.path().filename().string();
        if (!name.starts_with(".") && entry.is_directory(error)) generations.push_back(name);
    }
    for (const auto& recorded : recorded_generations(installation)) {
        if (std::ranges::find(generations, recorded) == generations.end()) generations.push_back(recorded);
    }
    std::ranges::sort(generations);
    return generations;
}

// The applications that are not terminal, over every supervisor of the
// installation; nothing when a supervisor holds its lock but does not
// answer, or answers as another installation.
std::optional<std::size_t> running_applications(const fs::path& root) {
    const auto installation = installation_at_root(root);
    std::size_t count = 0;
    for (const auto& generation : generations_at(installation)) {
        std::optional<Running> found;
        try {
            found = running(installation, generation);
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (!found) {
            if (supervisor_holds_lock(installation, generation)) return std::nullopt;
            continue;
        }
        for (const auto value : found->listing.all(Tag::Record)) {
            try {
                const auto state = terminal_named(Json::parse(value)["state"].string());
                if (state && is_live(*state)) ++count;
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
    }
    return count;
}

// Stops every supervisor of the installation, terminating its applications
// or, without `terminate`, only once it has none.
std::string stop_service(const fs::path& root, bool terminate, Millis timeout) {
    const auto installation = installation_at_root(root);
    const auto generations = generations_at(installation);
    std::vector<std::string> asked;
    std::vector<std::string> still;
    try {
        still = shut_down(installation, generations, terminate, timeout, asked);
    } catch (const std::exception& error) {
        return std::string("the service did not stop: ") + error.what();
    }
    // A supervisor that holds its lock without answering is not stopped.
    const auto deadline = Clock::now() + Millis(2000);
    for (const auto& generation : generations) {
        if (std::ranges::find(still, generation) != still.end()) continue;
        while (supervisor_holds_lock(installation, generation) && Clock::now() < deadline) {
            std::this_thread::sleep_for(Millis(20));
        }
        if (supervisor_holds_lock(installation, generation)) still.push_back(generation);
    }
    if (still.empty()) return {};
    std::string text = "the service did not stop: the supervisor of generation " + still.front();
    for (std::size_t i = 1; i < still.size(); ++i) text += ", " + still[i];
    return text + (still.size() == 1 ? " still runs" : " still run");
}

// Retire to the supervisor of `generation`, if one answers, within 250 ms.
void retire_supervisor(const fs::path& root, const std::string& generation) {
    const auto deadline = Clock::now() + Millis(250);
    try {
        auto session = connect_service(installation_at_root(root), generation, Purpose::Control, StartMode::No);
        if (Clock::now() >= deadline) return;
        Message request;
        request.kind = Kind::Retire;
        request.sequence = ++session.sequence;
        send_message(session.fd.get(), request, {}, deadline);
        receive_message(session.fd.get(), deadline);
    } catch (const std::exception&) {
        // A supervisor also retires by itself once `current` moves.
    } catch (const NotRun&) {
    }
}

} // namespace

int runtime_command(const std::vector<std::string>& all, const fs::path& self) {
    if (all.empty()) {
        throw RuntimeError("usage: neura runtime start|status|ps|inspect|stop|shutdown|enable|disable|cache clear", 1);
    }
    const auto installation = installation_of(self);
    const auto& command = all.front();
    std::vector<std::string> arguments(all.begin() + 1, all.end());
    if (command == "start") return start(installation, std::move(arguments));
    if (command == "status") return status(installation, std::move(arguments));
    if (command == "ps") return ps(installation, std::move(arguments));
    if (command == "inspect") return inspect(installation, std::move(arguments));
    if (command == "stop") return stop(installation, std::move(arguments));
    if (command == "shutdown") return shutdown(installation, std::move(arguments));
    if (command == "enable") return login_command(installation, true, arguments);
    if (command == "disable") return login_command(installation, false, arguments);
    if (command == "cache" && !arguments.empty() && arguments.front() == "clear") {
        arguments.erase(arguments.begin());
        return cache_clear(installation, std::move(arguments));
    }
    throw RuntimeError("unknown runtime command '" + command +
                           "'; expected start, status, ps, inspect, stop, shutdown, enable, disable, or cache clear",
                       1);
}

void install_lifecycle_hooks() {
    // The tests' stand-in for the service's commands takes this client's
    // place (docs/Install.md, "Test-only hooks").
    if (const char* stand_in = std::getenv("NEURA_TEST_SERVICE_CONTROL"); stand_in && *stand_in) return;
    lifecycle::ServiceHooks hooks;
    hooks.applications = running_applications;
    hooks.terminate = [](const fs::path& root) { return stop_service(root, true, Millis(60'000)); };
    hooks.stop_idle = [](const fs::path& root) { return stop_service(root, false, Millis(10'000)); };
    hooks.retire = retire_supervisor;
    lifecycle::set_service_hooks(std::move(hooks));
}

} // namespace neura::service

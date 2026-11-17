// The client of `neura run --runtime=auto|required` (docs/design/Service030.md,
// "Prepare and commit", "Streams, signals and the client's end"): it finds
// or starts the supervisor of its installation and generation, prepares a
// launch, sends the image when the supervisor asks for it, commits with
// its own directory and streams, and waits for the outcome, forwarding
// signals. Before the commit any failure leaves nothing started, and
// `auto` may run directly; after it, never.

#include "service.hpp"

#include "cli/commands.hpp"
#include "launcher/container.hpp"
#include "tooling/json.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

extern char** environ;

namespace neura::service {

namespace {

using Millis = std::chrono::milliseconds;
using cli::RuntimeError;

// --- Signals ----------------------------------------------------------------------------

int forward_pipe[2] = {-1, -1};

extern "C" void forward_handler(int signal) {
    const int saved = errno;
    const char byte = static_cast<char>(signal);
    [[maybe_unused]] const auto written = ::write(forward_pipe[1], &byte, 1);
    errno = saved;
}

constexpr std::pair<int, ForwardedSignal> forwarded[] = {
    {SIGINT, ForwardedSignal::Int},   {SIGTERM, ForwardedSignal::Term}, {SIGHUP, ForwardedSignal::Hup},
    {SIGQUIT, ForwardedSignal::Quit}, {SIGWINCH, ForwardedSignal::Winch},
};

void install_forwarding() {
    if (::pipe(forward_pipe) != 0) return;
    set_cloexec(forward_pipe[0]);
    set_cloexec(forward_pipe[1]);
    set_nonblocking(forward_pipe[0], true);
    set_nonblocking(forward_pipe[1], true);
    for (const auto& [number, _] : forwarded) {
        struct sigaction action{};
        action.sa_handler = forward_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        ::sigaction(number, &action, nullptr);
    }
}

// --- The launch -------------------------------------------------------------------------

struct Ended {
    Terminal state = Terminal::Unknown;
    int status = 0;
    int signal = 0;
    std::string application;
    std::vector<std::pair<std::string, std::uint64_t>> times;
};

Ended ended_of(const Message& message) {
    Ended ended;
    ended.application = std::string(message.get(Tag::ApplicationId));
    ended.state = static_cast<Terminal>(message.u8(Tag::Terminal));
    ended.status = static_cast<int>(message.i64(Tag::ExitStatus));
    if (message.has(Tag::SignalNumber)) ended.signal = message.u8(Tag::SignalNumber);
    for (const auto value : message.all(Tag::Time)) ended.times.push_back(read_named_value(value));
    return ended;
}

std::optional<Ended> ended_of_record(std::string_view text, const std::string& id) {
    try {
        const auto record = tooling::Json::parse(text);
        const auto state = terminal_named(record["state"].string());
        if (!state) return std::nullopt;
        Ended ended;
        ended.application = id.empty() ? record["id"].string() : id;
        ended.state = *state;
        if (record.contains("status")) ended.status = static_cast<int>(record["status"].integer());
        if (record.contains("signal")) ended.signal = static_cast<int>(record["signal"].integer());
        return ended;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

int status_of(const Ended& ended) {
    if (ended.signal != 0 &&
        (ended.state == Terminal::Signaled || ended.state == Terminal::WorkerLost || ended.state == Terminal::Stopped ||
         ended.state == Terminal::ClientLost)) {
        std::cerr << "neura: runtime: the application was ended by signal " << ended.signal << '\n';
        return 128 + ended.signal;
    }
    return ended.status;
}

void fallback_line(const std::string& why) {
    std::cerr << "neura: runtime: running directly: " << why << '\n';
}

} // namespace

int run_direct(const cli::Options& options, Clock::time_point client_start, Report report) {
    runner::RunOptions::Ending ending;
    const auto& path = options.files.front();
    const auto bytes = cli::image_bytes_of(runner::read_file(path));
    int status = 0;
    try {
        status = cli::run_image(options, bytes, path, options.tier_stats ? std::optional(client_start) : std::nullopt,
                                &ending);
    } catch (...) {
        if (options.runtime_report) {
            report.state = ending.started ? "exited" : "load-failed";
            report.status = 1;
            write_report(*options.runtime_report, report);
        }
        throw;
    }
    if (options.runtime_report) {
        report.state = ending.trapped ? "trapped" : "exited";
        report.status = status;
        const auto at = [&](Clock::time_point point) { return clock_ns(point) - clock_ns(client_start); };
        if (ending.loaded) report.times.emplace_back("loaded", at(ending.loaded_at));
        if (ending.started) report.times.emplace_back("run-started", at(ending.started_at));
        report.times.emplace_back("exit", at(Clock::now()));
        write_report(*options.runtime_report, report);
    }
    return status;
}

namespace {

// Waits for the application's outcome on a connection that is not its
// own, by asking for its record.
std::optional<Ended> poll_outcome(const Installation& installation, const RequestId& request, const Epoch& epoch) {
    for (;;) {
        Session session;
        try {
            session = connect_service(installation, installation.generation, Purpose::Control, StartMode::No);
        } catch (const NotRun&) {
            return std::nullopt;
        }
        if (session.welcome.get(Tag::Epoch) != std::string_view(reinterpret_cast<const char*>(epoch.data()), 16)) {
            return std::nullopt;
        }
        Message query;
        query.kind = Kind::Query;
        query.sequence = 2;
        query.add(Tag::RequestId, std::string_view(reinterpret_cast<const char*>(request.data()), 16));
        try {
            send_message(session.fd.get(), query, {}, Clock::now() + Millis(1000));
            auto status = receive_message(session.fd.get(), Clock::now() + Millis(1000));
            if (!status || status->message.kind != Kind::Status) return std::nullopt;
            const auto state = static_cast<Terminal>(status->message.u8(Tag::Terminal));
            if (state == Terminal::Unknown || state == Terminal::Expired) return std::nullopt;
            if (!is_live(state)) {
                if (const auto* record = status->message.find(Tag::Record)) return ended_of_record(record->bytes, {});
                return std::nullopt;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(Millis(100));
    }
}

} // namespace

void write_report(const fs::path& file, const Report& report) {
    using tooling::Json;
    Json::Object times;
    for (const auto& [name, ns] : report.times) times[name] = Json(static_cast<std::int64_t>(ns));
    Json::Object object{
        {"schema", Json(1)},
        {"path", Json(report.path)},
        {"reason", report.reason ? Json(*report.reason) : Json(nullptr)},
        {"application", report.application ? Json(*report.application) : Json(nullptr)},
        {"state", report.state ? Json(*report.state) : Json(nullptr)},
        {"status", report.status ? Json(*report.status) : Json(nullptr)},
        {"times", Json(times)},
    };
    const auto text = Json(object).dump() + "\n";
    runner::write_file(file, std::vector<std::uint8_t>(text.begin(), text.end()));
}

int run_with_service(const cli::Options& options, Clock::time_point client_start) {
    const bool required = options.runtime == cli::RuntimeMode::Required;
    const bool detach = options.detach;
    Report report;
    report.path = "service";
    const auto since_start = [&] { return clock_ns(Clock::now()) - clock_ns(client_start); };
    // Before the commit: `auto` runs directly, `required` and a detached
    // launch fail with status 5.
    const auto not_run = [&](const NotRun& why) -> int {
        if (!required && !detach && why.fallback) {
            auto line = why.message;
            constexpr std::string_view advice = "; run without --runtime";
            if (line.ends_with(advice)) line.resize(line.size() - advice.size());
            fallback_line(line);
            report.path = "direct-fallback";
            report.reason = why.reason;
            return run_direct(options, client_start, report);
        }
        if (options.runtime_report) {
            report.path = "service";
            report.reason = why.reason;
            report.state = "not-started";
            report.status = status_not_run;
            write_report(*options.runtime_report, report);
        }
        throw RuntimeError(why.message, status_not_run);
    };

    if (const auto option = unsupported_option(options)) {
        return not_run({*option + " is not supported by the service; run without --runtime", true, "unsupported-option"});
    }
    const auto installation = installation_of(options.executable.empty() ? fs::path("/proc/self/exe")
                                                                          : options.executable);
    if (installation.unmigrated) {
        throw RuntimeError("finish the 0.3.0 install first: run \"neura update --migrate-install\"", status_not_run);
    }
    std::error_code error;
    if (!fs::exists(installation.bin / "neurad", error)) {
        return not_run({"the service component is not installed; install Neura-runtime, or run without --runtime", true,
                        "not-installed"});
    }

    // The bytes the runner would load: the image, or the one inside a
    // native executable. The supervisor reads them from a descriptor and
    // checks them against the digest.
    const auto& path = options.files.front();
    UniqueFd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!file) throw std::runtime_error("cannot read " + path.string());
    auto bytes = runner::read_file(path);
    UniqueFd image_fd;
    if (!cli::has_image_magic(bytes) && launcher::is_container(bytes)) {
        bytes = cli::image_bytes_of(std::move(bytes));
#if defined(__linux__)
        image_fd.reset(::memfd_create("neura-image", MFD_CLOEXEC));
#endif
        if (!image_fd) {
            char pattern[] = "/tmp/neura-image-XXXXXX";
            image_fd.reset(::mkstemp(pattern));
            if (image_fd) ::unlink(pattern);
        }
        std::size_t done = 0;
        while (image_fd && done < bytes.size()) {
            const auto count = ::write(image_fd.get(), bytes.data() + done, bytes.size() - done);
            if (count <= 0) break;
            done += static_cast<std::size_t>(count);
        }
    } else {
        image_fd = std::move(file);
    }
    const auto lifetime = detach ? Lifetime::Detached : Lifetime::Foreground;
    const auto envelope = make_envelope(options, bytes, lifetime, client_start);
    RequestId request{};
    random_bytes(request);
    Message prepare;
    prepare.kind = Kind::Prepare;
    prepare.add(Tag::RequestId, std::string_view(reinterpret_cast<const char*>(request.data()), 16));
    put_envelope(prepare, envelope);
    try {
        encode(prepare);
    } catch (const ProtocolError&) {
        return not_run({"the launch's environment and arguments do not fit in one message of 1 MiB", true,
                        "unsupported-option"});
    }

    // The detached streams are opened by the client, for appending.
    UniqueFd detached_out;
    UniqueFd detached_err;
    UniqueFd detached_in;
    if (detach) {
        const auto open_output = [](const fs::path& file) {
            UniqueFd fd(::open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666));
            if (!fd) throw RuntimeError("cannot open " + file.string() + ": " + std::strerror(errno), 1);
            return fd;
        };
        detached_out = open_output(*options.detach_stdout);
        detached_err = open_output(*options.detach_stderr);
        detached_in.reset(::open(options.detach_stdin ? options.detach_stdin->c_str() : "/dev/null",
                                 O_RDONLY | O_CLOEXEC));
        if (!detached_in) {
            throw RuntimeError("cannot open " + options.detach_stdin->string() + ": " + std::strerror(errno), 1);
        }
    }

    Session session;
    try {
        session = connect_service(installation, installation.generation, Purpose::Launch, StartMode::OnDemand);
    } catch (const NotRun& why) {
        return not_run(why);
    }
    report.times.emplace_back("connected", since_start());
    Epoch epoch{};
    {
        const auto bytes_of_epoch = session.welcome.get(Tag::Epoch);
        if (bytes_of_epoch.size() == 16) std::copy(bytes_of_epoch.begin(), bytes_of_epoch.end(), epoch.begin());
    }
    if (session.welcome.get(Tag::Build) != own_build() ||
        session.welcome.get(Tag::Generation) != installation.generation) {
        return not_run({"the supervisor that answered is " + std::string(session.welcome.get(Tag::Build)) +
                            ", of another build; nothing was run (identity)",
                        false, "identity"});
    }

    // A refusal before the commit.
    const auto refused = [&](const Message& message) -> int {
        const auto reason_text = std::string(message.get(Tag::Reason));
        const auto reason = reason_named(reason_text);
        const auto sentence = std::string(message.get(Tag::Sentence));
        const bool fallback = reason && falls_back(*reason);
        if (fallback) {
            return not_run({required || detach ? "the service refused the launch: " + sentence + " (" + reason_text + ")"
                                               : sentence + " (" + reason_text + ")",
                            true, reason_text});
        }
        return not_run({sentence + "; nothing was run (" + reason_text + ")", false, reason_text});
    };
    const auto exchange = [&](Message& message, const std::vector<int>& fds, Millis wait) -> std::optional<Received> {
        message.sequence = ++session.sequence;
        send_message(session.fd.get(), message, fds, Clock::now() + wait);
        return receive_message(session.fd.get(), Clock::now() + wait);
    };

    Message prepared;
    try {
        auto reply = exchange(prepare, {}, Millis(1000));
        if (!reply) return not_run({"the service closed the connection; nothing was run", true, "disconnected"});
        if (reply->message.kind == Kind::Rejected) return refused(reply->message);
        if (reply->message.kind != Kind::Prepared) throw ProtocolError("expected Prepared");
        prepared = std::move(reply->message);
        report.times.emplace_back("accepted", since_start());
        if (prepared.u8(Tag::ImageNeeded) != 0) {
            Message image;
            image.kind = Kind::Image;
            image.add(Tag::Token, prepared.get(Tag::Token)).add_u8(Tag::Descriptors, 1);
            const auto wait = Millis(5000 + 1000 * static_cast<long long>(bytes.size() >> 26));
            auto sent = exchange(image, {image_fd.get()}, wait);
            if (!sent) return not_run({"the service closed the connection; nothing was run", true, "disconnected"});
            if (sent->message.kind == Kind::Rejected) return refused(sent->message);
            report.times.emplace_back("image-sent", since_start());
        }
    } catch (const TransportError& error) {
        return not_run({std::string("the service did not answer: ") + error.what(), true, "timeout"});
    } catch (const ProtocolError& error) {
        return not_run({std::string("the service sent a malformed message (") + error.what() + "); nothing was run",
                        false, "protocol"});
    }

    // The commit: from here the launch is never run directly.
    UniqueFd cwd(::open(".", O_RDONLY | O_CLOEXEC
#ifdef O_DIRECTORY
                                 | O_DIRECTORY
#endif
                        ));
    if (!cwd) throw RuntimeError(std::string("cannot open the working directory: ") + std::strerror(errno), 1);
    if (!detach) install_forwarding();
    const auto request_text = hex(request);
    const auto unknown = [&](const std::string& id) -> int {
        if (options.runtime_report) {
            report.path = "service";
            report.application = id.empty() ? std::nullopt : std::optional(id);
            report.state = "unknown";
            report.status = status_unknown;
            write_report(*options.runtime_report, report);
        }
        throw RuntimeError("the outcome of application " + (id.empty() ? "request " + request_text : id) +
                               " is unknown; it was not run again. Try \"neura runtime inspect " +
                               (id.empty() ? std::string("<ID>") : id) + "\"",
                           status_unknown);
    };
    std::string application;
    Message commit;
    commit.kind = Kind::Commit;
    commit.add(Tag::Token, prepared.get(Tag::Token)).add_u8(Tag::Descriptors, 4);
    const std::vector<int> streams = detach ? std::vector<int>{cwd.get(), detached_in.get(), detached_out.get(),
                                                               detached_err.get()}
                                            : std::vector<int>{cwd.get(), 0, 1, 2};
    std::optional<Received> committed;
    try {
        commit.sequence = ++session.sequence;
        send_message(session.fd.get(), commit, streams, Clock::now() + Millis(1000));
        // The supervisor answers once a worker has the launch; a worker
        // started for it may take up to its 2 seconds to be ready.
        committed = receive_message(session.fd.get(), Clock::now() + Millis(5000));
    } catch (const std::exception&) {
        committed.reset();
    }
    if (committed && committed->message.kind == Kind::Rejected) {
        const auto reason = std::string(committed->message.get(Tag::Reason));
        const bool not_started = committed->message.u8(Tag::NotStarted) != 0;
        if (not_started) {
            if (options.runtime_report) {
                report.path = "service";
                report.reason = reason;
                report.state = "not-started";
                report.status = status_not_run;
                write_report(*options.runtime_report, report);
            }
            throw RuntimeError("the service refused the launch: " + std::string(committed->message.get(Tag::Sentence)) +
                                   " (" + reason + ")",
                               status_not_run);
        }
    }
    std::optional<Ended> ended;
    if (!committed || committed->message.kind != Kind::Committed) {
        // The commit was sent; whether it ran is not known here.
        ended = poll_outcome(installation, request, epoch);
        if (!ended) return unknown(application);
    } else {
        application = std::string(committed->message.get(Tag::ApplicationId));
        report.application = application;
        const bool warm = committed->message.has(Tag::Path) &&
                          committed->message.u8(Tag::Path) == static_cast<std::uint8_t>(LaunchPath::Warm);
        report.path = warm ? "service-warm" : "service-fresh";
        report.times.emplace_back("committed", since_start());
        if (detach) {
            std::cout << application << '\n';
            std::cout.flush();
            if (options.runtime_report) {
                report.state = "running";
                write_report(*options.runtime_report, report);
            }
            return 0;
        }
        // Wait for the outcome, forwarding signals.
        MessageReader reader;
        bool lost = false;
        while (!ended && !lost) {
            pollfd polled[2] = {{session.fd.get(), POLLIN, 0}, {forward_pipe[0], POLLIN, 0}};
            const int ready = ::poll(polled, forward_pipe[0] >= 0 ? 2 : 1, -1);
            if (ready < 0) {
                if (errno == EINTR) continue;
                lost = true;
                break;
            }
            if (forward_pipe[0] >= 0 && (polled[1].revents & POLLIN)) {
                char signals[16];
                const auto count = ::read(forward_pipe[0], signals, sizeof signals);
                for (ssize_t i = 0; i < count; ++i) {
                    for (const auto& [number, name] : forwarded) {
                        if (number != signals[i]) continue;
                        Message signal;
                        signal.kind = Kind::Signal;
                        signal.sequence = ++session.sequence;
                        signal.add(Tag::ApplicationId, application).add_u8(Tag::SignalNumber, static_cast<std::uint8_t>(name));
                        try {
                            send_message(session.fd.get(), signal, {}, Clock::now() + Millis(1000));
                        } catch (const std::exception&) {
                            lost = true;
                        }
                    }
                }
            }
            if (polled[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                try {
                    if (!reader.fill(session.fd.get())) {
                        lost = true;
                        break;
                    }
                    while (auto message = reader.take()) {
                        if (message->message.kind == Kind::Outcome) ended = ended_of(message->message);
                    }
                } catch (const std::exception&) {
                    lost = true;
                }
            }
        }
        if (!ended) {
            ended = poll_outcome(installation, request, epoch);
            if (!ended) return unknown(application);
        }
    }
    if (options.runtime_report) {
        report.state = std::string(name(ended->state));
        report.status = ended->status;
        for (const auto& time : ended->times) report.times.push_back(time);
        report.times.emplace_back("client-end", since_start());
        write_report(*options.runtime_report, report);
    }
    return status_of(*ended);
}

} // namespace neura::service

// A stand-in supervisor for one worker of the runtime service
// (docs/design/Service030.md, "The worker"; order of work, step 5). It reads
// the options of a run as `neura run` does, builds the launch envelope from
// its own process, starts `neurad --worker`, gives it the launch and reports
// how it ended. The worker writes to the driver's own streams.
//
// usage: neura_service_worker_driver NEURAD [--supervisor-env NAME=VALUE]...
//            [--report FILE] [--pid-file FILE] [--exit-after-started]
//            [--remove-cwd] [--second-assignment] -- RUN-OPTIONS... IMAGE [-- ARGS]

#include "cli/commands.hpp"
#include "service/service.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

using namespace neura::service;

namespace {

std::string build_of(const std::string& neurad) {
    const std::string command = "'" + neurad + "' --build";
    std::FILE* pipe = ::popen(command.c_str(), "r");
    if (!pipe) return {};
    std::string text;
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe)) text += buffer;
    ::pclose(pipe);
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}

int fail(const std::string& message) {
    std::fprintf(stderr, "worker driver: %s\n", message.c_str());
    return 70;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return fail("usage: neura_service_worker_driver NEURAD [options] -- RUN-OPTIONS... IMAGE");
    const std::string neurad = argv[1];
    std::vector<std::string> supervisor_environment;
    std::string report_path;
    std::string pid_path;
    bool exit_after_started = false;
    bool remove_cwd = false;
    bool second_assignment = false;
    int i = 2;
    for (; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--") {
            ++i;
            break;
        }
        if (argument == "--supervisor-env" && i + 1 < argc) supervisor_environment.emplace_back(argv[++i]);
        else if (argument == "--report" && i + 1 < argc) report_path = argv[++i];
        else if (argument == "--pid-file" && i + 1 < argc) pid_path = argv[++i];
        else if (argument == "--exit-after-started") exit_after_started = true;
        else if (argument == "--remove-cwd") remove_cwd = true;
        else if (argument == "--second-assignment") second_assignment = true;
        else return fail("unknown option " + argument);
    }
    std::vector<std::string> run_arguments{"neura-run", "run"};
    for (; i < argc; ++i) run_arguments.emplace_back(argv[i]);
    auto run_argv = neura::tool::argv_of(run_arguments);
    try {
        const auto start = Clock::now();
        const auto options = neura::cli::read_options(static_cast<int>(run_arguments.size()), run_argv.data());
        const auto bytes = neura::cli::image_bytes_of(neura::runner::read_file(options.files.front()));
        const auto envelope = make_envelope(options, bytes, Lifetime::Foreground, start);

        // The image, as a supervisor's cache would hold it.
        const int memory = ::memfd_create("driver-image", MFD_CLOEXEC);
        if (memory < 0 || ::write(memory, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) {
            return fail("cannot hold the image");
        }
        UniqueFd image(memory);

        // The worker starts with the "supervisor's" environment: the
        // driver's, with the given variables replaced or added.
        std::vector<std::string> environment;
        for (char** entry = environ; entry && *entry; ++entry) {
            const std::string text = *entry;
            const auto name = text.substr(0, text.find('='));
            bool replaced = false;
            for (const auto& extra : supervisor_environment) {
                replaced = replaced || extra.substr(0, extra.find('=')) == name;
            }
            if (!replaced) environment.push_back(text);
        }
        environment.insert(environment.end(), supervisor_environment.begin(), supervisor_environment.end());

        auto worker = spawn_worker(neurad, build_of(neurad), &environment, nullptr);
        auto ready = receive_message(worker.channel.get(), Clock::now() + std::chrono::seconds(10));
        if (!ready || ready->message.kind != Kind::WorkerReady) return fail("the worker did not report ready");

        UniqueFd cwd(::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (remove_cwd) {
            char here[4096];
            if (::getcwd(here, sizeof here) == nullptr || ::rmdir(here) != 0) return fail("cannot remove the directory");
        }
        Message assignment;
        assignment.kind = Kind::WorkerAssign;
        assignment.add_u8(Tag::Descriptors, 5).add(Tag::ApplicationId, "dev.00000000.1");
        put_envelope(assignment, envelope);
        send_message(worker.channel.get(), assignment, {image.get(), cwd.get(), 0, 1, 2});
        if (!pid_path.empty()) std::ofstream(pid_path) << worker.pid << '\n';

        std::string terminal = "worker-lost";
        int status = -1;
        std::string trap_class;
        std::vector<std::string> times;
        for (;;) {
            std::optional<Received> received;
            try {
                received = receive_message(worker.channel.get(), std::nullopt);
            } catch (const TransportError&) {
                break;
            }
            if (!received) break;
            const auto& message = received->message;
            if (message.kind == Kind::WorkerStarted) {
                for (const auto value : message.all(Tag::Time)) times.push_back(read_named_value(value).first);
                if (exit_after_started) std::_Exit(99);
            } else if (message.kind == Kind::WorkerFinished) {
                terminal = std::string(name(static_cast<Terminal>(message.u8(Tag::Terminal))));
                status = static_cast<int>(message.i64(Tag::ExitStatus));
                if (message.has(Tag::TrapClass)) trap_class = std::string(message.get(Tag::TrapClass));
                for (const auto value : message.all(Tag::Time)) times.push_back(read_named_value(value).first);
            }
        }
        int wait_status = 0;
        ::waitpid(worker.pid, &wait_status, 0);
        int signal = 0;
        if (WIFSIGNALED(wait_status)) {
            signal = WTERMSIG(wait_status);
            if (status < 0) {
                terminal = signal == SIGKILL ? "worker-lost" : "signaled";
                status = 128 + signal;
            }
        } else if (status < 0) {
            status = WEXITSTATUS(wait_status);
        }
        std::string second = "not tried";
        if (second_assignment) {
            try {
                std::signal(SIGPIPE, SIG_IGN);
                send_message(worker.channel.get(), assignment, {image.get(), cwd.get(), 0, 1, 2});
                second = "accepted";
            } catch (const std::exception&) {
                second = "refused";
            }
        }
        if (!report_path.empty()) {
            std::ofstream report(report_path);
            report << "{\"terminal\":\"" << terminal << "\",\"status\":" << status << ",\"signal\":" << signal
                   << ",\"trapClass\":\"" << trap_class << "\",\"pid\":" << worker.pid << ",\"second\":\"" << second
                   << "\",\"times\":[";
            for (std::size_t t = 0; t < times.size(); ++t) report << (t ? "," : "") << '"' << times[t] << '"';
            report << "]}\n";
        }
        return status;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}

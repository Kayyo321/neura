// The cached environment readers of the runtime (docs/design/Service030.md,
// "The worker"): a process that holds its environment, as a worker of the
// service does until it has the caller's, aborts at the first cached read;
// after the environment is bound, the readers see the replaced values.
//
// Usage: neura_service_environment_tests

#include "jit/jit.hpp"
#include "nvm/environment.hpp"
#include "nvm/tier1.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>

#ifdef _WIN32
#include "platform/console.hpp"
#include "platform/environment.hpp"
#include "platform/process.hpp"
#include "platform/self.hpp"
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL " << what << '\n';
}

} // namespace

#ifdef _WIN32

// Windows has no fork: each case runs in a new process of this program,
// `neura_service_environment_tests --child N`, with its errors on NUL. The
// MSVC C runtime's abort ends a process with exit code 3 (W10).
namespace {

const std::pair<const char*, void (*)()> readers[] = {
    {"NEURA_TIER", [] { neura::nvm::tier_ceiling(); }},
    {"NEURA_TIER_CHECK", [] { neura::nvm::tier_check_enabled(); }},
    {"NEURA_TIER_CHECK_HEAP", [] { neura::nvm::tier_shadow_heap_always(); }},
    {"NEURA_TIER_EAGER", [] { neura::nvm::tier_eager(); }},
    {"NEURA_TIER_PROGRAMS", [] { neura::nvm::tier_programs_enabled(); }},
    {"NEURA_TIER3", [] { neura::nvm::tier3_environment(); }},
    {"NEURA_TIER3 (implicit)", [] { neura::nvm::tier3_environment_implicit(); }},
    {"NEURA_TIER3_AFTER", [] { neura::nvm::tier3_after(); }},
    {"NEURA_JIT", [] { neura::jit::environment_mode(); }},
};
constexpr int bound_case = static_cast<int>(std::size(readers));

int child(int which) {
    if (which >= 0 && which < bound_case) {
        neura::nvm::hold_environment();
        readers[which].second();
        return 0;
    }
    // Bound after replacement: the reader sees the new value, not the old.
    std::error_code error;
    neura::platform::set_env("NEURA_TIER", "2", error);
    neura::platform::set_env("NEURA_JIT", "on", error);
    neura::nvm::hold_environment();
    neura::platform::set_env("NEURA_TIER", "1", error);
    neura::platform::set_env("NEURA_JIT", "off", error);
    neura::nvm::bind_environment();
    return neura::nvm::tier_ceiling() == 1 && neura::jit::environment_mode() == neura::jit::Mode::Off ? 0 : 3;
}

std::optional<neura::platform::ExitStatus> in_child(int which) {
    neura::platform::Spawn spawn;
    spawn.program = neura::platform::executable_path();
    spawn.arguments = {"neura_service_environment_tests", "--child", std::to_string(which)};
    // The abort message is expected; keep the test output readable.
    spawn.input = neura::platform::Stdio::null();
    spawn.error = neura::platform::Stdio::null();
    std::error_code error;
    auto process = neura::platform::Process::start(spawn, error);
    if (!process) {
        std::cout << "environment_tests: cannot start a child: " << error.message() << '\n';
        return std::nullopt;
    }
    return process.wait();
}

bool aborted(const std::optional<neura::platform::ExitStatus>& status) {
    return status && *status == neura::platform::ExitStatus::exited(3);
}

} // namespace

int main(int argc, char** argv) {
    neura::platform::set_unattended_error_mode();
    if (argc == 3 && std::string(argv[1]) == "--child") return child(std::atoi(argv[2]));
    check(neura::nvm::environment_bound(), "a process starts with its environment bound");
    for (int which = 0; which < bound_case; ++which) {
        check(aborted(in_child(which)), std::string("a held environment aborts the reader of ") + readers[which].first);
    }
    const auto status = in_child(bound_case);
    check(status && status->success(), "a bound environment is read after it was replaced");
    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "service environment passed\n";
    return 0;
}

#else

namespace {

// Runs `body` in a child process and returns its wait status.
int in_child(const std::function<int()>& body) {
    std::fflush(stdout);
    const pid_t child = ::fork();
    if (child == 0) {
        // The abort message is expected; keep the test output readable. If
        // /dev/null cannot be opened the message is shown, which is harmless.
        if (std::freopen("/dev/null", "w", stderr) == nullptr) {
            std::fputs("environment_tests: cannot silence stderr\n", stdout);
        }
        std::_Exit(body());
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    return status;
}

bool aborted(int status) { return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT; }

} // namespace

int main() {
    check(neura::nvm::environment_bound(), "a process starts with its environment bound");
    const std::pair<const char*, std::function<void()>> readers[] = {
        {"NEURA_TIER", [] { neura::nvm::tier_ceiling(); }},
        {"NEURA_TIER_CHECK", [] { neura::nvm::tier_check_enabled(); }},
        {"NEURA_TIER_CHECK_HEAP", [] { neura::nvm::tier_shadow_heap_always(); }},
        {"NEURA_TIER_EAGER", [] { neura::nvm::tier_eager(); }},
        {"NEURA_TIER_PROGRAMS", [] { neura::nvm::tier_programs_enabled(); }},
        {"NEURA_TIER3", [] { neura::nvm::tier3_environment(); }},
        {"NEURA_TIER3 (implicit)", [] { neura::nvm::tier3_environment_implicit(); }},
        {"NEURA_TIER3_AFTER", [] { neura::nvm::tier3_after(); }},
        {"NEURA_JIT", [] { neura::jit::environment_mode(); }},
    };
    for (const auto& [name, read] : readers) {
        check(aborted(in_child([&] {
                  neura::nvm::hold_environment();
                  read();
                  return 0;
              })),
              std::string("a held environment aborts the reader of ") + name);
    }
    // Bound after replacement: the reader sees the new value, not the old.
    const int status = in_child([] {
        ::setenv("NEURA_TIER", "2", 1);
        ::setenv("NEURA_JIT", "on", 1);
        neura::nvm::hold_environment();
        ::setenv("NEURA_TIER", "1", 1);
        ::setenv("NEURA_JIT", "off", 1);
        neura::nvm::bind_environment();
        return neura::nvm::tier_ceiling() == 1 && neura::jit::environment_mode() == neura::jit::Mode::Off ? 0 : 3;
    });
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "a bound environment is read after it was replaced");
    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "service environment passed\n";
    return 0;
}

#endif

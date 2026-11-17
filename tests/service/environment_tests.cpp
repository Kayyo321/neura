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

#include <sys/wait.h>
#include <unistd.h>

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL " << what << '\n';
}

// Runs `body` in a child process and returns its wait status.
int in_child(const std::function<int()>& body) {
    std::fflush(stdout);
    const pid_t child = ::fork();
    if (child == 0) {
        // The abort message is expected; keep the test output readable.
        std::freopen("/dev/null", "w", stderr);
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

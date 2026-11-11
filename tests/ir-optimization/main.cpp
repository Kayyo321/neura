#include "support.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#define NEURA_TESTS_HAVE_PTHREAD 1
#endif

namespace {

// Runs every test registered for the suite and reports each failure.
int run_suite(int argc, char** argv) {
    using namespace neura::testing;
    if (argc != 3) {
        std::cerr << "usage: neura_ir_tests <structure|verifier|behavior|concurrency> <source-dir>\n";
        return 2;
    }
    const std::string suite = argv[1];
    const Environment environment{fs::canonical(argv[2])};
    library_root() = environment.source_dir / "stdlib";
    std::size_t run = 0;
    std::size_t failed = 0;
    for (const auto& test : registry()) {
        if (test.suite != suite) {
            continue;
        }
        ++run;
        const auto start = std::chrono::steady_clock::now();
        try {
            test.run(environment);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            std::cout << "PASS " << test.suite << '.' << test.name << " (" << elapsed.count() << " ms)\n";
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << test.suite << '.' << test.name << "\n  " << error.what() << '\n';
        }
    }
    if (run == 0) {
        std::cerr << "no tests registered for suite '" << suite << "'\n";
        return 2;
    }
    std::cout << run - failed << " of " << run << " " << suite << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

#ifdef NEURA_TESTS_HAVE_PTHREAD
struct Invocation {
    int argc;
    char** argv;
    int status;
};

void* run_on_thread(void* raw) {
    auto* invocation = static_cast<Invocation*>(raw);
    try {
        invocation->status = run_suite(invocation->argc, invocation->argv);
    } catch (const std::exception& error) {
        std::cerr << "neura tests: " << error.what() << '\n';
        invocation->status = 2;
    }
    return nullptr;
}
#endif

} // namespace

// Usage: neura_ir_tests <suite> <source-dir>
//
// The reference evaluator recurses on the native stack, once per evaluated
// call and expression, up to the call depth its tests allow (400). An
// instrumented build's frames are several times larger, so the process's
// default stack is not a bound the tests can rely on. The suite therefore runs
// on a thread whose stack size is set here. The pages are reserved, not
// committed, so a run that stays shallow pays nothing for it.
int main(int argc, char** argv) {
#ifdef NEURA_TESTS_HAVE_PTHREAD
    constexpr std::size_t stack_bytes = std::size_t{1} << 30;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) == 0) {
        Invocation invocation{argc, argv, 2};
        pthread_t thread;
        const bool started = pthread_attr_setstacksize(&attributes, stack_bytes) == 0 &&
                             pthread_create(&thread, &attributes, run_on_thread, &invocation) == 0;
        pthread_attr_destroy(&attributes);
        if (started) {
            pthread_join(thread, nullptr);
            return invocation.status;
        }
    }
#endif
    return run_suite(argc, argv);
}

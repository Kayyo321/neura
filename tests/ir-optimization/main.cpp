#include "support.hpp"

#include <chrono>
#include <exception>
#include <iostream>

// Usage: neura_ir_tests <suite> <source-dir>
// Runs every test registered for `suite` and reports each failure.
int main(int argc, char** argv) {
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

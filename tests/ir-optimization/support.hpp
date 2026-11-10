#pragma once

#include "neura-ir/ir.hpp"

#include <filesystem>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neura::testing {

namespace fs = std::filesystem;

struct Environment {
    fs::path source_dir;

    fs::path fixtures() const { return source_dir / "tests" / "ir-optimization" / "fixtures"; }
    fs::path examples() const { return source_dir / "examples"; }
};

using TestFunction = void (*)(const Environment&);

struct TestCase {
    std::string suite;
    std::string name;
    TestFunction run;
};

std::vector<TestCase>& registry();

struct Registration {
    Registration(const char* suite, const char* name, TestFunction run);
};

#define NEURA_TEST(suite, name)                                                         \
    static void suite##_##name(const ::neura::testing::Environment&);                    \
    static const ::neura::testing::Registration suite##_##name##_registration{#suite, #name, \
                                                                             suite##_##name}; \
    static void suite##_##name([[maybe_unused]] const ::neura::testing::Environment& environment)

class Failure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(std::string_view message, std::source_location where = std::source_location::current());

inline void check(bool condition, std::string_view message,
                  std::source_location where = std::source_location::current()) {
    if (!condition) {
        fail(message, where);
    }
}

template <typename Actual, typename Expected>
void check_equal(const Actual& actual, const Expected& expected, std::string_view what,
                 std::source_location where = std::source_location::current()) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << what << "\n  expected: " << expected << "\n  actual:   " << actual;
        fail(message.str(), where);
    }
}

// The trusted standard library root (<source-dir>/stdlib) that test
// programs resolve Std imports from. main() sets it from the source directory.
fs::path& library_root();

// Lowers files through the same source loader as the CLI. Each file's
// directory is added to the import roots, as the CLI does, and Std imports
// resolve in library_root().
IrProgram lower_files(std::vector<fs::path> files, std::vector<fs::path> roots = {});
IrProgram lower_fixture(const Environment& environment, std::string_view file_name);
std::vector<fs::path> fixture_files(const Environment& environment);
std::vector<fs::path> example_files(const Environment& environment);

// Finds a function or binding by its qualified name, such as
// "ControlFlow.skip_loop". Fails unless exactly one symbol matches.
SymbolId find_symbol(const IrProgram& program, std::string_view qualified_name);
std::string qualified_name(const IrProgram& program, SymbolId id);
const std::vector<IrNode>& body_of(const IrProgram& program, std::string_view qualified_name);
std::vector<IrNode>& body_of(IrProgram& program, std::string_view qualified_name);

// A compact rendering of IR structure without types and spans, for example
// "(return (binary + (load value) (integer 1)))".
std::string shape(const IrProgram& program, const std::vector<IrNode>& nodes);
std::string shape(const IrProgram& program, const IrNode& node);

std::string printed(const IrProgram& program);
bool same_symbols(const IrProgram& left, const IrProgram& right);
bool same_span(const Span& left, const Span& right);
std::string describe(const Span& span);

} // namespace neura::testing

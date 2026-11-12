#include "tooling/coverage.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <filesystem>

namespace {
void check(bool condition, const char* expression, int line) {
    if (!condition)
        throw std::runtime_error("coverage check failed at line " + std::to_string(line) + ": " + expression);
}
} // namespace

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

int main() try {
    neura::nvr::Image image;
    image.source_map = std::vector<neura::nvr::SourceLocation>{
        {0, 0, "source.nra", 5, 1}, {0, 1, "source.nra", 5, 8},
        {0, 2, "source.nra", 9, 1}, {1, 0, "std/test.nra", 20, 1}};
    const auto path = std::filesystem::temp_directory_path() / "neura-coverage-fixture.nra";
    neura::tooling::LineCoverage coverage(image, {{"source.nra", path}});
    coverage.block(0, 0);
    coverage.block(0, 0);
    coverage.block(0, 1);
    coverage.block(1, 0); // Standard library is excluded from project coverage.
    coverage.block(99, 99); // Missing source locations cannot invent hits.
    const auto report = coverage.report();
    CHECK(report["version"].integer() == 1);
    CHECK(report["granularity"].string() == "mapped-block-line");
    CHECK(report["files"].size() == 1);
    const auto& statements = report["files"][0]["statements"];
    CHECK(statements.size() == 2);
    CHECK(statements[0]["line"].integer() == 5);
    CHECK(statements[0]["count"].integer() == 3);
    CHECK(statements[1]["line"].integer() == 9);
    CHECK(statements[1]["count"].integer() == 0);
    CHECK(!report["files"][0].contains("branches"));
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}

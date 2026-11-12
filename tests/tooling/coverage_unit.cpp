#include "tooling/coverage.hpp"

#include <cassert>
#include <filesystem>

int main() {
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
    assert(report["version"].integer() == 1);
    assert(report["granularity"].string() == "mapped-block-line");
    assert(report["files"].size() == 1);
    const auto& statements = report["files"][0]["statements"];
    assert(statements.size() == 2);
    assert(statements[0]["line"].integer() == 5);
    assert(statements[0]["count"].integer() == 3);
    assert(statements[1]["line"].integer() == 9);
    assert(statements[1]["count"].integer() == 0);
    assert(!report["files"][0].contains("branches"));
}

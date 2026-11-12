#pragma once

#include "neura-ir/ir.hpp"
#include "tooling/json.hpp"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace neura::tooling {

// A test's id is relative to the process working directory, which is the
// workspace folder for editor clients. It is independent of line numbers,
// traversal order, and the compiler's internal symbol numbers.
struct TestInfo {
    SymbolId symbol;
    std::string id;
    std::string name;
    std::string label;
    std::string file;
    std::string path;
    Span span;
};

std::vector<TestInfo> discover_tests(const IrProgram& program,
                                     const std::vector<std::filesystem::path>& inputs,
                                     const std::filesystem::path& base);
Json test_info_json(const TestInfo& test);
Json test_output_json(std::string_view id, std::string_view stream, std::string_view bytes);

// Keep the protocol on a duplicate of stdout so output produced by native
// libraries cannot inject unframed bytes into it while a test is running.
class TestReporter {
public:
    TestReporter();
    ~TestReporter();
    TestReporter(const TestReporter&) = delete;
    TestReporter& operator=(const TestReporter&) = delete;
    void event(std::string_view kind, Json data = Json::Object{});

private:
    std::FILE* stream_ = nullptr;
};

// Capture both operating-system descriptors, rather than just ConsoleSink:
// this also catches printf/write from native imports. Spooling to temporary
// files keeps large test output out of the runner's memory. With a live
// callback, POSIX clients receive bounded chunks while the test runs, including
// before cancellation. Otherwise finish() drains the output. Descriptors
// restore on failure.
class TestOutputCapture {
public:
    using Emitter = std::function<void(std::string_view, std::string_view)>;
    explicit TestOutputCapture(Emitter live = {});
    ~TestOutputCapture();
    TestOutputCapture(const TestOutputCapture&) = delete;
    TestOutputCapture& operator=(const TestOutputCapture&) = delete;
    void finish(const Emitter& emit);

private:
    int saved_output_ = -1;
    int saved_error_ = -1;
    std::FILE* output_ = nullptr;
    std::FILE* error_ = nullptr;
    struct Live;
    std::unique_ptr<Live> live_;
    void restore() noexcept;
    void stop_live() noexcept;
};

} // namespace neura::tooling

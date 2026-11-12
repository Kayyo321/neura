#include "tooling/test_protocol.hpp"

#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* expression, int line) {
    if (!condition)
        throw std::runtime_error("output check failed at line " + std::to_string(line) + ": " + expression);
}
} // namespace

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

int main() try {
    using namespace neura::tooling;
    const std::string binary("A\0\xff", 3);
    const auto encoded = test_output_json("test", "stdout", binary);
    CHECK(encoded["encoding"].string() == "base64");
    CHECK(encoded["data"].string() == "QQD/");
    CHECK(encoded["output"].string() == std::string("A\0\xef\xbf\xbd", 5));
    CHECK(!test_output_json("test", "stdout", "valid 💙").contains("encoding"));
    std::string output;
    std::string error;
    const auto append = [&](std::string_view stream, std::string_view text) {
        (stream == "stdout" ? output : error).append(text);
    };
    const std::string long_text = std::string(16383, 'a') + "💙" + std::string(10, 'b');
    {
        TestOutputCapture capture;
        std::fwrite(long_text.data(), 1, long_text.size(), stdout);
        std::fwrite(binary.data(), 1, binary.size(), stderr);
        capture.finish(append);
    }
    CHECK(output == long_text);
    CHECK(error == binary);
#ifndef _WIN32
    std::mutex mutex;
    std::condition_variable ready;
    std::string live_output;
    bool live_stream_valid = true;
    {
        TestOutputCapture capture([&](std::string_view stream, std::string_view text) {
            {
                std::lock_guard lock(mutex);
                live_stream_valid = live_stream_valid && stream == "stdout";
                live_output.append(text);
            }
            ready.notify_one();
        });
        std::fputs("live before completion", stdout);
        std::fflush(stdout);
        {
            std::unique_lock lock(mutex);
            CHECK(ready.wait_for(lock, std::chrono::seconds(2), [&] { return !live_output.empty(); }));
        }
        std::fputs(" and final", stdout);
        capture.finish(append);
    }
    CHECK(live_stream_valid);
    CHECK(live_output == "live before completion and final");
#endif
    bool unwound = false;
    try {
        TestOutputCapture capture;
        std::fputs("discarded before failure", stdout);
        throw std::runtime_error("intentional capture unwind");
    } catch (const std::runtime_error& error_during_capture) {
        CHECK(std::string(error_during_capture.what()) == "intentional capture unwind");
        unwound = true;
    }
    CHECK(unwound);
    output.clear(); error.clear();
    {
        TestOutputCapture capture;
        std::fputs("restored", stdout);
        capture.finish(append);
    }
    CHECK(output == "restored");
    CHECK(error.empty());
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}

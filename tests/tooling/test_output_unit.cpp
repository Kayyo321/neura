#include "tooling/test_protocol.hpp"

#include <cassert>
#include <cstdio>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <string>

int main() {
    using namespace neura::tooling;
    const std::string binary("A\0\xff", 3);
    const auto encoded = test_output_json("test", "stdout", binary);
    assert(encoded["encoding"].string() == "base64");
    assert(encoded["data"].string() == "QQD/");
    assert(encoded["output"].string() == std::string("A\0\xef\xbf\xbd", 5));
    assert(!test_output_json("test", "stdout", "valid 💙").contains("encoding"));
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
    assert(output == long_text);
    assert(error == binary);
#ifndef _WIN32
    std::mutex mutex;
    std::condition_variable ready;
    std::string live_output;
    {
        TestOutputCapture capture([&](std::string_view stream, std::string_view text) {
            assert(stream == "stdout");
            { std::lock_guard lock(mutex); live_output.append(text); }
            ready.notify_one();
        });
        std::fputs("live before completion", stdout);
        std::fflush(stdout);
        {
            std::unique_lock lock(mutex);
            assert(ready.wait_for(lock, std::chrono::seconds(2), [&] { return !live_output.empty(); }));
        }
        std::fputs(" and final", stdout);
        capture.finish(append);
    }
    assert(live_output == "live before completion and final");
#endif
    try {
        TestOutputCapture capture;
        std::fputs("discarded before failure", stdout);
        throw std::runtime_error("test");
    } catch (const std::runtime_error&) {}
    output.clear(); error.clear();
    {
        TestOutputCapture capture;
        std::fputs("restored", stdout);
        capture.finish(append);
    }
    assert(output == "restored");
    assert(error.empty());
}

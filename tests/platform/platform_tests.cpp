// Unit tests of the platform layer (src/platform).
//
// Usage: neura_platform_tests <suite> [test-name]
//        neura_platform_tests --child <mode> [arguments...]
//
// The suites are text, process, file, console, socket, dynlib, environment
// and dirs. Tests that need a second process run this executable again with
// --child and a mode.

#include "platform/console.hpp"
#include "platform/dirs.hpp"
#include "platform/dynlib.hpp"
#include "platform/environment.hpp"
#include "platform/error.hpp"
#include "platform/file.hpp"
#include "platform/process.hpp"
#include "platform/self.hpp"
#include "platform/socket.hpp"
#include "platform/text.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "platform/windows.hpp"

#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#include <winioctl.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace platform = neura::platform;

namespace {

// ---------------------------------------------------------------------------
// Registry and checks.

struct Test {
    std::string suite;
    std::string name;
    std::function<void()> run;
};

std::vector<Test>& registry() {
    static std::vector<Test> tests;
    return tests;
}

struct Register {
    Register(const char* suite, const char* name, void (*run)()) { registry().push_back({suite, name, run}); }
};

#define PLATFORM_TEST(suite, name)                                                                                     \
    void suite##_##name();                                                                                             \
    const Register register_##suite##_##name(#suite, #name, suite##_##name);                                           \
    void suite##_##name()

class Failure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const std::string& what) {
    if (!condition) throw Failure(what);
}

template <typename A, typename B>
void check_equal(const A& actual, const B& expected, const std::string& what) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << what << ": got '" << actual << "', expected '" << expected << "'";
        throw Failure(message.str());
    }
}

void check_ok(const std::error_code& error, const std::string& what) {
    if (error) throw Failure(what + ": " + platform::error_message(error));
}

// Reported in the output when a case inside a test cannot run on this
// machine (W34): the test still passes on what it could check.
void not_run(const std::string& what) {
    std::cout << "  not run: " << what << '\n';
}

std::string hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const unsigned char byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 0x0F];
    }
    return text;
}

// A fresh directory under the temporary directory, removed at the end.
class Scratch {
public:
    Scratch() {
        std::error_code error;
        const auto temporary = platform::temp_directory(error);
        check_ok(error, "temp_directory");
        path_ = temporary / platform::unique_name("neura-platform-test-");
        fs::create_directories(path_);
    }
    ~Scratch() {
        std::error_code error;
        platform::remove_tree(path_, error);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void write_file(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// ---------------------------------------------------------------------------
// Child processes.

platform::Spawn child(const std::string& mode, std::vector<std::string> arguments = {}) {
    platform::Spawn spawn;
    spawn.program = platform::executable_path();
    spawn.arguments = {platform::utf8_path(spawn.program), "--child", mode};
    spawn.arguments.insert(spawn.arguments.end(), arguments.begin(), arguments.end());
    spawn.input = platform::Stdio::null();
    return spawn;
}

platform::Capture capture(const platform::Spawn& spawn, int timeout_ms = 30000) {
    auto result = platform::run_capture(spawn, std::chrono::milliseconds(timeout_ms), 1u << 24);
    check(result.started, "child started: " + platform::error_message(result.error));
    return result;
}

// Reads one line from a pipe (the child writes it whole).
std::string read_line(platform::native_file pipe) {
    std::string line;
    char c = 0;
    std::error_code error;
    while (platform::read_some(pipe, &c, 1, error) == 1) {
        if (c == '\n') break;
        line += c;
    }
    return line;
}

bool process_gone(platform::process_id pid, int timeout_ms) {
#ifdef _WIN32
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) return true;
    const bool gone = ::WaitForSingleObject(process, static_cast<DWORD>(timeout_ms)) == WAIT_OBJECT_0;
    ::CloseHandle(process);
    return gone;
#else
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::kill(pid, 0) != 0 && errno == ESRCH) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
#endif
}

std::atomic<bool> break_received{false};

#ifdef _WIN32
bool on_break(platform::ConsoleEvent event) {
    if (event != platform::ConsoleEvent::ctrl_break) return false;
    break_received = true;
    return true;
}
#else
void on_quit(int) {
    break_received = true;
}
#endif

std::vector<std::string> child_arguments(int argc, char** argv) {
    std::vector<std::string> arguments;
#ifdef _WIN32
    // The exact UTF-16 command line, so the test does not depend on the
    // code page the C runtime converts argv with.
    (void)argv;
    int count = 0;
    wchar_t** wide = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    for (int i = 0; i < count; ++i) arguments.push_back(platform::to_utf8(wide[i]));
    ::LocalFree(wide);
#else
    for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
#endif
    (void)argc;
    return arguments;
}

int run_child(int argc, char** argv) {
    const auto arguments = child_arguments(argc, argv);
    const std::string mode = arguments.size() > 2 ? arguments[2] : "";
    const auto argument = [&](std::size_t index) { return arguments.size() > 3 + index ? arguments[3 + index] : ""; };
    platform::set_binary_stdio();
    if (mode == "echo-args") {
        for (std::size_t i = 3; i < arguments.size(); ++i) std::cout << hex(arguments[i]) << '\n';
        return 0;
    }
    if (mode == "exit") return std::atoi(argument(0).c_str());
    if (mode == "crash") {
#ifdef _WIN32
        ::RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
        std::signal(SIGSEGV, SIG_DFL);
        std::raise(SIGSEGV);
#endif
        return 1;
    }
    if (mode == "sleep") {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(argument(0).c_str())));
        return 0;
    }
    if (mode == "spawn-grandchild") {
        auto spawn = child("sleep", {"60000"});
        auto grandchild = platform::Process::start(spawn);
        std::cout << grandchild.pid() << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 0;
    }
    if (mode == "wait-break") {
#ifdef _WIN32
        platform::on_console_event(on_break);
#else
        std::signal(SIGQUIT, on_quit);
#endif
        std::cout << "ready" << std::endl;
        for (int i = 0; i < 1000 && !break_received; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return break_received ? 42 : 1;
    }
    if (mode == "try-lock") {
        std::error_code error;
        const auto lock_mode = argument(1) == "shared" ? platform::LockMode::shared : platform::LockMode::exclusive;
        const auto lock = platform::FileLock::acquire(platform::path_from_utf8(argument(0)), lock_mode,
                                                      platform::LockWait::try_once, error);
        if (lock) return 0;
        return error == std::errc::resource_unavailable_try_again ? 3 : 1;
    }
    if (mode == "print-env") {
        const auto value = platform::get_env(argument(0));
        std::cout << (value ? *value : "<unset>");
        return 0;
    }
    if (mode == "print-cwd") {
        std::cout << platform::utf8_path(fs::current_path());
        return 0;
    }
    if (mode == "write-handle") {
        const auto value = std::stoull(argument(0));
#ifdef _WIN32
        const auto handle = reinterpret_cast<platform::native_file>(static_cast<std::uintptr_t>(value));
#else
        const auto handle = static_cast<platform::native_file>(value);
#endif
        std::error_code error;
        return platform::write_all(handle, "hi", 2, error) ? 0 : 1;
    }
    if (mode == "flood") {
        const std::string block(4096, 'x');
        for (long left = std::atol(argument(0).c_str()); left > 0; left -= 4096) {
            std::cout.write(block.data(), std::min<long>(left, 4096));
        }
        std::cout.flush();
        std::cerr << "done";
        return 0;
    }
    if (mode == "print-utf8") {
        platform::install_console_streams();
        std::cout << "h\xC3\xA9llo\n" << std::flush;
        std::cerr << "\xF0\x9D\x84\x9E\n" << std::flush;
        return 0;
    }
    std::cerr << "unknown child mode '" << mode << "'\n";
    return 2;
}

#ifdef _WIN32
// A junction needs no privilege, unlike a symbolic link, so file tests use
// one wherever a link is needed (W34).
void make_junction(const fs::path& link, const fs::path& target) {
    fs::create_directory(link);
    HANDLE handle = ::CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    check(handle != INVALID_HANDLE_VALUE, "open junction directory");
    const std::wstring print = fs::absolute(target).native();
    const std::wstring substitute = L"\\??\\" + print;
    const std::size_t names = (substitute.size() + 1 + print.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(8 + 8 + names);
    const auto put16 = [&](std::size_t offset, std::size_t value) {
        const auto word = static_cast<WORD>(value);
        std::memcpy(buffer.data() + offset, &word, sizeof word);
    };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(buffer.data(), &tag, sizeof tag);
    put16(4, 8 + names);                                       // ReparseDataLength
    put16(8, 0);                                               // SubstituteNameOffset
    put16(10, substitute.size() * sizeof(wchar_t));            // SubstituteNameLength
    put16(12, (substitute.size() + 1) * sizeof(wchar_t));      // PrintNameOffset
    put16(14, print.size() * sizeof(wchar_t));                 // PrintNameLength
    std::memcpy(buffer.data() + 16, substitute.c_str(), (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(buffer.data() + 16 + (substitute.size() + 1) * sizeof(wchar_t), print.c_str(),
                (print.size() + 1) * sizeof(wchar_t));
    DWORD bytes = 0;
    const BOOL done = ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                                        static_cast<DWORD>(buffer.size()), nullptr, 0, &bytes, nullptr);
    ::CloseHandle(handle);
    check(done != 0, "FSCTL_SET_REPARSE_POINT: " + platform::error_message(platform::last_error()));
}
#endif

// ---------------------------------------------------------------------------
// text

PLATFORM_TEST(text, utf8_validation) {
    check(platform::is_valid_utf8("plain"), "ASCII");
    check(platform::is_valid_utf8("h\xC3\xA9llo \xF0\x9D\x84\x9E"), "two- and four-byte sequences");
    check(!platform::is_valid_utf8("\xC0\x80"), "overlong NUL");
    check(!platform::is_valid_utf8("\xFF"), "invalid byte");
    check(!platform::is_valid_utf8("\xED\xA0\x80"), "encoded surrogate");
    check(!platform::is_valid_utf8("\xF4\x90\x80\x80"), "above U+10FFFF");
    check(!platform::is_valid_utf8("\xE2\x82"), "truncated sequence");
}

PLATFORM_TEST(text, paths) {
    const std::string name = "dir/h\xC3\xA9llo/\xF0\x9D\x84\x9E.txt";
    const auto path = platform::path_from_utf8(name);
    check_equal(platform::utf8_path(path), name, "generic round trip");
#ifdef _WIN32
    check_equal(platform::native_utf8_path(path), std::string("dir\\h\xC3\xA9llo\\\xF0\x9D\x84\x9E.txt"),
                "native form");
    check_equal(platform::utf8_path(fs::path(L"C:\\a\\b")), std::string("C:/a/b"), "drive path in generic form");
    check(path.native() == L"dir/h\u00E9llo/\U0001D11E.txt", "UTF-16 form");
#else
    check_equal(platform::native_utf8_path(path), name, "native form");
#endif
}

PLATFORM_TEST(text, strict_conversions) {
#ifdef _WIN32
    check(platform::to_utf16("h\xC3\xA9") == L"h\u00E9", "to_utf16");
    check(platform::to_utf8(L"\U0001D11E") == "\xF0\x9D\x84\x9E", "surrogate pair to UTF-8");
    check(!platform::try_to_utf16("\xC3"), "truncated UTF-8 refused");
    check(!platform::try_to_utf16("\xED\xA0\x80"), "encoded surrogate refused");
    const wchar_t lone[] = {L'a', 0xD800, L'b', 0};
    check(!platform::try_to_utf8(lone), "unpaired surrogate refused");
    bool threw = false;
    try {
        platform::utf8_path(fs::path(lone));
    } catch (const std::system_error& error) {
        threw = error.code() == platform::invalid_text_error();
    }
    check(threw, "utf8_path of an unpaired surrogate throws invalid_text_error");
    threw = false;
    try {
        platform::path_from_utf8("\xFF");
    } catch (const std::system_error& error) {
        threw = error.code() == platform::invalid_text_error();
    }
    check(threw, "path_from_utf8 of invalid UTF-8 throws");
    check(platform::to_utf16("").empty() && platform::to_utf8(L"").empty(), "empty text");
#else
    not_run("UTF-16 conversions exist on Windows only");
#endif
}

PLATFORM_TEST(text, error_messages) {
#ifdef _WIN32
    const auto message = platform::error_message(ERROR_FILE_NOT_FOUND);
#else
    const auto message = platform::error_message(ENOENT);
#endif
    check(!message.empty() && message.back() != '\n' && message.back() != '.', "message is trimmed: " + message);
    check(platform::is_valid_utf8(message), "message is UTF-8");
    check_equal(platform::error_message(std::make_error_code(std::errc::invalid_argument)),
                std::make_error_code(std::errc::invalid_argument).message(), "generic category message");
}

// ---------------------------------------------------------------------------
// process

const std::vector<std::vector<std::string>> quoting_cases = {
    {"plain", "two words", "", "tab\there", "quote\"inside", "\"", "\\\"", "back\\slash", "trailing\\",
     "trailing space\\", "a\\\\\"b", "\\\\server\\share\\", "h\xC3\xA9llo w\xC3\xB6rld", "new\nline"},
    {"\\\\", "\\\\ \\\\", "\"\"", " ", "x\"\"y", "end\\\\"},
};

PLATFORM_TEST(process, quote_argument) {
    check_equal(platform::quote_argument("plain"), std::string("plain"), "plain");
    check_equal(platform::quote_argument(""), std::string("\"\""), "empty");
    check_equal(platform::quote_argument("a b"), std::string("\"a b\""), "space");
    check_equal(platform::quote_argument("a\\b"), std::string("a\\b"), "inner backslash");
    check_equal(platform::quote_argument("a b\\"), std::string("\"a b\\\\\""), "backslash before closing quote");
    check_equal(platform::quote_argument("a\"b"), std::string("\"a\\\"b\""), "quote");
    check_equal(platform::quote_argument("a\\\"b"), std::string("\"a\\\\\\\"b\""), "backslash before quote");
    check_equal(platform::command_line({"C:\\Program Files\\x.exe", "a b", "c"}),
                std::string("\"C:\\Program Files\\x.exe\" \"a b\" c"), "command line");
}

PLATFORM_TEST(process, command_line_round_trip) {
#ifdef _WIN32
    for (const auto& arguments : quoting_cases) {
        std::vector<std::string> argv = {"C:\\Program Files\\Neura\\neura.exe"};
        argv.insert(argv.end(), arguments.begin(), arguments.end());
        const auto line = platform::to_utf16(platform::command_line(argv));
        int count = 0;
        wchar_t** parsed = ::CommandLineToArgvW(line.c_str(), &count);
        check(parsed != nullptr, "CommandLineToArgvW");
        std::vector<std::string> back;
        for (int i = 0; i < count; ++i) back.push_back(platform::to_utf8(parsed[i]));
        ::LocalFree(parsed);
        check_equal(back.size(), argv.size(), "argument count");
        for (std::size_t i = 0; i < argv.size(); ++i) check_equal(hex(back[i]), hex(argv[i]), "argument " + argv[i]);
    }
#else
    not_run("CommandLineToArgvW exists on Windows only");
#endif
}

PLATFORM_TEST(process, arguments_reach_child) {
    for (const auto& arguments : quoting_cases) {
        const auto result = capture(child("echo-args", arguments));
        check(result.status.success(), "child status " + result.status.describe());
        std::string expected;
        for (const auto& argument : arguments) expected += hex(argument) + "\n";
        check_equal(result.out, expected, "arguments as the child read them");
    }
}

PLATFORM_TEST(process, exit_statuses) {
    const auto exited = capture(child("exit", {"7"}));
    check(exited.status == platform::ExitStatus::exited(7), "exit 7: " + exited.status.describe());
    check_equal(exited.status.describe(), std::string("exited 7"), "describe");
    check_equal(exited.status.shell_status(), 7, "shell status");

    const auto crashed = capture(child("crash"));
#ifdef _WIN32
    check(crashed.status == platform::ExitStatus::crashed(0xC0000005u), "crash: " + crashed.status.describe());
    check_equal(crashed.status.describe(), std::string("crashed 0xC0000005"), "describe crash");
    check_equal(crashed.status.shell_status(), static_cast<int>(0xC0000005u), "crash shell status");
#else
    check(crashed.status == platform::ExitStatus::signaled(SIGSEGV), "crash: " + crashed.status.describe());
    check_equal(crashed.status.describe(), "signal " + std::to_string(SIGSEGV), "describe crash");
#endif
    check_equal(platform::ExitStatus::signaled(2).shell_status(), 130, "signal shell status");
}

PLATFORM_TEST(process, terminate_and_wait) {
    auto process = platform::Process::start(child("sleep", {"60000"}));
    check(!process.wait(std::chrono::milliseconds(50)).has_value(), "still running after 50 ms");
    check(process.terminate(15), "terminate");
    const auto status = process.wait();
    check(status && *status == platform::ExitStatus::signaled(15), "terminated: " + status->describe());
    check_equal(status->shell_status(), 143, "shell status of signal 15");
    check(process.wait(std::chrono::milliseconds(0)) == status, "status is kept");

    auto other = platform::Process::start(child("sleep", {"60000"}));
    check(other.kill_tree(), "kill_tree");
    check(*other.wait() == platform::ExitStatus::signaled(9), "killed tree is signal 9");
}

PLATFORM_TEST(process, job_kill_on_close) {
    auto spawn = child("spawn-grandchild");
    spawn.job = platform::JobMode::kill_on_close;
    spawn.output = platform::Stdio::pipe();
    platform::process_id child_pid = 0;
    platform::process_id grandchild_pid = 0;
    {
        auto process = platform::Process::start(spawn);
        child_pid = process.pid();
        const auto line = read_line(process.stdout_pipe().get());
        check(!line.empty(), "grandchild pid");
        grandchild_pid = static_cast<platform::process_id>(std::stoul(line));
        check(!process_gone(grandchild_pid, 0), "grandchild runs");
#ifdef _WIN32
        check(process.job() != nullptr, "a job was created");
#endif
    }
    check(process_gone(child_pid, 10000), "child ended with the job");
    check(process_gone(grandchild_pid, 10000), "grandchild ended with the job");
}

PLATFORM_TEST(process, break_to_group) {
#ifdef _WIN32
    if (::GetConsoleWindow() == nullptr && ::GetConsoleCP() == 0) {
        // Console events need a console that parent and child share.
        if (!::AllocConsole()) {
            not_run("no console to send CTRL_BREAK through");
            return;
        }
    }
    // This process must not end on the child's group event, which it does
    // not receive, but a stray event during the test should not end it
    // either.
    platform::ignore_console_events();
#endif
    auto spawn = child("wait-break");
    spawn.new_process_group = true;
    spawn.output = platform::Stdio::pipe();
    auto process = platform::Process::start(spawn);
    check_equal(read_line(process.stdout_pipe().get()), std::string("ready"), "child is ready");
    std::error_code error;
    check(process.interrupt(platform::ConsoleEvent::ctrl_break, error), "interrupt: " + platform::error_message(error));
    const auto status = process.wait(std::chrono::milliseconds(15000));
    check(status.has_value(), "child ended");
    check(*status == platform::ExitStatus::exited(42), "child saw the break: " + status->describe());
#ifdef _WIN32
    auto ungrouped = platform::Process::start(child("sleep", {"10"}));
    check(!ungrouped.interrupt(platform::ConsoleEvent::ctrl_break, error), "ctrl_break needs a process group");
    check(!ungrouped.interrupt(platform::ConsoleEvent::ctrl_c, error), "ctrl_c to a group is refused");
    ungrouped.wait();
#endif
}

PLATFORM_TEST(process, capture_limits) {
    const auto flood = capture(child("flood", {"300000"}));
    check(flood.status.success(), "flood status " + flood.status.describe());
    check_equal(flood.out.size(), std::size_t{300000}, "stdout bytes");
    check_equal(flood.err, std::string("done"), "stderr");

    auto spawn = child("flood", {"300000"});
    const auto capped = platform::run_capture(spawn, std::chrono::milliseconds(30000), 1000);
    check(capped.output_too_large, "cap reported");
    check(capped.out.size() <= 1000, "kept at most the cap");

    auto sleeper = child("sleep", {"60000"});
    sleeper.job = platform::JobMode::kill_on_close;
    const auto start = std::chrono::steady_clock::now();
    const auto timed = platform::run_capture(sleeper, std::chrono::milliseconds(300), 1000);
    check(timed.timed_out, "timeout reported");
    check(timed.status == platform::ExitStatus::signaled(9), "timed-out child killed: " + timed.status.describe());
    check(std::chrono::steady_clock::now() - start < std::chrono::seconds(20), "timeout is prompt");

#ifdef _WIN32
    // Without a job a grandchild survives the timeout and keeps the pipes
    // open; the readers are cancelled after the grace period.
    const auto orphaning = std::chrono::steady_clock::now();
    const auto orphaned = platform::run_capture(child("spawn-grandchild"), std::chrono::milliseconds(1000), 1000);
    check(orphaned.timed_out, "timeout with a grandchild reported");
    check(std::chrono::steady_clock::now() - orphaning < std::chrono::seconds(20), "readers cancelled");
    const auto grandchild = static_cast<DWORD>(std::strtoul(orphaned.out.c_str(), nullptr, 10));
    if (const HANDLE process = grandchild != 0 ? ::OpenProcess(PROCESS_TERMINATE, FALSE, grandchild) : nullptr) {
        ::TerminateProcess(process, 137);
        ::CloseHandle(process);
    }
#endif
}

PLATFORM_TEST(process, environment_and_directory) {
    Scratch scratch;
    auto spawn = child("print-env", {"NEURA_PLATFORM_CHILD"});
    spawn.environment = platform::environment();
    spawn.environment->emplace_back("NEURA_PLATFORM_CHILD", "v\xC3\xA4lue");
    check_equal(capture(spawn).out, std::string("v\xC3\xA4lue"), "environment passed");
    spawn.environment = std::vector<std::pair<std::string, std::string>>{};
#ifdef _WIN32
    // A child needs SystemRoot to start on Windows.
    spawn.environment->emplace_back("SystemRoot", *platform::get_env("SystemRoot"));
#endif
    check_equal(capture(spawn).out, std::string("<unset>"), "environment replaced");

    const auto directory = scratch.path() / platform::path_from_utf8("d\xC3\xADr");
    fs::create_directory(directory);
    auto cwd = child("print-cwd");
    cwd.working_directory = directory;
    // Windows reports a working directory as it was set, so a scratch
    // directory below an 8.3 %TEMP% (C:/Users/RUNNER~1/...) keeps its short
    // spelling; the canonical forms name the same directory on every host.
    check_equal(platform::utf8_path(fs::canonical(platform::path_from_utf8(capture(cwd).out))),
                platform::utf8_path(fs::canonical(directory)), "working directory");

    // A relative program is found in the working directory on every host.
    auto relative = child("print-cwd");
    relative.working_directory = relative.program.parent_path();
    relative.program = relative.program.filename();
    check_equal(capture(relative).out, platform::utf8_path(fs::canonical(relative.working_directory)),
                "relative program run from its working directory");
}

PLATFORM_TEST(process, inherited_handles) {
#ifdef _WIN32
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    check(::CreatePipe(&read, &write, nullptr, 0) != 0, "pipe");
    platform::UniqueFile read_end(read);
    platform::UniqueFile write_end(write);
    const auto value = std::to_string(reinterpret_cast<std::uintptr_t>(write));
#else
    int ends[2];
    check(::pipe(ends) == 0, "pipe");
    ::fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    platform::UniqueFile read_end(ends[0]);
    platform::UniqueFile write_end(ends[1]);
    const auto value = std::to_string(ends[1]);
#endif
    auto spawn = child("write-handle", {value});
    spawn.inherit.push_back(write_end.get());
    auto process = platform::Process::start(spawn);
    check(process.wait()->success(), "child wrote to the inherited handle");
    write_end.close();
    char buffer[8] = {};
    std::error_code error;
    const auto got = platform::read_some(read_end.get(), buffer, sizeof buffer, error);
    check_equal(std::string(buffer, got > 0 ? static_cast<std::size_t>(got) : 0), std::string("hi"), "bytes");
#ifdef _WIN32
    DWORD flags = 0;
    ::GetHandleInformation(write, &flags);
    check((flags & HANDLE_FLAG_INHERIT) == 0, "inheritance flag restored");

    // Starts on several threads that name the same handle all succeed, and
    // the flag is restored once the last has finished.
    HANDLE shared_read = nullptr;
    HANDLE shared_write = nullptr;
    check(::CreatePipe(&shared_read, &shared_write, nullptr, 0) != 0, "pipe");
    platform::UniqueFile shared_read_end(shared_read);
    platform::UniqueFile shared_write_end(shared_write);
    const auto shared_value = std::to_string(reinterpret_cast<std::uintptr_t>(shared_write));
    std::atomic<int> started{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 5; ++i) {
                auto each = child("write-handle", {shared_value});
                each.inherit.push_back(shared_write);
                std::error_code start_error;
                auto writer = platform::Process::start(each, start_error);
                if (writer && writer.wait()->success()) ++started;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    check_equal(started.load(), 20, "concurrent starts with one inherited handle");
    flags = 0;
    ::GetHandleInformation(shared_write, &flags);
    check((flags & HANDLE_FLAG_INHERIT) == 0, "inheritance flag restored after concurrent starts");
#endif
}

PLATFORM_TEST(process, start_failure) {
    platform::Spawn spawn;
    spawn.program = fs::path("no-such-program") / ("missing" + std::string(platform::executable_suffix()));
    spawn.arguments = {"missing"};
    std::error_code error;
    const auto process = platform::Process::start(spawn, error);
    check(!process.valid(), "no process");
    check(error == std::errc::no_such_file_or_directory, "missing program: " + platform::error_message(error));
}

PLATFORM_TEST(process, self) {
    const auto path = platform::executable_path();
    check(path.is_absolute() && fs::is_regular_file(path), "executable path " + platform::utf8_path(path));
    check(path.filename().string().starts_with("neura_platform_tests"), "file name");
    check_equal(path.extension().string(), std::string(platform::executable_suffix()), "suffix");
    check(platform::current_process_id() != 0, "pid");
}

// ---------------------------------------------------------------------------
// file

PLATFORM_TEST(file, lock_contention) {
    Scratch scratch;
    const auto path = scratch.path() / "lock";
    const auto text = platform::utf8_path(path);
    std::error_code error;
    {
        auto lock = platform::FileLock::acquire(path, platform::LockMode::exclusive, platform::LockWait::wait, error);
        check_ok(error, "exclusive lock");
        check_equal(capture(child("try-lock", {text, "exclusive"})).status.code(), 3, "exclusive held elsewhere");
        check_equal(capture(child("try-lock", {text, "shared"})).status.code(), 3, "shared against exclusive");
        auto second =
            platform::FileLock::acquire(path, platform::LockMode::shared, platform::LockWait::try_once, error);
        check(!second && error == std::errc::resource_unavailable_try_again, "second handle in this process");
    }
    check_equal(capture(child("try-lock", {text, "exclusive"})).status.code(), 0, "free after unlock");
    {
        auto shared = platform::FileLock::acquire(path, platform::LockMode::shared, platform::LockWait::wait, error);
        check_ok(error, "shared lock");
        check_equal(capture(child("try-lock", {text, "shared"})).status.code(), 0, "shared with shared");
        check_equal(capture(child("try-lock", {text, "exclusive"})).status.code(), 3, "exclusive against shared");
    }
    auto missing = platform::FileLock::acquire(scratch.path() / "absent", platform::LockMode::shared,
                                               platform::LockWait::try_once, error, false);
    check(!missing && error == std::errc::no_such_file_or_directory, "no create");
}

PLATFORM_TEST(file, lock_sees_removal) {
    Scratch scratch;
    const auto path = scratch.path() / "lease";
    std::error_code error;
    auto lock = platform::FileLock::acquire(path, platform::LockMode::shared, platform::LockWait::wait, error);
    check_ok(error, "lock");
    check(!lock.file_removed(), "present");
    std::error_code removed;
    fs::remove(path, removed);
    check_ok(removed, "remove a locked file");
    check(lock.file_removed(), "removal seen through the lock");
}

PLATFORM_TEST(file, replace_open_file) {
    Scratch scratch;
    const auto path = scratch.path() / "target";
    write_file(path, "old");
    std::error_code error;
    auto open = platform::open_no_follow(path, platform::OpenAccess::read, platform::OpenDisposition::open_existing,
                                         error);
    check_ok(error, "open target");
    check(platform::write_file_atomically(path, "new", error), "replace open file: " + platform::error_message(error));
    check_equal(read_file(path), std::string("new"), "new contents");
    char buffer[8] = {};
    const auto got = platform::read_some(open.get(), buffer, sizeof buffer, error);
    check_equal(std::string(buffer, got > 0 ? static_cast<std::size_t>(got) : 0), std::string("old"),
                "the open handle keeps the old file");
    std::size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(scratch.path())) {
        (void)entry;
        ++entries;
    }
    check_equal(entries, std::size_t{1}, "no temporary left behind");

    const auto from = scratch.path() / "from";
    write_file(from, "moved");
    check(platform::replace_file(from, path, error), "replace_file: " + platform::error_message(error));
    check_equal(read_file(path), std::string("moved"), "replaced");
    check(!fs::exists(from), "source gone");
}

PLATFORM_TEST(file, open_no_follow) {
    Scratch scratch;
    const auto path = scratch.path() / "file";
    std::error_code error;
    {
        auto file = platform::open_no_follow(path, platform::OpenAccess::write, platform::OpenDisposition::create_new,
                                             error);
        check_ok(error, "create_new");
        check(platform::write_all(file.get(), "abcdef", 6, error), "write");
    }
    auto again =
        platform::open_no_follow(path, platform::OpenAccess::write, platform::OpenDisposition::create_new, error);
    check(!again && error == std::errc::file_exists, "create_new on an existing file");
    {
        auto file = platform::open_no_follow(path, platform::OpenAccess::write,
                                             platform::OpenDisposition::create_or_truncate, error);
        check_ok(error, "create_or_truncate");
        check(platform::write_all(file.get(), "xy", 2, error), "write");
    }
    check_equal(read_file(path), std::string("xy"), "truncated");
    {
        auto file = platform::open_no_follow(path, platform::OpenAccess::append,
                                             platform::OpenDisposition::open_existing, error);
        check_ok(error, "append");
        std::FILE* stream = platform::open_c_stream(std::move(file), "ab", error);
        check(stream != nullptr, "C stream");
        std::fputs("z", stream);
        std::fclose(stream);
    }
    check_equal(read_file(path), std::string("xyz"), "appended through a C stream");
    {
        // As O_APPEND | O_TRUNC: emptied, and every write lands at the end.
        auto file = platform::open_no_follow(path, platform::OpenAccess::append,
                                             platform::OpenDisposition::create_or_truncate, error);
        check_ok(error, "append and truncate");
        check(platform::write_all(file.get(), "ab", 2, error), "write");
        check(platform::write_all(file.get(), "c", 1, error), "write");
    }
    check_equal(read_file(path), std::string("abc"), "truncated for appending");
    {
        const auto fresh = scratch.path() / "fresh";
        auto file = platform::open_no_follow(fresh, platform::OpenAccess::append,
                                             platform::OpenDisposition::create_or_truncate, error);
        check_ok(error, "append and truncate a new file");
        check(platform::write_all(file.get(), "n", 1, error), "write");
        file.close();
        check_equal(read_file(fresh), std::string("n"), "created for appending");
    }
    check_equal(static_cast<int>(platform::link_kind(path, error)), static_cast<int>(platform::LinkKind::none),
                "a file is no link");

#ifdef _WIN32
    const auto target = scratch.path() / "target";
    fs::create_directory(target);
    write_file(target / "inside", "kept");
    const auto junction = scratch.path() / "junction";
    make_junction(junction, target);
    check(platform::link_kind(junction, error) == platform::LinkKind::junction, "junction kind");
    auto through = platform::open_no_follow(junction, platform::OpenAccess::read,
                                            platform::OpenDisposition::open_existing, error);
    check(!through && error == std::errc::too_many_symbolic_link_levels,
          "junction refused: " + platform::error_message(error));
    through = platform::open_no_follow(junction, platform::OpenAccess::write,
                                       platform::OpenDisposition::create_or_truncate, error);
    check(!through && error == std::errc::too_many_symbolic_link_levels, "junction refused for writing");
    check(fs::exists(target / "inside"), "target untouched");
#endif
    const auto link = scratch.path() / "link";
    std::error_code link_error;
    fs::create_symlink(path, link, link_error);
    if (link_error) {
        not_run("symbolic links need a privilege here: " + platform::error_message(link_error));
    } else {
        check(platform::link_kind(link, error) == platform::LinkKind::symlink, "symlink kind");
        auto opened = platform::open_no_follow(link, platform::OpenAccess::read,
                                               platform::OpenDisposition::open_existing, error);
        check(!opened && error == std::errc::too_many_symbolic_link_levels, "symlink refused");
        opened = platform::open_no_follow(link, platform::OpenAccess::write,
                                          platform::OpenDisposition::create_or_truncate, error);
        check(!opened && error == std::errc::too_many_symbolic_link_levels, "symlink refused for writing");
        check_equal(read_file(path), std::string("abc"), "link target untouched");
    }
}

PLATFORM_TEST(file, remove_tree) {
    Scratch scratch;
    const auto root = scratch.path() / "tree";
    fs::create_directories(root / "sub" / "deeper");
    write_file(root / "sub" / "file", "x");
    write_file(root / "sub" / "deeper" / "readonly", "x");
    fs::permissions(root / "sub" / "deeper" / "readonly", fs::perms::owner_write | fs::perms::group_write |
                                                              fs::perms::others_write,
                    fs::perm_options::remove);
    const auto outside = scratch.path() / "outside";
    fs::create_directory(outside);
    write_file(outside / "keep", "keep");
#ifdef _WIN32
    make_junction(root / "junction", outside);
#else
    fs::create_directory_symlink(outside, root / "junction");
#endif
    std::error_code error;
    auto open = platform::open_no_follow(root / "sub" / "file", platform::OpenAccess::read,
                                         platform::OpenDisposition::open_existing, error);
    check_ok(error, "open a file inside the tree");
    const auto removed = platform::remove_tree(root, error);
    check_ok(error, "remove_tree");
    check_equal(removed, std::intmax_t{6}, "entries removed");
    check(!fs::exists(root), "tree gone");
    check_equal(read_file(outside / "keep"), std::string("keep"), "link target kept");
    check_equal(platform::remove_tree(root, error), std::intmax_t{0}, "missing tree");
    check_ok(error, "missing tree is no error");
}

PLATFORM_TEST(file, private_directory) {
    Scratch scratch;
    const auto path = scratch.path() / "private";
    std::error_code error;
    check(platform::make_private_directory(path, error), "make: " + platform::error_message(error));
    check(platform::is_private_directory(path, error), "is private");
    check_ok(error, "is_private_directory");
    check(!platform::make_private_directory(path, error) && error == std::errc::file_exists, "exists");
    const auto open = scratch.path() / "open";
    fs::create_directory(open);
#ifndef _WIN32
    fs::permissions(open, fs::perms::others_read, fs::perm_options::add);
#endif
    check(!platform::is_private_directory(open, error), "an ordinary directory is not private");
    write_file(path / "child", "x");
    check(platform::is_private_directory(path, error), "still private with a child");
}

PLATFORM_TEST(file, unique_names) {
    const auto a = platform::unique_name("p-");
    const auto b = platform::unique_name("p-");
    check(a.size() == 34 && a.starts_with("p-"), "shape " + a);
    check(a != b, "distinct");
    check(a.find_first_not_of("0123456789abcdef", 2) == std::string::npos, "hex digits");
}

// ---------------------------------------------------------------------------
// console

PLATFORM_TEST(console, terminals) {
    Scratch scratch;
    const auto path = scratch.path() / "file";
    write_file(path, "");
#ifdef _WIN32
    const int file = ::_wopen(path.c_str(), _O_RDONLY | _O_BINARY);
    const int null = ::_open("NUL", _O_RDONLY);
    check(file >= 0 && null >= 0, "open");
    check(!platform::is_terminal(file), "a file is no terminal");
    check(::_isatty(null) != 0, "the CRT calls NUL a tty");
    check(!platform::is_terminal(null), "NUL is no terminal");
    check(!platform::terminal_columns(null).has_value(), "no columns for NUL");
    check(!platform::enable_terminal_colors(null), "no colors for NUL");
    ::_close(file);
    ::_close(null);
#else
    const int file = ::open(path.c_str(), O_RDONLY);
    check(file >= 0, "open");
    check(!platform::is_terminal(file), "a file is no terminal");
    check(!platform::terminal_columns(file).has_value(), "no columns for a file");
    ::close(file);
#endif
    platform::set_unattended_error_mode();
}

PLATFORM_TEST(console, pipes_get_bytes) {
    const auto result = capture(child("print-utf8"));
    check(result.status.success(), "status");
    check_equal(hex(result.out), hex("h\xC3\xA9llo\n"), "stdout bytes unchanged, LF only");
    check_equal(hex(result.err), hex("\xF0\x9D\x84\x9E\n"), "stderr bytes unchanged");
}

// ---------------------------------------------------------------------------
// socket

struct Pair {
    std::array<platform::native_socket, 2> ends;
    ~Pair() {
        for (const auto end : ends) {
            if (end != platform::invalid_socket) platform::close_socket(end);
        }
    }
};

PLATFORM_TEST(socket, pair_and_poll) {
    check_ok(platform::ensure_sockets(), "ensure_sockets");
    std::error_code error;
    Pair pair{platform::socket_pair(error)};
    check_ok(error, "socket_pair");
    platform::PollItem items[2] = {{pair.ends[0], platform::poll_out, 0}, {pair.ends[1], platform::poll_in, 0}};
    check_equal(platform::poll(items, 0), 1, "only the writable end is ready");
    check((items[0].revents & platform::poll_out) != 0, "writable");
    check(::send(pair.ends[0], "ping", 4, 0) == 4, "send");
    items[1].revents = 0;
    check_equal(platform::poll(std::span(items + 1, 1), 5000), 1, "readable after send");
    check((items[1].revents & platform::poll_in) != 0, "poll_in");
    char buffer[8] = {};
    check(::recv(pair.ends[1], buffer, sizeof buffer, 0) == 4 && std::string(buffer, 4) == "ping", "recv");
    check(::send(pair.ends[1], "pong", 4, 0) == 4, "send back");
    check(::recv(pair.ends[0], buffer, sizeof buffer, 0) == 4 && std::string(buffer, 4) == "pong", "recv back");

    check(platform::set_nonblocking(pair.ends[1]), "nonblocking");
    check(::recv(pair.ends[1], buffer, sizeof buffer, 0) < 0 && platform::last_socket_error().would_block(),
          "empty nonblocking read would block");

    platform::close_socket(pair.ends[0]);
    pair.ends[0] = platform::invalid_socket;
    items[1].revents = 0;
    check_equal(platform::poll(std::span(items + 1, 1), 5000), 1, "closed peer is ready");
    check((items[1].revents & (platform::poll_in | platform::poll_hangup)) != 0, "readable or hung up");
    check(::recv(pair.ends[1], buffer, sizeof buffer, 0) == 0, "end of stream");
}

PLATFORM_TEST(socket, peer_pid) {
    std::error_code error;
    Pair pair{platform::socket_pair(error)};
    check_ok(error, "socket_pair");
    const auto pid = platform::peer_pid(pair.ends[0], error);
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    check(pid.has_value(), "peer pid: " + platform::error_message(error));
    check_equal(*pid, platform::current_process_id(), "this process is the peer");
#else
    (void)pid;
    not_run("no peer pid on this system");
#endif
}

PLATFORM_TEST(socket, not_inherited) {
#ifdef _WIN32
    std::error_code error;
    Pair pair{platform::socket_pair(error)};
    check_ok(error, "socket_pair");
    for (const auto end : pair.ends) {
        DWORD flags = 0;
        check(::GetHandleInformation(reinterpret_cast<HANDLE>(end), &flags) != 0, "handle information");
        check((flags & HANDLE_FLAG_INHERIT) == 0, "not inheritable");
    }
#else
    std::error_code error;
    Pair pair{platform::socket_pair(error)};
    check_ok(error, "socket_pair");
    for (const auto end : pair.ends) check((::fcntl(end, F_GETFD) & FD_CLOEXEC) != 0, "close on exec");
#endif
}

// ---------------------------------------------------------------------------
// dynlib

PLATFORM_TEST(dynlib, open_and_symbol) {
    std::string error;
#ifdef _WIN32
    using GetPid = DWORD(WINAPI*)();
    for (const auto search : {platform::LibrarySearch::system, platform::LibrarySearch::foreign}) {
        auto library = platform::Library::open("kernel32.dll", search, error);
        check(library.is_open(), "kernel32: " + error);
        const auto get_pid = reinterpret_cast<GetPid>(library.symbol("GetCurrentProcessId", &error));
        check(get_pid != nullptr && get_pid() == ::GetCurrentProcessId(), "symbol works");
        check(library.symbol("NoSuchFunctionAnywhere", &error) == nullptr && !error.empty(), "missing symbol");
    }
    auto bare = platform::Library::open("kernel32", platform::LibrarySearch::foreign, error);
    check(bare.is_open(), "bare name gets .dll: " + error);
    wchar_t system[MAX_PATH];
    ::GetSystemDirectoryW(system, MAX_PATH);
    const auto path = platform::utf8_path(fs::path(system) / "kernel32.dll");
    auto by_path = platform::Library::open(path, platform::LibrarySearch::foreign, error);
    check(by_path.is_open(), "path with '/': " + error);
    const auto modules = platform::loaded_modules();
    check(!modules.empty() && modules.front() == ::GetModuleHandleW(nullptr), "the executable comes first");
    check(platform::module_symbol(::GetModuleHandleW(L"kernel32.dll"), "GetCurrentProcessId") != nullptr,
          "module_symbol");
#else
#ifdef __APPLE__
    const char* name = "/usr/lib/libSystem.B.dylib";
#else
    const char* name = "libc.so.6";
#endif
    auto library = platform::Library::open(name, platform::LibrarySearch::foreign, error);
    check(library.is_open(), std::string(name) + ": " + error);
    check(library.symbol("getpid", &error) != nullptr, "getpid");
    check(library.symbol("no_such_function_anywhere", &error) == nullptr && !error.empty(), "missing symbol");
#endif
    auto missing = platform::Library::open("neura-no-such-library-anywhere", platform::LibrarySearch::foreign, error);
    check(!missing.is_open() && error.find("neura-no-such-library-anywhere") != std::string::npos,
          "missing library names itself: " + error);
}

// ---------------------------------------------------------------------------
// environment

PLATFORM_TEST(environment, get_set_unset) {
    const std::string name = "NEURA_PLATFORM_TEST_VARIABLE";
    std::error_code error;
    check(platform::set_env(name, "v\xC3\xA4lue", error), "set_env");
    check(platform::get_env(name) == std::optional<std::string>("v\xC3\xA4lue"), "get_env");
    check(std::getenv(name.c_str()) != nullptr, "the C runtime sees it");
#ifdef _WIN32
    check(platform::get_env("neura_platform_test_variable") == std::optional<std::string>("v\xC3\xA4lue"),
          "names are case-insensitive");
#endif
    bool listed = false;
    for (const auto& [key, value] : platform::environment()) {
        if (key == name) listed = value == "v\xC3\xA4lue";
        check(!key.empty() && key.front() != '=', "no per-drive entries");
    }
    check(listed, "environment() lists it");
    check(platform::set_env(name, "", error), "set empty");
    check(platform::get_env(name) == std::optional<std::string>(""), "empty value");
    check(platform::unset_env(name, error), "unset_env");
    check(!platform::get_env(name).has_value(), "unset");
    check(std::getenv(name.c_str()) == nullptr, "the C runtime sees the removal");
    check(platform::unset_env(name, error), "unsetting a missing variable succeeds");
    check(!platform::set_env("A=B", "x", error), "a name with '=' is refused");
}

PLATFORM_TEST(environment, invalid_text) {
#ifdef _WIN32
    const wchar_t lone[] = {L'x', 0xDC00, 0};
    check(::SetEnvironmentVariableW(L"NEURA_PLATFORM_BAD", lone) != 0, "set a raw value");
    std::error_code error;
    const auto value = platform::get_env("NEURA_PLATFORM_BAD", error);
    check(!value && error == platform::invalid_text_error(), "unpaired surrogate refused");
    ::SetEnvironmentVariableW(L"NEURA_PLATFORM_BAD", nullptr);
    check(!platform::get_env("\xFF", error) && error == platform::invalid_text_error(), "invalid name");
#else
    not_run("POSIX environments are bytes");
#endif
}

// ---------------------------------------------------------------------------
// dirs

PLATFORM_TEST(dirs, well_known) {
    std::error_code error;
    const auto temporary = platform::temp_directory(error);
    check_ok(error, "temp_directory");
    check(fs::is_directory(temporary), "temporary directory exists");
    const auto text = platform::utf8_path(temporary);
    check(text.back() != '/', "no trailing separator: " + text);
    const auto home = platform::home_directory(error);
    check_ok(error, "home_directory");
    check(fs::is_directory(home), "home exists");
    const auto local = platform::local_app_data(error);
#ifdef _WIN32
    check_ok(error, "local_app_data");
    check(fs::is_directory(local), "LocalAppData exists");
#else
    check(local.empty() && error == std::errc::not_supported, "LocalAppData is Windows only");
#endif
}

// ---------------------------------------------------------------------------

int run_suite(const std::string& suite, const char* only) {
    std::size_t run = 0;
    std::size_t failed = 0;
    for (const auto& test : registry()) {
        if (test.suite != suite || (only != nullptr && test.name != only)) continue;
        ++run;
        const auto start = std::chrono::steady_clock::now();
        try {
            test.run();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            std::cout << "PASS " << test.suite << '.' << test.name << " (" << elapsed.count() << " ms)" << std::endl;
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << test.suite << '.' << test.name << "\n  " << error.what() << std::endl;
        }
    }
    if (run == 0) {
        std::cerr << "no tests registered for suite '" << suite << "'\n";
        return 2;
    }
    std::cout << run - failed << " of " << run << " " << suite << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    platform::set_unattended_error_mode();
    if (argc >= 3 && std::string_view(argv[1]) == "--child") return run_child(argc, argv);
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: neura_platform_tests <text|process|file|console|socket|dynlib|environment|dirs> "
                     "[test-name]\n";
        return 2;
    }
    try {
        return run_suite(argv[1], argc == 3 ? argv[2] : nullptr);
    } catch (const std::exception& error) {
        std::cerr << "neura_platform_tests: " << error.what() << '\n';
        return 2;
    }
}

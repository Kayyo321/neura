// The standard host providers of neura run and neura test: argument checks,
// error codes, capability denial and path prefixes, partial I/O, ID limits,
// release of files and sockets when a run ends, and the trace file format
// with record and replay of library programs.

#include "backend/converter.hpp"
#include "cli/source_loader.hpp"
#include "fixtures.hpp"
#include "nvm/app_services.hpp"
#include "nvm/secret_services.hpp"
#include "nvm/providers.hpp"
#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

#ifndef _WIN32
#ifdef NEURA_NVM_TLS
#include <openssl/err.h>
#include <openssl/quic.h>
#include <openssl/ssl.h>
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace neura::testing {
namespace {

#ifndef _WIN32

namespace host_error = nvm::host_error;
using nvm::HostCompletion;
using nvm::HostValue;

HostValue text(std::string value) { return HostValue{0, std::move(value), {}}; }
HostValue bytes(std::string value) { return text(std::move(value)); }
HostValue i32(std::int32_t value) { return HostValue{static_cast<std::uint32_t>(value), {}, {}}; }
HostValue i64(std::int64_t value) { return HostValue{static_cast<std::uint64_t>(value), {}, {}}; }
HostValue boolean(bool value) { return HostValue{value ? 1u : 0u, {}, {}}; }

std::int32_t as_i32(const HostValue& value) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(value.bits)); }
std::int64_t as_i64(const HostValue& value) { return static_cast<std::int64_t>(value.bits); }

// The standard services with in-memory console streams.
struct Host {
    nvm::StringSink output;
    nvm::StringSink error;
    nvm::StringSource input;
    nvm::ConsoleWriteLine console{output};
    nvm::StandardServices services;
    nvm::HostRegistry registry;

    explicit Host(nvm::Capabilities capabilities, std::string input_text = {},
                  std::optional<std::size_t> output_capacity = std::nullopt)
        : output(output_capacity ? nvm::StringSink(*output_capacity) : nvm::StringSink()),
          input(std::move(input_text), 3), services(output, error, input, std::move(capabilities)) {
        registry.add(console);
        services.register_all(registry);
    }

    HostCompletion call(std::string_view name, std::vector<HostValue> arguments, std::uint32_t version = 1) {
        auto* service = registry.find(name, version);
        if (service == nullptr) fail("no service " + std::string(name));
        return service->invoke(nvm::HostRequest{0, 0, std::move(arguments)});
    }
};

nvm::Capabilities everything() {
    nvm::Capabilities capabilities;
    capabilities.fs = capabilities.net = capabilities.env = capabilities.clock = capabilities.entropy = true;
    return capabilities;
}

void expect_ok(const HostCompletion& completion, std::string_view what,
               std::source_location where = std::source_location::current()) {
    if (!completion.success) {
        fail(std::string(what) + ": expected success, got error " + std::to_string(completion.value.bits), where);
    }
}

void expect_error(const HostCompletion& completion, std::uint32_t code, std::string_view what,
                  std::source_location where = std::source_location::current()) {
    if (completion.success) fail(std::string(what) + ": expected error " + std::to_string(code), where);
    if (completion.value.bits != code) {
        fail(std::string(what) + ": expected error " + std::to_string(code) + ", got " +
                 std::to_string(completion.value.bits),
             where);
    }
}

// A fresh directory below the system temporary directory, removed with
// everything in it when the test ends.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view name) {
        path_ = fs::temp_directory_path() /
                ("neura-host-" + std::string(name) + "-" + std::to_string(::getpid()));
        fs::remove_all(path_);
        fs::create_directories(path_);
        path_ = fs::canonical(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }
    std::string operator/(std::string_view child) const { return (path_ / child).string(); }

private:
    fs::path path_;
};

void write_text_file(const fs::path& path, std::string_view contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// The system libraries the native interop tests open by name. macOS has no
// libc.so.6 or libm.so.6: libSystem provides both, and dyld resolves the name.
#ifdef __APPLE__
constexpr const char* c_library = "/usr/lib/libSystem.B.dylib";
constexpr const char* math_library = "/usr/lib/libSystem.B.dylib";
#else
constexpr const char* c_library = "libc.so.6";
constexpr const char* math_library = "libm.so.6";
#endif

// The number of open file descriptors of this process.
std::size_t open_descriptors() {
    std::size_t count = 0;
    // Linux lists them in procfs; macOS has no procfs, and /dev/fd lists the
    // process's descriptors. Either listing includes the descriptor of the
    // directory being read, which a difference between two counts cancels.
#ifdef __APPLE__
    constexpr const char* descriptor_directory = "/dev/fd";
#else
    constexpr const char* descriptor_directory = "/proc/self/fd";
#endif
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(descriptor_directory)) ++count;
    return count;
}

NEURA_TEST(host, console_write_counts_partial_writes) {
    Host host({}, {}, 5);
    auto written = host.call("neura.console.write", {i32(1), bytes("abcdefgh")});
    expect_ok(written, "partial write");
    check_equal(as_i32(written.value), 5, "bytes stored before the sink filled");
    expect_error(host.call("neura.console.write", {i32(1), bytes("gh")}), host_error::other, "full sink");
    check_equal(host.output.contents(), std::string("abcde"), "stored prefix");
    expect_ok(host.call("neura.console.write", {i32(1), bytes("")}), "empty write");
    check_equal(as_i32(host.call("neura.console.write", {i32(2), bytes("err")}).value), 3, "error stream");
    check_equal(host.error.contents(), std::string("err"), "error stream contents");
    expect_error(host.call("neura.console.write", {i32(0), bytes("x")}), host_error::invalid_input, "stream 0");
    expect_error(host.call("neura.console.write", {i32(3), bytes("x")}), host_error::invalid_input, "stream 3");
}

NEURA_TEST(host, console_read_serves_input_until_the_end) {
    Host host({}, "line\nmore");
    auto read = host.call("neura.console.read", {i32(100)});
    expect_ok(read, "read");
    // The test source hands out at most 3 bytes at a time.
    check_equal(read.value.bytes, std::string("lin"), "first chunk");
    check_equal(host.call("neura.console.read", {i32(2)}).value.bytes, std::string("e\n"), "bounded by max");
    check_equal(host.call("neura.console.read", {i32(100)}).value.bytes, std::string("mor"), "third chunk");
    check_equal(host.call("neura.console.read", {i32(100)}).value.bytes, std::string("e"), "last chunk");
    auto end = host.call("neura.console.read", {i32(100)});
    expect_ok(end, "end of input");
    check(end.value.bytes.empty(), "empty at the end of input");
    expect_error(host.call("neura.console.read", {i32(0)}), host_error::invalid_input, "max 0");
    expect_error(host.call("neura.console.read", {i32(-4)}), host_error::invalid_input, "negative max");
}

NEURA_TEST(host, ungranted_groups_return_capability_denied) {
    Host host({});
    const auto denied = [&](std::string_view name, std::vector<HostValue> arguments) {
        expect_error(host.call(name, std::move(arguments)), host_error::capability_denied, name);
    };
    denied("neura.env.get", {text("PATH")});
    denied("neura.clock.wall", {});
    denied("neura.clock.monotonic", {});
    denied("neura.clock.sleep", {i64(0)});
    denied("neura.entropy.fill", {i32(4)});
    denied("neura.fs.open", {text("x"), i32(0)});
    denied("neura.fs.read", {i64(1), i32(4)});
    denied("neura.fs.write", {i64(1), bytes("x")});
    denied("neura.fs.seek", {i64(1), i64(0), i32(0)});
    denied("neura.fs.close", {i64(1)});
    denied("neura.fs.stat", {text("."), boolean(true)});
    denied("neura.fs.list", {text(".")});
    denied("neura.fs.make_directory", {text("x")});
    denied("neura.fs.remove", {text("x"), boolean(false)});
    denied("neura.fs.rename", {text("x"), text("y")});
    denied("neura.net.connect", {text("127.0.0.1"), i32(9), i64(0)});
    denied("neura.net.listen", {text("127.0.0.1"), i32(0)});
    denied("neura.net.local_port", {i64(1)});
    denied("neura.net.accept", {i64(1)});
    denied("neura.net.read", {i64(1), i32(4), i64(0)});
    denied("neura.net.write", {i64(1), bytes("x")});
    denied("neura.net.close", {i64(1)});
    denied("neura.ffi.abi", {});
    denied("neura.ffi.arena.open", {});
    denied("neura.ffi.arena.close", {i64(1)});
    denied("neura.ffi.alloc", {i64(0), i64(8), i64(8)});
    denied("neura.ffi.library.open", {i64(0), text(c_library)});
    denied("neura.ffi.library.default", {});
    denied("neura.ffi.library.embedded", {});
    denied("neura.ffi.symbol", {i64(0), text("abs")});
    denied("neura.ffi.read", {i64(1), i64(1)});
    denied("neura.ffi.write", {i64(1), bytes("x")});
    denied("neura.ffi.strlen", {i64(1), i64(1)});
    denied("neura.ffi.call", {i64(1), bytes(""), bytes("")});
    denied("neura.ffi.upcall.create", {i64(0), i64(0), bytes("")});
    // A browser page serves the ui and history groups; a native run answers them with error 14.
    denied("neura.ui.root.open", {text("#app")});
    denied("neura.ui.root.close", {i64(1)});
    denied("neura.ui.template", {i64(1), text("{}")});
    denied("neura.ui.commit", {i64(1), bytes("\x01")});
    denied("neura.ui.event.next", {i64(1), i32(16)});
    denied("neura.ui.stats", {i64(1)});
    denied("neura.ui.focus", {i64(1), text("name")});
    denied("neura.history.current", {});
    denied("neura.history.push", {text("/")});
    denied("neura.history.replace", {text("/")});
    denied("neura.history.next", {});
    denied("neura.history.link", {text("/")});
    // Denial comes first, whatever the arguments.
    denied("neura.entropy.fill", {i32(-1)});
    // Console output and process exit need no capability.
    expect_ok(host.call("neura.console.write", {i32(1), bytes("x")}), "console");
    check(host.call("neura.process.exit", {i32(0)}).exit_status == 0, "process exit");
}

NEURA_TEST(host, native_services_allocate_read_write_and_call) {
    nvm::Capabilities capabilities;
    capabilities.native = true;
    Host host(capabilities);
    const auto abi = host.call("neura.ffi.abi", {});
    expect_ok(abi, "abi");
    check(abi.value.elements.size() == 2 && abi.value.elements[1].bytes.size() == 24, "abi layout bytes");
    check(abi.value.elements[0].bytes == nvm::native_target_triple(), "abi triple");

    const auto arena = host.call("neura.ffi.arena.open", {});
    expect_ok(arena, "arena");
    const auto block = host.call("neura.ffi.alloc", {arena.value, i64(16), i64(8)});
    expect_ok(block, "alloc");
    check(as_i64(block.value) % 8 == 0, "aligned allocation");
    expect_error(host.call("neura.ffi.alloc", {arena.value, i64(8), i64(3)}), host_error::invalid_input,
                 "alignment not a power of two");
    expect_error(host.call("neura.ffi.alloc", {i64(99), i64(8), i64(8)}), host_error::invalid_input,
                 "unknown arena");
    const auto zeroed = host.call("neura.ffi.read", {block.value, i64(4)});
    check(zeroed.success && zeroed.value.bytes == std::string(4, '\0'), "allocations are zeroed");
    expect_ok(host.call("neura.ffi.write", {block.value, bytes(std::string("hi\0", 3))}), "write");
    const auto length = host.call("neura.ffi.strlen", {block.value, i64(16)});
    check(length.success && as_i64(length.value) == 2, "strlen stops at the NUL");

    const auto lookup = host.call("neura.ffi.library.default", {});
    expect_ok(lookup, "default lookup");
    const auto abs = host.call("neura.ffi.symbol", {lookup.value, text("abs")});
    expect_ok(abs, "abs symbol");
    expect_error(host.call("neura.ffi.symbol", {lookup.value, text("no_such_symbol_neura")}), host_error::not_found,
                 "missing symbol");
    expect_error(host.call("neura.ffi.library.open", {i64(0), text("/no/such/library.so")}), host_error::not_found,
                 "missing library");
    expect_error(host.call("neura.ffi.library.embedded", {}), host_error::not_found, "no embedded library");

    // int abs(int): version 1, not variadic (0xffff), one argument, int32 result, int32 argument.
    const std::string signature{'\x01', '\xff', '\xff', '\x01', '\x00', '\x06', '\x06'};
    const std::int32_t argument = -42;
    std::string packed(4, '\0');
    std::memcpy(packed.data(), &argument, 4);
    const auto called = host.call("neura.ffi.call", {abs.value, bytes(signature), bytes(packed)});
    expect_ok(called, "abs call");
    std::int32_t answer = 0;
    std::memcpy(&answer, called.value.bytes.data(), 4);
    check(answer == 42, "abs(-42) is 42");
    expect_error(host.call("neura.ffi.call", {abs.value, bytes(signature), bytes("x")}), host_error::invalid_input,
                 "short arguments");
    expect_error(host.call("neura.ffi.call", {abs.value, bytes(std::string{'\x02'}), bytes("")}),
                 host_error::invalid_input, "unknown signature version");
    // A float in a variadic position is rejected: fixed count 1, two arguments.
    const std::string variadic{'\x01', '\x01', '\x00', '\x02', '\x00', '\x06', '\x0c', '\x0a'};
    expect_error(host.call("neura.ffi.call", {abs.value, bytes(variadic), bytes(std::string(12, '\0'))}),
                 host_error::invalid_input, "unpromoted variadic float");

    const auto math = host.call("neura.ffi.library.open", {arena.value, text(math_library)});
    expect_ok(math, "libm through the arena");
    expect_ok(host.call("neura.ffi.symbol", {math.value, text("cos")}), "cos in libm");

    expect_ok(host.call("neura.ffi.arena.close", {arena.value}), "arena close");
    expect_error(host.call("neura.ffi.arena.close", {arena.value}), host_error::invalid_input, "second close");
    expect_error(host.call("neura.ffi.symbol", {math.value, text("cos")}), host_error::invalid_input,
                 "a library unloaded with its arena");
    host.services.end_run();
}

NEURA_TEST(host, process_exit_is_terminal) {
    Host host({});
    const auto exit = host.call("neura.process.exit", {i32(255)});
    check(exit.success && exit.exit_status == 255, "terminal completion with the status");
    expect_error(host.call("neura.process.exit", {i32(256)}), host_error::invalid_input, "status 256");
    expect_error(host.call("neura.process.exit", {i32(-1)}), host_error::invalid_input, "status -1");
}

NEURA_TEST(host, clock_entropy_and_environment_services) {
    Host host(everything());
    const auto before = std::chrono::system_clock::now();
    const auto wall = host.call("neura.clock.wall", {});
    expect_ok(wall, "wall");
    check_equal(wall.value.elements.size(), std::size_t{2}, "seconds and nanoseconds");
    const auto seconds = as_i64(wall.value.elements[0]);
    const auto nanos = as_i32(wall.value.elements[1]);
    const auto expected = std::chrono::duration_cast<std::chrono::seconds>(before.time_since_epoch()).count();
    check(seconds >= expected && seconds <= expected + 5, "wall seconds follow the system clock");
    check(nanos >= 0 && nanos < 1'000'000'000, "nanoseconds in range");
    const auto first = as_i64(host.call("neura.clock.monotonic", {}).value);
    expect_ok(host.call("neura.clock.sleep", {i64(2'000'000)}), "sleep");
    const auto second = as_i64(host.call("neura.clock.monotonic", {}).value);
    check(second - first >= 2'000'000, "monotonic time covers the sleep");
    expect_ok(host.call("neura.clock.sleep", {i64(0)}), "sleep 0");
    expect_error(host.call("neura.clock.sleep", {i64(-1)}), host_error::invalid_input, "negative sleep");

    const auto random = host.call("neura.entropy.fill", {i32(32)});
    expect_ok(random, "entropy");
    check_equal(random.value.bytes.size(), std::size_t{32}, "requested count");
    check(random.value.bytes != host.call("neura.entropy.fill", {i32(32)}).value.bytes, "fresh bytes");
    check(host.call("neura.entropy.fill", {i32(0)}).value.bytes.empty(), "zero bytes");
    expect_error(host.call("neura.entropy.fill", {i32(-1)}), host_error::invalid_input, "negative count");
    expect_error(host.call("neura.entropy.fill", {i32(nvm::max_entropy_bytes + 1)}), host_error::invalid_input,
                 "too many bytes");

    ::setenv("NEURA_HOST_TEST_VALUE", "grüße", 1);
    const auto found = host.call("neura.env.get", {text("NEURA_HOST_TEST_VALUE")});
    expect_ok(found, "env found");
    check(found.value.elements.at(0).bits == 1 && found.value.elements.at(1).bytes == "grüße", "value");
    ::unsetenv("NEURA_HOST_TEST_VALUE");
    const auto missing = host.call("neura.env.get", {text("NEURA_HOST_TEST_VALUE")});
    check(missing.success && missing.value.elements.at(0).bits == 0, "missing");
    ::setenv("NEURA_HOST_TEST_VALUE", "\xff", 1);
    expect_error(host.call("neura.env.get", {text("NEURA_HOST_TEST_VALUE")}), host_error::unsupported,
                 "a value that is not UTF-8");
    ::unsetenv("NEURA_HOST_TEST_VALUE");
    expect_error(host.call("neura.env.get", {text("")}), host_error::invalid_input, "empty name");
    expect_error(host.call("neura.env.get", {text("A=B")}), host_error::invalid_input, "name with =");
    expect_error(host.call("neura.env.get", {text(std::string("A\0B", 3))}), host_error::invalid_input, "NUL");
}

NEURA_TEST(host, file_services_report_error_codes) {
    TemporaryDirectory dir("files");
    Host host(everything());
    const auto open = [&](const std::string& path, std::int32_t mode) {
        return host.call("neura.fs.open", {text(path), i32(mode)});
    };
    expect_error(open(dir / "missing", 0), host_error::not_found, "read a missing file");
    auto created = open(dir / "a.txt", 4);
    expect_ok(created, "create new");
    const auto id = as_i64(created.value);
    check_equal(id, std::int64_t{1}, "IDs start at 1");
    expect_error(open(dir / "a.txt", 4), host_error::already_exists, "create existing");
    expect_error(open(dir.path().string(), 0), host_error::is_a_directory, "open a directory");
    expect_error(open(dir / "a.txt/x", 1), host_error::not_a_directory, "file as directory");
    expect_error(open(dir / "a.txt", 5), host_error::invalid_input, "mode 5");
    expect_error(open("", 0), host_error::invalid_input, "empty path");
    expect_error(open(std::string("a\0b", 3), 0), host_error::invalid_input, "NUL in path");

    check_equal(as_i32(host.call("neura.fs.write", {i64(id), bytes("hello")}).value), 5, "write count");
    check_equal(as_i64(host.call("neura.fs.seek", {i64(id), i64(1), i32(0)}).value), std::int64_t{1}, "seek");
    check_equal(host.call("neura.fs.read", {i64(id), i32(3)}).value.bytes, std::string("ell"), "read");
    expect_error(host.call("neura.fs.read", {i64(id), i32(0)}), host_error::invalid_input, "read max 0");
    expect_error(host.call("neura.fs.seek", {i64(id), i64(0), i32(3)}), host_error::invalid_input, "origin 3");
    expect_error(host.call("neura.fs.seek", {i64(id), i64(-9), i32(0)}), host_error::invalid_input, "before start");
    expect_error(host.call("neura.fs.read", {i64(99), i32(3)}), host_error::invalid_input, "unknown ID");
    expect_ok(host.call("neura.fs.close", {i64(id)}), "close");
    expect_error(host.call("neura.fs.close", {i64(id)}), host_error::invalid_input, "closed ID");
    expect_error(host.call("neura.fs.write", {i64(id), bytes("x")}), host_error::invalid_input, "write closed");

    const auto reading = as_i64(open(dir / "a.txt", 0).value);
    check_equal(reading, std::int64_t{2}, "IDs are not reused");
    expect_error(host.call("neura.fs.write", {i64(reading), bytes("x")}), host_error::permission_denied,
                 "write a file opened for reading");
    const auto appending = as_i64(open(dir / "a.txt", 2).value);
    expect_error(host.call("neura.fs.read", {i64(appending), i32(1)}), host_error::permission_denied,
                 "read a file opened for appending");

    const auto stat = host.call("neura.fs.stat", {text(dir / "a.txt"), boolean(true)});
    expect_ok(stat, "stat");
    check(as_i32(stat.value.elements.at(0)) == 0 && as_i64(stat.value.elements.at(1)) == 5, "kind and size");
    check(as_i64(stat.value.elements.at(2)) > 1'600'000'000, "modified seconds");
    check_equal(as_i32(host.call("neura.fs.stat", {text(dir.path().string()), boolean(true)}).value.elements.at(0)), 1,
                "directory kind");
    expect_error(host.call("neura.fs.stat", {text(dir / "none"), boolean(true)}), host_error::not_found, "stat");

    expect_ok(host.call("neura.fs.make_directory", {text(dir / "sub")}), "make directory");
    expect_error(host.call("neura.fs.make_directory", {text(dir / "sub")}), host_error::already_exists, "exists");
    expect_error(host.call("neura.fs.make_directory", {text(dir / "x/y")}), host_error::not_found, "parent");
    write_text_file(dir.path() / "sub" / "z", "");
    write_text_file(dir.path() / "sub" / "B", "");
    write_text_file(dir.path() / "sub" / "\xc3\xa9", "");
    write_text_file(dir.path() / "sub" / "a", "");
    const auto listed = host.call("neura.fs.list", {text(dir / "sub")});
    expect_ok(listed, "list");
    std::vector<std::string> names;
    for (const auto& name : listed.value.elements) names.push_back(name.bytes);
    check(names == std::vector<std::string>{"B", "a", "z", "\xc3\xa9"}, "names sorted by bytes");
    expect_error(host.call("neura.fs.list", {text(dir / "a.txt")}), host_error::not_a_directory, "list a file");
    // Some file systems refuse to create a name that is not UTF-8: APFS
    // fails with EILSEQ. Such a directory can never list one, so the check
    // applies only where the name can exist.
    write_text_file(dir.path() / "sub" / "\xff", "");
    std::error_code not_created;
    if (fs::symlink_status(dir.path() / "sub" / "\xff", not_created).type() != fs::file_type::not_found) {
        expect_error(host.call("neura.fs.list", {text(dir / "sub")}), host_error::unsupported, "a name not UTF-8");
        fs::remove(dir.path() / "sub" / "\xff");
    } else {
        std::cout << "  this file system rejects names that are not UTF-8: that listing error was not checked\n";
    }

    expect_error(host.call("neura.fs.remove", {text(dir / "sub"), boolean(true)}), host_error::directory_not_empty,
                 "remove a full directory");
    expect_error(host.call("neura.fs.remove", {text(dir / "sub"), boolean(false)}), host_error::is_a_directory,
                 "remove a directory as a file");
    expect_error(host.call("neura.fs.remove", {text(dir / "a.txt"), boolean(true)}), host_error::not_a_directory,
                 "remove a file as a directory");
    expect_error(host.call("neura.fs.remove", {text(dir / "none"), boolean(false)}), host_error::not_found, "none");
    expect_ok(host.call("neura.fs.rename", {text(dir / "a.txt"), text(dir / "b.txt")}), "rename");
    expect_error(host.call("neura.fs.rename", {text(dir / "a.txt"), text(dir / "c.txt")}), host_error::not_found,
                 "rename a missing file");
    expect_error(host.call("neura.fs.rename", {text(dir / "b.txt"), text(dir / "sub")}), host_error::is_a_directory,
                 "rename a file over a directory");
    expect_ok(host.call("neura.fs.make_directory", {text(dir / "empty")}), "make directory");
    expect_error(host.call("neura.fs.rename", {text(dir / "empty"), text(dir / "sub")}),
                 host_error::directory_not_empty, "rename over a full directory");
    if (::geteuid() != 0) {
        write_text_file(dir.path() / "locked", "x");
        ::chmod((dir / "locked").c_str(), 0);
        expect_error(open(dir / "locked", 0), host_error::permission_denied, "unreadable file");
    }
}

NEURA_TEST(host, resolve_path_normalizes_and_follows_links) {
    TemporaryDirectory dir("resolve");
    fs::create_directories(dir.path() / "real" / "inner");
    fs::create_directory_symlink(dir.path() / "real", dir.path() / "link");
    fs::create_symlink("real/inner", dir.path() / "relative");
    fs::create_symlink("loop", dir.path() / "loop");
    const auto resolved = [](const std::string& path, bool follow = true) {
        const auto result = nvm::resolve_path(path, follow);
        if (result.error != 0) fail("cannot resolve " + path);
        return result.text();
    };
    const auto base = dir.path().string();
    check_equal(resolved(base + "//real/./inner/"), base + "/real/inner", "separators and dots");
    check_equal(resolved(base + "/real/../real/inner/.."), base + "/real", "parent components");
    check_equal(resolved(base + "/link/inner"), base + "/real/inner", "absolute link");
    check_equal(resolved(base + "/relative"), base + "/real/inner", "relative link");
    check_equal(resolved(base + "/relative", false), base + "/relative", "last link kept");
    check_equal(resolved(base + "/link/../x"), base + "/x", "parent of a link's target");
    check_equal(resolved(base + "/missing/../x/y"), base + "/x/y", "lexical after a missing component");
    check_equal(resolved("/.."), std::string("/"), "parent of the root");
    check_equal(resolved("."), fs::current_path().string(), "relative to the current directory");
    check_equal(nvm::resolve_path(base + "/loop/x", true).error, host_error::other, "link loop");
    check_equal(nvm::resolve_path("", true).error, host_error::invalid_input, "empty path");
}

NEURA_TEST(host, file_prefixes_reject_escapes) {
    TemporaryDirectory dir("prefix");
    const auto allowed = dir.path() / "allowed";
    const auto outside = dir.path() / "outside";
    fs::create_directories(allowed / "sub");
    fs::create_directories(outside);
    write_text_file(outside / "secret", "s");
    fs::create_directory_symlink(outside, allowed / "escape");
    fs::create_symlink(outside / "planted", allowed / "dangling");
    fs::create_directory_symlink(allowed / "sub", allowed / "inside");
    nvm::Capabilities capabilities;
    capabilities.fs = true;
    capabilities.fs_prefixes = {allowed.string()};
    Host host(capabilities);
    const auto path = [&](std::string_view child) { return text((allowed / child).string()); };
    const auto denied = [&](std::string_view name, std::vector<HostValue> arguments) {
        expect_error(host.call(name, std::move(arguments)), host_error::capability_denied, name);
    };
    expect_ok(host.call("neura.fs.open", {path("sub/f"), i32(1)}), "inside the prefix");
    expect_ok(host.call("neura.fs.list", {text(allowed.string())}), "the prefix itself");
    expect_ok(host.call("neura.fs.open", {path("inside/g"), i32(1)}), "a link that stays inside");
    denied("neura.fs.open", {path("../outside/secret"), i32(0)});
    denied("neura.fs.open", {path("sub/../../outside/secret"), i32(0)});
    denied("neura.fs.open", {path("escape/secret"), i32(0)});
    denied("neura.fs.list", {path("escape")});
    denied("neura.fs.stat", {path("escape"), boolean(true)});
    denied("neura.fs.open", {text((dir.path() / "allowed-sibling").string()), i32(1)});
    denied("neura.fs.rename", {path("sub/f"), path("escape/f")});
    // Opening a dangling link would create its target outside the prefix.
    denied("neura.fs.open", {path("dangling"), i32(1)});
    check(!fs::exists(outside / "planted"), "nothing created through the link");
    // CreateNew does not follow the last component; the link exists.
    expect_error(host.call("neura.fs.open", {path("dangling"), i32(4)}), host_error::already_exists, "create new");
    check(!fs::exists(outside / "planted"), "still nothing created");
    // Operations on the link entry itself stay inside the prefix.
    const auto link = host.call("neura.fs.stat", {path("escape"), boolean(false)});
    expect_ok(link, "stat of the link itself");
    check_equal(as_i32(link.value.elements.at(0)), 2, "a link is kind Other");
    expect_ok(host.call("neura.fs.remove", {path("escape"), boolean(false)}), "remove the link");
    check(fs::exists(outside / "secret"), "the target survives");
    // A relative prefix resolves against the current directory.
    const auto previous = fs::current_path();
    fs::current_path(dir.path());
    {
        nvm::Capabilities relative;
        relative.fs = true;
        relative.fs_prefixes = {"allowed"};
        Host inner(relative);
        expect_ok(inner.call("neura.fs.stat", {text("allowed/sub"), boolean(true)}), "relative inside");
        expect_error(inner.call("neura.fs.stat", {text("outside"), boolean(true)}), host_error::capability_denied,
                     "relative outside");
        expect_error(inner.call("neura.fs.stat", {text((dir.path() / "outside").string()), boolean(true)}),
                     host_error::capability_denied, "absolute outside");
    }
    fs::current_path(previous);
}

NEURA_TEST(host, file_ids_are_limited_and_released_when_a_run_ends) {
    TemporaryDirectory dir("limit");
    write_text_file(dir.path() / "f", "x");
    const auto baseline = open_descriptors();
    Host host(everything());
    for (std::size_t i = 0; i < nvm::max_open_ids; ++i) {
        expect_ok(host.call("neura.fs.open", {text(dir / "f"), i32(0)}), "open within the limit");
    }
    expect_error(host.call("neura.fs.open", {text(dir / "f"), i32(0)}), host_error::other, "one too many");
    check_equal(host.services.open_files(), nvm::max_open_ids, "open count");
    check_equal(open_descriptors(), baseline + nvm::max_open_ids, "descriptors held");
    expect_ok(host.call("neura.fs.close", {i64(10)}), "close one");
    expect_ok(host.call("neura.fs.open", {text(dir / "f"), i32(0)}), "room again");
    host.services.end_run();
    check_equal(host.services.open_files(), std::size_t{0}, "nothing open after the run");
    check_equal(open_descriptors(), baseline, "descriptors released");
    check_equal(as_i64(host.call("neura.fs.open", {text(dir / "f"), i32(0)}).value), std::int64_t{1},
                "IDs restart in the next run");
}

// Connects a client to a fresh listener and accepts it; returns the
// listener, client, and server IDs.
std::array<std::int64_t, 3> connected(Host& host) {
    const auto listener = host.call("neura.net.listen", {text("127.0.0.1"), i32(0)});
    expect_ok(listener, "listen");
    const auto port = host.call("neura.net.local_port", {listener.value});
    expect_ok(port, "local port");
    check(as_i32(port.value) > 0, "a chosen port");
    const auto client = host.call("neura.net.connect", {text("127.0.0.1"), port.value, i64(5'000'000'000)});
    expect_ok(client, "connect");
    const auto server = host.call("neura.net.accept", {listener.value});
    expect_ok(server, "accept");
    return {as_i64(listener.value), as_i64(client.value), as_i64(server.value)};
}

NEURA_TEST(host, socket_services_report_error_codes) {
    Host host(everything());
    const auto [listener, client, server] = connected(host);
    check_equal(as_i32(host.call("neura.net.write", {i64(client), bytes("ping")}).value), 4, "write count");
    check_equal(host.call("neura.net.read", {i64(server), i32(64), i64(0)}).value.bytes, std::string("ping"), "read");
    expect_error(host.call("neura.net.read", {i64(server), i32(8), i64(20'000'000)}), host_error::timed_out,
                 "read timeout");
    expect_error(host.call("neura.net.read", {i64(server), i32(0), i64(0)}), host_error::invalid_input, "max 0");
    expect_error(host.call("neura.net.read", {i64(server), i32(8), i64(-1)}), host_error::invalid_input,
                 "negative timeout");
    expect_error(host.call("neura.net.read", {i64(listener), i32(8), i64(0)}), host_error::invalid_input,
                 "read a listener");
    expect_error(host.call("neura.net.accept", {i64(client)}), host_error::invalid_input, "accept on a stream");
    expect_error(host.call("neura.net.write", {i64(77), bytes("x")}), host_error::invalid_input, "unknown ID");
    expect_error(host.call("neura.net.connect", {text("127.0.0.1"), i32(70000), i64(0)}), host_error::invalid_input,
                 "port out of range");
    expect_error(host.call("neura.net.connect", {text(""), i32(80), i64(0)}), host_error::invalid_input, "no host");
    expect_error(host.call("neura.net.listen", {text("127.0.0.1"), i32(-1)}), host_error::invalid_input, "port -1");

    // The peer closed: the stream ends, and writes soon fail with a reset.
    // The first write after the close is accepted locally and draws the
    // peer's reset, which a later write reports. Linux handles the reset of
    // a loopback connection within that first call; macOS handles it
    // asynchronously, and on a loaded machine many writes can be accepted
    // before it arrives. So the writes continue, a millisecond apart, until
    // one fails or a deadline passes.
    expect_ok(host.call("neura.net.close", {i64(server)}), "close the server side");
    check(host.call("neura.net.read", {i64(client), i32(8), i64(0)}).value.bytes.empty(), "end of stream");
    HostCompletion failed = HostCompletion::ok();
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (failed.success && std::chrono::steady_clock::now() < give_up) {
        failed = host.call("neura.net.write", {i64(client), bytes("after close")});
        if (failed.success) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect_error(failed, host_error::connection_reset, "write after the peer closed");

    const auto port = as_i32(host.call("neura.net.local_port", {i64(listener)}).value);
    expect_error(host.call("neura.net.listen", {text("127.0.0.1"), i32(port)}), host_error::already_exists,
                 "address in use");
    expect_ok(host.call("neura.net.close", {i64(listener)}), "close the listener");
    expect_error(host.call("neura.net.close", {i64(listener)}), host_error::invalid_input, "closed twice");
    expect_error(host.call("neura.net.connect", {text("127.0.0.1"), i32(port), i64(1'000'000'000)}),
                 host_error::connection_refused, "nothing listening");
}

NEURA_TEST(host, socket_ids_are_limited_and_released_when_a_run_ends) {
    const auto baseline = open_descriptors();
    Host host(everything());
    const auto [listener, client, server] = connected(host);
    const auto port = host.call("neura.net.local_port", {i64(listener)}).value;
    while (host.services.open_sockets() + 2 <= nvm::max_open_ids) {
        expect_ok(host.call("neura.net.connect", {text("127.0.0.1"), port, i64(0)}), "connect within the limit");
        expect_ok(host.call("neura.net.accept", {i64(listener)}), "accept within the limit");
    }
    expect_ok(host.call("neura.net.connect", {text("127.0.0.1"), port, i64(0)}), "the last ID");
    check_equal(host.services.open_sockets(), nvm::max_open_ids, "open count");
    expect_error(host.call("neura.net.accept", {i64(listener)}), host_error::other, "accept beyond the limit");
    expect_error(host.call("neura.net.connect", {text("127.0.0.1"), port, i64(0)}), host_error::other,
                 "connect beyond the limit");
    expect_error(host.call("neura.net.listen", {text("127.0.0.1"), i32(0)}), host_error::other,
                 "listen beyond the limit");
    host.services.end_run();
    check_equal(host.services.open_sockets(), std::size_t{0}, "nothing open after the run");
    check_equal(open_descriptors(), baseline, "descriptors released");
    (void)client;
    (void)server;
}

NEURA_TEST(host, trace_files_round_trip) {
    nvm::Trace trace;
    nvm::TraceEntry write;
    write.import = "neura.console.write";
    write.version = 1;
    write.arguments = {i32(1), bytes(std::string("a\"b\\c\n\r\t\x01\xff\0z", 12))};
    write.completion = HostCompletion::ok(i32(3));
    nvm::TraceEntry stat;
    stat.import = "neura.fs.stat";
    stat.version = 1;
    stat.arguments = {text("/tmp"), boolean(true)};
    HostValue nested;
    nested.elements = {i32(1), i64(-5), HostValue{}, text("x")};
    HostValue list;
    list.elements = {nested, HostValue{}};
    stat.completion = HostCompletion::ok(list);
    nvm::TraceEntry failed;
    failed.import = "neura.fs.open";
    failed.version = 7;
    failed.arguments = {};
    failed.completion = HostCompletion::failed(i32(14));
    nvm::TraceEntry exit;
    exit.import = "neura.process.exit";
    exit.version = 1;
    exit.arguments = {i32(3)};
    exit.completion = HostCompletion::terminate(3);
    trace.entries = {write, stat, failed, exit};
    trace.outcome.completed = false;
    trace.outcome.trap = nvm::TrapKind::HeapLimit;
    trace.outcome.reason = 0;
    trace.outcome.message = "line\none";

    const auto text_form = nvm::write_trace(trace);
    check_equal(nvm::write_trace(trace), text_form, "deterministic text");
    check(text_form.starts_with("neura-trace 1\nrequest neura.console.write 1 [1, \"a\\\"b\\\\c\\n\\r\\t\\x01\\xff\\x00z\"]\n"
                                "ok 3\n"),
          "documented escapes: " + text_form);
    check(text_form.find("ok [[1, 18446744073709551611, 0, \"x\"], 0]\n") != std::string::npos, "nested lists");
    check(text_form.find("request neura.fs.open 7 []\nerror 14\n") != std::string::npos, "error completion");
    check(text_form.find("request neura.process.exit 1 [3]\nexit 3\n") != std::string::npos, "terminal completion");
    check(text_form.ends_with("outcome trap heap-limit 0 \"line\\none\"\n"), "trap outcome");
    const auto read = nvm::read_trace(text_form);
    check(read.entries == trace.entries, "entries survive the round trip");
    check(read.outcome == trace.outcome, "outcome survives the round trip");
    check_equal(nvm::write_trace(read), text_form, "the same bytes again");

    nvm::Trace completed;
    completed.outcome.completed = true;
    completed.outcome.status = -2;
    check(nvm::read_trace(nvm::write_trace(completed)).outcome == completed.outcome, "negative status");
    check(nvm::read_trace("# comment\n\nneura-trace 1\r\n# more\noutcome completed 0\n").outcome.completed,
          "comments, blank lines, and CR LF");

    const auto rejects = [](std::string_view text, std::size_t line, std::string_view message) {
        try {
            nvm::read_trace(text);
        } catch (const nvm::TraceFormatError& error) {
            check_equal(error.line, line, "error line for " + std::string(message));
            check(std::string(error.what()).find(message) != std::string::npos,
                  "message '" + std::string(error.what()) + "' mentions '" + std::string(message) + "'");
            return;
        }
        fail("accepted malformed trace: " + std::string(text));
    };
    rejects("", 0, "missing the header");
    rejects("neura-trace 4\n", 1, "unsupported trace version 4");
    rejects("neura-trace 2\nrequest a.b 1 []\ncallback c.d 1 []\nok 0\noutcome completed 0\n", 4,
            "expected 'request', 'return', or 'ended'");
    rejects("neura-trace 2\nrequest a.b 1 []\ncallback c.d 1 []\n", 3, "no return");

    // Callbacks nest inside the request that made them.
    nvm::Trace with_callbacks;
    nvm::TraceEntry outer;
    outer.import = "neura.ffi.call";
    outer.version = 1;
    outer.arguments = {i64(5)};
    nvm::TraceCallback callback;
    callback.name = "neura.ffi.upcall";
    callback.version = 1;
    callback.arguments = {i64(1), bytes("ab")};
    nvm::TraceEntry inner;
    inner.import = "neura.console.write_line";
    inner.version = 1;
    inner.arguments = {text("in callback")};
    callback.entries.push_back(inner);
    callback.result = bytes("r");
    outer.callbacks.push_back(callback);
    callback.entries.clear();
    callback.result.reset();
    outer.callbacks.push_back(callback);
    outer.completion = HostCompletion::ok(bytes("done"));
    with_callbacks.entries.push_back(outer);
    const auto nested_text = nvm::write_trace(with_callbacks);
    check(nested_text.starts_with("neura-trace 2\n"), "a trace with callbacks is version 2");
    check(nvm::read_trace(nested_text).entries == with_callbacks.entries, "callbacks round-trip: " + nested_text);
    rejects("trace 1\n", 1, "expected the header");
    rejects("neura-trace 1\n", 1, "missing the outcome");
    rejects("neura-trace 1\nrequest a 1 []\n", 2, "no completion");
    rejects("neura-trace 1\nrequest a 1 []\nok\n", 3, "expected a value");
    rejects("neura-trace 1\nrequest a 1 [\"x]\n", 2, "unterminated string");
    rejects("neura-trace 1\nrequest a 1 [\"\\q\"]\n", 2, "unknown escape");
    rejects("neura-trace 1\nrequest a 1 [\"\\x4\"]\n", 2, "hexadecimal");
    rejects("neura-trace 1\nrequest a 1 [-1]\n", 2, "unsigned decimal");
    rejects("neura-trace 1\nrequest a 1 [1] extra\n", 2, "unexpected text");
    rejects("neura-trace 1\nrequest a 1 []\nexit 256\n", 3, "exceeds 255");
    rejects("neura-trace 1\noutcome trap mystery 0 \"\"\n", 2, "unknown trap kind");
    rejects("neura-trace 1\noutcome completed 0\noutcome completed 0\n", 3, "after the outcome");
    rejects("neura-trace 1\nreply 1\n", 2, "expected 'request' or 'outcome'");
}

// Builds a program with the installed standard library from the source tree.
nvr::Image build_with_library(const Environment& environment, const fs::path& file) {
    const auto canonical = fs::canonical(file);
    auto program = neura::lower(
        neura::read_sources({canonical}, {canonical.parent_path()}, environment.source_dir / "stdlib"));
    return nvr::load(nvr::encode(backend::build(std::move(program), {}).image));
}

NEURA_TEST(host, library_programs_record_and_replay) {
    TemporaryDirectory dir("record");
    const auto source = dir.path() / "files.nra";
    write_text_file(source, R"(let console = import:Std.Io.Console;
let file = import:Std.Fs.File;
let fs = import:Std.Fs.Fs;
let path = import:Std.Fs.Path;
let program = import:Std.Program;
let result = import:Std.Core.Result;
let unit = import:Std.Core.Unit;

export let Files = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let dir = path.of(args[0]);
        result.expect<unit.Unit>(fs.write_text(dir.join("data.txt"), "recorded text"), "write");
        console.print_line(result.expect<string>(fs.read_text(dir.join("data.txt")), "read"));
        console.error_line("to the error stream");
        let kept = result.expect<file.File>(file.open(dir.join("data.txt"), file.OpenMode.Read), "open");
        if (args.length > 1) {
            trap_with_message("stopped with a file open");
        }
        return program.ExitCode.Status(4);
    };
);
)");
    const auto image = build_with_library(environment, source);
    const auto work = dir.path() / "work";
    fs::create_directory(work);
    nvm::Capabilities capabilities;
    capabilities.fs = true;
    capabilities.fs_prefixes = {work.string()};
    const auto baseline = open_descriptors();
    Host host(capabilities);
    const auto bindings = nvm::bind(image, host.registry);
    const std::vector<std::string> arguments{work.string()};
    const auto recorded = nvm::record(image, bindings, arguments);
    check(recorded.outcome.completed && recorded.outcome.status == 4, "the recorded run completes");
    check_equal(host.services.open_files(), std::size_t{1}, "the run left its file open");
    host.services.end_run();
    check_equal(open_descriptors(), baseline, "released after the run");
    check_equal(host.output.contents(), std::string("recorded text\n"), "recorded output");

    // Replay from the file form, with the files gone: no service runs.
    const auto stored = nvm::read_trace(nvm::write_trace(recorded));
    fs::remove_all(work);
    nvm::StringSink output;
    nvm::StringSink error;
    const auto replayed = nvm::replay(image, stored, arguments, {}, [&](const nvm::TraceEntry& entry) {
        nvm::replay_console_output(entry.import, entry.version, entry.arguments, entry.completion, output, error);
    });
    check(replayed.matches, "replay matches: " + replayed.difference);
    check_equal(output.contents(), std::string("recorded text\n"), "replayed output");
    check_equal(error.contents(), std::string("to the error stream\n"), "replayed error stream");
    check(replayed.trace.steps == recorded.steps, "the same steps");

    // A different argument changes the first request.
    const auto diverged = nvm::replay(image, stored, std::vector<std::string>{"elsewhere"});
    check(!diverged.matches && diverged.difference.find("request 0 (neura.fs.open version 1) differs in argument 0") !=
                                   std::string::npos,
          "divergence names the request: " + diverged.difference);

    // A run that traps also releases its files.
    fs::create_directory(work);
    const std::vector<std::string> trapping{work.string(), "trap"};
    const auto result = nvm::run(image, bindings, trapping);
    check(!result.completed, "the run trapped");
    check_equal(host.services.open_files(), std::size_t{1}, "open at the trap");
    host.services.end_run();
    check_equal(open_descriptors(), baseline, "released after the trap");
}

// Every library host binding binds to a standard provider: the library's
// declarations and the providers agree on names, versions, and signatures.
NEURA_TEST(host, library_bindings_match_the_providers) {
    TemporaryDirectory dir("bindings");
    const auto source = dir.path() / "all.nra";
    std::string imports = R"(let console = import:Std.Io.Console;
let file = import:Std.Fs.File;
let fs = import:Std.Fs.Fs;
let path = import:Std.Fs.Path;
let program = import:Std.Program;
)";
    std::string uses = R"(
        console.print("x");
        console.print_line("x");
        console.read_line();
        fs.exists(path.of("x"));
        fs.list(path.of("x"));
        fs.create_directory(path.of("x"));
        fs.remove_tree(path.of("x"));
        fs.rename(path.of("x"), path.of("y"));
        fs.copy_file(path.of("x"), path.of("y"));
        file.open(path.of("x"), file.OpenMode.Read);
        program.Environment.get("x");
        program.Process.exit(0);
)";
    const bool time = fs::exists(environment.source_dir / "stdlib/std/time/duration.nra");
    if (time) {
        imports += "let duration = import:Std.Time.Duration;\nlet tcp_stream = import:Std.Net.TcpStream;\n"
                   "let tcp_listener = import:Std.Net.TcpListener;\n";
        uses += "        tcp_stream.connect(\"x\", 1, duration.of_nanos(0));\n"
                "        tcp_listener.bind(\"x\", 1);\n";
    }
    if (fs::exists(environment.source_dir / "stdlib/std/time/system-clock.nra")) {
        imports += "let system_clock = import:Std.Time.SystemClock;\n";
        uses += "        let clock = system_clock.create();\n        clock.now();\n        clock.monotonic();\n"
                "        clock.sleep(duration.of_nanos(0));\n";
    }
    if (fs::exists(environment.source_dir / "stdlib/std/dynamic-foreign/linker.nra")) {
        imports += "let arenas = import:Std.DynamicForeign.Arena;\n"
                   "let descriptors = import:Std.DynamicForeign.FunctionDescriptor;\n"
                   "let layouts = import:Std.DynamicForeign.ValueLayout;\n"
                   "let linkers = import:Std.DynamicForeign.Linker;\n"
                   "let lookups = import:Std.DynamicForeign.SymbolLookup;\n"
                   "let values_module = import:Std.DynamicForeign.ForeignValue;\n";
        uses += "        let arena = arenas.of_confined();\n"
                "        let segment = arena.allocate_utf8(\"x\");\n"
                "        segment.get_utf8(0);\n"
                "        lookups.library_lookup(\"x\", arena);\n"
                "        lookups.embedded().find(\"x\");\n"
                "        let linker = linkers.native_linker();\n"
                "        linker.canonical_layout(\"int\");\n"
                "        linker.downcall_handle(linker.default_lookup().find_or_trap(\"abs\"),\n"
                "            descriptors.of(layouts.Int32, layouts.Int32)).invoke(1);\n"
                "        linker.upcall_stub(func(values: values_module.ForeignValue[list]) -> values_module.ForeignValue\n"
                "            => values[0], descriptors.of(layouts.Int32, layouts.Int32), arena);\n"
                "        arena.close();\n";
    }
    write_text_file(source, imports + R"(
export let All = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {)" +
                                uses + R"(        return program.ExitCode.Ok;
    };
);
)");
    const auto image = build_with_library(environment, source);
    Host host({});
    nvm::bind(image, host.registry);
    check(image.imports.size() >= 12, "the program reaches the bindings: " + std::to_string(image.imports.size()));
    std::size_t native_imports = 0;
    for (const auto& import : image.imports) {
        if (import.name.starts_with("neura.ffi.")) ++native_imports;
    }
    if (fs::exists(environment.source_dir / "stdlib/std/dynamic-foreign/linker.nra")) {
        check(native_imports == 13, "every native binding is reached: " + std::to_string(native_imports));
        check(image.callbacks.size() == 1 && image.callbacks[0].name == "neura.ffi.upcall",
              "the upcall dispatcher is the image's callback");
    }
}


// UDP, Unix-domain sockets, options, resolution and send_file
// (src/nvm/socket_provider.cpp).
NEURA_TEST(host, datagram_and_unix_socket_services) {
    Host host(everything());
    const auto a = host.call("neura.net.udp.bind", {text("127.0.0.1"), i32(0)});
    const auto b = host.call("neura.net.udp.bind", {text("127.0.0.1"), i32(0)});
    expect_ok(a, "udp bind");
    expect_ok(b, "second udp bind");
    const auto port = host.call("neura.net.local_port", {b.value});
    expect_ok(port, "udp port");
    check_equal(as_i32(host.call("neura.net.udp.send_to", {a.value, text("127.0.0.1"), port.value, bytes("datagram")})
                           .value),
                8, "udp send count");
    const auto got = host.call("neura.net.udp.receive_from", {b.value, i32(64), i64(2'000'000'000)});
    expect_ok(got, "udp receive");
    check_equal(got.value.elements.at(0).bytes, std::string("datagram"), "udp payload");
    check_equal(got.value.elements.at(1).bytes, std::string("127.0.0.1"), "udp sender");
    check_equal(as_i32(host.call("neura.net.local_port", {a.value}).value), as_i32(got.value.elements.at(2)),
                "udp sender port");
    expect_error(host.call("neura.net.udp.receive_from", {b.value, i32(64), i64(20'000'000)}), host_error::timed_out,
                 "udp receive timeout");
    expect_error(host.call("neura.net.udp.send_to", {a.value, text("example.com"), port.value, bytes("x")}),
                 host_error::invalid_input, "udp send to a name");
    expect_error(host.call("neura.net.udp.bind", {text("127.0.0.1"), i32(70000)}), host_error::invalid_input,
                 "udp port range");

    TemporaryDirectory directory("unix");
    const auto path = (directory.path() / "s.sock").string();
    const auto listener = host.call("neura.net.unix.listen", {text(path)});
    expect_ok(listener, "unix listen");
    const auto client = host.call("neura.net.unix.connect", {text(path)});
    expect_ok(client, "unix connect");
    const auto server = host.call("neura.net.accept", {listener.value});
    expect_ok(server, "unix accept");
    check_equal(as_i32(host.call("neura.net.write", {client.value, bytes("over unix")}).value), 9, "unix write");
    check_equal(host.call("neura.net.read", {server.value, i32(64), i64(0)}).value.bytes, std::string("over unix"),
                "unix read");
    expect_error(host.call("neura.net.unix.listen", {text(path)}), host_error::already_exists, "path in use");
    expect_error(host.call("neura.net.unix.connect", {text((directory.path() / "none").string())}),
                 host_error::not_found, "no such socket");
    expect_error(host.call("neura.net.unix.connect", {text(std::string(200, 'x'))}), host_error::invalid_input,
                 "path too long");
#ifdef __linux__
    const auto abstract_name = "@neura-host-test-" + std::to_string(::getpid());
    const auto abstract = host.call("neura.net.unix.listen", {text(abstract_name)});
    expect_ok(abstract, "abstract listen");
    expect_ok(host.call("neura.net.unix.connect", {text(abstract_name)}), "abstract connect");
    check(!fs::exists(abstract_name), "the abstract namespace makes no file");
#endif

    const auto receiver_path = (directory.path() / "d.sock").string();
    const auto receiver = host.call("neura.net.unix.datagram", {text(receiver_path)});
    const auto sender_path = (directory.path() / "e.sock").string();
    const auto sender = host.call("neura.net.unix.datagram", {text(sender_path)});
    expect_ok(receiver, "unix datagram");
    expect_ok(sender, "unix datagram sender");
    check_equal(as_i32(host.call("neura.net.unix.send_to", {sender.value, text(receiver_path), bytes("dg")}).value), 2,
                "unix datagram send");
    const auto datagram = host.call("neura.net.unix.receive_from", {receiver.value, i32(16), i64(2'000'000'000)});
    expect_ok(datagram, "unix datagram receive");
    check_equal(datagram.value.elements.at(0).bytes, std::string("dg"), "unix datagram payload");
    check_equal(datagram.value.elements.at(1).bytes, sender_path, "unix datagram sender");
    expect_error(host.call("neura.net.unix.receive_from", {client.value, i32(16), i64(0)}), host_error::invalid_input,
                 "datagram receive on a stream");
    host.services.end_run();
    check_equal(host.services.open_sockets(), std::size_t{0}, "datagram and unix sockets closed with the run");
}

// UDP segmentation offload and receive coalescing, Multipath TCP, and
// splice through a pipe (src/nvm/socket_provider.cpp). Each is a Linux
// feature; elsewhere the services answer unsupported.
NEURA_TEST(host, udp_offload_mptcp_and_splice) {
    Host host(everything());
#ifdef __linux__
    const auto sender = host.call("neura.net.udp.bind", {text("127.0.0.1"), i32(0)});
    const auto receiver = host.call("neura.net.udp.bind", {text("127.0.0.1"), i32(0)});
    const auto receiver_port = host.call("neura.net.local_port", {receiver.value});
    expect_ok(host.call("neura.net.option.set", {sender.value, i32(13), i64(1000)}), "UDP_SEGMENT");
    check_equal(as_i64(host.call("neura.net.option.get", {sender.value, i32(13)}).value), std::int64_t{1000},
                "UDP_SEGMENT reads back");
    // One send of 2,500 bytes leaves as datagrams of 1,000, 1,000 and 500.
    check_equal(as_i32(host.call("neura.net.udp.send_to", {sender.value, text("127.0.0.1"), receiver_port.value,
                                                           bytes(std::string(2500, 'g'))}).value),
                2500, "GSO send count");
    std::vector<std::size_t> sizes;
    for (int i = 0; i < 3; ++i) {
        const auto got = host.call("neura.net.udp.receive_segments", {receiver.value, i32(65536), i64(2'000'000'000)});
        expect_ok(got, "receive a segment");
        sizes.push_back(got.value.elements.at(0).bytes.size());
        check_equal(as_i32(got.value.elements.at(3)), static_cast<std::int32_t>(sizes.back()),
                    "without GRO the segment size is the datagram");
    }
    check(sizes == std::vector<std::size_t>{1000, 1000, 500}, "segmented by the kernel");
    // With UDP_GRO, loopback delivers a segmented send coalesced, with its
    // segment size; a kernel that does not coalesce delivers the datagrams.
    expect_ok(host.call("neura.net.option.set", {receiver.value, i32(14), i64(1)}), "UDP_GRO");
    host.call("neura.net.udp.send_to", {sender.value, text("127.0.0.1"), receiver_port.value,
                                        bytes(std::string(3000, 'h'))});
    std::size_t total = 0;
    while (total < 3000) {
        const auto got = host.call("neura.net.udp.receive_segments", {receiver.value, i32(65536), i64(2'000'000'000)});
        if (!got.success) break;
        total += got.value.elements.at(0).bytes.size();
        check_equal(as_i32(got.value.elements.at(3)), std::int32_t{1000}, "the GRO segment size");
    }
    check_equal(total, std::size_t{3000}, "every coalesced byte arrives");
    expect_error(host.call("neura.net.option.set", {receiver.value, i32(15), i64(1)}), host_error::invalid_input,
                 "the protocol option is read only");
    check_equal(as_i64(host.call("neura.net.option.get", {receiver.value, i32(15)}).value), std::int64_t{17},
                "a UDP socket's protocol");

    const auto mptcp = host.call("neura.net.mptcp.listen", {text("127.0.0.1"), i32(0)});
    if (mptcp.success) {
        const auto port = host.call("neura.net.local_port", {mptcp.value});
        const auto client = host.call("neura.net.mptcp.connect", {text("127.0.0.1"), port.value, i64(2'000'000'000)});
        expect_ok(client, "MPTCP connect");
        const auto server = host.call("neura.net.accept", {mptcp.value});
        expect_ok(server, "MPTCP accept");
        check_equal(as_i64(host.call("neura.net.option.get", {client.value, i32(15)}).value), std::int64_t{262},
                    "the client is an MPTCP socket");
        check_equal(as_i32(host.call("neura.net.write", {client.value, bytes("multipath")}).value), 9, "MPTCP write");
        check_equal(host.call("neura.net.read", {server.value, i32(64), i64(2'000'000'000)}).value.bytes,
                    std::string("multipath"), "MPTCP read");
    } else {
        check(mptcp.value.bits == host_error::unsupported, "MPTCP is supported or answers unsupported");
        std::cout << "  (MPTCP unavailable on this kernel)\n";
    }

    // A stream into a pipe and out of it into another stream: the bytes never
    // reach the NVM.
    const auto [listener, client, server] = connected(host);
    const auto [listener2, client2, server2] = connected(host);
    const auto pipe = host.call("neura.net.pipe", {});
    expect_ok(pipe, "pipe");
    const auto read_end = pipe.value.elements.at(0);
    const auto write_end = pipe.value.elements.at(1);
    host.call("neura.net.write", {i64(client), bytes("spliced bytes")});
    const auto in = host.call("neura.net.splice", {i64(server), write_end, i64(64)});
    expect_ok(in, "splice a socket into the pipe");
    check_equal(as_i64(in.value), std::int64_t{13}, "into the pipe");
    const auto out = host.call("neura.net.splice", {read_end, i64(server2), i64(64)});
    expect_ok(out, "splice the pipe into a socket");
    check_equal(host.call("neura.net.read", {i64(client2), i32(64), i64(2'000'000'000)}).value.bytes,
                std::string("spliced bytes"), "out of the other socket");
    expect_error(host.call("neura.net.splice", {i64(server), i64(server2), i64(1)}), host_error::invalid_input,
                 "splice needs a pipe on one side");
    expect_error(host.call("neura.net.splice", {i64(server), write_end, i64(0)}), host_error::invalid_input, "count 0");
    host.services.end_run();
    check_equal(host.services.open_sockets(), std::size_t{0}, "pipes are closed with the run");
    (void)listener;
    (void)listener2;
    (void)client;
    (void)server2;
#endif
    Host denied({});
    expect_error(denied.call("neura.net.pipe", {}), host_error::capability_denied, "pipe without the net group");
    expect_error(denied.call("neura.net.mptcp.listen", {text("127.0.0.1"), i32(0)}), host_error::capability_denied,
                 "MPTCP without the net group");
}

NEURA_TEST(host, socket_extensions_reject_bad_requests) {
    Host host(everything());
    Host denied({});
    expect_error(host.call("neura.net.local_address", {i64(0)}), host_error::invalid_input, "unknown socket address");
    expect_error(host.call("neura.net.shutdown", {i64(0), i32(3)}), host_error::invalid_input, "invalid shutdown");
    expect_error(host.call("neura.net.raw.open", {i32(0), i32(256)}), host_error::invalid_input, "invalid raw family");
    expect_error(host.call("neura.net.raw.send_to", {i64(0), text("127.0.0.1"), bytes("data")}),
                 host_error::invalid_input, "send requires a raw socket ID");
    expect_error(host.call("neura.net.raw.receive_from", {i64(0), i32(1), i64(0)}),
                 host_error::invalid_input, "receive requires a raw socket ID");
    expect_error(host.call("neura.net.mptcp.connect", {text("127.0.0.1"), i32(0), i64(-1)}),
                 host_error::invalid_input, "negative connect timeout");
    expect_error(host.call("neura.net.mptcp.listen", {text("127.0.0.1"), i32(-1)}),
                 host_error::invalid_input, "invalid listen port");
    expect_error(host.call("neura.net.splice", {i64(0), i64(0), i64(1)}, 2),
                 host_error::invalid_input, "relay requires two connected streams");
    expect_error(host.call("neura.net.send_zerocopy", {i64(0), bytes("data")}),
                 host_error::invalid_input, "send requires a stream socket ID");
    expect_error(host.call("neura.net.udp.send_segmented", {i64(0), text("127.0.0.1"), i32(1), bytes("data"), i32(0)}),
                 host_error::invalid_input, "invalid segment size");
    expect_error(host.call("neura.net.udp.receive_segments", {i64(0), i32(1), i64(0)}),
                 host_error::invalid_input, "coalesced receive requires UDP");
    expect_error(host.call("neura.runtime.event_loop", {i32(0)}), host_error::invalid_input, "invalid wait backend");
    expect_error(denied.call("neura.runtime.event_loop", {i32(4)}), host_error::capability_denied, "wait backend denied");

    HostValue option;
    option.elements.push_back(i32(1));
    expect_error(host.call("neura.net.listen", {text("127.0.0.1"), i32(0), option, HostValue{}}, 3),
                 host_error::invalid_input, "parallel listen option lists");
    expect_error(host.call("neura.net.udp.bind", {text("127.0.0.1"), i32(0), option, HostValue{}}, 2),
                 host_error::invalid_input, "parallel UDP option lists");
}

NEURA_TEST(host, mdns_loopback_exchange_collects_only_matching_multicast) {
    // A test-only querier and responder on the IPv4 loopback multicast group.
    struct Socket {
        int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        ~Socket() { ::close(fd); }
    } peer, other_port;
    check(peer.fd >= 0 && other_port.fd >= 0, "open mDNS senders");
    const int enabled = 1;
    check(::setsockopt(peer.fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) == 0, "share mDNS port");
#ifdef SO_REUSEPORT
    check(::setsockopt(peer.fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof enabled) == 0, "share mDNS group");
#endif
    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(5353);
    check(::inet_pton(AF_INET, "224.0.0.251", &group.sin_addr) == 1, "mDNS IPv4 group");
    // Only the querier's wildcard socket should receive unicast to this port.
    check(::bind(peer.fd, reinterpret_cast<sockaddr*>(&group), sizeof group) == 0, "bind mDNS group and port");
    const ip_mreq membership{group.sin_addr, in_addr{htonl(INADDR_LOOPBACK)}};
    check(::setsockopt(peer.fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof membership) == 0, "join group");
    check(::setsockopt(peer.fd, IPPROTO_IP, IP_MULTICAST_IF, &membership.imr_interface,
                       sizeof membership.imr_interface) == 0, "send via loopback");
    check(::setsockopt(other_port.fd, IPPROTO_IP, IP_MULTICAST_IF, &membership.imr_interface,
                       sizeof membership.imr_interface) == 0, "send noise via loopback");
    sockaddr_in unicast = group;
    unicast.sin_addr = membership.imr_interface;
    sockaddr_in ephemeral = unicast;
    ephemeral.sin_port = 0;
    check(::bind(other_port.fd, reinterpret_cast<sockaddr*>(&ephemeral), sizeof ephemeral) == 0,
          "bind a different source port");
    const unsigned char ttl = 255;
    check(::setsockopt(peer.fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) == 0, "mDNS outbound TTL");
    const std::string query("\0\0\0\0\0\1\0\0\0\0\0\0\x04h3md\x05local\0\0\1\0\1", 28);
    auto response = query;
    response[2] = static_cast<char>(0x84);
    auto wrong_port = response;
    wrong_port[3] = 1;
    auto wrong_destination = response;
    wrong_destination[3] = 2;
    const auto send = [](int descriptor, const std::string& message, const sockaddr_in& destination) {
        const auto sent = ::sendto(descriptor, message.data(), message.size(), 0,
                                   reinterpret_cast<const sockaddr*>(&destination), sizeof destination);
        return sent >= 0 && static_cast<std::size_t>(sent) == message.size();
    };
    std::array<bool, 3> sent{};
    std::jthread responder([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            pollfd readable{peer.fd, POLLIN, 0};
            if (::poll(&readable, 1, 10) != 1) continue;
            std::array<char, 128> data{};
            const auto size = ::recv(peer.fd, data.data(), data.size(), MSG_DONTWAIT);
            if (size < 0 || std::string_view(data.data(), static_cast<std::size_t>(size)) != query) continue;
            sent[0] = send(other_port.fd, wrong_port, group);
            sent[1] = send(peer.fd, wrong_destination, unicast);
            sent[2] = send(peer.fd, response, group);
            return;
        }
    });
    Host host(everything());
    const auto answer = host.call("neura.net.mdns.exchange", {bytes(query), i64(150'000'000), text("127.0.0.1")});
    responder.request_stop();
    responder.join();
    expect_ok(answer, "mDNS query");
    check(sent[0] && sent[1] && sent[2], "the three loopback datagrams were sent");
    const auto contains = [&](const std::string& message) {
        return std::any_of(answer.value.elements.begin(), answer.value.elements.end(),
                           [&](const auto& packet) { return packet.bytes == message; });
    };
    check(contains(response), "received the loopback responder's exact datagram");
    check(!contains(wrong_port), "multicast from a non-mDNS source port was discarded");
    check(!contains(wrong_destination), "same-port unicast was discarded");
    Host denied({});
    expect_error(denied.call("neura.net.mdns.exchange", {bytes(query), i64(1), text("127.0.0.1")}),
                 host_error::capability_denied, "mDNS denied");
}

NEURA_TEST(host, socket_options_resolution_and_send_file) {
    Host host(everything());
    const auto [listener, client, server] = connected(host);
    const auto set = [&](std::int64_t socket, std::int32_t option, std::int64_t value) {
        return host.call("neura.net.option.set", {i64(socket), i32(option), i64(value)});
    };
    const auto get = [&](std::int64_t socket, std::int32_t option) {
        return host.call("neura.net.option.get", {i64(socket), i32(option)});
    };
    expect_ok(set(client, 3, 1), "TCP_NODELAY");
    check(as_i64(get(client, 3).value) != 0, "TCP_NODELAY reads back");
    expect_ok(set(client, 6, 1), "SO_KEEPALIVE");
    check(as_i64(get(client, 6).value) != 0, "SO_KEEPALIVE reads back");
    expect_ok(set(listener, 1, 1), "SO_REUSEADDR");
#ifdef __linux__
    expect_ok(set(client, 5, 1500), "TCP_USER_TIMEOUT");
    check_equal(as_i64(get(client, 5).value), std::int64_t{1500}, "TCP_USER_TIMEOUT reads back");
    expect_ok(set(client, 7, 30), "TCP_KEEPIDLE");
    expect_ok(set(client, 8, 5), "TCP_KEEPINTVL");
    expect_ok(set(client, 9, 4), "TCP_KEEPCNT");
    check_equal(as_i64(get(client, 9).value), std::int64_t{4}, "TCP_KEEPCNT reads back");
    expect_ok(set(listener, 2, 1), "SO_REUSEPORT");
    expect_ok(set(listener, 4, 16), "TCP_FASTOPEN");
#endif
    expect_error(set(client, 99, 1), host_error::invalid_input, "unknown option");
    expect_error(set(client, 3, -1), host_error::invalid_input, "negative value");
    expect_error(set(1234, 3, 1), host_error::invalid_input, "unknown socket");

    const auto local = host.call("neura.net.resolve", {text("localhost"), i32(4)});
    expect_ok(local, "resolve localhost");
    check(!local.value.elements.empty() && local.value.elements[0].bytes == "127.0.0.1", "localhost is 127.0.0.1");
    const auto numeric = host.call("neura.net.resolve", {text("::1"), i32(0)});
    expect_ok(numeric, "resolve a numeric address");
    check_equal(numeric.value.elements.at(0).bytes, std::string("::1"), "numeric address");
    // NotFound with a working resolver; Other when the machine has none.
    const auto invalid = host.call("neura.net.resolve", {text("name.invalid"), i32(0)});
    check(!invalid.success && (invalid.value.bits == host_error::not_found || invalid.value.bits == host_error::other),
          "the .invalid domain does not resolve");
    expect_error(host.call("neura.net.resolve", {text("localhost"), i32(5)}), host_error::invalid_input, "family");

    TemporaryDirectory directory("send-file");
    const auto file_path = directory.path() / "payload";
    {
        std::ofstream out(file_path, std::ios::binary);
        out << "0123456789abcdef";
    }
    const auto file = host.call("neura.fs.open", {text(file_path.string()), i32(0)});
    expect_ok(file, "open the payload");
    const auto sent = host.call("neura.net.send_file", {i64(server), file.value, i64(4), i64(8)});
    expect_ok(sent, "send_file");
    check_equal(as_i64(sent.value), std::int64_t{8}, "send_file count");
    check_equal(host.call("neura.net.read", {i64(client), i32(64), i64(2'000'000'000)}).value.bytes,
                std::string("456789ab"), "send_file bytes");
    const auto past_end = host.call("neura.net.send_file", {i64(server), file.value, i64(12), i64(100)});
    check_equal(as_i64(past_end.value), std::int64_t{4}, "send_file stops at the end of the file");
    expect_error(host.call("neura.net.send_file", {i64(listener), file.value, i64(0), i64(1)}),
                 host_error::invalid_input, "send_file to a listener");

    nvm::Capabilities net_only;
    net_only.net = true;
    Host no_files(net_only);
    expect_error(no_files.call("neura.net.send_file", {i64(1), i64(1), i64(0), i64(1)}),
                 host_error::capability_denied, "send_file needs fs");
    Host nothing({});
    for (const auto* name : {"neura.net.udp.bind", "neura.net.unix.listen", "neura.net.unix.connect",
                             "neura.net.unix.datagram", "neura.net.resolve"}) {
        const bool udp = std::string_view(name) == "neura.net.udp.bind";
        const bool resolve = std::string_view(name) == "neura.net.resolve";
        std::vector<HostValue> arguments{text("127.0.0.1")};
        if (udp || resolve) arguments.push_back(i32(0));
        expect_error(nothing.call(name, std::move(arguments)), host_error::capability_denied, name);
    }
    expect_error(nothing.call("neura.net.option.set", {i64(1), i32(3), i64(1)}), host_error::capability_denied,
                 "option denied");
}

NEURA_TEST(host, entropy_comes_from_the_system_generator) {
    Host host(everything());
    const auto first = host.call("neura.entropy.fill", {i32(32)});
    const auto second = host.call("neura.entropy.fill", {i32(32)});
    expect_ok(first, "entropy");
    check_equal(first.value.bytes.size(), std::size_t{32}, "entropy length");
    check(first.value.bytes != second.value.bytes, "two draws differ");
    check_equal(host.call("neura.entropy.fill", {i32(0)}).value.bytes.size(), std::size_t{0}, "empty draw");
    expect_error(host.call("neura.entropy.fill", {i32(-1)}), host_error::invalid_input, "negative count");
    expect_error(host.call("neura.entropy.fill", {i32((1 << 20) + 1)}), host_error::invalid_input, "over 1 MiB");

    const auto hardware = host.call("neura.entropy.hardware", {i32(20)});
    if (hardware.success) {
        check_equal(hardware.value.bytes.size(), std::size_t{20}, "hardware entropy length");
        check(hardware.value.bytes != host.call("neura.entropy.hardware", {i32(20)}).value.bytes, "two hardware draws");
    } else {
        check_equal(static_cast<std::uint32_t>(hardware.value.bits), host_error::unsupported, "no hardware source");
    }
    expect_error(host.call("neura.entropy.hardware", {i32(-1)}), host_error::invalid_input, "negative hardware count");
    const auto generation = host.call("neura.process.generation", {});
    expect_ok(generation, "generation");
    check_equal(host.call("neura.process.generation", {}).value.bits, generation.value.bits, "stable without a fork");
    const pid_t child = ::fork();
    if (child == 0) {
        const auto forked = host.call("neura.process.generation", {});
        std::_Exit(forked.value.bits == generation.value.bits + 1 ? 0 : 1);
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "a forked child sees the next generation");
    using namespace std::string_view_literals;
    check(nvm::vm_generation_uevent("change@/devices/LNXSYSTM:00/QEMUVGID:00\0ACTION=change\0NEW_VMGENID=1\0"sv),
          "a vmgenid uevent is a new generation");
    check(!nvm::vm_generation_uevent("change@/devices/x\0ACTION=change\0NEW_VMGENID=10\0"sv), "only the exact field");
    check(!nvm::vm_generation_uevent("add@/devices/y\0ACTION=add\0"sv), "another uevent");
    Host nothing({});
    expect_error(nothing.call("neura.entropy.hardware", {i32(1)}), host_error::capability_denied, "hardware denied");
    expect_error(nothing.call("neura.process.generation", {}), host_error::capability_denied, "generation denied");
}


// Secret memory (src/nvm/secret_services.cpp): locked, dump-excluded pages
// addressed by handle, zeroed on release.
NEURA_TEST(host, secret_memory_is_locked_and_released) {
    nvm::SecretServices secrets(true);
    nvm::HostRegistry registry;
    secrets.register_all(registry);
    const auto call = [&](std::string_view name, std::vector<HostValue> arguments) {
        return registry.find(name, 1)->invoke(nvm::HostRequest{0, 0, std::move(arguments)});
    };
#ifdef __linux__
    const auto created = call("neura.memory.lock.create", {i32(32)});
    expect_ok(created, "create");
    const auto handle = created.value;
    check_equal(secrets.live_buffers(), std::size_t{1}, "one buffer");
    expect_ok(call("neura.memory.lock.write", {handle, i32(0), bytes("correct horse battery staple")}), "write");
    const auto read = call("neura.memory.lock.read", {handle, i32(8), i32(5)});
    expect_ok(read, "read");
    check_equal(read.value.bytes, std::string("horse"), "read back");
    const auto zeroes = call("neura.memory.lock.read", {handle, i32(28), i32(4)});
    check_equal(zeroes.value.bytes, std::string(4, '\0'), "the rest is zero-filled");
    std::string whole = "correct horse battery staple";
    whole.append(4, '\0');
    check(call("neura.memory.lock.equal", {handle, bytes(whole)}).value.bits == 1, "equal");
    check(call("neura.memory.lock.equal", {handle, bytes("correct horse")}).value.bits == 0, "shorter is not equal");
    whole[0] = 'C';
    check(call("neura.memory.lock.equal", {handle, bytes(whole)}).value.bits == 0, "one byte differs");
    expect_error(call("neura.memory.lock.write", {handle, i32(30), bytes("xyz")}), host_error::invalid_input,
                 "write past the end");
    expect_error(call("neura.memory.lock.read", {handle, i32(-1), i32(1)}), host_error::invalid_input, "offset -1");
    expect_error(call("neura.memory.lock.read", {i64(99), i32(0), i32(1)}), host_error::invalid_input, "forged handle");
    expect_error(call("neura.memory.lock.create", {i32(0)}), host_error::invalid_input, "size 0");
    expect_error(call("neura.memory.lock.create", {i32(65537)}), host_error::invalid_input, "size past 64 KiB");

    // The kernel's view of the mapping: locked and excluded from core dumps.
    // AddressSanitizer intercepts mlock and makes it do nothing, so a
    // sanitized build has no locked mapping to find.
#if defined(__SANITIZE_ADDRESS__)
#define NEURA_TEST_MLOCK_INTERCEPTED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NEURA_TEST_MLOCK_INTERCEPTED 1
#endif
#endif
#if !defined(NEURA_TEST_MLOCK_INTERCEPTED)
    std::ifstream smaps("/proc/self/smaps");
    std::string line;
    bool in_region = false;
    bool locked = false;
    bool dont_dump = false;
    bool wipe_on_fork = false;
    while (std::getline(smaps, line)) {
        if (!line.empty() && std::isxdigit(static_cast<unsigned char>(line[0])) && line.find('-') != std::string::npos) {
            in_region = false;
            continue;
        }
        if (line.starts_with("Locked:") && line.find(" 4 kB") != std::string::npos) in_region = true;
        if (line.starts_with("Locked:") && line.find(" 0 kB") == std::string::npos) locked = true;
        if (in_region && line.starts_with("VmFlags:")) {
            dont_dump = dont_dump || line.find(" dd") != std::string::npos;
            wipe_on_fork = wipe_on_fork || line.find(" wf") != std::string::npos;
        }
    }
    check(locked, "a mapping is locked");
    check(dont_dump, "the locked mapping is excluded from core dumps (dd)");
    check(wipe_on_fork, "the locked mapping is wiped in a forked child (wf)");
#endif

    expect_ok(call("neura.memory.lock.release", {handle}), "release");
    check_equal(secrets.live_buffers(), std::size_t{0}, "released");
    expect_error(call("neura.memory.lock.read", {handle, i32(0), i32(1)}), host_error::invalid_input, "stale handle");
    expect_error(call("neura.memory.lock.release", {handle}), host_error::invalid_input, "second release");

    // The 1 MiB budget, then release_all at the end of the run.
    std::size_t made = 0;
    for (; made < 40; ++made) {
        if (!call("neura.memory.lock.create", {i32(65536)}).success) break;
    }
    check(made == 16, "sixteen 64 KiB buffers fit the 1 MiB budget: " + std::to_string(made));
    expect_error(call("neura.memory.lock.create", {i32(1)}), host_error::too_large, "past the budget");
    secrets.release_all();
    check_equal(secrets.locked_bytes(), std::size_t{0}, "nothing locked after release_all");

    secrets.set_recording(true);
    expect_error(call("neura.memory.lock.create", {i32(16)}), host_error::unsupported, "no buffer in a recorded run");
#else
    (void)call;
#endif
    nvm::SecretServices denied(false);
    nvm::HostRegistry other;
    denied.register_all(other);
    expect_error(other.find("neura.memory.lock.create", 1)->invoke(nvm::HostRequest{0, 0, {i32(8)}}),
                 host_error::capability_denied, "secret memory without the entropy group");
}


// The application services of a server image (src/nvm/app_provider.cpp)
// against a recording exchange.
NEURA_TEST(host, application_services_check_order_and_limits) {
    struct Recorder final : nvm::ServerExchange {
        std::optional<nvm::ServerRequest> next() override {
            nvm::ServerRequest request;
            request.request_id = "r";
            request.method = "POST";
            request.target = "/x?y";
            request.headers = {{"host", "h"}, {"content-type", "text/plain"}};
            request.body_length = 5;
            return request;
        }
        std::uint32_t read_body(std::size_t max, std::string& out) override {
            out = body.substr(0, max);
            body.erase(0, out.size());
            return 0;
        }
        std::uint32_t metric(std::string_view name, std::uint32_t kind, std::int64_t value) override {
            metrics.push_back(std::string(name) + ":" + std::to_string(kind) + ":" + std::to_string(value));
            return 0;
        }
        std::vector<std::string> metrics;
        std::uint32_t respond(std::uint32_t code, const nvm::HeaderPairs& fields, std::string_view content,
                              bool complete) override {
            status = code;
            headers = fields;
            sent += content;
            ended = complete;
            return 0;
        }
        std::uint32_t write_body(std::string_view bytes) override {
            sent += bytes;
            return 0;
        }
        std::uint32_t close_body() override {
            ended = true;
            return 0;
        }
        std::optional<std::string> config(std::string_view key) override {
            if (key == "k") return std::string("v");
            return std::nullopt;
        }
        void log(std::uint32_t, std::string_view message, const nvm::HeaderPairs&) override { logged = message; }
        std::string body = "hello";
        std::uint32_t status = 0;
        nvm::HeaderPairs headers;
        std::string sent;
        bool ended = false;
        std::string logged;
    };
    const auto flat = [](std::vector<std::string> values) {
        HostValue list;
        for (auto& value : values) list.elements.push_back(text(std::move(value)));
        return list;
    };
    Recorder exchange;
    nvm::ApplicationServices services(&exchange, true, true, 10);
    nvm::HostRegistry registry;
    services.register_all(registry);
    const auto call = [&](std::string_view name, std::vector<HostValue> arguments) {
        return registry.find(name, 1)->invoke(nvm::HostRequest{0, 0, std::move(arguments)});
    };
    expect_error(call("neura.http.server.respond", {i32(200), flat({}), bytes(""), boolean(true)}),
                 host_error::invalid_input, "respond before next");
    const auto request = call("neura.http.server.next", {});
    expect_ok(request, "next");
    check_equal(request.value.elements.at(2).bytes, std::string("POST"), "method");
    check_equal(request.value.elements.at(4).elements.size(), std::size_t{4}, "flat header fields");
    expect_error(call("neura.http.server.next", {}), host_error::invalid_input, "a second next");
    check_equal(call("neura.http.server.body.read", {i32(3)}).value.bytes, std::string("hel"), "body read");
    check_equal(call("neura.http.server.body.read", {i32(64)}).value.bytes, std::string("lo"), "body rest");
    check_equal(call("neura.http.server.body.read", {i32(64)}).value.bytes, std::string(), "body end");
    expect_error(call("neura.http.server.respond", {i32(99), flat({}), bytes(""), boolean(true)}),
                 host_error::invalid_input, "status 99");
    expect_error(call("neura.http.server.respond", {i32(200), flat({"Bad Name", "v"}), bytes(""), boolean(true)}),
                 host_error::invalid_input, "header name with a space");
    expect_error(call("neura.http.server.respond", {i32(200), flat({"X", "a\r\nb"}), bytes(""), boolean(true)}),
                 host_error::invalid_input, "header value with CR LF");
    expect_error(call("neura.http.server.respond", {i32(200), flat({"X"}), bytes(""), boolean(true)}),
                 host_error::invalid_input, "odd field list");
    expect_error(call("neura.http.server.respond", {i32(200), flat({}), bytes("01234567890"), boolean(true)}),
                 host_error::too_large, "complete body over S-7");
    // A streamed body is not buffered, so S-7 does not count it.
    expect_ok(call("neura.http.server.respond", {i32(201), flat({"X-A", "1"}), bytes("0123"), boolean(false)}),
              "respond with an open body");
    expect_ok(call("neura.http.server.body.write", {bytes("456789")}), "write up to S-7");
    expect_ok(call("neura.http.server.body.write", {bytes("x")}), "a streamed body past S-7");
    expect_ok(call("neura.http.server.body.close", {}), "close");
    expect_error(call("neura.http.server.body.write", {bytes("")}), host_error::invalid_input, "write after close");
    check_equal(exchange.status, std::uint32_t{201}, "status sent");
    check_equal(exchange.sent, std::string("0123456789x"), "body sent");
    check(exchange.ended && exchange.headers.size() == 1, "ended with one field");
    const auto found = call("neura.app.config", {text("k")});
    check(found.success && found.value.elements.at(0).bits == 1 && found.value.elements.at(1).bytes == "v", "config");
    check(call("neura.app.config", {text("none")}).value.elements.at(0).bits == 0, "config missing");
    expect_ok(call("neura.app.log", {i32(2), text("note"), flat({"a", "b"})}), "log");
    check_equal(exchange.logged, std::string("note"), "logged message");
    expect_error(call("neura.app.log", {i32(9), text("x"), flat({})}), host_error::invalid_input, "log level");
    expect_ok(call("neura.app.metric", {text("orders_total"), i32(1), i64(3)}), "counter");
    expect_ok(call("neura.app.metric", {text("queue:depth"), i32(2), i64(-4)}), "gauge");
    expect_error(call("neura.app.metric", {text("9lives"), i32(1), i64(1)}), host_error::invalid_input, "metric name");
    expect_error(call("neura.app.metric", {text("bad-name"), i32(1), i64(1)}), host_error::invalid_input, "dash");
    expect_error(call("neura.app.metric", {text("x"), i32(3), i64(1)}), host_error::invalid_input, "metric kind");
    expect_error(call("neura.app.metric", {text("x"), i32(1), i64(-1)}), host_error::invalid_input, "counter down");
    check(exchange.metrics == std::vector<std::string>{"orders_total:1:3", "queue:depth:2:-4"}, "metrics forwarded");

    nvm::ApplicationServices absent(nullptr, true, true);
    nvm::HostRegistry other;
    absent.register_all(other);
    for (const auto* name : {"neura.http.server.next", "neura.http.server.body.close"}) {
        expect_error(other.find(name, 1)->invoke(nvm::HostRequest{}), host_error::capability_denied, name);
    }
    expect_error(other.find("neura.app.config", 1)->invoke(nvm::HostRequest{0, 0, {text("k")}}),
                 host_error::capability_denied, "config without a host");
}


// The internal bindings of Std.Net.Internal.Sockets and Std.Net.Internal.HttpServer
// bind to the providers. Programs cannot import internal modules, so the
// check module is added to a copy of the library.
NEURA_TEST(host, internal_socket_and_server_bindings_match_the_providers) {
    if (!fs::exists(environment.source_dir / "stdlib/std/net/internal/sockets.nra")) return;
    TemporaryDirectory dir("internal-bindings");
    const auto library = dir.path() / "stdlib";
    fs::copy(environment.source_dir / "stdlib", library, fs::copy_options::recursive);
    write_text_file(library / "std/binding-check.nra", R"(let server = import:Std.Net.Internal.HttpServer;
let sockets = import:Std.Net.Internal.Sockets;
let locked = import:Std.Crypto.Internal.LockedMemory;

export let BindingCheck = module (
    export let run = func() {
        last bytes: uint8[list] = new;
        last fields: string[list] = new;
        last results = [sockets.udp_bind("x", 1), sockets.unix_connect("x"), sockets.unix_listen("x"),
                        sockets.unix_datagram("x"), sockets.accept(1 as int64)];
        last counts = [sockets.udp_send_to(1 as int64, "x", 1, bytes), sockets.unix_send_to(1 as int64, "x", bytes),
                       sockets.local_port(1 as int64), sockets.write(1 as int64, bytes)];
        last datagrams = [sockets.udp_receive_from(1 as int64, 1, 0 as int64),
                          sockets.unix_receive_from(1 as int64, 1, 0 as int64)];
        last longs = [sockets.get_option(1 as int64, 1), sockets.send_file(1 as int64, 1 as int64, 0 as int64, 0 as int64)];
        last done = [sockets.set_option(1 as int64, 1, 0 as int64), sockets.close(1 as int64)];
        last resolved = sockets.resolve("x", 0);
        last read = sockets.read(1 as int64, 1, 0 as int64);
        last next = server.next_request();
        last body = server.read_body(1);
        last replies = [server.respond(200 as uint32, fields, bytes, true), server.write_body(bytes), server.close_body(),
                        server.log(0 as uint32, "x", fields)];
        last config = server.config("x");
        last metric = server.metric("x", 1 as uint32, 1 as int64);
        last secret = locked.create(1);
        last secret_done = [locked.write(1 as int64, 0, bytes), locked.release(1 as int64)];
        last secret_read = locked.read(1 as int64, 0, 1);
        last secret_equal = locked.equal(1 as int64, bytes);
        last tls = [sockets.tls_connect("x", 1, 0 as int64, "", fields, ""),
                    sockets.tls_accept(1 as int64, "c", "k", fields, 0 as int64)];
        last tls_read = sockets.tls_read(1 as int64, 1, 0 as int64);
        last tls_write = sockets.tls_write(1 as int64, bytes);
        last tls_close = sockets.tls_close(1 as int64);
    };
);
)");
    const auto source = dir.path() / "check.nra";
    write_text_file(source, R"(let check = import:Std.BindingCheck;
let program = import:Std.Program;

export let Check = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        check.run();
        return program.ExitCode.Ok;
    };
);
)");
    const auto canonical = fs::canonical(source);
    auto lowered = neura::lower(neura::read_sources({canonical}, {canonical.parent_path()}, library));
    const auto image = nvr::load(nvr::encode(backend::build(std::move(lowered), {}).image));
    Host host({});
    nvm::ApplicationServices application(nullptr, false, false);
    application.register_all(host.registry);
    nvm::SecretServices secrets(false);
    secrets.register_all(host.registry);
    nvm::bind(image, host.registry);
    check(image.imports.size() >= 35, "every internal binding is reached: " + std::to_string(image.imports.size()));
}


#ifdef NEURA_NVM_TLS
// TLS tests generate a synthetic certificate used only on the loopback interface.
struct LoopbackTlsKeys {
    TemporaryDirectory dir{"tls"};
    std::string certificate = (dir.path() / "cert.pem").string();
    std::string key = (dir.path() / "key.pem").string();

    LoopbackTlsKeys() {
        const auto command = "openssl req -x509 -newkey rsa:2048 -nodes -keyout '" + key + "' -out '" + certificate +
                             "' -days 2 -subj /CN=localhost -addext subjectAltName=IP:127.0.0.1,DNS:localhost "
                             ">/dev/null 2>&1";
        check(std::system(command.c_str()) == 0, "generate loopback TLS certificate with openssl");
    }
};

HostValue tls_names(std::initializer_list<std::string_view> values) {
    HostValue list;
    for (const auto value : values) list.elements.push_back(text(std::string(value)));
    return list;
}

// TLS client and server streams (src/nvm/tls_provider.cpp) against each
// other, with a certificate the openssl command generates.
NEURA_TEST(host, tls_streams_verify_peers_and_negotiate_alpn) {
    const LoopbackTlsKeys material;
    const auto& certificate = material.certificate;
    const auto& key = material.key;
    Host server(everything());
    const auto listener = server.call("neura.net.listen", {text("127.0.0.1"), i32(0)});
    expect_ok(listener, "listen");
    const auto port = server.call("neura.net.local_port", {listener.value});
    std::vector<std::string> served;
    std::thread acceptor([&] {
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto accepted = server.call("neura.net.tls.accept", {listener.value, text(certificate), text(key),
                                                                       tls_names({"h2", "http/1.1"}), i64(10'000'000'000)});
            if (!accepted.success) {
                served.push_back("failed");
                continue;
            }
            const auto id = accepted.value.elements.at(0);
            served.push_back(accepted.value.elements.at(1).bytes);
            const auto got = server.call("neura.net.tls.read", {id, i32(64), i64(5'000'000'000)});
            server.call("neura.net.tls.write", {id, bytes("pong:" + got.value.bytes)});
            server.call("neura.net.tls.close", {id});
        }
    });
    Host client(everything());
    const auto connected = client.call("neura.net.tls.connect", {text("127.0.0.1"), port.value, i64(10'000'000'000),
                                                                 text(""), tls_names({"http/1.1"}), text(certificate)});
    expect_ok(connected, "verified connect");
    check_equal(connected.value.elements.at(1).bytes, std::string("http/1.1"), "client sees the ALPN choice");
    const auto id = connected.value.elements.at(0);
    check_equal(as_i32(client.call("neura.net.tls.write", {id, bytes("ping")}).value), 4, "tls write");
    check_equal(client.call("neura.net.tls.read", {id, i32(64), i64(5'000'000'000)}).value.bytes,
                std::string("pong:ping"), "tls read");
    check_equal(client.call("neura.net.tls.read", {id, i32(64), i64(5'000'000'000)}).value.bytes, std::string(),
                "empty at the end");
    expect_ok(client.call("neura.net.tls.close", {id}), "tls close");
    expect_error(client.call("neura.net.tls.close", {id}), host_error::invalid_input, "closed twice");

    expect_error(client.call("neura.net.tls.connect", {text("127.0.0.1"), port.value, i64(10'000'000'000), text(""),
                                                       tls_names({}), text("")}),
                 host_error::permission_denied, "a self-signed peer is not in the system store");
    expect_error(client.call("neura.net.tls.connect", {text("127.0.0.1"), port.value, i64(10'000'000'000),
                                                       text("other.example"), tls_names({}), text(certificate)}),
                 host_error::permission_denied, "a name the certificate does not carry");
    acceptor.join();
    check(served.size() == 3 && served[0] == "http/1.1", "the server chose http/1.1 first");
    expect_error(client.call("neura.net.tls.connect", {text("127.0.0.1"), i32(0), i64(0), text(""), tls_names({}),
                                                       text("")}),
                 host_error::invalid_input, "port 0");
    Host nothing({});
    expect_error(nothing.call("neura.net.tls.connect", {text("127.0.0.1"), i32(443), i64(0), text(""), tls_names({}),
                                                        text("")}),
                 host_error::capability_denied, "tls denied");
}

#if !defined(OPENSSL_NO_QUIC)
namespace {

constexpr std::string_view test_dns_query{"\0\x11\0\0\0\0\0\1\0\0\0\0\0\0\0\0\1\0\1", 19};

int doq_test_alpn(SSL*, const unsigned char** out, unsigned char* out_length, const unsigned char* input,
                  unsigned int length, void*) {
    static constexpr unsigned char protocol[] = {3, 'd', 'o', 'q'};
    unsigned char* selected = nullptr;
    if (SSL_select_next_proto(&selected, out_length, protocol, sizeof protocol, input, length) !=
        OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    *out = selected;
    return SSL_TLSEXT_ERR_OK;
}

class DoqTestServer {
public:
    explicit DoqTestServer(const std::string& certificate, const std::string& key)
        : descriptor_(BIO_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, BIO_SOCK_NONBLOCK)),
          context_(SSL_CTX_new(OSSL_QUIC_server_method()), SSL_CTX_free), listener_(nullptr, SSL_free) {
        check(descriptor_ >= 0, "open UDP loopback listener");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(::bind(descriptor_, reinterpret_cast<sockaddr*>(&address), sizeof address) == 0, "bind QUIC listener");
        socklen_t length = sizeof address;
        check(::getsockname(descriptor_, reinterpret_cast<sockaddr*>(&address), &length) == 0, "read bound port");
        port = ntohs(address.sin_port);
        check(context_ != nullptr, "create QUIC server context");
        check(SSL_CTX_use_certificate_file(context_.get(), certificate.c_str(), SSL_FILETYPE_PEM) == 1,
              "load QUIC fallback certificate");
        check(SSL_CTX_use_PrivateKey_file(context_.get(), key.c_str(), SSL_FILETYPE_PEM) == 1, "load QUIC key");
        SSL_CTX_set_alpn_select_cb(context_.get(), doq_test_alpn, nullptr);
        listener_.reset(SSL_new_listener(context_.get(), 0));
        check(listener_ != nullptr, "allocate QUIC listener");
        BIO* bio = BIO_new_dgram(descriptor_, BIO_NOCLOSE);
        check(bio != nullptr, "wrap UDP descriptor");
        SSL_set_bio(listener_.get(), bio, bio);
        check(SSL_set_blocking_mode(listener_.get(), 0) == 1 && SSL_listen(listener_.get()) == 1, "listen QUIC");
    }

    ~DoqTestServer() {
        listener_.reset();
        BIO_closesocket(descriptor_);
    }

    enum class Response { Valid, Truncated, Rejected, Close };

    void serve(std::stop_token stopping, Response response, bool& exchanged) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        const auto retry = [&] { return !stopping.stop_requested() && std::chrono::steady_clock::now() < deadline; };
        std::unique_ptr<SSL, decltype(&SSL_free)> connection(nullptr, SSL_free);
        while (retry() && !connection) {
            connection.reset(SSL_accept_connection(listener_.get(), SSL_ACCEPT_CONNECTION_NO_BLOCK));
            if (!connection) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!connection) return;
        while (retry() && !SSL_is_init_finished(connection.get())) {
            ERR_clear_error();
            const auto accepted = SSL_accept(connection.get());
            if (accepted == 1) break;
            const auto error = SSL_get_error(connection.get(), accepted);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!SSL_is_init_finished(connection.get())) return;
        std::unique_ptr<SSL, decltype(&SSL_free)> stream(nullptr, SSL_free);
        while (retry() && !stream) {
            stream.reset(SSL_accept_stream(connection.get(), SSL_ACCEPT_STREAM_NO_BLOCK));
            if (!stream) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!stream || response == Response::Rejected) return;

        std::string query;
        bool finished = false;
        while (retry() && query.size() <= test_dns_query.size()) {
            std::array<char, 32> data{};
            std::size_t received = 0;
            ERR_clear_error();
            const int status = SSL_read_ex(stream.get(), data.data(), data.size(), &received);
            if (status == 1) {
                query.append(data.data(), received);
            } else if (const auto error = SSL_get_error(stream.get(), status); error == SSL_ERROR_ZERO_RETURN) {
                finished = true;
                break;
            } else if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                return;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (!finished || query != test_dns_query) return;
        exchanged = true;
        if (response == Response::Close) {
            static_cast<void>(SSL_shutdown_ex(connection.get(), SSL_SHUTDOWN_FLAG_NO_BLOCK, nullptr, 0));
            return;
        }
        query[4] = static_cast<char>(0x80);
        if (response == Response::Truncated) query.resize(8);
        std::size_t sent = 0;
        while (retry()) {
            ERR_clear_error();
            const int status = SSL_write_ex2(stream.get(), query.data(), query.size(), SSL_WRITE_FLAG_CONCLUDE, &sent);
            if (status == 1 && sent == query.size()) break;
            const auto error = SSL_get_error(stream.get(), status);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while (retry() && SSL_handle_events(connection.get())) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    int port = 0;

private:
    int descriptor_;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context_;
    std::unique_ptr<SSL, decltype(&SSL_free)> listener_;
};

} // namespace

NEURA_TEST(host, doq_loopback_checks_identity_and_exact_stream_framing) {
    TemporaryDirectory dir("doq");
    const auto certificate = dir / "cert.pem";
    const auto key = dir / "key.pem";
    const auto command = "openssl req -x509 -newkey rsa:2048 -nodes -keyout '" + key + "' -out '" + certificate +
                         "' -days 2 -subj /CN=localhost -addext subjectAltName=DNS:localhost >/dev/null 2>&1";
    if (std::system(command.c_str()) != 0) {
        std::cout << "  (skipped: no openssl command)\n";
        return;
    }
    const auto configure_trust = [](const std::string& path) {
        check(::setenv("SSL_CERT_FILE", path.c_str(), 1) == 0, "set DoQ test trust store");
    };
    struct RestoreTrust {
        std::optional<std::string> saved;
        ~RestoreTrust() {
            if (saved) ::setenv("SSL_CERT_FILE", saved->c_str(), 1);
            else ::unsetenv("SSL_CERT_FILE");
        }
    } restore;
    if (const char* old = std::getenv("SSL_CERT_FILE")) restore.saved = old;
    Host client(everything());
    Host denied({});
    const auto arguments = [](int port, std::string name, std::string payload = std::string(test_dns_query)) {
        return std::vector<HostValue>{text("127.0.0.1"), i32(port), text(std::move(name)), bytes(std::move(payload))};
    };
    const auto exchange = [&](DoqTestServer::Response response, std::string name) {
        DoqTestServer peer(certificate, key);
        bool received_query = false;
        std::jthread serving([&](std::stop_token stop) { peer.serve(stop, response, received_query); });
        auto result = client.call("neura.net.quic.dns_exchange", arguments(peer.port, std::move(name)));
        serving.request_stop();
        serving.join();
        if (response != DoqTestServer::Response::Rejected) check(received_query, "query and stream FIN received");
        return result;
    };
    configure_trust(certificate);
    const auto reply = exchange(DoqTestServer::Response::Valid, "localhost");
    expect_ok(reply, "verified DNS-over-QUIC exchange");
    auto expected = std::string(test_dns_query.substr(2));
    expected[2] = static_cast<char>(0x80);
    check_equal(reply.value.bytes, expected, "complete response, without the length prefix");
    expect_error(exchange(DoqTestServer::Response::Truncated, "localhost"), host_error::unexpected_end,
                 "stream FIN with an incomplete record");
    expect_error(exchange(DoqTestServer::Response::Close, "localhost"), host_error::connection_reset,
                 "peer closes the connection before responding");
    expect_error(exchange(DoqTestServer::Response::Rejected, "somewhere.invalid"), host_error::permission_denied,
                 "certificate hostname does not match");
    configure_trust(dir / "untrusted.pem");
    expect_error(exchange(DoqTestServer::Response::Rejected, "localhost"), host_error::permission_denied,
                 "certificate is not in the configured trust store");
    configure_trust(certificate);
    DoqTestServer silent_peer(certificate, key);
    expect_error(client.call("neura.net.quic.dns_exchange", arguments(silent_peer.port, "localhost")),
                 host_error::timed_out, "loopback peer never replies");
    expect_error(client.call("neura.net.quic.dns_exchange", arguments(silent_peer.port, "")),
                 host_error::invalid_input, "missing server identity");
    expect_error(client.call("neura.net.quic.dns_exchange", arguments(silent_peer.port, "localhost", "short")),
                 host_error::invalid_input, "truncated query");
    expect_error(denied.call("neura.net.quic.dns_exchange", arguments(silent_peer.port, "localhost")),
                 host_error::capability_denied, "network disabled");
}
#endif

NEURA_TEST(host, tls_v2_requires_exact_versions_and_mutual_identity) {
    const LoopbackTlsKeys material;
    constexpr auto wait = 2'000'000'000LL;
    Host server(everything());
    Host client(everything());
    const auto listener = server.call("neura.net.listen", {text("127.0.0.1"), i32(0)});
    expect_ok(listener, "listen");
    const auto port = server.call("neura.net.local_port", {listener.value});
    const auto connect = [&](int version, bool mutual) {
        return client.call("neura.net.tls.connect", {text("127.0.0.1"), port.value, i64(wait), text("localhost"),
            tls_names({"h2"}), text(material.certificate), i32(version), i32(version),
            text(mutual ? material.certificate : ""), text(mutual ? material.key : ""), boolean(false)}, 2);
    };
    const auto accept = [&](std::int64_t timeout) {
        return server.call("neura.net.tls.accept", {listener.value, text(material.certificate), text(material.key),
            tls_names({"h2"}), i64(timeout), i32(13), i32(13), text(material.certificate), boolean(false)}, 2);
    };
    {
        HostCompletion accepted;
        std::jthread worker([&] { accepted = accept(wait); });
        const auto connected = connect(13, true);
        worker.join();
        expect_ok(connected, "verified mutual TLS client");
        expect_ok(accepted, "verified mutual TLS server");
        check_equal(accepted.value.elements.at(1).bytes, std::string("h2"), "server negotiated ALPN");
        const auto socket = connected.value.elements.at(0);
        const auto peer = accepted.value.elements.at(0);
        const auto local = client.call("neura.net.tls.local_address", {socket});
        expect_ok(local, "TLS socket local address");
        check_equal(local.value.elements.at(0).bytes, std::string("127.0.0.1"), "numeric loopback address");
        expect_error(client.call("neura.net.tls.kernel_offload", {socket}), host_error::unsupported,
                     "kernel offload without handshake opt-in");
        expect_ok(client.call("neura.net.tls.shutdown_write", {socket, i64(wait)}), "send close_notify");
        const auto ended = server.call("neura.net.tls.read", {peer, i32(64), i64(wait)});
        expect_ok(ended, "orderly TLS shutdown");
        check(ended.value.bytes.empty(), "close_notify is an orderly end");
        expect_ok(server.call("neura.net.tls.close", {peer}), "server close");
        expect_ok(client.call("neura.net.tls.close", {socket}), "client close");
    }
    {
        HostCompletion accepted;
        std::jthread worker([&] { accepted = accept(wait); });
        const auto connected = connect(13, false);
        worker.join();
        expect_error(accepted, host_error::permission_denied, "required client certificate missing");
        if (connected.success) {
            const auto socket = connected.value.elements.at(0);
            expect_error(client.call("neura.net.tls.read", {socket, i32(64), i64(wait)}), host_error::connection_reset,
                         "client reads the fatal TLS alert");
            expect_ok(client.call("neura.net.tls.close", {socket}), "rejected client close");
        }
    }
    {
        HostCompletion accepted;
        std::jthread worker([&] { accepted = accept(wait); });
        const auto connected = connect(12, true);
        worker.join();
        expect_error(connected, host_error::connection_reset, "TLS 1.2 cannot connect to a TLS 1.3-only server");
        expect_error(accepted, host_error::connection_reset, "unsupported peer version");
    }
    expect_error(accept(30'000'000), host_error::timed_out, "accept waits for a peer up to the handshake deadline");
    expect_ok(server.call("neura.net.close", {listener.value}), "listener close");
    expect_error(connect(13, true), host_error::connection_refused, "closed port");
    expect_error(connect(11, true), host_error::invalid_input, "minimum version is TLS 1.2");
    auto only_net = everything();
    only_net.fs = false;
    Host restricted(only_net);
    expect_error(restricted.call("neura.net.tls.connect", {text("127.0.0.1"), port.value, i64(wait), text(""),
        tls_names({}), text(material.certificate)}), host_error::capability_denied, "explicit CA requires fs");
}

#endif

#endif

} // namespace
} // namespace neura::testing

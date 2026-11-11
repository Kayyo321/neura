// The standard host providers of neura run and neura test: argument checks,
// error codes, capability denial and path prefixes, partial I/O, ID limits,
// release of files and sockets when a run ends, and the trace file format
// with record and replay of library programs.

#include "backend/converter.hpp"
#include "cli/source_loader.hpp"
#include "fixtures.hpp"
#include "nvm/providers.hpp"
#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

    HostCompletion call(std::string_view name, std::vector<HostValue> arguments) {
        auto* service = registry.find(name, 1);
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
    expect_ok(host.call("neura.net.close", {i64(server)}), "close the server side");
    check(host.call("neura.net.read", {i64(client), i32(8), i64(0)}).value.bytes.empty(), "end of stream");
    HostCompletion failed = HostCompletion::ok();
    for (int i = 0; i < 100 && failed.success; ++i) {
        failed = host.call("neura.net.write", {i64(client), bytes("after close")});
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

#endif

} // namespace
} // namespace neura::testing

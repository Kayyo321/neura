#pragma once

#include "host.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura::nvm {

// Error codes of the standard host services (Stdlib.md section 4.5). The
// library maps 1 through 14 to the IoErrorKind values of the same order and
// every other code to Other.
namespace host_error {
inline constexpr std::uint32_t not_found = 1;
inline constexpr std::uint32_t permission_denied = 2;
inline constexpr std::uint32_t already_exists = 3;
inline constexpr std::uint32_t invalid_input = 4;
inline constexpr std::uint32_t interrupted = 5;
inline constexpr std::uint32_t unexpected_end = 6;
inline constexpr std::uint32_t not_a_directory = 7;
inline constexpr std::uint32_t is_a_directory = 8;
inline constexpr std::uint32_t directory_not_empty = 9;
inline constexpr std::uint32_t connection_refused = 10;
inline constexpr std::uint32_t connection_reset = 11;
inline constexpr std::uint32_t timed_out = 12;
inline constexpr std::uint32_t unsupported = 13;
inline constexpr std::uint32_t capability_denied = 14;
// The one code the providers use for every failure without a kind of its
// own, including an exhausted ID table.
inline constexpr std::uint32_t other = 100;

// The code for an operating-system error number.
std::uint32_t from_errno(int error);
} // namespace host_error

// At most this many file IDs and this many socket IDs are open at once in a
// run (Stdlib.md section 5.5).
inline constexpr std::size_t max_open_ids = 1024;
// The most bytes one read request returns, whatever maximum it asks for.
inline constexpr std::size_t max_read_bytes = std::size_t{1} << 20;
// The most bytes one entropy request may ask for.
inline constexpr std::int32_t max_entropy_bytes = 1 << 20;

// The capability groups a runner grants. The console group and process exit
// are always available; every other group is denied unless granted. A denied
// service still binds, and each call returns error 14.
struct Capabilities {
    bool fs = false;
    // With fs granted and this empty, every path is allowed. Otherwise a
    // path is allowed when its resolved absolute form equals or lies below
    // one of these prefixes (see resolve_path).
    std::vector<std::string> fs_prefixes;
    bool net = false;
    bool env = false;
    bool clock = false;
    bool entropy = false;
};

// Resolves `path` against the current directory to an absolute path whose
// existing components contain no symbolic links and no `.` or `..`. Links
// are followed as the operating system would follow them; once a component
// does not exist the rest is applied lexically. With `follow_last` false the
// final component is appended unresolved, for operations that act on a
// directory entry itself (remove, rename, make_directory). Returns the
// components, or an error code: invalid_input for an empty path or one with a
// NUL byte, other for more than 40 links.
struct ResolvedPath {
    std::vector<std::string> components;
    std::uint32_t error = 0;

    std::string text() const;
    bool within(const ResolvedPath& prefix) const;
};
ResolvedPath resolve_path(std::string_view path, bool follow_last);

// Source of console input.
class ConsoleSource {
public:
    virtual ~ConsoleSource() = default;
    // Reads at least one and at most `max` bytes, or none at the end of
    // input. On failure returns an error code instead.
    struct Read {
        std::string bytes;
        std::uint32_t error = 0;
    };
    virtual Read read(std::size_t max) = 0;
};

// Reads a file descriptor, normally standard input.
class DescriptorSource final : public ConsoleSource {
public:
    explicit DescriptorSource(int descriptor) : descriptor_(descriptor) {}
    Read read(std::size_t max) override;

private:
    int descriptor_;
};

// Serves fixed input, `chunk` bytes at most per read.
class StringSource final : public ConsoleSource {
public:
    explicit StringSource(std::string input, std::size_t chunk = 4096)
        : input_(std::move(input)), chunk_(chunk) {}
    Read read(std::size_t max) override;

private:
    std::string input_;
    std::size_t position_ = 0;
    std::size_t chunk_;
};

// A host service implemented by a function.
class FunctionService final : public HostService {
public:
    using Handler = std::function<HostCompletion(const HostRequest&)>;
    FunctionService(HostSignature signature, Handler handler)
        : signature_(std::move(signature)), handler_(std::move(handler)) {}
    const HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override { return handler_(request); }

private:
    HostSignature signature_;
    Handler handler_;
};

// The standard providers of neura run and neura test (Stdlib.md section 4.5):
//
//   neura.console.write(int32 stream, uint8[list] bytes) -> int32 count
//   neura.console.read(int32 max) -> uint8[list]
//   neura.fs.open(string path, int32 mode) -> int64 file
//   neura.fs.read(int64 file, int32 max) -> uint8[list]
//   neura.fs.write(int64 file, uint8[list] bytes) -> int32 count
//   neura.fs.seek(int64 file, int64 offset, int32 origin) -> int64 position
//   neura.fs.close(int64 file) -> void
//   neura.fs.stat(string path, bool follow_links) -> [int32 kind, int64 size, int64 seconds, int32 nanos]
//   neura.fs.list(string path) -> string[list]
//   neura.fs.make_directory(string path) -> void
//   neura.fs.remove(string path, bool directory) -> void
//   neura.fs.rename(string from, string to) -> void
//   neura.clock.wall() -> [int64 seconds, int32 nanos]
//   neura.clock.monotonic() -> int64 nanos
//   neura.clock.sleep(int64 nanos) -> void
//   neura.entropy.fill(int32 count) -> uint8[list]
//   neura.env.get(string name) -> [bool found, string value]
//   neura.process.exit(int32 status) -> void (terminal)
//   neura.net.connect(string host, int32 port, int64 timeout_nanos) -> int64 socket
//   neura.net.listen(string host, int32 port) -> int64 listener
//   neura.net.local_port(int64 socket) -> int32 port
//   neura.net.accept(int64 listener) -> int64 socket
//   neura.net.read(int64 socket, int32 max, int64 timeout_nanos) -> uint8[list]
//   neura.net.write(int64 socket, uint8[list] bytes) -> int32 count
//   neura.net.close(int64 socket) -> void
//
// All are version 1 with a uint32 error. IDs are opaque, numbered from 1 in
// each run, and never reused within a run. end_run() closes every file and
// socket the run left open and restarts the numbering; the destructor does
// the same.
class StandardServices {
public:
    StandardServices(ConsoleSink& output, ConsoleSink& error, ConsoleSource& input, Capabilities capabilities);
    ~StandardServices();
    StandardServices(const StandardServices&) = delete;
    StandardServices& operator=(const StandardServices&) = delete;

    // Adds every standard service except neura.console.write_line, which
    // has its own class.
    void register_all(HostRegistry& registry);
    void end_run();

    std::size_t open_files() const;
    std::size_t open_sockets() const;
    const Capabilities& capabilities() const { return capabilities_; }

    struct Tables;

private:
    void add(HostSignature signature, FunctionService::Handler handler);

    ConsoleSink& output_;
    ConsoleSink& error_;
    ConsoleSource& input_;
    Capabilities capabilities_;
    std::vector<ResolvedPath> prefixes_;
    std::unique_ptr<Tables> tables_;
    std::vector<std::unique_ptr<FunctionService>> services_;
};

// Writes the console output that a recorded console request produced, for a
// replay that answers requests without invoking the services: the line and
// LF of a successful neura.console.write_line, and the stored prefix of a
// neura.console.write.
void replay_console_output(const std::string& import, std::uint32_t version, const std::vector<HostValue>& arguments,
                           const HostCompletion& completion, ConsoleSink& output, ConsoleSink& error);

} // namespace neura::nvm

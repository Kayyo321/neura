#pragma once

#include "host.hpp"
#include "machine.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neura::nvm {

// Error codes of the standard host services (Stdlib.md section 4.5). The
// library maps 1 through 14 to the IoErrorKind values of the same order, 15
// to Cancelled, and every other code to Other.
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
// The answer of a cancellable overlapping request (version 2 of
// neura.net.connect, accept, read, and write, neura.console.read, and
// neura.clock.sleep) that was cancelled before it finished.
inline constexpr std::uint32_t cancelled = 15;
// The application provider codes (docs/design/Backend030.md, "Provider ABI").
// The library maps them to Other until IoErrorKind gains their names.
inline constexpr std::uint32_t too_large = 16;
inline constexpr std::uint32_t busy = 17;
inline constexpr std::uint32_t outcome_unknown = 18;
inline constexpr std::uint32_t pool_exhausted = 19;
inline constexpr std::uint32_t version_mismatch = 20;
inline constexpr std::uint32_t constraint_violation = 21;
inline constexpr std::uint32_t serialization = 22;
// The HTTP client services' own codes (docs/runtime/NVM.md, "HTTP client
// services"): a refused redirect from HTTPS to HTTP, a refused redirect that
// would resend the body to another origin, and an HTTP/2 request the peer
// stated it did not process. The library reports them as http.RedirectDenied
// and http.NotProcessed.
inline constexpr std::uint32_t redirect_downgrade = 23;
inline constexpr std::uint32_t redirect_body = 24;
inline constexpr std::uint32_t not_processed = 25;
// The one code the providers use for every failure without a kind of its
// own, including an exhausted ID table.
inline constexpr std::uint32_t other = 100;

// The code for an operating-system error number.
std::uint32_t from_errno(int error);
#ifdef _WIN32
// The code for a Win32 error (GetLastError(), a DWORD) and for a Winsock
// error (WSAGetLastError()), by the table of docs/runtime/NVM.md (W15). New
// Windows code maps through these, never through errno. Winsock codes are
// Win32 codes too: from_win32 passes the Winsock range (10000 to 11999) to
// from_wsa, and from_wsa passes every other value to from_win32.
std::uint32_t from_win32(unsigned long error);
std::uint32_t from_wsa(int error);
#endif
} // namespace host_error

// Whether a failed send() should be made again at once rather than reported.
// EINTR means a signal interrupted it. On Apple systems the kernel can also
// answer EPROTOTYPE for a stream whose peer is tearing the connection down,
// a transient state after which the same call reports EPIPE or ECONNRESET;
// libuv retries it for the same reason.
bool retry_send(int error);

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
//
// The library default is all-denied: an embedder grants each group
// explicitly. Only the command-line tools (neura run, neura test, and the
// policy that neura build --native records) grant the ordinary groups by
// default. Raw sockets and application groups require explicit grants.
struct Capabilities {
    bool fs = false;
    // With fs granted and this empty, every path is allowed. Otherwise a
    // path is allowed when its resolved absolute form equals or lies below
    // one of these prefixes (see resolve_path).
    std::vector<std::string> fs_prefixes;
    bool net = false;
    // The raw IP socket boundary also requires net.
    bool raw = false;
    bool env = false;
    bool clock = false;
    bool entropy = false;
    // Native libraries, memory, and calls (Std.DynamicForeign). Native code
    // runs outside Neura's memory-safety and determinism guarantees.
    bool native = false;
    // The application groups of docs/design/Apps030.md: the request a
    // server-host worker was assigned (neura.http.server.*), and the host's
    // configuration and log (neura.app.*). Only neura-server-host grants
    // them, to its request workers; the command-line tools never do.
    bool http_server = false;
    bool app = false;
    // The SQL pools of Std.Data.Sql (neura.sql.*): granted by
    // neura-server-host to its workers when it has pools, and by
    // `neura run` and `neura test` with --sql-pool.
    bool sql = false;
    // HTTP(S) client requests have their own grant, independent of sockets.
    bool http_client = false;
    // With an HTTP client grant, no list allows all origins and an empty
    // list allows none. The provider normalizes the configured origins.
    std::optional<std::vector<std::string>> http_client_origins;
};

// The target triple of this runner, such as x86_64-linux-gnu; an image's
// embedded native library must name the same one.
std::string native_target_triple();

// Resolves `path` against the current directory to an absolute path whose
// existing components contain no symbolic links and no `.` or `..`. Links
// are followed as the operating system would follow them; once a component
// does not exist the rest is applied lexically. With `follow_last` false the
// final component is appended unresolved, for operations that act on a
// directory entry itself (remove, rename, make_directory). Returns the
// components, or an error code: invalid_input for an empty path or one with a
// NUL byte, other for more than 40 links.
//
// On Windows (W09) '/' and '\' both separate components, and the first
// component is the root: a drive ("C:") or a UNC share ("//server/share").
// The path may be absolute on a drive, root-relative (on the current
// directory's drive or share), relative, or UNC text that starts with two
// separators. "." and ".." are applied lexically first, as Windows opens a
// path (GetFullPathNameW); then the longest prefix that exists is opened
// (following links, junctions and short names) and replaced by its final
// path (GetFinalPathNameByHandleW), and the rest is appended as given; with
// `follow_last` false the final component is appended unresolved. Refused
// with invalid_input: a drive-relative "X:name", a ':' inside a component
// (alternate data streams), a reserved device name (CON, PRN, AUX, NUL, COM1
// to COM9, LPT1 to LPT9, CONIN$, CONOUT$, with or without an extension or
// trailing spaces, and COM and LPT with a superscript digit, which Windows
// also treats as devices), a component that ends in a dot or a space, the
// characters <>"|?* and control characters, and the device namespaces
// "\\?\" and "\\.\". A link or junction that exists but whose target does
// not answers not_found, since its target cannot be resolved to check it
// against a prefix; a link loop answers other; a component that exists but
// cannot be opened answers the code of that failure (from_win32); a final
// name that is not valid UTF-16 answers unsupported. text() is the generic
// form ("C:/a/b", "C:/" for a drive root, "//server/share/a"), and within()
// compares components ordinally without case (CompareStringOrdinal).
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
    // The descriptor that read() takes its bytes from, or -1 when it has
    // none. When it is readable, read() does not block, so an overlapping
    // read can wait for it.
    virtual int descriptor() const { return -1; }
#ifdef _WIN32
    // The Win32 HANDLE that read() takes its bytes from (a console, a pipe or
    // a file), or nullptr when it has none. An overlapping read waits for a
    // console handle and polls a pipe (W17).
    virtual void* handle() const { return nullptr; }
    // Whether read() holds bytes it took from the handle and has not returned
    // yet, so the next read() returns them without blocking.
    virtual bool buffered() const { return false; }
#endif
};

// Reads a file descriptor, normally standard input.
//
// On Windows the descriptor is a C runtime descriptor whose handle is read
// (W17). A real console is read with ReadConsoleW and converted to UTF-8: a
// surrogate pair split between two reads is joined, and an unpaired
// surrogate becomes U+FFFD. In line-input mode (the console's default) a
// read returns once Enter is pressed, the whole line is taken from the
// console at once (what does not fit in `max` is returned by the next reads),
// its "\r\n" is delivered as "\n" as a POSIX terminal delivers it, and
// Ctrl+Z at the start of a line is the end of input (the rest of that line
// is discarded; a later read waits for more input, as after Ctrl+D on a
// POSIX terminal). Pipes and files are read with ReadFile, and a broken pipe
// is the end of input. A descriptor without a handle answers other (100), as
// a closed descriptor does on POSIX.
class DescriptorSource final : public ConsoleSource {
public:
    explicit DescriptorSource(int descriptor) : descriptor_(descriptor) {}
    Read read(std::size_t max) override;
#ifndef _WIN32
    int descriptor() const override { return descriptor_; }
#else
    void* handle() const override;
    bool buffered() const override { return !pending_.empty(); }
#endif

private:
    int descriptor_;
#ifdef _WIN32
    // Reads console input into pending_ until it holds at least one byte or
    // the end of input; returns an error code, or 0.
    std::uint32_t read_console(void* console, bool& end);

    // Console text read but not yet returned (UTF-8), and a high surrogate
    // that ended the last read and waits for its pair.
    std::string pending_;
    wchar_t high_surrogate_ = 0;
    // Whether the next console text starts a line (for Ctrl+Z).
    bool line_start_ = true;
#endif
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
    using Begin = std::function<std::variant<HostCompletion, PendingOperation>(const HostRequest&)>;
    FunctionService(HostSignature signature, Handler handler)
        : signature_(std::move(signature)), handler_(std::move(handler)) {}
    // An overlapping service: `handler` serves it when it is called without
    // tasks around (it may block), and `begin` starts it without blocking.
    FunctionService(HostSignature signature, Handler handler, Begin begin, bool cancellable)
        : signature_(std::move(signature)), handler_(std::move(handler)), begin_(std::move(begin)),
          cancellable_(cancellable) {}
    const HostSignature& signature() const override { return signature_; }
    HostCompletion invoke(const HostRequest& request) override { return handler_(request); }
    bool overlapping() const override { return static_cast<bool>(begin_); }
    bool cancellable() const override { return begin_ && cancellable_; }
    std::variant<HostCompletion, PendingOperation> begin(const HostRequest& request) override {
        return begin_ ? begin_(request) : HostService::begin(request);
    }

private:
    HostSignature signature_;
    Handler handler_;
    Begin begin_;
    bool cancellable_ = false;
};

// The standard providers of neura run and neura test (Stdlib.md section 4.5):
//
//   neura.console.write(int32 stream, uint8[list] bytes) -> int32 count
//   neura.console.read(int32 max) -> uint8[list]     (overlapping; version 2 cancellable)
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
//   neura.clock.sleep(int64 nanos) -> void            (overlapping; version 2 cancellable)
//   neura.entropy.fill(int32 count) -> uint8[list]
//   neura.env.get(string name) -> [bool found, string value]
//   neura.process.exit(int32 status) -> void (terminal)
//   neura.net.connect(string host, int32 port, int64 timeout_nanos) -> int64 socket
//                                                     (overlapping; version 2 cancellable)
//   neura.net.listen(string host, int32 port) -> int64 listener
//   neura.net.local_port(int64 socket) -> int32 port
//   neura.net.accept(int64 listener) -> int64 socket  (overlapping; version 2 cancellable)
//   neura.net.read(int64 socket, int32 max, int64 timeout_nanos) -> uint8[list]
//                                                     (overlapping; version 2 cancellable)
//   neura.net.write(int64 socket, uint8[list] bytes) -> int32 count
//                                                     (overlapping; version 2 cancellable)
//   neura.net.close(int64 socket) -> void
//   neura.ffi.abi() -> [string triple, uint8[list] layouts]
//   neura.ffi.arena.open() -> int64 arena
//   neura.ffi.arena.close(int64 arena) -> void
//   neura.ffi.alloc(int64 arena, int64 size, int64 alignment) -> int64 address
//   neura.ffi.library.open(int64 arena, string path) -> int64 library
//   neura.ffi.library.default() -> int64 library
//   neura.ffi.library.embedded() -> int64 library
//   neura.ffi.symbol(int64 library, string name) -> int64 address
//   neura.ffi.read(int64 address, int64 length) -> uint8[list]
//   neura.ffi.write(int64 address, uint8[list] bytes) -> void
//   neura.ffi.strlen(int64 address, int64 max) -> int64 length
//   neura.ffi.call(int64 function, uint8[list] signature, uint8[list] arguments) -> uint8[list] result
//
// All are version 1 with a uint32 error. The six marked services also exist
// as version 2, with the same signature and behavior, whose requests can be
// cancelled; in an image with tasks both versions overlap (Concurrency.md
// section 10.4), so a waiting task does not hold up the others. A cancelled
// request answers host_error::cancelled unless it finished first. IDs are opaque, numbered from 1 in
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
    // Virtual time (docs/lang/Concurrency.md section 14.4): when set, the
    // clock services read this clock instead of the host's. The monotonic
    // clock is its reading, the wall clock is its reading after
    // 2000-01-01T00:00:00Z, and a sleep that does not overlap moves it
    // instead of waiting. Null restores the host's clocks.
    void set_clock(TaskClock* clock) { clock_ = clock; }
    // The shared object from the image's NATV section, which
    // neura.ffi.library.embedded loads on first use. The services keep a
    // copy.
    void set_embedded_object(std::string object);

    struct Tables;

private:
    void add(HostSignature signature, FunctionService::Handler handler);
    // Adds version 1 of an overlapping service, then version 2, the
    // cancellable form of the same service.
    void add_overlapping(HostSignature signature, FunctionService::Handler handler, FunctionService::Begin begin);

    ConsoleSink& output_;
    ConsoleSink& error_;
    ConsoleSource& input_;
    Capabilities capabilities_;
    TaskClock* clock_ = nullptr;
    std::vector<ResolvedPath> prefixes_;
    std::unique_ptr<Tables> tables_;
    std::string embedded_object_;
    std::vector<std::unique_ptr<FunctionService>> services_;
};

// Writes the console output that a recorded console request produced, for a
// replay that answers requests without invoking the services: the line and
// LF of a successful neura.console.write_line, and the stored prefix of a
// neura.console.write.
void replay_console_output(const std::string& import, std::uint32_t version, const std::vector<HostValue>& arguments,
                           const HostCompletion& completion, ConsoleSink& output, ConsoleSink& error);

} // namespace neura::nvm

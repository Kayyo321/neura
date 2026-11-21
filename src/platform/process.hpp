#pragma once

// Child processes started from an argument vector, never through a shell
// (W10). Windows uses CreateProcessW with STARTUPINFOEXW: only the standard
// handles and the handles named in Spawn::inherit are inherited
// (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), and a kill-on-close job is attached at
// creation (PROC_THREAD_ATTRIBUTE_JOB_LIST), so no child runs outside it.
// POSIX uses fork and execve.

#include "console.hpp"
#include "file.hpp"
#include "self.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace neura::platform {

// How a process ended.
//
// Windows: an exit code of 0xC0000000 or above is an NTSTATUS and is reported
// as crashed; an exit code of 128 + N after this Process ended the child with
// terminate(N) or kill_tree() is reported as signaled N (a termination Neura
// performs uses exit code 128 + N, W10). POSIX: WIFSIGNALED is signaled;
// crashed does not occur.
struct ExitStatus {
    enum class Kind { exited, signaled, crashed };

    Kind kind = Kind::exited;
    // The exit code (exited), the signal number (signaled) or the NTSTATUS
    // (crashed).
    std::uint32_t value = 0;

    static ExitStatus exited(int code) noexcept { return {Kind::exited, static_cast<std::uint32_t>(code)}; }
    static ExitStatus signaled(int signal) noexcept { return {Kind::signaled, static_cast<std::uint32_t>(signal)}; }
    static ExitStatus crashed(std::uint32_t status) noexcept { return {Kind::crashed, status}; }

    bool success() const noexcept { return kind == Kind::exited && value == 0; }
    int code() const noexcept { return static_cast<int>(value); }

    // "exited N", "signal N" or "crashed 0xC0000005" (eight uppercase hex
    // digits).
    std::string describe() const;

    // The status a POSIX shell would report: the exit code, 128 + N for a
    // signal, and the NTSTATUS (as a 32-bit two's-complement int) for a crash,
    // which Windows passes through as the exit code.
    int shell_status() const noexcept;

    friend bool operator==(const ExitStatus&, const ExitStatus&) = default;
};

// A standard stream of a child.
struct Stdio {
    enum class Mode {
        inherit, // this process's own stream
        null,    // NUL or /dev/null
        pipe,    // a pipe whose other end Process keeps (stdin_pipe() ...)
        file,    // `file`, which the caller keeps owning
    };
    Mode mode = Mode::inherit;
    native_file file = invalid_file();

    static Stdio inherited() noexcept { return {}; }
    static Stdio null() noexcept { return {Mode::null, invalid_file()}; }
    static Stdio pipe() noexcept { return {Mode::pipe, invalid_file()}; }
    static Stdio from(native_file handle) noexcept { return {Mode::file, handle}; }
};

enum class JobMode {
    none,
    // Windows: a new job with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
    // JOB_OBJECT_LIMIT_BREAKAWAY_OK holds the child and every descendant that
    // does not break away; it ends them when Process is destroyed (or this
    // process ends). POSIX: the child leads a new process group, and
    // destroying a Process that still runs sends SIGKILL to the group.
    kill_on_close,
};

struct Spawn {
    // The file to run. It is not searched for on PATH. On Windows it must
    // name the file exactly, extension included. A relative path is resolved
    // against working_directory when that is set (on POSIX the child changes
    // directory first; Windows joins the two), else against this process's.
    std::filesystem::path program;
    // The full argument vector, argv[0] included, as UTF-8. Windows joins it
    // with command_line().
    std::vector<std::string> arguments;
    // The child's working directory; this process's when empty.
    std::filesystem::path working_directory;
    // The child's whole environment as name/value pairs (UTF-8); this
    // process's when absent. Windows sorts the block by upper-cased name as
    // CreateProcessW expects.
    std::optional<std::vector<std::pair<std::string, std::string>>> environment;

    Stdio input;
    Stdio output;
    Stdio error;

    // Further handles the child inherits, with the same values: Windows marks
    // them inheritable while any start that names them runs (counted across
    // threads, then restored) and lists them in
    // PROC_THREAD_ATTRIBUTE_HANDLE_LIST (sockets too); POSIX clears
    // FD_CLOEXEC on them in the child.
    std::vector<native_file> inherit;

    // Windows: CREATE_NEW_PROCESS_GROUP, so interrupt(ctrl_break) reaches the
    // child's group. POSIX: setpgid(0, 0).
    bool new_process_group = false;
    JobMode job = JobMode::none;
    // Windows: DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP (no console).
    // POSIX: setsid().
    bool detached = false;
    // Windows: CREATE_BREAKAWAY_FROM_JOB, to leave the job this process runs
    // in (it fails when that job does not allow breakaway). Ignored on POSIX.
    bool breakaway = false;
};

#ifdef _WIN32
using native_process = void*; // HANDLE
#else
using native_process = int; // pid_t
#endif

class Process {
public:
    Process() noexcept = default;
    Process(Process&& other) noexcept;
    Process& operator=(Process&& other) noexcept;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    // Closes the handles and, for JobMode::kill_on_close, ends the job's
    // processes. A POSIX child that was never waited for is left to be reaped
    // by the caller's SIGCHLD policy unless it was in a kill-on-close group.
    ~Process();

    // Starts a child. A failure sets `error` (the system's code; ENOENT or
    // ERROR_FILE_NOT_FOUND for a missing program) and returns an invalid
    // Process.
    static Process start(const Spawn& spawn, std::error_code& error);
    // Throws std::system_error on failure.
    static Process start(const Spawn& spawn);

    bool valid() const noexcept;
    explicit operator bool() const noexcept { return valid(); }
    process_id pid() const noexcept { return pid_; }
    // The process HANDLE on Windows, the pid on POSIX.
    native_process native_handle() const noexcept { return handle_; }
    // The job HANDLE (Windows, JobMode::kill_on_close), else nullptr.
    void* job() const noexcept { return job_; }

    // The parent ends of Stdio::pipe() streams; invalid otherwise. Close
    // stdin_pipe() to give the child end of input.
    UniqueFile& stdin_pipe() noexcept { return stdin_; }
    UniqueFile& stdout_pipe() noexcept { return stdout_; }
    UniqueFile& stderr_pipe() noexcept { return stderr_; }

    // Waits for the child to end, at most `timeout` when given. Returns
    // nullopt when the time ran out (or on an error, with `error` set). A
    // status, once returned, is returned again by later calls.
    std::optional<ExitStatus> wait(std::optional<std::chrono::milliseconds> timeout, std::error_code& error);
    std::optional<ExitStatus> wait(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    // Ends the child as signal `signal` would: kill(pid, signal) on POSIX,
    // TerminateProcess with exit code 128 + signal on Windows (W10).
    bool terminate(int signal = 15) noexcept;

    // Sends a console event. Windows: only ConsoleEvent::ctrl_break, through
    // GenerateConsoleCtrlEvent to the child's process group, which needs
    // Spawn::new_process_group and a shared console; anything else returns
    // false with ERROR_NOT_SUPPORTED (Windows cannot direct CTRL_C to one
    // group). POSIX: SIGINT (ctrl_c), SIGQUIT (ctrl_break) or SIGHUP (the
    // others), to the group when the child leads one.
    bool interrupt(ConsoleEvent event, std::error_code& error) noexcept;

    // Ends the child and its descendants: TerminateJobObject (exit code 137)
    // when there is a job, else TerminateProcess; SIGKILL to the process group
    // when the child leads one, else to the child.
    bool kill_tree() noexcept;

private:
    void close() noexcept;

    native_process handle_{};
    void* job_ = nullptr;
    process_id pid_ = 0;
    bool group_leader_ = false;
    bool kill_group_on_close_ = false;
    int terminated_by_ = 0; // the signal terminate()/kill_tree() used, 0 if none
    std::optional<ExitStatus> status_;
    UniqueFile stdin_;
    UniqueFile stdout_;
    UniqueFile stderr_;
};

// One argument quoted so that CommandLineToArgvW (and the MSVC CRT) read it
// back unchanged: unquoted when it is non-empty and has no space, tab,
// newline, vertical tab or '"'; otherwise in quotes, with each '"' and the
// backslashes before it or before the closing quote doubled as those rules
// require.
std::string quote_argument(std::string_view argument);

// The arguments joined by single spaces, each through quote_argument(). The
// first argument is quoted whenever it contains a space or tab even though
// Windows parses argv[0] by simpler rules, and must not contain '"'.
std::string command_line(const std::vector<std::string>& arguments);

struct Capture {
    bool started = false;
    bool timed_out = false;
    // A stream went over the cap; the child was ended with kill_tree().
    bool output_too_large = false;
    ExitStatus status;
    std::string out;
    std::string err;
    // Why the child could not be started or waited for.
    std::error_code error;
};

// Runs `spawn` with stdout and stderr captured (spawn.output and spawn.error
// are replaced by pipes; spawn.input is used as given, typically
// Stdio::null()), at most `timeout` when it is positive, with at most `cap`
// bytes kept per stream. A timeout or an overlong stream ends the child with
// kill_tree() (so use JobMode::kill_on_close to end its descendants too).
// Windows reads the pipes on two threads; POSIX polls them.
Capture run_capture(Spawn spawn, std::chrono::milliseconds timeout, std::size_t cap);

} // namespace neura::platform

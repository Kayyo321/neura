#include "process.hpp"

#include "internal.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <map>
#include <mutex>
#endif

#ifdef _WIN32
#include "windows.hpp"
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace fs = std::filesystem;

namespace neura::platform {

std::string ExitStatus::describe() const {
    switch (kind) {
    case Kind::exited: return "exited " + std::to_string(code());
    case Kind::signaled: return "signal " + std::to_string(value);
    case Kind::crashed: {
        char text[16];
        std::snprintf(text, sizeof text, "0x%08X", static_cast<unsigned>(value));
        return std::string("crashed ") + text;
    }
    }
    return "exited " + std::to_string(code());
}

int ExitStatus::shell_status() const noexcept {
    switch (kind) {
    case Kind::exited: return code();
    case Kind::signaled: return 128 + static_cast<int>(value);
    case Kind::crashed: return static_cast<int>(value);
    }
    return code();
}

std::string quote_argument(std::string_view argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        return std::string(argument);
    }
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char c : argument) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        // Backslashes are literal unless a quote follows them.
        if (c == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
        } else {
            quoted.append(backslashes, '\\');
        }
        quoted += c;
        backslashes = 0;
    }
    // Backslashes before the closing quote are doubled so they stay
    // literal.
    quoted.append(backslashes * 2, '\\');
    quoted += '"';
    return quoted;
}

std::string command_line(const std::vector<std::string>& arguments) {
    std::string line;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (index == 0) {
            // argv[0] runs to the next quote (when it starts with one) or to
            // the first space or tab; backslashes are always literal.
            if (argument.find('"') != std::string::npos) {
                throw std::invalid_argument("a program name cannot contain '\"'");
            }
            if (argument.empty() || argument.find_first_of(" \t") != std::string::npos) {
                line += '"' + argument + '"';
            } else {
                line += argument;
            }
            continue;
        }
        line += ' ';
        line += quote_argument(argument);
    }
    return line;
}

Process::Process(Process&& other) noexcept
    : handle_(other.handle_), job_(other.job_), pid_(other.pid_), group_leader_(other.group_leader_),
      kill_group_on_close_(other.kill_group_on_close_), terminated_by_(other.terminated_by_),
      status_(other.status_), stdin_(std::move(other.stdin_)), stdout_(std::move(other.stdout_)),
      stderr_(std::move(other.stderr_)) {
    other.handle_ = native_process{};
    other.job_ = nullptr;
    other.pid_ = 0;
    other.status_.reset();
}

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        job_ = other.job_;
        pid_ = other.pid_;
        group_leader_ = other.group_leader_;
        kill_group_on_close_ = other.kill_group_on_close_;
        terminated_by_ = other.terminated_by_;
        status_ = other.status_;
        stdin_ = std::move(other.stdin_);
        stdout_ = std::move(other.stdout_);
        stderr_ = std::move(other.stderr_);
        other.handle_ = native_process{};
        other.job_ = nullptr;
        other.pid_ = 0;
        other.status_.reset();
    }
    return *this;
}

Process::~Process() {
    close();
}

Process Process::start(const Spawn& spawn) {
    std::error_code error;
    Process process = start(spawn, error);
    if (error) throw std::system_error(error, "cannot start " + utf8_path(spawn.program));
    return process;
}

std::optional<ExitStatus> Process::wait(std::optional<std::chrono::milliseconds> timeout) {
    std::error_code error;
    auto status = wait(timeout, error);
    if (error) throw std::system_error(error, "cannot wait for process " + std::to_string(pid_));
    return status;
}

#ifdef _WIN32

namespace {

using detail::Handle;
using detail::last_win32_error;
using detail::win32_error;

// Exit codes at or above this are NTSTATUS values of a crash (W10).
constexpr DWORD crash_status = 0xC0000000;

bool make_inheritable(HANDLE source, Handle& copy, std::error_code& error) {
    HANDLE duplicate = nullptr;
    if (!::DuplicateHandle(::GetCurrentProcess(), source, ::GetCurrentProcess(), &duplicate, 0, TRUE,
                           DUPLICATE_SAME_ACCESS)) {
        error = last_win32_error();
        return false;
    }
    copy.reset(duplicate);
    return true;
}

// Makes the child's end of one standard stream. `child_reads` says which
// end of a pipe the child gets.
bool child_stream(const Stdio& stdio, DWORD standard, bool child_reads, Handle& child, UniqueFile& parent,
                  std::error_code& error) {
    switch (stdio.mode) {
    case Stdio::Mode::inherit: {
        const HANDLE own = ::GetStdHandle(standard);
        // A process without that stream gives its child none either.
        if (own == nullptr || own == INVALID_HANDLE_VALUE) return true;
        return make_inheritable(own, child, error);
    }
    case Stdio::Mode::null: {
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        child.reset(::CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                  OPEN_EXISTING, 0, nullptr));
        if (!child) {
            error = last_win32_error();
            return false;
        }
        return true;
    }
    case Stdio::Mode::pipe: {
        HANDLE read = nullptr;
        HANDLE write = nullptr;
        if (!::CreatePipe(&read, &write, nullptr, 0)) {
            error = last_win32_error();
            return false;
        }
        Handle read_end(read);
        Handle write_end(write);
        Handle& child_end = child_reads ? read_end : write_end;
        if (!::SetHandleInformation(child_end.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            error = last_win32_error();
            return false;
        }
        child = std::move(child_end);
        parent.reset(child_reads ? write_end.release() : read_end.release());
        return true;
    }
    case Stdio::Mode::file:
        if (stdio.file == nullptr || stdio.file == INVALID_HANDLE_VALUE) {
            error = win32_error(ERROR_INVALID_HANDLE);
            return false;
        }
        return make_inheritable(stdio.file, child, error);
    }
    return true;
}

// The environment block CreateProcessW takes with
// CREATE_UNICODE_ENVIRONMENT, sorted by upper-cased name as Windows keeps
// its own.
bool environment_block(const std::vector<std::pair<std::string, std::string>>& variables, std::wstring& block,
                       std::error_code& error) {
    std::vector<std::pair<std::wstring, std::wstring>> wide;
    wide.reserve(variables.size());
    for (const auto& [name, value] : variables) {
        auto wide_name = try_to_utf16(name);
        auto wide_value = try_to_utf16(value);
        if (!wide_name || !wide_value) {
            error = invalid_text_error();
            return false;
        }
        if (wide_name->empty() || wide_name->find(L'=', 1) != std::wstring::npos ||
            wide_name->find(L'\0') != std::wstring::npos || wide_value->find(L'\0') != std::wstring::npos) {
            error = win32_error(ERROR_INVALID_PARAMETER);
            return false;
        }
        wide.emplace_back(std::move(*wide_name), std::move(*wide_value));
    }
    std::stable_sort(wide.begin(), wide.end(), [](const auto& left, const auto& right) {
        return ::CompareStringOrdinal(left.first.data(), static_cast<int>(left.first.size()), right.first.data(),
                                      static_cast<int>(right.first.size()), TRUE) == CSTR_LESS_THAN;
    });
    block.clear();
    for (const auto& [name, value] : wide) {
        block += name;
        block += L'=';
        block += value;
        block += L'\0';
    }
    if (wide.empty()) block += L'\0';
    block += L'\0';
    return true;
}

std::wstring preferred(const fs::path& path) {
    std::wstring text = path.native();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    return text;
}

// A Spawn::inherit handle keeps its value in the child, so it is made
// inheritable in place rather than duplicated. Starts on several threads can
// name the same handle: the first marks it inheritable and the last to finish
// restores the flag it had, so no start sees the flag restored while it still
// needs it and none leaves it set.
struct InheritUse {
    std::size_t users = 0;
    DWORD flags = 0;
};

struct InheritRegistry {
    std::mutex mutex;
    std::map<HANDLE, InheritUse> uses;
};

InheritRegistry& inherit_registry() {
    static InheritRegistry registry;
    return registry;
}

// Holds a caller's handles inheritable for the duration of one start.
struct InheritGuard {
    std::vector<HANDLE> held;

    bool hold(HANDLE handle, std::error_code& error) {
        auto& registry = inherit_registry();
        const std::lock_guard lock(registry.mutex);
        auto& use = registry.uses[handle];
        if (use.users == 0) {
            DWORD flags = 0;
            if (!::GetHandleInformation(handle, &flags) ||
                !::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                error = last_win32_error();
                registry.uses.erase(handle);
                return false;
            }
            use.flags = flags;
        }
        ++use.users;
        held.push_back(handle);
        return true;
    }

    ~InheritGuard() {
        if (held.empty()) return;
        auto& registry = inherit_registry();
        const std::lock_guard lock(registry.mutex);
        for (const HANDLE handle : held) {
            const auto use = registry.uses.find(handle);
            if (use == registry.uses.end() || --use->second.users > 0) continue;
            ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, use->second.flags & HANDLE_FLAG_INHERIT);
            registry.uses.erase(use);
        }
    }
};

struct AttributeList {
    std::vector<std::byte> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    ~AttributeList() {
        if (list) ::DeleteProcThreadAttributeList(list);
    }
};

} // namespace

bool Process::valid() const noexcept {
    return handle_ != nullptr;
}

Process Process::start(const Spawn& spawn, std::error_code& error) {
    error.clear();
    std::wstring command;
    try {
        std::vector<std::string> arguments = spawn.arguments;
        if (arguments.empty()) arguments.push_back(utf8_path(spawn.program.filename()));
        command = to_utf16(command_line(arguments));
    } catch (const std::system_error& failure) {
        error = failure.code();
        return {};
    } catch (const std::invalid_argument&) {
        error = win32_error(ERROR_INVALID_PARAMETER);
        return {};
    }
    std::wstring block;
    if (spawn.environment && !environment_block(*spawn.environment, block, error)) return {};

    Process process;
    Handle child_in;
    Handle child_out;
    Handle child_err;
    if (!child_stream(spawn.input, STD_INPUT_HANDLE, true, child_in, process.stdin_, error) ||
        !child_stream(spawn.output, STD_OUTPUT_HANDLE, false, child_out, process.stdout_, error) ||
        !child_stream(spawn.error, STD_ERROR_HANDLE, false, child_err, process.stderr_, error)) {
        return {};
    }

    InheritGuard guard;
    std::vector<HANDLE> inherited;
    for (const Handle* stream : {&child_in, &child_out, &child_err}) {
        if (stream->valid()) inherited.push_back(stream->get());
    }
    for (const native_file handle : spawn.inherit) {
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
            error = win32_error(ERROR_INVALID_HANDLE);
            return {};
        }
        if (std::find(inherited.begin(), inherited.end(), handle) != inherited.end()) continue;
        if (!guard.hold(handle, error)) return {};
        inherited.push_back(handle);
    }

    Handle job;
    if (spawn.job == JobMode::kill_on_close) {
        job.reset(::CreateJobObjectW(nullptr, nullptr));
        if (!job) {
            error = last_win32_error();
            return {};
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        if (!::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
            error = last_win32_error();
            return {};
        }
    }

    // UpdateProcThreadAttribute keeps a pointer to each value, so the values
    // must outlive the list until CreateProcessW has read it.
    HANDLE job_handle = job.get();
    AttributeList attributes;
    const DWORD attribute_count = (inherited.empty() ? 0 : 1) + (job ? 1 : 0);
    if (attribute_count > 0) {
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, attribute_count, 0, &size);
        attributes.storage.resize(size);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.storage.data());
        if (!::InitializeProcThreadAttributeList(list, attribute_count, 0, &size)) {
            error = last_win32_error();
            return {};
        }
        attributes.list = list;
        if (!inherited.empty() &&
            !::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                                         inherited.size() * sizeof(HANDLE), nullptr, nullptr)) {
            error = last_win32_error();
            return {};
        }
        if (job && !::UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &job_handle,
                                                sizeof job_handle, nullptr, nullptr)) {
            error = last_win32_error();
            return {};
        }
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = child_in.get();
    startup.StartupInfo.hStdOutput = child_out.get();
    startup.StartupInfo.hStdError = child_err.get();
    startup.lpAttributeList = attributes.list;

    DWORD flags = CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    if (spawn.new_process_group || spawn.detached) flags |= CREATE_NEW_PROCESS_GROUP;
    if (spawn.detached) flags |= DETACHED_PROCESS;
    if (spawn.breakaway) flags |= CREATE_BREAKAWAY_FROM_JOB;

    // As on POSIX, where the child changes directory before execve, a
    // relative program is found in the working directory when one is given
    // (CreateProcessW alone would look in this process's).
    const bool in_directory = !spawn.program.has_root_path() && !spawn.working_directory.empty();
    const std::wstring program = preferred(in_directory ? spawn.working_directory / spawn.program : spawn.program);
    const std::wstring directory = preferred(spawn.working_directory);
    PROCESS_INFORMATION info{};
    if (!::CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, inherited.empty() ? FALSE : TRUE, flags,
                          spawn.environment ? block.data() : nullptr, directory.empty() ? nullptr : directory.c_str(),
                          &startup.StartupInfo, &info)) {
        error = last_win32_error();
        return {};
    }
    ::CloseHandle(info.hThread);
    process.handle_ = info.hProcess;
    process.pid_ = info.dwProcessId;
    process.job_ = job.release();
    process.group_leader_ = spawn.new_process_group || spawn.detached;
    return process;
}

void Process::close() noexcept {
    if (job_ != nullptr) ::CloseHandle(job_);
    if (handle_ != nullptr) ::CloseHandle(handle_);
    job_ = nullptr;
    handle_ = nullptr;
    stdin_.close();
    stdout_.close();
    stderr_.close();
}

std::optional<ExitStatus> Process::wait(std::optional<std::chrono::milliseconds> timeout, std::error_code& error) {
    error.clear();
    if (status_) return status_;
    if (!valid()) {
        error = win32_error(ERROR_INVALID_HANDLE);
        return std::nullopt;
    }
    DWORD milliseconds = INFINITE;
    if (timeout) {
        const auto count = std::max<std::chrono::milliseconds::rep>(timeout->count(), 0);
        milliseconds = static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(count, INFINITE - 1));
    }
    const DWORD result = ::WaitForSingleObject(handle_, milliseconds);
    if (result == WAIT_TIMEOUT) return std::nullopt;
    if (result != WAIT_OBJECT_0) {
        error = last_win32_error();
        return std::nullopt;
    }
    DWORD code = 0;
    if (!::GetExitCodeProcess(handle_, &code)) {
        error = last_win32_error();
        return std::nullopt;
    }
    if (terminated_by_ != 0 && code == static_cast<DWORD>(128 + terminated_by_)) {
        status_ = ExitStatus::signaled(terminated_by_);
    } else if (code >= crash_status) {
        status_ = ExitStatus::crashed(code);
    } else {
        status_ = ExitStatus::exited(static_cast<int>(code));
    }
    return status_;
}

bool Process::terminate(int signal) noexcept {
    if (!valid() || status_) return false;
    if (!::TerminateProcess(handle_, static_cast<UINT>(128 + signal))) return false;
    terminated_by_ = signal;
    return true;
}

bool Process::interrupt(ConsoleEvent event, std::error_code& error) noexcept {
    error.clear();
    if (!valid() || status_) {
        error = win32_error(ERROR_INVALID_HANDLE);
        return false;
    }
    if (event != ConsoleEvent::ctrl_break || !group_leader_) {
        error = win32_error(ERROR_NOT_SUPPORTED);
        return false;
    }
    if (!::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid_)) {
        error = last_win32_error();
        return false;
    }
    return true;
}

bool Process::kill_tree() noexcept {
    if (!valid()) return false;
    constexpr int kill_signal = 9;
    if (job_ != nullptr) {
        if (!::TerminateJobObject(job_, 128 + kill_signal)) return false;
        if (!status_) terminated_by_ = kill_signal;
        return true;
    }
    return terminate(kill_signal);
}

Capture run_capture(Spawn spawn, std::chrono::milliseconds timeout, std::size_t cap) {
    Capture capture;
    spawn.output = Stdio::pipe();
    spawn.error = Stdio::pipe();
    Process process = Process::start(spawn, capture.error);
    if (!process) return capture;
    capture.started = true;

    // Each pipe is drained on its own thread so a child that fills one pipe
    // while this process waits on the other cannot deadlock.
    struct Reader {
        HANDLE pipe;
        std::string* sink;
        Handle done;
        std::thread thread;
    };
    const auto event = [] { return Handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr)); };
    Handle too_large = event();
    Reader readers[2] = {{process.stdout_pipe().get(), &capture.out, event(), {}},
                         {process.stderr_pipe().get(), &capture.err, event(), {}}};
    if (!too_large || !readers[0].done || !readers[1].done) {
        capture.error = last_win32_error();
        process.kill_tree();
        std::error_code ignored;
        if (auto status = process.wait(std::nullopt, ignored)) capture.status = *status;
        return capture;
    }
    std::atomic<bool> over{false};
    std::atomic<bool> stop{false};
    for (auto& reader : readers) {
        reader.thread = std::thread([&reader, &over, &stop, cap, overflow = too_large.get()] {
            char buffer[4096];
            while (!stop) {
                std::error_code error;
                const auto got = read_some(reader.pipe, buffer, sizeof buffer, error);
                if (got <= 0) break;
                if (reader.sink->size() + static_cast<std::size_t>(got) > cap) {
                    over = true;
                    ::SetEvent(overflow);
                    break;
                }
                reader.sink->append(buffer, static_cast<std::size_t>(got));
            }
            ::SetEvent(reader.done.get());
        });
    }

    using Clock = std::chrono::steady_clock;
    const bool bounded = timeout.count() > 0;
    const auto deadline = Clock::now() + timeout;
    const auto remaining = [&]() -> DWORD {
        if (!bounded) return INFINITE;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return left <= 0 ? 0 : static_cast<DWORD>(std::min<long long>(left, INFINITE - 1));
    };
    // Wait for both pipes to reach end of input, then for the process. The
    // events are manual-reset, so only those not yet set are waited on.
    for (;;) {
        if (over) {
            capture.output_too_large = true;
            break;
        }
        HANDLE waits[3] = {too_large.get()};
        DWORD count = 1;
        for (auto& reader : readers) {
            if (::WaitForSingleObject(reader.done.get(), 0) == WAIT_TIMEOUT) waits[count++] = reader.done.get();
        }
        if (count == 1) {
            if (over) {
                capture.output_too_large = true;
                break;
            }
            const DWORD result = ::WaitForSingleObject(process.native_handle(), remaining());
            if (result == WAIT_TIMEOUT) capture.timed_out = true;
            break;
        }
        const DWORD result = ::WaitForMultipleObjects(count, waits, FALSE, remaining());
        if (result == WAIT_TIMEOUT) {
            capture.timed_out = true;
            break;
        }
        if (result == WAIT_FAILED) {
            capture.error = last_win32_error();
            break;
        }
    }
    if (capture.timed_out || capture.output_too_large || capture.error) process.kill_tree();
    // A descendant outside a job can keep a pipe open after the child ends;
    // its read is cancelled after a grace period. A cancel finds nothing to
    // cancel while the reader is between reads, so the reader is told to stop
    // and the cancel is repeated until it has.
    for (auto& reader : readers) {
        if (::WaitForSingleObject(reader.done.get(), 2000) == WAIT_TIMEOUT) {
            stop = true;
            do {
                ::CancelSynchronousIo(reader.thread.native_handle());
            } while (::WaitForSingleObject(reader.done.get(), 50) == WAIT_TIMEOUT);
        }
        reader.thread.join();
    }
    std::error_code wait_error;
    if (auto status = process.wait(std::nullopt, wait_error)) capture.status = *status;
    if (!capture.error) capture.error = wait_error;
    return capture;
}

#else // POSIX

namespace {

std::error_code errno_error(int code = errno) noexcept {
    return {code, std::system_category()};
}

bool cloexec_pipe(int ends[2]) noexcept {
#ifdef __linux__
    return ::pipe2(ends, O_CLOEXEC) == 0;
#else
    if (::pipe(ends) != 0) return false;
    ::fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}

// The child's end of one standard stream; left invalid, the child keeps
// this process's own.
bool child_stream(const Stdio& stdio, bool child_reads, UniqueFile& child, UniqueFile& parent,
                  std::error_code& error) {
    switch (stdio.mode) {
    case Stdio::Mode::inherit: return true;
    case Stdio::Mode::null: {
        const int null = ::open("/dev/null", O_RDWR | O_CLOEXEC);
        if (null < 0) {
            error = errno_error();
            return false;
        }
        child.reset(null);
        return true;
    }
    case Stdio::Mode::pipe: {
        int ends[2];
        if (!cloexec_pipe(ends)) {
            error = errno_error();
            return false;
        }
        child.reset(child_reads ? ends[0] : ends[1]);
        parent.reset(child_reads ? ends[1] : ends[0]);
        return true;
    }
    case Stdio::Mode::file:
        if (stdio.file < 0) {
            error = errno_error(EBADF);
            return false;
        }
        // Duplicated so this Process can close its copy like the others.
        child.reset(::fcntl(stdio.file, F_DUPFD_CLOEXEC, 0));
        if (!child) {
            error = errno_error();
            return false;
        }
        return true;
    }
    return true;
}

ExitStatus decode(int status) noexcept {
    if (WIFSIGNALED(status)) return ExitStatus::signaled(WTERMSIG(status));
    return ExitStatus::exited(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
}

} // namespace

bool Process::valid() const noexcept {
    return pid_ > 0;
}

Process Process::start(const Spawn& spawn, std::error_code& error) {
    error.clear();
    // Everything the child needs is prepared before fork: between fork and
    // exec only async-signal-safe calls are allowed.
    std::vector<std::string> arguments = spawn.arguments;
    if (arguments.empty()) arguments.push_back(spawn.program.filename().string());
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::vector<std::string> variables;
    std::vector<char*> envp;
    if (spawn.environment) {
        for (const auto& [name, value] : *spawn.environment) variables.push_back(name + "=" + value);
        for (auto& variable : variables) envp.push_back(variable.data());
        envp.push_back(nullptr);
    }
    const std::string program = spawn.program.native();
    const std::string directory = spawn.working_directory.native();
    const bool own_group = spawn.new_process_group || spawn.job == JobMode::kill_on_close;

    Process process;
    UniqueFile child[3];
    if (!child_stream(spawn.input, true, child[0], process.stdin_, error) ||
        !child_stream(spawn.output, false, child[1], process.stdout_, error) ||
        !child_stream(spawn.error, false, child[2], process.stderr_, error)) {
        return {};
    }
    int status_pipe[2];
    if (!cloexec_pipe(status_pipe)) {
        error = errno_error();
        return {};
    }
    UniqueFile status_read(status_pipe[0]);
    UniqueFile status_write(status_pipe[1]);

    const pid_t pid = ::fork();
    if (pid < 0) {
        error = errno_error();
        return {};
    }
    if (pid == 0) {
        const auto fail = [&](int code) {
            (void)!::write(status_write.get(), &code, sizeof code);
            ::_exit(127);
        };
        if (spawn.detached) {
            if (::setsid() < 0) fail(errno);
        } else if (own_group) {
            if (::setpgid(0, 0) != 0) fail(errno);
        }
        // Move the sources above 2 first so one dup2 cannot overwrite
        // another's source.
        int source[3] = {-1, -1, -1};
        for (int stream = 0; stream < 3; ++stream) {
            if (!child[stream]) continue;
            source[stream] = ::fcntl(child[stream].get(), F_DUPFD_CLOEXEC, 3);
            if (source[stream] < 0) fail(errno);
        }
        for (int stream = 0; stream < 3; ++stream) {
            if (source[stream] >= 0 && ::dup2(source[stream], stream) < 0) fail(errno);
        }
        for (const int descriptor : spawn.inherit) {
            const int flags = ::fcntl(descriptor, F_GETFD);
            if (flags < 0 || ::fcntl(descriptor, F_SETFD, flags & ~FD_CLOEXEC) != 0) fail(errno);
        }
        if (!directory.empty() && ::chdir(directory.c_str()) != 0) fail(errno);
        ::execve(program.c_str(), argv.data(), spawn.environment ? envp.data() : environ);
        fail(errno);
    }
    // Also set in the parent, so a signal sent to the group right after
    // start cannot miss it.
    if (own_group && !spawn.detached) ::setpgid(pid, pid);
    status_write.close();
    int child_error = 0;
    std::ptrdiff_t got = 0;
    do {
        got = ::read(status_read.get(), &child_error, sizeof child_error);
    } while (got < 0 && errno == EINTR);
    if (got == static_cast<std::ptrdiff_t>(sizeof child_error)) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        error = errno_error(child_error);
        return {};
    }
    process.handle_ = pid;
    process.pid_ = pid;
    process.group_leader_ = own_group || spawn.detached;
    process.kill_group_on_close_ = spawn.job == JobMode::kill_on_close;
    return process;
}

void Process::close() noexcept {
    if (valid() && !status_ && kill_group_on_close_) {
        ::kill(-pid_, SIGKILL);
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
        }
    }
    handle_ = 0;
    pid_ = 0;
    stdin_.close();
    stdout_.close();
    stderr_.close();
}

std::optional<ExitStatus> Process::wait(std::optional<std::chrono::milliseconds> timeout, std::error_code& error) {
    error.clear();
    if (status_) return status_;
    if (!valid()) {
        error = errno_error(ECHILD);
        return std::nullopt;
    }
    int status = 0;
    if (!timeout) {
        for (;;) {
            if (::waitpid(pid_, &status, 0) == pid_) break;
            if (errno == EINTR) continue;
            error = errno_error();
            return std::nullopt;
        }
        status_ = decode(status);
        return status_;
    }
    // No portable wait with a timeout: poll with a growing pause.
    const auto deadline = std::chrono::steady_clock::now() + *timeout;
    auto pause = std::chrono::milliseconds(1);
    for (;;) {
        const pid_t result = ::waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            status_ = decode(status);
            return status_;
        }
        if (result < 0 && errno != EINTR) {
            error = errno_error();
            return std::nullopt;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return std::nullopt;
        std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(pause, deadline - now));
        pause = std::min(pause * 2, std::chrono::milliseconds(20));
    }
}

bool Process::terminate(int signal) noexcept {
    if (!valid() || status_) return false;
    if (::kill(pid_, signal) != 0) return false;
    terminated_by_ = signal;
    return true;
}

bool Process::interrupt(ConsoleEvent event, std::error_code& error) noexcept {
    error.clear();
    if (!valid() || status_) {
        error = errno_error(ESRCH);
        return false;
    }
    int signal = SIGHUP;
    if (event == ConsoleEvent::ctrl_c) signal = SIGINT;
    if (event == ConsoleEvent::ctrl_break) signal = SIGQUIT;
    if (::kill(group_leader_ ? -pid_ : pid_, signal) != 0) {
        error = errno_error();
        return false;
    }
    return true;
}

bool Process::kill_tree() noexcept {
    if (!valid() || status_) return false;
    if (::kill(group_leader_ ? -pid_ : pid_, SIGKILL) != 0) return false;
    terminated_by_ = SIGKILL;
    return true;
}

Capture run_capture(Spawn spawn, std::chrono::milliseconds timeout, std::size_t cap) {
    Capture capture;
    spawn.output = Stdio::pipe();
    spawn.error = Stdio::pipe();
    Process process = Process::start(spawn, capture.error);
    if (!process) return capture;
    capture.started = true;
    UniqueFile* pipes[2] = {&process.stdout_pipe(), &process.stderr_pipe()};
    std::string* sinks[2] = {&capture.out, &capture.err};
    using Clock = std::chrono::steady_clock;
    const bool bounded = timeout.count() > 0;
    const auto deadline = Clock::now() + timeout;
    while (pipes[0]->valid() || pipes[1]->valid()) {
        pollfd polls[2];
        int which[2];
        nfds_t count = 0;
        for (int i = 0; i < 2; ++i) {
            if (!pipes[i]->valid()) continue;
            polls[count] = {pipes[i]->get(), POLLIN, 0};
            which[count++] = i;
        }
        int wait_ms = -1;
        if (bounded) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0) {
                capture.timed_out = true;
                break;
            }
            wait_ms = static_cast<int>(std::min<long long>(left, 3600 * 1000));
        }
        const int ready = ::poll(polls, count, wait_ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            capture.error = errno_error();
            break;
        }
        for (nfds_t i = 0; i < count; ++i) {
            if ((polls[i].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;
            char buffer[4096];
            std::error_code error;
            const auto got = read_some(polls[i].fd, buffer, sizeof buffer, error);
            auto& sink = *sinks[which[i]];
            if (got <= 0) {
                pipes[which[i]]->close();
            } else if (sink.size() + static_cast<std::size_t>(got) > cap) {
                capture.output_too_large = true;
                pipes[which[i]]->close();
            } else {
                sink.append(buffer, static_cast<std::size_t>(got));
            }
        }
        if (capture.output_too_large) break;
    }
    if (capture.timed_out || capture.output_too_large || capture.error) process.kill_tree();
    std::error_code wait_error;
    if (bounded && !capture.timed_out && !capture.output_too_large) {
        // The pipes closed; the child may still run.
        const auto left = std::max<Clock::duration>(deadline - Clock::now(), Clock::duration::zero());
        if (!process.wait(std::chrono::duration_cast<std::chrono::milliseconds>(left), wait_error) && !wait_error) {
            capture.timed_out = true;
            process.kill_tree();
        }
    }
    if (auto status = process.wait(std::nullopt, wait_error)) capture.status = *status;
    if (!capture.error) capture.error = wait_error;
    return capture;
}

#endif

} // namespace neura::platform

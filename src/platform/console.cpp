#include "console.hpp"

#ifdef _WIN32
#include "windows.hpp"

#include <algorithm>
#include <atomic>
#include <crtdbg.h>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace neura::platform {

#ifdef _WIN32

namespace {

HANDLE descriptor_handle(int fd) noexcept {
    return reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
}

bool is_console(HANDLE handle) noexcept {
    DWORD mode = 0;
    return handle != nullptr && handle != INVALID_HANDLE_VALUE && ::GetConsoleMode(handle, &mode) != 0;
}

// Writes UTF-8 to a console as UTF-16. There is no put area, so every write
// reaches xsputn or overflow, which take a lock: std::cout and std::cerr may
// be written from several threads. Each write goes to the console at once,
// as the C runtime's unbuffered console stream does, except for the bytes of
// a sequence that is not yet complete.
class ConsoleStreambuf final : public std::streambuf {
public:
    explicit ConsoleStreambuf(HANDLE console) : console_(console) {}

protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        const std::lock_guard lock(mutex_);
        pending_.append(data, static_cast<std::size_t>(size));
        write_complete();
        return size;
    }

    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        const std::lock_guard lock(mutex_);
        pending_ += traits_type::to_char_type(c);
        write_complete();
        return c;
    }

    int sync() override {
        return 0;
    }

private:
    // The length of the prefix of pending_ that ends on a sequence boundary.
    std::size_t complete_length() const noexcept {
        const std::size_t size = pending_.size();
        for (std::size_t back = 1; back <= 4 && back <= size; ++back) {
            const auto byte = static_cast<unsigned char>(pending_[size - back]);
            if ((byte & 0xC0u) == 0x80u) continue; // a continuation byte
            std::size_t length = 1;
            if (byte >= 0xF0) {
                length = 4;
            } else if (byte >= 0xE0) {
                length = 3;
            } else if (byte >= 0xC0) {
                length = 2;
            }
            return length > back ? size - back : size;
        }
        return size;
    }

    void write_complete() {
        const std::size_t length = complete_length();
        if (length == 0) return;
        // Not strict: an invalid byte is shown as U+FFFD instead of losing
        // the rest of the text.
        const int wide = ::MultiByteToWideChar(CP_UTF8, 0, pending_.data(), static_cast<int>(length), nullptr, 0);
        if (wide > 0) {
            buffer_.resize(static_cast<std::size_t>(wide));
            ::MultiByteToWideChar(CP_UTF8, 0, pending_.data(), static_cast<int>(length), buffer_.data(), wide);
            const wchar_t* next = buffer_.data();
            DWORD left = static_cast<DWORD>(wide);
            while (left > 0) {
                DWORD wrote = 0;
                if (!::WriteConsoleW(console_, next, std::min<DWORD>(left, 8192), &wrote, nullptr) || wrote == 0) {
                    break;
                }
                next += wrote;
                left -= wrote;
            }
        }
        pending_.erase(0, length);
    }

    HANDLE console_;
    std::mutex mutex_;
    std::string pending_;
    std::wstring buffer_;
};

std::atomic<ConsoleHandler> console_handler{nullptr};

BOOL WINAPI dispatch_console_event(DWORD type) {
    const ConsoleHandler handler = console_handler.load();
    if (handler == nullptr) return FALSE;
    ConsoleEvent event;
    switch (type) {
    case CTRL_C_EVENT: event = ConsoleEvent::ctrl_c; break;
    case CTRL_BREAK_EVENT: event = ConsoleEvent::ctrl_break; break;
    case CTRL_CLOSE_EVENT: event = ConsoleEvent::close; break;
    case CTRL_LOGOFF_EVENT: event = ConsoleEvent::logoff; break;
    case CTRL_SHUTDOWN_EVENT: event = ConsoleEvent::shutdown; break;
    default: return FALSE;
    }
    return handler(event) ? TRUE : FALSE;
}

bool ignore_event(ConsoleEvent event) {
    // The system ends a process once its handler returns from close, logoff
    // or shutdown. Returning would close the job of a waiting dispatcher and
    // kill the child before the child's own handler finishes, so this thread
    // waits instead: the process ends when its child does, or when the
    // system's timeout ends both.
    if (event == ConsoleEvent::close || event == ConsoleEvent::logoff || event == ConsoleEvent::shutdown) {
        ::Sleep(INFINITE);
    }
    return true;
}

} // namespace

void set_binary_stdio() noexcept {
    // A console in line-input mode delivers "\r\n", which only text mode
    // turns into "\n" (as a POSIX terminal delivers it), so console stdin
    // stays in text mode. Pipes and files are byte-exact.
    if (!is_terminal(0)) ::_setmode(0, _O_BINARY);
    for (const int fd : {1, 2}) ::_setmode(fd, _O_BINARY);
}

bool is_terminal(int fd) noexcept {
    return is_console(descriptor_handle(fd));
}

bool enable_terminal_colors(int fd) noexcept {
    const HANDLE handle = descriptor_handle(fd);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || !::GetConsoleMode(handle, &mode)) return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

std::optional<int> terminal_columns(int fd) noexcept {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!::GetConsoleScreenBufferInfo(descriptor_handle(fd), &info)) return std::nullopt;
    const int columns = info.srWindow.Right - info.srWindow.Left + 1;
    if (columns <= 0) return std::nullopt;
    return columns;
}

void install_console_streams() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Never freed: the streams flush at exit, after static destructors
        // could have run.
        const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (is_console(out)) std::cout.rdbuf(new ConsoleStreambuf(out));
        const HANDLE err = ::GetStdHandle(STD_ERROR_HANDLE);
        if (is_console(err)) {
            auto* buffer = new ConsoleStreambuf(err);
            std::cerr.rdbuf(buffer);
            std::clog.rdbuf(buffer);
        }
    });
}

void set_unattended_error_mode() noexcept {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    ::_set_abort_behavior(0, _CALL_REPORTFAULT);
#ifdef _DEBUG
    // The debug CRT's assertion and error reports would otherwise open a
    // dialog and wait.
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
}

void on_console_event(ConsoleHandler handler) noexcept {
    static std::once_flag once;
    console_handler.store(handler);
    std::call_once(once, [] { ::SetConsoleCtrlHandler(dispatch_console_event, TRUE); });
}

void ignore_console_events() noexcept {
    on_console_event(ignore_event);
}

#else // POSIX

void set_binary_stdio() noexcept {}

bool is_terminal(int fd) noexcept {
    return ::isatty(fd) == 1;
}

bool enable_terminal_colors(int fd) noexcept {
    return is_terminal(fd);
}

std::optional<int> terminal_columns(int fd) noexcept {
    winsize size{};
    if (::ioctl(fd, TIOCGWINSZ, &size) != 0 || size.ws_col == 0) return std::nullopt;
    return static_cast<int>(size.ws_col);
}

void install_console_streams() {}

void set_unattended_error_mode() noexcept {}

#endif

} // namespace neura::platform

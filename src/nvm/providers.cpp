#include "provider_internal.hpp"
#include "hpack_provider.hpp"
#include "http_fetch_provider.hpp"
#include "generation_internal.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <optional>
#include <thread>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <immintrin.h>
#elif defined(_M_X64) && defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#endif
#ifndef _WIN32
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <fcntl.h>
#include <linux/netlink.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/auxv.h>
#include <sys/random.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/random.h>
#elif defined(_WIN32)
#include "platform/environment.hpp"
#include "platform/file.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <bcrypt.h>
#include <io.h>
#include <mutex>
#include <winioctl.h>
#include <string_view>
#if defined(__MINGW32__) && defined(_UCRT)
// Some MinGW UCRT headers omit this declaration although libucrt exports it.
// Keep the thread-local UCRT API; a process-global CRT alias is not equivalent.
extern "C" _CRTIMP _invalid_parameter_handler __cdecl
_set_thread_local_invalid_parameter_handler(_invalid_parameter_handler);
#endif
#endif

namespace neura::nvm {

std::uint32_t host_error::from_errno(int error) {
    switch (error) {
    case ENOENT:
        return not_found;
    case EACCES:
    case EPERM:
    case EROFS:
        return permission_denied;
    case EEXIST:
    case EADDRINUSE:
        return already_exists;
    case EINVAL:
    case ENAMETOOLONG:
    case EADDRNOTAVAIL:
    case EAFNOSUPPORT:
        return invalid_input;
    case EINTR:
        return interrupted;
    case ENOTDIR:
        return not_a_directory;
    case EISDIR:
        return is_a_directory;
    case ENOTEMPTY:
        return directory_not_empty;
    case ECONNREFUSED:
        return connection_refused;
    case ECONNRESET:
    case ECONNABORTED:
    case EPIPE:
    case ENOTCONN:
        return connection_reset;
    case ETIMEDOUT:
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
        return timed_out;
    case ENOSYS:
    case EXDEV:
    case ESPIPE:
#ifdef ENOTSUP
    case ENOTSUP:
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    case EOPNOTSUPP:
#endif
        return unsupported;
    default:
        return other;
    }
}

#ifdef _WIN32
// The table of docs/runtime/NVM.md (W15).
std::uint32_t host_error::from_win32(unsigned long error) {
    if (error >= WSABASEERR && error < WSABASEERR + 2000) return from_wsa(static_cast<int>(error));
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_MOD_NOT_FOUND:
    case ERROR_PROC_NOT_FOUND:
        return not_found;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_WRITE_PROTECT:
    case ERROR_PRIVILEGE_NOT_HELD:
        return permission_denied;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return already_exists;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
    case ERROR_FILENAME_EXCED_RANGE:
    case ERROR_BAD_PATHNAME:
    case ERROR_NEGATIVE_SEEK:
    case ERROR_INVALID_HANDLE:
        return invalid_input;
    case ERROR_OPERATION_ABORTED:
        return interrupted;
    case ERROR_HANDLE_EOF:
        return unexpected_end;
    case ERROR_DIRECTORY:
        return not_a_directory;
    case ERROR_DIR_NOT_EMPTY:
        return directory_not_empty;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
        return connection_reset;
    case ERROR_TIMEOUT:
    case WAIT_TIMEOUT:
    case ERROR_SEM_TIMEOUT:
        return timed_out;
    case ERROR_NOT_SUPPORTED:
    case ERROR_CALL_NOT_IMPLEMENTED:
    case ERROR_NOT_SAME_DEVICE:
    // A name that is not valid UTF-16 (W07).
    case ERROR_NO_UNICODE_TRANSLATION:
        return unsupported;
    default:
        return other;
    }
}

std::uint32_t host_error::from_wsa(int error) {
    if (error < WSABASEERR || error >= WSABASEERR + 2000) return from_win32(static_cast<unsigned long>(error));
    switch (error) {
    case WSAECONNREFUSED:
        return connection_refused;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENETRESET:
    case WSAESHUTDOWN:
    case WSAENOTCONN:
    case WSAEDISCON:
        return connection_reset;
    // As EAGAIN is on POSIX.
    case WSAETIMEDOUT:
    case WSAEWOULDBLOCK:
    case WSATRY_AGAIN:
        return timed_out;
    case WSAEADDRINUSE:
        return already_exists;
    case WSAEADDRNOTAVAIL:
    case WSAEAFNOSUPPORT:
    case WSAEINVAL:
    case WSAEFAULT:
    case WSAENAMETOOLONG:
    case WSAENOTSOCK:
    case WSAEBADF:
        return invalid_input;
    case WSAEACCES:
        return permission_denied;
    case WSAEINTR:
        return interrupted;
    case WSAEOPNOTSUPP:
    case WSAEPROTONOSUPPORT:
    case WSAESOCKTNOSUPPORT:
    case WSAEPFNOSUPPORT:
    case WSAVERNOTSUPPORTED:
        return unsupported;
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA:
        return not_found;
    default:
        return other;
    }
}
#endif

bool retry_send(int error) {
#if defined(__APPLE__) && defined(EPROTOTYPE)
    if (error == EPROTOTYPE) return true;
#endif
    return error == EINTR;
}

namespace provider {

#ifdef _WIN32

namespace {

bool is_console(HANDLE handle) {
    DWORD mode = 0;
    return ::GetConsoleMode(handle, &mode) != 0;
}

// Whether a console input record starts input that a console read
// delivers: a key press other than a modifier or lock key, or the release of
// Alt that ends an Alt+numpad character. Key releases, focus, menu, mouse and
// buffer-size events are not delivered.
bool starts_input(const INPUT_RECORD& record) {
    if (record.EventType != KEY_EVENT) return false;
    const auto& key = record.Event.KeyEvent;
    if (!key.bKeyDown) return key.wVirtualKeyCode == VK_MENU && key.uChar.UnicodeChar != 0;
    switch (key.wVirtualKeyCode) {
    case VK_SHIFT:
    case VK_CONTROL:
    case VK_MENU:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
        return false;
    default:
        return true;
    }
}

// Whether a key press is pending in a console's input buffer. The records
// before it are removed, so that the handle, which is signaled while it holds
// any record, stops being signaled when nothing a read delivers is there.
bool console_input_pending(HANDLE console) {
    for (;;) {
        INPUT_RECORD records[64];
        DWORD count = 0;
        if (!::PeekConsoleInputW(console, records, 64, &count)) return true;
        DWORD skip = 0;
        while (skip < count && !starts_input(records[skip])) ++skip;
        if (skip > 0) {
            DWORD removed = 0;
            if (!::ReadConsoleInputW(console, records, skip, &removed)) return true;
        }
        if (skip < count) return true;
        if (count < 64) return false;
    }
}

} // namespace

bool readable_now(NativeFile descriptor) {
    const HANDLE handle = descriptor;
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return true;
    if (is_console(handle)) return console_input_pending(handle);
    if (::GetFileType(handle) == FILE_TYPE_PIPE) {
        // A broken pipe is the end of input, which a read returns at once;
        // another failure is reported by the read.
        DWORD available = 0;
        if (!::PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) return true;
        return available > 0;
    }
    return true;
}

bool readable_now(NativeSocket socket) {
    WSAPOLLFD entry{static_cast<SOCKET>(socket), POLLRDNORM, 0};
    return ::WSAPoll(&entry, 1, 0) != 0;
}

#else

bool readable_now(int descriptor) {
    pollfd entry{descriptor, POLLIN, 0};
    for (;;) {
        const int ready = ::poll(&entry, 1, 0);
        if (ready >= 0) return ready > 0;
        if (errno != EINTR) return false;
    }
}

#endif

int timeout_milliseconds(std::int64_t nanos) {
    if (nanos <= 0) return -1;
    const auto milliseconds = nanos / 1'000'000 + (nanos % 1'000'000 != 0 ? 1 : 0);
    return static_cast<int>(std::min<std::int64_t>(milliseconds, INT_MAX));
}

} // namespace provider

using namespace provider;

#ifdef _WIN32

// The first component is the root ("C:" or "//server/share").
std::string ResolvedPath::text() const {
    if (components.empty()) return {};
    std::string result = components.front();
    for (std::size_t i = 1; i < components.size(); ++i) {
        result += '/';
        result += components[i];
    }
    if (components.size() == 1) result += '/';
    return result;
}

bool ResolvedPath::within(const ResolvedPath& prefix) const {
    if (prefix.components.size() > components.size()) return false;
    for (std::size_t i = 0; i < prefix.components.size(); ++i) {
        // Both come from resolve_path, which gives valid UTF-8.
        const auto mine = platform::try_to_utf16(components[i]);
        const auto theirs = platform::try_to_utf16(prefix.components[i]);
        if (!mine || !theirs) return false;
        if (::CompareStringOrdinal(mine->data(), static_cast<int>(mine->size()), theirs->data(),
                                   static_cast<int>(theirs->size()), TRUE) != CSTR_EQUAL) {
            return false;
        }
    }
    return true;
}

#else

std::string ResolvedPath::text() const {
    if (components.empty()) return "/";
    std::string result;
    for (const auto& component : components) {
        result += '/';
        result += component;
    }
    return result;
}

bool ResolvedPath::within(const ResolvedPath& prefix) const {
    return prefix.components.size() <= components.size() &&
           std::equal(prefix.components.begin(), prefix.components.end(), components.begin());
}

#endif

namespace {

#ifdef _WIN32

bool is_separator(char c) { return c == '/' || c == '\\'; }
bool is_wide_separator(wchar_t c) { return c == L'/' || c == L'\\'; }

bool same_ascii_letters(std::string_view text, std::string_view upper) {
    if (text.size() != upper.size()) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i] >= 'a' && text[i] <= 'z' ? static_cast<char>(text[i] - 'a' + 'A') : text[i];
        if (c != upper[i]) return false;
    }
    return true;
}

// Whether Windows opens a device in place of a file of this name: the part
// before the first dot, without trailing spaces, is a reserved device name.
bool device_name(std::string_view component) {
    auto base = component.substr(0, component.find('.'));
    while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
    for (const std::string_view device : {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"}) {
        if (same_ascii_letters(base, device)) return true;
    }
    if (base.size() < 4) return false;
    const auto prefix = base.substr(0, 3);
    if (!same_ascii_letters(prefix, "COM") && !same_ascii_letters(prefix, "LPT")) return false;
    const auto number = base.substr(3);
    // 1 to 9, or a superscript one, two or three (U+00B9, U+00B2, U+00B3).
    return (number.size() == 1 && number[0] >= '1' && number[0] <= '9') || number == "\xC2\xB9" ||
           number == "\xC2\xB2" || number == "\xC2\xB3";
}

// The refusals of W09 for one component ("." and ".." pass).
bool refused_component(std::string_view component) {
    if (component == "." || component == "..") return false;
    for (const char c : component) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') {
            return true;
        }
    }
    if (component.back() == '.' || component.back() == ' ') return true;
    return device_name(component);
}

// A path split into its root and its components, as Windows API text.
struct WidePath {
    // "C:" or "\\server\share"; empty when the text has neither.
    std::wstring root;
    std::vector<std::wstring> components;

    // The path as API text: extended-length, so that it is neither limited to
    // MAX_PATH nor reinterpreted.
    std::wstring api(std::size_t count) const {
        std::wstring text = root.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + root.substr(2) : L"\\\\?\\" + root;
        for (std::size_t i = 0; i < count; ++i) {
            text += L'\\';
            text += components[i];
        }
        if (count == 0) text += L'\\';
        return text;
    }
};

// Splits an absolute path as GetFullPathNameW and GetFinalPathNameByHandleW
// give it: "C:\a", "\\server\share\a", or either with the "\\?\" prefix.
std::optional<WidePath> split_wide(std::wstring_view text) {
    WidePath result;
    std::wstring unc;
    if (text.starts_with(L"\\\\?\\UNC\\")) {
        unc = L"\\\\" + std::wstring(text.substr(8));
        text = unc;
    } else if (text.starts_with(L"\\\\?\\")) {
        text.remove_prefix(4);
    }
    std::size_t at = 0;
    if (text.size() >= 2 && text[1] == L':') {
        result.root = std::wstring(text.substr(0, 2));
        at = 2;
    } else if (text.size() >= 2 && is_wide_separator(text[0]) && is_wide_separator(text[1])) {
        std::wstring root = L"\\\\";
        at = 2;
        for (int part = 0; part < 2; ++part) {
            auto end = at;
            while (end < text.size() && !is_wide_separator(text[end])) ++end;
            if (end == at) return std::nullopt;
            if (part == 1) root += L'\\';
            root.append(text.substr(at, end - at));
            at = end;
            if (at < text.size()) ++at;
        }
        result.root = std::move(root);
    } else {
        return std::nullopt;
    }
    while (at < text.size()) {
        auto end = at;
        while (end < text.size() && !is_wide_separator(text[end])) ++end;
        if (end > at) result.components.emplace_back(text.substr(at, end - at));
        at = end + 1;
    }
    return result;
}

// Whether a failed open says that the path, or a directory on it, does not
// exist.
bool missing(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_NOT_READY:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_DIRECTORY:
    case ERROR_INVALID_NAME:
        return true;
    default:
        return false;
    }
}

// The final path of an open file or directory ("\\?\C:\..." or
// "\\?\UNC\..."), or the error.
std::variant<std::wstring, DWORD> final_path(HANDLE handle) {
    std::wstring text(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetFinalPathNameByHandleW(handle, text.data(), static_cast<DWORD>(text.size()),
                                                         FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (length == 0) return ::GetLastError();
        if (length < text.size()) {
            text.resize(length);
            return text;
        }
        text.resize(length);
    }
}

#else

void split_components(std::string_view text, std::deque<std::string>& out, bool at_front) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('/', start), text.size());
        const auto part = text.substr(start, end - start);
        if (!part.empty() && part != ".") parts.emplace_back(part);
        start = end + 1;
    }
    if (at_front) {
        out.insert(out.begin(), parts.begin(), parts.end());
    } else {
        out.insert(out.end(), parts.begin(), parts.end());
    }
}

#endif

} // namespace

#ifdef _WIN32
std::uint32_t provider::windows_path_error(std::string_view path) {
    if (path.empty() || path.find('\0') != std::string_view::npos) return host_error::invalid_input;
    std::size_t at = 0;
    if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1])) {
        // A UNC root, "\\server\share"; "\\?\" and "\\.\" name the device
        // namespaces and are refused.
        at = 2;
        for (int part = 0; part < 2; ++part) {
            auto end = at;
            while (end < path.size() && !is_separator(path[end])) ++end;
            const auto name = path.substr(at, end - at);
            if (name.empty() || name == "." || name == ".." || refused_component(name)) {
                return host_error::invalid_input;
            }
            at = end;
            if (at < path.size()) ++at;
        }
    } else if (path.size() >= 2 && path[1] == ':' &&
               ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
        // "X:name" and "X:" are relative to the current directory of drive X.
        if (path.size() == 2 || !is_separator(path[2])) return host_error::invalid_input;
        at = 3;
    }
    while (at < path.size()) {
        auto end = at;
        while (end < path.size() && !is_separator(path[end])) ++end;
        if (end > at && refused_component(path.substr(at, end - at))) return host_error::invalid_input;
        at = end + 1;
    }
    return 0;
}
#endif

ResolvedPath resolve_path(std::string_view path, bool follow_last) {
    ResolvedPath result;
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        result.error = host_error::invalid_input;
        return result;
    }
#ifdef _WIN32
    if (const auto refused = windows_path_error(path); refused != 0) {
        result.error = refused;
        return result;
    }
    const auto wide = platform::try_to_utf16(path);
    if (!wide) {
        result.error = host_error::invalid_input;
        return result;
    }
    // "." and "..", the current directory and the current drive, lexically.
    std::wstring full(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetFullPathNameW(wide->c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length == 0) {
            result.error = host_error::from_win32(::GetLastError());
            return result;
        }
        if (length < full.size()) {
            full.resize(length);
            break;
        }
        full.resize(length);
    }
    auto lexical = split_wide(full);
    if (!lexical) {
        result.error = host_error::invalid_input;
        return result;
    }
    std::optional<std::wstring> last;
    if (!follow_last && !lexical->components.empty()) {
        last = std::move(lexical->components.back());
        lexical->components.pop_back();
    }
    // The longest prefix that exists is replaced by its final path, which
    // follows links, junctions and short names; the rest cannot hold a link.
    WidePath resolved = *lexical;
    std::size_t existing = lexical->components.size();
    for (;;) {
        const auto api = lexical->api(existing);
        const platform::UniqueFile entry(::CreateFileW(api.c_str(), 0,
                                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                       nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (entry) {
            const auto final_name = final_path(entry.get());
            if (const auto* failure = std::get_if<DWORD>(&final_name)) {
                result.error = host_error::from_win32(*failure);
                return result;
            }
            auto canonical = split_wide(std::get<std::wstring>(final_name));
            if (!canonical) {
                result.error = host_error::other;
                return result;
            }
            resolved = std::move(*canonical);
            resolved.components.insert(resolved.components.end(), lexical->components.begin() + existing,
                                        lexical->components.end());
            break;
        }
        const DWORD failure = ::GetLastError();
        if (failure == ERROR_CANT_RESOLVE_FILENAME) {
            // A loop of links.
            result.error = host_error::other;
            return result;
        }
        if (!missing(failure)) {
            result.error = host_error::from_win32(failure);
            return result;
        }
        // The root of a missing drive or share: nothing to resolve.
        if (existing == 0) break;
        // An entry that exists although it cannot be followed is a link or
        // junction whose target does not: where it leads cannot be checked.
        if (::GetFileAttributesW(api.c_str()) != INVALID_FILE_ATTRIBUTES) {
            result.error = host_error::not_found;
            return result;
        }
        --existing;
    }
    if (last) resolved.components.push_back(std::move(*last));

    std::wstring root = resolved.root;
    if (root.starts_with(L"\\\\")) {
        root[0] = L'/';
        root[1] = L'/';
        std::replace(root.begin(), root.end(), L'\\', L'/');
    }
    const auto root_text = platform::try_to_utf8(root);
    if (!root_text) {
        result.error = host_error::unsupported;
        return result;
    }
    result.components.push_back(*root_text);
    for (const auto& component : resolved.components) {
        auto text = platform::try_to_utf8(component);
        if (!text) {
            result.components.clear();
            result.error = host_error::unsupported;
            return result;
        }
        result.components.push_back(std::move(*text));
    }
    return result;
#else
    std::deque<std::string> pending;
    if (path.front() != '/') {
        std::string directory(PATH_MAX, '\0');
        if (::getcwd(directory.data(), directory.size()) == nullptr) {
            result.error = host_error::from_errno(errno);
            return result;
        }
        directory.resize(std::char_traits<char>::length(directory.c_str()));
        std::deque<std::string> current;
        split_components(directory, current, false);
        result.components.assign(current.begin(), current.end());
    }
    split_components(path, pending, false);
    std::optional<std::string> last;
    if (!follow_last && !pending.empty() && pending.back() != "..") {
        last = pending.back();
        pending.pop_back();
    }
    int links = 0;
    bool exists = true;
    while (!pending.empty()) {
        auto component = std::move(pending.front());
        pending.pop_front();
        if (component == "..") {
            if (!result.components.empty()) result.components.pop_back();
            continue;
        }
        if (exists) {
            result.components.push_back(component);
            const auto candidate = result.text();
            struct stat info {};
            if (::lstat(candidate.c_str(), &info) != 0) {
                exists = false;
                continue;
            }
            if (!S_ISLNK(info.st_mode)) continue;
            if (++links > 40) {
                result.error = host_error::other;
                return result;
            }
            std::string target(static_cast<std::size_t>(info.st_size > 0 ? info.st_size : PATH_MAX) + 1, '\0');
            const auto length = ::readlink(candidate.c_str(), target.data(), target.size());
            if (length < 0) {
                result.error = host_error::from_errno(errno);
                return result;
            }
            target.resize(static_cast<std::size_t>(length));
            result.components.pop_back();
            if (!target.empty() && target.front() == '/') result.components.clear();
            split_components(target, pending, true);
            continue;
        }
        result.components.push_back(component);
    }
    if (last) result.components.push_back(*last);
    return result;
#endif
}

#ifdef _WIN32

namespace {

void ignore_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {}

} // namespace

void* DescriptorSource::handle() const {
    if (descriptor_ < 0) return nullptr;
    // A descriptor that is not open answers -1 instead of ending the process
    // through the C runtime's invalid-parameter handler.
    const auto previous = ::_set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
    const auto handle = ::_get_osfhandle(descriptor_);
    ::_set_thread_local_invalid_parameter_handler(previous);
    // -2: a standard descriptor of a process that has no such stream.
    if (handle == -1 || handle == -2 || handle == 0) return nullptr;
    return reinterpret_cast<void*>(handle);
}

std::uint32_t DescriptorSource::read_console(void* console, bool& end) {
    DWORD mode = 0;
    const bool lines = ::GetConsoleMode(console, &mode) && (mode & ENABLE_LINE_INPUT) != 0;
    constexpr DWORD chunk = 4096;
    std::wstring buffer(chunk, L'\0');
    std::wstring text;
    while (pending_.empty()) {
        text.clear();
        if (high_surrogate_ != 0) {
            text.push_back(high_surrogate_);
            high_surrogate_ = 0;
        }
        // In line-input mode the console returns one line, in pieces when it
        // is longer than the buffer. All of it is taken, so that no part of
        // it stays inside the console, where readiness cannot see it.
        for (;;) {
            DWORD got = 0;
            if (!::ReadConsoleW(console, buffer.data(), chunk, &got, nullptr)) {
                return host_error::from_win32(::GetLastError());
            }
            text.append(buffer.data(), got);
            if (!lines || got < chunk || buffer[got - 1] == L'\n') break;
        }
        if (lines && line_start_ && !text.empty() && text.front() == L'\x1a') {
            // Ctrl+Z at the start of a line ends the input; the rest of the
            // line goes with it.
            end = true;
            return 0;
        }
        if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) {
            high_surrogate_ = text.back();
            text.pop_back();
        }
        if (lines) {
            // A line ends in "\n", as a POSIX terminal delivers it.
            std::size_t kept = 0;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') continue;
                text[kept++] = text[i];
            }
            text.resize(kept);
        }
        if (text.empty()) continue;
        line_start_ = text.back() == L'\n';
        // Without WC_ERR_INVALID_CHARS an unpaired surrogate becomes U+FFFD.
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                               nullptr, nullptr);
        if (size <= 0) return host_error::from_win32(::GetLastError());
        const auto start = pending_.size();
        pending_.resize(start + static_cast<std::size_t>(size));
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), pending_.data() + start, size,
                              nullptr, nullptr);
    }
    return 0;
}

#endif

ConsoleSource::Read DescriptorSource::read(std::size_t max) {
    Read result;
#ifdef _WIN32
    if (pending_.empty()) {
        void* const source = handle();
        if (source == nullptr) {
            // As a closed descriptor (EBADF) answers on POSIX.
            result.error = host_error::other;
            return result;
        }
        if (is_console(source)) {
            bool end = false;
            result.error = read_console(source, end);
            if (result.error != 0 || end) return result;
        } else {
            std::string buffer(max, '\0');
            std::error_code error;
            const auto count = platform::read_some(source, buffer.data(), buffer.size(), error);
            if (count < 0) {
                result.error = host_error::from_win32(static_cast<unsigned long>(error.value()));
                return result;
            }
            buffer.resize(static_cast<std::size_t>(count));
            result.bytes = std::move(buffer);
            return result;
        }
    }
    const auto count = std::min(max, pending_.size());
    result.bytes = pending_.substr(0, count);
    pending_.erase(0, count);
#else
    std::string buffer(max, '\0');
    for (;;) {
        const auto count = ::read(descriptor_, buffer.data(), buffer.size());
        if (count >= 0) {
            buffer.resize(static_cast<std::size_t>(count));
            result.bytes = std::move(buffer);
            return result;
        }
        if (errno != EINTR) break;
    }
    result.error = host_error::from_errno(errno);
#endif
    return result;
}

ConsoleSource::Read StringSource::read(std::size_t max) {
    const auto count = std::min({max, chunk_, input_.size() - position_});
    Read result;
    result.bytes = input_.substr(position_, count);
    position_ += count;
    return result;
}

void replay_console_output(const std::string& import, std::uint32_t version, const std::vector<HostValue>& arguments,
                           const HostCompletion& completion, ConsoleSink& output, ConsoleSink& error) {
    if (version != 1 || !completion.success) return;
    if (import == "neura.console.write_line" && arguments.size() == 1) {
        output.write(arguments[0].bytes + "\n");
    } else if (import == "neura.console.write" && arguments.size() == 2) {
        const auto stream = as_int32(arguments[0]);
        const auto count = std::clamp<std::int64_t>(as_int32(completion.value), 0,
                                                     static_cast<std::int64_t>(arguments[1].bytes.size()));
        const auto bytes = std::string_view(arguments[1].bytes).substr(0, static_cast<std::size_t>(count));
        if (stream == 1) output.write(bytes);
        if (stream == 2) error.write(bytes);
    }
}

namespace {

// Fills `size` bytes from the operating system's cryptographic generator.
bool fill_entropy(char* data, std::size_t size) {
#if defined(__linux__)
    std::size_t filled = 0;
    while (filled < size) {
        const auto got = ::getrandom(data + filled, size - filled, 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        filled += static_cast<std::size_t>(got);
    }
    return true;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    for (std::size_t filled = 0; filled < size; filled += 256) {
        if (::getentropy(data + filled, std::min<std::size_t>(256, size - filled)) != 0) return false;
    }
    return true;
#elif defined(_WIN32)
    return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(data), static_cast<ULONG>(size),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    (void)data;
    return size == 0;
#endif
}


// Hardware random words for neura.entropy.hardware: RDSEED on x86-64
// (including Windows) and RNDR on AArch64 Linux. False when the processor has
// no such source or it keeps failing.
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((target("rdseed"))) bool hardware_word(std::uint64_t& word) {
    for (int attempt = 0; attempt < 1024; ++attempt) {
        unsigned long long value = 0;
        // The intrinsics, not GCC's builtin names, which clang (macOS) lacks.
        if (_rdseed64_step(&value)) {
            word = value;
            return true;
        }
        _mm_pause();
    }
    return false;
}
bool hardware_available() {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    return __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) && (ebx & (1u << 18)) != 0;
}
#elif defined(__aarch64__) && defined(__linux__)
bool hardware_word(std::uint64_t& word) {
    for (int attempt = 0; attempt < 1024; ++attempt) {
        std::uint64_t value = 0;
        int ok = 0;
        // RNDR sets NZCV to 0b0100 on failure; cset on ne reads success.
        asm volatile("mrs %0, s3_3_c2_c4_0\n\tcset %w1, ne" : "=r"(value), "=r"(ok) : : "cc");
        if (ok) {
            word = value;
            return true;
        }
    }
    return false;
}
bool hardware_available() { return (::getauxval(AT_HWCAP2) & HWCAP2_RNG) != 0; }
#elif defined(_M_X64) && defined(_MSC_VER)
// cl: the same instructions through its intrinsics, which need no target
// attribute.
bool hardware_word(std::uint64_t& word) {
    for (int attempt = 0; attempt < 1024; ++attempt) {
        unsigned __int64 value = 0;
        if (_rdseed64_step(&value)) {
            word = value;
            return true;
        }
        _mm_pause();
    }
    return false;
}
bool hardware_available() {
    int registers[4] = {};
    __cpuid(registers, 0);
    if (registers[0] < 7) return false;
    __cpuidex(registers, 7, 0);
    return (static_cast<unsigned>(registers[1]) & (1u << 18)) != 0;
}
#else
bool hardware_word(std::uint64_t&) { return false; }
bool hardware_available() { return false; }
#endif

#if defined(__linux__)
// Detector availability depends on the running guest, never on build headers.
// Serialize observations with fork so the child cannot inherit a held mutex.
struct VmGeneration {
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pid_t owner = ::getpid();
    std::uint64_t epoch = 0;
    std::optional<std::uint64_t> observed;
    int clock = -2; // -2: not probed; -1: unavailable for this process.
    int events = -2;
};
VmGeneration vm_generation;

void prepare_generation_fork() { ::pthread_mutex_lock(&vm_generation.mutex); }
void finish_generation_fork() { ::pthread_mutex_unlock(&vm_generation.mutex); }
void reset_generation_descriptors() {
    if (vm_generation.clock >= 0) ::close(vm_generation.clock);
    if (vm_generation.events >= 0) ::close(vm_generation.events);
    vm_generation.clock = vm_generation.events = -2;
}
void child_generation_fork() {
    vm_generation.owner = ::getpid();
    ++vm_generation.epoch;
    // A netlink queue shared with the parent would let one consume the other's
    // restore notification. Each child opens its own queue on first use.
    reset_generation_descriptors();
    finish_generation_fork();
}
[[maybe_unused]] const int vm_generation_fork_hooks =
    ::pthread_atfork(prepare_generation_fork, finish_generation_fork, child_generation_fork);

int vm_generation_socket() {
    namespace fs = std::filesystem;
    bool present = false;
    for (const char* driver : {"/sys/bus/acpi/drivers/vmgenid", "/sys/bus/platform/drivers/vmgenid"}) {
        std::error_code error;
        fs::directory_iterator entry(driver, error), end;
        while (!error && entry != end) {
            if (entry->path().filename() != "module" && entry->is_symlink(error)) present = true;
            entry.increment(error);
        }
    }
    if (!present) return -1;
    const int socket = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (socket < 0) return -1;
    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = 1;
    if (::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        ::close(socket);
        return -1;
    }
    return socket;
}

std::uint64_t vm_generation_events(int socket) {
    if (socket < 0) return 0;
    std::uint64_t count = 0;
    // Bounded even when unrelated uevents arrive continuously. Overflow or a
    // truncated kernel event conservatively triggers a reseed as well.
    for (int attempt = 0; attempt < 64; ++attempt) {
        char bytes[8192];
        sockaddr_nl sender{};
        iovec buffer{bytes, sizeof bytes};
        msghdr message{};
        message.msg_name = &sender;
        message.msg_namelen = sizeof sender;
        message.msg_iov = &buffer;
        message.msg_iovlen = 1;
        const auto got = ::recvmsg(socket, &message, MSG_DONTWAIT);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && errno == ENOBUFS) ++count;
        if (got <= 0) break;
        // Only the kernel (port ID zero), never another userspace sender.
        if (message.msg_namelen != sizeof sender || sender.nl_family != AF_NETLINK || sender.nl_pid != 0) continue;
        if ((message.msg_flags & MSG_TRUNC) != 0 ||
            generation_internal::vmgenid_event(std::string_view(bytes, static_cast<std::size_t>(got)))) ++count;
    }
    return count;
}

HostCompletion process_generation() {
    struct Lock {
        Lock() { prepare_generation_fork(); }
        ~Lock() { finish_generation_fork(); }
    } lock;
    // Keep this check even with atfork: syscall-level forks can bypass libc.
    const pid_t now = ::getpid();
    if (vm_generation.owner != now) {
        vm_generation.owner = now;
        ++vm_generation.epoch;
        reset_generation_descriptors();
    }
    if (vm_generation.events == -2) vm_generation.events = vm_generation_socket();
    vm_generation.epoch += vm_generation_events(vm_generation.events);
    if (vm_generation.clock == -2) {
        vm_generation.clock = ::open("/dev/vmclock0", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    }
    if (vm_generation.clock >= 0) {
        std::array<unsigned char, generation_internal::vmclock_size> snapshot{};
        // The kernel supplies a coherent snapshot with a bounded seqcount
        // retry. pread starts at zero on the cached descriptor every time.
        ssize_t size = ::pread(vm_generation.clock, snapshot.data(), snapshot.size(), 0);
        if (size < 0 && errno == EINTR) size = ::pread(vm_generation.clock, snapshot.data(), snapshot.size(), 0);
        if (size >= 0) {
            const auto observation = generation_internal::vmclock_generation(
                std::span(snapshot).first(static_cast<std::size_t>(size)));
            if (!observation.valid) return fail(host_error::other);
            if (observation.generation && vm_generation.observed != observation.generation) {
                vm_generation.observed = observation.generation;
                ++vm_generation.epoch;
            }
        } else if (errno == EACCES || errno == EPERM || errno == ENODEV || errno == ENOENT) {
            ::close(vm_generation.clock);
            vm_generation.clock = -1;
        } else {
            return fail(host_error::from_errno(errno));
        }
    }
    // Missing or inaccessible optional drivers leave the remaining detectors
    // active; they are not a failure of OS entropy or POSIX fork detection.
    return HostCompletion::ok(scalar(vm_generation.epoch));
}
#elif defined(_WIN32)
HostCompletion process_generation() {
    // vmgenerationcounter.h specifies this ABI. Older SDKs and some cross
    // toolchains omit the header, so use its documented control code and POD.
    // The IOCTL requires BOTH read and write access even in polling mode:
    // https://learn.microsoft.com/en-us/windows/win32/hyperv_v2/virtual-machine-generation-identifier
    constexpr DWORD read_generation = CTL_CODE(FILE_DEVICE_ACPI, 1, METHOD_BUFFERED,
                                              FILE_READ_ACCESS | FILE_WRITE_ACCESS);
    struct VmGenerationId {
        ULONGLONG low;
        ULONGLONG high;
    };
    static std::mutex mutex;
    static std::optional<VmGenerationId> previous;
    static std::uint64_t generation = 0;
    const std::lock_guard lock(mutex);
    const platform::UniqueFile device(::CreateFileW(L"\\\\.\\VmGenerationCounter",
        GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!device) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
            error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) {
            if (previous) {
                previous.reset();
                ++generation;
            }
            return HostCompletion::ok(scalar(generation));
        }
        return fail(host_error::from_win32(error));
    }
    VmGenerationId observed{};
    DWORD returned = 0;
    if (!::DeviceIoControl(device.get(), read_generation, nullptr, 0, &observed, sizeof observed, &returned, nullptr)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD) {
            if (previous) {
                previous.reset();
                ++generation;
            }
            return HostCompletion::ok(scalar(generation));
        }
        return fail(host_error::from_win32(error));
    }
    if (returned != sizeof observed) return fail(host_error::other);
    if (!previous || previous->low != observed.low || previous->high != observed.high) {
        previous = observed;
        ++generation;
    }
    return HostCompletion::ok(scalar(generation));
}
#else
HostCompletion process_generation() {
    static std::atomic<long> owner{static_cast<long>(::getpid())};
    static std::atomic<std::uint64_t> generation{0};
    const long now = static_cast<long>(::getpid());
    long seen = owner.load();
    if (seen != now && owner.compare_exchange_strong(seen, now)) generation.fetch_add(1);
    return HostCompletion::ok(scalar(generation.load()));
}
#endif

#ifdef _WIN32
// Sleeps below the system timer's default period (15.6 ms), which
// std::this_thread::sleep_for rounds up to, on a high-resolution waitable
// timer. False when the sleep is longer or the timer is not available; the
// caller then sleeps as usual.
bool sleep_precisely(std::int64_t nanos) {
    if (nanos <= 0 || nanos >= 16'000'000) return false;
    const platform::UniqueFile timer(
        ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    if (!timer) return false;
    LARGE_INTEGER due{};
    // Relative, in units of 100 ns, rounded up.
    due.QuadPart = -((nanos + 99) / 100);
    if (!::SetWaitableTimer(timer.get(), &due, 0, nullptr, nullptr, FALSE)) return false;
    return ::WaitForSingleObject(timer.get(), INFINITE) == WAIT_OBJECT_0;
}
#endif

} // namespace

StandardServices::StandardServices(ConsoleSink& output, ConsoleSink& error, ConsoleSource& input,
                                   Capabilities capabilities)
    : output_(output), error_(error), input_(input), capabilities_(std::move(capabilities)),
      tables_(std::make_unique<Tables>()) {
    for (const auto& prefix : capabilities_.fs_prefixes) {
        auto resolved = resolve_path(prefix, true);
        if (resolved.error != 0) {
            throw std::invalid_argument("cannot resolve the file system prefix '" + prefix + "'");
        }
        prefixes_.push_back(std::move(resolved));
    }

    // Console: always allowed.
    add(signature("neura.console.write", {int32(), byte_list()}, int32()), [this](const HostRequest& request) {
        const auto stream = as_int32(request.arguments.at(0));
        const auto& bytes = request.arguments.at(1).bytes;
        if (stream != 1 && stream != 2) return fail(host_error::invalid_input);
        if (bytes.size() > static_cast<std::size_t>(INT32_MAX)) return fail(host_error::invalid_input);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto written = (stream == 1 ? output_ : error_).write_some(bytes);
        if (written.written == 0 && written.failed) return fail(host_error::other);
        return HostCompletion::ok(int32_value(static_cast<std::int32_t>(written.written)));
    });
    const FunctionService::Handler read_input = [this](const HostRequest& request) {
        const auto max = as_int32(request.arguments.at(0));
        if (max < 1) return fail(host_error::invalid_input);
        auto read = input_.read(std::min(static_cast<std::size_t>(max), max_read_bytes));
        if (read.error != 0) return fail(read.error);
        return HostCompletion::ok(bytes_value(std::move(read.bytes)));
    };
    // Overlapping: input that is not a descriptor (in memory) completes at
    // once. A descriptor is waited for, and a read that finds it readable
    // does not block.
    add_overlapping(signature("neura.console.read", {int32()}, byte_list()), read_input,
                    [this, read_input](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        const auto max = as_int32(request.arguments.at(0));
#ifdef _WIN32
        // A console handle is waited for: it is signaled while it holds input
        // records, and readable_now drops those a read would not deliver. A
        // pipe cannot be waited for and is asked at each check (W17). Bytes
        // the source already holds are returned at once.
        void* const handle = input_.handle();
        if (max < 1 || handle == nullptr || input_.buffered()) return read_input(request);
        PendingOperation operation;
        if (is_console(handle)) operation.wait_source = WindowsWait{handle};
        const auto count = std::min(static_cast<std::size_t>(max), max_read_bytes);
        operation.attempt = [this, handle, count](bool cancelled) -> std::optional<HostCompletion> {
            if (!input_.buffered() && !readable_now(handle)) {
                if (cancelled) return cancelled_answer();
                return std::nullopt;
            }
            auto read = input_.read(count);
            if (read.error != 0) return fail(read.error);
            return HostCompletion::ok(bytes_value(std::move(read.bytes)));
        };
        return operation;
#else
        const auto descriptor = input_.descriptor();
        if (max < 1 || descriptor < 0) return read_input(request);
        PendingOperation operation;
        operation.wait_source = PosixWait{{descriptor, PosixWait::Events::Read}};
        const auto count = std::min(static_cast<std::size_t>(max), max_read_bytes);
        operation.attempt = [this, descriptor, count](bool cancelled) -> std::optional<HostCompletion> {
            if (!readable_now(descriptor)) {
                if (cancelled) return cancelled_answer();
                return std::nullopt;
            }
            auto read = input_.read(count);
            if (read.error != 0) return fail(read.error);
            return HostCompletion::ok(bytes_value(std::move(read.bytes)));
        };
        return operation;
#endif
    });

    // Process exit: not an external capability, so always allowed.
    add(signature("neura.process.exit", {int32()}, void_type()), [](const HostRequest& request) {
        const auto status = as_int32(request.arguments.at(0));
        if (status < 0 || status > 255) return fail(host_error::invalid_input);
        return HostCompletion::terminate(status);
    });

    add(signature("neura.env.get", {text()}, tuple({boolean(), text()})), [this](const HostRequest& request) {
        if (!capabilities_.env) return denied();
        const auto& name = request.arguments.at(0).bytes;
        if (!usable_text(name) || name.find('=') != std::string::npos) return fail(host_error::invalid_input);
#ifdef _WIN32
        // The UTF-16 environment (W18): names match without case, and a
        // value that is not valid UTF-16 answers unsupported.
        std::error_code error;
        auto value = platform::get_env(name, error);
        if (error) return fail(host_error::unsupported);
        if (!value) return HostCompletion::ok(tuple_value({scalar(0), bytes_value({})}));
        std::string text = std::move(*value);
#else
        const char* value = std::getenv(name.c_str());
        if (value == nullptr) return HostCompletion::ok(tuple_value({scalar(0), bytes_value({})}));
        std::string text(value);
#endif
        if (!nvr::valid_utf8(text)) return fail(host_error::unsupported);
        return HostCompletion::ok(tuple_value({scalar(1), bytes_value(std::move(text))}));
    });

    add(signature("neura.clock.wall", {}, tuple({int64(), int32()})), [this](const HostRequest&) {
        if (!capabilities_.clock) return denied();
        // Under virtual time the wall clock starts at 2000-01-01T00:00:00Z.
        constexpr std::int64_t virtual_epoch = 946'684'800LL * 1'000'000'000LL;
        const auto now = clock_ ? virtual_epoch + clock_->now()
                                : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
        auto seconds = now / 1'000'000'000;
        auto nanos = now % 1'000'000'000;
        if (nanos < 0) {
            nanos += 1'000'000'000;
            seconds -= 1;
        }
        return HostCompletion::ok(
            tuple_value({int64_value(seconds), int32_value(static_cast<std::int32_t>(nanos))}));
    });
    add(signature("neura.clock.monotonic", {}, int64()), [this](const HostRequest&) {
        if (!capabilities_.clock) return denied();
        const auto now = clock_ ? clock_->now()
                                : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
        return HostCompletion::ok(int64_value(now));
    });
    const FunctionService::Handler sleep = [this](const HostRequest& request) {
        if (!capabilities_.clock) return denied();
        const auto nanos = as_int64(request.arguments.at(0));
        if (nanos < 0) return fail(host_error::invalid_input);
        if (clock_) {
            clock_->wait_until(deadline_after(clock_->now(), nanos));
        } else {
#ifdef _WIN32
            if (!sleep_precisely(nanos)) std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
#else
            std::this_thread::sleep_for(std::chrono::nanoseconds(nanos));
#endif
        }
        return HostCompletion::ok();
    };
    // Overlapping: a deadline on the task clock; no thread sleeps.
    add_overlapping(signature("neura.clock.sleep", {int64()}, void_type()), sleep,
                    [this](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        if (!capabilities_.clock) return denied();
        const auto nanos = as_int64(request.arguments.at(0));
        if (nanos < 0) return fail(host_error::invalid_input);
        if (nanos == 0) return HostCompletion::ok();
        PendingOperation operation;
        operation.deadline = deadline_after(request.now, nanos);
        operation.attempt = [](bool cancelled) -> std::optional<HostCompletion> {
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        operation.expire = [] { return HostCompletion::ok(); };
        return operation;
    });

    // RDSEED or RNDR words; Unsupported (13) without a hardware source,
    // Other (100) when the source keeps failing.
    add(signature("neura.entropy.hardware", {int32()}, byte_list()), [this](const HostRequest& request) {
        if (!capabilities_.entropy) return denied();
        const auto count = as_int32(request.arguments.at(0));
        if (count < 0 || count > max_entropy_bytes) return fail(host_error::invalid_input);
        if (count > 0 && !hardware_available()) return fail(host_error::unsupported);
        std::string bytes;
        bytes.reserve(static_cast<std::size_t>(count) + 8);
        while (bytes.size() < static_cast<std::size_t>(count)) {
            std::uint64_t word = 0;
            if (!hardware_word(word)) return fail(host_error::other);
            for (int i = 0; i < 8; ++i) bytes += static_cast<char>(word >> (8 * i));
        }
        bytes.resize(static_cast<std::size_t>(count));
        return HostCompletion::ok(bytes_value(std::move(bytes)));
    });

    add(signature("neura.process.generation", {}, HostType(Kind::Unsigned, 64)), [this](const HostRequest&) {
        if (!capabilities_.entropy) return denied();
        return process_generation();
    });

    add(signature("neura.entropy.fill", {int32()}, byte_list()), [this](const HostRequest& request) {
        if (!capabilities_.entropy) return denied();
        const auto count = as_int32(request.arguments.at(0));
        if (count < 0 || count > max_entropy_bytes) return fail(host_error::invalid_input);
        std::string bytes(static_cast<std::size_t>(count), '\0');
        // Only the operating system's cryptographic generator: getrandom on
        // Linux, getentropy on macOS and the BSDs, BCryptGenRandom on
        // Windows. When it fails the request fails with code 100; there is
        // no weaker fallback.
        if (!fill_entropy(bytes.data(), bytes.size())) return fail(host_error::other);
        return HostCompletion::ok(bytes_value(std::move(bytes)));
    });

    // The ui and history groups are served by a browser page (docs/runtime/Providers.md, "UI services"). A native
    // run binds them, so that an image that mounts a component loads, and answers every request with error 14.
    const auto deny = [](const HostRequest&) { return denied(); };
    add(signature("neura.ui.root.open", {text()}, int64()), deny);
    add(signature("neura.ui.root.close", {int64()}, void_type()), deny);
    add(signature("neura.ui.template", {int64(), text()}, int64()), deny);
    add(signature("neura.ui.commit", {int64(), byte_list()}, void_type()), deny);
    add(signature("neura.ui.event.next", {int64(), int32()}, byte_list()), deny);
    add(signature("neura.ui.stats", {int64()}, tuple({int64(), int64(), int64(), int64()})), deny);
    add(signature("neura.ui.focus", {int64(), text()}, void_type()), deny);
    add(signature("neura.history.current", {}, text()), deny);
    add(signature("neura.history.push", {text()}, text()), deny);
    add(signature("neura.history.replace", {text()}, text()), deny);
    add(signature("neura.history.next", {}, text()), deny);
    add(signature("neura.history.link", {text()}, text()), deny);

    const Adder adder = [this](HostSignature signature, FunctionService::Handler handler) {
        add(std::move(signature), std::move(handler));
    };
    add_fs_services(adder, *tables_, capabilities_, prefixes_);
    const OverlapAdder overlap = [this](HostSignature signature, FunctionService::Handler handler,
                                        FunctionService::Begin begin) {
        add_overlapping(std::move(signature), std::move(handler), std::move(begin));
    };
    add_net_services(adder, overlap, *tables_, capabilities_);
    add_socket_services(adder, overlap, *tables_, capabilities_);
    add_tls_services(adder, *tables_, capabilities_, prefixes_);
    add_doq_services(adder, capabilities_, prefixes_);
    const OverlapAdder fetch_adder = [this](HostSignature signature, FunctionService::Handler handler,
                                          FunctionService::Begin begin) {
        services_.push_back(std::make_unique<FunctionService>(std::move(signature), std::move(handler),
                                                            std::move(begin), true));
    };
    add_http_fetch_services(fetch_adder, capabilities_);
    add_hpack_services(adder);
    add_ffi_services(adder, *tables_, capabilities_);
}

void StandardServices::set_embedded_object(std::string object) {
    embedded_object_ = std::move(object);
    tables_->native.embedded_object = &embedded_object_;
}

StandardServices::~StandardServices() { end_run(); }

void StandardServices::add(HostSignature signature, FunctionService::Handler handler) {
    services_.push_back(std::make_unique<FunctionService>(std::move(signature), std::move(handler)));
}

void StandardServices::add_overlapping(HostSignature signature, FunctionService::Handler handler,
                                       FunctionService::Begin begin) {
    if (signature.version == 2) {
        services_.push_back(std::make_unique<FunctionService>(std::move(signature), std::move(handler),
                                                               std::move(begin), true));
        return;
    }
    auto cancellable = signature;
    cancellable.version = 2;
    services_.push_back(std::make_unique<FunctionService>(std::move(signature), handler, begin, false));
    services_.push_back(
        std::make_unique<FunctionService>(std::move(cancellable), std::move(handler), std::move(begin), true));
}

void StandardServices::register_all(HostRegistry& registry) {
    for (auto& service : services_) registry.add(*service);
}

void StandardServices::end_run() {
    close_files(*tables_);
    close_tls(*tables_);
    close_sockets(*tables_);
    close_native(*tables_);
}

std::size_t StandardServices::open_files() const { return tables_->files.size(); }
std::size_t StandardServices::open_sockets() const { return tables_->sockets.size(); }

} // namespace neura::nvm

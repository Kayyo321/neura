#include "system.hpp"

#include "cli/install_layout.hpp"
#include "cli/tool.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include "platform/dirs.hpp"
#include "platform/environment.hpp"
#include "platform/error.hpp"
#include "platform/file.hpp"
#include "platform/socket.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <bcrypt.h>
#include <sddl.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__linux__)
#include <link.h>
#include <sys/random.h>
#endif
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace neura::service {

#ifdef _WIN32

namespace {

HANDLE handle_of(Descriptor descriptor) { return reinterpret_cast<HANDLE>(descriptor); }

std::string system_message(DWORD code) { return platform::error_message(static_cast<int>(code)); }

std::string socket_message() { return platform::last_socket_error().message(); }

std::string text_of(const fs::path& path) { return platform::utf8_path(path); }

} // namespace

void UniqueFd::reset(Descriptor fd) {
    if (fd_ != invalid_descriptor && fd_ != 0) {
        if (kind_ == Kind::socket) {
            ::closesocket(static_cast<SOCKET>(fd_));
        } else {
            ::CloseHandle(handle_of(fd_));
        }
    }
    fd_ = fd;
}

void set_cloexec(Descriptor fd) { ::SetHandleInformation(handle_of(fd), HANDLE_FLAG_INHERIT, 0); }

void set_nonblocking(Descriptor fd, bool nonblocking) {
    platform::set_nonblocking(static_cast<platform::native_socket>(fd), nonblocking);
}

// --- Messages ----------------------------------------------------------------

namespace {

// The handles of a whole message: the values of its handles_tag fields,
// owned from here on, so that they are closed whether or not the message
// decodes. The bounds are checked again by decode().
std::vector<UniqueFd> take_handles(std::string_view bytes) {
    std::vector<UniqueFd> handles;
    std::size_t at = header_bytes;
    while (at + 6 <= bytes.size()) {
        std::uint16_t tag = 0;
        std::uint32_t length = 0;
        std::memcpy(&tag, bytes.data() + at, 2);
        std::memcpy(&length, bytes.data() + at + 2, 4);
        at += 6;
        if (length > bytes.size() - at) break;
        if (tag == handles_tag) {
            for (std::size_t i = 0; i + 8 <= length; i += 8) {
                handles.emplace_back(static_cast<Descriptor>(get_u64(bytes.substr(at, length), i)));
            }
        }
        at += length;
    }
    if (handles.size() > 8) throw ProtocolError("a message carried more descriptors than any message may");
    return handles;
}

int remaining_ms(std::optional<Clock::time_point> deadline) {
    if (!deadline) return -1;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, 1 << 30));
}

int wait_for(Descriptor fd, short events, int timeout_ms) {
    platform::PollItem item{static_cast<platform::native_socket>(fd), events, 0};
    return platform::poll(std::span(&item, 1), timeout_ms);
}

} // namespace

bool MessageReader::fill(Descriptor fd) {
    while (!ready_) {
        // Only what is available: a socket that has nothing to read is left
        // alone, whatever its blocking mode.
        const int readable = wait_for(fd, platform::poll_in, 0);
        if (readable < 0) {
            if (platform::last_socket_error().interrupted()) continue;
            throw TransportError("the connection failed: " + socket_message());
        }
        if (readable == 0) return true;
        const auto have = buffer_.size();
        std::string chunk(need_ - have, '\0');
        const int count = ::recv(static_cast<SOCKET>(fd), chunk.data(), static_cast<int>(chunk.size()), 0);
        if (count == SOCKET_ERROR) {
            const auto error = platform::last_socket_error();
            if (error.interrupted()) continue;
            if (error.would_block()) return true;
            throw TransportError("the connection failed: " + error.message());
        }
        if (count == 0) {
            if (buffer_.empty()) return false;
            throw TransportError("the connection ended inside a message");
        }
        buffer_.append(chunk.data(), static_cast<std::size_t>(count));
        if (buffer_.size() < need_) continue;
        if (need_ == 4) {
            std::array<std::uint8_t, 4> length{};
            std::memcpy(length.data(), buffer_.data(), 4);
            need_ = 4 + std::size_t{announced_length(length)};
            continue;
        }
        auto handles = take_handles(buffer_);
        const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(buffer_.data()), buffer_.size());
        auto message = decode(bytes, handles.size());
        ready_ = Received{std::move(message), std::move(handles)};
        buffer_.clear();
        need_ = 4;
    }
    return true;
}

#else

void UniqueFd::reset(int fd) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

void set_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags >= 0) ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

void set_nonblocking(int fd, bool nonblocking) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0) return;
    ::fcntl(fd, F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

// --- Messages ----------------------------------------------------------------

bool MessageReader::fill(int fd) {
    while (!ready_) {
        const auto have = buffer_.size();
        std::string chunk(need_ - have, '\0');
        iovec io{chunk.data(), chunk.size()};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 8)];
        msghdr header{};
        header.msg_iov = &io;
        header.msg_iovlen = 1;
        header.msg_control = control;
        header.msg_controllen = sizeof control;
        int flags = 0;
#ifdef MSG_CMSG_CLOEXEC
        flags |= MSG_CMSG_CLOEXEC;
#endif
        const auto count = ::recvmsg(fd, &header, flags | MSG_DONTWAIT);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            throw TransportError(std::string("the connection failed: ") + std::strerror(errno));
        }
        for (auto* message = CMSG_FIRSTHDR(&header); message; message = CMSG_NXTHDR(&header, message)) {
            if (message->cmsg_level != SOL_SOCKET || message->cmsg_type != SCM_RIGHTS) continue;
            const auto n = (message->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            const int* fds = reinterpret_cast<const int*>(CMSG_DATA(message));
            for (std::size_t i = 0; i < n; ++i) {
                set_cloexec(fds[i]);
                descriptors_.emplace_back(fds[i]);
            }
        }
        if (header.msg_flags & MSG_CTRUNC) {
            descriptors_.clear();
            throw ProtocolError("a message carried more descriptors than any message may");
        }
        if (count == 0) {
            if (buffer_.empty() && descriptors_.empty()) return false;
            throw TransportError("the connection ended inside a message");
        }
        buffer_.append(chunk.data(), static_cast<std::size_t>(count));
        if (buffer_.size() < need_) continue;
        if (need_ == 4) {
            std::array<std::uint8_t, 4> length{};
            std::memcpy(length.data(), buffer_.data(), 4);
            need_ = 4 + std::size_t{announced_length(length)};
            continue;
        }
        const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(buffer_.data()), buffer_.size());
        auto message = decode(bytes, descriptors_.size());
        ready_ = Received{std::move(message), std::move(descriptors_)};
        descriptors_.clear();
        buffer_.clear();
        need_ = 4;
    }
    return true;
}

#endif

std::optional<Received> MessageReader::take() {
    auto out = std::move(ready_);
    ready_.reset();
    return out;
}

#ifndef _WIN32

namespace {

int remaining_ms(std::optional<Clock::time_point> deadline) {
    if (!deadline) return -1;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<int>(std::min<long long>(left, 1 << 30));
}

} // namespace

void send_message(int fd, const Message& message, const std::vector<int>& descriptors,
                  std::optional<Clock::time_point> deadline) {
    const auto bytes = encode(message);
    std::size_t sent = 0;
    bool first = true;
    while (sent < bytes.size()) {
        iovec io{const_cast<char*>(bytes.data() + sent), bytes.size() - sent};
        msghdr header{};
        header.msg_iov = &io;
        header.msg_iovlen = 1;
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int) * 8)];
        if (first && !descriptors.empty()) {
            if (descriptors.size() > 8) throw TransportError("too many descriptors for one message");
            header.msg_control = control;
            header.msg_controllen = CMSG_SPACE(sizeof(int) * descriptors.size());
            auto* cmsg = CMSG_FIRSTHDR(&header);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int) * descriptors.size());
            std::memcpy(CMSG_DATA(cmsg), descriptors.data(), sizeof(int) * descriptors.size());
        }
        int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        const auto count = ::sendmsg(fd, &header, flags);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd poll_fd{fd, POLLOUT, 0};
                const int ready = ::poll(&poll_fd, 1, remaining_ms(deadline));
                if (ready == 0) throw TransportError("the peer did not accept the message in time");
                continue;
            }
            throw TransportError(std::string("cannot send: ") + std::strerror(errno));
        }
        first = false;
        sent += static_cast<std::size_t>(count);
    }
}

std::optional<Received> receive_message(int fd, std::optional<Clock::time_point> deadline) {
    MessageReader reader;
    for (;;) {
        if (!reader.fill(fd)) return std::nullopt;
        if (auto message = reader.take()) return message;
        pollfd poll_fd{fd, POLLIN, 0};
        const int ready = ::poll(&poll_fd, 1, remaining_ms(deadline));
        if (ready < 0 && errno == EINTR) continue;
        if (ready == 0) throw TransportError("no reply in time");
    }
}

std::optional<std::uint32_t> peer_uid(int fd) {
#if defined(SO_PEERCRED)
    ucred credentials{};
    socklen_t length = sizeof credentials;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) return std::nullopt;
    return credentials.uid;
#else
    uid_t uid = 0;
    gid_t gid = 0;
    if (::getpeereid(fd, &uid, &gid) != 0) return std::nullopt;
    return uid;
#endif
}

void random_bytes(std::span<std::uint8_t> out) {
    std::size_t done = 0;
#if defined(__linux__)
    while (done < out.size()) {
        const auto count = ::getrandom(out.data() + done, out.size() - done, 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        done += static_cast<std::size_t>(count);
    }
#elif defined(__APPLE__)
    ::arc4random_buf(out.data(), out.size());
    done = out.size();
#endif
    if (done < out.size()) throw TransportError("no random bytes are available");
}

// --- The build ------------------------------------------------------------------

namespace {

#if defined(__linux__)
struct BuildIdSearch {
    std::uintptr_t address;
    std::string id;
};

int find_build_id(dl_phdr_info* info, std::size_t, void* data) {
    auto* search = static_cast<BuildIdSearch*>(data);
    bool contains = false;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD) continue;
        const auto start = info->dlpi_addr + segment.p_vaddr;
        if (search->address >= start && search->address < start + segment.p_memsz) contains = true;
    }
    if (!contains) return 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_NOTE) continue;
        const auto* at = reinterpret_cast<const unsigned char*>(info->dlpi_addr + segment.p_vaddr);
        const auto* end = at + segment.p_memsz;
        while (at + sizeof(ElfW(Nhdr)) <= end) {
            const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(at);
            const auto* name = at + sizeof(ElfW(Nhdr));
            const auto* desc = name + ((note->n_namesz + 3) & ~3u);
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
                search->id = hex(std::span(desc, note->n_descsz));
                return 1;
            }
            at = desc + ((note->n_descsz + 3) & ~3u);
        }
    }
    return 1;
}
#endif

} // namespace

std::string own_build() {
    static const std::string build = [] {
        std::string identity;
        const void* address = reinterpret_cast<const void*>(&own_build);
#if defined(__linux__)
        BuildIdSearch search{reinterpret_cast<std::uintptr_t>(address), {}};
        dl_iterate_phdr(find_build_id, &search);
        if (!search.id.empty()) identity = "build-id " + search.id;
#endif
        if (identity.empty()) {
            Dl_info info{};
            struct stat status{};
            if (::dladdr(address, &info) != 0 && info.dli_fname && ::stat(info.dli_fname, &status) == 0) {
                std::ostringstream text;
                text << "file " << status.st_dev << ':' << status.st_ino << ':' << status.st_size << ':'
                     << status.st_mtime;
                identity = text.str();
            } else {
                identity = "unknown";
            }
        }
#ifdef NEURA_SERVICE_TEST_BUILD
        // A test supervisor of another build (tests/service, case 11).
        identity += " " NEURA_SERVICE_TEST_BUILD;
#endif
        return neura::tool::version_line() + "; " + identity;
    }();
    return build;
}

#else

namespace {

// Closes handles that were duplicated into `process` for a message that was
// not sent whole.
void close_given(HANDLE process, const std::vector<HANDLE>& given) {
    for (HANDLE theirs : given) {
        ::DuplicateHandle(process, theirs, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
    }
}

} // namespace

void send_message(Descriptor fd, const Message& message, const std::vector<Descriptor>& descriptors,
                  std::optional<Clock::time_point> deadline, void* recipient) {
    if (descriptors.size() > 8) throw TransportError("too many descriptors for one message");
    // Every service socket is non-blocking, so that the deadline holds.
    set_nonblocking(fd, true);
    std::vector<HANDLE> given;
    std::string bytes;
    if (descriptors.empty()) {
        bytes = encode(message);
    } else {
        if (recipient == nullptr) throw TransportError("no process to pass the handles to");
        Message out = message;
        std::string values;
        for (const auto descriptor : descriptors) {
            HANDLE theirs = nullptr;
            if (!::DuplicateHandle(::GetCurrentProcess(), handle_of(descriptor), recipient, &theirs, 0, FALSE,
                                   DUPLICATE_SAME_ACCESS)) {
                const auto code = ::GetLastError();
                close_given(recipient, given);
                throw TransportError("cannot pass a handle to the peer: " + system_message(code));
            }
            given.push_back(theirs);
            put_u64(values, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(theirs)));
        }
        out.fields.push_back({handles_tag, std::move(values)});
        try {
            bytes = encode(out);
        } catch (...) {
            close_given(recipient, given);
            throw;
        }
    }
    std::size_t sent = 0;
    try {
        while (sent < bytes.size()) {
            const auto size = static_cast<int>(std::min<std::size_t>(bytes.size() - sent, 1 << 20));
            const int count = ::send(static_cast<SOCKET>(fd), bytes.data() + sent, size, 0);
            if (count == SOCKET_ERROR) {
                const auto error = platform::last_socket_error();
                if (error.interrupted()) continue;
                if (error.would_block()) {
                    const int ready = wait_for(fd, platform::poll_out, remaining_ms(deadline));
                    if (ready == 0) throw TransportError("the peer did not accept the message in time");
                    continue;
                }
                throw TransportError("cannot send: " + error.message());
            }
            sent += static_cast<std::size_t>(count);
        }
    } catch (...) {
        // Handles of a message the peer never gets whole are its no longer.
        close_given(recipient, given);
        throw;
    }
}

void send_message(Descriptor fd, const Message& message, const std::vector<Descriptor>& descriptors,
                  std::optional<Clock::time_point> deadline) {
    if (descriptors.empty()) {
        send_message(fd, message, descriptors, deadline, nullptr);
        return;
    }
    std::error_code error;
    const auto pid = platform::peer_pid(static_cast<platform::native_socket>(fd), error);
    if (!pid) throw TransportError("cannot tell which process is at the other end: " + platform::error_message(error));
    UniqueFd process(reinterpret_cast<Descriptor>(::OpenProcess(PROCESS_DUP_HANDLE, FALSE, *pid)));
    if (!process) {
        throw TransportError("cannot pass handles to process " + std::to_string(*pid) + ": " +
                             system_message(::GetLastError()));
    }
    send_message(fd, message, descriptors, deadline, handle_of(process.get()));
}

std::optional<Received> receive_message(Descriptor fd, std::optional<Clock::time_point> deadline) {
    MessageReader reader;
    for (;;) {
        if (!reader.fill(fd)) return std::nullopt;
        if (auto message = reader.take()) return message;
        const int ready = wait_for(fd, platform::poll_in, remaining_ms(deadline));
        if (ready < 0 && platform::last_socket_error().interrupted()) continue;
        if (ready < 0) throw TransportError("the connection failed: " + socket_message());
        if (ready == 0) throw TransportError("no reply in time");
    }
}

namespace {

// The user SID and integrity level of a process's token.
struct TokenIdentity {
    std::vector<std::byte> user;  // TOKEN_USER
    DWORD integrity = 0;          // the RID of the mandatory label
    bool known = false;
};

TokenIdentity identity_of(HANDLE process) {
    TokenIdentity identity;
    HANDLE token = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &token)) return identity;
    UniqueFd owned(reinterpret_cast<Descriptor>(token));
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0) return identity;
    identity.user.resize(size);
    if (!::GetTokenInformation(token, TokenUser, identity.user.data(), size, &size)) return identity;
    size = 0;
    ::GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
    if (size == 0) return identity;
    std::vector<std::byte> label(size);
    if (!::GetTokenInformation(token, TokenIntegrityLevel, label.data(), size, &size)) return identity;
    const auto* mandatory = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(label.data());
    const auto count = *::GetSidSubAuthorityCount(mandatory->Label.Sid);
    if (count == 0) return identity;
    identity.integrity = *::GetSidSubAuthority(mandatory->Label.Sid, count - 1);
    identity.known = true;
    return identity;
}

PSID sid_of(TokenIdentity& identity) { return reinterpret_cast<TOKEN_USER*>(identity.user.data())->User.Sid; }

} // namespace

PeerIdentity peer_identity(Descriptor socket) {
    PeerIdentity peer;
    std::error_code error;
    const auto pid = platform::peer_pid(static_cast<platform::native_socket>(socket), error);
    if (!pid) return peer;
    UniqueFd process(reinterpret_cast<Descriptor>(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *pid)));
    if (!process) return peer;
    auto theirs = identity_of(handle_of(process.get()));
    auto own = identity_of(::GetCurrentProcess());
    if (!theirs.known || !own.known) return peer;
    peer.known = true;
    peer.same_user = ::EqualSid(sid_of(theirs), sid_of(own)) != FALSE;
    peer.integrity = theirs.integrity < own.integrity ? -1 : theirs.integrity > own.integrity ? 1 : 0;
    return peer;
}

void random_bytes(std::span<std::uint8_t> out) {
    std::size_t done = 0;
    while (done < out.size()) {
        const auto chunk = static_cast<ULONG>(std::min<std::size_t>(out.size() - done, 1u << 30));
        if (!BCRYPT_SUCCESS(::BCryptGenRandom(nullptr, out.data() + done, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            throw TransportError("no random bytes are available");
        }
        done += chunk;
    }
}

// --- The build ------------------------------------------------------------------

namespace {

// The GUID and age of the CodeView record of a loaded module, as
// "rsds <32 hexadecimal digits>.<age>", or nothing.
std::string codeview_identity(HMODULE module) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) {
        return {};
    }
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (directory.VirtualAddress == 0) return {};
    const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + directory.VirtualAddress);
    const std::size_t count = directory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& entry = entries[i];
        if (entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW || entry.AddressOfRawData == 0 || entry.SizeOfData < 24) continue;
        const auto* record = base + entry.AddressOfRawData;
        if (std::memcmp(record, "RSDS", 4) != 0) continue;
        std::uint32_t age = 0;
        std::memcpy(&age, record + 20, 4);
        return "rsds " + hex(std::span(record + 4, 16)) + "." + std::to_string(age);
    }
    return {};
}

} // namespace

std::string own_build() {
    static const std::string build = [] {
        std::string identity;
        HMODULE module = nullptr;
        if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCWSTR>(&own_build), &module)) {
            identity = codeview_identity(module);
            if (identity.empty()) {
                std::wstring name(32768, L'\0');
                const DWORD length = ::GetModuleFileNameW(module, name.data(), static_cast<DWORD>(name.size()));
                name.resize(length);
                std::ifstream input(fs::path(name), std::ios::binary);
                if (length != 0 && input) {
                    std::ostringstream bytes;
                    bytes << input.rdbuf();
                    identity = "sha256 " + util::sha256_hex(bytes.str());
                }
            }
        }
        if (identity.empty()) identity = "unknown";
#ifdef NEURA_SERVICE_TEST_BUILD
        // A test supervisor of another build (tests/service, case 11).
        identity += " " NEURA_SERVICE_TEST_BUILD;
#endif
        return neura::tool::version_line() + "; " + identity;
    }();
    return build;
}

#endif

// --- Files ----------------------------------------------------------------------

namespace {

std::string short_digest(std::string_view text) { return util::sha256_hex(text).substr(0, 16); }

} // namespace

std::string endpoint_of(const fs::path& root, std::string_view generation) {
#ifdef _WIN32
    // The root in generic form, as the lifecycle names it (W29).
    std::string text = text_of(root);
#else
    std::string text = root.string();
#endif
    text += '\0';
    text += generation;
    return short_digest(text);
}

#ifdef _WIN32

namespace {

// The real path of `path`, without the \\?\ prefix that the system's final
// path may carry, as the lifecycle names an installation.
fs::path real_path(const fs::path& path, std::error_code& error) {
    auto real = fs::canonical(path, error);
    if (error) return {};
    const auto& text = real.native();
    if (text.starts_with(L"\\\\?\\UNC\\")) return fs::path(L"\\\\" + text.substr(8));
    if (text.starts_with(L"\\\\?\\")) return fs::path(text.substr(4));
    return real;
}

} // namespace

Installation installation_of(const fs::path& executable) {
    Installation installation;
    std::error_code error;
    auto real = real_path(executable, error);
    if (error) real = executable;
    installation.bin = real.parent_path();
    const auto detection = lifecycle::detect_install(real);
    if (detection.kind == lifecycle::Detection::Kind::Installed) {
        installation.root = real_path(detection.root, error);
        if (error) installation.root = detection.root;
        installation.generation = detection.running_generation;
        installation.installed = true;
        installation.unmigrated = lifecycle::needs_migration(detection);
    } else {
        installation.root = installation.bin;
        installation.generation = "dev";
    }
    installation.id = short_digest(text_of(installation.root));
    return installation;
}

Installation installation_at_root(const fs::path& root) {
    Installation installation;
    std::error_code error;
    installation.root = real_path(root, error);
    if (error) installation.root = root;
    // `current` is a junction to versions/<generation> (W22).
    const auto current = real_path(installation.root / "current", error);
    if (!error) installation.generation = text_of(current.filename());
    installation.bin = installation.root / "versions" / installation.generation / "bin";
    installation.installed = true;
    installation.id = short_digest(text_of(installation.root));
    return installation;
}

#else

Installation installation_of(const fs::path& executable) {
    Installation installation;
    std::error_code error;
    auto real = fs::canonical(executable, error);
    if (error) real = executable;
    installation.bin = real.parent_path();
    const auto detection = lifecycle::detect_install(real);
    if (detection.kind == lifecycle::Detection::Kind::Installed) {
        installation.root = fs::canonical(detection.root, error);
        if (error) installation.root = detection.root;
        installation.generation = detection.running_generation;
        installation.installed = true;
        // The lifecycle's own rule (docs/design/Lifecycle030.md): a schema
        // 1 marker and a core component manifest in the running generation,
        // which only a 0.3 archive placed by a 0.2 updater has.
        installation.unmigrated = lifecycle::needs_migration(detection);
    } else {
        installation.root = installation.bin;
        installation.generation = "dev";
    }
    installation.id = short_digest(installation.root.string());
    return installation;
}

Installation installation_at_root(const fs::path& root) {
    Installation installation;
    std::error_code error;
    installation.root = fs::canonical(root, error);
    if (error) installation.root = root;
    installation.generation = fs::read_symlink(installation.root / "current", error).filename().string();
    installation.bin = installation.root / "versions" / installation.generation / "bin";
    installation.installed = true;
    installation.id = short_digest(installation.root.string());
    return installation;
}

#endif

#ifdef _WIN32

namespace {

// This user's SID as text (S-1-5-21-...), as the lifecycle hashes it.
std::string user_sid_text() {
    auto own = identity_of(::GetCurrentProcess());
    if (own.user.empty()) return {};
    wchar_t* text = nullptr;
    if (!::ConvertSidToStringSidW(sid_of(own), &text)) return {};
    std::string sid = platform::try_to_utf8(text).value_or(std::string());
    ::LocalFree(text);
    return sid;
}

// %LOCALAPPDATA%/Neura, which holds the run and state directories.
fs::path service_base() {
    std::error_code error;
    const auto local = platform::local_app_data(error);
    if (error || local.empty()) {
        throw TransportError("the folder %LOCALAPPDATA% is not known (" + platform::error_message(error) +
                             "); the service is unavailable");
    }
    return local / "Neura";
}

bool exists_no_follow(const fs::path& path) {
    std::error_code error;
    return fs::symlink_status(path, error).type() != fs::file_type::not_found && !error;
}

// Creates `directory` with a protected DACL that grants only this user, or
// accepts it when it already is such a directory.
void private_directory_at(const fs::path& directory) {
    std::error_code error;
    if (!platform::make_private_directory(directory, error) && error != std::errc::file_exists) {
        throw TransportError("cannot create " + text_of(directory) + ": " + platform::error_message(error));
    }
}

} // namespace

bool private_directory(const fs::path& directory) {
    std::error_code error;
    return platform::is_private_directory(directory, error);
}

void make_private_directory(const fs::path& directory) {
    private_directory_at(directory);
    if (!private_directory(directory)) {
        throw TransportError(text_of(directory) + " is not a directory of this user that only this user can open");
    }
}

fs::path run_directory(bool create) {
    const auto base = service_base();
    const auto run = base / "run";
    if (!exists_no_follow(run)) {
        if (!create) return run;
        // The base is also the default install root (W22): an ordinary
        // directory; only what the service keeps in it is private.
        std::error_code error;
        fs::create_directories(base, error);
        if (error) throw TransportError("cannot create " + text_of(base) + ": " + error.message());
        make_private_directory(run);
        return run;
    }
    if (!private_directory(run)) {
        throw TransportError(text_of(run) +
                             " is refused: it must be a directory of this user that only this user can open");
    }
    return run;
}

Endpoint endpoint_files(const fs::path& run, const fs::path& root, std::string_view generation) {
    // An AF_UNIX address holds 107 bytes of path and its NUL.
    constexpr std::size_t longest = 107;
    const auto name = endpoint_of(root, generation);
    Endpoint endpoint;
    endpoint.directory = run / name;
    if (platform::native_utf8_path(endpoint.directory / "s").size() > longest) {
        std::error_code error;
        const auto temporary = platform::temp_directory(error);
        if (error) throw TransportError("the temporary directory is not known: " + platform::error_message(error));
        endpoint.directory = temporary / ("neura-" + short_digest(user_sid_text())) / name;
    }
    endpoint.socket = endpoint.directory / "s";
    endpoint.lock = endpoint.directory / "lock";
    endpoint.start = endpoint.directory / "start";
    if (platform::native_utf8_path(endpoint.socket).size() > longest) {
        throw TransportError("the socket path " + text_of(endpoint.socket) +
                             " is longer than this system allows (107 bytes); the service is unavailable");
    }
    return endpoint;
}

fs::path state_root(const Installation& installation) { return service_base() / "state" / installation.id; }

void make_state_root(const Installation& installation) {
    const auto root = state_root(installation);
    std::error_code error;
    fs::create_directories(root.parent_path().parent_path(), error);
    if (error) throw TransportError("cannot create " + text_of(root.parent_path().parent_path()) + ": " + error.message());
    for (const auto& directory : {root.parent_path(), root}) {
        if (!exists_no_follow(directory)) private_directory_at(directory);
    }
}

#else

bool private_directory(const fs::path& directory) {
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0) return false;
    return S_ISDIR(status.st_mode) && status.st_uid == ::geteuid() && (status.st_mode & 0777) == 0700;
}

void make_private_directory(const fs::path& directory) {
    if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        throw TransportError("cannot create " + directory.string() + ": " + std::strerror(errno));
    }
    if (!private_directory(directory)) {
        throw TransportError(directory.string() + " is not a directory of this user with mode 0700");
    }
}

fs::path run_directory(bool create) {
    const auto uid = ::geteuid();
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg == '/') {
        if (private_directory(xdg)) return fs::path(xdg);
    }
    const char* tmp = std::getenv("TMPDIR");
    fs::path base = tmp && *tmp == '/' ? fs::path(tmp) : fs::path("/tmp");
    const auto directory = base / ("neura-" + std::to_string(uid));
    struct stat status{};
    if (::lstat(directory.c_str(), &status) != 0) {
        if (!create) return directory;
        make_private_directory(directory);
        return directory;
    }
    if (!private_directory(directory)) {
        throw TransportError(directory.string() + " is refused: it must be a directory of this user with mode 0700");
    }
    return directory;
}

Endpoint endpoint_files(const fs::path& run, const fs::path& root, std::string_view generation) {
    Endpoint endpoint;
    endpoint.directory = run / "neura" / endpoint_of(root, generation);
    endpoint.socket = endpoint.directory / "s";
    endpoint.lock = endpoint.directory / "lock";
    endpoint.start = endpoint.directory / "start";
    if (endpoint.socket.native().size() >= sizeof(sockaddr_un::sun_path)) {
        throw TransportError("the socket path " + endpoint.socket.string() + " is longer than this system allows (" +
                             std::to_string(sizeof(sockaddr_un::sun_path) - 1) + " bytes); the service is unavailable");
    }
    return endpoint;
}

fs::path state_root(const Installation& installation) {
    fs::path base;
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state == '/') {
        base = state;
    } else if (const char* home = std::getenv("HOME"); home && *home == '/') {
        base = fs::path(home) / ".local/state";
    } else {
        base = fs::path("/tmp") / ("neura-state-" + std::to_string(::geteuid()));
    }
    return base / "neura" / installation.id;
}

#endif

fs::path state_directory(const Installation& installation, std::string_view generation) {
    return state_root(installation) / std::string(generation);
}

#ifdef _WIN32

void write_atomically(const fs::path& path, std::string_view text) {
    std::error_code error;
    if (!platform::write_file_atomically(path, text, error)) {
        throw TransportError("cannot write " + text_of(path) + ": " + platform::error_message(error));
    }
}

#else

void write_atomically(const fs::path& path, std::string_view text) {
    auto temporary = path;
    temporary += ".tmp" + std::to_string(::getpid());
    {
        const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) throw TransportError("cannot write " + path.string() + ": " + std::strerror(errno));
        UniqueFd owned(fd);
        std::size_t done = 0;
        while (done < text.size()) {
            const auto count = ::write(fd, text.data() + done, text.size() - done);
            if (count < 0) {
                if (errno == EINTR) continue;
                ::unlink(temporary.c_str());
                throw TransportError("cannot write " + path.string() + ": " + std::strerror(errno));
            }
            done += static_cast<std::size_t>(count);
        }
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        throw TransportError("cannot write " + path.string() + ": " + std::strerror(errno));
    }
}

#endif

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

std::vector<std::string> live_generations(const Installation& installation) {
    std::vector<std::string> generations;
    if (installation.installed) {
#ifdef _WIN32
        const auto detection = lifecycle::detect_install(installation.bin / neura::tool::executable_file("neura"));
#else
        const auto detection = lifecycle::detect_install(installation.bin / "neura");
#endif
        if (detection.kind == lifecycle::Detection::Kind::Installed) generations = detection.marker.generations;
    }
    if (std::ranges::find(generations, installation.generation) == generations.end()) {
        generations.push_back(installation.generation);
    }
    return generations;
}

std::vector<std::string> recorded_generations(const Installation& installation) {
    std::vector<std::string> generations;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(state_root(installation), error)) {
        if (entry.is_directory(error)) generations.push_back(entry.path().filename().string());
    }
    std::ranges::sort(generations);
    return generations;
}

#ifdef _WIN32

std::vector<std::string> environment_entries() {
    std::vector<std::string> entries;
    for (const auto& [name, value] : platform::environment()) entries.push_back(name + "=" + value);
    return entries;
}

void clear_environment() {
    for (const auto& [name, value] : platform::environment()) {
        std::error_code error;
        platform::unset_env(name, error);
    }
}

// --- Process settings -----------------------------------------------------------

int resource_of(std::uint8_t) { return -1; }

std::uint64_t signal_bit(int signal) { return signal > 0 && signal <= 64 ? std::uint64_t{1} << (signal - 1) : 0; }

void capture_process_settings(Envelope& envelope) {
    // Not captured on Windows (W29): umask, resource limits, nice and the
    // signal dispositions; the priority class stands in for nice.
    envelope.resource_limits.clear();
    envelope.nice = static_cast<std::int64_t>(::GetPriorityClass(::GetCurrentProcess()));
    envelope.ignored_signals = 0;
    envelope.blocked_signals = 0;
}

#else

std::vector<std::string> environment_entries() {
    std::vector<std::string> entries;
    for (char** entry = process_environment(); entry && *entry; ++entry) entries.emplace_back(*entry);
    return entries;
}

char** process_environment() {
#if defined(__APPLE__)
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

void clear_environment() {
#if defined(__linux__)
    ::clearenv();
#else
    std::vector<std::string> names;
    for (char** entry = process_environment(); entry && *entry; ++entry) {
        const std::string_view text(*entry);
        names.emplace_back(text.substr(0, text.find('=')));
    }
    for (const auto& name : names) ::unsetenv(name.c_str());
#endif
}

// --- Process settings -----------------------------------------------------------

int resource_of(std::uint8_t resource) {
    switch (static_cast<ResourceKind>(resource)) {
    case ResourceKind::Nofile: return RLIMIT_NOFILE;
    case ResourceKind::Stack: return RLIMIT_STACK;
    case ResourceKind::Core: return RLIMIT_CORE;
    case ResourceKind::Fsize: return RLIMIT_FSIZE;
    case ResourceKind::As: return RLIMIT_AS;
    case ResourceKind::Cpu: return RLIMIT_CPU;
    }
    return -1;
}

std::uint64_t signal_bit(int signal) { return signal > 0 && signal <= 64 ? std::uint64_t{1} << (signal - 1) : 0; }

void capture_process_settings(Envelope& envelope) {
    const auto mask = ::umask(0);
    ::umask(mask);
    envelope.umask = static_cast<std::uint32_t>(mask);
    envelope.resource_limits.clear();
    for (std::uint8_t resource = 1; resource <= 6; ++resource) {
        rlimit limit{};
        if (::getrlimit(resource_of(resource), &limit) != 0) continue;
        const auto value = [](rlim_t v) { return v == RLIM_INFINITY ? unlimited : static_cast<std::uint64_t>(v); };
        envelope.resource_limits.push_back({resource, value(limit.rlim_cur), value(limit.rlim_max)});
    }
    errno = 0;
    const int nice_value = ::getpriority(PRIO_PROCESS, 0);
    envelope.nice = errno == 0 ? nice_value : 0;
    envelope.ignored_signals = 0;
    for (int signal = 1; signal < NSIG && signal <= 64; ++signal) {
        struct sigaction action{};
        if (::sigaction(signal, nullptr, &action) == 0 && action.sa_handler == SIG_IGN) {
            envelope.ignored_signals |= signal_bit(signal);
        }
    }
    sigset_t blocked;
    sigemptyset(&blocked);
    ::sigprocmask(SIG_BLOCK, nullptr, &blocked);
    envelope.blocked_signals = 0;
    for (int signal = 1; signal < NSIG && signal <= 64; ++signal) {
        if (sigismember(&blocked, signal) == 1) envelope.blocked_signals |= signal_bit(signal);
    }
}

#endif

std::uint64_t clock_ns(Clock::time_point point) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(point.time_since_epoch()).count());
}

Clock::time_point clock_at(std::uint64_t ns) {
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(ns)));
}

std::string milliseconds(std::uint64_t ns) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", static_cast<double>(ns) / 1e6);
    return text;
}

} // namespace neura::service

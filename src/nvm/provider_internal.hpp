#pragma once

// Shared pieces of the standard host providers (providers.hpp). Not part of
// the NVM's public interface.

#include "providers.hpp"

#include <cstdint>
#include <climits>
#include <functional>
#include <map>
#include <memory>

#ifdef _WIN32
#include "platform/file.hpp"
#include "platform/socket.hpp"

#include <string_view>
#endif

namespace neura::nvm {

namespace provider {
// What the file and socket tables hold: a HANDLE and a SOCKET on Windows, a
// descriptor elsewhere, each with the value that means "none".
#ifdef _WIN32
using NativeFile = platform::native_file;
using NativeSocket = platform::native_socket;
inline const NativeFile invalid_native_file = reinterpret_cast<NativeFile>(static_cast<std::intptr_t>(-1));
inline constexpr NativeSocket invalid_native_socket = platform::invalid_socket;
#else
using NativeFile = int;
using NativeSocket = int;
inline constexpr NativeFile invalid_native_file = -1;
inline constexpr NativeSocket invalid_native_socket = -1;
#endif

// An upcall stub (ffi_provider.cpp); destroyed with UpcallDeleter.
struct Upcall;
struct UpcallDeleter {
    void operator()(Upcall* upcall) const;
};
// A TLS stream (tls_provider.cpp); destroyed with TlsDeleter.
struct TlsStream;
struct TlsDeleter {
    void operator()(TlsStream* stream) const;
};
} // namespace provider

struct StandardServices::Tables {
    struct File {
        provider::NativeFile descriptor = provider::invalid_native_file;
        bool readable = false;
        bool writable = false;
    };
    struct Socket {
        provider::NativeSocket descriptor = provider::invalid_native_socket;
        bool listener = false;
        // A UDP or Unix datagram socket (socket_provider.cpp).
        bool datagram = false;
        // One end of a pipe of neura.net.pipe, for neura.net.splice.
        bool pipe = false;
        // A raw IP socket, which only accepts raw send/receive operations.
        bool raw = false;
    };
    std::map<std::int64_t, File> files;
    std::int64_t next_file = 1;
    std::map<std::int64_t, Socket> sockets;
    std::int64_t next_socket = 1;
    // TLS streams of neura.net.tls.*, numbered on their own.
    std::map<std::int64_t, std::unique_ptr<provider::TlsStream, provider::TlsDeleter>> tls;
    std::int64_t next_tls = 1;
    // Native memory and libraries of Std.DynamicForeign. Arena 0 is the
    // global arena, released at the end of the run.
    using Upcall = provider::Upcall;
    struct Arena {
        std::vector<void*> blocks;
        std::vector<void*> libraries;
        // Upcall stubs whose arena this is; closing it disables them.
        std::vector<Upcall*> upcalls;
    };
    struct Native {
        std::map<std::int64_t, Arena> arenas;
        std::int64_t next_arena = 1;
        Arena global;
        // Library handles by ID; ID 0 is the process's default lookup.
        std::map<std::int64_t, void*> libraries;
        std::int64_t next_library = 1;
        // The image's embedded shared object, if any, and its library ID
        // once loaded.
        const std::string* embedded_object = nullptr;
        std::int64_t embedded_id = 0;
        // Every upcall stub of the run. Stubs stay allocated until the run
        // ends, so a call through a disabled stub is detected, not a crash.
        std::vector<std::unique_ptr<Upcall, provider::UpcallDeleter>> upcalls;
    };
    Native native;
};

namespace provider {

using Kind = nvr::TypeKind;

inline HostType void_type() { return HostType(Kind::Void); }
inline HostType boolean() { return HostType(Kind::Bool); }
inline HostType int32() { return HostType(Kind::Signed, 32); }
inline HostType int64() { return HostType(Kind::Signed, 64); }
inline HostType uint32() { return HostType(Kind::Unsigned, 32); }
inline HostType text() { return HostType(Kind::Text); }
inline HostType byte_list() { return HostType(Kind::List, std::vector<HostType>{HostType(Kind::Unsigned, 8)}); }
inline HostType text_list() { return HostType(Kind::List, std::vector<HostType>{text()}); }
inline HostType tuple(std::vector<HostType> fields) { return HostType(Kind::Object, std::move(fields)); }

// A version 1 signature with a uint32 error.
inline HostSignature signature(std::string name, std::vector<HostType> parameters, HostType success) {
    return HostSignature{std::move(name), 1, std::move(parameters), std::move(success), uint32()};
}

inline HostValue scalar(std::uint64_t bits) { return HostValue{bits, {}, {}}; }
inline HostValue int32_value(std::int32_t value) { return scalar(static_cast<std::uint32_t>(value)); }
inline HostValue int64_value(std::int64_t value) { return scalar(static_cast<std::uint64_t>(value)); }
inline HostValue bytes_value(std::string bytes) { return HostValue{0, std::move(bytes), {}}; }
inline HostValue tuple_value(std::vector<HostValue> elements) { return HostValue{0, {}, std::move(elements)}; }

inline std::int32_t as_int32(const HostValue& value) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value.bits));
}
inline std::int64_t as_int64(const HostValue& value) { return static_cast<std::int64_t>(value.bits); }
inline bool as_bool(const HostValue& value) { return value.bits != 0; }

inline HostCompletion fail(std::uint32_t code) { return HostCompletion::failed(scalar(code)); }
inline HostCompletion denied() { return fail(host_error::capability_denied); }

// Text that the operating system can take as a C string.
inline bool usable_text(const std::string& text) {
    return !text.empty() && text.find('\0') == std::string::npos;
}

// The poll timeout, in milliseconds, for a positive nanosecond timeout:
// rounded up and capped; -1 waits without limit.
int timeout_milliseconds(std::int64_t nanos);
// Whether `descriptor` has data to read or has reached its end, without
// waiting.
//
// On Windows it takes a HANDLE (W17): a console is readable when a key press
// other than a modifier or lock key is pending (the input records before it,
// such as key releases, modifier keys, focus, menu, mouse and buffer-size
// events, which a console read would not deliver, are removed so the handle
// stops being signaled); a pipe when PeekNamedPipe sees bytes or a broken
// pipe (the end of input); a disk file or another character device always.
// A handle that cannot be asked counts as readable, so the read reports the
// failure. The SOCKET overload asks WSAPoll with a timeout of 0; a socket
// that WSAPoll refuses also counts as readable, as poll(2) reports POLLNVAL.
bool readable_now(NativeFile descriptor);
#ifdef _WIN32
bool readable_now(NativeSocket socket);

// The syntax refusals of the Windows file-system host (W09), for a path as a
// program gives it: invalid_input for a drive-relative "X:name", a ':' inside
// a component, a reserved device name, a component that ends in a dot or a
// space, one of <>"|?* or a control character, and the "\\?\" and "\\.\"
// device namespaces; 0 when the text may be passed on. resolve_path applies
// it; a service that takes a path without prefixes applies it itself.
std::uint32_t windows_path_error(std::string_view path);
#endif

using Adder = std::function<void(HostSignature, FunctionService::Handler)>;
// Adds version 1 and the cancellable version 2 of an overlapping service.
using OverlapAdder = std::function<void(HostSignature, FunctionService::Handler, FunctionService::Begin)>;

// The task clock reading `nanos` after `now`, without overflow.
inline std::int64_t deadline_after(std::int64_t now, std::int64_t nanos) {
    return nanos > INT64_MAX - now ? INT64_MAX - 1 : now + nanos;
}
inline HostCompletion cancelled_answer() { return fail(host_error::cancelled); }

void add_fs_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities,
                     const std::vector<ResolvedPath>& prefixes);
void add_net_services(const Adder& add, const OverlapAdder& overlap, StandardServices::Tables& tables,
                      const Capabilities& capabilities);
void add_ffi_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities);
void close_files(StandardServices::Tables& tables);
void close_native(StandardServices::Tables& tables);
// The target triple this runner was built for.
std::string native_triple();
// Loads a shared object held in memory and returns its handle, or nullptr.
void* load_embedded_object(const std::string& object);
void close_sockets(StandardServices::Tables& tables);
// UDP, Unix-domain sockets, options, resolution and send_file, over the same
// socket table.
std::optional<std::uint32_t> set_socket_option(NativeSocket socket, std::int32_t option, std::int64_t value);
std::optional<std::uint32_t> set_socket_options(NativeSocket socket, const HostValue& options, const HostValue& values);
void add_socket_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities);
// TLS client and server streams (OpenSSL when built with NEURA_NVM_TLS).
void add_tls_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities,
                      const std::vector<ResolvedPath>& prefixes);
void close_tls(StandardServices::Tables& tables);
void add_doq_services(const Adder& add, const Capabilities& capabilities);

} // namespace provider
} // namespace neura::nvm

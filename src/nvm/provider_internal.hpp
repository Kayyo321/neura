#pragma once

// Shared pieces of the standard host providers (providers.hpp). Not part of
// the NVM's public interface.

#include "providers.hpp"

#include <cstdint>
#include <climits>
#include <functional>
#include <map>
#include <memory>

namespace neura::nvm {

namespace provider {
// An upcall stub (ffi_provider.cpp); destroyed with UpcallDeleter.
struct Upcall;
struct UpcallDeleter {
    void operator()(Upcall* upcall) const;
};
} // namespace provider

struct StandardServices::Tables {
    struct File {
        int descriptor = -1;
        bool readable = false;
        bool writable = false;
    };
    struct Socket {
        int descriptor = -1;
        bool listener = false;
    };
    std::map<std::int64_t, File> files;
    std::int64_t next_file = 1;
    std::map<std::int64_t, Socket> sockets;
    std::int64_t next_socket = 1;
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
bool readable_now(int descriptor);

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

} // namespace provider
} // namespace neura::nvm

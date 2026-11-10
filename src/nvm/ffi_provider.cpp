// The native interface provider behind Std.DynamicForeign (ForeignInterop.md):
// native libraries, symbol lookup, arena-owned native memory, and downcalls
// through libffi. Every service needs the native capability (--allow-native).
//
// Wire formats, shared with stdlib/std/dynamic-foreign/internal/native.nra:
//
// A type is one tag byte. A struct tag is followed by a little-endian uint16
// member count and that many member types; padding is not encoded, because
// the C layout of the members determines it.
//
//   0 void   1 bool   2 int8   3 uint8   4 int16  5 uint16  6 int32
//   7 uint32 8 int64  9 uint64 10 float  11 double 12 pointer 13 struct
//
// A call signature is: version byte 1, uint16 fixed-argument count (0xffff
// when the function is not variadic), uint16 argument count, the result
// type, then each argument type. Arguments are the values' bytes back to back
// in native byte order, each exactly its type's size (a struct its C size).
// The result is the result type's bytes, empty for void.

#include "provider_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#ifndef _WIN32
#include <dlfcn.h>
#include <ffi.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace neura::nvm::provider {

namespace {

constexpr std::int64_t max_transfer_bytes = std::int64_t{1} << 24;
constexpr std::int64_t max_allocation_bytes = std::int64_t{1} << 32;

enum Tag : std::uint8_t {
    Void = 0, Bool = 1, Int8 = 2, UInt8 = 3, Int16 = 4, UInt16 = 5, Int32 = 6, UInt32 = 7,
    Int64 = 8, UInt64 = 9, Float = 10, Double = 11, Pointer = 12, Struct = 13
};

std::string target_triple() {
#if defined(__x86_64__) || defined(_M_X64)
    std::string arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    std::string arch = "aarch64";
#elif defined(__riscv) && __riscv_xlen == 64
    std::string arch = "riscv64";
#elif defined(__i386__) || defined(_M_IX86)
    std::string arch = "i386";
#elif defined(__arm__)
    std::string arch = "arm";
#else
    std::string arch = "unknown";
#endif
#if defined(__linux__)
    return arch + "-linux-gnu";
#elif defined(__APPLE__)
    return arch + "-apple-darwin";
#elif defined(_WIN32)
    return arch + "-windows-msvc";
#else
    return arch + "-unknown";
#endif
}

} // namespace

std::string native_triple() { return target_triple(); }

#ifdef _WIN32

void add_ffi_services(const Adder& add, StandardServices::Tables&, const Capabilities& capabilities) {
    const auto unsupported = [&capabilities](const HostRequest&) {
        return capabilities.native ? fail(host_error::unsupported) : denied();
    };
    add(signature("neura.ffi.abi", {}, tuple({text(), byte_list()})), unsupported);
    add(signature("neura.ffi.arena.open", {}, int64()), unsupported);
    add(signature("neura.ffi.arena.close", {int64()}, void_type()), unsupported);
    add(signature("neura.ffi.alloc", {int64(), int64(), int64()}, int64()), unsupported);
    add(signature("neura.ffi.library.open", {int64(), text()}, int64()), unsupported);
    add(signature("neura.ffi.library.default", {}, int64()), unsupported);
    add(signature("neura.ffi.library.embedded", {}, int64()), unsupported);
    add(signature("neura.ffi.symbol", {int64(), text()}, int64()), unsupported);
    add(signature("neura.ffi.read", {int64(), int64()}, byte_list()), unsupported);
    add(signature("neura.ffi.write", {int64(), byte_list()}, void_type()), unsupported);
    add(signature("neura.ffi.strlen", {int64(), int64()}, int64()), unsupported);
    add(signature("neura.ffi.call", {int64(), byte_list(), byte_list()}, byte_list()), unsupported);
    add(signature("neura.ffi.upcall.create", {int64(), int64(), byte_list()}, int64()), unsupported);
}

void close_native(StandardServices::Tables&) {}

struct Upcall {};
void UpcallDeleter::operator()(Upcall* upcall) const { delete upcall; }

#else

namespace {

// Decodes a type description and owns the libffi struct types it builds.
class TypeReader {
public:
    explicit TypeReader(const std::string& bytes) : bytes_(bytes) {}

    bool done() const { return position_ == bytes_.size(); }

    bool byte(std::uint8_t& out) {
        if (position_ >= bytes_.size()) return false;
        out = static_cast<std::uint8_t>(bytes_[position_++]);
        return true;
    }

    bool u16(std::uint16_t& out) {
        std::uint8_t low = 0, high = 0;
        if (!byte(low) || !byte(high)) return false;
        out = static_cast<std::uint16_t>(low | (high << 8));
        return true;
    }

    // Reads one type; `tag` receives its top-level tag.
    ffi_type* type(std::uint8_t& tag, int depth = 0) {
        if (depth > 32 || !byte(tag)) return nullptr;
        switch (tag) {
        case Void: return &ffi_type_void;
        case Bool: return &ffi_type_uint8;
        case Int8: return &ffi_type_sint8;
        case UInt8: return &ffi_type_uint8;
        case Int16: return &ffi_type_sint16;
        case UInt16: return &ffi_type_uint16;
        case Int32: return &ffi_type_sint32;
        case UInt32: return &ffi_type_uint32;
        case Int64: return &ffi_type_sint64;
        case UInt64: return &ffi_type_uint64;
        case Float: return &ffi_type_float;
        case Double: return &ffi_type_double;
        case Pointer: return &ffi_type_pointer;
        case Struct: {
            std::uint16_t count = 0;
            if (!u16(count) || count == 0) return nullptr;
            auto owned = std::make_unique<Owned>();
            owned->elements.reserve(static_cast<std::size_t>(count) + 1);
            for (std::uint16_t i = 0; i < count; ++i) {
                std::uint8_t member = 0;
                auto* element = type(member, depth + 1);
                if (element == nullptr || member == Void) return nullptr;
                owned->elements.push_back(element);
            }
            owned->elements.push_back(nullptr);
            owned->type.size = 0;
            owned->type.alignment = 0;
            owned->type.type = FFI_TYPE_STRUCT;
            owned->type.elements = owned->elements.data();
            auto* result = &owned->type;
            owned_.push_back(std::move(owned));
            return result;
        }
        default: return nullptr;
        }
    }

private:
    struct Owned {
        ffi_type type{};
        std::vector<ffi_type*> elements;
    };
    const std::string& bytes_;
    std::size_t position_ = 0;
    std::vector<std::unique_ptr<Owned>> owned_;
};

// The size of a type that libffi has laid out (ffi_prep_cif computes struct
// sizes); scalars have fixed sizes.
std::size_t size_of(ffi_type* type) { return type == &ffi_type_void ? 0 : type->size; }

bool is_integral(std::uint8_t tag) { return tag >= Bool && tag <= UInt64 && tag != Int64 && tag != UInt64; }

bool is_signed(std::uint8_t tag) { return tag == Int8 || tag == Int16 || tag == Int32; }

void* address_of(const HostValue& value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value.bits));
}

// The callbacks of the downcall in progress on this thread, for upcalls.
thread_local CallbackContext* active_callbacks = nullptr;

HostCompletion call(const HostRequest& request) {
    void* function = address_of(request.arguments.at(0));
    const auto& signature = request.arguments.at(1).bytes;
    const auto& arguments = request.arguments.at(2).bytes;
    if (function == nullptr) return fail(host_error::invalid_input);

    TypeReader reader(signature);
    std::uint8_t version = 0;
    std::uint16_t fixed = 0, count = 0;
    if (!reader.byte(version) || version != 1 || !reader.u16(fixed) || !reader.u16(count))
        return fail(host_error::invalid_input);
    std::uint8_t result_tag = 0;
    auto* result_type = reader.type(result_tag);
    if (result_type == nullptr) return fail(host_error::invalid_input);
    std::vector<ffi_type*> types;
    std::vector<std::uint8_t> tags;
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint8_t tag = 0;
        auto* type = reader.type(tag);
        if (type == nullptr || tag == Void) return fail(host_error::invalid_input);
        types.push_back(type);
        tags.push_back(tag);
    }
    if (!reader.done()) return fail(host_error::invalid_input);
    const bool variadic = fixed != 0xffff;
    if (variadic) {
        if (fixed > count || fixed == 0) return fail(host_error::invalid_input);
        // C's default argument promotions: a variadic argument is never a
        // float or an integer narrower than int.
        for (std::size_t i = fixed; i < tags.size(); ++i) {
            if (tags[i] == Float || tags[i] == Bool || tags[i] == Int8 || tags[i] == UInt8 || tags[i] == Int16 ||
                tags[i] == UInt16)
                return fail(host_error::invalid_input);
        }
    }

    ffi_cif cif{};
    const auto status = variadic
        ? ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, fixed, count, result_type, types.data())
        : ffi_prep_cif(&cif, FFI_DEFAULT_ABI, count, result_type, types.data());
    if (status != FFI_OK) return fail(host_error::unsupported);

    // Copy each argument into its own aligned buffer.
    std::vector<std::unique_ptr<std::max_align_t[]>> storage;
    std::vector<void*> values;
    std::size_t offset = 0;
    for (auto* type : types) {
        const auto size = size_of(type);
        if (offset + size > arguments.size()) return fail(host_error::invalid_input);
        const auto slots = (std::max<std::size_t>(size, 1) + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
        storage.push_back(std::make_unique<std::max_align_t[]>(slots));
        std::memcpy(storage.back().get(), arguments.data() + offset, size);
        values.push_back(storage.back().get());
        offset += size;
    }
    if (offset != arguments.size()) return fail(host_error::invalid_input);

    const auto result_size = size_of(result_type);
    const auto result_slots =
        (std::max<std::size_t>({result_size, sizeof(ffi_arg), 1}) + sizeof(std::max_align_t) - 1) /
        sizeof(std::max_align_t);
    auto result = std::make_unique<std::max_align_t[]>(result_slots);
    auto* outer = active_callbacks;
    active_callbacks = request.callbacks;
    ffi_call(&cif, FFI_FN(function), result.get(), values.data());
    active_callbacks = outer;

    std::string bytes(result_size, '\0');
    if (is_integral(result_tag)) {
        // libffi widens integral results narrower than a register to ffi_arg.
        if (is_signed(result_tag)) {
            ffi_sarg widened = 0;
            std::memcpy(&widened, result.get(), sizeof widened);
            const std::int64_t value = widened;
            if (result_tag == Int8) { const auto v = static_cast<std::int8_t>(value); std::memcpy(bytes.data(), &v, 1); }
            if (result_tag == Int16) { const auto v = static_cast<std::int16_t>(value); std::memcpy(bytes.data(), &v, 2); }
            if (result_tag == Int32) { const auto v = static_cast<std::int32_t>(value); std::memcpy(bytes.data(), &v, 4); }
        } else {
            ffi_arg widened = 0;
            std::memcpy(&widened, result.get(), sizeof widened);
            const std::uint64_t value = widened;
            if (result_tag == Bool || result_tag == UInt8) {
                const auto v = static_cast<std::uint8_t>(value);
                std::memcpy(bytes.data(), &v, 1);
            }
            if (result_tag == UInt16) { const auto v = static_cast<std::uint16_t>(value); std::memcpy(bytes.data(), &v, 2); }
            if (result_tag == UInt32) { const auto v = static_cast<std::uint32_t>(value); std::memcpy(bytes.data(), &v, 4); }
        }
    } else if (result_size > 0) {
        std::memcpy(bytes.data(), result.get(), result_size);
    }
    return HostCompletion::ok(bytes_value(std::move(bytes)));
}

// Writes `bytes`, the value of an integral result of `tag`, widened to the
// ffi_arg slot that libffi expects for closure results narrower than a
// register.
void store_result(void* slot, std::uint8_t tag, const std::string& bytes, std::size_t size) {
    if (!is_integral(tag)) {
        std::memcpy(slot, bytes.data(), size);
        return;
    }
    std::uint64_t raw = 0;
    std::memcpy(&raw, bytes.data(), std::min<std::size_t>(size, sizeof raw));
    if (is_signed(tag)) {
        const auto bits = static_cast<unsigned>(size * 8);
        auto value = static_cast<std::int64_t>(raw << (64 - bits)) >> (64 - bits);
        const auto widened = static_cast<ffi_sarg>(value);
        std::memcpy(slot, &widened, sizeof widened);
    } else {
        const auto widened = static_cast<ffi_arg>(raw);
        std::memcpy(slot, &widened, sizeof widened);
    }
}

} // namespace

// An upcall stub: a libffi closure that calls the program's
// neura.ffi.upcall callback with the stub's ID and the packed arguments.
struct Upcall {
    std::int64_t stub = 0;
    bool alive = true;
    std::string signature;
    std::unique_ptr<TypeReader> reader;
    ffi_cif cif{};
    std::vector<ffi_type*> types;
    std::vector<std::uint8_t> tags;
    ffi_type* result_type = nullptr;
    std::uint8_t result_tag = 0;
    ffi_closure* closure = nullptr;
    void* code = nullptr;

    ~Upcall() {
        if (closure != nullptr) ffi_closure_free(closure);
    }
};

void UpcallDeleter::operator()(Upcall* upcall) const { delete upcall; }

namespace {

void upcall_entry(ffi_cif*, void* result, void** arguments, void* data) {
    auto* upcall = static_cast<Upcall*>(data);
    if (active_callbacks == nullptr || !upcall->alive) {
        std::fputs(upcall->alive ? "neura: native code called an upcall stub outside a downcall\n"
                                 : "neura: native code called an upcall stub after its arena closed\n",
                   stderr);
        std::fflush(stderr);
        std::_Exit(134);
    }
    std::string packed;
    for (std::size_t i = 0; i < upcall->types.size(); ++i) {
        packed.append(static_cast<const char*>(arguments[i]), size_of(upcall->types[i]));
    }
    const auto size = size_of(upcall->result_type);
    const std::vector<HostValue> callback_arguments{int64_value(upcall->stub), bytes_value(std::move(packed))};
    const auto answer = active_callbacks->call("neura.ffi.upcall", 1, callback_arguments);
    if (!answer || answer->bytes.size() != size) {
        // The run ended in the callback, or it returned a malformed result:
        // give native code zeros; the run reports how it ended.
        std::memset(result, 0, std::max<std::size_t>(size, sizeof(ffi_arg)));
        return;
    }
    if (size > 0) store_result(result, upcall->result_tag, answer->bytes, size);
}

struct SizeAlign {
    std::size_t size;
    std::size_t align;
};

template <typename T>
SizeAlign layout_of() { return {sizeof(T), alignof(T)}; }

} // namespace

void add_ffi_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities) {
    auto& native = tables.native;
    const auto& granted = capabilities.native;

    // [triple, bytes]: size and alignment of bool, char, short, int, long,
    // long long, float, double, void*, size_t, and wchar_t, then 1 when plain
    // char is signed, then 1 when the byte order is little-endian.
    add(signature("neura.ffi.abi", {}, tuple({text(), byte_list()})), [&granted](const HostRequest&) {
        if (!granted) return denied();
        const SizeAlign layouts[] = {layout_of<bool>(),   layout_of<char>(),        layout_of<short>(),
                                     layout_of<int>(),    layout_of<long>(),        layout_of<long long>(),
                                     layout_of<float>(),  layout_of<double>(),      layout_of<void*>(),
                                     layout_of<std::size_t>(), layout_of<wchar_t>()};
        std::string bytes;
        for (const auto& layout : layouts) {
            bytes.push_back(static_cast<char>(layout.size));
            bytes.push_back(static_cast<char>(layout.align));
        }
        bytes.push_back(static_cast<char>(static_cast<char>(-1) < 0 ? 1 : 0));
        const std::uint16_t probe = 1;
        std::uint8_t first = 0;
        std::memcpy(&first, &probe, 1);
        bytes.push_back(static_cast<char>(first));
        return HostCompletion::ok(tuple_value({bytes_value(target_triple()), bytes_value(std::move(bytes))}));
    });

    add(signature("neura.ffi.arena.open", {}, int64()), [&granted, &native](const HostRequest&) {
        if (!granted) return denied();
        if (native.arenas.size() >= max_open_ids) return fail(host_error::other);
        const auto id = native.next_arena++;
        native.arenas.emplace(id, StandardServices::Tables::Arena{});
        return HostCompletion::ok(int64_value(id));
    });
    add(signature("neura.ffi.arena.close", {int64()}, void_type()), [&granted, &native](const HostRequest& request) {
        if (!granted) return denied();
        const auto found = native.arenas.find(as_int64(request.arguments.at(0)));
        if (found == native.arenas.end()) return fail(host_error::invalid_input);
        for (auto* upcall : found->second.upcalls) upcall->alive = false;
        for (auto* block : found->second.blocks) std::free(block);
        for (auto* library : found->second.libraries) ::dlclose(library);
        native.arenas.erase(found);
        return HostCompletion::ok();
    });
    // alloc(arena, size, alignment): zeroed memory freed when the arena
    // closes, or at the end of the run for arena 0 (the global arena).
    add(signature("neura.ffi.alloc", {int64(), int64(), int64()}, int64()),
        [&granted, &native](const HostRequest& request) {
            if (!granted) return denied();
            const auto id = as_int64(request.arguments.at(0));
            const auto size = as_int64(request.arguments.at(1));
            const auto align = as_int64(request.arguments.at(2));
            if (size < 0 || size > max_allocation_bytes || align < 1 || (align & (align - 1)) != 0 || align > 4096)
                return fail(host_error::invalid_input);
            auto* arena = id == 0 ? &native.global : nullptr;
            if (arena == nullptr) {
                const auto found = native.arenas.find(id);
                if (found == native.arenas.end()) return fail(host_error::invalid_input);
                arena = &found->second;
            }
            const auto alignment = std::max<std::size_t>(static_cast<std::size_t>(align), alignof(std::max_align_t));
            const auto rounded = (std::max<std::size_t>(static_cast<std::size_t>(size), 1) + alignment - 1) /
                                 alignment * alignment;
            void* block = std::aligned_alloc(alignment, rounded);
            if (block == nullptr) return fail(host_error::other);
            std::memset(block, 0, rounded);
            arena->blocks.push_back(block);
            return HostCompletion::ok(int64_value(static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(block))));
        });

    const auto library_id = [&native](void* handle, std::int64_t arena_id) {
        const auto id = native.next_library++;
        native.libraries.emplace(id, handle);
        if (arena_id == 0) native.global.libraries.push_back(handle);
        else native.arenas.at(arena_id).libraries.push_back(handle);
        return HostCompletion::ok(int64_value(id));
    };
    // library.open(arena, path): a path containing '/' loads that file;
    // another name is searched the way the platform's loader searches.
    add(signature("neura.ffi.library.open", {int64(), text()}, int64()),
        [&granted, &native, library_id](const HostRequest& request) {
            if (!granted) return denied();
            const auto arena = as_int64(request.arguments.at(0));
            const auto& path = request.arguments.at(1).bytes;
            if (!usable_text(path) || (arena != 0 && !native.arenas.contains(arena)))
                return fail(host_error::invalid_input);
            void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (handle == nullptr) return fail(host_error::not_found);
            return library_id(handle, arena);
        });
    // The libraries already loaded into the process, including the C library.
    add(signature("neura.ffi.library.default", {}, int64()), [&granted, &native](const HostRequest&) {
        if (!granted) return denied();
        native.libraries.emplace(0, RTLD_DEFAULT);
        return HostCompletion::ok(int64_value(0));
    });
    // The image's embedded native library (NVR NATV section), loaded once.
    add(signature("neura.ffi.library.embedded", {}, int64()), [&granted, &native, library_id](const HostRequest&) {
        if (!granted) return denied();
        if (native.embedded_id != 0) return HostCompletion::ok(int64_value(native.embedded_id));
        if (native.embedded_object == nullptr || native.embedded_object->empty()) return fail(host_error::not_found);
        void* handle = load_embedded_object(*native.embedded_object);
        if (handle == nullptr) return fail(host_error::other);
        auto completion = library_id(handle, 0);
        native.embedded_id = as_int64(completion.value);
        return completion;
    });
    add(signature("neura.ffi.symbol", {int64(), text()}, int64()), [&granted, &native](const HostRequest& request) {
        if (!granted) return denied();
        const auto found = native.libraries.find(as_int64(request.arguments.at(0)));
        const auto& name = request.arguments.at(1).bytes;
        if (found == native.libraries.end() || !usable_text(name)) return fail(host_error::invalid_input);
        void* address = ::dlsym(found->second, name.c_str());
        if (address == nullptr) return fail(host_error::not_found);
        return HostCompletion::ok(int64_value(static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(address))));
    });

    add(signature("neura.ffi.read", {int64(), int64()}, byte_list()), [&granted](const HostRequest& request) {
        if (!granted) return denied();
        const auto* address = static_cast<const char*>(address_of(request.arguments.at(0)));
        const auto length = as_int64(request.arguments.at(1));
        if (length < 0 || length > max_transfer_bytes || (address == nullptr && length > 0))
            return fail(host_error::invalid_input);
        return HostCompletion::ok(bytes_value(std::string(address, static_cast<std::size_t>(length))));
    });
    add(signature("neura.ffi.write", {int64(), byte_list()}, void_type()), [&granted](const HostRequest& request) {
        if (!granted) return denied();
        auto* address = static_cast<char*>(address_of(request.arguments.at(0)));
        const auto& bytes = request.arguments.at(1).bytes;
        if (static_cast<std::int64_t>(bytes.size()) > max_transfer_bytes || (address == nullptr && !bytes.empty()))
            return fail(host_error::invalid_input);
        std::memcpy(address, bytes.data(), bytes.size());
        return HostCompletion::ok();
    });
    // strlen(address, max): the bytes before the first NUL, at most max.
    add(signature("neura.ffi.strlen", {int64(), int64()}, int64()), [&granted](const HostRequest& request) {
        if (!granted) return denied();
        const auto* address = static_cast<const char*>(address_of(request.arguments.at(0)));
        const auto max = as_int64(request.arguments.at(1));
        if (address == nullptr || max < 0) return fail(host_error::invalid_input);
        return HostCompletion::ok(int64_value(static_cast<std::int64_t>(
            ::strnlen(address, static_cast<std::size_t>(std::min(max, max_transfer_bytes))))));
    });

    add(signature("neura.ffi.call", {int64(), byte_list(), byte_list()}, byte_list()),
        [&granted](const HostRequest& request) {
            if (!granted) return denied();
            return call(request);
        });

    // upcall.create(arena, stub, signature): a native function pointer that
    // calls the program's neura.ffi.upcall callback with `stub`, valid while
    // the arena is open and only during a downcall. Arena 0 is global.
    add(signature("neura.ffi.upcall.create", {int64(), int64(), byte_list()}, int64()),
        [&granted, &native](const HostRequest& request) {
            if (!granted) return denied();
            const auto arena_id = as_int64(request.arguments.at(0));
            auto* arena = arena_id == 0 ? &native.global : nullptr;
            if (arena == nullptr) {
                const auto found = native.arenas.find(arena_id);
                if (found == native.arenas.end()) return fail(host_error::invalid_input);
                arena = &found->second;
            }
            std::unique_ptr<Upcall, UpcallDeleter> upcall(new Upcall());
            upcall->stub = as_int64(request.arguments.at(1));
            upcall->signature = request.arguments.at(2).bytes;
            upcall->reader = std::make_unique<TypeReader>(upcall->signature);
            auto& reader = *upcall->reader;
            std::uint8_t version = 0;
            std::uint16_t fixed = 0, count = 0;
            if (!reader.byte(version) || version != 1 || !reader.u16(fixed) || fixed != 0xffff || !reader.u16(count))
                return fail(host_error::invalid_input);
            upcall->result_type = reader.type(upcall->result_tag);
            if (upcall->result_type == nullptr) return fail(host_error::invalid_input);
            for (std::uint16_t i = 0; i < count; ++i) {
                std::uint8_t tag = 0;
                auto* type = reader.type(tag);
                if (type == nullptr || tag == Void) return fail(host_error::invalid_input);
                upcall->types.push_back(type);
                upcall->tags.push_back(tag);
            }
            if (!reader.done()) return fail(host_error::invalid_input);
            if (ffi_prep_cif(&upcall->cif, FFI_DEFAULT_ABI, count, upcall->result_type, upcall->types.data()) !=
                FFI_OK)
                return fail(host_error::unsupported);
            upcall->closure = static_cast<ffi_closure*>(ffi_closure_alloc(sizeof(ffi_closure), &upcall->code));
            if (upcall->closure == nullptr) return fail(host_error::other);
            if (ffi_prep_closure_loc(upcall->closure, &upcall->cif, upcall_entry, upcall.get(), upcall->code) !=
                FFI_OK)
                return fail(host_error::unsupported);
            const auto address = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(upcall->code));
            arena->upcalls.push_back(upcall.get());
            native.upcalls.push_back(std::move(upcall));
            return HostCompletion::ok(int64_value(address));
        });
}

void* load_embedded_object(const std::string& object) {
    std::string path;
    int descriptor = -1;
#ifdef __linux__
    descriptor = ::memfd_create("neura-native", MFD_CLOEXEC);
    if (descriptor >= 0) path = "/proc/self/fd/" + std::to_string(descriptor);
#endif
    if (descriptor < 0) {
        const char* directory = std::getenv("TMPDIR");
        std::string pattern = std::string(directory != nullptr && *directory != '\0' ? directory : "/tmp") +
                              "/neura-native-XXXXXX";
        descriptor = ::mkstemp(pattern.data());
        if (descriptor < 0) return nullptr;
        path = pattern;
    }
    std::size_t written = 0;
    while (written < object.size()) {
        const auto count = ::write(descriptor, object.data() + written, object.size() - written);
        if (count <= 0) {
            ::close(descriptor);
            return nullptr;
        }
        written += static_cast<std::size_t>(count);
    }
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (path.rfind("/proc/self/fd/", 0) != 0) ::unlink(path.c_str());
    ::close(descriptor);
    return handle;
}

void close_native(StandardServices::Tables& tables) {
    auto& native = tables.native;
    for (auto& [id, arena] : native.arenas) {
        for (auto* block : arena.blocks) std::free(block);
        for (auto* library : arena.libraries) ::dlclose(library);
    }
    native.arenas.clear();
    for (auto* block : native.global.blocks) std::free(block);
    for (auto* library : native.global.libraries) ::dlclose(library);
    native.global = {};
    native.libraries.clear();
    native.upcalls.clear();
    native.next_arena = 1;
    native.next_library = 1;
    native.embedded_id = 0;
}

#endif

} // namespace neura::nvm::provider

namespace neura::nvm {

std::string native_target_triple() { return provider::native_triple(); }

} // namespace neura::nvm

// How a kernel of the generated table (kernel_table.hpp) binds to a Neura
// function, shared by the stages that must agree on it: lowering checks a
// `kernel("name", version)` declaration against it, the loader checks an
// image's KERN entries, and the runtime calls the kernel by it. The rules are
// stated in docs/runtime/Kernels.md ("Binding a kernel"); scripts/gen-kernels.py
// applies the same rules when it writes the declarations.
//
// Each C parameter takes one role:
//  - Argument: a Neura parameter, in C order. A scalar is the Neura scalar of
//    the same width; a text view is a string; a bytes view or a state is a
//    uint8 array or list; a T view or writable T view is a T array or list.
//  - Scratch: a writable view with a NEURA_KERNEL_BUFFER size. The runtime
//    supplies it; it is not a Neura parameter.
//  - Output: a text or bytes output, or a scalar result pointer: the value
//    the Neura function returns.
//  - ErrorOffset: the `error_offset` result pointer of a kernel that can
//    return NEURA_ERR_* statuses; its Neura result is an outcome tuple.
// A kernel with a work budget, or with more than one value to return, has no
// binding yet.
#pragma once

#include "kernel_table.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace neura::kernels {

enum class Role : std::uint8_t { Argument, Scratch, Output, ErrorOffset };

// What the Neura function returns.
enum class ResultShape : std::uint8_t {
    Void,     // nothing: a status-only kernel
    Scalar,   // value_kind: the scalar kind (I8 ... F64, Bool)
    Text,     // a string from a text output
    Bytes,    // a uint8[list] from a bytes output
    // A tuple [uint32 status, int64 error offset] followed by the value, if
    // the kernel has one (value_kind Status when it has none): status 0 and
    // offset 0 for success, the NEURA_ERR_* code and the kernel's offset for
    // an error, with the value then zero or empty.
    Outcome,
};

struct Binding {
    const KernelInfo* info = nullptr;
    // Why the kernel cannot be bound, or null.
    const char* problem = nullptr;
    std::vector<Role> roles;
    // The C parameter of each Neura parameter, in order.
    std::vector<std::size_t> arguments;
    ResultShape result = ResultShape::Void;
    // The scalar kind of a Scalar result or of an Outcome's value; OutText or
    // OutBytes for a text or bytes value; Status for none.
    Kind value_kind = Kind::Status;
    // The C parameter that receives the value, or `none` when the C result is
    // the value. The error offset's parameter, or `none`.
    static constexpr std::size_t none = static_cast<std::size_t>(-1);
    std::size_t value_param = none;
    std::size_t error_param = none;
};

inline bool scalar_kind(Kind kind) { return kind >= Kind::I8 && kind <= Kind::Bool; }
inline bool view_kind(Kind kind) { return kind >= Kind::ViewI8 && kind <= Kind::ViewF64; }
inline bool mut_kind(Kind kind) { return kind >= Kind::MutI8 && kind <= Kind::MutF64; }
inline bool result_pointer_kind(Kind kind) { return kind >= Kind::ResultI8 && kind <= Kind::ResultBool; }

// The scalar kind of a T view, writable T view, or T result pointer.
inline Kind element_kind(Kind kind) {
    if (view_kind(kind)) return static_cast<Kind>(static_cast<int>(Kind::I8) + (static_cast<int>(kind) - static_cast<int>(Kind::ViewI8)));
    if (mut_kind(kind)) return static_cast<Kind>(static_cast<int>(Kind::I8) + (static_cast<int>(kind) - static_cast<int>(Kind::MutI8)));
    if (result_pointer_kind(kind))
        return static_cast<Kind>(static_cast<int>(Kind::I8) + (static_cast<int>(kind) - static_cast<int>(Kind::ResultI8)));
    if (kind == Kind::Bytes || kind == Kind::State) return Kind::U8;
    return kind;
}

// Width in bits of a scalar kind; 8 for Bool.
inline unsigned scalar_width(Kind kind) {
    switch (kind) {
    case Kind::I8: case Kind::U8: case Kind::Bool: return 8;
    case Kind::I16: case Kind::U16: return 16;
    case Kind::I32: case Kind::U32: case Kind::F32: return 32;
    default: return 64;
    }
}
inline bool signed_kind(Kind kind) {
    return kind == Kind::I8 || kind == Kind::I16 || kind == Kind::I32 || kind == Kind::I64;
}
inline bool float_kind(Kind kind) { return kind == Kind::F32 || kind == Kind::F64; }

// The Neura spelling of a scalar kind.
inline std::string_view neura_scalar_name(Kind kind) {
    switch (kind) {
    case Kind::I8: return "int8";
    case Kind::I16: return "int16";
    case Kind::I32: return "int32";
    case Kind::I64: return "int64";
    case Kind::U8: return "uint8";
    case Kind::U16: return "uint16";
    case Kind::U32: return "uint32";
    case Kind::U64: return "uint64";
    case Kind::F32: return "float32";
    case Kind::F64: return "float64";
    case Kind::Bool: return "bool";
    default: return "?";
    }
}

inline bool buffered(const KernelInfo& info, std::size_t param) {
    for (std::size_t i = 0; i < info.buffer_count; ++i)
        if (info.buffers[i].param == param) return true;
    return false;
}

inline Binding plan_binding(const KernelInfo& info) {
    Binding binding;
    binding.info = &info;
    binding.roles.resize(info.param_count, Role::Argument);
    for (std::size_t i = 0; i < info.param_count; ++i) {
        const auto kind = info.params[i];
        const bool error_offset = kind == Kind::ResultI64 && std::strcmp(info.param_names[i], "error_offset") == 0;
        if (kind == Kind::Work) {
            binding.problem = "it takes a work budget, which the runtime does not supply yet";
        } else if (error_offset) {
            binding.roles[i] = Role::ErrorOffset;
            binding.error_param = i;
        } else if (kind == Kind::OutText || kind == Kind::OutBytes || result_pointer_kind(kind)) {
            binding.roles[i] = Role::Output;
            if (binding.value_param != Binding::none) binding.problem = "it returns more than one value";
            binding.value_param = i;
        } else if (mut_kind(kind) && buffered(info, i)) {
            binding.roles[i] = Role::Scratch;
        } else {
            binding.arguments.push_back(i);
        }
    }
    if (info.result != Kind::Status) {
        if (binding.value_param != Binding::none) binding.problem = "it returns more than one value";
        binding.value_kind = info.result;
    } else if (binding.value_param != Binding::none) {
        binding.value_kind = info.params[binding.value_param];
        if (result_pointer_kind(binding.value_kind)) binding.value_kind = element_kind(binding.value_kind);
    }
    if (binding.error_param != Binding::none) {
        binding.result = ResultShape::Outcome;
    } else if (binding.value_kind == Kind::OutText) {
        binding.result = ResultShape::Text;
    } else if (binding.value_kind == Kind::OutBytes) {
        binding.result = ResultShape::Bytes;
    } else if (binding.value_kind != Kind::Status) {
        binding.result = ResultShape::Scalar;
    }
    return binding;
}

// The kernel with this name and ABI version, or null.
inline const KernelInfo* find_kernel(std::string_view name, std::uint32_t version) {
    for (const auto& info : kKernels) {
        if (info.version == version && name == info.name) return &info;
    }
    return nullptr;
}

// Whether any kernel has this name, at any version.
inline bool known_kernel_name(std::string_view name) {
    for (const auto& info : kKernels) {
        if (name == info.name) return true;
    }
    return false;
}

}  // namespace neura::kernels

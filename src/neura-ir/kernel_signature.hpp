#pragma once

// The Neura side of a kernel binding (docs/runtime/Kernels.md, "Binding a
// kernel"): which functions a C kernel implements, and whether a function's
// Neura signature is the one its kernel's parameter and result kinds give.

#include "attributes.hpp"
#include "ir.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neura {

// The kernel name and ABI version of a function bound with
// `kernel("name", version)`, or nothing.
std::optional<std::pair<std::string, std::uint32_t>> kernel_binding(const IrSymbol& symbol);

// True for a bodyless function that a kernel implements.
inline bool kernel_bound(const IrSymbol& symbol) {
    return !symbol.has_body && has_attribute(symbol, AttributeId::KernelBinding);
}

// For each Neura parameter of a kernel-bound function, whether the kernel
// writes the list or array passed for it in place: a writable view (neura_mut_*)
// or a state (neura_state*). Every other parameter is only read, and a value
// the kernel returns is storage the runtime allocates for the call
// (docs/runtime/Kernels.md). Nothing when the binding names no kernel this
// compiler knows, so a caller must assume that every argument is written.
std::optional<std::vector<bool>> kernel_writes(const IrSymbol& function);

// Whether this compiler's kernel table has the kernel at this ABI version.
bool kernel_known(const std::string& name, std::uint32_t version);

// The reason the declaration cannot bind the kernel: an unknown name or
// version, a kernel without a binding, or a signature other than the one
// the kernel's kinds give (which the reason spells out). Nothing when it
// matches.
std::optional<std::string> kernel_signature_problem(const IrProgram& program, const IrSymbol& function,
                                                    const std::string& name, std::uint32_t version);

} // namespace neura

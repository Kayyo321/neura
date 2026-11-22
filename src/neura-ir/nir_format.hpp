#pragma once

#include "ir.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace neura {

// The versioned .nir file: NeuraIR before optimization. docs/NirFormat.md
// specifies the encoding. Reading what write_nir produced yields a program
// equal to the original in every symbol, type, region, operand, and span.
inline constexpr std::uint16_t nir_major_version = 1;
// ConformanceCall (docs/design/Records030.md) is written under this minor
// version, assigned to static interface requirements.
inline constexpr std::uint16_t nir_records_minor = 13;
// NeuraIR 1.12, the matching milestone (docs/design/Matching030.md): a
// return inside a scoped value, which the postfix `?` lowers to, and the
// type `any` with its operations. A file uses it only when its program
// does.
inline constexpr std::uint16_t nir_any_minor = 12;
// NeuraIR 1.14, the lifetime milestone (docs/design/Lifetime030.md): the
// operations Cleanup, Pending and Yield and the attribute generator.resume.
// A file uses it only when its program does.
inline constexpr std::uint16_t nir_lifetime_minor = 14;
// NeuraIR 1.15, the 0.3.0 expressions (docs/design/Expressions030.md): the
// operations ConcatText (tag 73) and TypeOf (tag 74). A file uses it only
// when its program does.
inline constexpr std::uint16_t nir_expressions_minor = 15;
// NeuraIR 1.16 adds proof-only law records and no executable operations.
inline constexpr std::uint16_t nir_laws_minor = 16;
inline constexpr std::uint16_t nir_narrowing_minor = 16;
// NeuraIR 1.17 adds the private Cell-storage operation and view failures.
inline constexpr std::uint16_t nir_pointer_minor = 17;
// The highest minor version this code reads and writes. The writer emits the
// lowest minor version that can represent the program, so a program that
// uses no newer feature is written as 1.0.
inline constexpr std::uint16_t nir_minor_version = nir_pointer_minor;

// The minor version that adds class hooks (docs/design/Hooks030.md): the
// attributes hook.get, hook.set and hook.of. A module that declares a clause
// is written with it, so that an older reader refuses the module.
inline constexpr std::uint16_t nir_hooks_minor = 11;

std::vector<std::uint8_t> write_nir(const IrProgram& program);

class NirError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Decodes a .nir file and checks its structure: magic, version, enum tags,
// symbol and file references, nesting depth, the attribute registry's rules
// (attributes.def), and that every byte is read.
// Semantic checks belong to the NeuraIR verifier, which the build runs on
// the result. Throws NirError naming `origin`.
IrProgram read_nir(std::span<const std::uint8_t> bytes, const std::string& origin,
                   std::uint16_t* minor_version = nullptr, std::size_t proof_fuel = default_proof_fuel);

// True when `bytes` starts with the .nir magic.
bool is_nir(std::span<const std::uint8_t> bytes);

} // namespace neura

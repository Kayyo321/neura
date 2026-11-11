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
// The highest minor version this code reads and writes. The writer emits the
// lowest minor version that can represent the program, so a program that
// uses no newer feature is written as 1.0.
inline constexpr std::uint16_t nir_minor_version = 10;

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
                   std::uint16_t* minor_version = nullptr);

// True when `bytes` starts with the .nir magic.
bool is_nir(std::span<const std::uint8_t> bytes);

} // namespace neura

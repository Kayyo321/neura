#pragma once

#include "nvr/image.hpp"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace neura::decompiler {

// The lossless text form of an NVR image (docs/Decompiler.md, Assembly).
// Every record of every table appears in table order with its index, so the
// text names each field the encoding holds: types, constants, layouts,
// globals, graphs with every node and weighted edge, imports, functions with
// their frames, blocks, inputs, selectors, and exits, the entry, the source
// map, and optional sections this code does not interpret, as hex.
//
// assemble(disassemble(image)) == image for every image, and because the
// encoding of a valid image is canonical, assembling the text of a loaded
// image and encoding it reproduces the original bytes.
std::string disassemble(const nvr::Image& image);

class AssemblyError : public std::runtime_error {
public:
    AssemblyError(std::size_t line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message), line(line) {}

    std::size_t line;
};

// Parses the text form. Checks the syntax and that every record's index is
// the next one of its table; the image contract is checked by encoding the
// result and loading it with the shared loader. Throws AssemblyError.
nvr::Image assemble(std::string_view text);

} // namespace neura::decompiler

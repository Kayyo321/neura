#pragma once

#include "lift/expression.hpp"
#include "nvr/image.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neura::decompiler {

// The typed operations recovered from a block's graph (src/lift).
using lift::Expression;
using lift::ExpressionPtr;

// How the meaning of a block's graph was established.
enum class Evidence {
    // The graph has no neurons and passes inputs through: nothing to lift.
    None,
    // Regenerating the block from the lifted operations with the compiler's
    // circuit library produced the identical graph and exits.
    Structural,
    // The lifted operations agree with the graph on every input.
    Exhaustive,
    // Not lifted: the graph is shown as a circuit.
    Circuit,
};

// The lifted form of a block (values, condition, checks, dispatch) and the
// evidence the decompiler reports for it.
struct LiftedBlock : lift::LiftedForm {
    Evidence evidence = Evidence::None;
    // Number of input lanes an exhaustive check covered.
    std::size_t lanes_checked = 0;
};

struct LiftOptions {
    // Blocks with at most this many input lanes may be lifted by exhaustive
    // evaluation when no structural match exists.
    std::size_t exhaustive_lanes = 20;
};

// Lifts one block of a loaded, validated image. Uses only the executable
// tables and graphs; optional sections never inform the result.
LiftedBlock lift_block(const nvr::Image& image, std::uint32_t function, std::uint32_t block,
                       const LiftOptions& options = {});

// Regenerates a lifted block's graph and exits from its lifted form, as the
// structural proof does. `graph` receives the regenerated graph.
std::optional<nvr::Block> regenerate(const nvr::Image& image, std::uint32_t function, std::uint32_t block,
                                     const LiftedBlock& lifted, nvr::Graph& graph);

std::string render(const nvr::Image& image, const Expression& expression,
                   const std::vector<std::string>& input_names);

} // namespace neura::decompiler

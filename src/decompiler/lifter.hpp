#pragma once

#include "nvr/image.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace neura::decompiler {

// A typed operation recovered from a block's graph. Leaves are block inputs
// and immediates; inner nodes are the operations of docs/lang/Numbers.md.
struct Expression {
    enum class Kind { Input, Immediate, Unary, Binary, Convert };

    Kind kind = Kind::Input;
    // Image type of the result.
    std::uint32_t type = 0;
    // "+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">=", "&&", "||",
    // "!", "neg", "index" (a signed index widened to uint64 after a bounds
    // check), and "+~", an addition whose overflow is not checked, as in
    // generated code that proves it cannot overflow.
    std::string op;
    std::vector<std::shared_ptr<const Expression>> operands;
    // Input: position in the block's input list.
    std::size_t input = 0;
    // Immediate: the value's slot encoding.
    std::uint64_t bits = 0;
};

using ExpressionPtr = std::shared_ptr<const Expression>;

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

struct LiftedBlock {
    Evidence evidence = Evidence::None;
    // The operation behind each value an exit reads from the graph, keyed by
    // the value's first output lane.
    std::map<std::uint32_t, ExpressionPtr> values;
    // For a block that selects among exits by a condition: the exit taken
    // when the condition holds, and the condition. Exits not covered, such
    // as traps, are taken when their checks fail.
    std::optional<std::size_t> exit_when_true;
    ExpressionPtr condition;
    // Checks whose failure selects a trap exit: (reason, operation whose
    // failure it is), in evaluation order.
    std::vector<std::pair<std::uint32_t, ExpressionPtr>> checks;
    // Dispatch blocks: the layout each Call exit is taken for.
    std::map<std::size_t, std::vector<std::uint32_t>> dispatch;
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

#pragma once

#include "expression.hpp"
#include "nvr/image.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace neura::lift {

using Checks = std::vector<std::pair<std::uint32_t, ExpressionPtr>>;

// The structural proof. Regenerates `block` from the lifted form with the
// compiler's circuit library, the way the converter builds it (operands
// first, left to right, each check right after the operation that can fail,
// the condition last), and holds only when the regenerated block equals
// `block` and its graph equals `graph`. Nothing weaker is accepted: no
// sampling, no exhaustive evaluation, no heuristics. The image supplies the
// type table; `graph` is normally image.graphs[block.graph], and a test can
// pass a modified copy.
//
// `checks`, when given, receives the checks in evaluation order: (reason,
// value or condition whose computation registered it).
bool prove_structure(const nvr::Image& image, const nvr::Block& block, const nvr::Graph& graph,
                     const LiftedForm& form, Checks* checks = nullptr);

// Rebuilds a block from lifted operations: inputs in their order, values in
// the given order, the condition last, exits as `original` lists them.
// `graph` receives the regenerated graph. Returns nothing when the circuit
// library cannot assemble the block.
std::optional<nvr::Block> replay(const nvr::Image& image, const nvr::Block& original,
                                 const std::vector<std::pair<std::uint32_t, ExpressionPtr>>& values,
                                 const ExpressionPtr& condition, std::optional<std::size_t> exit_when_true,
                                 nvr::Graph& graph, Checks* checks);

// replay() compared with `original` and `graph`.
bool replay_matches(const nvr::Image& image, const nvr::Block& original, const nvr::Graph& graph,
                    const std::vector<std::pair<std::uint32_t, ExpressionPtr>>& values, const ExpressionPtr& condition,
                    std::optional<std::size_t> exit_when_true, Checks* checks);

// The graph of a dispatch block that takes Call exit e for each layout in
// `exits[e]` and the last exit (an invalid-dispatch trap) otherwise, built
// the way the converter builds dispatch blocks.
nvr::Graph dispatch_circuit(const nvr::Block& block, const std::map<std::size_t, std::vector<std::uint32_t>>& exits);

// The circuit of one expression over input words of the given widths, as a
// graph whose outputs are the expression's lanes. Checks are not emitted.
nvr::Graph expression_circuit(const nvr::Image& image, const std::vector<unsigned>& input_widths,
                              const Expression& expression);

} // namespace neura::lift

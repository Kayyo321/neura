#pragma once

#include "expression.hpp"
#include "nvr/image.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace neura::lift {

// Structural recognition of a block: a search for the typed operations
// behind its graph. Candidates come from sampling the graph by reference
// evaluation, so they are only guesses; a candidate is returned as Lifted
// only after the structural proof (proof.hpp) holds for it. Uses only the
// executable tables and graphs; optional sections never inform the result.
struct Recognition {
    enum class Status {
        // The graph has no neurons, no exit reads it, and the block has at
        // most one exit: there is nothing to lift.
        Trivial,
        // A form whose regeneration reproduces the block and its graph.
        Lifted,
        // No form was found; `reason` says where the search stopped.
        Unrecognized,
    };

    Status status = Status::Unrecognized;
    LiftedForm form;
    const char* reason = "";
    // Set when the search reached the replay stage without a structural
    // match: the candidates found for each value, in lane order. A caller
    // may establish one of them by other evidence.
    bool searched = false;
    std::vector<std::pair<std::uint32_t, std::vector<ExpressionPtr>>> candidates;
};

struct RecognizeOptions {
    // Also search bitwise operations and shifts of integers ("&", "|", "^",
    // "~", "<<", ">>"). The decompiler's search leaves them out, so its
    // output does not depend on this library's runtime use.
    bool extended = false;
    // The search gives up, as unrecognized, once this time has passed. A
    // runtime that lifts lazily bounds its time per image with it; the
    // search's result never depends on it otherwise.
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};

Recognition recognize(const nvr::Image& image, std::uint32_t function, std::uint32_t block,
                      const RecognizeOptions& options = {});

// Reference evaluation of a graph on input lanes, with the checked
// arithmetic of graph profile 1: the output lanes, or nothing when the
// evaluation overflows. The recognizer samples graphs with it; the lift
// library has no dependency on the NVM.
std::optional<std::vector<std::int64_t>> evaluate_reference(const nvr::Graph& graph,
                                                            const std::vector<std::int64_t>& inputs);

} // namespace neura::lift

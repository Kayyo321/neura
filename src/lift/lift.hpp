#pragma once

#include "expression.hpp"
#include "lowered_block.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neura::lift {

// Tier 1 (docs/runtime/NVM.md, "Execution tiers"): a block whose graph is
// proven to compute typed operations, compiled to native 64-bit code.
//
// A block is accepted only when both proofs hold:
// - structural: regenerating the block from its lifted form with the
//   compiler's circuit library yields the identical block and graph
//   (proof.hpp);
// - overflow: reference evaluation of the graph cannot overflow for any 0/1
//   input, and every output lane an exit can read (the selector and each
//   value) is exactly 0 or 1 (overflow.hpp).
// The lowering must also exist: every operation has an exact native form.
struct LiftResult {
    std::shared_ptr<const LoweredBlock> block;
    // When `block` is empty: why the block stays on the reference evaluator.
    std::string rejection;
};

// Lifts, proves, and lowers block `block` of function `function` of a loaded,
// validated image. With `hint`, the recognizer's search is skipped and the
// hint is proven like any other candidate: a hint is never trusted.
LiftResult lift_and_prove(const nvr::Image& image, std::uint32_t function, std::uint32_t block,
                          const LiftedForm* hint = nullptr);

// The accepted lowered block, or nothing.
std::optional<LoweredBlock> lift_and_prove_block(const nvr::Image& image, std::uint32_t function,
                                                 std::uint32_t block);

// What a block's lowering and proofs depend on besides its graph: input
// types and whether inputs repeat, the selector, and each exit's kind, trap
// reason, and the lanes and types it reads. Blocks with the same graph and
// key lift to the same result; slot numbers, constants, targets, and the
// function do not matter.
std::string lift_key(const nvr::Image& image, const nvr::Block& block);

// Results per (graph, lift_key), for one image. Thread-safe.
class LiftCache {
public:
    explicit LiftCache(const nvr::Image& image) : image_(image) {}

    const LiftResult& get(std::uint32_t function, std::uint32_t block);

    // Distinct (graph, key) pairs lifted so far.
    std::size_t size() const;

private:
    const nvr::Image& image_;
    mutable std::mutex mutex_;
    std::map<std::pair<std::uint32_t, std::string>, std::unique_ptr<LiftResult>> results_;
};

} // namespace neura::lift

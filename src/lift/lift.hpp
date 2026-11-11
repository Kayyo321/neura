#pragma once

#include "expression.hpp"
#include "lowered_block.hpp"
#include "nvr/image.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
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
    // The overflow analysis ran out of the work it was given before it
    // decided (`rejection` is exhausted_work): more work may prove the
    // block. `form` is then the form whose structural proof held, so that a
    // retry need not search again.
    bool exhausted = false;
    std::shared_ptr<const LiftedForm> form;
};

inline constexpr const char* exhausted_work = "the overflow analysis ran out of work";

// The overflow proof and the 0/1 proof of every output lane depend on the
// graph alone. A cache shared by the lifts of one image proves each graph
// once. Thread-safe.
class GraphProofs {
public:
    // Empty when the graph's proofs hold, else the rejection. With `work`,
    // the case analysis is bounded (overflow.hpp); a verdict that ran out of
    // work is exhausted_work and is not remembered.
    std::string verdict(const nvr::Graph& graph, std::uint32_t index,
                        std::uint64_t work = std::numeric_limits<std::uint64_t>::max());

private:
    std::mutex mutex_;
    std::map<std::uint32_t, std::string> verdicts_;
    // The most work a graph's analysis has run out of: less cannot decide it.
    std::map<std::uint32_t, std::uint64_t> exhausted_;
};

// Lifts, proves, and lowers block `block` of function `function` of a loaded,
// validated image. With `hint`, the recognizer's search is skipped and the
// hint is proven like any other candidate: a hint is never trusted. A search
// that passes `deadline` is abandoned, as a rejection that says so. `proofs`
// shares graph proofs between the blocks of an image, and `work` bounds the
// overflow analysis of a graph not yet proven.
LiftResult lift_and_prove(const nvr::Image& image, std::uint32_t function, std::uint32_t block,
                          const LiftedForm* hint = nullptr,
                          std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
                          GraphProofs* proofs = nullptr,
                          std::uint64_t work = std::numeric_limits<std::uint64_t>::max());

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

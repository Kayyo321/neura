#pragma once

#include "lift/hints.hpp"
#include "lift/lift.hpp"
#include "lift/lowered_block.hpp"
#include "nvr/image.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace neura::nvm {

// Tier 1 of the executor (docs/runtime/NVM.md, "Execution tiers"): blocks
// whose graphs are proven to compute typed operations run as native 64-bit
// code. Which blocks run lifted never changes what a run does, only how fast
// it runs, so the choices below (thresholds, the time budget, and races
// between threads lifting the same block) affect timing and the statistics
// alone.
struct TierOptions {
    // Executions of a block with a LIFT hint before the hint is proven, and
    // of a block without one before its form is searched for, counted over
    // every run that shares the cache (the tests of `neura test`, the
    // members of a fleet or of `run --each`, and parallel items). Proving a
    // hint (regenerating the block and the unrefined overflow analysis)
    // costs about as much as a few hundred reference steps of its graph.
    std::uint32_t hint_after = 256;
    std::uint32_t search_after = 2000;
    // What lifting a block may cost, in proportion to the time the block
    // has already spent on tier 0 (about 1.6 ns per graph edge per step):
    // the refined overflow analysis may spend about half of that, and a
    // search an eighth, so a block that never repays its proof costs a
    // bounded share more than it would on tier 0. When an attempt runs out,
    // the block is tried again once it has run eight times as often, with
    // eight times the allowance.
    //
    // The overflow analysis counts its work in units of about 15 ns
    // (lift/overflow.hpp): one unit per `steps_per_work` edge-steps. A
    // search is given `search_picoseconds` of wall time per edge-step.
    std::uint64_t steps_per_work = 16;
    std::uint64_t minimum_proof_work = 256;
    std::uint64_t search_picoseconds = 200;
    // Wall time the searches of one image may take in total.
    std::chrono::milliseconds search_budget{250};
};

// Steps of one execution by how the block was evaluated. `lifted` ran a
// proven lowered block; `wired` and `empty` are blocks whose graphs have no
// neurons (outputs that are input lanes, or no outputs at all), which need
// no proof; `declined` were lowered blocks that left the step to the
// reference evaluator; `reference` is every other step.
struct TierSteps {
    std::uint64_t lifted = 0;
    std::uint64_t wired = 0;
    std::uint64_t empty = 0;
    std::uint64_t declined = 0;
    std::uint64_t reference = 0;
};

struct TierStatistics {
    // Blocks in the image, blocks with a LIFT hint, and blocks with neurons
    // whose lifting was attempted and accepted, by source.
    std::uint64_t blocks = 0;
    std::uint64_t hinted = 0;
    std::uint64_t attempted = 0;
    std::uint64_t lifted_by_hint = 0;
    std::uint64_t lifted_by_search = 0;
    // Hints that failed a proof, and why blocks stayed on tier 0.
    std::uint64_t hints_rejected = 0;
    // Proofs that ran out of work and wait for the block to run more.
    std::uint64_t deferred = 0;
    std::map<std::string, std::uint64_t> rejections;
    // Time spent proving hints and searching, in microseconds.
    std::uint64_t hint_microseconds = 0;
    std::uint64_t search_microseconds = 0;
    TierSteps steps;
    // Steps each block ran on tier 0, by (function, block), and why the
    // block was not lifted: its rejection, or that it was not tried.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> reference_blocks;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> block_reasons;
};

// The lifted blocks of one validated image, shared by every execution of it:
// a run, the members of a fleet, and the worker threads of parallel items.
// Thread-safe. Lowered blocks are immutable once published and live as long
// as the cache.
class TierCache {
public:
    explicit TierCache(const nvr::Image& image, TierOptions options = {});
    ~TierCache();
    TierCache(const TierCache&) = delete;
    TierCache& operator=(const TierCache&) = delete;

    const nvr::Image& image() const { return image_; }
    const TierOptions& options() const { return options_; }
    bool hinted(std::uint32_t function, std::uint32_t block) const;
    // Executions of a block with neurons before it is first lifted.
    std::uint64_t threshold(std::uint32_t function, std::uint32_t block) const;

    struct Resolution {
        // The proven block, or null: the block stays on tier 0.
        const lift::LoweredBlock* block = nullptr;
        // False when nothing is decided yet: another thread is lifting the
        // block, or its proof needs more work than `runs` steps allow. Ask
        // again once the block has run `retry_after` times.
        bool settled = true;
        std::uint64_t retry_after = 0;
    };
    // The lowered form of a block with neurons that has run `runs` times,
    // lifting it now unless that was done or tried before. A failure is
    // remembered and never retried. Blocks that share a graph and a lift key
    // (lift::lift_key) share the result.
    Resolution resolve(std::uint32_t function, std::uint32_t block, std::uint64_t runs);
    // The settled result of a block, without lifting it.
    Resolution lookup(std::uint32_t function, std::uint32_t block) const;

    // Counts one more execution of a block that is not settled yet.
    std::uint64_t count(std::uint32_t function, std::uint32_t block) {
        return counts_[first_entry_[function] + block].fetch_add(1, std::memory_order_relaxed) + 1;
    }

    void add_steps(const TierSteps& steps);
    void add_reference_steps(std::uint32_t function, std::uint32_t block, std::uint64_t steps);
    TierStatistics statistics() const;

private:
    struct Entry {
        enum class State : std::uint8_t { Unknown, Lifting, Settled };
        State state = State::Unknown;
        std::shared_ptr<const lift::LoweredBlock> lowered;
        // A form whose structural proof held while the overflow analysis ran
        // out of work, and whether it came from the hint.
        std::shared_ptr<const lift::LiftedForm> retry;
        bool from_hint = false;
        // The hint failed a proof; the block may still be searched for.
        bool hint_failed = false;
        bool attempted = false;
        // The count of executions at which to try again: every run sharing
        // the cache waits for it.
        std::uint64_t next_attempt = 0;
    };
    using Key = std::pair<std::uint32_t, std::string>;

    Entry& entry(std::uint32_t function, std::uint32_t block);
    const Entry& entry(std::uint32_t function, std::uint32_t block) const;

    const nvr::Image& image_;
    TierOptions options_;
    lift::HintTable hints_;
    std::vector<std::size_t> first_entry_;
    std::vector<Entry> entries_;
    std::unique_ptr<std::atomic<std::uint64_t>[]> counts_;
    mutable std::mutex mutex_;
    // Accepted results by (graph, lift key), and keys whose search failed.
    std::map<Key, std::shared_ptr<const lift::LoweredBlock>> accepted_;
    std::map<Key, std::string> searched_;
    lift::GraphProofs graph_proofs_;
    std::chrono::steady_clock::duration search_spent_{};
    std::chrono::steady_clock::duration hint_spent_{};
    TierStatistics statistics_;
};

// The diagnostic lines of `neura run --tier-stats`: blocks lifted and steps
// per tier. Not one of the invariants of docs/runtime/NVM.md: the figures
// depend on thresholds, the time budget, and thread timing.
std::string describe(const TierStatistics& statistics);

// The highest tier the process allows: NEURA_TIER=0 or 1 lowers it for
// every execution (the test suite uses it), and nothing raises it.
std::uint8_t tier_ceiling();
// NEURA_TIER_CHECK=1: run every block that tier 1 evaluates on the
// reference evaluator too and abort with a report on any difference.
bool tier_check_enabled();
// NEURA_TIER_EAGER=1: lift every block when it first runs, with ample proof
// work and search time, so that short test runs exercise tier 1. It changes
// only which blocks run lifted, and when.
bool tier_eager();

} // namespace neura::nvm

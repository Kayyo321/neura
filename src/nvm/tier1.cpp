#include "tier1.hpp"

#include "lift/lift.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

namespace neura::nvm {
namespace {

std::uint64_t microseconds(std::chrono::steady_clock::duration duration) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
}

// The reason a block stays on tier 0, without the details that differ from
// block to block, for the statistics.
std::string reason_class(const std::string& rejection) {
    if (rejection.rfind("hint: ", 0) == 0) return "hint: " + reason_class(rejection.substr(6));
    const auto colon = rejection.find(": ");
    if (rejection.rfind("not lowered: ", 0) == 0 || rejection.rfind("not recognized: ", 0) == 0) {
        return rejection;
    }
    return colon == std::string::npos ? rejection : rejection.substr(0, colon);
}

} // namespace

std::uint8_t tier_ceiling() {
    static const std::uint8_t ceiling = [] {
        const char* text = std::getenv("NEURA_TIER");
        if (text && std::strcmp(text, "0") == 0) return std::uint8_t{0};
        if (text && std::strcmp(text, "1") == 0) return std::uint8_t{1};
        return std::uint8_t{2};
    }();
    return ceiling;
}

bool tier_check_enabled() {
    static const bool enabled = [] {
        const char* text = std::getenv("NEURA_TIER_CHECK");
        return text && std::strcmp(text, "1") == 0;
    }();
    return enabled;
}

std::string describe(const TierStatistics& s) {
    const auto& steps = s.steps;
    const auto total = steps.lifted + steps.wired + steps.empty + steps.declined + steps.reference + steps.region;
    const auto percent = [&](std::uint64_t part) {
        char text[32];
        std::snprintf(text, sizeof text, "%.1f%%", total ? 100.0 * static_cast<double>(part) / total : 0.0);
        return std::string(text);
    };
    const auto milliseconds = [](std::uint64_t us) {
        char text[32];
        std::snprintf(text, sizeof text, "%.1f ms", static_cast<double>(us) / 1000.0);
        return std::string(text);
    };
    std::string out = "tier: blocks " + std::to_string(s.blocks) + ", hinted " + std::to_string(s.hinted) +
                      ", attempted " + std::to_string(s.attempted) + ", lifted " +
                      std::to_string(s.lifted_by_hint + s.lifted_by_search) + " (" + std::to_string(s.lifted_by_hint) +
                      " by hint, " + std::to_string(s.lifted_by_search) + " by search), hints rejected " +
                      std::to_string(s.hints_rejected) + "; proving " + milliseconds(s.hint_microseconds) +
                      ", searching " + milliseconds(s.search_microseconds) + "\n";
    out += "tier: steps " + std::to_string(total) + ": tier 2 " + percent(steps.region) + " (in regions " +
           std::to_string(steps.region) + "), tier 1 " + percent(steps.lifted + steps.wired + steps.empty) +
           " (lifted " + std::to_string(steps.lifted) + ", wired " + std::to_string(steps.wired) + ", empty " +
           std::to_string(steps.empty) + "), tier 0 " + percent(steps.reference + steps.declined) + " (reference " +
           std::to_string(steps.reference) + ", declined " + std::to_string(steps.declined) + ")\n";
    const auto& r = s.region_counts;
    if (s.regions != 0 || !s.region_rejections.empty()) {
        out += "tier: regions " + std::to_string(s.regions) + " (" + std::to_string(s.region_blocks) +
               " blocks, " + std::to_string(s.checks_eliminated) + " checks eliminated, " +
               std::to_string(s.refreshed) + " formed again); entered " +
               std::to_string(r.entries) + ", refused " + std::to_string(r.refused) + ", left " +
               std::to_string(r.leaves) + "; deoptimized: budget " + std::to_string(r.budget) + ", exits " +
               std::to_string(r.exits) + ", checks " + std::to_string(r.checks) + ", selectors " +
               std::to_string(r.selectors) + ", references " + std::to_string(r.references) + ", state traps " +
               std::to_string(r.state_traps) + "\n";
        for (const auto& [reason, count] : s.region_rejections) {
            out += "tier: no region " + std::to_string(count) + ": " + reason + "\n";
        }
        const auto top = [&](const std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t>& counts,
                             const char* what) {
            std::vector<std::pair<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>>> ends;
            for (const auto& [block, count] : counts) ends.emplace_back(count, block);
            std::sort(ends.begin(), ends.end(), std::greater<>());
            for (std::size_t i = 0; i < ends.size() && i < 5; ++i) {
                out += std::string("tier: ") + what + " " + std::to_string(ends[i].first) + " times at function " +
                       std::to_string(ends[i].second.first) + " block " + std::to_string(ends[i].second.second) + "\n";
            }
        };
        top(s.region_deopts, "deoptimized");
        top(s.region_leaves, "left");
    }
    if (!s.jit_mode.empty()) {
        if (!s.jit_off.empty() && s.jit_compiled == 0 && s.jit_failures.empty()) {
            out += s.jit_mode == "off" ? std::string("tier: jit off\n")
                                       : "tier: jit " + s.jit_mode + ", not used: " + s.jit_off + "\n";
        } else {
            std::uint64_t failed = 0;
            for (const auto& [reason, count] : s.jit_failures) failed += count;
            out += "tier: jit " + s.jit_mode + ": " + std::to_string(s.jit_compiled) + " regions compiled (" +
                   std::to_string(s.jit_bytes) + " bytes of code), " + std::to_string(failed) +
                   " on the interpreter; entries native " + std::to_string(r.native) + ", interpreted " +
                   std::to_string(r.entries - r.native) + "\n";
            for (const auto& [reason, count] : s.jit_failures) {
                out += "tier: jit fallback " + std::to_string(count) + ": " + reason + "\n";
            }
        }
    }
    for (const auto& [reason, count] : s.rejections) {
        out += "tier: not lifted " + std::to_string(count) + ": " + reason + "\n";
    }
    std::vector<std::pair<std::uint64_t, std::pair<std::uint32_t, std::uint32_t>>> busiest;
    for (const auto& [block, count] : s.reference_blocks) busiest.emplace_back(count, block);
    std::sort(busiest.begin(), busiest.end(), std::greater<>());
    for (std::size_t i = 0; i < busiest.size() && i < 8; ++i) {
        const auto& [count, block] = busiest[i];
        const auto reason = s.block_reasons.find(block);
        out += "tier: tier 0 steps " + std::to_string(count) + " in function " + std::to_string(block.first) +
               " block " + std::to_string(block.second) + ": " +
               (reason == s.block_reasons.end() ? std::string("below the threshold") : reason->second) + "\n";
    }
    return out;
}

bool tier_eager() {
    static const bool enabled = [] {
        const char* text = std::getenv("NEURA_TIER_EAGER");
        return text && std::strcmp(text, "1") == 0;
    }();
    return enabled;
}

TierCache::TierCache(const nvr::Image& image, TierOptions options)
    : image_(image), options_(options), hints_(lift::decode_hints(image)) {
    if (tier_eager()) {
        options_.hint_after = 1;
        options_.search_after = 1;
        options_.minimum_proof_work = std::uint64_t{1} << 32;
        options_.search_budget = std::chrono::milliseconds(60'000);
        options_.region_after = 1;
    }
    first_entry_.reserve(image.functions.size());
    std::size_t total = 0;
    for (const auto& function : image.functions) {
        first_entry_.push_back(total);
        total += function.blocks.size();
    }
    entries_ = std::vector<Entry>(total);
    counts_ = std::make_unique<std::atomic<std::uint64_t>[]>(total);
    regions_ = std::vector<RegionEntry>(total);
    region_counts_ = std::make_unique<std::atomic<std::uint64_t>[]>(total);
    statistics_.blocks = total;
    statistics_.hinted = hints_.size();
}

TierCache::~TierCache() = default;

TierCache::Entry& TierCache::entry(std::uint32_t function, std::uint32_t block) {
    return entries_[first_entry_[function] + block];
}

const TierCache::Entry& TierCache::entry(std::uint32_t function, std::uint32_t block) const {
    return entries_[first_entry_[function] + block];
}

bool TierCache::hinted(std::uint32_t function, std::uint32_t block) const {
    return hints_.contains({function, block});
}

std::uint64_t TierCache::threshold(std::uint32_t function, std::uint32_t block) const {
    const std::lock_guard lock(mutex_);
    const std::uint64_t base = hints_.contains({function, block}) ? options_.hint_after : options_.search_after;
    return std::max(base, entry(function, block).next_attempt);
}

TierCache::Resolution TierCache::lookup(std::uint32_t function, std::uint32_t block) const {
    const std::lock_guard lock(mutex_);
    const auto& found = entry(function, block);
    if (found.state != Entry::State::Settled) return {nullptr, false};
    return {found.lowered.get(), true};
}

TierCache::Resolution TierCache::resolve(std::uint32_t function, std::uint32_t block, std::uint64_t runs) {
    const auto& decoded = image_.functions.at(function).blocks.at(block);
    const auto& graph = image_.graphs.at(decoded.graph);
    const auto budget = std::chrono::steady_clock::duration(options_.search_budget);
    Key key;
    // What this attempt tries: a form whose structure is proven and whose
    // overflow analysis ran out of work before (`retry`), else the hint
    // unless it failed, else a search once the block has run search_after
    // times, while the image's search budget lasts.
    std::shared_ptr<const lift::LiftedForm> retry;
    const lift::LiftedForm* hint = nullptr;
    bool search = false;
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t edges = 0;
    for (const auto& node : graph.nodes) edges += node.edges.size();
    {
        const std::lock_guard lock(mutex_);
        auto& found = entry(function, block);
        if (found.state == Entry::State::Settled) return {found.lowered.get(), true};
        // Another thread is lifting the block; ask again later.
        if (found.state == Entry::State::Lifting) return {nullptr, false, std::max<std::uint64_t>(runs * 2, 2)};
        if (runs < found.next_attempt) return {nullptr, false, found.next_attempt};
        key = {decoded.graph, lift::lift_key(image_, decoded)};
        if (const auto it = accepted_.find(key); it != accepted_.end()) {
            found.lowered = it->second;
            found.state = Entry::State::Settled;
            statistics_.block_reasons.erase({function, block});
            return {found.lowered.get(), true};
        }
        retry = found.retry;
        if (retry) {
            hint = retry.get();
        } else if (const auto it = hints_.find({function, block}); it != hints_.end() && !found.hint_failed) {
            hint = it->second.get();
        }
        const auto searched = searched_.find(key);
        const bool searchable = !retry && searched == searched_.end() && search_spent_ < budget;
        search = searchable && runs >= options_.search_after;
        if (!hint && !search) {
            if (searchable) return {nullptr, false, options_.search_after};
            found.state = Entry::State::Settled;
            const auto reason = searched != searched_.end() ? "as its graph and key: " + searched->second
                                                            : std::string("search budget spent");
            ++statistics_.rejections[reason];
            if (!found.hint_failed) statistics_.block_reasons[{function, block}] = reason;
            return {nullptr, true};
        }
        if (!retry && !found.attempted) {
            found.attempted = true;
            ++statistics_.attempted;
        }
        found.state = Entry::State::Lifting;
        // A search's time, unlike the proof's work, is wall time: whether a
        // search finishes can differ between runs. That changes only which
        // blocks run lifted, never what a run does.
        const auto allowed = std::chrono::nanoseconds(runs * edges * options_.search_picoseconds / 1000);
        deadline = std::chrono::steady_clock::now() +
                   std::min(budget - search_spent_,
                            std::chrono::duration_cast<std::chrono::steady_clock::duration>(allowed));
    }

    // The overflow analysis may use work in proportion to the steps the
    // block has run: a graph that needs much of it is proven only once the
    // block has run often enough for the lifted form to repay the proof, and
    // a run that ends sooner never pays for it. The proof found does not
    // depend on the work allowed, only whether one is found.
    const auto work = std::max<std::uint64_t>(options_.minimum_proof_work,
                                              runs * edges / std::max<std::uint64_t>(options_.steps_per_work, 1));

    // Prove the hint or the retried form, or search. Both run outside the
    // lock; other threads meanwhile run the block on tier 0.
    lift::LiftResult result;
    std::chrono::steady_clock::duration time{};
    std::string hint_rejection;
    const auto start = std::chrono::steady_clock::now();
    if (hint) {
        result = lift::lift_and_prove(image_, function, block, hint, std::chrono::steady_clock::time_point::max(),
                                      &graph_proofs_, work);
    } else {
        result = lift::lift_and_prove(image_, function, block, nullptr, deadline, &graph_proofs_, work);
        // A search abandoned for time decides nothing either: it is tried
        // again later while the image's search budget lasts.
        if (!result.block && result.rejection.find("out of time") != std::string::npos) result.exhausted = true;
    }
    time = std::chrono::steady_clock::now() - start;

    const std::lock_guard lock(mutex_);
    (hint ? hint_spent_ : search_spent_) += time;
    auto& found = entry(function, block);
    const bool by_hint = retry ? found.from_hint : hint != nullptr;
    if (result.block) {
        found.state = Entry::State::Settled;
        found.retry.reset();
        found.lowered = result.block;
        accepted_.emplace(key, result.block);
        statistics_.block_reasons.erase({function, block});
        ++(by_hint ? statistics_.lifted_by_hint : statistics_.lifted_by_search);
        return {found.lowered.get(), true};
    }
    if (result.exhausted) {
        // Not decided: try again once the block has run eight times as
        // often, with the form whose structure is proven, if any.
        found.state = Entry::State::Unknown;
        found.retry = result.form;
        if (!retry) found.from_hint = hint != nullptr;
        ++statistics_.deferred;
        statistics_.block_reasons[{function, block}] =
            "deferred after " + std::to_string(runs) + " runs: the proof or search needs more work";
        // The analysis's work grows about in proportion to the nodes it
        // settles, so the share it reached says how much more the rest
        // needs: the next attempt waits until the block's allowance has
        // grown that much (at least eightfold), so a run too short to repay
        // the proof does not pay for attempts that cannot finish.
        const auto share = std::max(result.progress, 1.0 / 256);
        const auto factor = std::clamp<std::uint64_t>(static_cast<std::uint64_t>(std::ceil(2.0 / share)), 8, 512);
        found.next_attempt = std::max<std::uint64_t>(runs * factor, 256);
        return {nullptr, false, found.next_attempt};
    }
    // A failure is final for its source: a failed hint is not tried again
    // (the block may still be searched for), and a failed search is not
    // repeated for any block with its graph and key.
    found.retry.reset();
    const auto reason = (by_hint ? "hint: " : "") + result.rejection;
    ++statistics_.rejections[reason_class(reason)];
    statistics_.block_reasons[{function, block}] = reason;
    if (by_hint) {
        found.hint_failed = true;
        ++statistics_.hints_rejected;
        // Only a wrong form leaves a search something to find: a graph's
        // overflow verdict, or an operation without a lowering, holds for
        // any form the search could find.
        const bool wrong_form = result.rejection == "structural proof failed" ||
                                result.rejection.rfind("not lowered: ill-typed form", 0) == 0;
        const bool searchable = wrong_form && searched_.find(key) == searched_.end() && search_spent_ < budget;
        if (searchable) {
            found.state = Entry::State::Unknown;
            found.next_attempt = std::max<std::uint64_t>(runs + 1, options_.search_after);
            return {nullptr, false, found.next_attempt};
        }
    } else {
        searched_.emplace(key, reason_class(result.rejection));
    }
    found.state = Entry::State::Settled;
    return {nullptr, true};
}

void TierCache::add_reference_steps(std::uint32_t function, std::uint32_t block, std::uint64_t steps) {
    const std::lock_guard lock(mutex_);
    statistics_.reference_blocks[{function, block}] += steps;
}

void TierCache::add_steps(const TierSteps& steps) {
    const std::lock_guard lock(mutex_);
    statistics_.steps.lifted += steps.lifted;
    statistics_.steps.wired += steps.wired;
    statistics_.steps.empty += steps.empty;
    statistics_.steps.declined += steps.declined;
    statistics_.steps.reference += steps.reference;
    statistics_.steps.region += steps.region;
}

TierStatistics TierCache::statistics() const {
    const std::lock_guard lock(mutex_);
    auto copy = statistics_;
    copy.hint_microseconds = microseconds(hint_spent_);
    copy.search_microseconds = microseconds(search_spent_);
    return copy;
}

} // namespace neura::nvm

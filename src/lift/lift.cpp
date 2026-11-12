#include "lift.hpp"

#include "overflow.hpp"
#include "proof.hpp"
#include "recognize.hpp"

#include <algorithm>
#include <numeric>

namespace neura::lift {
namespace {

LiftResult rejected(std::string why) {
    LiftResult result;
    result.rejection = std::move(why);
    return result;
}

// Overflow freedom, and every output lane (each is read by some exit or is
// the selector) exactly 0 or 1. The unrefined analysis first: its intervals
// are sound but wider, and it settles most graphs at a fraction of the cost.
std::string prove_graph(const nvr::Graph& graph, std::uint64_t work, double* progress = nullptr,
                        std::shared_ptr<const OverflowProof>* partial = nullptr, const ProofAdvice* advice = nullptr) {
    std::vector<std::uint32_t> lanes(graph.outputs.size());
    std::iota(lanes.begin(), lanes.end(), 0U);
    if (const auto quick = prove_no_overflow(graph, false);
        quick.safe && lanes_are_bits(quick, graph, lanes, nullptr, false)) {
        return {};
    }
    // With advice, the analysis first lets the cuts it names stand
    // (decisive); a graph not proven so is analysed again with the next
    // use: the search after every advised cut, and last without advice, as
    // the analysis has always run. An earlier attempt that ran out of work
    // continues with its use.
    const auto* earlier = partial ? partial->get() : nullptr;
    if (advice && advice->instance.empty()) advice = nullptr;
    auto use = !advice ? AdviceUse::none : earlier ? earlier->advice_use : AdviceUse::decisive;
    // A use that lets advice stand is cheap where the advice is right; where
    // it is not, it is given up after an allowance in proportion to the
    // graph's edges, as if it had failed.
    std::uint64_t edges = 0;
    for (const auto& node : graph.nodes) edges += node.edges.size();
    for (;;) {
        const auto allowance = use == AdviceUse::decisive ? edges * 256
                               : use == AdviceUse::first  ? edges * 4096
                                                          : std::numeric_limits<std::uint64_t>::max();
        const bool limited = allowance < work;
        auto left = limited ? allowance : work;
        const auto before = left;
        // An earlier attempt that ran out of work: resume it, or, when it
        // settled every node and ran out on the lanes, take its intervals.
        const auto* resume = earlier && earlier->advice_use == use ? earlier : nullptr;
        const auto* given = use == AdviceUse::none ? nullptr : advice;
        const auto overflow =
            resume && resume->safe ? *resume : prove_no_overflow(graph, true, &left, resume, given, use);
        std::string failure;
        if (overflow.exhausted) {
            failure = exhausted_work;
        } else if (!overflow.safe) {
            failure = "overflow not excluded";
        } else if (!lanes_are_bits(overflow, graph, lanes, nullptr, true, &left, given)) {
            failure = left == 0 ? exhausted_work : "an output lane not proven 0 or 1";
        }
        work -= before - left;
        if (failure == exhausted_work && limited) {
            // The use's allowance ran out, not the caller's work.
            failure = "the advice did not settle the graph";
        } else {
            if (partial && (overflow.exhausted || overflow.safe)) {
                *partial = std::make_shared<const OverflowProof>(overflow);
            }
            if (overflow.exhausted && progress) {
                *progress = static_cast<double>(overflow.node) /
                            static_cast<double>(std::max<std::size_t>(graph.nodes.size(), 1));
            }
            if (failure.empty() || failure == exhausted_work || use == AdviceUse::none) return failure;
        }
        use = use == AdviceUse::decisive ? AdviceUse::first : AdviceUse::none;
        earlier = nullptr;
    }
}

} // namespace

std::string GraphProofs::verdict(const nvr::Graph& graph, std::uint32_t index, std::uint64_t work, double* progress,
                                 const ProofAdvice* advice) {
    // An analysis that ran out of work is resumed where it stopped, so
    // attempts with more work (or another block of the same graph) pay only
    // for the nodes not yet settled. Two threads may prove one graph at
    // once; they reach the same verdict, and the furthest partial analysis
    // is kept.
    std::shared_ptr<const OverflowProof> partial;
    {
        const std::lock_guard lock(mutex_);
        if (const auto it = verdicts_.find(index); it != verdicts_.end()) return it->second;
        if (const auto it = partials_.find(index); it != partials_.end()) partial = it->second;
    }
    auto verdict = prove_graph(graph, work, progress, &partial, advice);
    const std::lock_guard lock(mutex_);
    if (verdict == exhausted_work) {
        auto& kept = partials_[index];
        const auto reach = [](const OverflowProof& p) {
            return p.safe ? std::numeric_limits<std::size_t>::max() : p.node;
        };
        if (partial && (!kept || reach(*partial) > reach(*kept))) kept = partial;
        return verdict;
    }
    partials_.erase(index);
    return verdicts_.emplace(index, std::move(verdict)).first->second;
}

LiftResult lift_and_prove(const nvr::Image& image, std::uint32_t function, std::uint32_t block_index,
                          const LiftedForm* hint, std::chrono::steady_clock::time_point deadline,
                          GraphProofs* proofs, std::uint64_t work) {
    const auto& block = image.functions.at(function).blocks.at(block_index);
    const auto& graph = image.graphs.at(block.graph);
    LiftedForm form;
    bool trivial = false;
    if (hint) {
        form = *hint;
    } else {
        // The decompiler's vocabulary first; bitwise operations and shifts
        // widen the search, which bounds its candidates, so they are tried
        // only for a block the narrower search does not recognize.
        RecognizeOptions narrow;
        narrow.deadline = deadline;
        auto recognition = recognize(image, function, block_index, narrow);
        if (recognition.status == Recognition::Status::Unrecognized) {
            RecognizeOptions options;
            options.extended = true;
            options.deadline = deadline;
            auto extended = recognize(image, function, block_index, options);
            if (extended.status == Recognition::Status::Lifted) recognition = std::move(extended);
        }
        switch (recognition.status) {
        case Recognition::Status::Trivial:
            trivial = true;
            break;
        case Recognition::Status::Lifted:
            form = std::move(recognition.form);
            break;
        case Recognition::Status::Unrecognized:
            return rejected(std::string("not recognized: ") + recognition.reason);
        }
    }

    // The lowering type-checks the form first, so nothing ill-typed reaches
    // the circuit library.
    auto lowered = lower(image, block, form);
    if (!lowered.block) {
        return rejected("not lowered: " + lowered.failure);
    }

    // Structural proof. A trivial block has no neuron, no lane an exit
    // reads, and at most one exit, which the runtime takes without a
    // selector: the lowering above already required every graph output lane
    // to be read, so there is nothing for regeneration to establish.
    // Regeneration also records the gadgets that built the graph: advice for
    // the overflow proof, which establishes everything itself.
    ProofAdvice advice;
    if (!trivial && !prove_structure(image, block, graph, form, nullptr, &advice)) {
        return rejected("structural proof failed");
    }

    double progress = 1.0;
    if (auto failure = proofs ? proofs->verdict(graph, block.graph, work, &progress, &advice)
                              : prove_graph(graph, work, &progress, nullptr, &advice);
        !failure.empty()) {
        auto result = rejected(std::move(failure));
        if (result.rejection == exhausted_work) {
            result.exhausted = true;
            result.progress = progress;
            result.form = std::make_shared<const LiftedForm>(std::move(form));
        }
        return result;
    }

    LiftResult result;
    result.block = std::make_shared<const LoweredBlock>(std::move(*lowered.block));
    return result;
}

std::optional<LoweredBlock> lift_and_prove_block(const nvr::Image& image, std::uint32_t function,
                                                 std::uint32_t block) {
    auto result = lift_and_prove(image, function, block);
    if (!result.block) {
        return std::nullopt;
    }
    return *result.block;
}

std::string lift_key(const nvr::Image& image, const nvr::Block& block) {
    std::string key;
    const auto put = [&](std::uint64_t value) {
        key += std::to_string(value);
        key += ',';
    };
    put(block.inputs.size());
    for (std::size_t i = 0; i < block.inputs.size(); ++i) {
        const auto& input = block.inputs[i];
        put(static_cast<std::uint64_t>(input.kind));
        put(input.type);
        // Which earlier input it repeats, if any: a repeated input shares
        // lanes in the regenerated block.
        std::size_t first = i;
        for (std::size_t j = 0; j < i; ++j) {
            if (block.inputs[j].kind == input.kind && block.inputs[j].index == input.index) {
                first = j;
                break;
            }
        }
        put(first);
    }
    put(block.selector_start);
    put(block.exits.size());
    const auto value = [&](const nvr::Value& v) {
        put(static_cast<std::uint64_t>(v.kind));
        put(v.type);
        put(v.kind == nvr::ValueKind::Lanes ? v.index : 0);
    };
    for (const auto& exit : block.exits) {
        key += '|';
        put(static_cast<std::uint64_t>(exit.kind));
        put(exit.kind == nvr::ExitKind::Trap ? exit.reason : 0);
        put(exit.assignments.size());
        for (const auto& assignment : exit.assignments) value(assignment.value);
        put(exit.arguments.size());
        for (const auto& argument : exit.arguments) value(argument);
    }
    (void)image;
    return key;
}

const LiftResult& LiftCache::get(std::uint32_t function, std::uint32_t block) {
    const auto& b = image_.functions.at(function).blocks.at(block);
    auto key = std::make_pair(b.graph, lift_key(image_, b));
    {
        std::lock_guard lock(mutex_);
        if (const auto it = results_.find(key); it != results_.end()) {
            return *it->second;
        }
    }
    auto result = std::make_unique<LiftResult>(lift_and_prove(image_, function, block));
    std::lock_guard lock(mutex_);
    auto [it, inserted] = results_.emplace(std::move(key), std::move(result));
    return *it->second;
}

std::size_t LiftCache::size() const {
    std::lock_guard lock(mutex_);
    return results_.size();
}

} // namespace neura::lift

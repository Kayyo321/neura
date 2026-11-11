#include "lift.hpp"

#include "overflow.hpp"
#include "proof.hpp"
#include "recognize.hpp"

#include <numeric>

namespace neura::lift {
namespace {

LiftResult rejected(std::string why) {
    LiftResult result;
    result.rejection = std::move(why);
    return result;
}

} // namespace

LiftResult lift_and_prove(const nvr::Image& image, std::uint32_t function, std::uint32_t block_index,
                          const LiftedForm* hint) {
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
        auto recognition = recognize(image, function, block_index);
        if (recognition.status == Recognition::Status::Unrecognized) {
            RecognizeOptions options;
            options.extended = true;
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
    if (!trivial && !prove_structure(image, block, graph, form)) {
        return rejected("structural proof failed");
    }

    // Overflow proof, and every output lane (each is read by some exit or is
    // the selector) exactly 0 or 1.
    const auto overflow = prove_no_overflow(graph);
    if (!overflow.safe) {
        return rejected("overflow not excluded");
    }
    std::vector<std::uint32_t> lanes(graph.outputs.size());
    std::iota(lanes.begin(), lanes.end(), 0U);
    if (!lanes_are_bits(overflow, graph, lanes)) {
        return rejected("an output lane not proven 0 or 1");
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

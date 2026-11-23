#pragma once

#include "nvm/tier3.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace neura::testing {
namespace frame_liveness_oracle {

// Walk one original slot value forward while it remains in the frame.
// This uses the executable image directly and never consults native liveness.
class ImageWalk {
public:
    explicit ImageWalk(const nvr::Image& image) : image_(image), observed_(image.functions.size()) {}

    bool needs(std::uint32_t function, std::uint32_t resume, std::uint32_t slot) {
        if (function >= image_.functions.size()) return true;
        const auto& original = image_.functions[function];
        if (slot >= original.frame_types.size() || original.frame_types[slot] >= image_.types.size() ||
            nvr::is_reference(image_.types[original.frame_types[slot]].kind)) return true;

        std::vector<bool> visited(original.blocks.size());
        std::vector<std::uint32_t> work{resume};
        const auto reads = [slot](const nvr::Value& value) {
            return value.kind == nvr::ValueKind::Slot && value.index == slot;
        };
        while (!work.empty()) {
            const auto index = work.back();
            work.pop_back();
            if (index >= original.blocks.size()) return true;
            if (visited[index]) continue;
            visited[index] = true;
            const auto& block = original.blocks[index];
            if (std::ranges::any_of(block.inputs, reads)) return true;
            for (const auto& exit : block.exits) {
                if (std::ranges::any_of(exit.arguments, reads) ||
                    std::ranges::any_of(exit.assignments, [&](const auto& assignment) { return reads(assignment.value); }))
                    return true;

                // Operands read the old frame. A parallel assignment takes
                // effect before a poll, callee or host can observe this word.
                if (std::ranges::any_of(exit.assignments, [slot](const auto& assignment) { return assignment.slot == slot; }))
                    continue;
                if (exit.poll) return true;

                switch (exit.kind) {
                case nvr::ExitKind::Continue:
                    work.push_back(exit.resume);
                    break;
                case nvr::ExitKind::State:
                    if (slot != exit.result_slot) work.push_back(exit.resume);
                    break;
                case nvr::ExitKind::Call: {
                    if (observes(exit.target)) return true;
                    const auto words = 1 + image_.functions[exit.target].more_results.size();
                    // A result overwrites the old value only after the call.
                    if (slot < exit.result_slot || slot - exit.result_slot >= words) work.push_back(exit.resume);
                    break;
                }
                case nvr::ExitKind::Return:
                case nvr::ExitKind::Trap:
                case nvr::ExitKind::MessageTrap:
                    break;
                case nvr::ExitKind::Host:
                case nvr::ExitKind::Scheduler:
                default:
                    return true;
                }
            }
        }
        return false;
    }

private:
    bool observes(std::uint32_t function) {
        if (function >= image_.functions.size()) return true;
        auto& cached = observed_[function];
        if (!cached) cached = reaches_observer(function);
        return *cached;
    }

    bool reaches_observer(std::uint32_t function) const {
        std::vector<bool> visited(image_.functions.size());
        std::vector<std::uint32_t> work{function};
        while (!work.empty()) {
            const auto current = work.back();
            work.pop_back();
            if (current >= image_.functions.size()) return true;
            if (visited[current]) continue;
            visited[current] = true;
            for (const auto& block : image_.functions[current].blocks) {
                for (const auto& exit : block.exits) {
                    if (exit.poll || exit.kind == nvr::ExitKind::Host || exit.kind == nvr::ExitKind::Scheduler)
                        return true;
                    if (exit.kind == nvr::ExitKind::Call) work.push_back(exit.target);
                }
            }
        }
        return false;
    }

    const nvr::Image& image_;
    std::vector<std::optional<bool>> observed_;
};

} // namespace frame_liveness_oracle

inline void audit_frame_liveness(const nvr::Image& image, const nvm::tier3::Function& entry, std::size_t expected) {
    frame_liveness_oracle::ImageWalk oracle(image);
    std::size_t checked_states = 0;
    std::size_t checked_slots = 0;
    for (const auto& state : entry.states) {
        bool has_omission = false;
        for (std::uint32_t slot = 0; slot < state.slots.size(); ++slot) {
            if (state.slots[slot].kind != nvm::tier3::StateSlot::Kind::Dead) continue;
            if (oracle.needs(state.function, state.block, slot)) {
                std::fprintf(stderr, "frame liveness audit: omitted a needed slot %u at function %u block %u\n",
                             slot, state.function, state.block);
                std::abort();
            }
            has_omission = true;
            ++checked_slots;
        }
        if (has_omission) ++checked_states;
    }
    if (checked_slots != expected) {
        std::fprintf(stderr, "frame liveness audit: checked %zu slots but the pruner reported %zu\n", checked_slots, expected);
        std::abort();
    }
    std::fprintf(stderr, "frame-liveness-audit {\"function\":%u,\"checked_states\":%zu,\"checked_slots\":%zu}\n",
                 entry.function, checked_states, checked_slots);
}

} // namespace neura::testing

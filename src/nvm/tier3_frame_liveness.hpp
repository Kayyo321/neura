#pragma once

#include "tier3.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace neura::nvm::tier3::detail {

// Liveness at the entry to each original image block, before it reads any
// input or charges a step. Only scalar payloads can be omitted: reference
// payloads are roots even when no later image instruction reads them.
class FrameLiveness {
public:
    FrameLiveness(const nvr::Image& image, std::uint32_t function) {
        if (function >= image.functions.size()) return;
        const auto& original = image.functions[function];
        const auto slots = original.frame_types.size();
        const auto words = (slots + 63) / 64;
        scalar_.reserve(slots);
        for (const auto type : original.frame_types) {
            scalar_.push_back(type < image.types.size() && scalar(image.types[type].kind));
        }

        std::vector<Flow> flow(original.blocks.size());
        std::vector<std::vector<std::size_t>> predecessors(flow.size());
        std::vector<std::uint8_t> observers;
        for (std::size_t block = 0; block < flow.size(); ++block) {
            const auto& source = original.blocks[block];
            auto& info = flow[block];
            info.reads.resize(words);
            const auto read = [&](const nvr::Value& value) {
                if (value.kind == nvr::ValueKind::Slot && value.index < slots) {
                    info.reads[value.index / 64] |= bit(value.index);
                }
            };
            for (const auto& input : source.inputs) read(input);
            for (const auto& exit : source.exits) {
                // Exit operands, including the RHS of assignments, read the
                // old frame before any assignment or result commits.
                std::vector<std::uint32_t> written;
                for (const auto& assignment : exit.assignments) {
                    read(assignment.value);
                    written.push_back(assignment.slot);
                }
                for (const auto& argument : exit.arguments) read(argument);

                const auto observe = [&] {
                    info.edges.push_back({nvr::absent, written, true});
                };
                const auto follow = [&](std::uint32_t target, std::uint32_t result = nvr::absent,
                                        std::size_t width = 1) {
                    if (target >= flow.size()) {
                        observe();
                        return;
                    }
                    Edge edge{target, written, false};
                    if (result < slots && width <= slots - result) {
                        for (std::size_t word = 0; word < width; ++word) {
                            edge.written.push_back(result + static_cast<std::uint32_t>(word));
                        }
                    }
                    info.edges.push_back(std::move(edge));
                    predecessors[target].push_back(block);
                };
                if (exit.poll) observe();
                switch (exit.kind) {
                case nvr::ExitKind::Continue:
                    follow(exit.resume);
                    break;
                case nvr::ExitKind::Call:
                    if (observers.empty()) observers = observing_functions(image);
                    if (exit.target >= image.functions.size()) {
                        observe();
                        break;
                    }
                    if (observers[exit.target]) observe();
                    follow(exit.resume, exit.result_slot, image.functions[exit.target].more_results.size() + 1);
                    break;
                case nvr::ExitKind::State:
                    follow(exit.resume, exit.result_slot);
                    break;
                case nvr::ExitKind::Host:
                    // The caller still exists when the driver receives the
                    // request, before either host result word is written.
                    observe();
                    follow(exit.resume, exit.result_slot);
                    follow(exit.error_resume, exit.error_slot);
                    break;
                case nvr::ExitKind::Scheduler:
                    observe();
                    break;
                case nvr::ExitKind::Return:
                case nvr::ExitKind::Trap:
                case nvr::ExitKind::MessageTrap:
                    break;
                default:
                    observe();
                    break;
                }
            }
        }

        live_.assign(flow.size(), Bits(words));
        std::deque<std::size_t> work;
        std::vector<std::uint8_t> queued(flow.size(), 1);
        for (auto block = flow.size(); block > 0; --block) work.push_back(block - 1);
        const Bits all(words, ~std::uint64_t{0});
        while (!work.empty()) {
            const auto block = work.front();
            work.pop_front();
            queued[block] = 0;
            const auto& info = flow[block];
            auto needed = info.reads;
            for (const auto& edge : info.edges) {
                auto continuation = edge.observes ? all : live_[edge.target];
                for (const auto slot : edge.written) {
                    if (slot < slots) continuation[slot / 64] &= ~bit(slot);
                }
                for (std::size_t word = 0; word < words; ++word) needed[word] |= continuation[word];
            }
            if (needed == live_[block]) continue;
            live_[block] = std::move(needed);
            for (const auto predecessor : predecessors[block]) {
                if (queued[predecessor]) continue;
                queued[predecessor] = 1;
                work.push_back(predecessor);
            }
        }
    }

    bool dead_scalar(std::uint32_t block, std::uint32_t slot) const {
        return block < live_.size() && slot < scalar_.size() && scalar_[slot] &&
               (live_[block][slot / 64] & bit(slot)) == 0;
    }

private:
    using Bits = std::vector<std::uint64_t>;

    static std::uint64_t bit(std::uint32_t slot) {
        return std::uint64_t{1} << (slot % 64);
    }

    struct Edge {
        std::uint32_t target;
        std::vector<std::uint32_t> written;
        bool observes;
    };

    struct Flow {
        Bits reads;
        std::vector<Edge> edges;
    };

    static bool scalar(nvr::TypeKind kind) {
        switch (kind) {
        case nvr::TypeKind::Bool:
        case nvr::TypeKind::Unsigned:
        case nvr::TypeKind::Signed:
        case nvr::TypeKind::Float:
        case nvr::TypeKind::BFloat: return true;
        default: return false;
        }
    }

    // A host request or scheduler boundary in any transitive callee can
    // expose the suspended caller frame. The image's call exits include
    // every possible direct, closure and dispatch target.
    static std::vector<std::uint8_t> observing_functions(const nvr::Image& image) {
        std::vector<std::uint8_t> result(image.functions.size());
        std::vector<std::vector<std::size_t>> callers(image.functions.size());
        std::vector<std::size_t> work;
        const auto mark = [&](std::size_t function) {
            if (result[function]) return;
            result[function] = 1;
            work.push_back(function);
        };
        for (std::size_t function = 0; function < image.functions.size(); ++function) {
            for (const auto& block : image.functions[function].blocks) {
                for (const auto& exit : block.exits) {
                    if (exit.poll || exit.kind == nvr::ExitKind::Host || exit.kind == nvr::ExitKind::Scheduler) {
                        mark(function);
                    }
                    if (exit.kind != nvr::ExitKind::Call) continue;
                    if (exit.target < callers.size()) callers[exit.target].push_back(function);
                    else mark(function);
                }
            }
        }
        while (!work.empty()) {
            const auto function = work.back();
            work.pop_back();
            for (const auto caller : callers[function]) mark(caller);
        }
        return result;
    }

    std::vector<std::uint8_t> scalar_;
    std::vector<Bits> live_;
};

inline bool precommit_deopt(const Inst& inst) {
    switch (inst.op) {
    case Op::Deopt:
    case Op::Guard:
    case Op::Budget:
    case Op::SwitchCheck:
    case Op::CheckNotNil:
    case Op::Lookup:
    case Op::CheckKind:
    case Op::CheckInit:
    case Op::CheckIndex:
    case Op::CheckGlobalInit: return true;
    // The native return stub uses its entry state only when the return
    // helper refuses before charging a step or popping the frame.
    case Op::Return: return true;
    case Op::Lift: return (inst.flags & Checked) != 0;
    case Op::StateCall: return (inst.flags & CheckReference) != 0;
    default: return false;
    }
}

// A state shared with commit, an inlined frame or a possible
// observer still has its full payload. Keep initialized flags unchanged.
inline std::size_t prune_dead_frame_slots(const nvr::Image& image, Function& function) {
    if (function.variant != Variant::SparseFrames || function.entries.size() != 1 ||
        function.function >= image.functions.size()) {
        return 0;
    }
    enum class Use : std::uint8_t { None, DeoptOnly, Full };
    std::vector<Use> uses(function.states.size());
    for (const auto& frame : function.states) {
        if (frame.caller < uses.size()) uses[frame.caller] = Use::Full;
    }
    for (const auto& block : function.blocks) {
        for (const auto id : block.insts) {
            const auto& inst = function.insts[id];
            if (inst.state >= uses.size()) continue;
            auto& use = uses[inst.state];
            if (!precommit_deopt(inst)) use = Use::Full;
            else if (use == Use::None) use = Use::DeoptOnly;
        }
    }

    if (std::none_of(uses.begin(), uses.end(), [](Use use) { return use == Use::DeoptOnly; })) return 0;
    const FrameLiveness live(image, function.function);
    std::size_t pruned = 0;
    for (std::size_t i = 0; i < function.states.size(); ++i) {
        auto& frame = function.states[i];
        if (uses[i] != Use::DeoptOnly || frame.caller != none || frame.function != function.function) continue;
        for (std::uint32_t slot = 0; slot < frame.slots.size(); ++slot) {
            auto& value = frame.slots[slot];
            if (value.kind == StateSlot::Kind::Unchanged || value.kind == StateSlot::Kind::Dead) continue;
            if (live.dead_scalar(frame.block, slot)) {
                value = {StateSlot::Kind::Dead, 0};
                ++pruned;
            }
        }
    }
    return pruned;
}

} // namespace neura::nvm::tier3::detail

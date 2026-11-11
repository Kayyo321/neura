// Tier 1 steps (docs/runtime/NVM.md, "Execution tiers"). Included inside
// Execution::State in machine.cpp; the cache is in tier1.hpp.
//
// A tier step is a reference step with another way of computing the
// graph's outputs. It charges the step, makes the scheduling decision, and
// checks the block's slot inputs for initialization in input order exactly
// as the reference step does (prepare_step), then computes the selected
// exit and the values the exits read, and commits the exit with the
// reference commit (commit_exit), which resolves values, checks references,
// and assigns in the same order. Only the lanes differ in representation:
// whole values instead of one int64 per bit.

    // Where a tier commit reads Lanes values: the reference output lanes, a
    // lowered block's values, or a wired block's input lanes.
    enum class TierLanes : std::uint8_t { None, Lifted, Wired };
    // The input lane a wired output lane is.
    struct WireSource {
        std::uint32_t input;
        std::uint32_t bit;
    };

    // Decides how tier 1 evaluates a block the first time this run enters it.
    void plan_tier(std::uint32_t function, std::uint32_t block, const nvr::Block& decoded, BlockPlan& plan) {
        const auto& graph = image_.graphs[decoded.graph];
        if (tier_inputs_.size() < decoded.inputs.size()) tier_inputs_.resize(decoded.inputs.size());
        if (graph.nodes.empty()) {
            // No neuron: no overflow, and each output lane is an input lane,
            // which holds a bit. Nothing needs proving.
            if (graph.outputs.empty()) {
                plan.tier = decoded.exits.size() == 1 ? BlockPlan::Tier::Empty : BlockPlan::Tier::Reference;
                return;
            }
            std::vector<WireSource> lanes;
            const auto* counts = input_lanes_.data() + plan.first_input;
            for (std::uint32_t i = 0; i < decoded.inputs.size(); ++i) {
                for (std::uint32_t bit = 0; bit < counts[i]; ++bit) lanes.push_back({i, bit});
            }
            plan.first_wire = wire_sources_.size();
            for (const auto wire : graph.outputs) wire_sources_.push_back(lanes.at(wire));
            plan.tier = BlockPlan::Tier::Wired;
            return;
        }
        const auto settled = tier_->lookup(function, block);
        if (settled.settled) {
            use_lowered(plan, settled.block);
            return;
        }
        plan.tier = BlockPlan::Tier::Counting;
        plan.threshold = tier_->threshold(function, block);
    }

    void use_lowered(BlockPlan& plan, const lift::LoweredBlock* lowered) {
        if (!lowered) {
            plan.tier = BlockPlan::Tier::Reference;
            return;
        }
        plan.tier = BlockPlan::Tier::Lifted;
        plan.lifted = lowered;
        if (tier_outputs_.size() < lowered->outputs.size()) tier_outputs_.resize(lowered->outputs.size());
        const auto registers = lowered->input_widths.size() + lowered->code.size();
        if (tier_registers_.size() < registers) tier_registers_.resize(registers);
    }

    std::optional<Event> tier_step(std::vector<std::int64_t>& scratch) {
        // Charge the step and check the inputs, deferring their lanes.
        if (auto event = begin_active_step<false, true>()) return event;
        auto& plan = *active_plan_;
        switch (plan.tier) {
        case BlockPlan::Tier::Empty:
            ++tier_steps_.empty;
            return finish_tier_step(TierLanes::None, 0);
        case BlockPlan::Tier::Wired:
            gather_inputs(*active_block_);
            if (tier_check_) check_tier(*active_block_, plan, nvr::absent, scratch);
            ++tier_steps_.wired;
            return finish_tier_step(TierLanes::Wired, 0);
        case BlockPlan::Tier::Counting: {
            if (++plan.runs < plan.threshold) break;
            const auto& frame = frames_.back();
            const auto resolved = tier_->resolve(frame.function, frame.block, plan.runs);
            if (!resolved.settled) {
                plan.threshold = static_cast<std::uint32_t>(std::min<std::uint64_t>(resolved.retry_after, 0xffffffffU));
                break;
            }
            use_lowered(plan, resolved.block);
            if (!resolved.block) break;
            [[fallthrough]];
        }
        case BlockPlan::Tier::Lifted: {
            const auto& lowered = *plan.lifted;
            gather_inputs(*active_block_);
            const auto exit = lift::execute(lowered, std::span(tier_inputs_).first(lowered.input_widths.size()),
                                            std::span(tier_outputs_).first(lowered.outputs.size()),
                                            tier_registers_);
            if (!exit) {
                // The lowered block declines: the reference evaluator decides.
                ++tier_steps_.declined;
                return finish_reference_step(scratch, true);
            }
            if (tier_check_) check_tier(*active_block_, plan, *exit, scratch);
            ++tier_steps_.lifted;
            return finish_tier_step(TierLanes::Lifted, *exit);
        }
        case BlockPlan::Tier::Reference:
            break;
        }
        ++tier_steps_.reference;
        ++plan.reference_steps;
        return finish_reference_step(scratch, true);
    }

    // Tier 0 steps per block, for the statistics of --tier-stats.
    void flush_reference_steps() {
        for (std::size_t f = 0; f + 1 < function_plans_.size(); ++f) {
            for (auto b = function_plans_[f]; b < function_plans_[f + 1]; ++b) {
                if (block_plans_[b].reference_steps != 0) {
                    tier_->add_reference_steps(static_cast<std::uint32_t>(f),
                                               static_cast<std::uint32_t>(b - function_plans_[f]),
                                               block_plans_[b].reference_steps);
                }
            }
        }
    }

    // The input values of the active block. prepare_step has checked each
    // slot input, and validated inputs address only slots and constants.
    void gather_inputs(const nvr::Block& block) {
        const auto& frame = frames_.back();
        auto* out = tier_inputs_.data();
        for (const auto& input : block.inputs) {
            *out++ = input.kind == nvr::ValueKind::Slot ? frame.slots[input.index] : const_values_[input.index];
        }
    }

    // The bit a wired block's output lane holds.
    std::uint64_t wired_lane(const BlockPlan& plan, std::uint32_t lane) const {
        const auto source = wire_sources_[plan.first_wire + lane];
        return (tier_inputs_[source.input] >> source.bit) & 1U;
    }

    // The value a Lanes descriptor reads during a tier commit.
    std::uint64_t tier_value(const nvr::Value& value) const {
        const auto& plan = *tier_plan_;
        if (tier_lanes_ == TierLanes::Lifted) {
            const auto& outputs = plan.lifted->outputs;
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                if (outputs[i].first_lane == value.index) return tier_outputs_[i];
            }
            // The lowering covers every value an exit reads.
            throw std::logic_error("a lowered block lacks a value its exit reads");
        }
        const auto count = nvr::lane_count(type(value.type));
        std::uint64_t bits = 0;
        for (unsigned i = 0; i < count; ++i) bits |= wired_lane(plan, value.index + i) << i;
        return bits;
    }

    // Commits exit `exit` (for a wired block, the exit its selector lanes
    // name) as finish_active_step commits the reference outputs.
    std::optional<Event> finish_tier_step(TierLanes lanes, std::uint32_t exit) {
        const auto& block = *std::exchange(active_block_, nullptr);
        struct Scope {
            State& state;
            ~Scope() { state.tier_lanes_ = TierLanes::None; }
        } scope{*this};
        tier_lanes_ = lanes;
        tier_plan_ = active_plan_;
        try {
            if (lanes == TierLanes::Wired && block.exits.size() > 1) {
                // As select() decodes the selector lanes, which are bits.
                exit = 0;
                for (std::uint32_t i = 0; i < active_plan_->selector_lanes; ++i) {
                    exit |= static_cast<std::uint32_t>(wired_lane(*active_plan_, block.selector_start + i)) << i;
                }
                if (exit >= block.exits.size()) {
                    fault(TrapKind::InvalidSelector, "selector " + std::to_string(exit) + " for " +
                                                         std::to_string(block.exits.size()) + " exits");
                }
            }
            auto event = commit_exit(block.exits[exit]);
            if (event && event->kind == Event::Kind::Completed && frames_.empty()) set_finished(*event);
            return event;
        } catch (const Fault& failure) {
            return trap(failure);
        }
    }

    // NEURA_TIER_CHECK=1: evaluates the active block by reference too and
    // aborts with a report when the exit or an output lane differs. `exit`
    // is the lowered block's exit (nvr::absent for a wired block, whose
    // lanes are compared directly).
    void check_tier(const nvr::Block& block, const BlockPlan& plan, std::uint32_t exit,
                    std::vector<std::int64_t>& scratch) {
        const auto& graph = image_.graphs[block.graph];
        auto wires = lanes(scratch, std::size_t{graph.input_lanes} + graph.nodes.size());
        write_inputs<true>(block, plan, frames_.back(), wires.first(graph.input_lanes));
        const auto& frame = frames_.back();
        std::string report;
        if (const auto failed = evaluate_reference(block.graph, wires)) {
            report = "the reference evaluator overflows at node " + std::to_string(*failed);
        }
        std::vector<std::int64_t> expected;
        for (const auto wire : graph.outputs) expected.push_back(wires[wire]);
        std::vector<std::int64_t> actual(graph.outputs.size(), -1);
        if (plan.tier == BlockPlan::Tier::Wired) {
            for (std::uint32_t lane = 0; lane < actual.size(); ++lane) {
                actual[lane] = static_cast<std::int64_t>(wired_lane(plan, lane));
            }
        }
        std::uint32_t reference_exit = 0;
        if (report.empty() && block.exits.size() > 1) {
            for (std::uint32_t i = 0; i < plan.selector_lanes; ++i) {
                const auto lane = expected[block.selector_start + i];
                if (lane != 0 && lane != 1) report = "the reference selector lane " + std::to_string(i) + " is not a bit";
                reference_exit |= static_cast<std::uint32_t>(lane & 1) << i;
            }
        }
        if (report.empty() && plan.tier == BlockPlan::Tier::Lifted) {
            const auto& taken = block.exits[exit];
            const auto& wanted = block.exits.at(reference_exit);
            // The lowering names the first trap exit of a check's reason;
            // trap exits of one reason do the same.
            const bool same_trap = taken.kind == nvr::ExitKind::Trap && wanted.kind == nvr::ExitKind::Trap &&
                                   taken.reason == wanted.reason;
            if (exit != reference_exit && !same_trap) {
                report = "exit " + std::to_string(exit) + " instead of " + std::to_string(reference_exit);
            } else if (taken.kind != nvr::ExitKind::Trap) {
                lift::write_lanes(*plan.lifted, exit, std::span(tier_outputs_).first(plan.lifted->outputs.size()),
                                  actual);
            } else {
                actual = expected;
            }
        }
        for (std::size_t lane = 0; report.empty() && lane < expected.size(); ++lane) {
            if (actual[lane] != expected[lane]) {
                report = "output lane " + std::to_string(lane) + " is " + std::to_string(actual[lane]) +
                         " instead of " + std::to_string(expected[lane]);
            }
        }
        if (report.empty()) return;
        std::string inputs;
        for (std::size_t i = 0; i < block.inputs.size(); ++i) {
            inputs += (i ? ", " : "") + std::to_string(tier_inputs_[i]);
        }
        std::fprintf(stderr,
                     "neura: NEURA_TIER_CHECK: tier 1 differs from the reference in function %u block %u "
                     "(graph %u, %s): %s; inputs [%s]\n",
                     frame.function, frame.block, block.graph,
                     plan.tier == BlockPlan::Tier::Lifted ? "lifted" : "wired", report.c_str(), inputs.c_str());
        std::abort();
    }

// Tier 2 regions (docs/runtime/NVM.md, "Execution tiers"). Included inside
// Execution::State in machine.cpp; the region programs are in tier2.hpp.
//
// The dispatcher enters a region at a block that has one, runs its blocks
// one after another, and returns when the region leaves to a block outside
// it or deoptimizes. Nothing a region does is visible as a difference from
// running its blocks one at a time at tier 1:
//
// - Steps. Each block a region completes is one step, added in bulk when
//   the region ends (to the run, to the function under --profile, and to
//   the running task). Nothing in a region reads the count, and Budget
//   checks guarantee that the step limit, the scheduler's quantum, and the
//   caller's allowance cannot be reached inside the region, so the trap or
//   the park happens at the same block, at tier 1.
// - Slots. Slot registers are written back, with their initialized flags,
//   whenever the region ends. A region allocates nothing, so no collection
//   can run while a reference lives only in a register.
// - Failures. A block commits only after every operation that can fail;
//   when one fails, the region writes back the state at the block's entry
//   and tier 1 runs the block, producing the trap (kind, reason, text,
//   function, block, callers) or the decline exactly as without regions.
// - Uninitialized slots. A slot read before any write on some path must be
//   initialized at entry, or the region is not entered; every other read
//   follows a write, and a slot never becomes uninitialized in a frame.
// - State operations inside a region call the reference state() with the
//   same arguments, checks, and traps; they read and write existing
//   storage only.
// - Native code. A region the cache has compiled (src/jit) runs as machine
//   code instead of interpret_region: the same program, op for op, with the
//   same entry checks, write-back, and counts around it, which are this
//   file's code for both.

    // A write of a state operation to heap or global storage, logged under
    // NEURA_TIER_CHECK=1 (checked_region).
    struct HeapWrite {
        bool global = false;
        bool field = false;
        std::uint64_t handle = 0;
        std::uint32_t index = 0;
        std::uint64_t old_value = 0;
        std::uint8_t old_initialized = 0;
        std::uint64_t new_value = 0;
        bool operator==(const HeapWrite&) const = default;
    };

    struct RegionOutcome {
        std::uint64_t blocks = 0;
        bool deoptimized = false;
    };

    // The region entered at the current block, or null. Counts arrivals at
    // a lifted, wired, or empty block until the image's threshold, then
    // asks the cache to form the region.
    const tier2::Region* region_for(std::uint32_t function, std::uint32_t block, BlockPlan& plan) {
        switch (plan.region_state) {
        case BlockPlan::RegionState::Settled:
            if (plan.region_threshold != 0 && tier_->count_region(function, block) >= plan.region_threshold) {
                // The cache may have formed the region again with more
                // blocks.
                const auto resolved = tier_->region(function, block, plan.region_threshold);
                plan.region = resolved.region;
                plan.region_threshold = resolved.retry_after;
                reserve_region(plan.region);
            }
            return plan.region;
        case BlockPlan::RegionState::Unknown:
            if (plan.tier == BlockPlan::Tier::Counting) return nullptr;
            if (plan.tier == BlockPlan::Tier::Reference) {
                plan.region_state = BlockPlan::RegionState::Settled;
                return nullptr;
            }
            plan.region_state = BlockPlan::RegionState::Counting;
            plan.region_threshold = tier_->region_threshold(function, block);
            [[fallthrough]];
        case BlockPlan::RegionState::Counting: {
            const auto arrivals = tier_->count_region(function, block);
            if (arrivals < plan.region_threshold) return nullptr;
            const auto resolved = tier_->region(function, block, arrivals);
            if (!resolved.settled) {
                plan.region_threshold = resolved.retry_after;
                return nullptr;
            }
            plan.region_state = BlockPlan::RegionState::Settled;
            plan.region = resolved.region;
            plan.region_threshold = resolved.retry_after;
            reserve_region(plan.region);
            return plan.region;
        }
        }
        return nullptr;
    }

    void reserve_region(const tier2::Region* region) {
        if (region && region_registers_.size() < region->register_count) {
            region_registers_.resize(region->register_count);
            region_initialized_.resize(region->register_count);
        }
    }

    // Runs regions from the current block while there are any, at most
    // `allowance` blocks in all. Returns the blocks run. When it returns,
    // the current block has no region, or a region deoptimized there, or
    // the allowance is spent: the next block runs at tier 1.
    std::uint64_t run_regions(std::uint64_t allowance) {
        std::uint64_t total = 0;
        while (total < allowance) {
            if (finished_ || pending_ || active_block_ || !started_ || frames_.empty()) break;
            if (scheduler_ && scheduler_->deciding) break;
            auto frame = frames_.back();
            auto& plan = block_plan(frame.function, frame.block);
            const auto* region = region_for(frame.function, frame.block, plan);
            if (!region) break;
            const auto* native = jit_ ? native_code(plan) : nullptr;
            const auto outcome = tier_check_ ? checked_region(*region, allowance - total, native)
                                             : run_region(*region, allowance - total, native);
            total += outcome.blocks;
            if (outcome.deoptimized) break;
        }
        return total;
    }

    // The native code of the plan's region, compiled by the cache the first
    // time any run asks (src/jit), or null: the region interpreter runs it.
    const jit::Program* native_code(BlockPlan& plan) {
        if (plan.native_region != plan.region) {
            plan.native = plan.region ? tier_->native(*plan.region) : nullptr;
            plan.native_region = plan.region;
        }
        return plan.native;
    }

    // The helpers of native code (jit::Context). Each runs the region
    // interpreter's code for its op and reports a failure the interpreter
    // deoptimizes for as status 1. An exception other than a Fault cannot
    // unwind through generated code: the helper keeps it and reports status
    // 2, and run_native rethrows it where the interpreter's exception would
    // have left the region, before any write-back.
    static std::uint64_t jit_check_reference(jit::Context* context, std::uint32_t type_index, std::uint64_t bits,
                                             std::uint64_t) {
        auto& self = *static_cast<State*>(context->owner);
        try {
            if (!(self.type(type_index).kind == nvr::TypeKind::Object && bits == 0 &&
                  self.image_.minor_version >= 2)) {
                self.typed_entry(bits, type_index);
            }
            context->status = 0;
        } catch (const Fault&) {
            context->status = 1;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            context->status = 2;
        }
        return 0;
    }

    static std::uint64_t jit_state(jit::Context* context, std::uint32_t pc, std::uint64_t, std::uint64_t) {
        auto& self = *static_cast<State*>(context->owner);
        const auto& region = *context->region;
        const auto& op = region.code[pc];
        try {
            auto& arguments = self.region_arguments_;
            arguments.clear();
            for (unsigned i = 0; i < op.width; ++i) {
                arguments.push_back(context->registers[region.operands[op.other + i]]);
            }
            const auto result = self.state(*region.exits[op.index], arguments);
            context->status = 0;
            return result;
        } catch (const Fault&) {
            context->status = 1;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            context->status = 2;
        }
        return 0;
    }

    static std::uint64_t jit_poll(jit::Context* context, std::uint32_t, std::uint64_t, std::uint64_t) {
        auto& self = *static_cast<State*>(context->owner);
        try {
            // As poll(), with the quantum out of reach (Budget).
            self.serve_dump_request();
            ++self.scheduler_->polls;
            context->status = 0;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            context->status = 2;
        }
        return 0;
    }

    // The region's native code: the region program compiled, with the same
    // ending as interpret_region's.
    void run_native(const jit::Program& native, const tier2::Region& region, std::uint64_t allowance,
                    std::uint64_t& done, std::uint32_t& next_block, bool& deoptimized) {
        ++region_counts_.native;
        auto& context = jit_context_;
        context.registers = region_registers_.data();
        context.initialized = region_initialized_.data();
        context.steps = &steps_;
        context.max_steps = limits_.max_steps;
        context.allowance = allowance;
        context.polls = scheduler_ ? &scheduler_->polls : nullptr;
        context.quantum = limits_.task_quantum;
        context.done = 0;
        context.next_block = 0;
        context.outcome = 0;
        context.status = 0;
        context.region = &region;
        native.run(context);
        done = context.done;
        next_block = context.next_block;
        deoptimized = true;
        switch (static_cast<jit::Outcome>(context.outcome)) {
        case jit::Outcome::Leave:
            deoptimized = false;
            ++region_counts_.leaves;
            break;
        case jit::Outcome::Budget: ++region_counts_.budget; break;
        case jit::Outcome::Checks: ++region_counts_.checks; break;
        case jit::Outcome::Selectors: ++region_counts_.selectors; break;
        case jit::Outcome::References: ++region_counts_.references; break;
        case jit::Outcome::StateTraps: ++region_counts_.state_traps; break;
        case jit::Outcome::Exits: ++region_counts_.exits; break;
        case jit::Outcome::Exception: std::rethrow_exception(std::exchange(region_exception_, nullptr));
        }
    }

    // The region interpreter: the region program, op by op.
    void interpret_region(const tier2::Region& region, std::uint64_t allowance, std::uint64_t& done,
                          std::uint32_t& next_block, bool& deoptimized) {
        auto* regs = region_registers_.data();
        auto* init = region_initialized_.data();
        const auto* code = region.code.data();
        std::uint32_t pc = 0;
        const auto deopt = [&](std::uint32_t block, std::uint64_t& counter) {
            next_block = block;
            deoptimized = true;
            ++counter;
        };
        for (bool running = true; running;) {
            const auto& op = code[pc++];
            switch (op.code) {
            case tier2::Code::Budget: {
                const auto& checkpoint = region.checkpoints[op.a];
                const auto used = steps_ + done;
                const bool steps_fit = used <= limits_.max_steps && limits_.max_steps - used >= checkpoint.steps;
                const bool allowed = allowance - done >= checkpoint.steps;
                const bool polls_fit = !scheduler_ || checkpoint.polls == 0 ||
                                       scheduler_->polls + checkpoint.polls < limits_.task_quantum;
                if (!steps_fit || !allowed || !polls_fit) {
                    deopt(op.block, region_counts_.budget);
                    running = false;
                }
                break;
            }
            case tier2::Code::Lift: {
                bool failed = false;
                const auto value = lift::evaluate(region.instructions[op.index], regs[op.a], regs[op.b], failed);
                if (failed) {
                    deopt(op.block, region_counts_.checks);
                    running = false;
                    break;
                }
                regs[op.dst] = value;
                if (op.flags & tier2::defines) init[op.dst] = 1;
                break;
            }
            case tier2::Code::Mask:
                regs[op.dst] = regs[op.a] & lift::mask_of(op.width);
                break;
            case tier2::Code::Move:
                regs[op.dst] = regs[op.a];
                break;
            case tier2::Code::Gather: {
                std::uint64_t value = 0;
                const auto* bits = region.gathers.data() + op.index;
                for (unsigned i = 0; i < op.width; ++i) value |= ((regs[bits[i].reg] >> bits[i].bit) & 1U) << i;
                regs[op.dst] = value;
                break;
            }
            case tier2::Code::Store:
                regs[op.dst] = regs[op.a];
                init[op.dst] = 1;
                break;
            case tier2::Code::Branch:
                pc = (regs[op.a] & 1U) != 0 ? op.target : op.other;
                break;
            case tier2::Code::Switch: {
                const auto index = regs[op.a];
                if (index >= op.width) {
                    deopt(op.block, region_counts_.selectors);
                    running = false;
                    break;
                }
                pc = region.operands[op.index + index];
                break;
            }
            case tier2::Code::Jump:
                done += op.flags & tier2::completes;
                pc = op.target;
                break;
            case tier2::Code::CheckRef: {
                const auto bits = regs[op.a];
                if (type(op.index).kind == nvr::TypeKind::Object && bits == 0 && image_.minor_version >= 2) break;
                try {
                    typed_entry(bits, op.index);
                } catch (const Fault&) {
                    deopt(op.block, region_counts_.references);
                    running = false;
                }
                break;
            }
            case tier2::Code::State: {
                auto& arguments = region_arguments_;
                arguments.clear();
                for (unsigned i = 0; i < op.width; ++i) arguments.push_back(regs[region.operands[op.other + i]]);
                try {
                    const auto result = state(*region.exits[op.index], arguments);
                    if (op.dst != nvr::absent) regs[op.dst] = result;
                } catch (const Fault&) {
                    deopt(op.block, region_counts_.state_traps);
                    running = false;
                }
                break;
            }
            case tier2::Code::Poll:
                if (scheduler_) {
                    // As poll(), with the quantum out of reach (Budget).
                    serve_dump_request();
                    ++scheduler_->polls;
                }
                break;
            case tier2::Code::Complete:
                ++done;
                break;
            case tier2::Code::Leave:
                done += op.flags & tier2::completes;
                next_block = op.target;
                ++region_counts_.leaves;
                running = false;
                break;
            case tier2::Code::Deopt:
                deopt(op.block, region_counts_.exits);
                running = false;
                break;
            }
        }
    }

    RegionOutcome run_region(const tier2::Region& region, std::uint64_t allowance, const jit::Program* native) {
        auto frame = frames_.back();
        auto* regs = region_registers_.data();
        auto* init = region_initialized_.data();
        const auto slot_count = region.slots.size();
        for (std::size_t r = 0; r < slot_count; ++r) {
            regs[r] = frame.slots[region.slots[r]];
            init[r] = frame.initialized[region.slots[r]];
        }
        for (const auto r : region.entry_reads) {
            if (!init[r]) {
                ++region_counts_.refused;
                return {0, true};
            }
        }
        for (const auto& [r, width] : region.entry_canonical) {
            if ((regs[r] >> width) != 0) {
                ++region_counts_.refused;
                return {0, true};
            }
        }
        if (region.uses_float && !lift::float_environment_is_default()) {
            ++region_counts_.refused;
            return {0, true};
        }
        for (const auto& constant : region.constants) {
            regs[constant.reg] = const_values_[constant.index] & lift::mask_of(constant.width);
        }
        for (const auto& [r, value] : region.immediates) regs[r] = value;
        ++region_counts_.entries;
        std::uint64_t done = 0;
        std::uint32_t next_block = 0;
        bool deoptimized = false;
        if (native) {
            run_native(*native, region, allowance, done, next_block, deoptimized);
        } else {
            interpret_region(region, allowance, done, next_block, deoptimized);
        }
        for (const auto r : region.written) {
            frame.slots[region.slots[r]] = regs[r];
            frame.initialized[region.slots[r]] = init[r];
        }
        frame.block = next_block;
        steps_ += done;
        if (limits_.profile) {
            if (scheduler_) current_task().steps += done;
            if (function_steps_.size() != image_.functions.size()) function_steps_.resize(image_.functions.size());
            function_steps_[region.function] += done;
        }
        tier_steps_.region += done;
        // For --tier-stats, kept in the block plans: no map work per run.
        block_plan(region.function, region.entry).region_steps += done;
        auto& ended = block_plan(region.function, next_block);
        ++(deoptimized ? ended.region_deopts : ended.region_leaves);
        return {done, deoptimized};
    }

    void flush_region_events() {
        if (region_counts_.entries == 0 && region_counts_.refused == 0 && region_counts_.budget == 0) return;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> steps;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> deopts;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> leaves;
        for (std::size_t f = 0; f + 1 < function_plans_.size(); ++f) {
            for (auto b = function_plans_[f]; b < function_plans_[f + 1]; ++b) {
                auto& plan = block_plans_[b];
                const std::pair key{static_cast<std::uint32_t>(f), static_cast<std::uint32_t>(b - function_plans_[f])};
                if (plan.region_steps) steps[key] += std::exchange(plan.region_steps, 0);
                if (plan.region_deopts) deopts[key] += std::exchange(plan.region_deopts, 0);
                if (plan.region_leaves) leaves[key] += std::exchange(plan.region_leaves, 0);
            }
        }
        tier_->add_region_events(region_counts_, steps, deopts, leaves);
        region_counts_ = {};
    }

    // NEURA_TIER_CHECK=1 for regions: the region runs, then everything it
    // changed is put back (slots, initialized flags, the block, steps,
    // per-function and task steps, polls, and every heap and global write,
    // from a log), the same number of blocks run at tier 1, and the two
    // results must agree: the block reached, every slot and flag of the
    // frame, the steps and polls, and the writes to heap and global storage
    // in order with their values. Reads are covered by the slots they feed.
    // It checks a region's native code (src/jit) the same way.
    RegionOutcome checked_region(const tier2::Region& region, std::uint64_t allowance, const jit::Program* native) {
        auto before = frames_.back();
        const auto function = before.function;
        const auto depth = frames_.size();
        const auto slot_count = image_.functions[function].frame_types.size();
        const std::vector<std::uint64_t> slots_before(before.slots, before.slots + slot_count);
        const std::vector<std::uint8_t> init_before(before.initialized, before.initialized + slot_count);
        const auto block_before = before.block;
        const auto steps_before = steps_;
        const auto function_steps_before = function_steps_;
        const auto task_steps_before = scheduler_ && limits_.profile ? current_task().steps : 0;
        const auto polls_before = scheduler_ ? scheduler_->polls : 0;

        std::vector<HeapWrite> region_writes;
        heap_log_ = &region_writes;
        const auto outcome = run_region(region, allowance, native);
        heap_log_ = nullptr;

        auto frame = frames_.back();
        const std::vector<std::uint64_t> slots_region(frame.slots, frame.slots + slot_count);
        const std::vector<std::uint8_t> init_region(frame.initialized, frame.initialized + slot_count);
        const auto block_region = frame.block;
        const auto steps_region = steps_;
        const auto polls_region = scheduler_ ? scheduler_->polls : 0;

        // Undo the region.
        for (auto it = region_writes.rbegin(); it != region_writes.rend(); ++it) undo_write(*it);
        std::copy(slots_before.begin(), slots_before.end(), frame.slots);
        std::copy(init_before.begin(), init_before.end(), frame.initialized);
        frame.block = block_before;
        steps_ = steps_before;
        function_steps_ = function_steps_before;
        if (scheduler_ && limits_.profile) current_task().steps = task_steps_before;
        if (scheduler_) scheduler_->polls = polls_before;
        tier_steps_.region -= outcome.blocks;

        // The same blocks at tier 1.
        std::vector<HeapWrite> tier1_writes;
        heap_log_ = &tier1_writes;
        std::string report;
        for (std::uint64_t k = 0; k < outcome.blocks && report.empty(); ++k) {
            if (auto event = step(wires_)) {
                report = "tier 1 produced an event after " + std::to_string(k) + " blocks";
            }
        }
        heap_log_ = nullptr;
        tier_steps_.region += outcome.blocks;
        if (report.empty() && frames_.size() != depth) report = "tier 1 left the frame";
        if (report.empty()) {
            auto after = frames_.back();
            if (after.block != block_region) {
                report = "tier 1 reached block " + std::to_string(after.block) + ", the region block " +
                         std::to_string(block_region);
            } else if (steps_ != steps_region) {
                report = "steps " + std::to_string(steps_) + " at tier 1, " + std::to_string(steps_region);
            } else if (scheduler_ && scheduler_->polls != polls_region) {
                report = "polls differ";
            } else if (tier1_writes != region_writes) {
                report = "heap writes differ (" + std::to_string(tier1_writes.size()) + " at tier 1, " +
                         std::to_string(region_writes.size()) + " in the region)";
            } else {
                for (std::size_t s = 0; s < slot_count && report.empty(); ++s) {
                    if (after.initialized[s] != init_region[s] ||
                        (after.initialized[s] && after.slots[s] != slots_region[s])) {
                        report = "slot " + std::to_string(s) + " is " + std::to_string(after.slots[s]) +
                                 " at tier 1, " + std::to_string(slots_region[s]) + " in the region";
                    }
                }
            }
        }
        if (!report.empty()) {
            std::fprintf(stderr,
                         "neura: NEURA_TIER_CHECK: a region%s differs from tier 1 in function %u (entry block %u, "
                         "%llu blocks): %s\n%s",
                         native ? " (native code)" : "", region.function, region.entry,
                         static_cast<unsigned long long>(outcome.blocks),
                         report.c_str(), tier2::describe(region).c_str());
            std::abort();
        }
        return outcome;
    }

    void undo_write(const HeapWrite& write) {
        if (write.global) {
            globals_[write.index] = write.old_value;
            global_initialized_[write.index] = write.old_initialized;
            return;
        }
        auto& item = entry(write.handle);
        item.cells[write.index] = write.old_value;
        if (write.field) item.initialized[write.index] = write.old_initialized;
    }

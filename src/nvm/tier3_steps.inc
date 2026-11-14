// Tier 3, the executor's side (src/nvm/tier3.hpp). Included inside
// Execution::State in machine.cpp.
//
// A native program is one function program turned into SSA with a lazily
// written frame. The run enters it where it would enter the program
// (run_regions), and an interpreted program transfers to it at a checkpoint:
// at its Budget, once the program's checkpoint arrivals reach the cache's
// threshold, run_program ends its activation as a Leave at that block, and
// the dispatcher's next entry runs the native program. The same happens at a
// Call whose callee has native code and a Return into a caller that has.
//
// What a native activation does is what run_program does, op for op, with
// these differences in where things are kept:
//
// - Slots live in SSA values, and the frame is written at WriteBack (before a
//   helper and a call), by a Return into the caller's result slot, and by
//   deoptimization, which materializes every frame its FrameState describes
//   (tier3_deoptimize, the code the IR interpreter and the native stubs of
//   a later stage share).
// - Blocks are counted by an SSA counter C that crosses native calls in the
//   context; steps are added to the run's at the points reference code can
//   read them: a helper, a call, a return, and every ending (tier3_flush).
// - A Budget compares C + K + P with the room, which tier3_flush recomputes
//   from the run's steps and the allowance: it holds what remains of the step
//   limit and of the block allowance for this activation chain.
//
// Tier 3 never runs with a scheduler (a task image's programs stay on the
// program interpreter) and never under NEURA_TIER_CHECK=1, whose region
// check runs programs on it.

    bool tier3_ready() const { return tier3_ && !scheduler_ && !tier_check_; }

    // The native code of `program`, or null. At a checkpoint (`count`), an
    // arrival is counted and the program is turned native once the cache's
    // threshold is reached.
    const tier3::Program* tier3_code(const tier2::Region& program, bool count) {
        if (!tier3_ready()) return nullptr;
        if (const auto* code = tier_->native_code(program)) return code;
        if (!count || tier_->count_native(program.program_id) < tier_->options().native_after) return nullptr;
        // Building the program is timed as tier 1 (--tier-stats).
        const TierTimes::Scope timing(times_, TierTimes::Category::Tier1);
        return tier_->compile_native(program);
    }

    // Adds the blocks and kernel steps since the last flush to the run's
    // steps, and under --profile to the running frame's function (R4), and
    // recomputes the room.
    void tier3_flush(tier3::Context& context) {
        const auto total = context.C + context.K;
        if (total != context.flushed) {
            const auto count = total - context.flushed;
            steps_ += count;
            if (limits_.profile) {
                if (function_steps_.size() != image_.functions.size()) function_steps_.resize(image_.functions.size());
                function_steps_[frames_.back().function] += count;
            }
            tier_steps_.region += count;
            region_counts_.program_steps += count;
            native_counts_.steps += count;
            if (context.region) block_plan(context.region->function, context.region->entry).region_steps += count;
            context.flushed = total;
        }
        tier3_room(context);
    }

    // The steps a Budget may let pass: what remains of the step limit (the
    // steps already flushed are in `steps_`) and the allowance, in blocks.
    void tier3_room(tier3::Context& context) {
        const auto left = limits_.max_steps - std::min(steps_, limits_.max_steps);
        const auto limit = left > std::numeric_limits<std::uint64_t>::max() - context.flushed
                               ? std::numeric_limits<std::uint64_t>::max()
                               : left + context.flushed;
        context.room = std::min(limit, context.allowance);
    }

    static State& tier3_self(tier3::Context* context) { return *static_cast<State*>(context->owner); }

    static void tier3_frame(tier3::Context* context, std::uint64_t** slots, std::uint8_t** initialized) {
        auto frame = tier3_self(context).frames_.back();
        *slots = frame.slots;
        *initialized = frame.initialized;
    }

    static std::uint64_t tier3_run_constant(tier3::Context* context, std::uint32_t index) {
        return tier3_self(context).const_values_[index];
    }

    // An inline state operation: its Fault is the deoptimization the program
    // interpreter makes, any other exception is kept for the executor.
    static tier3::Status tier3_state(tier3::Context* context, const nvr::Exit* exit, const std::uint64_t* arguments,
                                     std::uint32_t count, std::uint64_t* result) {
        auto& self = tier3_self(context);
        try {
            self.region_arguments_.assign(arguments, arguments + count);
            *result = self.state(*exit, self.region_arguments_);
            return tier3::Status::Ok;
        } catch (const Fault&) {
            return tier3::Status::Fault;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            return tier3::Status::Exception;
        }
    }

    static tier3::Status tier3_check_reference(tier3::Context* context, std::uint32_t type_index,
                                               std::uint64_t bits) {
        auto& self = tier3_self(context);
        try {
            if (!(self.type(type_index).kind == nvr::TypeKind::Object && bits == 0 && self.image_.minor_version >= 2)) {
                self.typed_entry(bits, type_index);
            }
            return tier3::Status::Ok;
        } catch (const Fault&) {
            return tier3::Status::Fault;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            return tier3::Status::Exception;
        }
    }

    // R3: the block's step is in the context's counts; it is added to the
    // run's before the reference state operation runs, whose fault is the
    // run's trap, the block having been charged.
    static tier3::Status tier3_helper(tier3::Context* context, const nvr::Exit* exit, const std::uint64_t* arguments,
                                      std::uint32_t count, std::uint64_t* result) {
        auto& self = tier3_self(context);
        self.tier3_flush(*context);
        try {
            self.region_arguments_.assign(arguments, arguments + count);
            *result = self.state(*exit, self.region_arguments_);
        } catch (const Fault& failure) {
            self.trap(failure);
            ++self.region_counts_.traps;
            ++self.native_counts_.traps;
            return tier3::Status::Trapped;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            return tier3::Status::Exception;
        }
        ++self.region_counts_.helpers;
        // A kernel charged steps of its own.
        self.tier3_room(*context);
        return tier3::Status::Ok;
    }

    // What the state's frame levels say to write into a frame: the slots and
    // flags that are not unchanged, and the block.
    static void tier3_write_level(const tier3::FrameState& state, const std::uint64_t* values, std::uint64_t* slots,
                                  std::uint8_t* initialized) {
        for (std::size_t i = 0; i < state.slots.size(); ++i) {
            const auto& slot = state.slots[i];
            if (slot.kind == tier3::StateSlot::Kind::Value) slots[i] = values[slot.value];
            else if (slot.kind == tier3::StateSlot::Kind::Const) slots[i] = slot.value;
            const auto& flag = state.flags[i];
            if (flag.kind == tier3::StateFlag::Kind::One) initialized[i] = 1;
            else if (flag.kind == tier3::StateFlag::Kind::Value) initialized[i] = static_cast<std::uint8_t>(values[flag.value] & 1U);
        }
    }

    static void tier3_write_back(tier3::Context* context, const tier3::Function* fn, std::uint32_t state_id,
                                 const std::uint64_t* values) {
        auto& self = tier3_self(context);
        // The physical frame is the outermost level of the chain.
        const auto* state = &fn->states[state_id];
        while (state->caller != tier3::none) state = &fn->states[state->caller];
        auto frame = self.frames_.back();
        tier3_write_level(*state, values, frame.slots, frame.initialized);
        frame.block = state->block;
    }

    // The frames of a state, as tier 1 would have them: the physical frame
    // written, each inlined frame pushed as push_frame makes it (zero,
    // uninitialized slots, the return slot and block, `called_from` the
    // caller's program) and written, and every frame's block where tier 1
    // resumes in it (the innermost frame's: where it runs next).
    static void tier3_materialize(tier3::Context* context, const tier3::Function* fn, std::uint32_t state_id,
                                  const std::uint64_t* values) {
        auto& self = tier3_self(context);
        std::vector<const tier3::FrameState*> chain;
        for (auto s = state_id; s != tier3::none; s = fn->states[s].caller) chain.push_back(&fn->states[s]);
        std::reverse(chain.begin(), chain.end());
        {
            auto frame = self.frames_.back();
            tier3_write_level(*chain.front(), values, frame.slots, frame.initialized);
            frame.block = chain.front()->block;
        }
        for (std::size_t level = 1; level < chain.size(); ++level) {
            const auto& state = *chain[level];
            self.frames_.push(state.function, self.image_.functions[state.function].frame_types.size(), {},
                              state.return_slot, state.return_block);
            self.frames_.set_called_from(fn->programs[chain[level - 1]->program]);
            auto frame = self.frames_.back();
            tier3_write_level(state, values, frame.slots, frame.initialized);
            frame.block = state.block;
        }
    }

    // Leaves compiled code at `state_id`: its frames materialized, the
    // counters of the state in the context, the steps flushed.
    static void tier3_deoptimize(tier3::Context* context, const tier3::Function* fn, std::uint32_t state_id,
                                 tier3::Reason reason, const std::uint64_t* values) {
        auto& self = tier3_self(context);
        tier3_materialize(context, fn, state_id, values);
        const auto& leaf = fn->states[state_id];
        context->C = values[leaf.blocks];
        context->K = leaf.kernel_steps == tier3::none ? 0 : values[leaf.kernel_steps];
        context->next_block = leaf.block;
        context->reason = reason;
        self.tier3_flush(*context);
    }

    static void tier3_leave(tier3::Context* context, std::uint32_t block) {
        auto& self = tier3_self(context);
        self.frames_.back().block = block;
        context->next_block = block;
        self.tier3_flush(*context);
    }

    // commit_exit's Call, after the commit and the caller's block: the step
    // is added to the run's, the callee's frame pushed, whose fault (the frame
    // limit) is the run's trap.
    static tier3::Status tier3_call(tier3::Context* context, const nvr::Exit* exit, std::uint32_t callee,
                                    const std::uint64_t* arguments, std::uint32_t count,
                                    const tier2::Region* caller) {
        auto& self = tier3_self(context);
        self.tier3_flush(*context);
        self.region_arguments_.assign(arguments, arguments + count);
        try {
            self.push_frame(callee, self.region_arguments_, exit->result_slot, exit->resume);
        } catch (const Fault& failure) {
            self.trap(failure);
            ++self.region_counts_.traps;
            ++self.native_counts_.traps;
            return tier3::Status::Trapped;
        }
        self.frames_.set_called_from(caller);
        ++self.region_counts_.calls;
        return tier3::Status::Ok;
    }

    // The native code a call enters: the callee's program has its native
    // code, run_program's conditions for entering a callee hold (its early
    // reads are parameters, which fit their types, and the floating-point
    // environment suits it), and there is room for its frames.
    static const tier3::Program* tier3_callee_code(tier3::Context* context, std::uint32_t callee) {
        auto& self = tier3_self(context);
        if (callee >= self.function_programs_.size()) return nullptr;
        const auto* program = self.function_programs_[callee];
        if (program == nullptr || (program->uses_float && !context->float_default) || program->entries.empty() ||
            program->entries.front().block != 0) {
            return nullptr;
        }
        const auto frame = self.frames_.back();
        for (const auto slot : program->entries.front().reads) {
            if (slot >= program->parameter_count) return nullptr;
        }
        for (std::uint32_t slot = 0; slot < program->parameter_count; ++slot) {
            const auto width = program->slot_widths[slot];
            if (width < 64 && (frame.slots[slot] >> width) != 0) return nullptr;
        }
        const auto* code = self.tier3_code(*program, false);
        if (code == nullptr || self.frames_.size() + code->ir.max_inline_depth > self.limits_.max_frames) return nullptr;
        return code;
    }

    // commit_exit's Return for a frame that is neither the last nor a
    // callback's (anything else is tier 1's: a Fault here). `blocks` is the
    // count before the return block's step.
    static tier3::Status tier3_return(tier3::Context* context, std::uint64_t value, bool, std::uint64_t blocks) {
        auto& self = tier3_self(context);
        const auto depth = self.frames_.size();
        if (depth < 2 || self.frames_.header(depth - 1).boundary) return tier3::Status::Fault;
        context->C = blocks + 1;
        self.tier3_flush(*context);
        const auto header = self.frames_.header(depth - 1);
        self.frames_.pop_back();
        auto caller = self.frames_.back();
        if (header.return_slot != nvr::absent) {
            caller.slots[header.return_slot] = value;
            caller.initialized[header.return_slot] = 1;
        }
        caller.block = header.return_block;
        context->next_block = header.return_block;
        ++self.region_counts_.returns;
        return tier3::Status::Ok;
    }

    static constexpr tier3::Helpers tier3_helpers_{
        &State::tier3_frame,   &State::tier3_run_constant, &State::tier3_state,       &State::tier3_check_reference,
        &State::tier3_helper,  &State::tier3_write_back,   &State::tier3_deoptimize,  &State::tier3_leave,
        &State::tier3_call,    &State::tier3_callee_code,  &State::tier3_return,
    };

    // A native program entered at entries[entry_index] by the dispatcher: the
    // checks run_program makes, then the IR runs until it ends. At every
    // ending every frame is as tier 1 would have it, the innermost frame's
    // block is where the run goes on, and the steps are in the run's.
    RegionOutcome run_native_program(const tier3::Program& code, std::uint32_t entry_index, std::uint64_t allowance) {
        const auto& entered = *code.ir.region;
        {
            auto frame = frames_.back();
            const auto& entry = entered.entries[entry_index];
            bool refuse = entered.uses_float && !lift::float_environment_is_default();
            for (const auto slot : entry.reads) refuse = refuse || !frame.initialized[slot];
            for (std::size_t slot = 0; slot < entered.slot_widths.size() && !refuse; ++slot) {
                const auto width = entered.slot_widths[slot];
                refuse = width < 64 && frame.initialized[slot] && (frame.slots[slot] >> width) != 0;
            }
            refuse = refuse || frames_.size() + code.ir.max_inline_depth > limits_.max_frames;
            if (refuse) {
                ++region_counts_.refused;
                ++native_counts_.refused;
                return {0, true};
            }
        }
        ++region_counts_.entries;
        ++native_counts_.entered;
        auto& context = tier3_context_;
        context.owner = this;
        context.helpers = &tier3_helpers_;
        context.C = 0;
        context.K = 0;
        context.flushed = 0;
        context.allowance = allowance;
        context.native_depth = 0;
        context.float_default = lift::float_environment_is_default();
        context.region = &entered;
        context.next_block = 0;
        tier3_room(context);
        const auto result = tier3::interpret(context, code, entry_index, false);
        std::uint64_t blocks = context.C;
        bool deoptimized = false;
        switch (result.outcome) {
        case tier3::Outcome::Leave:
            ++region_counts_.leaves;
            ++native_counts_.left;
            break;
        case tier3::Outcome::Deopt:
            deoptimized = true;
            switch (context.reason) {
            case tier3::Reason::Budget: ++region_counts_.budget; ++native_counts_.budget; break;
            case tier3::Reason::Checks: ++region_counts_.checks; ++native_counts_.checks; break;
            case tier3::Reason::Selectors: ++region_counts_.selectors; ++native_counts_.selectors; break;
            case tier3::Reason::References: ++region_counts_.references; ++native_counts_.references; break;
            case tier3::Reason::StateTraps: ++region_counts_.state_traps; ++native_counts_.state_traps; break;
            case tier3::Reason::Guards: ++region_counts_.checks; ++native_counts_.guards; break;
            case tier3::Reason::Exits: ++region_counts_.exits; ++native_counts_.exits; break;
            }
            break;
        case tier3::Outcome::Trapped:
            // A helper's fault became the run's trap: the block that
            // trapped is the finished event's round, not this activation's.
            return {blocks - 1, true};
        case tier3::Outcome::Exception: std::rethrow_exception(std::exchange(region_exception_, nullptr));
        case tier3::Outcome::Return: throw std::logic_error("tier 3: the first activation returned a value");
        }
        const auto frame = frames_.back();
        auto& ended = block_plan(frame.function, frame.block);
        ++(deoptimized ? ended.region_deopts : ended.region_leaves);
        if (deoptimized) ++native_deopts_[{frame.function, frame.block}];
        return {blocks, deoptimized};
    }

    void flush_native_events() {
        if (!tier3_) return;
        tier_->add_native_events(native_counts_, native_deopts_);
        native_counts_ = {};
        native_deopts_.clear();
    }

    std::vector<Execution::Tier3Frame> tier3_frames() const {
        std::vector<Execution::Tier3Frame> list;
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            const auto& header = frames_.header(i);
            const auto count = image_.functions[header.function].frame_types.size();
            list.push_back({header.function, header.block, header.return_slot, header.return_block, header.boundary,
                            header.called_from, std::vector<std::uint64_t>(frames_.slots(i), frames_.slots(i) + count),
                            std::vector<std::uint8_t>(frames_.initialized(i), frames_.initialized(i) + count)});
        }
        return list;
    }

    Execution::Tier3Ending tier3_run(const tier3::Program& program, std::uint32_t entry) {
        if (!started_) start();
        auto& context = tier3_context_;
        context.owner = this;
        context.helpers = &tier3_helpers_;
        context.C = 0;
        context.K = 0;
        context.flushed = 0;
        context.allowance = std::numeric_limits<std::uint64_t>::max();
        context.native_depth = 0;
        context.float_default = lift::float_environment_is_default();
        context.region = program.ir.region;
        context.next_block = 0;
        tier3_room(context);
        // Tests only: the step limit of a stopped run does not apply.
        context.room = std::numeric_limits<std::uint64_t>::max();
        const auto result = tier3::interpret(context, program, entry, false);
        return {static_cast<unsigned>(result.outcome), static_cast<unsigned>(context.reason), context.next_block,
                context.C};
    }

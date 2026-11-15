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

    // The variant of native programs this run uses (tier3::Variant): under
    // --profile one that inlines nothing, so that every call and return
    // attributes its steps as the reference does; under NEURA_TIER_CHECK=1
    // the checkable one (which tier3_ready keeps from running).
    tier3::Variant tier3_variant() const {
        if (limits_.profile) return tier3::Variant::Profile;
        if (tier_check_) return tier3::Variant::Checkable;
        return tier3::Variant::Standard;
    }

    // The native code of `program`, or null. At a checkpoint (`count`), an
    // arrival is counted and the program is turned native once the cache's
    // threshold is reached.
    const tier3::Program* tier3_code(const tier2::Region& program, bool count) {
        if (!tier3_ready()) return nullptr;
        const auto variant = tier3_variant();
        if (const auto* code = tier_->native_code(program, variant)) return code;
        if (!count || tier_->count_native(program.program_id) < tier_->options().native_after) return nullptr;
        // Building the program is timed as tier 1 (--tier-stats).
        const TierTimes::Scope timing(times_, TierTimes::Category::Tier1);
        return tier_->compile_native(program, variant);
    }

    // The speculation record of a Dispatch op (tier3::DispatchRecord): kept
    // only while tier 3 is on, read only by the inliner.
    void tier3_record(const tier2::Region& program, std::uint32_t table, std::uint64_t tag) {
        if (auto* record = tier_->dispatch_record(program, table)) record->record(tag);
    }

    static void tier3_record_dispatch(tier3::Context* context, const tier2::Region* program, std::uint32_t table,
                                      std::uint64_t tag) {
        tier3_self(context).tier3_record(*program, table, tag);
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
            if (context.region) {
                // The plan of the program last flushed to, kept: block_plans_
                // does not move once made.
                if (context.region != tier3_plan_region_) {
                    tier3_plan_ = &block_plan(context.region->function, context.region->entry);
                    tier3_plan_region_ = context.region;
                }
                tier3_plan_->region_steps += count;
            }
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

    // The helper's arguments, copied word by word: native code has just
    // stored them as separate words, and a wide load would span two of those
    // stores and wait for both. Vectorization is off for the loop so that it
    // stays 8-byte loads and stores. The vector is resized only when the count
    // changes and keeps its capacity.
    void tier3_copy_arguments(const std::uint64_t* arguments, std::uint32_t count) {
        if (region_arguments_.size() != count) region_arguments_.resize(count);
        std::uint64_t* target = region_arguments_.data();
#if defined(__clang__)
#pragma clang loop vectorize(disable) interleave(disable)
#elif defined(__GNUC__) && __GNUC__ >= 14
#pragma GCC novector
#endif
        for (std::uint32_t i = 0; i < count; ++i) {
            target[i] = arguments[i];
        }
    }

    // An inline state operation: its Fault is the deoptimization the program
    // interpreter makes, any other exception is kept for the executor.
    static tier3::Status tier3_state(tier3::Context* context, const nvr::Exit* exit, const std::uint64_t* arguments,
                                     std::uint32_t count, std::uint64_t* result) {
        auto& self = tier3_self(context);
        try {
            self.tier3_copy_arguments(arguments, count);
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
            self.tier3_copy_arguments(arguments, count);
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
        self.tier3_copy_arguments(arguments, count);
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

    // --- The helpers of machine code (tier3::NativeHelpers) -------------------
    //
    // Each does what the IR interpreter does through the helper of the same
    // name above, for the running activation's program (context->program),
    // then reports a NativeStatus and leaves the context describing the
    // innermost frame, which native code reloads. None lets an exception
    // through native code: one other than a Fault is kept for the executor.

    static void tier3_native_frame(tier3::Context* context) {
        auto frame = tier3_self(context).frames_.back();
        context->slots = frame.slots;
        context->flags = frame.initialized;
        context->block = &frame.block;
    }

    static void tier3_native_status(tier3::Context* context, tier3::NativeStatus status) {
        context->status = static_cast<std::uint32_t>(status);
        tier3_native_frame(context);
    }

    static void tier3_native_exception(tier3::Context* context) {
        tier3_self(context).region_exception_ = std::current_exception();
        tier3_native_status(context, tier3::NativeStatus::Exception);
    }

    static tier3::NativeStatus tier3_native_of(tier3::Status status) {
        switch (status) {
        case tier3::Status::Ok: return tier3::NativeStatus::Ok;
        case tier3::Status::Fault: return tier3::NativeStatus::Fault;
        case tier3::Status::Trapped: return tier3::NativeStatus::Trapped;
        default: return tier3::NativeStatus::Exception;
        }
    }

    // The deoptimization values: one per SSA value of `program`.
    void tier3_values(tier3::Context& context, const tier3::Program& program) {
        if (tier3_values_.size() < program.ir.insts.size()) tier3_values_.resize(program.ir.insts.size());
        context.values = tier3_values_.data();
    }

    static std::uint64_t tier3_native_state(tier3::Context* context, std::uint32_t exit, const std::uint64_t* arguments,
                                            std::uint32_t count) {
        std::uint64_t result = 0;
        try {
            const auto status = tier3_state(context, context->program->ir.exits[exit], arguments, count, &result);
            tier3_native_status(context, tier3_native_of(status));
        } catch (...) {
            tier3_native_exception(context);
        }
        return result;
    }

    static std::uint64_t tier3_native_check_reference(tier3::Context* context, std::uint32_t type, std::uint64_t bits) {
        try {
            tier3_native_status(context, tier3_native_of(tier3_check_reference(context, type, bits)));
        } catch (...) {
            tier3_native_exception(context);
        }
        return 0;
    }

    static std::uint64_t tier3_native_helper(tier3::Context* context, std::uint32_t exit, const std::uint64_t* arguments,
                                             std::uint32_t count) {
        std::uint64_t result = 0;
        try {
            context->region = context->program->ir.region;
            const auto status = tier3_helper(context, context->program->ir.exits[exit], arguments, count, &result);
            tier3_native_status(context, tier3_native_of(status));
        } catch (...) {
            tier3_native_exception(context);
        }
        return result;
    }

    // The callee's frame pushed (or the frame limit's trap), then its native
    // code, entered by the caller: the code a call enters is the IR
    // interpreter's (tier3_callee_code), up to the native depth; past it, or
    // without code, the activation leaves at the callee's block 0.
    static std::uint64_t tier3_native_call(tier3::Context* context, std::uint32_t exit, std::uint32_t callee,
                                           const std::uint64_t* arguments, std::uint32_t count) {
        try {
            const auto& caller = *context->program;
            context->region = caller.ir.region;
            if (tier3_call(context, caller.ir.exits[exit], callee, arguments, count, caller.ir.programs[0]) ==
                tier3::Status::Trapped) {
                tier3_native_status(context, tier3::NativeStatus::Trapped);
                return 0;
            }
            return tier3_native_entry(context, callee);
        } catch (...) {
            tier3_native_exception(context);
            return 0;
        }
    }

    // After a push native code made itself (the call block's step is in C,
    // not yet added to the run's steps: nothing reads them before the next
    // flush, and no fault can come of the push).
    static std::uint64_t tier3_native_enter(tier3::Context* context, std::uint32_t callee) {
        try {
            context->region = context->program->ir.region;
            return tier3_native_entry(context, callee);
        } catch (...) {
            tier3_native_exception(context);
            return 0;
        }
    }

    // The callee's native code, entered by the caller, once its frame is
    // pushed: up to the native depth; past it, or without code, the
    // activation leaves at the callee's block 0.
    static std::uint64_t tier3_native_entry(tier3::Context* context, std::uint32_t callee) {
        const auto* code = tier3_callee_code(context, callee);
        if (code == nullptr || !code->machine || context->native_depth >= tier3::max_native_depth) {
            tier3_leave(context, 0);
            tier3_native_status(context, tier3::NativeStatus::Left);
            return 0;
        }
        ++context->native_depth;
        context->program = code;
        context->region = code->ir.region;
        tier3_self(context).tier3_values(*context, *code);
        tier3_native_status(context, tier3::NativeStatus::Ok);
        return reinterpret_cast<std::uint64_t>(code->machine->address() + code->entry_code.at(0));
    }

    static std::uint64_t tier3_native_return(tier3::Context* context, std::uint64_t value, std::uint64_t blocks) {
        try {
            context->region = context->program->ir.region;
            tier3_native_status(context, tier3_native_of(tier3_return(context, value, true, blocks)));
        } catch (...) {
            tier3_native_exception(context);
        }
        return value;
    }

    static std::uint64_t tier3_native_leave(tier3::Context* context, std::uint32_t block) {
        try {
            context->region = context->program->ir.region;
            tier3_leave(context, block);
            tier3_native_status(context, tier3::NativeStatus::Ok);
        } catch (...) {
            tier3_native_exception(context);
        }
        return 0;
    }

    static std::uint64_t tier3_native_deoptimize(tier3::Context* context, std::uint32_t state, std::uint32_t reason) {
        try {
            context->region = context->program->ir.region;
            tier3_deoptimize(context, &context->program->ir, state, static_cast<tier3::Reason>(reason), context->values);
            tier3_native_status(context, tier3::NativeStatus::Ok);
        } catch (...) {
            tier3_native_exception(context);
        }
        return 0;
    }

    static std::uint64_t tier3_native_evaluate(tier3::Context* context, std::uint32_t inst, std::uint64_t a,
                                               std::uint64_t b) {
        bool failed = false;
        const auto value = lift::evaluate(context->program->ir.insts[inst].lifted, a, b, failed);
        tier3_native_status(context, failed ? tier3::NativeStatus::Fault : tier3::NativeStatus::Ok);
        return value;
    }

    static constexpr tier3::NativeHelpers tier3_native_helpers_{
        &State::tier3_native_state,  &State::tier3_native_check_reference, &State::tier3_native_helper,
        &State::tier3_native_call,   &State::tier3_native_return,          &State::tier3_native_leave,
        &State::tier3_native_deoptimize, &State::tier3_native_evaluate,         &State::tier3_native_enter,
    };

    static constexpr tier3::Helpers tier3_helpers_{
        &State::tier3_frame,   &State::tier3_run_constant, &State::tier3_state,       &State::tier3_check_reference,
        &State::tier3_helper,  &State::tier3_write_back,   &State::tier3_deoptimize,  &State::tier3_leave,
        &State::tier3_call,    &State::tier3_callee_code,  &State::tier3_return,       &State::tier3_record_dispatch,
    };

    // A native program entered at entries[entry_index] by the dispatcher: the
    // checks run_program makes, then the IR runs until it ends. At every
    // ending every frame is as tier 1 would have it, the innermost frame's
    // block is where the run goes on, and the steps are in the run's.
    RegionOutcome run_native_program(const tier3::Program& code, std::uint32_t entry_index, std::uint64_t allowance) {
        const auto& entered = *code.ir.region;
        // The machine code of the entry, made now if it is made on first use;
        // an entry without it is refused.
        const std::uint8_t* address = nullptr;
        if (tier3_native_ && code.machine) {
            std::uint64_t bytes = 0;
            std::string failure;
            if (code.entry_code.at(entry_index) == jit::native::lazy) {
                const TierTimes::Scope timing(times_, TierTimes::Category::Tier1);
                address = code.entry(entry_index, bytes, failure);
            } else {
                address = code.entry(entry_index, bytes, failure);
            }
            if (bytes != 0 || !failure.empty()) tier_->note_native_entry(bytes, failure);
        }
        {
            auto frame = frames_.back();
            const auto& entry = entered.entries[entry_index];
            bool refuse = entered.uses_float && !lift::float_environment_is_default();
            refuse = refuse || (tier3_native_ && code.machine && address == nullptr);
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
        tier3::Result result;
        if (tier3_native_ && code.machine) {
            // The machine code, from the trampoline; an activation the
            // dispatcher entered ends its Return as a Leave.
            context.native_depth = 1;
            context.program = &code;
            context.run_constants = const_values_.data();
            context.functions = tier3_native_helpers_;
            context.status = 0;
            context.frames = frames_.record();
            context.max_frames = limits_.max_frames;
            context.inline_calls = 0;
            context.inline_returns = 0;
            tier3_values(context, code);
            tier3_native_frame(&context);
            code.enter(context, address);
            region_counts_.calls += context.inline_calls;
            region_counts_.returns += context.inline_returns;
            result.outcome = static_cast<tier3::Outcome>(context.outcome);
            if (result.outcome == tier3::Outcome::Return) result.outcome = tier3::Outcome::Leave;
        } else {
            result = tier3::interpret(context, code, entry_index, false);
        }
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

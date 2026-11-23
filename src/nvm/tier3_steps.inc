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
// program interpreter). Under NEURA_TIER_CHECK=1 the region check runs the
// checkable variant (tier3::Variant), whose writes go through state(), which
// logs them for the check.

    bool tier3_ready() const { return tier3_ && !scheduler_; }

    // The variant of native programs this run uses (tier3::Variant): under
    // --profile one that inlines nothing, so that every call and return
    // attributes its steps as the reference does; under NEURA_TIER_CHECK=1
    // the checkable one, which the region check runs.
    tier3::Variant tier3_variant() const {
        if (limits_.profile) return tier3::Variant::Profile;
        if (tier_check_) return tier3::Variant::Checkable;
        return tier3_full_frames_ ? tier3::Variant::Standard : tier3::Variant::SparseFrames;
    }

    // The native code of `program`, or null. At a checkpoint (`count`), an
    // arrival is counted and the program is turned native once the cache's
    // threshold is reached.
    const tier3::Program* tier3_code(const tier2::Region& program, bool count) {
        if (!tier3_ready()) return nullptr;
        const auto variant = tier3_variant();
        if (!tier3_checked_loops_.empty()) {
            const auto found = tier3_checked_loops_.find(program.function);
            if (found != tier3_checked_loops_.end()) {
                auto& checked = found->second;
                if (checked.compiled_sites != checked.sites.size()) {
                    // The failure that adds an exclusion clears the callee
                    // cell and unwinds the entire native chain before the
                    // next lookup. Its superseded builds can now be freed.
                    checked.versions.clear();
                    checked.compiled_sites = checked.sites.size();
                }
                auto version = checked.versions.find(&program);
                if (version == checked.versions.end()) {
                    const TierTimes::Scope timing(times_, TierTimes::Category::Tier1);
                    auto replacement = tier_->rebuild_native(program, variant, checked.sites);
                    version = checked.versions.emplace(&program, std::move(replacement)).first;
                }
                return version->second.get();
            }
        }
        if (const auto* code = tier_->native_code(program, variant)) return code;
        if (!count) return nullptr;
        const auto arrivals = tier_->count_native(program.program_id);
        if (arrivals < tier_->options().native_after || !tier_->native_ready(program, arrivals)) return nullptr;
        // Building the program is timed as tier 1 (--tier-stats).
        const TierTimes::Scope timing(times_, TierTimes::Category::Tier1);
        return tier_->compile_native(program, variant);
    }

    void tier3_note_preflight_failure(std::uint32_t function, tier3::LoopSite site) {
        // Count failures at the original loop header, including across OSR,
        // direct native entry and Tier 2 program refreshes. Other runs using
        // the same cache have independent feedback and callee cells.
        constexpr unsigned failure_limit = 3;
        auto& failures = tier3_preflight_failures_[{function, site}];
        if (failures >= failure_limit || ++failures != failure_limit) return;
        tier3_checked_loops_[function].sites.push_back(site);
        if (function < tier3_cells_.size()) tier3_cells_[function] = {};
        ++native_counts_.loop_preflight_backoffs;
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
    // The room is for C: the kernel steps the chain charged up to now (the
    // context's K, part of `flushed`) are taken off. A Budget of a function
    // without inline kernels, where K does not change, compares C + P with it;
    // one of a function with them, C + (K - context.K) + P.
    void tier3_room(tier3::Context& context) {
        const auto left = limits_.max_steps - std::min(steps_, limits_.max_steps);
        const auto base = context.flushed - std::min(context.K, context.flushed);
        const auto limit = left > std::numeric_limits<std::uint64_t>::max() - base
                               ? std::numeric_limits<std::uint64_t>::max()
                               : left + base;
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
        if (exit->operation == nvr::StateOp::AllocateObject) ++self.native_counts_.slow_allocations;
        if (exit->operation == nvr::StateOp::AppendElement) ++self.native_counts_.slow_appends;
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
        if (exit->kind == nvr::ExitKind::State && exit->operation == nvr::StateOp::Kernel) ++self.native_counts_.kernel_helpers;
        // A state helper may charge steps of its own.
        self.tier3_room(*context);
        return tier3::Status::Ok;
    }

    // What the state's frame levels say to write into a frame: the slots and
    // flags that are not unchanged, and the block. A deferred float value is
    // written canonical (Function::frame_canon).
    static void tier3_write_level(const tier3::Function& fn, const tier3::FrameState& state, const std::uint64_t* values,
                                  std::uint64_t* slots, std::uint8_t* initialized) {
        for (std::size_t i = 0; i < state.slots.size(); ++i) {
            const auto& slot = state.slots[i];
            if (slot.kind == tier3::StateSlot::Kind::Value) {
                const auto v = static_cast<std::size_t>(slot.value);
                const auto format = v < fn.frame_canon.size() ? fn.frame_canon[v] : 0;
                slots[i] = format == 0 ? values[v] : tier3::canonical_nan(values[v], format);
            } else if (slot.kind == tier3::StateSlot::Kind::Const) {
                slots[i] = slot.value;
            }
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
        tier3_write_level(*fn, *state, values, frame.slots, frame.initialized);
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
            tier3_write_level(*fn, *chain.front(), values, frame.slots, frame.initialized);
            frame.block = chain.front()->block;
        }
        for (std::size_t level = 1; level < chain.size(); ++level) {
            const auto& state = *chain[level];
            self.frames_.push(state.function, self.image_.functions[state.function].frame_types.size(), {},
                              state.return_slot, state.return_block);
            self.frames_.set_called_from(fn->programs[chain[level - 1]->program]);
            auto frame = self.frames_.back();
            tier3_write_level(*fn, state, values, frame.slots, frame.initialized);
            frame.block = state.block;
        }
    }

#ifdef NEURA_TEST_FRAME_HOOKS
    // The checker compares full and sparse executions using the original
    // image's continuation mask, computed independently of the native IR.
    void record_frame_replay(const tier3::Function& fn, std::uint32_t state_id) {
        ++frame_replay_events_;
        // Keep a separate sample of specializations with one native entry.
        // The interpreter-resume graph may otherwise use the entire budget.
        const bool entry_only = fn.region && fn.entries.size() == 1 && fn.region->entries.size() > 1;
        auto& sampled = entry_only ? frame_replay_entry_reports_ : frame_replay_reports_;
        if (sampled == 32) return;
        ++sampled;
        const auto& leaf = fn.states[state_id];
        std::size_t omitted = 0;
        for (auto state = state_id; state != tier3::none; state = fn.states[state].caller) {
            omitted += std::ranges::count_if(fn.states[state].slots, [](const auto& slot) {
                return slot.kind == tier3::StateSlot::Kind::Dead;
            });
        }
        frame_replay_omitted_ += omitted;
        std::ostringstream output;
        output << "frame-recovery {\"function\":" << leaf.function << ",\"block\":" << leaf.block
               << ",\"step\":" << steps_ << ",\"variant\":" << static_cast<unsigned>(fn.variant)
               << ",\"omitted\":" << omitted << ",\"frames\":[";
        std::map<std::uint32_t, tier3::detail::FrameLiveness> liveness;
        for (std::size_t i = 0; i < frames_.size(); ++i) {
            const auto& frame = frames_.header(i);
            const auto& live = liveness.try_emplace(frame.function, image_, frame.function).first->second;
            const auto size = image_.functions[frame.function].frame_types.size();
            const auto* flags = frames_.initialized(i);
            const auto* slots = frames_.slots(i);
            if (i != 0) output << ',';
            output << "{\"function\":" << frame.function << ",\"block\":" << frame.block
                   << ",\"initialized\":[";
            for (std::size_t slot = 0; slot < size; ++slot) {
                if (slot != 0) output << ',';
                output << (flags[slot] != 0 ? "true" : "false");
            }
            output << "],\"values\":[";
            std::vector<std::uint32_t> dead;
            for (std::uint32_t slot = 0; slot < size; ++slot) {
                if (slot != 0) output << ',';
                const auto masked = live.dead_scalar(frame.block, slot);
                if (masked) dead.push_back(slot);
                if (masked || flags[slot] == 0) output << "null";
                else output << slots[slot];
            }
            output << "],\"dead\":[";
            for (std::size_t slot = 0; slot < dead.size(); ++slot) {
                if (slot != 0) output << ',';
                output << dead[slot];
            }
            output << "]}";
        }
        output << "]}\n";
        std::fputs(output.str().c_str(), stderr);
    }
#endif

    // Leaves compiled code at `state_id`: its frames materialized, the
    // counters of the state in the context, the steps flushed.
    static void tier3_deoptimize(tier3::Context* context, const tier3::Function* fn, std::uint32_t state_id,
                                 tier3::Reason reason, const std::uint64_t* values) {
        auto& self = tier3_self(context);
        tier3_materialize(context, fn, state_id, values);
        const auto& leaf = fn->states[state_id];
        context->C = values[leaf.blocks];
        // A function without inline kernels leaves K as the chain's callees
        // charged it.
        if (leaf.kernel_steps != tier3::none) context->K = values[leaf.kernel_steps];
        context->next_block = leaf.block;
        context->reason = reason;
        self.tier3_flush(*context);
#ifdef NEURA_TEST_FRAME_HOOKS
        if (reason == tier3::Reason::FrameReplay) {
            self.record_frame_replay(*fn, state_id);
            return;
        }
#endif
        if (leaf.loop_preflight) self.tier3_note_preflight_failure(fn->function, {leaf.function, leaf.block});
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
        const auto* published = self.tier3_code(*program, false);
        if (published == nullptr) return nullptr;
        const auto& code = published->for_entry(0);
        if (self.frames_.size() + code.ir.max_inline_depth > self.limits_.max_frames) return nullptr;
        if (code.ir.uses_float && !context->float_default) return nullptr;
        if (!tier3_clean_entry(code.ir, 0, frame.slots)) return nullptr;
        return &code;
    }

    // Widen: the reference store of an element, which widens the storage
    // first when the value does not fit it.
    static tier3::Status tier3_set_element(tier3::Context* context, std::uint64_t handle, std::uint64_t index,
                                           std::uint64_t bits) {
        auto& self = tier3_self(context);
        try {
            self.heap_.set_element(self.heap_.find(handle), static_cast<std::size_t>(index), bits);
            return tier3::Status::Ok;
        } catch (...) {
            self.region_exception_ = std::current_exception();
            return tier3::Status::Exception;
        }
    }

    // NEURA_TIER3_VERIFY=1, after an inline allocation or append: the heap's
    // own consistency (Heap::check) and the executor's accounting.
    static void tier3_check_heap(tier3::Context* context) {
        auto& self = tier3_self(context);
        auto problem = self.heap_.check();
        if (problem.empty() && self.peak_cells_ < self.live_cells_) problem = "peak heap cells below the live cells";
        if (problem.empty() && self.next_collection_ > self.limits_.max_heap_cells) problem = "the collection threshold above the limit";
        if (problem.empty() && self.heap_.last_handle() > self.limits_.max_handles) problem = "a handle above the handle limit";
        if (problem.empty()) return;
        std::fprintf(stderr, "neura: NEURA_TIER3_VERIFY: the heap is not consistent after an inline allocation or append: %s\n",
                     problem.c_str());
        std::abort();
    }

    static std::uint64_t tier3_native_set_element(tier3::Context* context, std::uint64_t handle, std::uint64_t index,
                                                  std::uint64_t bits) {
        tier3_native_status(context, tier3_native_of(tier3_set_element(context, handle, index, bits)));
        return 0;
    }

    // An allocation's or append's slow path inside inlined code (design 3.6):
    // the state's frames as tier 1 has them at the block's commit, the
    // helper under R3, its result in the innermost frame, which goes on at
    // the exit's resume block outside native code.
    static tier3::Status tier3_inlined_helper(tier3::Context* context, const tier3::Function* fn, std::uint32_t state,
                                              const std::uint64_t* values, const nvr::Exit* exit,
                                              const std::uint64_t* arguments, std::uint32_t count, std::uint64_t* result) {
        auto& self = tier3_self(context);
        tier3_materialize(context, fn, state, values);
        const auto status = tier3_helper(context, exit, arguments, count, result);
        if (status != tier3::Status::Ok) return status;
        auto frame = self.frames_.back();
        if (exit->result_slot != nvr::absent) {
            frame.slots[exit->result_slot] = *result;
            frame.initialized[exit->result_slot] = 1;
        }
        frame.block = exit->resume;
        context->next_block = exit->resume;
        return tier3::Status::Ok;
    }

    static std::uint64_t tier3_native_inlined_helper(tier3::Context* context, std::uint32_t exit, const std::uint64_t* arguments,
                                                     std::uint32_t count, std::uint32_t state) {
        std::uint64_t result = 0;
        try {
            const auto& ir = context->program->ir;
            context->region = ir.region;
            tier3_native_status(context, tier3_native_of(tier3_inlined_helper(context, &ir, state, context->values,
                                                                                ir.exits[exit], arguments, count, &result)));
        } catch (...) {
            tier3_native_exception(context);
        }
        return result;
    }

    static std::uint64_t tier3_native_grow_list(tier3::Context* context, void* entry) {
        try {
            tier3_self(context).heap_.grow_list_entry(entry);
            tier3_native_status(context, tier3::NativeStatus::Ok);
        } catch (...) {
            tier3_native_exception(context);
        }
        return 0;
    }

    static void tier3_native_check_heap(tier3::Context* context) {
        tier3_check_heap(context);
        tier3_native_status(context, tier3::NativeStatus::Ok);
    }

    // What native code reads and writes of the heap, the globals and the
    // cell accounting (tier3::Context). Inline allocation is off when the
    // pool is not in use, a fleet shares the cell count, or a heap log
    // records writes.
    void tier3_publish(tier3::Context& context, bool float_default) {
        if (tier3_cells_.size() != image_.functions.size()) tier3_cells_.assign(image_.functions.size(), {});
        // A host boundary can change the environment between native chains.
        // Drop floating-point entries before a nondefault chain can use them.
        // Its enter helper will refuse to cache them again in that environment.
        if (context.float_default && !float_default) {
            for (auto& cell : tier3_cells_) {
                if (cell.program && (cell.region->uses_float || cell.program->ir.uses_float)) cell = {};
            }
        }
        context.float_default = float_default;
        context.callee_cells = tier3_cells_.data();
        context.handle_table = heap_.handle_table();
        context.allocator = heap_.native_allocator();
        context.globals = globals_.data();
        context.global_initialized = global_initialized_.data();
        context.live_cells = &live_cells_;
        context.next_collection = &next_collection_;
        context.peak_cells = &peak_cells_;
        context.max_handles = limits_.max_handles;
        context.fast_allocation = Heap::native_allocation() && fleet_heap_ == nullptr && heap_log_ == nullptr;
        context.fast_allocations = 0;
        context.fast_appends = 0;
        context.cell_entries = 0;
        context.heap_functions = {&State::tier3_native_set_element, &State::tier3_native_check_heap,
                                  &State::tier3_native_inlined_helper, &State::tier3_native_grow_list};
    }

    // The statistics of the inline allocations and appends of an activation.
    void tier3_count_fast(const tier3::Context& context) {
        native_counts_.fast_allocations += context.fast_allocations;
        native_counts_.fast_appends += context.fast_appends;
        native_counts_.cell_entries += context.cell_entries;
    }

    // commit_exit's Return for a frame that is neither the last nor a
    // callback's (anything else is tier 1's: a Fault here). `blocks` is the
    // count before the return block's step.
    static tier3::Status tier3_return(tier3::Context* context, std::uint64_t value, bool, std::uint64_t blocks,
                                      std::uint32_t words) {
        auto& self = tier3_self(context);
        const auto depth = self.frames_.size();
        if (depth < 2 || self.frames_.header(depth - 1).boundary) return tier3::Status::Fault;
        // Tier 1 validates a frame that returns with a class hook event open.
        if (!self.frames_.hooks.empty() && self.frames_.hooks.back().depth >= depth) return tier3::Status::Fault;
        context->C = blocks + 1;
        self.tier3_flush(*context);
        const auto header = self.frames_.header(depth - 1);
        // The storage regions the frame entered and did not release (NVR
        // 1.7), as commit_exit's Return releases them. Native code pops a
        // frame in place only for a function without EnterRegion
        // (tier3::Function::enters_region), so every such frame comes here.
        if (!self.frames_.regions.empty()) self.release_regions_from(self.frames_, depth);
        self.frames_.pop_back();
        auto caller = self.frames_.back();
        if (header.return_slot != nvr::absent) {
            caller.slots[header.return_slot] = value;
            caller.initialized[header.return_slot] = 1;
            // The words after the first of a multi-word function (NVR 1.7),
            // which the Return left in the context.
            for (std::uint32_t word = 0; word < words; ++word) {
                caller.slots[header.return_slot + 1 + word] = context->result_words[word];
                caller.initialized[header.return_slot + 1 + word] = 1;
            }
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
        auto& self = tier3_self(context);
        self.tier3_values(*context, *code);
        self.tier3_cache_callee(callee, *code);
        ++self.native_counts_.helper_entries;
        tier3_native_status(context, tier3::NativeStatus::Ok);
        return reinterpret_cast<std::uint64_t>(code->machine->address() + code->entry_code.at(0));
    }

    // Fills the callee's cell (tier3::CalleeCell) after the enter helper
    // entered `code` as the native code of the run's program of `callee`, so
    // that native code enters it again without the helper while what the
    // helper settled from the program and the variant alone still holds:
    // the standard variant (the profile and checkable ones keep the helper),
    // no clean slots at entry 0 (whose NaNs the helper checks), and machine
    // code with its entry 0 made. What depends on the call (the argument
    // widths, the native depth, the frame limit) is checked by the native code
    // that uses the cell. The dispatcher drops floating-point cells before a
    // nondefault chain, and use_program clears a cell when its program changes.
    void tier3_cache_callee(std::uint32_t callee, const tier3::Program& code) {
        if (!tier3::standard_variant(tier3_variant()) || callee >= tier3_cells_.size()) return;
        const auto* program = callee < function_programs_.size() ? function_programs_[callee] : nullptr;
        if (program == nullptr || !code.machine) return;
        if (!code.ir.clean_slots.empty() && !code.ir.clean_slots.front().empty()) return;
        if (code.entry_code.at(0) == jit::native::lazy) return;
        auto& cell = tier3_cells_[callee];
        cell.program = &code;
        cell.region = code.ir.region;
        cell.frames = code.ir.max_inline_depth;
        cell.entry = code.machine->address() + code.entry_code.at(0);
    }

    static std::uint64_t tier3_native_return(tier3::Context* context, std::uint64_t value, std::uint64_t blocks,
                                             std::uint32_t words) {
        try {
            context->region = context->program->ir.region;
            tier3_native_status(context, tier3_native_of(tier3_return(context, value, true, blocks, words)));
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
        &State::tier3_call,    &State::tier3_callee_code,  &State::tier3_return,      &State::tier3_set_element,
        &State::tier3_check_heap, &State::tier3_record_dispatch, &State::tier3_inlined_helper,
    };

    // Whether every slot the entry's code takes as clean
    // (Function::clean_slots) holds no NaN but the canonical NaN of its float
    // format.
    static bool tier3_clean_entry(const tier3::Function& fn, std::uint32_t entry, const std::uint64_t* slots) {
        if (entry >= fn.clean_slots.size()) return true;
        for (const auto slot : fn.clean_slots[entry]) {
            if (tier3::canonical_nan(slots[slot], fn.slot_types[slot].width) != slots[slot]) return false;
        }
        return true;
    }

    // The initialized flags of a frame the dispatcher enters a program with
    // are what the image proves (tier2.cpp, analyse_flags): 0 or 1 where the
    // fact says so, and equal where two slots share a fact. A frame that
    // differs aborts the process, for NEURA_TIER3_VERIFY.
    template <typename Flags>
    static void tier3_check_flag_facts(const tier2::Region& program, const tier2::Region::Entry& entry,
                                       const Flags& initialized) {
        std::map<std::uint32_t, std::uint32_t> seen;
        for (std::uint32_t slot = 0; slot < entry.flags.size() && slot < program.slot_widths.size(); ++slot) {
            const auto fact = entry.flags[slot];
            if (fact == tier2::Region::flag_unknown) continue;
            const std::uint32_t flag = initialized[slot] ? 1 : 0;
            bool wrong = false;
            if (fact <= 1) {
                wrong = flag != fact;
            } else {
                const auto [it, first] = seen.try_emplace(fact, flag);
                wrong = !first && it->second != flag;
            }
            if (wrong) {
                std::fprintf(stderr,
                             "neura: NEURA_TIER3_VERIFY: the frame at function %u block %u has the initialized flag of slot %u "
                             "as %u, against the fact %u of the image analysis\n",
                             program.function, entry.block, slot, flag, fact);
                std::abort();
            }
        }
    }

    // A native program entered at entries[entry_index] by the dispatcher: the
    // checks run_program makes, then the IR runs until it ends. At every
    // ending every frame is as tier 1 would have it, the innermost frame's
    // block is where the run goes on, and the steps are in the run's.
    RegionOutcome run_native_program(const tier3::Program& published, std::uint32_t entry_index, std::uint64_t allowance) {
        const auto& initial = published.for_entry(entry_index);
        tier3::Program::EntryCode selected{&initial, nullptr, entry_index};
        // The code of the entry, made now if it is made on first use;
        // an entry without it is refused.
        if (tier3_native_ && initial.machine) {
            std::uint64_t bytes = 0;
            std::string failure;
            if (initial.entry_code.at(entry_index) == jit::native::lazy) {
                const TierTimes::Scope timing(times_, TierTimes::Category::Tier1);
                selected = initial.entry(image_, entry_index, bytes, failure);
            } else {
                selected = initial.entry(image_, entry_index, bytes, failure);
            }
            if (bytes != 0 || !failure.empty()) tier_->note_native_entry(bytes, failure, selected.program != &initial);
        }
        const auto& code = *selected.program;
        const auto& entered = *code.ir.region;
        const auto* address = selected.address;
        const bool float_default = lift::float_environment_is_default();
        {
            auto frame = frames_.back();
            const auto& entry = entered.entries[entry_index];
            // Native float code (the program's, or an inline sqrt's) runs only
            // in the default floating-point environment.
            bool refuse = (entered.uses_float || code.ir.uses_float) && !float_default;
            refuse = refuse || (tier3_native_ && code.machine && address == nullptr);
            for (const auto slot : entry.reads) refuse = refuse || !frame.initialized[slot];
            // The machine code of a function program's entry 0 takes the
            // initialized flag of every slot but a parameter as 0, as a call
            // leaves the callee's frame (jit/tier3_lower.cpp, simplify_phis).
            if (tier3_native_ && code.machine && entered.program && entry.block == 0) {
                for (auto slot = entered.parameter_count; slot < entered.slot_widths.size() && !refuse; ++slot) {
                    refuse = frame.initialized[slot];
                }
            }
            for (std::size_t slot = 0; slot < entered.slot_widths.size() && !refuse; ++slot) {
                const auto width = entered.slot_widths[slot];
                refuse = width < 64 && frame.initialized[slot] && (frame.slots[slot] >> width) != 0;
            }
            // NEURA_TIER3_VERIFY=1: the flags the image proves at this block
            // (Region::Entry::flags) are the frame's.
            if (code.verify && tier3_native_ && code.machine) tier3_check_flag_facts(entered, entry, frame.initialized);
            refuse = refuse || frames_.size() + code.ir.max_inline_depth > limits_.max_frames;
            refuse = refuse || !tier3_clean_entry(code.ir, selected.index, frame.slots);
            if (refuse) {
                ++region_counts_.refused;
                ++native_counts_.refused;
                return {0, true};
            }
        }
        ++region_counts_.entries;
        ++native_counts_.entered;
        if (selected.program != &initial) ++native_counts_.osr_entries;
        auto& context = tier3_context_;
        context.owner = this;
        context.helpers = &tier3_helpers_;
        context.C = 0;
        context.K = 0;
        context.flushed = 0;
        context.allowance = allowance;
        context.native_depth = 0;
        context.region = &entered;
        context.next_block = 0;
        tier3_publish(context, float_default);
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
            result = tier3::interpret(context, code, selected.index, false);
        }
        tier3_count_fast(context);
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
            case tier3::Reason::FrameReplay: break;
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

    std::vector<std::uint64_t> heap_snapshot() const {
        auto words = heap_.snapshot();
        words.push_back(live_cells_);
        words.push_back(peak_cells_);
        words.push_back(next_collection_);
        return words;
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
        context.region = program.ir.region;
        context.next_block = 0;
        tier3_publish(context, lift::float_environment_is_default());
        tier3_room(context);
        // Tests only: the step limit of a stopped run does not apply.
        context.room = std::numeric_limits<std::uint64_t>::max();
        const auto result = tier3::interpret(context, program, entry, false);
        return {static_cast<unsigned>(result.outcome), static_cast<unsigned>(context.reason), context.next_block,
                context.C};
    }

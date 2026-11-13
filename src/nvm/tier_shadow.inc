// The shadow-run oracle of NEURA_TIER_CHECK=2 (docs/runtime/NVM.md,
// "Controls"). Included inside Execution::State in machine.cpp.
//
// A run that would run above tier 1 gets a shadow: a second State of the
// same image, arguments, and limits, started from the same initial state and
// restricted to tier 1 (no regions, no native code, no check of its own).
// The shadow shares the image and the tier cache, whose lowered blocks are
// immutable once published; its counts of block executions join the run's,
// which changes only when blocks are lifted. It has its own heap, frames,
// globals, scheduler, and counters, and no host bindings, observer, task
// dump, schedule log, or timeline, and it adds nothing to the cache's
// statistics. It produces no output and has no external effect: it never
// reaches a host service, so it loads no native library and touches no
// file, socket, or console.
//
// Sync points are every return of compiled code to the dispatcher
// (shadow_returned), every event the run hands to its caller
// (shadow_event), and the end of each callback a host service invokes while
// the run waits for it (shadow_invoke_callback). At each one the shadow is
// advanced one step at a time to the run's step count, and the two are
// compared in a fixed order (shadow_compare): steps, the outcome, the event,
// the frames, the globals, the heap's counters, function steps, and the
// scheduler. The heaps are compared entry by entry at the end of the run, at
// every sync point under NEURA_TIER_CHECK_HEAP=1, and otherwise at a sync
// point once at least 64 sync points and at least shadow_heap_spacing times
// as many steps as the heap had live entries at the last comparison have
// passed since it, so that the walks cost a bounded share of the run. The
// first difference is reported and the process aborts.
//
// What enters the run from outside reaches the shadow after the run has
// taken it, through the same entry points of Execution: a completion
// (complete), a delivery (deliver), a clock reading (advance_clock), and a
// callback (invoke_callback), whose host requests and nested callbacks are
// recorded as the run makes them and then replayed into the shadow, the
// recorded completions answering its requests. A task dump reads the run
// and changes nothing, so the shadow needs none. Modes whose inputs are not
// passed on this way turn the oracle off with one line on standard error: a
// graph device, a schedule chooser, more than one parallel worker, a fleet,
// and a caller that says so (RunLimits::tier_check_off).
//
// What is compared is every field of the state a run carries from one step
// to the next, except these. The bits of an uninitialized slot, field, or
// global, which nothing can read and a region may leave as it found them.
// The tier machinery, which differs between the run and its shadow by
// design and decides only how a block is evaluated: block plans, the tier
// cache's counts, region registers and native context, tier statistics,
// and the timeline. Scratch buffers that every step overwrites before it
// reads them (graph inputs, outputs, wires, exit values, kernel buffers),
// and the collector's mark stacks, which are empty between steps. Caches
// built from the image alone (trace plans, kernel bindings, names). The
// mark bit of each heap entry, clear between collections. And the
// arguments of outstanding parallel items beyond their shape: an item's
// argument is the value its run is given, and that run has its own shadow.

    enum class ShadowOff : std::uint8_t { Device, Chooser, Workers, Fleet, Caller };

    // The heaps are compared again once the run has taken this many steps
    // per live heap entry found at the last comparison (and 64 sync points
    // have passed), so that the walks cost at most about half a visit of an
    // entry per step. Measured on binary-trees, whose heap is large and whose
    // regions return millions of times: 1 makes the checked run about 4
    // times as long as an unchecked one, 2 about 3.5 times, 8 about 3 times.
    static constexpr std::uint64_t shadow_heap_spacing = 2;

    // A callback a host service invoked while the run waited for a request,
    // with the host requests it made and their completions, in order.
    struct ShadowCall;
    struct ShadowRequest {
        HostRequest request;
        std::vector<std::unique_ptr<ShadowCall>> calls;
        HostCompletion completion;
    };
    struct ShadowCall {
        std::string name;
        std::uint32_t version = 0;
        std::vector<HostValue> arguments;
        std::vector<std::unique_ptr<ShadowRequest>> requests;
        std::optional<HostValue> result;
    };

    // The step count and the innermost frame's function and block.
    struct ShadowPlace {
        std::uint64_t step = 0;
        std::uint32_t function = nvr::absent;
        std::uint32_t block = nvr::absent;
    };

    struct Shadow {
        std::unique_ptr<State> state;
        TierShadowHooks* hooks = nullptr;
        bool heap_always = false;
        std::uint64_t syncs = 0;
        // The end of the run has been compared.
        bool ended = false;
        // Where compiled code is about to be entered, where it was last
        // entered (when `compiled`), and the last sync point without a
        // difference.
        ShadowPlace entering;
        ShadowPlace entered;
        bool compiled = false;
        ShadowPlace synced;
        // Live heap entries at the last comparison of the heaps, and the
        // step count and sync points since then.
        std::size_t heap_entries = 0;
        std::uint64_t heap_step = 0;
        std::uint64_t heap_syncs = 0;
        // The requests of callbacks the run is serving, innermost last.
        std::vector<ShadowRequest*> serving;
    };

    // Decides, when the state is made, whether the run gets a shadow, and
    // makes it. `allowed` is false for a parallel item whose run has none.
    void plan_shadow(bool allowed) {
        if (!allowed || (!limits_.tier_shadow && !tier_shadow_enabled())) return;
        make_shadow();
    }

    NEURA_SHADOW_PATH void make_shadow() {
        if (std::min(limits_.tier, tier_ceiling()) < 2) return;
        if (limits_.graph_device) return shadow_off(ShadowOff::Device, nullptr);
        if (limits_.chooser) return shadow_off(ShadowOff::Chooser, nullptr);
        if (limits_.parallel_workers > 1) return shadow_off(ShadowOff::Workers, nullptr);
        if (limits_.tier_check_off) return shadow_off(ShadowOff::Caller, limits_.tier_check_off);
        // The shadow replaces the region check of NEURA_TIER_CHECK=1.
        tier_check_ = false;
        start_shadow();
    }

    // A member of a fleet, which the oracle does not check.
    NEURA_SHADOW_PATH void shadow_leave_fleet() {
        shadow_.reset();
        shadow_off(ShadowOff::Fleet, nullptr);
    }

    // advance() with the shadow's sync points: the same loop, and a sync
    // point after compiled code returns and at each event.
    NEURA_SHADOW_PATH Event shadowed_advance() {
        if (finished_) {
            shadow_event(*finished_);
            return *finished_;
        }
        if (pending_) {
            throw std::logic_error("advance() called while a host request is pending");
        }
        serve_dump_request();
        for (;;) {
            // A difference reported to the test hooks ends the shadow, and the
            // run goes on without sync points.
            if (tier2_) {
                if (shadow_) shadow_entering();
                // A program that trapped in place (a call's frame limit) has
                // finished the run: the next step's event is the sync point.
                if (run_regions(std::numeric_limits<std::uint64_t>::max()) != 0 && shadow_ && !finished_) {
                    shadow_returned();
                }
            }
            if (auto event = step(wires_)) {
                const bool ended = event->kind == Event::Kind::Completed || event->kind == Event::Kind::Trapped;
                time_as(ended ? TierTimes::Category::Other : TierTimes::Category::Host);
                if (shadow_) shadow_event(*event);
                return std::move(*event);
            }
        }
    }

    // The oracle is not applied: one line on standard error, once per
    // process for each reason, or to the test hooks.
    NEURA_SHADOW_PATH void shadow_off(ShadowOff reason, const char* why) {
        static constexpr const char* reasons[] = {
            "the run uses a graph device",
            "a schedule chooser picks the tasks",
            "parallel items run on more than one worker",
            "the run is a member of a fleet",
        };
        const auto index = static_cast<unsigned>(reason);
        const std::string line =
            std::string("neura: NEURA_TIER_CHECK=2 is not applied: ") + (why ? why : reasons[index]) + "\n";
        if (limits_.tier_shadow && limits_.tier_shadow->notice) {
            limits_.tier_shadow->notice(line);
            return;
        }
        static std::atomic<unsigned> printed{0};
        const auto bit = 1U << index;
        if ((printed.fetch_or(bit) & bit) != 0) return;
        std::fputs(line.c_str(), stderr);
    }

    // Makes the shadow as the run starts, from the run's own inputs.
    NEURA_SHADOW_PATH void start_shadow() {
        auto limits = limits_;
        limits.tier = 1;
        limits.tier_cache = tier_;
        limits.jit = jit::Mode::Off;
        limits.tier_times = nullptr;
        limits.observer = nullptr;
        limits.block_observer = nullptr;
        limits.task_dump = nullptr;
        limits.schedule = nullptr;
        limits.chooser = nullptr;
        limits.tier_shadow = nullptr;
        limits.tier_check_off = nullptr;
        auto shadow = std::make_unique<Shadow>();
        shadow->hooks = limits_.tier_shadow;
        shadow->heap_always = tier_shadow_heap_always() || (shadow->hooks && shadow->hooks->every_heap);
        if (item_) {
            shadow->state = std::make_unique<State>(image_, limits, *item_, false);
        } else if (start_function_) {
            shadow->state = std::make_unique<State>(image_, *start_function_, start_arguments_, limits);
        } else {
            shadow->state = std::make_unique<State>(image_, std::span<const std::string>(arguments_), limits, entry_);
        }
        shadow->state->shadow_role_ = true;
        shadow->state->tier_check_ = false;
        shadow_ = std::move(shadow);
    }

    ShadowPlace shadow_place() const {
        ShadowPlace place{steps_};
        if (!frames_.empty()) {
            const auto& top = frames_.header(frames_.size() - 1);
            place.function = top.function;
            place.block = top.block;
        }
        return place;
    }

    static std::string shadow_place_text(const ShadowPlace& place) {
        if (place.function == nvr::absent) return "no function (step " + std::to_string(place.step) + ")";
        return "function " + std::to_string(place.function) + " block " + std::to_string(place.block) + " (step " +
               std::to_string(place.step) + ")";
    }

    // --- Sync points ---------------------------------------------------------

    void shadow_entering() { shadow_->entering = shadow_place(); }

    // Compiled code ran at least one block and returned to the dispatcher.
    void shadow_returned() {
        shadow_->entered = shadow_->entering;
        shadow_->compiled = true;
        shadow_sync(nullptr, "after compiled code returned");
    }

    // The run hands `event` to its caller. The end of the run is compared
    // once, however often its final event is asked for.
    NEURA_SHADOW_PATH void shadow_event(const Event& event) {
        const bool final = event.kind == Event::Kind::Completed || event.kind == Event::Kind::Trapped;
        if (final && shadow_->ended) return;
        shadow_sync(&event, std::string("at a ") + shadow_event_name(&event) + " event");
        if (final && shadow_) shadow_->ended = true;
    }

    NEURA_SHADOW_PATH void shadow_sync(const Event* event, const std::string& where) {
        auto& other = *shadow_->state;
        ++shadow_->syncs;
        if (shadow_->hooks) ++shadow_->hooks->syncs;
        // The shadow runs to the run's step count. At an event it then
        // takes one more step when it has not produced one: a scheduling
        // point or a limit trap takes no step.
        std::optional<Event> theirs;
        try {
            while (!theirs && other.steps_ < steps_ && !other.pending_) theirs = other.step(other.wires_);
            if (event && !theirs && other.steps_ == steps_ && !other.pending_) theirs = other.step(other.wires_);
        } catch (const std::exception& error) {
            return shadow_report(where, std::string("the shadow failed: ") + error.what());
        }
        if (auto* hooks = shadow_->hooks; hooks && hooks->perturb != TierShadowHooks::Perturb::None &&
                                          !hooks->perturbed && steps_ >= hooks->at_step) {
            shadow_perturb(*hooks);
        }
        const bool final = event && (event->kind == Event::Kind::Completed || event->kind == Event::Kind::Trapped);
        auto& shadow = *shadow_;
        const bool heap = shadow.heap_always || final ||
                          (++shadow.heap_syncs >= 64 &&
                           steps_ - std::min(steps_, shadow.heap_step) >= shadow_heap_spacing * shadow.heap_entries);
        if (heap) {
            shadow.heap_step = steps_;
            shadow.heap_syncs = 0;
        }
        const auto difference = shadow_compare(other, event, theirs ? &*theirs : nullptr, heap);
        if (!difference.empty()) return shadow_report(where, difference);
        shadow_->synced = shadow_place();
    }

    // Prints the first difference and aborts, or gives it to the test hooks
    // and goes on without the shadow.
    void shadow_report(const std::string& where, const std::string& difference) {
        const auto& shadow = *shadow_;
        std::string text = "neura: NEURA_TIER_CHECK: the shadow run differs at step " + std::to_string(steps_) + " " +
                           where + "; compiled code was ";
        text += shadow.compiled ? "last entered in " + shadow_place_text(shadow.entered) : std::string("not entered");
        text += ", the last sync point was in " + shadow_place_text(shadow.synced) + ", and the run is now in " +
                shadow_place_text(shadow_place()) + ": " + difference + "\n";
        auto* hooks = shadow.hooks;
        shadow_.reset();
        if (hooks && hooks->report) {
            hooks->report(text);
            return;
        }
        std::fputs(text.c_str(), stderr);
        std::abort();
    }

    // --- Inputs from outside -------------------------------------------------

    NEURA_SHADOW_PATH void shadow_complete(const HostCompletion& completion) {
        auto& other = *shadow_->state;
        if (!other.pending_) {
            return shadow_report("at a host completion", "a host request waiting: yes in the run, no in the shadow");
        }
        other.complete(completion);
    }

    NEURA_SHADOW_PATH void shadow_deliver(std::uint64_t sequence, const HostCompletion& completion) {
        try {
            shadow_->state->deliver(sequence, completion);
        } catch (const std::logic_error&) {
            shadow_report("at a delivery", "host request " + std::to_string(sequence) +
                                               " pending: yes in the run, no in the shadow");
        }
    }

    NEURA_SHADOW_PATH void shadow_advance_clock(std::int64_t now, std::size_t expired) {
        const auto theirs = shadow_->state->advance_clock(now);
        if (theirs != expired) {
            shadow_report("at the clock reading " + std::to_string(now),
                          "timers and deadlines expired: " + std::to_string(expired) + " in the run, " +
                              std::to_string(theirs) + " in the shadow");
        }
    }

    // A callback of a host service: the run makes it, recording its host
    // requests, their completions, and the callbacks made while they were
    // served; then, for a callback of the request the run waits for, the
    // shadow makes the same callback with the recorded answers.
    NEURA_SHADOW_PATH std::optional<HostValue> shadow_invoke_callback(std::string_view name, std::uint32_t version,
                                                    const std::vector<HostValue>& arguments,
                                                    const Execution::HostHandler& host) {
        auto call = std::make_unique<ShadowCall>();
        call->name = std::string(name);
        call->version = version;
        call->arguments = arguments;
        auto* recorded = call.get();
        const bool outermost = shadow_->serving.empty();
        if (!outermost) shadow_->serving.back()->calls.push_back(std::move(call));
        const Execution::HostHandler recording = [this, recorded, &host](const HostRequest& request) {
            recorded->requests.push_back(std::make_unique<ShadowRequest>());
            auto& made = *recorded->requests.back();
            made.request = request;
            if (shadow_) shadow_->serving.push_back(&made);
            struct Served {
                State& state;
                ~Served() {
                    if (state.shadow_ && !state.shadow_->serving.empty()) state.shadow_->serving.pop_back();
                }
            } served{*this};
            made.completion = host(request);
            return made.completion;
        };
        recorded->result = invoke_callback(name, version, arguments, recording);
        if (outermost && shadow_) {
            std::string difference;
            const auto result = shadow_replay(*shadow_->state, *call, difference);
            if (difference.empty() && result != call->result) {
                difference = "callback " + call->name + " result: " + shadow_result_text(call->result) +
                             " in the run, " + shadow_result_text(result) + " in the shadow";
            }
            if (!difference.empty()) {
                shadow_report("after a callback", difference);
            } else {
                shadow_sync(nullptr, "after a callback");
            }
        }
        return recorded->result;
    }

    // Makes a recorded callback in the shadow, answering its host requests
    // with the recorded completions after replaying the callbacks made while
    // each was served. Sets `difference` at the first request that differs.
    static std::optional<HostValue> shadow_replay(State& other, const ShadowCall& call, std::string& difference) {
        std::size_t next = 0;
        const auto label = "callback " + call.name + " host request ";
        auto result = other.invoke_callback(
            call.name, call.version, call.arguments, [&](const HostRequest& request) -> HostCompletion {
                if (!difference.empty()) return HostCompletion::failed();
                if (next >= call.requests.size()) {
                    difference = label + std::to_string(next) + ": none in the run, import " +
                                 std::to_string(request.import) + " in the shadow";
                    return HostCompletion::failed();
                }
                const auto& made = *call.requests[next];
                const auto position = std::to_string(next++);
                if (made.request.import != request.import) {
                    difference = label + position + " import: " + std::to_string(made.request.import) +
                                 " in the run, " + std::to_string(request.import) + " in the shadow";
                    return HostCompletion::failed();
                }
                if (auto text = shadow_arguments_differ(made.request.arguments, request.arguments); !text.empty()) {
                    difference = label + position + " " + text;
                    return HostCompletion::failed();
                }
                for (const auto& nested : made.calls) {
                    const auto nested_result = shadow_replay(other, *nested, difference);
                    if (!difference.empty()) break;
                    if (nested_result != nested->result) {
                        difference = "callback " + nested->name + " result: " + shadow_result_text(nested->result) +
                                     " in the run, " + shadow_result_text(nested_result) + " in the shadow";
                        break;
                    }
                }
                return made.completion;
            });
        if (difference.empty() && next != call.requests.size()) {
            difference = "callback " + call.name + " host requests: " + std::to_string(call.requests.size()) +
                         " in the run, " + std::to_string(next) + " in the shadow";
        }
        return result;
    }

    // --- Comparison ----------------------------------------------------------

    // The first difference found, as "item: A in the run, B in the shadow".
    // Item names are built only for a difference.
    struct ShadowDiff {
        std::string text;

        template <typename Item>
        bool differs(std::uint64_t mine, std::uint64_t theirs, const Item& item) {
            if (!text.empty()) return true;
            if (mine == theirs) return false;
            return note(item(), std::to_string(mine), std::to_string(theirs));
        }
        template <typename Item>
        bool differs(const std::string& mine, const std::string& theirs, const Item& item) {
            if (!text.empty()) return true;
            if (mine == theirs) return false;
            return note(item(), mine, theirs);
        }
        bool note(const std::string& item, const std::string& mine, const std::string& theirs) {
            text = item + ": " + mine + " in the run, " + theirs + " in the shadow";
            return true;
        }
    };

    static const char* shadow_event_name(const Event* event) {
        if (!event) return "none";
        switch (event->kind) {
        case Event::Kind::Completed: return "completion";
        case Event::Kind::Trapped: return "trap";
        case Event::Kind::HostRequest: return "host request";
        case Event::Kind::Poll: return "poll";
        case Event::Kind::Waiting: return "wait";
        case Event::Kind::Cancel: return "cancel";
        }
        return "unknown";
    }

    static std::string shadow_value_text(const HostValue& value, unsigned depth = 0) {
        std::string text = std::to_string(value.bits);
        if (!value.bytes.empty()) {
            text += " \"";
            for (std::size_t i = 0; i < value.bytes.size() && i < 64; ++i) {
                const auto c = static_cast<unsigned char>(value.bytes[i]);
                if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
                    text += static_cast<char>(c);
                } else {
                    constexpr std::string_view hex = "0123456789abcdef";
                    text += "\\x";
                    text += hex[c >> 4];
                    text += hex[c & 15];
                }
            }
            text += value.bytes.size() > 64 ? "...\"" : "\"";
        }
        if (!value.elements.empty()) {
            text += " [";
            for (std::size_t i = 0; i < value.elements.size() && i < 8; ++i) {
                text += (i ? ", " : "") + (depth < 3 ? shadow_value_text(value.elements[i], depth + 1) : "...");
            }
            text += value.elements.size() > 8 ? ", ...]" : "]";
        }
        return text;
    }

    static std::string shadow_result_text(const std::optional<HostValue>& value) {
        return value ? shadow_value_text(*value) : std::string("nothing");
    }

    // The first argument that differs, or nothing.
    static std::string shadow_arguments_differ(const std::vector<HostValue>& mine, const std::vector<HostValue>& theirs) {
        if (mine.size() != theirs.size()) {
            return "arguments: " + std::to_string(mine.size()) + " in the run, " + std::to_string(theirs.size()) +
                   " in the shadow";
        }
        for (std::size_t i = 0; i < mine.size(); ++i) {
            if (!(mine[i] == theirs[i])) {
                return "argument " + std::to_string(i) + ": " + shadow_value_text(mine[i]) + " in the run, " +
                       shadow_value_text(theirs[i]) + " in the shadow";
            }
        }
        return {};
    }

    std::string shadow_compare(const State& other, const Event* mine, const Event* theirs, bool heap) const {
        ShadowDiff d;
        if (d.differs(steps_, other.steps_, [&] {
                return theirs && !mine ? std::string("steps (the shadow stopped first at a ") +
                                             shadow_event_name(theirs) + " event)"
                                       : std::string("steps");
            })) {
            return d.text;
        }
        if (shadow_compare_outcome(d, other) || shadow_compare_event(d, mine, theirs)) return d.text;
        if (shadow_compare_frames(d, frames_, other.frames_, [] { return std::string("frame "); })) return d.text;
        for (std::size_t g = 0; g < globals_.size(); ++g) {
            const auto item = [&] { return "global " + std::to_string(g); };
            if (d.differs(global_initialized_[g], other.global_initialized_[g],
                          [&] { return item() + " initialized"; })) {
                return d.text;
            }
            if (global_initialized_[g] && d.differs(globals_[g], other.globals_[g], item)) return d.text;
        }
        if (d.differs(heap_.last_handle(), other.heap_.last_handle(), [] { return std::string("last handle"); }) ||
            d.differs(live_cells_, other.live_cells_, [] { return std::string("live heap cells"); }) ||
            d.differs(peak_cells_, other.peak_cells_, [] { return std::string("peak heap cells"); }) ||
            d.differs(next_collection_, other.next_collection_, [] { return std::string("collection threshold"); })) {
            return d.text;
        }
        if (limits_.profile) {
            const auto count = std::max(function_steps_.size(), other.function_steps_.size());
            for (std::size_t f = 0; f < count; ++f) {
                const auto a = f < function_steps_.size() ? function_steps_[f] : 0;
                const auto b = f < other.function_steps_.size() ? other.function_steps_[f] : 0;
                if (d.differs(a, b, [&] { return "steps of function " + std::to_string(f); })) return d.text;
            }
        }
        if (shadow_compare_scheduler(d, other)) return d.text;
        if (d.differs(sequence_, other.sequence_, [] { return std::string("host requests issued"); }) ||
            d.differs(heap_.last_identity(), other.heap_.last_identity(),
                      [] { return std::string("identity numbers given"); }) ||
            shadow_compare_pending(d, other) || shadow_compare_parallel(d, other)) {
            return d.text;
        }
        for (std::size_t c = 0; c < const_values_.size() && c < other.const_values_.size(); ++c) {
            if (d.differs(const_values_[c], other.const_values_[c], [&] { return "constant " + std::to_string(c); })) {
                return d.text;
            }
        }
        if (heap) shadow_compare_heap(d, other);
        return d.text;
    }

    // Where the response to the request the run waits for goes.
    bool shadow_compare_pending(ShadowDiff& d, const State& other) const {
        if (!pending_ || !other.pending_) return false;
        const auto& a = *pending_;
        const auto& b = *other.pending_;
        return d.differs(a.import, b.import, [] { return std::string("waiting request import"); }) ||
               d.differs(a.success_slot, b.success_slot, [] { return std::string("waiting request success slot"); }) ||
               d.differs(a.success_block, b.success_block, [] { return std::string("waiting request success block"); }) ||
               d.differs(a.error_slot, b.error_slot, [] { return std::string("waiting request error slot"); }) ||
               d.differs(a.error_block, b.error_block, [] { return std::string("waiting request error block"); });
    }

    // The parallel items started and not yet finished, in order: what each
    // runs, on what, and within what limits. Their arguments are compared
    // by shape; each item's own run is checked by its own shadow.
    bool shadow_compare_parallel(ShadowDiff& d, const State& other) const {
        const auto count = [](const State& state) { return state.parallel_ ? state.parallel_->items.size() : 0; };
        if (d.differs(count(*this), count(other), [] { return std::string("parallel items outstanding"); })) return true;
        if (!parallel_ || !other.parallel_) return false;
        const auto& a = *parallel_;
        const auto& b = *other.parallel_;
        if (d.differs(a.next_id, b.next_id, [] { return std::string("next parallel item"); }) ||
            d.differs(a.batch, b.batch, [] { return std::string("parallel items in the map"); })) {
            return true;
        }
        for (std::size_t i = 0; i < a.items.size(); ++i) {
            const auto& x = *a.items[i];
            const auto& y = *b.items[i];
            const auto item = [&] { return "parallel item " + std::to_string(x.id); };
            if (d.differs(x.id, y.id, [&] { return item() + " identity"; }) ||
                d.differs(x.index, y.index, [&] { return item() + " index"; }) ||
                d.differs(x.function, y.function, [&] { return item() + " function"; }) ||
                d.differs(x.max_steps, y.max_steps, [&] { return item() + " step limit"; }) ||
                d.differs(x.max_cells, y.max_cells, [&] { return item() + " heap limit"; }) ||
                d.differs(x.charged_cells, y.charged_cells, [&] { return item() + " cells charged"; }) ||
                d.differs(x.argument.nodes.size(), y.argument.nodes.size(), [&] { return item() + " argument objects"; }) ||
                d.differs(x.argument.root, y.argument.root, [&] { return item() + " argument"; })) {
                return true;
            }
        }
        return false;
    }

    static std::string shadow_outcome_text(const State& state) {
        if (!state.finished_) return state.pending_ ? "waiting for a host request" : "running";
        return state.finished_->kind == Event::Kind::Trapped ? "trapped" : "finished";
    }

    bool shadow_compare_outcome(ShadowDiff& d, const State& other) const {
        if (d.differs(shadow_outcome_text(*this), shadow_outcome_text(other), [] { return std::string("outcome"); })) {
            return true;
        }
        if (!finished_ || finished_->kind != Event::Kind::Trapped) return false;
        const auto& a = finished_->trap;
        const auto& b = other.finished_->trap;
        if (d.differs(std::string(describe(a.kind)), std::string(describe(b.kind)), [] { return std::string("trap kind"); }) ||
            d.differs(a.reason, b.reason, [] { return std::string("trap reason"); }) ||
            d.differs("\"" + a.detail + "\"", "\"" + b.detail + "\"", [] { return std::string("trap detail"); }) ||
            d.differs(a.function, b.function, [] { return std::string("trap function"); }) ||
            d.differs(a.block, b.block, [] { return std::string("trap block"); }) ||
            d.differs(a.callers.size(), b.callers.size(), [] { return std::string("trap callers"); })) {
            return true;
        }
        for (std::size_t i = 0; i < a.callers.size(); ++i) {
            if (d.differs(a.callers[i].first, b.callers[i].first,
                          [&] { return "trap caller " + std::to_string(i) + " function"; }) ||
                d.differs(a.callers[i].second, b.callers[i].second,
                          [&] { return "trap caller " + std::to_string(i) + " block"; })) {
                return true;
            }
        }
        return false;
    }

    static bool shadow_compare_event(ShadowDiff& d, const Event* mine, const Event* theirs) {
        if (d.differs(std::string(shadow_event_name(mine)), std::string(shadow_event_name(theirs)),
                      [] { return std::string("event"); })) {
            return true;
        }
        if (!mine) return false;
        const auto& a = *mine;
        const auto& b = *theirs;
        switch (a.kind) {
        case Event::Kind::HostRequest:
            if (d.differs(a.request.import, b.request.import, [] { return std::string("host request import"); }) ||
                d.differs(a.request.sequence, b.request.sequence, [] { return std::string("host request sequence"); }) ||
                d.differs(a.overlapping, b.overlapping, [] { return std::string("host request overlapping"); }) ||
                d.differs(a.task, b.task, [] { return std::string("host request task"); }) ||
                d.differs(a.tick, b.tick, [] { return std::string("host request tick"); })) {
                return true;
            }
            if (auto text = shadow_arguments_differ(a.request.arguments, b.request.arguments); !text.empty()) {
                d.text = "host request " + text;
                return true;
            }
            return false;
        case Event::Kind::Poll:
        case Event::Kind::Waiting:
            return d.differs(a.tick, b.tick, [] { return std::string("event tick"); }) ||
                   d.differs(a.timer, b.timer, [] { return std::string("event timer"); }) ||
                   d.differs(static_cast<std::uint64_t>(a.deadline), static_cast<std::uint64_t>(b.deadline),
                             [] { return std::string("event deadline"); });
        case Event::Kind::Cancel:
            return d.differs(a.sequence, b.sequence, [] { return std::string("cancelled request"); }) ||
                   d.differs(a.tick, b.tick, [] { return std::string("event tick"); });
        case Event::Kind::Completed:
            return d.differs(static_cast<std::uint32_t>(a.status), static_cast<std::uint32_t>(b.status),
                             [] { return std::string("status"); }) ||
                   d.differs(a.result, b.result, [] { return std::string("result"); });
        case Event::Kind::Trapped:
            return false;
        }
        return false;
    }

    // Every frame from the bottom: its function, block, return slot and
    // block, and each slot's initialized flag and, when initialized, value.
    // An uninitialized slot's bits cannot be read, so they are not compared.
    // FrameHeader::called_from is tier machinery and not compared.
    template <typename Prefix>
    bool shadow_compare_frames(ShadowDiff& d, const FrameStack& mine, const FrameStack& theirs,
                               const Prefix& prefix) const {
        if (d.differs(mine.size(), theirs.size(), [&] { return prefix() + "count"; })) return true;
        for (std::size_t i = 0; i < mine.size(); ++i) {
            const auto& a = mine.header(i);
            const auto& b = theirs.header(i);
            const auto item = [&] { return prefix() + std::to_string(i); };
            if (d.differs(a.function, b.function, [&] { return item() + " function"; }) ||
                d.differs(a.block, b.block, [&] { return item() + " block"; }) ||
                d.differs(a.return_slot, b.return_slot, [&] { return item() + " return slot"; }) ||
                d.differs(a.return_block, b.return_block, [&] { return item() + " return block"; }) ||
                d.differs(a.boundary, b.boundary, [&] { return item() + " callback boundary"; })) {
                return true;
            }
            const auto slots = image_.functions[a.function].frame_types.size();
            const auto* values = mine.slots(i);
            const auto* flags = mine.initialized(i);
            const auto* other_values = theirs.slots(i);
            const auto* other_flags = theirs.initialized(i);
            for (std::size_t s = 0; s < slots; ++s) {
                if (flags[s] == other_flags[s] && (!flags[s] || values[s] == other_values[s])) continue;
                const auto slot = [&] { return item() + " slot " + std::to_string(s); };
                if (d.differs(flags[s], other_flags[s], [&] { return slot() + " initialized"; }) ||
                    d.differs(values[s], other_values[s], slot)) {
                    return true;
                }
            }
        }
        return false;
    }

    static const char* shadow_task_state(TaskState state) {
        switch (state) {
        case TaskState::Running: return "running";
        case TaskState::Runnable: return "runnable";
        case TaskState::Joining: return "joining";
        case TaskState::LeavingScope: return "leaving a scope";
        case TaskState::Finished: return "finished";
        case TaskState::Sending: return "sending";
        case TaskState::Receiving: return "receiving";
        case TaskState::TimerWait: return "waiting for a timer";
        case TaskState::Selecting: return "selecting";
        case TaskState::HostWait: return "waiting for a host request";
        case TaskState::GuardWait: return "waiting for a guard";
        }
        return "unknown";
    }

    // The keys of a map in ascending order. The vector is filled by index:
    // more calls of std::vector<std::uint64_t>::push_back in this file would
    // keep the compiler from inlining it in the region interpreter.
    template <typename Map>
    static std::vector<std::uint64_t> shadow_sorted_keys(const Map& map) {
        std::vector<std::uint64_t> keys(map.size());
        std::size_t next = 0;
        for (const auto& [key, value] : map) keys[next++] = key;
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    // A queue of parked operations, in order: each waiter's task and arm.
    template <typename Item>
    static bool shadow_compare_waiters(ShadowDiff& d, const std::deque<Waiter>& a, const std::deque<Waiter>& b,
                                       const Item& item) {
        if (d.differs(a.size(), b.size(), [&] { return item() + " count"; })) return true;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (d.differs(a[i].task, b[i].task, [&] { return item() + " " + std::to_string(i) + " task"; }) ||
                d.differs(a[i].arm, b[i].arm, [&] { return item() + " " + std::to_string(i) + " arm"; })) {
                return true;
            }
        }
        return false;
    }

    // Every field of a task, in declaration order. The running task's frames
    // are the machine's, compared before the scheduler.
    bool shadow_compare_task(ShadowDiff& d, std::uint64_t id, const Task& task, const Task& peer, bool running) const {
        const auto item = [&] { return "task " + std::to_string(id); };
        if (d.differs(task.parent, peer.parent, [&] { return item() + " parent"; }) ||
            d.differs(task.parent_scope, peer.parent_scope, [&] { return item() + " parent scope"; }) ||
            d.differs(task.context, peer.context, [&] { return item() + " context"; }) ||
            d.differs(task.unjoinable, peer.unjoinable, [&] { return item() + " unjoinable"; }) ||
            d.differs(task.spawn_function, peer.spawn_function, [&] { return item() + " spawn function"; }) ||
            d.differs(task.spawn_block, peer.spawn_block, [&] { return item() + " spawn block"; }) ||
            d.differs(task.result_type, peer.result_type, [&] { return item() + " result type"; }) ||
            d.differs(task.result, peer.result, [&] { return item() + " result"; }) ||
            d.differs(std::string(shadow_task_state(task.state)), std::string(shadow_task_state(peer.state)),
                      [&] { return item() + " state"; })) {
            return true;
        }
        if (!running && shadow_compare_frames(d, task.frames, peer.frames, [&] { return item() + " frame "; })) {
            return true;
        }
        if (d.differs(task.scopes.size(), peer.scopes.size(), [&] { return item() + " open scopes"; })) return true;
        for (std::size_t s = 0; s < task.scopes.size(); ++s) {
            const auto& x = task.scopes[s];
            const auto& y = peer.scopes[s];
            const auto scope = [&] { return item() + " scope " + std::to_string(s); };
            if (d.differs(x.id, y.id, [&] { return scope() + " identity"; }) ||
                d.differs(x.frame_depth, y.frame_depth, [&] { return scope() + " frame depth"; }) ||
                d.differs(x.live_children, y.live_children, [&] { return scope() + " live children"; }) ||
                d.differs(x.children.size(), y.children.size(), [&] { return scope() + " children"; })) {
                return true;
            }
            for (std::size_t c = 0; c < x.children.size(); ++c) {
                if (d.differs(x.children[c], y.children[c], [&] { return scope() + " child " + std::to_string(c); })) {
                    return true;
                }
            }
        }
        if (d.differs(task.waiting_for, peer.waiting_for, [&] { return item() + " waits for"; }) ||
            d.differs(task.join_slot, peer.join_slot, [&] { return item() + " result slot"; }) ||
            d.differs(task.close_on_wake, peer.close_on_wake, [&] { return item() + " close on wake"; }) ||
            d.differs(task.wait_value, peer.wait_value, [&] { return item() + " value in flight"; }) ||
            d.differs(task.wait_value_type, peer.wait_value_type, [&] { return item() + " type in flight"; }) ||
            d.differs(task.select.size(), peer.select.size(), [&] { return item() + " select arms"; })) {
            return true;
        }
        for (std::size_t s = 0; s < task.select.size(); ++s) {
            const auto& x = task.select[s];
            const auto& y = peer.select[s];
            const auto arm = [&] { return item() + " select arm " + std::to_string(s); };
            if (d.differs(static_cast<std::uint64_t>(x.kind), static_cast<std::uint64_t>(y.kind),
                          [&] { return arm() + " kind"; }) ||
                d.differs(x.handle, y.handle, [&] { return arm() + " object"; }) ||
                d.differs(x.value, y.value, [&] { return arm() + " value"; }) ||
                d.differs(x.value_type, y.value_type, [&] { return arm() + " value type"; })) {
                return true;
            }
        }
        return d.differs(task.selects, peer.selects, [&] { return item() + " selects"; }) ||
               d.differs(task.stashed, peer.stashed, [&] { return item() + " stashed"; }) ||
               d.differs(task.stash, peer.stash, [&] { return item() + " stash"; }) ||
               d.differs(task.stash_type, peer.stash_type, [&] { return item() + " stash type"; }) ||
               d.differs(task.pending.import, peer.pending.import, [&] { return item() + " pending import"; }) ||
               d.differs(task.pending.success_slot, peer.pending.success_slot,
                         [&] { return item() + " pending success slot"; }) ||
               d.differs(task.pending.success_block, peer.pending.success_block,
                         [&] { return item() + " pending success block"; }) ||
               d.differs(task.pending.error_slot, peer.pending.error_slot,
                         [&] { return item() + " pending error slot"; }) ||
               d.differs(task.pending.error_block, peer.pending.error_block,
                         [&] { return item() + " pending error block"; }) ||
               d.differs(task.cancellable, peer.cancellable, [&] { return item() + " cancellable"; }) ||
               d.differs(task.cancel_sent, peer.cancel_sent, [&] { return item() + " cancel sent"; }) ||
               d.differs(task.started, peer.started, [&] { return item() + " started"; }) ||
               d.differs(task.steps, peer.steps, [&] { return item() + " steps"; }) ||
               d.differs(task.since_tick, peer.since_tick, [&] { return item() + " state since tick"; }) ||
               d.differs(task.runnable_ticks, peer.runnable_ticks, [&] { return item() + " runnable ticks"; }) ||
               d.differs(task.channel_ticks, peer.channel_ticks, [&] { return item() + " channel ticks"; }) ||
               d.differs(task.host_ticks, peer.host_ticks, [&] { return item() + " host ticks"; });
    }

    // The scheduler: every field of Scheduler, in declaration order, with
    // every task, scope record, channel, timer, guard, deadline, pending
    // request, cancellation, and spawn-site total.
    bool shadow_compare_scheduler(ShadowDiff& d, const State& other) const {
        if (d.differs(scheduler_ != nullptr, other.scheduler_ != nullptr, [] { return std::string("scheduler"); })) {
            return true;
        }
        if (!scheduler_) return false;
        const auto& a = *scheduler_;
        const auto& b = *other.scheduler_;
        if (d.differs(a.tasks.size(), b.tasks.size(), [] { return std::string("tasks"); })) return true;
        for (auto x = a.tasks.begin(), y = b.tasks.begin(); x != a.tasks.end(); ++x, ++y) {
            if (d.differs(x->first, y->first, [] { return std::string("task identity"); })) return true;
            const bool running = x->second.state == TaskState::Running && x->first == a.current;
            if (shadow_compare_task(d, x->first, x->second, y->second, running)) return true;
        }
        if (d.differs(a.runnable.size(), b.runnable.size(), [] { return std::string("runnable tasks"); })) return true;
        for (std::size_t i = 0; i < a.runnable.size(); ++i) {
            if (d.differs(a.runnable[i], b.runnable[i], [&] { return "runnable task " + std::to_string(i); })) return true;
        }
        if (d.differs(a.current, b.current, [] { return std::string("current task"); }) ||
            d.differs(a.next_id, b.next_id, [] { return std::string("next task identity"); }) ||
            d.differs(a.live, b.live, [] { return std::string("live tasks"); }) ||
            d.differs(a.peak_live, b.peak_live, [] { return std::string("peak live tasks"); }) ||
            d.differs(a.polls, b.polls, [] { return std::string("polls"); }) ||
            d.differs(a.ticks, b.ticks, [] { return std::string("tick"); }) ||
            d.differs(a.deciding, b.deciding, [] { return std::string("between tasks"); }) ||
            d.differs(a.polled, b.polled, [] { return std::string("poll offered"); }) ||
            d.differs(a.next_scope, b.next_scope, [] { return std::string("next scope identity"); }) ||
            d.differs(a.scopes.size(), b.scopes.size(), [] { return std::string("open scopes"); })) {
            return true;
        }
        for (auto x = a.scopes.begin(), y = b.scopes.begin(); x != a.scopes.end(); ++x, ++y) {
            const auto item = [&] { return "scope " + std::to_string(x->first); };
            if (d.differs(x->first, y->first, [] { return std::string("scope identity"); }) ||
                d.differs(x->second.owner, y->second.owner, [&] { return item() + " owner"; }) ||
                d.differs(x->second.parent, y->second.parent, [&] { return item() + " parent"; }) ||
                d.differs(x->second.cancelled, y->second.cancelled, [&] { return item() + " cancelled"; }) ||
                d.differs(x->second.deadline_passed, y->second.deadline_passed,
                          [&] { return item() + " deadline passed"; }) ||
                d.differs(x->second.cancelled_tick, y->second.cancelled_tick,
                          [&] { return item() + " cancelled at tick"; }) ||
                d.differs(static_cast<std::uint64_t>(x->second.deadline), static_cast<std::uint64_t>(y->second.deadline),
                          [&] { return item() + " deadline"; })) {
                return true;
            }
        }
        const auto channels = shadow_sorted_keys(a.channels);
        const auto other_channels = shadow_sorted_keys(b.channels);
        if (d.differs(channels.size(), other_channels.size(), [] { return std::string("channels"); })) return true;
        for (std::size_t i = 0; i < channels.size(); ++i) {
            const auto handle = channels[i];
            if (d.differs(handle, other_channels[i], [&] { return "channel object " + std::to_string(i); })) return true;
            const auto& x = a.channels.at(handle);
            const auto& y = b.channels.at(handle);
            const auto item = [&] { return "channel " + std::to_string(x.number) + " (handle " + std::to_string(handle) + ")"; };
            if (d.differs(x.number, y.number, [&] { return item() + " number"; }) ||
                d.differs(x.type, y.type, [&] { return item() + " type"; }) ||
                d.differs(x.capacity, y.capacity, [&] { return item() + " capacity"; }) ||
                d.differs(x.closed, y.closed, [&] { return item() + " closed"; }) ||
                d.differs(x.buffer.size(), y.buffer.size(), [&] { return item() + " buffered values"; })) {
                return true;
            }
            for (std::size_t k = 0; k < x.buffer.size(); ++k) {
                if (d.differs(x.buffer[k], y.buffer[k], [&] { return item() + " buffer value " + std::to_string(k); })) {
                    return true;
                }
            }
            if (shadow_compare_waiters(d, x.senders, y.senders, [&] { return item() + " sender"; }) ||
                shadow_compare_waiters(d, x.receivers, y.receivers, [&] { return item() + " receiver"; })) {
                return true;
            }
        }
        const auto timers = shadow_sorted_keys(a.timers);
        const auto other_timers = shadow_sorted_keys(b.timers);
        if (d.differs(timers.size(), other_timers.size(), [] { return std::string("timers"); })) return true;
        for (std::size_t i = 0; i < timers.size(); ++i) {
            const auto handle = timers[i];
            if (d.differs(handle, other_timers[i], [&] { return "timer object " + std::to_string(i); })) return true;
            const auto& x = a.timers.at(handle);
            const auto& y = b.timers.at(handle);
            const auto item = [&] { return "timer " + std::to_string(x.number) + " (handle " + std::to_string(handle) + ")"; };
            if (d.differs(x.number, y.number, [&] { return item() + " number"; }) ||
                d.differs(static_cast<std::uint64_t>(x.deadline), static_cast<std::uint64_t>(y.deadline),
                          [&] { return item() + " deadline"; }) ||
                d.differs(x.fired, y.fired, [&] { return item() + " fired"; }) ||
                d.differs(x.armed, y.armed, [&] { return item() + " armed"; }) ||
                shadow_compare_waiters(d, x.waiters, y.waiters, [&] { return item() + " waiter"; })) {
                return true;
            }
        }
        const auto guards = shadow_sorted_keys(a.guards);
        const auto other_guards = shadow_sorted_keys(b.guards);
        if (d.differs(guards.size(), other_guards.size(), [] { return std::string("guards"); })) return true;
        for (std::size_t i = 0; i < guards.size(); ++i) {
            const auto handle = guards[i];
            if (d.differs(handle, other_guards[i], [&] { return "guard object " + std::to_string(i); })) return true;
            const auto& x = a.guards.at(handle);
            const auto& y = b.guards.at(handle);
            const auto item = [&] { return "guard " + std::to_string(x.number) + " (handle " + std::to_string(handle) + ")"; };
            if (d.differs(x.number, y.number, [&] { return item() + " number"; }) ||
                d.differs(x.value, y.value, [&] { return item() + " value"; }) ||
                d.differs(x.type, y.type, [&] { return item() + " type"; }) ||
                d.differs(x.held, y.held, [&] { return item() + " held"; }) ||
                d.differs(x.holder, y.holder, [&] { return item() + " holder"; }) ||
                shadow_compare_waiters(d, x.waiters, y.waiters, [&] { return item() + " waiter"; })) {
                return true;
            }
        }
        if (d.differs(a.next_channel, b.next_channel, [] { return std::string("next channel number"); }) ||
            d.differs(a.next_timer, b.next_timer, [] { return std::string("next timer number"); }) ||
            d.differs(a.next_guard, b.next_guard, [] { return std::string("next guard number"); }) ||
            d.differs(a.queued, b.queued, [] { return std::string("queued messages"); }) ||
            d.differs(a.peak_queued, b.peak_queued, [] { return std::string("peak queued messages"); }) ||
            d.differs(a.requests.size(), b.requests.size(), [] { return std::string("pending host requests"); })) {
            return true;
        }
        for (auto x = a.requests.begin(), y = b.requests.begin(); x != a.requests.end(); ++x, ++y) {
            if (d.differs(x->first, y->first, [] { return std::string("pending host request"); }) ||
                d.differs(x->second, y->second, [&] { return "task of pending host request " + std::to_string(x->first); })) {
                return true;
            }
        }
        if (d.differs(a.peak_requests, b.peak_requests, [] { return std::string("peak pending host requests"); }) ||
            d.differs(a.deadlines.size(), b.deadlines.size(), [] { return std::string("armed deadlines"); })) {
            return true;
        }
        std::size_t armed = 0;
        for (auto x = a.deadlines.begin(), y = b.deadlines.begin(); x != a.deadlines.end(); ++x, ++y, ++armed) {
            const auto item = [&] { return "armed deadline " + std::to_string(armed); };
            if (d.differs(static_cast<std::uint64_t>(x->first), static_cast<std::uint64_t>(y->first), item) ||
                d.differs(x->second.scope, y->second.scope, [&] { return item() + " of a scope"; }) ||
                d.differs(x->second.id, y->second.id, [&] { return item() + " owner"; })) {
                return true;
            }
        }
        if (d.differs(a.cancellations.size(), b.cancellations.size(), [] { return std::string("cancellations"); })) {
            return true;
        }
        for (std::size_t i = 0; i < a.cancellations.size(); ++i) {
            if (d.differs(a.cancellations[i], b.cancellations[i], [&] { return "cancellation " + std::to_string(i); })) {
                return true;
            }
        }
        if (d.differs(a.sites.size(), b.sites.size(), [] { return std::string("spawn sites of released tasks"); })) {
            return true;
        }
        for (std::size_t i = 0; i < a.sites.size(); ++i) {
            const auto& x = a.sites[i];
            const auto& y = b.sites[i];
            const auto item = [&] { return "spawn site " + std::to_string(i); };
            if (d.differs(x.function, y.function, [&] { return item() + " function"; }) ||
                d.differs(x.block, y.block, [&] { return item() + " block"; }) ||
                d.differs(x.tasks, y.tasks, [&] { return item() + " tasks"; }) ||
                d.differs(x.steps, y.steps, [&] { return item() + " steps"; }) ||
                d.differs(x.runnable_ticks, y.runnable_ticks, [&] { return item() + " runnable ticks"; }) ||
                d.differs(x.channel_ticks, y.channel_ticks, [&] { return item() + " channel ticks"; }) ||
                d.differs(x.host_ticks, y.host_ticks, [&] { return item() + " host ticks"; })) {
                return true;
            }
        }
        if (d.differs(a.site_index.size(), b.site_index.size(), [] { return std::string("spawn site index"); })) {
            return true;
        }
        for (auto x = a.site_index.begin(), y = b.site_index.begin(); x != a.site_index.end(); ++x, ++y) {
            if (d.differs(x->first.first, y->first.first, [] { return std::string("spawn site index function"); }) ||
                d.differs(x->first.second, y->first.second, [] { return std::string("spawn site index block"); }) ||
                d.differs(x->second, y->second, [] { return std::string("spawn site index position"); })) {
                return true;
            }
        }
        return false;
    }

    // --- The heap ------------------------------------------------------------
    //
    // The oracle reads and changes the heap only through the functions of
    // this section, so that a changed heap representation needs changes
    // here only.

    // An entry's cells: an object's fields or an array's or list's
    // elements; none for text and bytes. Its initialized flags: one per
    // field of an object; none for a collection, whose cells are always
    // initialized. Its bytes: those of text or bytes; none otherwise.
    std::size_t shadow_cell_count(Heap::Ref item) const {
        switch (heap_.kind(item)) {
        case EntryKind::Object: return heap_.field_count(item);
        case EntryKind::Array:
        case EntryKind::List: return heap_.length(item);
        default: return 0;
        }
    }
    std::size_t shadow_flag_count(Heap::Ref item) const {
        return heap_.kind(item) == EntryKind::Object ? heap_.field_count(item) : 0;
    }
    std::uint64_t shadow_cell(Heap::Ref item, std::size_t cell) const {
        return heap_.kind(item) == EntryKind::Object ? heap_.field(item, static_cast<std::uint32_t>(cell))
                                                      : heap_.element(item, cell);
    }
    std::string_view shadow_bytes(Heap::Ref item) const {
        const auto kind = heap_.kind(item);
        return kind == EntryKind::Text || kind == EntryKind::Bytes ? heap_.text(item) : std::string_view();
    }

    void shadow_heap_flip_cell(std::uint64_t handle, std::size_t cell) {
        const auto item = heap_.find(handle);
        const auto field = static_cast<std::uint32_t>(cell);
        heap_.restore_field(item, field, heap_.field(item, field) ^ 1U, heap_.initialized(item, field));
    }
    void shadow_heap_clear_flag(std::uint64_t handle, std::size_t cell) {
        const auto item = heap_.find(handle);
        const auto field = static_cast<std::uint32_t>(cell);
        heap_.restore_field(item, field, heap_.field(item, field), false);
    }
    void shadow_heap_next_version(std::uint64_t handle) { heap_.advance_version(heap_.find(handle)); }

    static const char* shadow_kind_name(EntryKind kind) {
        switch (kind) {
        case EntryKind::Object: return "object";
        case EntryKind::Array: return "array";
        case EntryKind::List: return "list";
        case EntryKind::Text: return "text";
        case EntryKind::Bytes: return "bytes";
        }
        return "unknown";
    }

    // Entry by entry in handle order: presence, kind, type, cells and their
    // initialized flags (an uninitialized cell's bits are not compared),
    // bytes, version, and identity number.
    // Both heaps are walked in handle order in step, without copying; an
    // entry that Heap::same finds equal is passed over at once.
    bool shadow_compare_heap(ShadowDiff& d, const State& other) const {
        std::size_t entries = 0;
        auto x = heap_.next_live(0);
        auto y = other.heap_.next_live(0);
        while (x != 0 || y != 0) {
            if (y == 0 || (x != 0 && x < y)) {
                return d.note("heap handle " + std::to_string(x), "present", "absent");
            }
            if (x == 0 || y < x) {
                return d.note("heap handle " + std::to_string(y), "absent", "present");
            }
            ++entries;
            const auto a = heap_.find(x);
            const auto b = other.heap_.find(y);
            const auto handle = x;
            x = heap_.next_live(x);
            y = other.heap_.next_live(y);
            if (Heap::same(heap_, a, other.heap_, b)) continue;
            const auto item = [&] { return "heap handle " + std::to_string(handle); };
            const auto a_kind = heap_.kind(a);
            const auto b_kind = other.heap_.kind(b);
            const auto cells = shadow_cell_count(a);
            const auto flags = shadow_flag_count(a);
            if ((a_kind != b_kind && d.note(item() + " kind", shadow_kind_name(a_kind), shadow_kind_name(b_kind))) ||
                d.differs(heap_.type(a), other.heap_.type(b), [&] { return item() + " type"; }) ||
                d.differs(cells, other.shadow_cell_count(b), [&] { return item() + " cells"; }) ||
                d.differs(flags, other.shadow_flag_count(b), [&] { return item() + " initialized flags"; })) {
                return true;
            }
            for (std::size_t c = 0; c < cells; ++c) {
                const bool flagged = c < flags;
                const bool a_flag = flagged && heap_.initialized(a, static_cast<std::uint32_t>(c));
                const bool b_flag = flagged && other.heap_.initialized(b, static_cast<std::uint32_t>(c));
                const bool set = !flagged || a_flag;
                const auto a_cell = shadow_cell(a, c);
                const auto b_cell = other.shadow_cell(b, c);
                if ((!flagged || a_flag == b_flag) && (!set || a_cell == b_cell)) continue;
                const auto cell = [&] { return item() + " cell " + std::to_string(c); };
                if ((flagged && d.differs(a_flag, b_flag, [&] { return cell() + " initialized"; })) ||
                    d.differs(a_cell, b_cell, cell)) {
                    return true;
                }
            }
            const auto a_bytes = shadow_bytes(a);
            const auto b_bytes = other.shadow_bytes(b);
            if (a_bytes != b_bytes) {
                std::size_t at = 0;
                while (at < a_bytes.size() && at < b_bytes.size() && a_bytes[at] == b_bytes[at]) ++at;
                return d.note(item() + " bytes from byte " + std::to_string(at),
                              std::to_string(a_bytes.size()) + " bytes", std::to_string(b_bytes.size()) + " bytes");
            }
            if (d.differs(heap_.version(a), other.heap_.version(b), [&] { return item() + " version"; }) ||
                d.differs(heap_.identity(a), other.heap_.identity(b), [&] { return item() + " identity"; })) {
                return true;
            }
        }
        shadow_->heap_entries = entries;
        return false;
    }

    // --- Perturbation for tests (TierShadowHooks) ----------------------------

    void shadow_perturb(TierShadowHooks& hooks) {
        using Perturb = TierShadowHooks::Perturb;
        const auto scalar = [&](std::uint32_t type_index) { return !nvr::is_reference(type(type_index).kind); };
        bool done = false;
        switch (hooks.perturb) {
        case Perturb::None:
            return;
        case Perturb::SlotValue:
        case Perturb::SlotInitialized: {
            if (frames_.empty()) break;
            auto frame = frames_.back();
            const auto& types = image_.functions[frame.function].frame_types;
            for (std::size_t s = 0; s < types.size() && !done; ++s) {
                if (!frame.initialized[s]) continue;
                if (hooks.perturb == Perturb::SlotInitialized) {
                    frame.initialized[s] = 0;
                    done = true;
                } else if (scalar(types[s])) {
                    frame.slots[s] ^= 1U;
                    done = true;
                }
            }
            break;
        }
        case Perturb::FrameBlock: {
            if (frames_.empty()) break;
            auto frame = frames_.back();
            const auto blocks = image_.functions[frame.function].blocks.size();
            if (blocks < 2) break;
            frame.block = static_cast<std::uint32_t>((frame.block + 1) % blocks);
            done = true;
            break;
        }
        case Perturb::Global:
            for (std::size_t g = 0; g < globals_.size() && !done; ++g) {
                if (global_initialized_[g] && scalar(image_.globals[g].type)) {
                    globals_[g] ^= 1U;
                    done = true;
                }
            }
            break;
        case Perturb::HeapField:
        case Perturb::HeapInitialized:
        case Perturb::ListVersion:
            for (auto handle = heap_.next_live(0); handle != 0; handle = heap_.next_live(handle)) {
                const auto entry = heap_.find(handle);
                if (hooks.perturb == Perturb::ListVersion) {
                    if (heap_.kind(entry) != EntryKind::List) continue;
                    shadow_heap_next_version(handle);
                    done = true;
                    break;
                }
                if (heap_.kind(entry) != EntryKind::Object) continue;
                const auto& fields = image_.layouts[heap_.type(entry)].fields;
                for (std::size_t f = 0; f < shadow_flag_count(entry) && !done; ++f) {
                    if (!heap_.initialized(entry, static_cast<std::uint32_t>(f))) continue;
                    if (hooks.perturb == Perturb::HeapInitialized) {
                        shadow_heap_clear_flag(handle, f);
                        done = true;
                    } else if (scalar(fields[f])) {
                        shadow_heap_flip_cell(handle, f);
                        done = true;
                    }
                }
                if (done) break;
            }
            break;
        case Perturb::LiveCells:
            ++live_cells_;
            done = true;
            break;
        case Perturb::Steps:
            ++steps_;
            done = true;
            break;
        case Perturb::TaskSelects:
            if (!scheduler_ || scheduler_->tasks.empty()) break;
            ++scheduler_->tasks.begin()->second.selects;
            done = true;
            break;
        case Perturb::ChannelBuffer:
            if (!scheduler_) break;
            for (const auto handle : shadow_sorted_keys(scheduler_->channels)) {
                auto& buffer = scheduler_->channels.at(handle).buffer;
                if (buffer.empty()) continue;
                buffer.front() ^= 1U;
                done = true;
                break;
            }
            break;
        }
        hooks.perturbed = done;
    }

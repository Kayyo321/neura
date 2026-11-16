// Inlining for tier 3 (docs: wo3 design 5.8). A call is replaced by the IR of
// the callee's function program, built in the caller's SSA:
//
// - The callee's parameters are the call's arguments; its other slots start as
//   zero and uninitialized, as FrameStack::push gives them. Its frame states
//   get a level of their own whose caller is the call's state (the caller's
//   frame after the call block's commit, at the call block), with the call's
//   result slot and resume block, so that a deoptimization inside the callee
//   materializes the frame push_frame would have built.
// - The callee's Return adds its block to C and jumps to a join block whose
//   phis take the place of the Call (the result) and of the counter reload
//   after it; the caller goes on at the call's resume block.
// - The Budget at the callee's entry block disappears unless a loop comes
//   back to it. The Budget at the caller's resume block disappears as well
//   unless a loop comes back to it or a call that is not inlined returns
//   there: an entry of the program at that block checks it on its own edge,
//   with the state the block's entry had on that edge. The Budgets before
//   them cover the inlined blocks once the lengths are recomputed, and C
//   counts every inlined block as the reference does.
//
// What is inlined (the limits of design 5.8, measured in program ops and
// image blocks): the callee has a published function program whose entry 0
// is at block 0 and reads only parameters, is not on the inline chain, sits at
// most 4 levels deep, has at most 64 blocks and 400 ops after its own
// inlining, and keeps the native program within 8 times the root's ops and
// 20000. After its own inlining it has no Helper, no Call, no Leave and no
// write-back: inlined code writes a frame only on the slow path of an
// allocation or append (AllocObject, Append), which materializes every frame
// of its state, runs the helper, and leaves at the helper's resume block. Its arguments and return
// values fit the types of the slots they go to. Host, scheduler and message
// exits are deoptimizations in function programs already.
//
// Dispatch and closure sites: the exits the recorded tags select (at most 6,
// not megamorphic) whose callees each pass those rules with at most 8 blocks
// have their calls inlined, and a chain of class tests, one per recorded tag
// in recording order, jumps to the exit's code before the generic Dispatch,
// which every other tag reaches as before: a miss never deoptimizes. A table
// with exactly one entry has its call inlined without records or tests.
#include "tier3.hpp"

#include "tier3_passes.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace neura::nvm::tier3 {

void DispatchRecord::record(std::uint64_t tag) {
    if (tag == ~std::uint64_t{0}) return;
    const auto held = tag + 1;
    for (auto& entry : tags) {
        auto current = entry.load(std::memory_order_relaxed);
        if (current == held) return;
        if (current == 0) {
            if (entry.compare_exchange_strong(current, held, std::memory_order_relaxed)) return;
            if (current == held) return;
        }
    }
    if (megamorphic.load(std::memory_order_relaxed) == 0) megamorphic.store(1, std::memory_order_relaxed);
}

bool DispatchRecord::read(std::vector<std::uint64_t>& out) const {
    out.clear();
    if (megamorphic.load(std::memory_order_relaxed) != 0) return false;
    for (const auto& entry : tags) {
        const auto held = entry.load(std::memory_order_relaxed);
        if (held == 0) break;
        out.push_back(held - 1);
    }
    return true;
}

namespace {

constexpr std::uint32_t max_depth = 4;
constexpr std::uint64_t max_callee_blocks = 64;
constexpr std::uint64_t max_callee_ops = 400;
constexpr std::uint64_t max_dispatch_blocks = 8;
constexpr std::size_t max_class_exits = 6;
constexpr std::uint64_t max_total_ops = 20000;
constexpr std::uint64_t max_growth = 8;

// A function program's size in the units of the limits.
struct Size {
    std::uint64_t blocks = 0;
    std::uint64_t ops = 0;
};

Size size_of(const tier2::Region& program) { return {program.blocks.size(), program.code.size()}; }

ValueType integer(unsigned width) { return {Type::Int, static_cast<std::uint8_t>(std::clamp(width, 1U, 64U))}; }

unsigned bits_of(std::uint64_t value) {
    unsigned bits = 0;
    while (bits < 64 && (value >> bits) != 0) ++bits;
    return bits;
}

// A value of type `value` can stand for a slot of type `slot`: the same kind
// and no wider, so that it fits the slot as the reference requires.
bool fits(ValueType value, ValueType slot) { return value.type == slot.type && value.width <= slot.width; }

// The first instruction of a block that is not a phi, or none.
std::uint32_t first_non_phi(const Function& f, std::uint32_t block) {
    for (const auto id : f.blocks[block].insts) {
        if (f.insts[id].op != Op::Phi) return id;
    }
    return none;
}

bool starts_with_budget(const Function& f, std::uint32_t block) {
    const auto id = first_non_phi(f, block);
    return id != none && f.insts[id].op == Op::Budget;
}

// Whether a path from `block` comes back to it without reaching a block
// that starts with a Budget (`block` itself not counted as one).
bool on_open_cycle(const Function& f, std::uint32_t block) {
    std::vector<std::uint8_t> seen(f.blocks.size(), 0);
    std::vector<std::uint32_t> work(f.blocks[block].succs.begin(), f.blocks[block].succs.end());
    while (!work.empty()) {
        const auto b = work.back();
        work.pop_back();
        if (b == block) return true;
        if (seen[b] || starts_with_budget(f, b)) continue;
        seen[b] = 1;
        for (const auto s : f.blocks[b].succs) work.push_back(s);
    }
    return false;
}

void replace_pred(Block& block, std::uint32_t from, std::uint32_t to) {
    const auto found = std::find(block.preds.begin(), block.preds.end(), from);
    if (found == block.preds.end()) throw std::logic_error("tier 3: an inlined edge is missing");
    *found = to;
}

// A callee's IR after its own inlining, ready to be copied into callers.
struct Callee {
    std::shared_ptr<const Function> ir;
    Size size;
};

// Where a Call sits in its block: the write-back before it, the counter
// reload after it (and the kernel step reload, in a function with inline
// kernels), and the jump to the resume block.
struct CallSite {
    std::uint32_t block = none;
    std::uint32_t write_back = none;
    std::uint32_t call = none;
    std::uint32_t reload = none;
    std::uint32_t jump = none;
    std::uint32_t kernel_reload = none;
};

std::optional<CallSite> call_site(const Function& f, std::uint32_t block) {
    const auto& insts = f.blocks[block].insts;
    for (std::size_t k = 0; k < insts.size(); ++k) {
        if (f.insts[insts[k]].op != Op::Call) continue;
        // The builder's shape: write-back, call, constants (the result's
        // flag), counter reload, kernel step reload (with inline kernels),
        // jump.
        if (k == 0 || insts.size() < k + 3) return std::nullopt;
        auto r = k + 1;
        while (r < insts.size() && f.insts[insts[r]].op == Op::Const) ++r;
        const bool kernel = f.inline_kernels && r + 3 == insts.size() && f.insts[insts[r + 1]].op == Op::EntryCounter &&
                            f.insts[insts[r + 1]].imm == 1;
        if (r + (kernel ? 3 : 2) != insts.size()) return std::nullopt;
        const auto& back = f.insts[insts[k - 1]];
        const auto& reload = f.insts[insts[r]];
        const auto& jump = f.insts[insts.back()];
        if (back.op != Op::WriteBack || back.state != f.insts[insts[k]].state || reload.op != Op::EntryCounter ||
            reload.imm != 0 || jump.op != Op::Jump || f.blocks[block].succs.size() != 1) {
            return std::nullopt;
        }
        return CallSite{block, insts[k - 1], insts[k], insts[r], insts.back(), kernel ? insts[r + 1] : none};
    }
    return std::nullopt;
}

class Inliner {
public:
    Inliner(const nvr::Image& image, const InlineSource& source) : image_(image), source_(source) {}

    void run(Function& root) {
        const auto own = size_of(*root.region);
        limit_ = std::min<std::uint64_t>(max_growth * own.ops, max_total_ops);
        total_ = own.ops;
        std::vector<std::uint32_t> chain{root.function};
        inline_into(root, chain, 0, own);
        detail::remove_unreachable(root);
        detail::compact(root);
        detail::compute_budgets(root);
    }

private:
    // Inlines what the rules allow into `f`, at `level` below the root, with
    // the functions of `chain` (f's included) above it. Returns f's size
    // after its own inlining, starting from `own`.
    Size inline_into(Function& f, std::vector<std::uint32_t>& chain, std::uint32_t level, Size own) {
        if (level >= max_depth) return own;
        // The blocks that are exits of a Dispatch, and the Dispatch blocks.
        std::vector<std::uint8_t> dispatch_exit(f.blocks.size(), 0);
        std::vector<std::uint32_t> dispatches;
        std::vector<std::uint32_t> calls;
        for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
            if (f.blocks[b].insts.empty()) continue;
            if (f.insts[f.blocks[b].insts.back()].op == Op::Dispatch) {
                dispatches.push_back(b);
                for (const auto s : f.blocks[b].succs) dispatch_exit[s] = 1;
            }
        }
        for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
            if (!dispatch_exit[b] && call_site(f, b)) calls.push_back(b);
        }
        // The root keeps within the total; a callee within its own limits.
        const auto room = [&](const Size& add) {
            if (level == 0) return total_ + add.ops <= limit_;
            return own.ops + add.ops <= max_callee_ops && own.blocks + add.blocks <= max_callee_blocks;
        };
        const auto grow = [&](const Size& add) {
            own.ops += add.ops;
            own.blocks += add.blocks;
            if (level == 0) total_ += add.ops;
        };
        for (const auto b : calls) {
            const auto site = *call_site(f, b);
            const auto& call = f.insts[site.call];
            const auto callee = prepare(static_cast<std::uint32_t>(call.imm), chain, level + 1, max_callee_blocks);
            if (!callee || !room(callee->size) || !compatible(f, site, *callee->ir)) continue;
            splice(f, site, *callee->ir);
            grow(callee->size);
        }
        for (const auto d : dispatches) inline_dispatch(f, d, chain, level, room, grow);
        return own;
    }

    template <typename Room, typename Grow>
    void inline_dispatch(Function& f, std::uint32_t d, std::vector<std::uint32_t>& chain, std::uint32_t level,
                         const Room& room, const Grow& grow) {
        const auto dispatch = f.blocks[d].insts.back();
        const auto table_index = static_cast<std::uint32_t>(f.insts[dispatch].imm);
        if (table_index >= f.dispatch_tables.size() || table_index >= f.dispatch_origins.size()) return;
        const auto table = f.dispatch_tables[table_index];
        // The exits to inline, and the class tests (tag, exit) to make.
        std::vector<std::uint32_t> exits;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> tests;
        if (table.exits.size() == 1) {
            exits.push_back(table.exits.front().second);
        } else {
            const auto [program, index] = f.dispatch_origins[table_index];
            const auto* record = source_.record ? source_.record(*f.programs[program], index) : nullptr;
            std::vector<std::uint64_t> tags;
            if (record == nullptr || !record->read(tags)) return;
            for (const auto tag : tags) {
                const auto found = std::lower_bound(table.exits.begin(), table.exits.end(), tag,
                                                    [](const auto& entry, std::uint64_t key) { return entry.first < key; });
                if (found == table.exits.end() || found->first != tag) continue;
                tests.emplace_back(tag, found->second);
                if (std::find(exits.begin(), exits.end(), found->second) == exits.end()) exits.push_back(found->second);
            }
            if (exits.size() > max_class_exits) return;
        }
        if (exits.empty()) return;
        // Every exit's callee must pass, or nothing is inlined here.
        std::vector<std::pair<CallSite, Callee>> chosen;
        Size added;
        for (const auto exit : exits) {
            if (exit == table.fallback || exit >= f.blocks[d].succs.size()) return;
            const auto block = f.blocks[d].succs[exit];
            const auto site = call_site(f, block);
            if (!site || f.blocks[block].preds.size() != 1 || f.insts[f.blocks[block].insts.front()].op == Op::Phi) return;
            const auto callee =
                prepare(static_cast<std::uint32_t>(f.insts[site->call].imm), chain, level + 1, max_dispatch_blocks);
            if (!callee || !compatible(f, *site, *callee->ir)) return;
            added.ops += callee->size.ops;
            added.blocks += callee->size.blocks;
            chosen.emplace_back(*site, *callee);
        }
        if (!room(added)) return;
        for (const auto& [site, callee] : chosen) {
            splice(f, site, *callee.ir);
            grow(callee.size);
        }
        if (!tests.empty()) {
            class_tests(f, d, tests);
            ++f.class_tested_dispatches;
        }
    }

    // The callee `function`'s IR after its own inlining at `level`, when it
    // passes the rules; `max_blocks` is the most blocks it may have.
    std::optional<Callee> prepare(std::uint32_t function, std::vector<std::uint32_t>& chain, std::uint32_t level,
                                  std::uint64_t max_blocks) {
        if (level > max_depth) return std::nullopt;
        // No recursion on the current chain.
        if (std::find(chain.begin(), chain.end(), function) != chain.end()) return std::nullopt;
        const auto* program = source_.program ? source_.program(function) : nullptr;
        if (program == nullptr || !program->program || program->entries.empty() ||
            program->entries.front().block != 0) {
            return std::nullopt;
        }
        for (const auto slot : program->entries.front().reads) {
            if (slot >= program->parameter_count) return std::nullopt;
        }
        const auto base = size_of(*program);
        if (base.blocks > max_blocks || base.ops > max_callee_ops) return std::nullopt;
        auto key = chain;
        key.push_back(function);
        key.push_back(level);
        if (const auto found = prepared_.find(key); found != prepared_.end()) {
            if (!found->second || found->second->size.blocks > max_blocks) return std::nullopt;
            return found->second;
        }
        auto& slot = prepared_[key];
        auto built = build(image_, *program, false);
        if (!built.program) return std::nullopt;
        auto& ir = built.program->ir;
        chain.push_back(function);
        const auto own = inline_into(ir, chain, level, base);
        chain.pop_back();
        if (own.blocks > max_callee_blocks || own.ops > max_callee_ops || !inlinable(ir)) return std::nullopt;
        // Only entry 0 is ever entered in a caller.
        ir.entries.resize(1);
        detail::remove_unreachable(ir);
        detail::compact(ir);
        slot = Callee{std::make_shared<const Function>(std::move(ir)), own};
        if (slot->size.blocks > max_blocks) return std::nullopt;
        return slot;
    }

    // The callee's code from entry 0 never writes a frame, never calls, and
    // reads only the counters at entry.
    static bool inlinable(const Function& g) {
        std::vector<std::uint8_t> seen(g.blocks.size(), 0);
        std::vector<std::uint32_t> work{g.entries.front().ir_block};
        seen[work.front()] = 1;
        while (!work.empty()) {
            const auto b = work.back();
            work.pop_back();
            for (const auto id : g.blocks[b].insts) {
                const auto& in = g.insts[id];
                switch (in.op) {
                case Op::Call:
                case Op::Helper:
                case Op::Leave:
                case Op::WriteBack: return false;
                case Op::EntryCounter:
                    if (b != g.entries.front().ir_block || in.imm > 1) return false;
                    break;
                default: break;
                }
            }
            for (const auto s : g.blocks[b].succs) {
                if (!seen[s]) {
                    seen[s] = 1;
                    work.push_back(s);
                }
            }
        }
        return true;
    }

    // The call's arguments fit the callee's parameters and its returned
    // values fit the call's result; a callee with inline kernels goes only
    // into a caller that has them (whose K is an SSA value).
    static bool compatible(const Function& f, const CallSite& site, const Function& g) {
        const auto& call = f.insts[site.call];
        if (g.inline_kernels && !f.inline_kernels) return false;
        if (call.count != g.parameter_count || g.parameter_count > g.slot_types.size()) return false;
        for (std::uint32_t i = 0; i < call.count; ++i) {
            const auto& argument = f.insts[f.extras[call.extra + i]];
            if (!fits(argument.type, g.slot_types[i]) && !constant_fits(argument, g.slot_types[i])) return false;
        }
        const bool has_result = f.exits[call.aux]->result_slot != none;
        for (const auto& block : g.blocks) {
            if (block.insts.empty()) continue;
            const auto& last = g.insts[block.insts.back()];
            if (last.op != Op::Return) continue;
            if (!has_result) continue;
            if (last.operands[1] == none || !fits(g.insts[last.operands[1]].type, call.type)) return false;
        }
        return true;
    }

    // A constant, typed by its bits, as the argument of a float parameter
    // whose width holds them: it becomes a constant of the parameter's type.
    static bool constant_fits(const Inst& argument, ValueType slot) {
        return argument.op == Op::Const && argument.type.type == Type::Int &&
               (slot.type == Type::F32 || slot.type == Type::F64) && argument.type.width <= slot.width;
    }

    std::uint32_t add_inst(Function& f, Inst in) {
        f.insts.push_back(std::move(in));
        return static_cast<std::uint32_t>(f.insts.size() - 1);
    }

    std::uint32_t add_block(Function& f, const Block& like) {
        Block block;
        block.origin_block = like.origin_block;
        block.origin_function = like.origin_function;
        block.loop_depth = like.loop_depth;
        block.cold = like.cold;
        f.blocks.push_back(std::move(block));
        return static_cast<std::uint32_t>(f.blocks.size() - 1);
    }

    // Copies `g` into `f` in place of the call at `site`.
    void splice(Function& f, const CallSite& site, const Function& g) {
        const auto x = site.block;
        const auto resume = f.blocks[x].succs.front();
        const auto call_state = f.insts[site.call].state;
        const auto& exit = *f.exits[f.insts[site.call].aux];
        std::vector<std::uint32_t> arguments(f.extras.begin() + f.insts[site.call].extra,
                                             f.extras.begin() + f.insts[site.call].extra + f.insts[site.call].count);
        const auto charged = f.insts[site.call].operands[0];
        // K at the call (none without inline kernels): the callee's K at its
        // entry, and throughout a callee without inline kernels.
        const auto kernel = f.insts[site.call].operands[1];
        const auto x_origin = f.insts[site.call];

        const auto oi = static_cast<std::uint32_t>(f.insts.size());
        const auto ob = static_cast<std::uint32_t>(f.blocks.size());
        const auto os = static_cast<std::uint32_t>(f.states.size());
        const auto oe = static_cast<std::uint32_t>(f.exits.size());
        const auto od = static_cast<std::uint32_t>(f.dispatch_tables.size());
        const auto op = static_cast<std::uint32_t>(f.programs.size());
        const auto ot = static_cast<std::uint32_t>(f.type_tests.size());
        const auto entry = g.entries.front().ir_block;

        // What the callee's entry values are here: the arguments for its
        // parameters, zero and uninitialized for its other slots, the
        // caller's count after the call block's step.
        std::map<std::uint32_t, std::uint32_t> at_entry;
        std::vector<std::uint32_t> made;
        for (const auto id : g.blocks[entry].insts) {
            const auto& in = g.insts[id];
            Inst value;
            value.op = Op::Const;
            value.origin_block = x_origin.origin_block;
            value.origin_function = x_origin.origin_function;
            value.origin_seq = x_origin.origin_seq;
            const auto slot = static_cast<std::uint32_t>(in.imm);
            switch (in.op) {
            case Op::EntryLoad:
                if (slot < g.parameter_count && !constant_fits(f.insts[arguments[slot]], g.slot_types[slot])) {
                    at_entry[id] = arguments[slot];
                    continue;
                }
                if (slot < g.parameter_count) {
                    value.type = g.slot_types[slot];
                    value.imm = f.insts[arguments[slot]].imm;
                    break;
                }
                value.type = in.type;
                value.imm = 0;
                break;
            case Op::EntryFlag:
                value.type = integer(1);
                value.imm = slot < g.parameter_count ? 1 : 0;
                break;
            case Op::EntryCounter: at_entry[id] = in.imm == 0 ? charged : kernel; continue;
            default: continue;
            }
            const auto made_id = add_inst(f, std::move(value));
            made.push_back(made_id);
            at_entry[id] = made_id;
        }
        const auto shift = static_cast<std::uint32_t>(f.insts.size()) - oi;
        const auto map = [&](std::uint32_t v) -> std::uint32_t {
            if (v == none) return none;
            if (const auto found = at_entry.find(v); found != at_entry.end()) return found->second;
            return v + oi + shift;
        };

        // Instructions, operands, exits, dispatch tables and programs.
        for (const auto& source : g.insts) {
            auto in = source;
            for (auto& operand : in.operands) operand = map(operand);
            if (in.count != 0) {
                const auto first = static_cast<std::uint32_t>(f.extras.size());
                for (std::uint32_t i = 0; i < in.count; ++i) {
                    const auto v = g.extras[in.extra + i];
                    f.extras.push_back(in.op == Op::Gather && (i % 2) != 0 ? v : map(v));
                }
                in.extra = first;
            }
            if (in.state != none) in.state += os;
            in.guard = map(in.guard);
            if ((in.op == Op::StateCall && (in.flags & CheckReference) == 0) || in.op == Op::Helper || in.op == Op::Call ||
                in.op == Op::AllocObject || in.op == Op::Append) {
                in.aux += oe;
            }
            if (in.op == Op::Dispatch) in.imm += od;
            // The counter checks and the returns of a callee without inline
            // kernels see the caller's K, which does not change in it.
            if (in.op == Op::Budget && in.operands[1] == none) in.operands[1] = kernel;
            if (in.op == Op::Return && in.operands[2] == none) in.operands[2] = kernel;
            // The callee's type tests follow the caller's.
            if (in.op == Op::CheckKind || in.op == Op::TestKind) in.imm += ot;
            f.insts.push_back(std::move(in));
        }
        f.exits.insert(f.exits.end(), g.exits.begin(), g.exits.end());
        f.dispatch_tables.insert(f.dispatch_tables.end(), g.dispatch_tables.begin(), g.dispatch_tables.end());
        f.type_tests.insert(f.type_tests.end(), g.type_tests.begin(), g.type_tests.end());
        for (const auto& [program, table] : g.dispatch_origins) f.dispatch_origins.emplace_back(program + op, table);
        f.programs.insert(f.programs.end(), g.programs.begin(), g.programs.end());
        f.call_frames.insert(g.call_frames.begin(), g.call_frames.end());

        // Frame states: the callee's own level becomes an inlined frame
        // below the call's state.
        for (const auto& source : g.states) {
            auto state = source;
            for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
                auto& slot = state.slots[r];
                auto& flag = state.flags[r];
                if (slot.kind == StateSlot::Kind::Value) slot.value = map(static_cast<std::uint32_t>(slot.value));
                if (flag.kind == StateFlag::Kind::Value) flag.value = map(flag.value);
                if (source.caller != none || r >= g.parameter_count) continue;
                // The frame push_frame makes holds the argument, initialized.
                if (slot.kind == StateSlot::Kind::Unchanged) slot = {StateSlot::Kind::Value, arguments[r]};
                if (flag.kind == StateFlag::Kind::Unchanged) flag = {StateFlag::Kind::One, 0};
            }
            state.blocks = map(state.blocks);
            state.kernel_steps = source.kernel_steps == none ? kernel : map(state.kernel_steps);
            state.program += op;
            if (source.caller != none) {
                state.caller += os;
            } else {
                state.caller = call_state;
                state.return_slot = exit.result_slot;
                state.return_block = exit.resume;
            }
            f.states.push_back(std::move(state));
        }

        // Blocks. The callee's entry blocks are left empty and unreached.
        const auto base = oi + shift;
        for (std::uint32_t b = 0; b < g.blocks.size(); ++b) {
            auto block = g.blocks[b];
            block.entry = none;
            for (auto& p : block.preds) p += ob;
            for (auto& s : block.succs) s += ob;
            for (auto& id : block.insts) id += base;
            if (g.blocks[b].entry != none) {
                block.insts.clear();
                block.succs.clear();
            }
            f.blocks.push_back(std::move(block));
        }
        const auto first = g.blocks[entry].succs.front() + ob;
        replace_pred(f.blocks[first], entry + ob, x);

        // The call block jumps into the callee.
        {
            auto& insts = f.blocks[x].insts;
            std::erase_if(insts, [&](std::uint32_t id) {
                return id == site.write_back || id == site.call || id == site.reload || id == site.jump ||
                       id == site.kernel_reload;
            });
            insts.insert(insts.end(), made.begin(), made.end());
            insts.push_back(site.jump);
            f.blocks[x].succs = {first};
        }

        // The returns jump to the join, whose phis are the call's result and
        // the counter after the return block's step.
        const auto join = add_block(f, f.blocks[x]);
        std::vector<std::uint32_t> results;
        std::vector<std::uint32_t> counts;
        std::vector<std::uint32_t> kernel_counts;
        for (std::uint32_t b = ob; b < ob + g.blocks.size(); ++b) {
            auto& block = f.blocks[b];
            if (block.insts.empty() || f.insts[block.insts.back()].op != Op::Return) continue;
            const auto ret = block.insts.back();
            Inst jump;
            jump.op = Op::Jump;
            jump.origin_block = f.insts[ret].origin_block;
            jump.origin_function = f.insts[ret].origin_function;
            jump.origin_seq = f.insts[ret].origin_seq;
            const auto value = f.insts[ret].operands[1];
            kernel_counts.push_back(f.insts[ret].operands[2]);
            auto& add = f.insts[ret];
            add.op = Op::CounterAdd;
            add.type = integer(64);
            add.operands[1] = none;
            add.operands[2] = none;
            add.imm = 1;
            add.flags = 0;
            add.state = none;
            results.push_back(value);
            counts.push_back(ret);
            const auto jump_id = add_inst(f, std::move(jump));
            f.blocks[b].insts.push_back(jump_id);
            f.blocks[b].succs = {join};
            f.blocks[join].preds.push_back(b);
        }
        const auto phi = [&](std::uint32_t id, const std::vector<std::uint32_t>& operands) {
            auto& in = f.insts[id];
            in.op = Op::Phi;
            in.operands[0] = in.operands[1] = in.operands[2] = none;
            in.flags = 0;
            in.state = none;
            in.aux = none;
            in.imm = 0;
            in.extra = static_cast<std::uint32_t>(f.extras.size());
            in.count = static_cast<std::uint32_t>(operands.size());
            f.extras.insert(f.extras.end(), operands.begin(), operands.end());
        };
        auto& join_insts = f.blocks[join].insts;
        if (exit.result_slot != none) {
            phi(site.call, results);
            join_insts.push_back(site.call);
        }
        phi(site.reload, counts);
        join_insts.push_back(site.reload);
        if (site.kernel_reload != none) {
            phi(site.kernel_reload, kernel_counts);
            join_insts.push_back(site.kernel_reload);
        }
        if (exit.result_slot == none) {
            // Nothing reads the result of a call without a result slot.
            auto& in = f.insts[site.call];
            in.op = Op::Const;
            in.operands[0] = none;
            in.flags = 0;
            in.state = none;
            in.aux = none;
            in.imm = 0;
            in.count = 0;
            join_insts.push_back(site.call);
        }
        Inst to_resume;
        to_resume.op = Op::Jump;
        to_resume.origin_block = x_origin.origin_block;
        to_resume.origin_function = x_origin.origin_function;
        to_resume.origin_seq = x_origin.origin_seq;
        join_insts.push_back(add_inst(f, std::move(to_resume)));
        f.blocks[join].succs = {resume};
        replace_pred(f.blocks[resume], x, join);

        // The callee's entry checkpoint, and the caller's resume checkpoint.
        if (starts_with_budget(f, first) && !on_open_cycle(f, first)) remove_budget(f, first);
        // A resume block that a call not inlined returns to keeps its check:
        // the callee's blocks run before the caller's next ones.
        const bool after_call = std::any_of(f.blocks[resume].preds.begin(), f.blocks[resume].preds.end(),
                                            [&](std::uint32_t p) { return call_site(f, p).has_value(); });
        if (!after_call && starts_with_budget(f, resume) && !on_open_cycle(f, resume)) {
            const auto budget = first_non_phi(f, resume);
            for (std::uint32_t k = 0; k < f.blocks[resume].preds.size(); ++k) {
                const auto p = f.blocks[resume].preds[k];
                if (f.blocks[p].entry != none) check_on_edge(f, resume, k, budget);
            }
            remove_budget(f, resume);
        }

        f.max_inline_depth = std::max(f.max_inline_depth, g.max_inline_depth + 1);
        f.uses_float = f.uses_float || g.uses_float;
        f.inlined_calls += 1 + g.inlined_calls;
        f.class_tested_dispatches += g.class_tested_dispatches;
    }

    static void remove_budget(Function& f, std::uint32_t block) {
        auto& insts = f.blocks[block].insts;
        const auto budget = first_non_phi(f, block);
        insts.erase(std::find(insts.begin(), insts.end(), budget));
    }

    // Puts a copy of the Budget `budget` of `block` at the end of its entry
    // predecessor number `k`, with the block's phis read on that edge.
    static void check_on_edge(Function& f, std::uint32_t block, std::uint32_t k, std::uint32_t budget) {
        const auto pred = f.blocks[block].preds[k];
        std::map<std::uint32_t, std::uint32_t> on_edge;
        for (const auto id : f.blocks[block].insts) {
            const auto& in = f.insts[id];
            if (in.op != Op::Phi) break;
            on_edge[id] = f.extras[in.extra + k];
        }
        const auto edge = [&](std::uint32_t v) {
            const auto found = on_edge.find(v);
            return found == on_edge.end() ? v : found->second;
        };
        auto state = f.states[f.insts[budget].state];
        for (auto& slot : state.slots) {
            if (slot.kind == StateSlot::Kind::Value) slot.value = edge(static_cast<std::uint32_t>(slot.value));
        }
        for (auto& flag : state.flags) {
            if (flag.kind == StateFlag::Kind::Value) flag.value = edge(flag.value);
        }
        state.blocks = state.blocks == none ? none : edge(state.blocks);
        state.kernel_steps = state.kernel_steps == none ? none : edge(state.kernel_steps);
        auto check = f.insts[budget];
        check.operands[0] = edge(check.operands[0]);
        if (check.operands[1] != none) check.operands[1] = edge(check.operands[1]);
        check.state = static_cast<std::uint32_t>(f.states.size());
        f.states.push_back(std::move(state));
        f.insts.push_back(std::move(check));
        auto& insts = f.blocks[pred].insts;
        insts.insert(insts.end() - 1, static_cast<std::uint32_t>(f.insts.size() - 1));
    }

    // A chain of class tests at the end of dispatch block `d`: for each
    // (tag, exit) in order, a jump to the exit's code when the tag is the
    // one tested, and after the last the block's Dispatch, unchanged.
    void class_tests(Function& f, std::uint32_t d, const std::vector<std::pair<std::uint64_t, std::uint32_t>>& tests) {
        const auto dispatch = f.blocks[d].insts.back();
        const auto tag = f.insts[dispatch].operands[0];
        const auto exits = f.blocks[d].succs;
        const auto generic = add_block(f, f.blocks[d]);
        f.blocks[d].insts.pop_back();
        f.blocks[generic].insts.push_back(dispatch);
        f.blocks[generic].succs = exits;
        for (const auto s : exits) replace_pred(f.blocks[s], d, generic);
        f.blocks[d].succs.clear();
        const auto origin = f.insts[dispatch];
        const auto make = [&](Inst in) {
            in.origin_block = origin.origin_block;
            in.origin_function = origin.origin_function;
            in.origin_seq = origin.origin_seq;
            return add_inst(f, std::move(in));
        };
        auto current = d;
        for (std::size_t i = 0; i < tests.size(); ++i) {
            const auto [value, exit] = tests[i];
            Inst constant;
            constant.op = Op::Const;
            constant.type = integer(bits_of(value));
            constant.imm = value;
            const auto c = make(std::move(constant));
            Inst equal;
            equal.op = Op::Lift;
            equal.type = integer(1);
            equal.lifted.op = lift::Opcode::Equal;
            equal.lifted.width = 64;
            equal.operands[0] = tag;
            equal.operands[1] = c;
            const auto e = make(std::move(equal));
            Inst branch;
            branch.op = Op::Branch;
            branch.operands[0] = e;
            const auto br = make(std::move(branch));
            const auto next = i + 1 == tests.size() ? generic : add_block(f, f.blocks[d]);
            auto& block = f.blocks[current];
            block.insts.insert(block.insts.end(), {c, e, br});
            const auto target = exits[exit];
            block.succs = {target, next};
            f.blocks[target].preds.push_back(current);
            f.blocks[next].preds.push_back(current);
            current = next;
        }
    }

    const nvr::Image& image_;
    const InlineSource& source_;
    std::uint64_t limit_ = 0;
    std::uint64_t total_ = 0;
    // Callees prepared so far, by inline chain, function and level.
    std::map<std::vector<std::uint32_t>, std::optional<Callee>> prepared_;
};

} // namespace

void inline_calls(const nvr::Image& image, Function& function, const InlineSource& source, bool verify) {
    if (function.region == nullptr) return;
    Inliner(image, source).run(function);
    if (!verify) return;
    const auto violations = tier3::verify(image, function);
    if (violations.empty()) return;
    std::fprintf(stderr, "neura: NEURA_TIER3_VERIFY: the IR of function %u is not well formed after inlining:\n",
                 function.function);
    for (const auto& violation : violations) std::fprintf(stderr, "  %s\n", violation.c_str());
    std::fprintf(stderr, "%s", describe(function).c_str());
    std::abort();
}

} // namespace neura::nvm::tier3

#include "tier3_passes.hpp"

#include "lift/evaluate.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

// Loops, preheaders, and loop-invariant code motion with guarded loop
// entries (docs/runtime/NVM.md, "Execution tiers"). A loop is a natural loop
// whose header dominates its whole body; a loop the interpreter can enter
// anywhere but its header (a resume block inside it is an entry too) is left
// alone. Every entry into a loop's header, the entries from the interpreter
// included, passes through the loop's preheader, so what the preheader runs
// runs on every entry.
namespace neura::nvm::tier3::detail {

std::vector<Loop> find_loops(const Function& f, const Cfg& cfg) {
    const auto n = f.blocks.size();
    std::map<std::uint32_t, std::vector<std::uint32_t>> latches;
    for (const auto b : cfg.rpo) {
        for (const auto s : f.blocks[b].succs) {
            if (cfg.dominates(s, b)) latches[s].push_back(b);
        }
    }
    std::vector<Loop> loops;
    for (const auto& [header, ends] : latches) {
        Loop loop;
        loop.header = header;
        loop.latches = ends;
        loop.in_body.assign(n, 0);
        loop.in_body[header] = 1;
        std::vector<std::uint32_t> work;
        for (const auto latch : ends) {
            if (!loop.in_body[latch]) {
                loop.in_body[latch] = 1;
                work.push_back(latch);
            }
        }
        while (!work.empty()) {
            const auto x = work.back();
            work.pop_back();
            for (const auto p : f.blocks[x].preds) {
                if (!loop.in_body[p]) {
                    loop.in_body[p] = 1;
                    work.push_back(p);
                }
            }
        }
        bool single_entry = true;
        for (std::uint32_t b = 0; b < n; ++b) {
            if (!loop.in_body[b]) continue;
            loop.body.push_back(b);
            if (!cfg.dominates(header, b)) single_entry = false;
        }
        if (single_entry) loops.push_back(std::move(loop));
    }
    for (auto& loop : loops) {
        for (const auto& other : loops) {
            if (other.in_body[loop.header]) ++loop.depth;
        }
    }
    std::stable_sort(loops.begin(), loops.end(), [](const Loop& a, const Loop& b) { return a.depth > b.depth; });
    return loops;
}

std::uint32_t head_state(const Function& f, const Loop& loop) {
    const auto& header = f.blocks[loop.header];
    if (!header.image_start || header.entry != none) return none;
    for (const auto id : header.insts) {
        const auto& in = f.insts[id];
        if (in.op == Op::Phi) continue;
        if (in.op != Op::Budget || in.state == none) return none;
        const auto& state = f.states[in.state];
        if (state.caller != none || state.function != f.function || state.block != header.origin_block) return none;
        return in.state;
    }
    return none;
}

std::uint32_t make_preheader(Function& f, const Loop& loop) {
    const auto h = loop.header;
    std::vector<std::uint32_t> entering;
    std::vector<std::uint32_t> back;
    for (std::uint32_t k = 0; k < f.blocks[h].preds.size(); ++k) {
        (loop.in_body[f.blocks[h].preds[k]] ? back : entering).push_back(k);
    }
    if (entering.empty()) throw std::logic_error("tier 3: a loop has no entry");
    if (entering.size() == 1) {
        const auto p = f.blocks[h].preds[entering[0]];
        if (f.blocks[p].preheader && f.blocks[p].succs.size() == 1) return p;
    }
    const auto old_preds = f.blocks[h].preds;
    const auto p = static_cast<std::uint32_t>(f.blocks.size());
    {
        Block block;
        block.origin_block = f.blocks[h].origin_block;
        block.origin_function = f.blocks[h].origin_function;
        block.loop_depth = f.blocks[h].loop_depth;
        block.preheader = true;
        f.blocks.push_back(std::move(block));
    }
    for (const auto k : entering) f.blocks[p].preds.push_back(old_preds[k]);
    std::vector<std::uint32_t> seen;
    for (const auto k : entering) {
        const auto x = old_preds[k];
        if (std::find(seen.begin(), seen.end(), x) != seen.end()) continue;
        seen.push_back(x);
        std::replace(f.blocks[x].succs.begin(), f.blocks[x].succs.end(), h, p);
    }
    // The header's phis take the entry operands from one phi of the
    // preheader (or the one value the entries share).
    std::vector<std::uint32_t> made;
    const auto header_insts = f.blocks[h].insts;
    for (const auto id : header_insts) {
        if (f.insts[id].op != Op::Phi) break;
        const auto old_extra = f.insts[id].extra;
        std::vector<std::uint32_t> operands;
        for (std::uint32_t k = 0; k < old_preds.size(); ++k) operands.push_back(f.extras[old_extra + k]);
        bool same = true;
        for (const auto k : entering) same = same && operands[k] == operands[entering[0]];
        std::uint32_t entry_value = operands[entering[0]];
        if (!same) {
            Inst phi;
            phi.op = Op::Phi;
            phi.type = f.insts[id].type;
            phi.origin_block = f.blocks[p].origin_block;
            phi.origin_function = f.blocks[p].origin_function;
            phi.extra = static_cast<std::uint32_t>(f.extras.size());
            phi.count = static_cast<std::uint32_t>(entering.size());
            for (const auto k : entering) f.extras.push_back(operands[k]);
            entry_value = static_cast<std::uint32_t>(f.insts.size());
            f.insts.push_back(phi);
            made.push_back(entry_value);
        }
        std::vector<std::uint32_t> now;
        for (std::uint32_t k = 0; k < old_preds.size(); ++k) {
            if (k == entering[0]) now.push_back(entry_value);
            else if (std::find(entering.begin(), entering.end(), k) == entering.end()) now.push_back(operands[k]);
        }
        auto& phi = f.insts[id];
        phi.count = static_cast<std::uint32_t>(now.size());
        for (std::size_t i = 0; i < now.size(); ++i) f.extras[old_extra + i] = now[i];
    }
    std::vector<std::uint32_t> preds;
    for (std::uint32_t k = 0; k < old_preds.size(); ++k) {
        if (k == entering[0]) preds.push_back(p);
        else if (std::find(entering.begin(), entering.end(), k) == entering.end()) preds.push_back(old_preds[k]);
    }
    f.blocks[h].preds = std::move(preds);
    Inst jump;
    jump.op = Op::Jump;
    jump.origin_block = f.blocks[p].origin_block;
    jump.origin_function = f.blocks[p].origin_function;
    const auto jump_id = static_cast<std::uint32_t>(f.insts.size());
    f.insts.push_back(jump);
    f.blocks[p].insts = made;
    f.blocks[p].insts.push_back(jump_id);
    f.blocks[p].succs.assign(1, h);
    return p;
}

std::uint32_t entry_operand(const Function& f, const Loop& loop, std::uint32_t phi) {
    const auto& header = f.blocks[loop.header];
    for (std::uint32_t k = 0; k < header.preds.size(); ++k) {
        const auto p = header.preds[k];
        if (f.blocks[p].preheader && !(p < loop.in_body.size() && loop.in_body[p])) {
            return f.extras[f.insts[phi].extra + k];
        }
    }
    // No preheader: the loop's one entering predecessor.
    for (std::uint32_t k = 0; k < header.preds.size(); ++k) {
        const auto p = header.preds[k];
        if (!(p < loop.in_body.size() && loop.in_body[p])) return f.extras[f.insts[phi].extra + k];
    }
    throw std::logic_error("tier 3: a loop has no entry");
}

std::uint32_t preheader_state(Function& f, const Loop& loop, std::uint32_t) {
    const auto source = head_state(f, loop);
    if (source == none) throw std::logic_error("tier 3: a loop has no entry state");
    std::map<std::uint32_t, std::uint32_t> phis;
    for (const auto id : f.blocks[loop.header].insts) {
        if (f.insts[id].op != Op::Phi) break;
        phis[id] = entry_operand(f, loop, id);
    }
    FrameState state = f.states[source];
    const auto map = [&](std::uint32_t v) {
        const auto found = phis.find(v);
        return found == phis.end() ? v : found->second;
    };
    if (state.blocks != none) state.blocks = map(state.blocks);
    if (state.kernel_steps != none) state.kernel_steps = map(state.kernel_steps);
    for (auto& slot : state.slots) {
        if (slot.kind == StateSlot::Kind::Value) slot.value = map(static_cast<std::uint32_t>(slot.value));
    }
    for (auto& flag : state.flags) {
        if (flag.kind == StateFlag::Kind::Value) flag.value = map(flag.value);
    }
    const auto id = static_cast<std::uint32_t>(f.states.size());
    f.states.push_back(std::move(state));
    return id;
}

namespace {

const char* heap_kind_name(Op op) {
    switch (op) {
    case Op::CheckNotNil: return "nil";
    case Op::Lookup: return "lookup";
    case Op::CheckKind: return "kind";
    case Op::CheckInit: return "init";
    case Op::CheckIndex: return "bounds";
    default: return "global";
    }
}

// A pure op that is safe to run where it did not run before. An op a guard
// covers stays where the guard dominates it; a division whose check went
// stays where the check's proof held (a hardware division by zero traps).
bool hoistable_pure(const Inst& in) {
    if (in.guard != none) return false;
    switch (in.op) {
    case Op::RunConst:
    case Op::Mask:
    case Op::Gather: return true;
    case Op::Lift:
        return (in.flags & Checked) == 0 && in.lifted.op != lift::Opcode::Constant &&
               in.lifted.op != lift::Opcode::Divide && in.lifted.op != lift::Opcode::Remainder;
    default: return false;
    }
}

bool hoist_one(Function& f, const Cfg& cfg, const Loop& loop, Counts& counts) {
    const Places places(f);
    const auto n = f.blocks.size();
    const bool has_state = head_state(f, loop) != none;
    // A header phi that only ever carries its own value holds what the
    // preheader gives it.
    std::vector<std::uint32_t> invariant_phis;
    {
        const auto& header = f.blocks[loop.header];
        for (const auto id : header.insts) {
            const auto& phi = f.insts[id];
            if (phi.op != Op::Phi) break;
            bool self = true;
            for (std::uint32_t k = 0; k < header.preds.size(); ++k) {
                if (loop.in_body[header.preds[k]] && f.extras[phi.extra + k] != id) self = false;
            }
            if (self) invariant_phis.push_back(id);
        }
    }
    std::vector<std::uint8_t> outside(f.insts.size(), 0);
    for (const auto id : invariant_phis) outside[id] = 1;
    // A constant in the body moves with the op that needs it.
    const auto is_outside = [&](std::uint32_t v) {
        if (v == none) return true;
        if (outside[v]) return true;
        const auto b = places.block[v];
        return b != none && (!loop.in_body[b] || f.insts[v].op == Op::Const);
    };
    // Whether an effect can run between the header and the start of a block,
    // along some path of the body (back edges to the header cut).
    std::vector<std::uint8_t> tainted(n, 0);
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto b : cfg.rpo) {
            if (!loop.in_body[b] || b == loop.header) continue;
            std::uint8_t in = 0;
            for (const auto p : f.blocks[b].preds) {
                if (!loop.in_body[p]) continue;
                std::uint8_t out = tainted[p];
                for (const auto id : f.blocks[p].insts) {
                    if (is_effect(f.insts[id])) out = 1;
                }
                in |= out;
            }
            if (in != tainted[b]) {
                tainted[b] = in;
                changed = true;
            }
        }
    }
    // What the loop writes of the heap: the heap reads and checks that may
    // leave it are those nothing in it changes (6.4, 6.6 of the design: a
    // helper, call or state call may free, collect, grow or write anything).
    bool safepoint = false;
    bool widens = false;
    bool stable_for_preflight = true;
    bool has_list_bounds = false;
    std::vector<std::uint64_t> fields;
    std::vector<std::uint64_t> globals;
    for (const auto b : loop.body) {
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            if (is_effect(in) && in.op != Op::StoreElement) stable_for_preflight = false;
            if (in.op == Op::CheckIndex && in.operands[1] != none && f.insts[in.operands[1]].op == Op::LoadLength) {
                has_list_bounds = true;
            }
            if (in.op == Op::Helper || in.op == Op::Call || in.op == Op::StateCall || in.op == Op::AllocObject ||
                in.op == Op::Append) {
                safepoint = true;
            }
            else if (in.op == Op::Widen) widens = true;
            else if (in.op == Op::StoreField) fields.push_back(in.imm);
            else if (in.op == Op::StoreGlobal) globals.push_back(in.imm);
        }
    }
    const auto written = [](const std::vector<std::uint64_t>& list, std::uint64_t index) {
        return std::find(list.begin(), list.end(), index) != list.end();
    };
    // Entries whose kind and type a check before the loop, or one that
    // leaves it, has fixed: a field or version may be read from them.
    std::vector<std::uint8_t> kind_checked(f.insts.size(), 0);
    for (std::uint32_t id = 0; id < f.insts.size(); ++id) {
        const auto& in = f.insts[id];
        const auto b = places.block[id];
        if (in.op == Op::CheckKind && b != none && !loop.in_body[b] && cfg.dominates(b, loop.header)) {
            kind_checked[in.operands[0]] = 1;
        }
    }
    struct Move {
        std::uint32_t id;
        bool check;
    };
    std::vector<Move> plan;
    const char* first_refusal = nullptr;
    const auto refuse = [&](const char* reason) {
        if (!first_refusal) first_refusal = reason;
    };
    const auto check_refusal = [&](const Inst& in, bool on_every_path, bool effect_seen) -> const char* {
        if (!has_state) return "header has no reconstructible frame state";
        // Fixed-width element stores keep field flags, handles, lengths and
        // backing storage stable, including through aliases. The speculative
        // checks can fail only by resuming at the original header, before
        // any of the loop's steps or effects. A zero-trip loop then skips
        // its original checks, as does any path that never reaches a use.
        const bool preflight = stable_for_preflight && has_list_bounds && !safepoint && !widens &&
                               (in.op == Op::CheckNotNil || in.op == Op::Lookup || in.op == Op::CheckKind ||
                                in.op == Op::CheckInit || in.op == Op::CheckIndex);
        if (!preflight && !on_every_path) return "check does not execute on every back edge";
        if (!preflight && effect_seen) return "check follows a possible effect";
        if (in.guard != none) return "check depends on a guard";
        return nullptr;
    };
    for (const auto b : cfg.rpo) {
        if (!loop.in_body[b]) continue;
        bool effect_seen = tainted[b] != 0;
        bool on_every_path = true;
        for (const auto latch : loop.latches) on_every_path = on_every_path && cfg.dominates(b, latch);
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            if (is_effect(in)) {
                effect_seen = true;
                continue;
            }
            if (!is_outside(in.operands[0]) || !is_outside(in.operands[1]) || !is_outside(in.operands[2])) continue;
            if (in.op == Op::Gather || in.op == Op::Phi) {
                bool all = in.op == Op::Gather;
                for (std::uint32_t i = 0; i < in.count && all; i += 2) all = is_outside(f.extras[in.extra + i]);
                if (!all) continue;
            }
            const auto take_constants = [&] {
                for (const auto operand : in.operands) {
                    if (operand != none && f.insts[operand].op == Op::Const && !outside[operand] && places.block[operand] != none &&
                        loop.in_body[places.block[operand]]) {
                        outside[operand] = 1;
                        plan.push_back({operand, false});
                    }
                }
            };
            if (!is_outside(in.operands[3])) continue;
            if (hoistable_pure(in)) {
                take_constants();
                outside[id] = 1;
                plan.push_back({id, false});
            } else if (is_heap_check(in.op)) {
                // A check of values (nil, bounds) holds wherever they are the
                // same; one of an entry's liveness, kind or flags, or of a
                // global's flag, while nothing in the loop frees or
                // collects.
                const bool of_values = in.op == Op::CheckNotNil || in.op == Op::CheckIndex;
                if (const auto reason = check_refusal(in, on_every_path, effect_seen)) {
                    refuse(reason);
                } else if (!of_values && safepoint) {
                    refuse("loop may invalidate a heap check");
                } else {
                    take_constants();
                    if (in.op == Op::Lookup) outside[id] = 1;
                    if (in.op == Op::CheckKind) kind_checked[in.operands[0]] = 1;
                    plan.push_back({id, true});
                }
            } else if (is_heap_read(in.op) && in.op != Op::LoadElement) {
                if (safepoint) {
                    refuse("loop may invalidate a heap read");
                    continue;
                }
                bool moves = true;
                switch (in.op) {
                case Op::LoadField:
                case Op::LoadVersion: moves = kind_checked[in.operands[0]] != 0 && !(in.op == Op::LoadField && written(fields, in.imm)); break;
                case Op::LoadData:
                case Op::LoadStorage: moves = !widens; break;
                case Op::LoadGlobal: moves = !written(globals, in.imm); break;
                default: break;
                }
                if (moves) {
                    take_constants();
                    outside[id] = 1;
                    plan.push_back({id, false});
                    ++counts["licm.read"];
                } else {
                    refuse("heap read depends on mutable or unchecked storage");
                }
            } else if (in.op == Op::Lift && (in.flags & Checked) != 0 &&
                       !lift::float_opcode(in.lifted.op)) {
                if (const auto reason = check_refusal(in, on_every_path, effect_seen)) {
                    refuse(reason);
                } else {
                    take_constants();
                    outside[id] = 1;
                    plan.push_back({id, true});
                }
            }
        }
    }
    // A preheader only when something moves; the phis alone are not worth one.
    if (plan.empty()) {
        if (!first_refusal) first_refusal = invariant_phis.empty() ? "no movable invariant operation" : "only invariant phis";
        ++counts[std::string("licm.skip.") + first_refusal];
        return false;
    }
    const auto preheader = make_preheader(f, loop);
    Replacements repl(f.insts.size());
    for (const auto id : invariant_phis) repl.set(id, entry_operand(f, loop, id));
    repl.apply(f);
    {
        // The invariant phis are gone.
        auto& header = f.blocks[loop.header];
        std::vector<std::uint32_t> kept;
        for (const auto id : header.insts) {
            if (std::find(invariant_phis.begin(), invariant_phis.end(), id) == invariant_phis.end()) kept.push_back(id);
        }
        header.insts = std::move(kept);
    }
    std::uint32_t state = none;
    std::vector<std::uint8_t> moving(f.insts.size(), 0);
    for (const auto& m : plan) moving[m.id] = 1;
    for (auto& block : f.blocks) {
        bool any = false;
        for (const auto id : block.insts) any = any || moving[id];
        if (!any) continue;
        std::vector<std::uint32_t> kept;
        for (const auto id : block.insts) {
            if (!moving[id]) kept.push_back(id);
        }
        block.insts = std::move(kept);
    }
    auto& target = f.blocks[preheader].insts;
    for (const auto& m : plan) {
        if (m.check) {
            if (state == none) state = preheader_state(f, loop, preheader);
            f.insts[m.id].state = state;
            ++counts[is_heap_check(f.insts[m.id].op) ? std::string("licm.") + heap_kind_name(f.insts[m.id].op) : std::string("licm.check")];
        } else if (is_heap_read(f.insts[m.id].op)) {
        } else if (f.insts[m.id].op != Op::Const) {
            ++counts["licm.pure"];
        }
        target.insert(target.end() - 1, m.id);
    }
    return true;
}

} // namespace

bool hoist_invariants(Function& f, Counts& counts) {
    std::vector<std::uint32_t> headers;
    {
        const Cfg cfg(f);
        for (const auto& loop : find_loops(f, cfg)) headers.push_back(loop.header);
    }
    bool any = false;
    for (const auto header : headers) {
        const Cfg cfg(f);
        const auto loops = find_loops(f, cfg);
        const auto found = std::find_if(loops.begin(), loops.end(), [&](const Loop& l) { return l.header == header; });
        if (found == loops.end()) continue;
        any |= hoist_one(f, cfg, *found, counts);
    }
    return any;
}

} // namespace neura::nvm::tier3::detail

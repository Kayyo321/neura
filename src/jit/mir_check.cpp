#include "jit/mir_check.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura::jit::mir {
namespace {

constexpr std::uint32_t kEmpty = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t kInvalid = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kLimit = 32;

std::string text(const Location& l) {
    switch (l.kind) {
    case LocKind::Reg:
        return "register " + std::to_string(l.index);
    case LocKind::Stack:
        return "stack slot " + std::to_string(l.index);
    case LocKind::FrameSlot:
        return "frame slot " + std::to_string(l.index);
    }
    return "?";
}

std::string held(std::uint32_t v) { return v == kEmpty ? std::string("nothing") : "vreg " + std::to_string(v); }

class Checker {
public:
    Checker(const Function& f, const Allocation& a) : f_(f), a_(a) {}

    std::vector<std::string> run() {
        report_ = true;
        if (!shape()) return errors_;
        for (const auto home : f_.spill_home) {
            if (home >= 0) frames_ = std::max(frames_, static_cast<std::uint32_t>(home) + 1);
        }
        size_ = 32 + static_cast<std::size_t>(a_.stack_slots) + frames_;
        const auto n = f_.blocks.size();
        for (std::size_t b = 0; b < n; ++b) {
            for (const auto s : f_.blocks[b].succs) {
                if (f_.blocks[b].succs.size() > 1 && f_.blocks[s].preds.size() > 1) {
                    error("block " + std::to_string(b) + ": a critical edge to block " + std::to_string(s));
                }
            }
        }

        // The fixed point, without reporting.
        report_ = false;
        std::vector<State> entry(n);
        std::vector<bool> seen(n, false);
        std::set<std::size_t> work;
        for (std::size_t b = 0; b < n; ++b) {
            if (f_.blocks[b].preds.empty()) {
                entry[b].assign(size_, kEmpty);
                seen[b] = true;
                work.insert(b);
            }
        }
        while (!work.empty()) {
            const auto b = *work.begin();
            work.erase(work.begin());
            State s = entry[b];
            run_block(s, b);
            for (std::size_t k = 0; k < f_.blocks[b].succs.size(); ++k) {
                const auto succ = f_.blocks[b].succs[k];
                const State e = run_edge(s, b, k);
                if (!seen[succ]) {
                    entry[succ] = e;
                    seen[succ] = true;
                    work.insert(succ);
                } else if (meet(entry[succ], e)) {
                    work.insert(succ);
                }
            }
        }

        // Every check, on the fixed point.
        report_ = true;
        for (std::size_t b = 0; b < n; ++b) {
            if (!seen[b]) {
                error("block " + std::to_string(b) + " is not reachable from an entry");
                continue;
            }
            State s = entry[b];
            check_entry(s, b);
            run_block(s, b);
            for (std::size_t k = 0; k < f_.blocks[b].succs.size(); ++k) run_edge(s, b, k);
        }
        return errors_;
    }

private:
    using State = std::vector<std::uint32_t>;

    const Function& f_;
    const Allocation& a_;
    std::vector<std::string> errors_;
    bool report_ = true;
    std::uint32_t frames_ = 0;
    std::size_t size_ = 0;

    bool reporting() const { return report_ && errors_.size() < kLimit; }

    void error(const std::string& message) {
        if (reporting()) errors_.push_back(message);
    }

    // Reports the message `make` builds, building it only when it is reported.
    template <typename Make>
    void fail(Make&& make) {
        if (reporting()) errors_.push_back(make());
    }

    bool shape() {
        const auto n = f_.blocks.size();
        if (a_.blocks.size() != n) {
            error("the allocation has " + std::to_string(a_.blocks.size()) + " blocks, the function " +
                  std::to_string(n));
            return false;
        }
        if (f_.classes.size() != f_.vreg_count || f_.spill_home.size() != f_.vreg_count) {
            error("the function has no class or spill home for some vreg");
            return false;
        }
        bool ok = true;
        for (std::size_t b = 0; b < n; ++b) {
            const auto& block = f_.blocks[b];
            const auto& alloc = a_.blocks[b];
            const std::string where = "block " + std::to_string(b) + ": ";
            if (alloc.insts.size() != block.insts.size() || alloc.edges.size() != block.succs.size()) {
                error(where + "instruction or edge count differs");
                ok = false;
                continue;
            }
            for (const auto s : block.succs) {
                if (s >= n) {
                    error(where + "a successor out of range");
                    ok = false;
                }
            }
            for (const auto& phi : block.phis) {
                if (phi.inputs.size() != block.preds.size() || phi.def.id >= f_.vreg_count) {
                    error(where + "a malformed phi");
                    ok = false;
                }
                for (const auto& input : phi.inputs) {
                    if (input.id >= f_.vreg_count) {
                        error(where + "a phi input out of range");
                        ok = false;
                    }
                }
            }
            for (std::size_t j = 0; j < block.insts.size(); ++j) {
                const auto& inst = block.insts[j];
                const auto& ia = alloc.insts[j];
                if (ia.uses.size() != inst.uses.size() || ia.defs.size() != inst.defs.size() ||
                    ia.state_uses.size() != inst.state_uses.size()) {
                    error(at(b, j) + "operand count differs");
                    ok = false;
                }
                for (const auto* ops : {&inst.uses, &inst.defs, &inst.state_uses}) {
                    for (const auto& op : *ops) {
                        if (op.vreg.id >= f_.vreg_count || op.vreg.cls != f_.classes[op.vreg.id]) {
                            error(at(b, j) + "an operand with a bad vreg");
                            ok = false;
                        }
                    }
                }
            }
        }
        for (std::size_t b = 0; b < n && ok; ++b) {
            for (const auto s : f_.blocks[b].succs) {
                const auto& preds = f_.blocks[s].preds;
                if (std::count(preds.begin(), preds.end(), b) != 1) {
                    error("block " + std::to_string(b) + ": an edge its successor does not list once");
                    ok = false;
                }
            }
        }
        return ok;
    }

    static std::string at(std::size_t b, std::size_t j) {
        return "block " + std::to_string(b) + ", instruction " + std::to_string(j) + ": ";
    }

    bool allocatable(RegClass cls, std::uint32_t reg) const {
        const auto mask = cls == RegClass::Gpr ? f_.allocatable_gpr : f_.allocatable_xmm;
        return reg < 16 && ((mask >> reg) & 1U) != 0;
    }

    std::uint32_t scratch(RegClass cls) const { return cls == RegClass::Gpr ? scratch_gpr : scratch_xmm; }

    std::size_t key(const Location& l, RegClass cls) const {
        switch (l.kind) {
        case LocKind::Reg:
            if (l.index >= 16) return kInvalid;
            return (cls == RegClass::Gpr ? 0 : 16) + l.index;
        case LocKind::Stack:
            if (l.index >= a_.stack_slots) return kInvalid;
            return 32 + static_cast<std::size_t>(l.index);
        case LocKind::FrameSlot:
            if (l.index >= frames_) return kInvalid;
            return 32 + static_cast<std::size_t>(a_.stack_slots) + l.index;
        }
        return kInvalid;
    }

    void kill_scratch(State& s) const {
        s[scratch_gpr] = kEmpty;
        s[16 + scratch_xmm] = kEmpty;
    }

    // Empties every location that holds vreg v. A def or a phi makes a new
    // instance of its vreg each time it runs (in a loop, once per pass), and
    // a copy of the previous instance, say a spill from the last pass, no
    // longer holds the vreg.
    static void forget(State& s, std::uint32_t v) {
        for (auto& held_vreg : s) {
            if (held_vreg == v) held_vreg = kEmpty;
        }
    }

    // Runs a move list. On an edge, `frame_writes` collects the frame slots it
    // writes, whose homes are checked once the phis hold their values.
    template <typename Where>
    void apply(State& s, const std::vector<Move>& moves, Where&& where, std::vector<std::size_t>* frame_writes) {
        kill_scratch(s);
        for (std::size_t i = 0; i < moves.size(); ++i) {
            const auto& m = moves[i];
            const auto what = [&] {
                return where() + "move " + std::to_string(i) + " (" + text(m.from) + " to " + text(m.to) + "): ";
            };
            const auto from = key(m.from, m.cls);
            const auto to = key(m.to, m.cls);
            if (from == kInvalid || to == kInvalid) {
                fail([&] { return what() + "a location out of range"; });
                if (to != kInvalid) s[to] = kEmpty;
                continue;
            }
            if (m.from.kind != LocKind::Reg && m.to.kind != LocKind::Reg) {
                fail([&] { return what() + "between two memory locations"; });
            }
            for (const auto* l : {&m.from, &m.to}) {
                if (l->kind == LocKind::Reg && !allocatable(m.cls, l->index) && l->index != scratch(m.cls)) {
                    fail([&] { return what() + "a register that is neither allocatable nor the scratch"; });
                }
            }
            const auto v = s[from];
            if (v == kEmpty) {
                fail([&] { return what() + "reads a location that holds nothing"; });
            } else if (f_.classes[v] != m.cls) {
                fail([&] { return what() + "moves vreg " + std::to_string(v) + " as the other class"; });
            } else if (m.to.kind == LocKind::FrameSlot) {
                if (frame_writes != nullptr) {
                    frame_writes->push_back(to);
                } else if (f_.spill_home[v] != static_cast<std::int32_t>(m.to.index)) {
                    fail([&] {
                        return what() + "writes vreg " + std::to_string(v) + " to a frame slot that is not its home";
                    });
                }
            }
            s[to] = v;
        }
        kill_scratch(s);
    }

    template <typename What>
    void check_register(const Location& l, RegClass cls, What&& what) {
        if (l.kind != LocKind::Reg) {
            fail([&] { return what() + "is in " + text(l) + ", not a register"; });
        } else if (!allocatable(cls, l.index)) {
            fail([&] { return what() + "is in a register that is not allocatable"; });
        }
    }

    void run_block(State& s, std::size_t b) {
        const auto& block = f_.blocks[b];
        for (std::size_t j = 0; j < block.insts.size(); ++j) run_inst(s, b, j);
    }

    void run_inst(State& s, std::size_t b, std::size_t j) {
        const auto& inst = f_.blocks[b].insts[j];
        const auto& ia = a_.blocks[b].insts[j];
        const auto where = [&] { return at(b, j); };
        apply(s, ia.before, [&] { return where() + "before, "; }, nullptr);

        for (std::size_t k = 0; k < inst.uses.size(); ++k) {
            const auto& op = inst.uses[k];
            const auto& l = ia.uses[k];
            const auto what = [&] {
                return where() + "use " + std::to_string(k) + " (vreg " + std::to_string(op.vreg.id) + ") ";
            };
            const auto kk = key(l, op.vreg.cls);
            if (kk == kInvalid) {
                fail([&] { return what() + "is in a location out of range"; });
                continue;
            }
            if (s[kk] != op.vreg.id) fail([&] { return what() + "reads " + text(l) + ", which holds " + held(s[kk]); });
            switch (op.c) {
            case Constraint::Register:
                check_register(l, op.vreg.cls, what);
                break;
            case Constraint::Fixed:
                check_register(l, op.vreg.cls, what);
                if (l.kind == LocKind::Reg && l.index != op.fixed) {
                    fail([&] { return what() + "is not in its fixed register"; });
                }
                break;
            case Constraint::Any:
                if (l.kind == LocKind::Reg) check_register(l, op.vreg.cls, what);
                break;
            case Constraint::SameAsUse:
                fail([&] { return what() + "has the def-only constraint SameAsUse"; });
                break;
            }
        }

        // Temps.
        for (const auto cls : {RegClass::Gpr, RegClass::Xmm}) {
            const auto& temps = cls == RegClass::Gpr ? ia.temps_gpr : ia.temps_xmm;
            const auto wanted = cls == RegClass::Gpr ? inst.temps_gpr : inst.temps_xmm;
            const auto clobbers = cls == RegClass::Gpr ? inst.clobbers_gpr : inst.clobbers_xmm;
            if (temps.size() != wanted) fail([&] { return where() + "the wrong number of temps"; });
            for (std::size_t t = 0; t < temps.size(); ++t) {
                const auto r = temps[t];
                const auto what = [&] { return where() + "temp register " + std::to_string(r) + " "; };
                if (!allocatable(cls, r)) fail([&] { return what() + "is not allocatable"; });
                if (r < 16 && ((clobbers >> r) & 1U) != 0) {
                    fail([&] { return what() + "is clobbered by its instruction"; });
                }
                if (std::count(temps.begin(), temps.end(), r) != 1) fail([&] { return what() + "is given twice"; });
                for (std::size_t k = 0; k < inst.uses.size(); ++k) {
                    if (inst.uses[k].vreg.cls == cls && ia.uses[k] == Location{LocKind::Reg, r}) {
                        fail([&] { return what() + "is also a use"; });
                    }
                }
                for (std::size_t k = 0; k < inst.defs.size(); ++k) {
                    if (inst.defs[k].vreg.cls == cls && ia.defs[k] == Location{LocKind::Reg, r}) {
                        fail([&] { return what() + "is also a def"; });
                    }
                }
            }
        }

        // The instruction's effects: clobbers, temps, defs.
        for (std::uint32_t r = 0; r < 16; ++r) {
            if (((inst.clobbers_gpr >> r) & 1U) != 0) s[r] = kEmpty;
            if (((inst.clobbers_xmm >> r) & 1U) != 0) s[16 + r] = kEmpty;
        }
        for (const auto r : ia.temps_gpr) {
            if (r < 16) s[r] = kEmpty;
        }
        for (const auto r : ia.temps_xmm) {
            if (r < 16) s[16 + static_cast<std::size_t>(r)] = kEmpty;
        }
        kill_scratch(s);
        std::vector<std::size_t> written;
        std::vector<std::pair<std::size_t, std::uint32_t>> instances;
        for (std::size_t k = 0; k < inst.defs.size(); ++k) {
            const auto& op = inst.defs[k];
            const auto& l = ia.defs[k];
            const auto cls = op.vreg.cls;
            const auto what = [&] {
                return where() + "def " + std::to_string(k) + " (vreg " + std::to_string(op.vreg.id) + ") ";
            };
            const auto kk = key(l, cls);
            if (kk == kInvalid) {
                fail([&] { return what() + "is in a location out of range"; });
                continue;
            }
            if (std::find(written.begin(), written.end(), kk) != written.end()) {
                fail([&] { return what() + "shares its location with another def"; });
            }
            written.push_back(kk);
            switch (op.c) {
            case Constraint::Register:
                check_register(l, cls, what);
                break;
            case Constraint::Fixed:
                check_register(l, cls, what);
                if (l.kind == LocKind::Reg && l.index != op.fixed) {
                    fail([&] { return what() + "is not in its fixed register"; });
                }
                break;
            case Constraint::Any:
                if (l.kind == LocKind::Reg) check_register(l, cls, what);
                break;
            case Constraint::SameAsUse:
                check_register(l, cls, what);
                if (op.same_as >= inst.uses.size()) {
                    fail([&] { return what() + "names no use"; });
                } else if (inst.uses[op.same_as].vreg.cls != cls || !(ia.uses[op.same_as] == l)) {
                    fail([&] { return what() + "is not in the register of use " + std::to_string(op.same_as); });
                }
                break;
            }
            if (l.kind == LocKind::FrameSlot && f_.spill_home[op.vreg.id] != static_cast<std::int32_t>(l.index)) {
                fail([&] { return what() + "is written to a frame slot that is not its home"; });
            }
            instances.push_back({kk, op.vreg.id});
        }
        for (const auto& instance : instances) forget(s, instance.second);
        for (const auto& [kk, v] : instances) s[kk] = v;

        for (std::size_t k = 0; k < inst.state_uses.size(); ++k) {
            const auto& op = inst.state_uses[k];
            const auto& l = ia.state_uses[k];
            const auto what = [&] {
                return where() + "state use " + std::to_string(k) + " (vreg " + std::to_string(op.vreg.id) + ") ";
            };
            const auto kk = key(l, op.vreg.cls);
            if (kk == kInvalid) {
                fail([&] { return what() + "is in a location out of range"; });
                continue;
            }
            if (s[kk] != op.vreg.id) {
                fail([&] {
                    return what() + "reads " + text(l) + ", which holds " + held(s[kk]) + " after the instruction";
                });
            }
            if (l.kind == LocKind::Reg) check_register(l, op.vreg.cls, what);
        }
        apply(s, ia.after, [&] { return where() + "after, "; }, nullptr);
    }

    // The entry location the allocation claims for vreg v at block b, or null.
    const Location* claim(std::size_t b, std::uint32_t v) const {
        const auto& entry = a_.blocks[b].entry;
        const auto it = std::lower_bound(entry.begin(), entry.end(), v, [](const auto& e, std::uint32_t x) {
            return e.first < x;
        });
        return it != entry.end() && it->first == v ? &it->second : nullptr;
    }

    State run_edge(const State& out, std::size_t b, std::size_t k) {
        State s = out;
        const auto succ = f_.blocks[b].succs[k];
        const auto& target = f_.blocks[succ];
        const auto pred =
            static_cast<std::size_t>(std::find(target.preds.begin(), target.preds.end(), b) - target.preds.begin());
        const auto where = [&] { return "edge " + std::to_string(b) + " -> " + std::to_string(succ) + ": "; };
        std::vector<std::size_t> frame_writes;
        apply(s, a_.blocks[b].edges[k], where, &frame_writes);
        std::vector<std::pair<std::size_t, std::uint32_t>> installs;
        for (const auto& phi : target.phis) {
            const auto* l = claim(succ, phi.def.id);
            if (l == nullptr) continue;
            const auto kk = key(*l, phi.def.cls);
            const auto what = [&] { return where() + "phi vreg " + std::to_string(phi.def.id) + " "; };
            if (kk == kInvalid) {
                fail([&] { return what() + "has a location out of range"; });
                continue;
            }
            const auto input = phi.inputs[pred].id;
            if (s[kk] != input) {
                fail([&] {
                    return what() + "expects vreg " + std::to_string(input) + " in " + text(*l) + ", which holds " +
                           held(s[kk]);
                });
            }
            for (const auto& install : installs) {
                if (install.first == kk) fail([&] { return what() + "shares its location with another phi"; });
            }
            installs.push_back({kk, phi.def.id});
            if (l->kind == LocKind::Reg) check_register(*l, phi.def.cls, what);
            if (l->kind == LocKind::FrameSlot) frame_writes.push_back(kk);
        }
        // The edge makes a new instance of every phi of the successor, after
        // the inputs are read.
        for (const auto& phi : target.phis) forget(s, phi.def.id);
        for (const auto& [kk, v] : installs) s[kk] = v;
        for (const auto kk : frame_writes) {
            const auto v = s[kk];
            const auto slot = kk - 32 - static_cast<std::size_t>(a_.stack_slots);
            if (v != kEmpty && f_.spill_home[v] != static_cast<std::int32_t>(slot)) {
                fail([&] {
                    return where() + "frame slot " + std::to_string(slot) + " holds vreg " + std::to_string(v) +
                           ", whose home it is not";
                });
            }
        }
        return s;
    }

    void check_entry(const State& s, std::size_t b) {
        const auto& entry = a_.blocks[b].entry;
        const auto& phis = f_.blocks[b].phis;
        for (std::size_t i = 0; i < entry.size(); ++i) {
            const auto& [v, l] = entry[i];
            const std::string what = "block " + std::to_string(b) + " entry: vreg " + std::to_string(v) + " ";
            if (i > 0 && entry[i - 1].first >= v) error(what + "is not in increasing order");
            if (v >= f_.vreg_count) {
                error(what + "is out of range");
                continue;
            }
            const bool is_phi = std::any_of(phis.begin(), phis.end(), [&](const Phi& p) { return p.def.id == v; });
            if (is_phi) continue;
            const auto kk = key(l, f_.classes[v]);
            if (kk == kInvalid) {
                error(what + "is claimed in a location out of range");
            } else if (s[kk] != v) {
                error(what + "is claimed in " + text(l) + ", which holds " + held(s[kk]));
            }
        }
    }

    // Narrows `into` to what it shares with `other`; whether anything changed.
    static bool meet(State& into, const State& other) {
        bool changed = false;
        for (std::size_t i = 0; i < into.size(); ++i) {
            if (into[i] != kEmpty && into[i] != other[i]) {
                into[i] = kEmpty;
                changed = true;
            }
        }
        return changed;
    }
};

}  // namespace

std::vector<std::string> check(const Function& function, const Allocation& allocation) {
    return Checker(function, allocation).run();
}

}  // namespace neura::jit::mir

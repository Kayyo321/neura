// The register allocator of tier 3 (src/jit/regalloc.{hpp,cpp}) and its
// checker (src/jit/mir_check.{hpp,cpp}). The test checks:
//
// - hand-built functions: a loop with more live values than registers, a
//   value live through a loop that does not use it, phi swaps and a rotation
//   that need the scratch registers, fixed registers
//   (a shift count, a division), calls with values live across them, a
//   SameAsUse def whose use is live after it, state uses that keep a value
//   alive, temps, both classes together, and functions the allocator must
//   reject;
// - the move sequencer, against the parallel meaning of its input;
// - random functions (reducible control flow with nested loops, several
//   entries, cold blocks, phis, calls with clobbers, fixed registers, state
//   uses, temps, both classes, 2 to 40 variables), each allocated twice for
//   determinism and checked by the checker;
// - the checker on allocations made wrong on purpose, which it must reject,
//   among them copies a loop's earlier pass left behind.
//
// It is a program of its own (no framework): it prints the failures and exits
// with their number. Options: --seeds N (random functions, 20000 by default),
// --first S (the first seed), --timing (the allocator's time on the largest
// random function and on a function of about 20000 instructions), --dump S
// (print the random function of seed S and its allocation, and stop).
#include "jit/mir.hpp"
#include "jit/mir_check.hpp"
#include "jit/regalloc.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace mir = neura::jit::mir;
using mir::Allocation;
using mir::AllocationError;
using mir::Constraint;
using mir::Function;
using mir::Inst;
using mir::Location;
using mir::LocKind;
using mir::Move;
using mir::Operand;
using mir::RegClass;
using mir::VReg;

std::size_t checks = 0;
std::size_t failures = 0;

void expect(bool ok, const std::string& what) {
    ++checks;
    if (ok) return;
    if (++failures <= 40) std::fprintf(stderr, "FAIL: %s\n", what.c_str());
}

constexpr std::uint8_t rax = 0, rcx = 1, rdx = 2, rbx = 3, rbp = 5, rsi = 6, rdi = 7, r8 = 8, r9 = 9, r10 = 10,
                       r11 = 11, r12 = 12, r14 = 14;
constexpr std::uint64_t bit(unsigned r) { return std::uint64_t{1} << r; }
constexpr std::uint64_t kDesignGpr = bit(rax) | bit(rcx) | bit(rdx) | bit(rbx) | bit(rbp) | bit(rsi) | bit(rdi) |
                                     bit(r8) | bit(r9) | bit(r10);
constexpr std::uint64_t kDesignXmm = 0x7FFF;
constexpr std::uint64_t kCallerSavedGpr = bit(rax) | bit(rcx) | bit(rdx) | bit(rsi) | bit(rdi) | bit(r8) | bit(r9) |
                                          bit(r10) | bit(r11);
constexpr std::uint64_t kEveryRegister = 0xFFFF;

Operand reg(VReg v) { return {v, Constraint::Register, 0, 0}; }
Operand any(VReg v) { return {v, Constraint::Any, 0, 0}; }
Operand fixed(VReg v, std::uint8_t r) { return {v, Constraint::Fixed, r, 0}; }
Operand same(VReg v, std::uint8_t use) { return {v, Constraint::SameAsUse, 0, use}; }

Location in_reg(std::uint32_t r) { return {LocKind::Reg, r}; }
Location in_stack(std::uint32_t i) { return {LocKind::Stack, i}; }
Location in_frame(std::uint32_t i) { return {LocKind::FrameSlot, i}; }

bool is_reg(const Location& l, std::uint32_t r) { return l.kind == LocKind::Reg && l.index == r; }

struct Builder {
    Function f;

    explicit Builder(std::uint64_t gpr = kDesignGpr, std::uint64_t xmm = kDesignXmm) {
        f.allocatable_gpr = gpr;
        f.allocatable_xmm = xmm;
    }
    VReg value(RegClass cls, std::int32_t home = -1) {
        f.classes.push_back(cls);
        f.spill_home.push_back(home);
        return {f.vreg_count++, cls};
    }
    VReg gpr(std::int32_t home = -1) { return value(RegClass::Gpr, home); }
    VReg xmm(std::int32_t home = -1) { return value(RegClass::Xmm, home); }
    std::uint32_t block(std::uint32_t depth = 0) {
        f.blocks.emplace_back();
        f.blocks.back().loop_depth = depth;
        return static_cast<std::uint32_t>(f.blocks.size() - 1);
    }
    void edge(std::uint32_t a, std::uint32_t b) {
        f.blocks[a].succs.push_back(b);
        f.blocks[b].preds.push_back(a);
    }
    Inst& inst(std::uint32_t b, std::vector<Operand> defs, std::vector<Operand> uses, std::vector<Operand> state = {}) {
        Inst i;
        i.defs = std::move(defs);
        i.uses = std::move(uses);
        i.state_uses = std::move(state);
        f.blocks[b].insts.push_back(std::move(i));
        return f.blocks[b].insts.back();
    }
    void phi(std::uint32_t b, VReg def, std::vector<VReg> inputs) {
        f.blocks[b].phis.push_back({def, std::move(inputs)});
    }
};

std::string first_error(const std::vector<std::string>& errors) {
    return errors.empty() ? std::string() : errors.front();
}

// Allocates `f`, checks the result with the checker and against a second
// allocation, and returns it.
Allocation verified(const Function& f, const std::string& name) {
    Allocation a;
    try {
        a = mir::allocate(f);
    } catch (const AllocationError& e) {
        expect(false, name + ": rejected: " + e.what());
        return a;
    }
    const auto errors = mir::check(f, a);
    expect(errors.empty(), name + ": " + first_error(errors));
    expect(mir::allocate(f) == a, name + ": a second allocation differs");
    return a;
}

void expect_rejected(const Function& f, const std::string& name) {
    try {
        (void)mir::allocate(f);
        expect(false, name + ": accepted");
    } catch (const AllocationError&) {
        expect(true, name);
    }
}

bool moves_touch(const std::vector<Move>& moves, std::uint32_t reg, RegClass cls) {
    return std::any_of(moves.begin(), moves.end(), [&](const Move& m) {
        return m.cls == cls && (is_reg(m.from, reg) || is_reg(m.to, reg));
    });
}

// Every location the allocation names, operands and moves and entries.
template <typename F>
void for_each_location(const Allocation& a, F&& f) {
    for (const auto& block : a.blocks) {
        for (const auto& [v, l] : block.entry) f(l);
        for (const auto& inst : block.insts) {
            for (const auto* list : {&inst.uses, &inst.defs, &inst.state_uses}) {
                for (const auto& l : *list) f(l);
            }
            for (const auto* moves : {&inst.before, &inst.after}) {
                for (const auto& m : *moves) {
                    f(m.from);
                    f(m.to);
                }
            }
        }
        for (const auto& moves : block.edges) {
            for (const auto& m : moves) {
                f(m.from);
                f(m.to);
            }
        }
    }
}

// ---- hand-built functions --------------------------------------------------

// A loop whose body keeps 30 integer and 20 float values live at once, with
// 10 integer and 15 float registers: values spill, slot values to their frame
// slots and the others to native stack slots.
void pressure_test() {
    Builder b;
    const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
    b.edge(entry, header);
    b.edge(header, body);
    b.edge(header, exit);
    b.edge(body, header);
    std::vector<VReg> initial, current, next;
    const std::size_t gprs = 30, xmms = 20;
    for (std::size_t i = 0; i < gprs + xmms; ++i) {
        const auto cls = i < gprs ? RegClass::Gpr : RegClass::Xmm;
        const std::int32_t home = i % 5 == 4 ? -1 : static_cast<std::int32_t>(i);
        initial.push_back(b.value(cls, home));
        current.push_back(b.value(cls, home));
        next.push_back(b.value(cls, home));
        b.inst(entry, {any(initial.back())}, {});
    }
    for (std::size_t i = 0; i < current.size(); ++i) b.phi(header, current[i], {initial[i], next[i]});
    b.inst(header, {}, {reg(current[0]), reg(current[gprs])});
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto partner = i < gprs ? current[(i + 1) % gprs] : current[gprs + (i + 1 - gprs) % xmms];
        const auto c = i % 3 == 0 ? same(next[i], 0) : reg(next[i]);
        b.inst(body, {c}, {reg(current[i]), reg(partner)});
    }
    std::vector<Operand> all;
    for (const auto v : current) all.push_back(any(v));
    b.inst(exit, {}, all);
    const auto a = verified(b.f, "pressure");
    std::size_t frame = 0, stack = 0;
    for_each_location(a, [&](const Location& l) {
        frame += l.kind == LocKind::FrameSlot ? 1 : 0;
        stack += l.kind == LocKind::Stack ? 1 : 0;
    });
    expect(frame > 0, "pressure: slot values spill to their frame slots");
    expect(stack > 0, "pressure: other values spill to native stack slots");
    expect(a.stack_slots > 0, "pressure: native stack slots are counted");
}

// A value defined before a loop and used only after it is live through
// every block of the loop, though no block of the loop uses it: the body,
// which needs every register, must not take its register.
void live_through_loop_test() {
    Builder b;
    const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
    b.edge(entry, header);
    b.edge(header, body);
    b.edge(header, exit);
    b.edge(body, header);
    const auto v = b.gpr(), x0 = b.gpr(), x = b.gpr(), x1 = b.gpr();
    b.inst(entry, {reg(v)}, {});
    b.inst(entry, {reg(x0)}, {});
    b.phi(header, x, {x0, x1});
    b.inst(header, {}, {reg(x)});
    std::vector<Operand> all;
    for (int i = 0; i < 10; ++i) {
        const auto t = b.gpr();
        b.inst(body, {reg(t)}, {});
        all.push_back(reg(t));
    }
    b.inst(body, {}, all);
    b.inst(body, {reg(x1)}, {reg(x)});
    b.inst(exit, {}, {reg(v)});
    verified(b.f, "live through a loop");
}

// (a, b) = (b, a) around a loop, and (a, b, c) = (b, c, a): the back edge's
// moves form a cycle that goes through the scratch register.
void cycle_test(RegClass cls, std::size_t length) {
    Builder b;
    const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
    b.edge(entry, header);
    b.edge(header, body);
    b.edge(header, exit);
    b.edge(body, header);
    std::vector<VReg> initial, current;
    for (std::size_t i = 0; i < length; ++i) {
        initial.push_back(b.value(cls));
        current.push_back(b.value(cls));
        b.inst(entry, {reg(initial.back())}, {});
    }
    for (std::size_t i = 0; i < length; ++i) b.phi(header, current[i], {initial[i], current[(i + 1) % length]});
    std::vector<Operand> uses;
    for (const auto v : current) uses.push_back(reg(v));
    b.inst(header, {}, uses);
    b.inst(body, {}, uses);
    b.inst(exit, {}, uses);
    const std::string name = std::string(cls == RegClass::Gpr ? "gpr" : "xmm") + " cycle of " + std::to_string(length);
    const auto a = verified(b.f, name);
    if (a.blocks.size() != 4) return;
    const auto& back = a.blocks[body].edges[0];
    const auto scratch = cls == RegClass::Gpr ? mir::scratch_gpr : mir::scratch_xmm;
    expect(moves_touch(back, scratch, cls), name + ": the back edge goes through the scratch register");
    expect(back.size() == length + 1, name + ": a cycle of n moves takes n + 1");
}

// A shift count in rcx with a SameAsUse def, and a division with its
// dividend and results in rax and rdx and a divisor elsewhere.
void fixed_test() {
    Builder b;
    const auto entry = b.block();
    const auto x = b.gpr(), n = b.gpr(), keep = b.gpr(), shifted = b.gpr(), high = b.gpr(), divisor = b.gpr();
    const auto quotient = b.gpr(), remainder = b.gpr();
    b.inst(entry, {reg(x)}, {});
    b.inst(entry, {reg(n)}, {});
    b.inst(entry, {reg(keep)}, {});
    b.inst(entry, {reg(divisor)}, {});
    b.inst(entry, {same(shifted, 0)}, {reg(x), fixed(n, rcx)});
    b.inst(entry, {reg(high)}, {});
    b.inst(entry, {fixed(quotient, rax), fixed(remainder, rdx)}, {fixed(shifted, rax), fixed(high, rdx), reg(divisor)});
    b.inst(entry, {}, {reg(quotient), reg(remainder), reg(keep), reg(n), reg(divisor)});
    const auto a = verified(b.f, "fixed");
    if (a.blocks.empty()) return;
    const auto& shift = a.blocks[0].insts[4];
    expect(is_reg(shift.uses[1], rcx), "fixed: the shift count is in rcx");
    expect(shift.defs[0] == shift.uses[0] && shift.defs[0].kind == LocKind::Reg,
           "fixed: the shift's def is in its use's register");
    const auto& div = a.blocks[0].insts[6];
    expect(is_reg(div.uses[0], rax) && is_reg(div.uses[1], rdx), "fixed: the dividend is in rax and rdx");
    expect(is_reg(div.defs[0], rax) && is_reg(div.defs[1], rdx), "fixed: the results are in rax and rdx");
    expect(div.uses[2].kind == LocKind::Reg && !is_reg(div.uses[2], rax) && !is_reg(div.uses[2], rdx),
           "fixed: the divisor is in another register");
}

// A helper call in a loop: values live across it stay in rbx, rbp or r14 or go
// to memory; arguments go to rdi and rsi, the result comes from rax. Then a
// native call, which clobbers every register: everything live across it is in
// memory.
void call_test() {
    {
        Builder b(kDesignGpr | bit(r14));
        const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
        b.edge(entry, header);
        b.edge(header, body);
        b.edge(header, exit);
        b.edge(body, header);
        std::vector<VReg> initial, current, next;
        for (std::size_t i = 0; i < 10; ++i) {
            const auto cls = i < 6 ? RegClass::Gpr : RegClass::Xmm;
            initial.push_back(b.value(cls, static_cast<std::int32_t>(i)));
            current.push_back(b.value(cls, static_cast<std::int32_t>(i)));
            next.push_back(b.value(cls, static_cast<std::int32_t>(i)));
            b.inst(entry, {reg(initial.back())}, {});
        }
        for (std::size_t i = 0; i < current.size(); ++i) b.phi(header, current[i], {initial[i], next[i]});
        b.inst(header, {}, {reg(current[0])});
        const auto result = b.gpr();
        auto& call = b.inst(body, {fixed(result, rax)}, {fixed(current[0], rdi), fixed(current[1], rsi)});
        call.clobbers_gpr = kCallerSavedGpr;
        call.clobbers_xmm = kEveryRegister;
        call.is_call = true;
        for (std::size_t i = 0; i < current.size(); ++i) {
            if (i == 0) {
                b.inst(body, {same(next[i], 0)}, {reg(current[i]), reg(result)});
            } else {
                b.inst(body, {reg(next[i])}, {reg(current[i])});
            }
        }
        std::vector<Operand> uses;
        for (const auto v : current) uses.push_back(any(v));
        b.inst(exit, {}, uses);
        verified(b.f, "helper call in a loop");
    }
    {
        // One value live across a helper call takes a register the call keeps.
        Builder b;
        const auto entry = b.block();
        const auto v = b.gpr(), r = b.gpr();
        b.inst(entry, {reg(v)}, {});
        auto& call = b.inst(entry, {fixed(r, rax)}, {});
        call.clobbers_gpr = kCallerSavedGpr;
        call.is_call = true;
        b.inst(entry, {}, {reg(v), reg(r)});
        const auto a = verified(b.f, "one value across a call");
        if (!a.blocks.empty()) {
            const auto& def = a.blocks[0].insts[0].defs[0];
            expect(is_reg(def, rbx) || is_reg(def, rbp), "one value across a call: it is in rbx or rbp");
        }
    }
    {
        Builder b;
        const auto entry = b.block();
        const auto v = b.gpr(3), w = b.xmm(), r = b.gpr();
        b.inst(entry, {reg(v)}, {});
        b.inst(entry, {reg(w)}, {});
        auto& call = b.inst(entry, {fixed(r, rax)}, {}, {any(v), any(w)});
        call.clobbers_gpr = kEveryRegister;
        call.clobbers_xmm = kEveryRegister;
        call.is_call = true;
        b.inst(entry, {}, {reg(v), reg(w), reg(r)});
        const auto a = verified(b.f, "native call");
        if (!a.blocks.empty()) {
            const auto& at_call = a.blocks[0].insts[2];
            expect(at_call.state_uses[0].kind != LocKind::Reg && at_call.state_uses[1].kind != LocKind::Reg,
                   "native call: the values live across it are in memory");
            expect(at_call.state_uses[0] == in_frame(3), "native call: a slot value is in its frame slot");
        }
    }
}

// d = a + b with d SameAsUse(a): when a is live after, a copy of a goes to d's
// register first; when it is not, d takes a's register and nothing moves.
void same_as_use_test() {
    for (const bool live_after : {true, false}) {
        Builder b;
        const auto entry = b.block();
        const auto x = b.gpr(), y = b.gpr(), d = b.gpr();
        b.inst(entry, {reg(x)}, {});
        b.inst(entry, {reg(y)}, {});
        b.inst(entry, {same(d, 0)}, {reg(x), reg(y)});
        if (live_after) {
            b.inst(entry, {}, {reg(x), reg(d)});
        } else {
            b.inst(entry, {}, {reg(d), reg(y)});
        }
        const std::string name = live_after ? "SameAsUse, use live after" : "SameAsUse, use dead after";
        const auto a = verified(b.f, name);
        if (a.blocks.empty()) continue;
        const auto& add = a.blocks[0].insts[2];
        expect(add.defs[0] == add.uses[0], name + ": the def is in the use's register");
        if (live_after) {
            expect(add.before.size() == 1, name + ": one copy before the instruction");
            expect(!(a.blocks[0].insts[3].uses[0] == add.defs[0]), name + ": the use keeps its own register");
        } else {
            expect(add.before.empty(), name + ": no copy");
            expect(a.blocks[0].insts[0].defs[0] == add.defs[0], name + ": the def takes the use's register");
        }
    }
}

// A value whose last ordinary use is early stays live until a later
// instruction's frame state names it, in a location the instruction keeps.
void state_use_test() {
    Builder b;
    const auto entry = b.block();
    const auto v = b.gpr(), w = b.gpr(), x = b.gpr(), r = b.gpr();
    b.inst(entry, {reg(v)}, {});
    b.inst(entry, {reg(w)}, {});
    b.inst(entry, {}, {reg(v), reg(w)});
    b.inst(entry, {reg(x)}, {});
    auto& call = b.inst(entry, {fixed(r, rax)}, {fixed(x, rdi)}, {any(v)});
    call.clobbers_gpr = kCallerSavedGpr;
    call.is_call = true;
    b.inst(entry, {}, {reg(r)});
    const auto a = verified(b.f, "state use");
    if (a.blocks.empty()) return;
    const auto& at_call = a.blocks[0].insts[4];
    const auto& l = at_call.state_uses[0];
    expect(l.kind != LocKind::Reg || ((kCallerSavedGpr >> l.index) & 1U) == 0,
           "state use: not in a clobbered register");
}

// Temps: distinct from the uses and defs, and never a register the
// instruction clobbers.
void temps_test() {
    Builder b;
    const auto entry = b.block();
    const auto x = b.gpr(), y = b.gpr(), z = b.gpr(), d = b.gpr(), f = b.xmm();
    b.inst(entry, {reg(x)}, {});
    b.inst(entry, {reg(y)}, {});
    b.inst(entry, {reg(z)}, {});
    b.inst(entry, {reg(f)}, {});
    auto& op = b.inst(entry, {reg(d)}, {reg(x), reg(y), reg(f)});
    op.temps_gpr = 2;
    op.temps_xmm = 1;
    auto& call = b.inst(entry, {}, {fixed(x, rdi)});
    call.temps_gpr = 1;
    call.clobbers_gpr = kCallerSavedGpr;
    call.clobbers_xmm = kEveryRegister;
    b.inst(entry, {}, {reg(x), reg(y), reg(z), reg(d)});
    const auto a = verified(b.f, "temps");
    if (a.blocks.empty()) return;
    const auto& ia = a.blocks[0].insts[4];
    expect(ia.temps_gpr.size() == 2 && ia.temps_xmm.size() == 1, "temps: as many as asked");
    for (const auto t : ia.temps_gpr) {
        expect(!is_reg(ia.uses[0], t) && !is_reg(ia.uses[1], t) && !is_reg(ia.defs[0], t), "temps: not a use or def");
    }
    expect(!is_reg(ia.uses[2], ia.temps_xmm.empty() ? 99 : ia.temps_xmm[0]), "temps: the XMM temp is not the XMM use");
    const auto& ic = a.blocks[0].insts[5];
    expect(ic.temps_gpr.size() == 1 && ((kCallerSavedGpr >> ic.temps_gpr[0]) & 1U) == 0,
           "temps: a call's temp survives it");
}

// Both classes in one function: conversions, a float division whose result
// is fixed to xmm0, a call that clobbers every XMM register.
void mixed_test() {
    Builder b;
    const auto entry = b.block(), next = b.block();
    b.edge(entry, next);
    const auto i = b.gpr(0), f = b.xmm(1), g = b.xmm(2), h = b.xmm(), j = b.gpr(), k = b.xmm(), m = b.gpr();
    b.inst(entry, {any(i)}, {});
    b.inst(entry, {reg(f)}, {reg(i)});
    b.inst(entry, {any(g)}, {});
    b.inst(entry, {same(h, 0)}, {reg(f), any(g)});
    b.inst(next, {reg(j)}, {reg(h)});
    auto& call = b.inst(next, {fixed(k, 0)}, {fixed(h, 0), fixed(g, 1), fixed(j, rdi)}, {any(i), any(f)});
    call.clobbers_gpr = kCallerSavedGpr;
    call.clobbers_xmm = kEveryRegister;
    call.is_call = true;
    b.inst(next, {reg(m)}, {reg(k), reg(i)});
    b.inst(next, {}, {reg(m), reg(f), reg(g), reg(i)});
    verified(b.f, "mixed");
}

void rejection_test() {
    {
        Builder b;  // 0 -> 2 is critical: 0 has two successors, 2 two predecessors
        const auto x = b.block(), y = b.block(), z = b.block();
        b.edge(x, y);
        b.edge(x, z);
        b.edge(y, z);
        expect_rejected(b.f, "a critical edge");
    }
    {
        Builder b;  // the cycle 3 <-> 4 is entered at both 3 and 4
        const auto e = b.block(), p = b.block(), q = b.block(), x = b.block(), y = b.block();
        b.edge(e, p);
        b.edge(e, q);
        b.edge(p, x);
        b.edge(q, y);
        b.edge(x, y);
        b.edge(y, x);
        expect_rejected(b.f, "an irreducible loop");
    }
    {
        Builder b;
        const auto e = b.block();
        const auto v = b.gpr();
        b.inst(e, {}, {reg(v)});
        b.inst(e, {reg(v)}, {});
        expect_rejected(b.f, "a use before its definition");
    }
    {
        Builder b;
        const auto e = b.block(), x = b.block(), y = b.block();
        b.edge(e, x);
        b.edge(y, x);
        b.edge(x, y);  // y is only reachable from x: fine; now add an unreachable block
        const auto lost = b.block();
        b.edge(lost, lost);
        expect_rejected(b.f, "an unreachable block");
    }
    {
        Builder b;
        const auto e = b.block();
        std::vector<Operand> uses;
        for (int i = 0; i < 11; ++i) {
            const auto v = b.gpr();
            b.inst(e, {reg(v)}, {});
            uses.push_back(reg(v));
        }
        b.inst(e, {}, uses);
        expect_rejected(b.f, "11 register uses with 10 registers");
    }
    {
        Builder b(kDesignGpr & ~bit(rcx));
        const auto e = b.block();
        const auto v = b.gpr();
        b.inst(e, {reg(v)}, {});
        b.inst(e, {}, {fixed(v, rcx)});
        expect_rejected(b.f, "a fixed register that is not allocatable");
    }
    {
        Builder b(kDesignGpr | bit(r11));
        b.block();
        expect_rejected(b.f, "r11 allocatable");
    }
    {
        Builder b;
        const auto e = b.block();
        const auto v = b.gpr();
        b.inst(e, {reg(v)}, {});
        b.inst(e, {reg(v)}, {});
        expect_rejected(b.f, "a vreg defined twice");
    }
    {
        Builder b;  // a temp on a native call: no register survives it
        const auto e = b.block();
        auto& call = b.inst(e, {}, {});
        call.clobbers_gpr = kEveryRegister;
        call.temps_gpr = 1;
        expect_rejected(b.f, "a temp on an instruction that clobbers every register");
    }
}

// ---- the move sequencer ----------------------------------------------------

struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    std::uint32_t below(std::uint32_t n) { return n == 0 ? 0 : static_cast<std::uint32_t>(next() % n); }
    bool chance(std::uint32_t percent) { return below(100) < percent; }
};

// Runs `moves` one after another on symbolic contents and returns the
// contents of `places`; `parallel` reads every source first.
struct Place {
    Location l;
    RegClass cls;
};
std::size_t place_key(const Location& l, RegClass cls) {
    switch (l.kind) {
    case LocKind::Reg:
        return (cls == RegClass::Gpr ? 0 : 16) + l.index;
    case LocKind::Stack:
        return 32 + l.index;
    case LocKind::FrameSlot:
        return 1000 + l.index;
    }
    return 0;
}

void sequencer_case(const std::vector<Move>& parallel, const std::string& name, std::uint32_t spare,
                    bool expect_spare) {
    std::vector<std::uint64_t> before(2000), after_parallel, after;
    for (std::size_t i = 0; i < before.size(); ++i) before[i] = 1000000 + i;
    after_parallel = before;
    for (const auto& m : parallel) after_parallel[place_key(m.to, m.cls)] = before[place_key(m.from, m.cls)];
    bool used = false;
    const auto moves = mir::sequentialize(parallel, spare, used);
    after = before;
    bool memory_to_memory = false, foreign = false;
    for (const auto& m : moves) {
        memory_to_memory |= m.from.kind != LocKind::Reg && m.to.kind != LocKind::Reg;
        after[place_key(m.to, m.cls)] = after[place_key(m.from, m.cls)];
    }
    // Every destination gets its source's old value; nothing else changes but
    // the scratch registers and the spare slot.
    const auto scratch_gpr = place_key(in_reg(mir::scratch_gpr), RegClass::Gpr);
    const auto scratch_xmm = place_key(in_reg(mir::scratch_xmm), RegClass::Xmm);
    const auto spare_key = place_key(in_stack(spare), RegClass::Gpr);
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (i == scratch_gpr || i == scratch_xmm || i == spare_key) continue;
        if (after[i] != after_parallel[i]) foreign = true;
    }
    expect(!foreign, name + ": the sequence does what the parallel move does");
    expect(!memory_to_memory, name + ": no move between two memory locations");
    if (expect_spare) expect(used, name + ": goes through the spare slot");
    if (!used) {
        expect(std::none_of(moves.begin(), moves.end(), [&](const Move& m) {
                   return m.to == in_stack(spare) || m.from == in_stack(spare);
               }),
               name + ": the spare slot is untouched when not reported");
    }
}

void sequencer_test() {
    const auto G = RegClass::Gpr, X = RegClass::Xmm;
    sequencer_case({{in_reg(rax), in_reg(rcx), G}, {in_reg(rcx), in_reg(rax), G}}, "gpr swap", 9, false);
    {
        bool used = false;
        const auto moves = mir::sequentialize({{in_reg(rax), in_reg(rcx), G}, {in_reg(rcx), in_reg(rax), G}}, 9, used);
        expect(moves_touch(moves, mir::scratch_gpr, G) && moves.size() == 3, "gpr swap through r11");
        const auto xmoves = mir::sequentialize({{in_reg(1), in_reg(2), X}, {in_reg(2), in_reg(1), X}}, 9, used);
        expect(moves_touch(xmoves, mir::scratch_xmm, X) && xmoves.size() == 3, "xmm swap through xmm15");
        const auto mixed = mir::sequentialize({{in_reg(1), in_reg(2), X}, {in_reg(1), in_reg(2), G}}, 9, used);
        expect(mixed.size() == 2, "a GPR and an XMM register of the same number are different places");
    }
    sequencer_case({{in_stack(0), in_stack(1), G}, {in_stack(1), in_stack(0), G}}, "stack swap", 9, true);
    sequencer_case({{in_stack(0), in_stack(1), G}, {in_stack(1), in_stack(0), X}}, "mixed stack swap", 9, false);
    sequencer_case({{in_stack(0), in_stack(1), G}, {in_stack(1), in_reg(rax), G}, {in_reg(rax), in_stack(0), G}},
                   "stack cycle through a register", 9, false);
    sequencer_case({{in_frame(0), in_stack(1), G}, {in_stack(1), in_frame(2), G}, {in_frame(2), in_frame(0), G}},
                   "three memory locations", 9, true);
    sequencer_case({{in_reg(rax), in_reg(rcx), G}, {in_reg(rax), in_reg(rdx), G}, {in_reg(rcx), in_reg(rax), G}},
                   "a cycle with a branch", 9, false);
    sequencer_case({{in_stack(0), in_stack(1), G}, {in_stack(1), in_stack(0), G}, {in_reg(3), in_reg(4), X},
                    {in_reg(4), in_reg(3), X}},
                   "a stack swap and an XMM swap", 9, true);

    // Random parallel moves over 4 GPRs, 4 XMMs, 3 stack and 2 frame slots.
    Rng rng(77);
    for (int round = 0; round < 20000; ++round) {
        std::vector<Place> places;
        for (std::uint32_t r : {0U, 1U, 2U, 3U}) places.push_back({in_reg(r), G});
        for (std::uint32_t r : {0U, 1U, 2U, 3U}) places.push_back({in_reg(r), X});
        for (std::uint32_t s = 0; s < 3; ++s) places.push_back({in_stack(s), G});
        for (std::uint32_t s = 0; s < 2; ++s) places.push_back({in_frame(s), G});
        std::vector<Move> moves;
        std::vector<bool> taken(places.size(), false);
        const auto count = 1 + rng.below(8);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto cls = rng.chance(50) ? G : X;
            // A destination and a source of this class: registers of the
            // class, or memory.
            std::vector<std::size_t> fit;
            for (std::size_t p = 0; p < places.size(); ++p) {
                if (places[p].l.kind != LocKind::Reg || places[p].cls == cls) fit.push_back(p);
            }
            const auto to = fit[rng.below(static_cast<std::uint32_t>(fit.size()))];
            const auto from = fit[rng.below(static_cast<std::uint32_t>(fit.size()))];
            if (taken[to]) continue;
            // A source read by moves of two classes is one value read twice:
            // keep a memory source to one class.
            bool clash = false;
            for (const auto& m : moves) {
                if (m.from == places[from].l && m.cls != cls && places[from].l.kind != LocKind::Reg) clash = true;
                if (m.to == places[to].l && places[to].l.kind != LocKind::Reg) clash = true;
            }
            if (clash) continue;
            taken[to] = true;
            moves.push_back({places[from].l, places[to].l, cls});
        }
        sequencer_case(moves, "random parallel move " + std::to_string(round), 3, false);
    }
}

// ---- random functions ------------------------------------------------------

class Generator {
public:
    Generator(std::uint64_t seed, std::int64_t budget) : rng_(seed), budget_(budget) {}

    Function make() {
        choose_registers();
        const auto count = 2 + rng_.below(39);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto cls = rng_.chance(35) ? RegClass::Xmm : RegClass::Gpr;
            if (i < 2) cls = i == 0 ? RegClass::Gpr : RegClass::Xmm;
            vars_.push_back({cls, rng_.chance(75) ? static_cast<std::int32_t>(i) : -1});
        }
        const auto entry = new_block();
        env_ = define_all(entry);
        const auto start = new_block();
        edge(entry, start);
        const auto extra = rng_.chance(20) ? 1 + rng_.below(2) : 0;
        if (extra > 0) {
            std::vector<std::vector<VReg>> inputs{env_};
            for (std::uint32_t e = 0; e < extra; ++e) {
                const auto block = new_block();
                inputs.push_back(define_all(block));
                edge(block, start);
            }
            for (std::size_t i = 0; i < vars_.size(); ++i) {
                const auto d = fresh(vars_[i].cls, vars_[i].home);
                std::vector<VReg> in;
                for (const auto& from : inputs) in.push_back(from[i]);
                f_.blocks[start].phis.push_back({d, in});
                env_[i] = d;
            }
        }
        cur_ = start;
        entered_loop_ = rng_.chance(15);
        while (budget_ > 0) statement(0);
        finish();
        split_critical_edges();
        return layout();
    }

private:
    struct Var {
        RegClass cls;
        std::int32_t home;
    };

    Rng rng_;
    std::int64_t budget_;
    Function f_;
    std::vector<Var> vars_;
    std::vector<VReg> env_;
    std::vector<VReg> avail_;
    std::vector<std::pair<std::int64_t, VReg>> pending_;
    std::uint32_t cur_ = 0;
    std::uint32_t depth_ = 0;
    bool entered_loop_ = false;
    std::vector<std::uint8_t> regs_[2];

    std::vector<std::uint8_t>& regs(RegClass cls) { return regs_[cls == RegClass::Gpr ? 0 : 1]; }
    bool allocatable(RegClass cls, std::uint8_t r) {
        const auto mask = cls == RegClass::Gpr ? f_.allocatable_gpr : f_.allocatable_xmm;
        return ((mask >> r) & 1U) != 0;
    }

    void choose_registers() {
        if (rng_.below(10) < 6) {
            f_.allocatable_gpr = kDesignGpr | (rng_.chance(50) ? bit(r14) : 0);
            f_.allocatable_xmm = kDesignXmm;
        } else {
            std::vector<std::uint8_t> g{rax, rcx, rdx, rbx, rbp, rsi, rdi, r8, r9, r10, r14};
            std::vector<std::uint8_t> x{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
            for (auto* list : {&g, &x}) {
                for (std::size_t i = list->size(); i > 1; --i) {
                    std::swap((*list)[i - 1], (*list)[rng_.below(static_cast<std::uint32_t>(i))]);
                }
            }
            const auto ng = 2 + rng_.below(9), nx = 2 + rng_.below(14);
            for (std::uint32_t i = 0; i < ng; ++i) f_.allocatable_gpr |= bit(g[i]);
            for (std::uint32_t i = 0; i < nx; ++i) f_.allocatable_xmm |= bit(x[i]);
        }
        for (std::uint8_t r = 0; r < 16; ++r) {
            if (allocatable(RegClass::Gpr, r)) regs_[0].push_back(r);
            if (allocatable(RegClass::Xmm, r)) regs_[1].push_back(r);
        }
    }

    VReg fresh(RegClass cls, std::int32_t home) {
        f_.classes.push_back(cls);
        f_.spill_home.push_back(home);
        return {f_.vreg_count++, cls};
    }

    std::uint32_t new_block() {
        f_.blocks.emplace_back();
        f_.blocks.back().loop_depth = depth_;
        --budget_;
        return static_cast<std::uint32_t>(f_.blocks.size() - 1);
    }

    void edge(std::uint32_t a, std::uint32_t b) {
        f_.blocks[a].succs.push_back(b);
        f_.blocks[b].preds.push_back(a);
    }

    std::vector<VReg> define_all(std::uint32_t block) {
        std::vector<VReg> values;
        for (const auto& var : vars_) {
            const auto d = fresh(var.cls, var.home);
            Inst inst;
            inst.defs.push_back(rng_.chance(60) ? any(d) : reg(d));
            f_.blocks[block].insts.push_back(inst);
            values.push_back(d);
        }
        return values;
    }

    // A value of class `cls` that dominates the current point.
    VReg pick(RegClass cls) {
        std::vector<VReg> from_env, from_avail;
        for (const auto v : env_) {
            if (v.cls == cls) from_env.push_back(v);
        }
        for (const auto v : avail_) {
            if (v.cls == cls) from_avail.push_back(v);
        }
        if (!from_avail.empty() && (from_env.empty() || rng_.chance(25))) {
            return from_avail[rng_.below(static_cast<std::uint32_t>(from_avail.size()))];
        }
        return from_env[rng_.below(static_cast<std::uint32_t>(from_env.size()))];
    }
    VReg pick_any() { return pick(rng_.chance(60) ? RegClass::Gpr : RegClass::Xmm); }

    std::size_t var_of(RegClass cls) {
        std::vector<std::size_t> fit;
        for (std::size_t i = 0; i < vars_.size(); ++i) {
            if (vars_[i].cls == cls) fit.push_back(i);
        }
        return fit[rng_.below(static_cast<std::uint32_t>(fit.size()))];
    }

    // A new value of class `cls`: the next value of a variable, or a
    // temporary kept for later uses. It takes effect once the instruction is
    // built, so the instruction never reads its own defs.
    VReg result(RegClass cls) {
        if (rng_.chance(75)) {
            const auto i = var_of(cls);
            const auto d = fresh(cls, vars_[i].home);
            pending_.push_back({static_cast<std::int64_t>(i), d});
            return d;
        }
        const auto d = fresh(cls, -1);
        pending_.push_back({-1, d});
        return d;
    }

    void commit() {
        for (const auto& [var, d] : pending_) {
            if (var < 0) {
                if (avail_.size() < 48) avail_.push_back(d);
                continue;
            }
            const auto i = static_cast<std::size_t>(var);
            if (rng_.chance(30) && avail_.size() < 48) avail_.push_back(env_[i]);
            env_[i] = d;
        }
        pending_.clear();
    }

    void state(Inst& inst, std::uint32_t most) {
        const auto count = rng_.below(most + 1);
        for (std::uint32_t i = 0; i < count; ++i) inst.state_uses.push_back(any(pick_any()));
    }

    // Whether the instruction can be allocated, counted conservatively: in
    // each class, a value read from a register may stay live after the
    // instruction and so hold its register through the defs, alongside the
    // fixed registers, the SameAsUse and Register defs and the temps; temps
    // need registers the instruction neither clobbers nor fixes.
    bool fits(const Inst& inst) {
        for (const auto cls : {RegClass::Gpr, RegClass::Xmm}) {
            const auto n = static_cast<std::uint32_t>(regs(cls).size());
            const auto clobbers = (cls == RegClass::Gpr ? inst.clobbers_gpr : inst.clobbers_xmm) &
                                  (cls == RegClass::Gpr ? f_.allocatable_gpr : f_.allocatable_xmm);
            const auto temps = cls == RegClass::Gpr ? inst.temps_gpr : inst.temps_xmm;
            std::vector<std::uint32_t> fixed_regs, register_vregs;
            std::uint32_t defs = 0;
            std::vector<bool> named(inst.uses.size(), false);
            for (const auto& d : inst.defs) {
                if (d.c == Constraint::SameAsUse) named[d.same_as] = true;
                if (d.vreg.cls != cls) continue;
                if (d.c == Constraint::Fixed) {
                    if (std::find(fixed_regs.begin(), fixed_regs.end(), d.fixed) == fixed_regs.end()) {
                        fixed_regs.push_back(d.fixed);
                    }
                } else if (d.c != Constraint::Any) {
                    ++defs;
                }
            }
            for (std::size_t k = 0; k < inst.uses.size(); ++k) {
                const auto& u = inst.uses[k];
                if (u.vreg.cls != cls) continue;
                if (u.c == Constraint::Fixed) {
                    if (std::find(fixed_regs.begin(), fixed_regs.end(), u.fixed) == fixed_regs.end()) {
                        fixed_regs.push_back(u.fixed);
                    }
                } else if (u.c == Constraint::Register && !named[k]) {
                    if (std::find(register_vregs.begin(), register_vregs.end(), u.vreg.id) == register_vregs.end()) {
                        register_vregs.push_back(u.vreg.id);
                    }
                }
            }
            const auto total = static_cast<std::uint32_t>(fixed_regs.size() + register_vregs.size()) + defs + temps;
            if (total > n) return false;
            auto taken = clobbers;
            for (const auto r : fixed_regs) taken |= bit(r);
            if (temps > n - static_cast<std::uint32_t>(__builtin_popcountll(taken))) return false;
        }
        return true;
    }

    void emit(Inst inst) {
        f_.blocks[cur_].insts.push_back(std::move(inst));
        --budget_;
    }

    // A random instruction that fits, at the end of the current block.
    void instruction() {
        for (int attempt = 0; attempt < 8; ++attempt) {
            Inst inst;
            const auto saved_count = f_.vreg_count;
            build(inst);
            if (fits(inst)) {
                emit(std::move(inst));
                commit();
                return;
            }
            pending_.clear();
            f_.vreg_count = saved_count;
            f_.classes.resize(saved_count);
            f_.spill_home.resize(saved_count);
        }
    }

    std::uint8_t pick_reg(RegClass cls) {
        const auto& list = regs(cls);
        return list[rng_.below(static_cast<std::uint32_t>(list.size()))];
    }

    void build(Inst& inst) {
        const auto kind = rng_.below(100);
        const auto G = RegClass::Gpr;
        const auto X = RegClass::Xmm;
        if (kind < 34) {  // arithmetic
            const auto cls = rng_.chance(65) ? G : X;
            const auto n = 1 + rng_.below(3);
            for (std::uint32_t k = 0; k < n; ++k) {
                const auto v = pick(cls);
                inst.uses.push_back(rng_.chance(70) ? reg(v) : any(v));
            }
            const auto d = result(cls);
            const auto c = rng_.below(10);
            inst.defs.push_back(c < 5 ? reg(d) : c < 8 ? same(d, 0) : any(d));
            if (rng_.chance(15)) inst.temps_gpr = static_cast<std::uint8_t>(1 + rng_.below(2));
            if (rng_.chance(10)) inst.temps_xmm = 1;
            if (rng_.chance(20)) state(inst, 8);
        } else if (kind < 40) {  // shift by a register count
            if (!allocatable(G, rcx)) return;
            const auto x = pick(G), n = pick(G);
            inst.uses = {reg(x), fixed(n, rcx)};
            inst.defs = {same(result(G), 0)};
        } else if (kind < 44) {  // division
            if (!allocatable(G, rax) || !allocatable(G, rdx)) return;
            inst.uses = {fixed(pick(G), rax), fixed(pick(G), rdx), reg(pick(G))};
            inst.defs = {fixed(result(G), rax), fixed(result(G), rdx)};
            if (rng_.chance(30)) state(inst, 6);
        } else if (kind < 54) {  // a helper call
            inst.is_call = true;
            inst.clobbers_gpr = kCallerSavedGpr;
            inst.clobbers_xmm = kEveryRegister;
            std::vector<std::uint8_t> args;
            for (const std::uint8_t r : {rdi, rsi, rdx, rcx, r8, r9}) {
                if (allocatable(G, r)) args.push_back(r);
            }
            const auto ng = std::min<std::uint32_t>(rng_.below(5), static_cast<std::uint32_t>(args.size()));
            for (std::uint32_t k = 0; k < ng; ++k) inst.uses.push_back(fixed(pick(G), args[k]));
            std::vector<std::uint8_t> xargs;
            for (const std::uint8_t r : {0, 1, 2}) {
                if (allocatable(X, r)) xargs.push_back(r);
            }
            const auto nx = std::min<std::uint32_t>(rng_.below(3), static_cast<std::uint32_t>(xargs.size()));
            for (std::uint32_t k = 0; k < nx; ++k) inst.uses.push_back(fixed(pick(X), xargs[k]));
            if (rng_.chance(30)) inst.uses.push_back(any(pick_any()));
            const auto which = rng_.below(3);
            if (which == 1) {
                const std::uint8_t r = allocatable(G, rax) ? rax : pick_reg(G);
                inst.defs.push_back(fixed(result(G), r));
            } else if (which == 2) {
                inst.defs.push_back(fixed(result(X), allocatable(X, 0) ? std::uint8_t{0} : pick_reg(X)));
            }
            if (rng_.chance(25)) inst.temps_gpr = 1;
            state(inst, 10);
        } else if (kind < 58) {  // a native call
            inst.is_call = true;
            inst.clobbers_gpr = kEveryRegister;
            inst.clobbers_xmm = kEveryRegister;
            if (rng_.chance(60)) inst.defs.push_back(fixed(result(G), pick_reg(G)));
            if (rng_.chance(30)) inst.uses.push_back(any(pick_any()));
            state(inst, 12);
        } else if (kind < 68) {  // a check with a frame state
            const auto cls = rng_.chance(70) ? G : X;
            inst.uses.push_back(reg(pick(cls)));
            if (rng_.chance(50)) inst.uses.push_back(reg(pick(cls)));
            if (rng_.chance(50)) inst.defs.push_back(same(result(cls), 0));
            state(inst, 12);
        } else if (kind < 74) {  // a conversion
            const auto cls = rng_.chance(50) ? G : X;
            inst.uses.push_back(reg(pick(cls)));
            inst.defs.push_back(reg(result(cls == G ? X : G)));
        } else if (kind < 81) {  // a constant
            const auto cls = rng_.chance(60) ? G : X;
            const auto d = result(cls);
            inst.defs.push_back(rng_.chance(70) ? reg(d) : any(d));
        } else if (kind < 86) {  // memory operands
            const auto cls = rng_.chance(60) ? G : X;
            const auto n = 1 + rng_.below(3);
            for (std::uint32_t k = 0; k < n; ++k) inst.uses.push_back(any(pick(cls)));
            inst.defs.push_back(any(result(cls)));
        } else if (kind < 92) {  // two defs
            const auto cls = rng_.chance(60) ? G : X;
            inst.uses = {reg(pick(cls)), reg(pick(cls))};
            inst.defs = {reg(result(cls)), reg(result(cls))};
        } else {  // a fixed use without a call, the same value also in a register
            const auto cls = rng_.chance(60) ? G : X;
            const auto v = pick(cls);
            inst.uses = {fixed(v, pick_reg(cls)), reg(rng_.chance(50) ? v : pick(cls))};
            inst.defs.push_back(reg(result(cls)));
            if (rng_.chance(25)) inst.temps_gpr = 1;
        }
    }

    void branch() {
        Inst inst;
        const auto cls = rng_.chance(70) ? RegClass::Gpr : RegClass::Xmm;
        inst.uses.push_back(reg(pick(cls)));
        if (rng_.chance(50)) inst.uses.push_back(reg(pick(cls)));
        if (fits(inst)) emit(std::move(inst));
    }

    void straight() {
        const auto n = rng_.below(5);
        for (std::uint32_t i = 0; i < n && budget_ > 0; ++i) instruction();
        if (rng_.chance(20)) {
            const auto next = new_block();
            edge(cur_, next);
            cur_ = next;
        }
    }

    void statement(std::uint32_t nest) {
        const auto r = rng_.below(100);
        if (nest < 3 && r < 22) {
            branch_statement(nest);
        } else if (nest < 3 && r < 44) {
            loop_statement(nest);
        } else {
            straight();
        }
    }

    void sequence(std::uint32_t nest) {
        const auto n = 1 + rng_.below(4);
        for (std::uint32_t i = 0; i < n && budget_ > 0; ++i) statement(nest);
    }

    // At a join of `ends`, a phi for each variable whose values differ.
    void join(std::uint32_t block, const std::vector<std::vector<VReg>>& envs) {
        for (std::size_t i = 0; i < vars_.size(); ++i) {
            bool differ = false;
            for (const auto& e : envs) differ |= e[i].id != envs.front()[i].id;
            if (!differ) {
                env_[i] = envs.front()[i];
                continue;
            }
            const auto d = fresh(vars_[i].cls, vars_[i].home);
            std::vector<VReg> inputs;
            for (const auto& e : envs) inputs.push_back(e[i]);
            f_.blocks[block].phis.push_back({d, inputs});
            env_[i] = d;
        }
    }

    void branch_statement(std::uint32_t nest) {
        branch();
        const auto cond = cur_;
        const auto then_block = new_block(), else_block = new_block();
        edge(cond, then_block);
        edge(cond, else_block);
        const auto env = env_;
        const auto avail = avail_.size();
        cur_ = then_block;
        sequence(nest + 1);
        const auto then_end = cur_;
        const auto then_env = env_;
        env_ = env;
        avail_.resize(avail);
        cur_ = else_block;
        if (rng_.chance(30)) {
            f_.blocks[else_block].cold = true;
            for (int i = 0; i < 3; ++i) instruction();
        } else {
            sequence(nest + 1);
        }
        const auto else_end = cur_;
        const auto else_env = env_;
        avail_.resize(avail);
        const auto merge = new_block();
        edge(then_end, merge);
        edge(else_end, merge);
        join(merge, {then_env, else_env});
        cur_ = merge;
    }

    void loop_statement(std::uint32_t nest) {
        const auto pre = cur_;
        ++depth_;
        const auto header = new_block();
        edge(pre, header);
        std::vector<std::size_t> phis;
        for (std::size_t i = 0; i < vars_.size(); ++i) {
            const auto d = fresh(vars_[i].cls, vars_[i].home);
            f_.blocks[header].phis.push_back({d, {env_[i]}});
            env_[i] = d;
        }
        if (nest == 0 && entered_loop_) {
            // An entry straight into the loop header, as the transfer from
            // the interpreter makes: nothing from before the loop dominates it.
            entered_loop_ = false;
            const auto entry = new_block();
            const auto values = define_all(entry);
            edge(entry, header);
            for (std::size_t i = 0; i < vars_.size(); ++i) f_.blocks[header].phis[i].inputs.push_back(values[i]);
            avail_.clear();
        }
        cur_ = header;
        for (int i = 0; i < 2; ++i) instruction();
        branch();
        const auto cond = cur_;
        const auto body = new_block(), exit = new_block();
        edge(cond, body);
        edge(cond, exit);
        const auto exit_env = env_;
        const auto avail = avail_.size();
        cur_ = body;
        sequence(nest + 1);
        bool broke = false;
        std::vector<VReg> break_env;
        if (rng_.chance(25)) {
            branch();
            const auto at = cur_;
            const auto rest = new_block();
            edge(at, exit);
            edge(at, rest);
            break_env = env_;
            broke = true;
            cur_ = rest;
            for (int i = 0; i < 2; ++i) instruction();
        }
        edge(cur_, header);
        for (std::size_t i = 0; i < vars_.size(); ++i) f_.blocks[header].phis[i].inputs.push_back(env_[i]);
        --depth_;
        env_ = exit_env;
        avail_.resize(avail);
        cur_ = exit;
        if (broke) join(exit, {exit_env, break_env});
        f_.blocks[exit].loop_depth = depth_;
    }

    void finish() {
        Inst inst;
        for (const auto v : env_) inst.uses.push_back(any(v));
        state(inst, 6);
        f_.blocks[cur_].insts.push_back(std::move(inst));
    }

    void split_critical_edges() {
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            for (std::size_t k = 0; k < f_.blocks[b].succs.size(); ++k) {
                const auto s = f_.blocks[b].succs[k];
                if (f_.blocks[b].succs.size() < 2 || f_.blocks[s].preds.size() < 2) continue;
                f_.blocks.emplace_back();
                const auto n = static_cast<std::uint32_t>(f_.blocks.size() - 1);
                f_.blocks[n].loop_depth = std::min(f_.blocks[b].loop_depth, f_.blocks[s].loop_depth);
                f_.blocks[n].preds = {b};
                f_.blocks[n].succs = {s};
                f_.blocks[b].succs[k] = n;
                auto& preds = f_.blocks[s].preds;
                *std::find(preds.begin(), preds.end(), b) = n;
            }
        }
    }

    // Entries first, then the others in reverse postorder, cold blocks last.
    Function layout() {
        const auto n = static_cast<std::uint32_t>(f_.blocks.size());
        std::vector<std::uint8_t> seen(n, 0);
        std::vector<std::uint32_t> post;
        for (std::uint32_t e = 0; e < n; ++e) {
            if (!f_.blocks[e].preds.empty() || seen[e]) continue;
            std::vector<std::pair<std::uint32_t, std::size_t>> stack{{e, 0}};
            seen[e] = 1;
            while (!stack.empty()) {
                auto& [b, k] = stack.back();
                if (k < f_.blocks[b].succs.size()) {
                    const auto s = f_.blocks[b].succs[k++];
                    if (!seen[s]) {
                        seen[s] = 1;
                        stack.push_back({s, 0});
                    }
                } else {
                    post.push_back(b);
                    stack.pop_back();
                }
            }
        }
        std::vector<std::uint32_t> order;
        for (std::uint32_t e = 0; e < n; ++e) {
            if (f_.blocks[e].preds.empty()) order.push_back(e);
        }
        for (const bool cold : {false, true}) {
            for (auto it = post.rbegin(); it != post.rend(); ++it) {
                const auto& block = f_.blocks[*it];
                if (!block.preds.empty() && block.cold == cold) order.push_back(*it);
            }
        }
        std::vector<std::uint32_t> where(n);
        for (std::uint32_t i = 0; i < n; ++i) where[order[i]] = i;
        Function out = f_;
        for (std::uint32_t i = 0; i < n; ++i) {
            out.blocks[i] = f_.blocks[order[i]];
            for (auto& p : out.blocks[i].preds) p = where[p];
            for (auto& s : out.blocks[i].succs) s = where[s];
        }
        return out;
    }
};

std::size_t instruction_count(const Function& f) {
    std::size_t n = 0;
    for (const auto& b : f.blocks) n += b.insts.size();
    return n;
}

std::int64_t budget_for(std::uint64_t seed) {
    Rng rng(seed ^ 0xABCDEF);
    if (rng.chance(4)) return 300 + rng.below(700);
    return 5 + rng.below(160);
}

struct RandomStats {
    std::size_t functions = 0, rejected = 0, invalid = 0, nondeterministic = 0, instructions = 0, largest = 0;
    std::uint64_t largest_seed = 0;
    double generate = 0, allocate = 0, check = 0;  // seconds
};

double since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

RandomStats random_test(std::uint64_t first, std::uint64_t count, bool verbose) {
    RandomStats stats;
    std::size_t printed = 0;
    const auto report = [&](std::uint64_t seed, const std::string& what) {
        if (verbose && printed++ < 20) {
            std::fprintf(stderr, "FAIL: random seed %llu: %s\n", static_cast<unsigned long long>(seed), what.c_str());
        }
    };
    for (std::uint64_t seed = first; seed < first + count; ++seed) {
        auto t0 = std::chrono::steady_clock::now();
        const auto f = Generator(seed, budget_for(seed)).make();
        stats.generate += since(t0);
        ++stats.functions;
        const auto size = instruction_count(f);
        stats.instructions += size;
        if (size > stats.largest) {
            stats.largest = size;
            stats.largest_seed = seed;
        }
        Allocation a;
        t0 = std::chrono::steady_clock::now();
        try {
            a = mir::allocate(f);
        } catch (const AllocationError& e) {
            ++stats.rejected;
            report(seed, std::string("rejected: ") + e.what());
            continue;
        }
        stats.allocate += since(t0);
        t0 = std::chrono::steady_clock::now();
        const auto errors = mir::check(f, a);
        stats.check += since(t0);
        if (!errors.empty()) {
            ++stats.invalid;
            report(seed, errors.front());
            continue;
        }
        if (!(mir::allocate(f) == a)) {
            ++stats.nondeterministic;
            report(seed, "a second allocation differs");
        }
    }
    return stats;
}

// ---- the checker on wrong allocations ---------------------------------------

void expect_accepted(const Function& f, const Allocation& a, const std::string& name) {
    const auto errors = mir::check(f, a);
    expect(errors.empty(), name + ": " + first_error(errors));
}

void expect_refused(const Function& f, const Allocation& a, const std::string& name) {
    expect(!mir::check(f, a).empty(), name + ": the checker accepts it");
}

Allocation shaped(const Function& f) {
    Allocation a;
    a.blocks.resize(f.blocks.size());
    for (std::size_t b = 0; b < f.blocks.size(); ++b) {
        a.blocks[b].insts.resize(f.blocks[b].insts.size());
        a.blocks[b].edges.resize(f.blocks[b].succs.size());
    }
    return a;
}

// Allocations written by hand, each right once and then wrong in one way.
void planted_test() {
    const auto G = RegClass::Gpr;
    {
        // v lives across a call that clobbers rax.
        Builder b;
        const auto e = b.block();
        const auto v = b.gpr();
        b.inst(e, {reg(v)}, {});
        b.inst(e, {}, {}).clobbers_gpr = bit(rax);
        b.inst(e, {}, {reg(v)});
        auto a = shaped(b.f);
        a.blocks[0].insts[0].defs = {in_reg(rbx)};
        a.blocks[0].insts[2].uses = {in_reg(rbx)};
        expect_accepted(b.f, a, "planted: a value in rbx across a clobber of rax");
        a.blocks[0].insts[0].defs = {in_reg(rax)};
        a.blocks[0].insts[2].uses = {in_reg(rax)};
        expect_refused(b.f, a, "planted: a value in a clobbered register");
    }
    {
        // A temp in a register the instruction clobbers, or one that is a use.
        Builder b;
        const auto e = b.block();
        const auto v = b.gpr();
        b.inst(e, {reg(v)}, {});
        auto& op = b.inst(e, {}, {reg(v)});
        op.clobbers_gpr = bit(rcx);
        op.temps_gpr = 1;
        auto a = shaped(b.f);
        a.blocks[0].insts[0].defs = {in_reg(rax)};
        a.blocks[0].insts[1].uses = {in_reg(rax)};
        a.blocks[0].insts[1].temps_gpr = {rdx};
        expect_accepted(b.f, a, "planted: a temp in rdx");
        a.blocks[0].insts[1].temps_gpr = {rcx};
        expect_refused(b.f, a, "planted: a clobbered temp");
        a.blocks[0].insts[1].temps_gpr = {rax};
        expect_refused(b.f, a, "planted: a temp that is a use");
        a.blocks[0].insts[1].temps_gpr = {};
        expect_refused(b.f, a, "planted: a missing temp");
    }
    {
        // (a, b) = (b, a) around a loop, in rax and rcx.
        Builder b;
        const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
        b.edge(entry, header);
        b.edge(header, body);
        b.edge(header, exit);
        b.edge(body, header);
        const auto a0 = b.gpr(), b0 = b.gpr(), x = b.gpr(), y = b.gpr();
        b.inst(entry, {reg(a0)}, {});
        b.inst(entry, {reg(b0)}, {});
        b.phi(header, x, {a0, y});
        b.phi(header, y, {b0, x});
        b.inst(header, {}, {reg(x), reg(y)});
        b.inst(body, {}, {reg(x), reg(y)});
        b.inst(exit, {}, {reg(x), reg(y)});
        auto a = shaped(b.f);
        a.blocks[entry].insts[0].defs = {in_reg(rax)};
        a.blocks[entry].insts[1].defs = {in_reg(rcx)};
        a.blocks[header].entry = {{x.id, in_reg(rax)}, {y.id, in_reg(rcx)}};
        a.blocks[body].entry = {{x.id, in_reg(rax)}, {y.id, in_reg(rcx)}};
        a.blocks[exit].entry = {{x.id, in_reg(rax)}, {y.id, in_reg(rcx)}};
        for (const auto blk : {header, body, exit}) a.blocks[blk].insts[0].uses = {in_reg(rax), in_reg(rcx)};
        const std::vector<Move> swap{{in_reg(rax), in_reg(r11), G}, {in_reg(rcx), in_reg(rax), G},
                                     {in_reg(r11), in_reg(rcx), G}};
        a.blocks[body].edges[0] = swap;
        expect_accepted(b.f, a, "planted: a swap through r11");
        a.blocks[body].edges[0] = {{in_reg(rcx), in_reg(rax), G}, {in_reg(rax), in_reg(rcx), G}};
        expect_refused(b.f, a, "planted: a swap without the scratch");
        a.blocks[body].edges[0] = {};
        expect_refused(b.f, a, "planted: a swap left out");
        a.blocks[body].edges[0] = {{in_reg(rax), in_reg(r12), G}, {in_reg(rcx), in_reg(rax), G},
                                   {in_reg(r12), in_reg(rcx), G}};
        expect_refused(b.f, a, "planted: a swap through r12");
        a.blocks[body].edges[0] = swap;
        a.blocks[exit].entry = {{x.id, in_reg(rcx)}, {y.id, in_reg(rax)}};
        expect_refused(b.f, a, "planted: a wrong entry claim");
    }
    {
        // A state use in the register the instruction's def writes.
        Builder b;
        const auto e = b.block();
        const auto v = b.gpr(), d = b.gpr();
        b.inst(e, {reg(v)}, {});
        b.inst(e, {reg(d)}, {}, {any(v)});
        b.inst(e, {}, {reg(d)});
        auto a = shaped(b.f);
        a.blocks[0].insts[0].defs = {in_reg(rax)};
        a.blocks[0].insts[1].defs = {in_reg(rcx)};
        a.blocks[0].insts[1].state_uses = {in_reg(rax)};
        a.blocks[0].insts[2].uses = {in_reg(rcx)};
        expect_accepted(b.f, a, "planted: a state use apart from the def");
        a.blocks[0].insts[1].defs = {in_reg(rax)};
        a.blocks[0].insts[2].uses = {in_reg(rax)};
        expect_refused(b.f, a, "planted: a state use in the def's register");
    }
    {
        // Frame slots, a move between memory locations, SameAsUse.
        Builder b;
        const auto e = b.block();
        const auto v = b.gpr(0), w = b.gpr(1), d = b.gpr();
        b.inst(e, {reg(v)}, {});
        b.inst(e, {reg(w)}, {});
        b.inst(e, {same(d, 0)}, {reg(v), reg(w)});
        b.inst(e, {}, {reg(d), any(w)});
        auto a = shaped(b.f);
        a.stack_slots = 2;
        a.blocks[0].insts[0].defs = {in_reg(rax)};
        a.blocks[0].insts[1].defs = {in_reg(rcx)};
        a.blocks[0].insts[2].uses = {in_reg(rax), in_reg(rcx)};
        a.blocks[0].insts[2].defs = {in_reg(rax)};
        a.blocks[0].insts[3].before = {{in_reg(rcx), in_frame(1), G}};
        a.blocks[0].insts[3].uses = {in_reg(rax), in_frame(1)};
        expect_accepted(b.f, a, "planted: w spilled to its frame slot");
        a.blocks[0].insts[3].before = {{in_reg(rcx), in_frame(0), G}};
        a.blocks[0].insts[3].uses = {in_reg(rax), in_frame(0)};
        expect_refused(b.f, a, "planted: w in another value's frame slot");
        a.blocks[0].insts[3].before = {{in_reg(rcx), in_stack(0), G}, {in_stack(0), in_stack(1), G}};
        a.blocks[0].insts[3].uses = {in_reg(rax), in_stack(1)};
        expect_refused(b.f, a, "planted: a move between two stack slots");
        a.blocks[0].insts[3].before = {};
        a.blocks[0].insts[3].uses = {in_reg(rax), in_reg(rcx)};
        expect_accepted(b.f, a, "planted: w in rcx");
        a.blocks[0].insts[2].defs = {in_reg(rdx)};
        a.blocks[0].insts[3].uses = {in_reg(rdx), in_reg(rcx)};
        expect_refused(b.f, a, "planted: a SameAsUse def apart from its use");
    }
    {
        // A value moved in a loop body and overwritten, but not moved back on
        // the back edge: the header's entry state no longer has it.
        Builder b;
        const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
        b.edge(entry, header);
        b.edge(header, body);
        b.edge(header, exit);
        b.edge(body, header);
        const auto v = b.gpr(), w = b.gpr();
        b.inst(entry, {reg(v)}, {});
        b.inst(header, {}, {reg(v)});
        b.inst(body, {reg(w)}, {});
        b.inst(body, {}, {reg(v), reg(w)});
        b.inst(exit, {}, {reg(v)});
        auto a = shaped(b.f);
        a.blocks[entry].insts[0].defs = {in_reg(rbx)};
        a.blocks[header].insts[0].uses = {in_reg(rbx)};
        a.blocks[body].insts[0].defs = {in_reg(rax)};
        a.blocks[body].insts[1].uses = {in_reg(rbx), in_reg(rax)};
        a.blocks[exit].insts[0].uses = {in_reg(rbx)};
        expect_accepted(b.f, a, "planted: a loop value kept in rbx");
        a.blocks[body].insts[0].before = {{in_reg(rbx), in_reg(rsi), G}};
        a.blocks[body].insts[0].defs = {in_reg(rbx)};
        a.blocks[body].insts[1].uses = {in_reg(rsi), in_reg(rbx)};
        expect_refused(b.f, a, "planted: a loop value lost on the back edge");
        a.blocks[body].edges[0] = {{in_reg(rsi), in_reg(rbx), G}};
        expect_accepted(b.f, a, "planted: a loop value moved back on the back edge");
    }
}

// A copy of a value made on an earlier pass through a loop does not hold the
// value the next pass defines. Each case is refused; the cases the allocation
// gets right are accepted.
void stale_copy_test() {
    const auto G = RegClass::Gpr;
    {
        // v is defined in the loop body and spilled to stack slot 0 on one of
        // two paths, then read from the slot: on a pass along the other path
        // the slot holds the v of an earlier pass.
        Builder b;
        const auto entry = b.block(), header = b.block(1), body = b.block(1), store = b.block(1), skip = b.block(1),
                   latch = b.block(1), exit = b.block();
        b.edge(entry, header);
        b.edge(header, body);
        b.edge(header, exit);
        b.edge(body, store);
        b.edge(body, skip);
        b.edge(store, latch);
        b.edge(skip, latch);
        b.edge(latch, header);
        const auto c0 = b.gpr(), c = b.gpr(), c1 = b.gpr(), v = b.gpr();
        b.inst(entry, {reg(c0)}, {});
        b.phi(header, c, {c0, c1});
        b.inst(header, {}, {reg(c)});
        b.inst(body, {reg(v)}, {});
        b.inst(body, {}, {reg(c)});
        b.inst(store, {}, {});
        b.inst(skip, {}, {});
        b.inst(latch, {}, {any(v)});
        b.inst(latch, {reg(c1)}, {reg(c)});
        b.inst(exit, {}, {reg(c)});
        auto a = shaped(b.f);
        a.stack_slots = 1;
        a.blocks[entry].insts[0].defs = {in_reg(rbx)};
        a.blocks[header].entry = {{c.id, in_reg(rbx)}};
        a.blocks[header].insts[0].uses = {in_reg(rbx)};
        a.blocks[body].insts[0].defs = {in_reg(rax)};
        a.blocks[body].insts[1].uses = {in_reg(rbx)};
        a.blocks[store].insts[0].before = {{in_reg(rax), in_stack(0), G}};
        a.blocks[skip].insts[0].before = {{in_reg(rax), in_stack(0), G}};
        a.blocks[latch].insts[0].uses = {in_stack(0)};
        a.blocks[latch].insts[1].uses = {in_reg(rbx)};
        a.blocks[latch].insts[1].defs = {in_reg(rbx)};
        a.blocks[exit].insts[0].uses = {in_reg(rbx)};
        expect_accepted(b.f, a, "stale copy: a spill on both paths");
        a.blocks[skip].insts[0].before = {};
        expect_refused(b.f, a, "stale copy: a spill on one path, read on both");
    }
    {
        // v is defined at the top of the loop body, read from stack slot 0,
        // and spilled there only afterwards: each pass reads the v of the
        // pass before.
        Builder b;
        const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
        b.edge(entry, header);
        b.edge(header, body);
        b.edge(header, exit);
        b.edge(body, header);
        const auto c0 = b.gpr(), c = b.gpr(), c1 = b.gpr(), v = b.gpr();
        b.inst(entry, {reg(c0)}, {});
        b.phi(header, c, {c0, c1});
        b.inst(header, {}, {reg(c)});
        b.inst(body, {reg(v)}, {});
        b.inst(body, {}, {any(v)});
        b.inst(body, {}, {});
        b.inst(body, {reg(c1)}, {reg(c), reg(v)});
        b.inst(exit, {}, {reg(c)});
        auto a = shaped(b.f);
        a.stack_slots = 1;
        a.blocks[entry].insts[0].defs = {in_reg(rbx)};
        a.blocks[header].entry = {{c.id, in_reg(rbx)}};
        a.blocks[header].insts[0].uses = {in_reg(rbx)};
        a.blocks[body].insts[0].defs = {in_reg(rax)};
        a.blocks[body].insts[1].before = {{in_reg(rax), in_stack(0), G}};
        a.blocks[body].insts[1].uses = {in_stack(0)};
        a.blocks[body].insts[3].uses = {in_reg(rbx), in_reg(rax)};
        a.blocks[body].insts[3].defs = {in_reg(rbx)};
        a.blocks[exit].insts[0].uses = {in_reg(rbx)};
        expect_accepted(b.f, a, "stale copy: a spill before the read");
        a.blocks[body].insts[1].before = {};
        a.blocks[body].insts[2].before = {{in_reg(rax), in_stack(0), G}};
        expect_refused(b.f, a, "stale copy: a spill after the read, read on the next pass");
    }
    {
        // A vreg defined twice, before a loop and in its body (not SSA, which
        // the allocator rejects, but a function the checker can be given): it
        // is spilled after the first definition only, and the body reads the
        // spill slot after the second.
        Builder b;
        const auto entry = b.block(), header = b.block(1), body = b.block(1), exit = b.block();
        b.edge(entry, header);
        b.edge(header, body);
        b.edge(header, exit);
        b.edge(body, header);
        const auto c0 = b.gpr(), c = b.gpr(), c1 = b.gpr(), v = b.gpr();
        b.inst(entry, {reg(v)}, {});
        b.inst(entry, {reg(c0)}, {});
        b.phi(header, c, {c0, c1});
        b.inst(header, {}, {reg(c)});
        b.inst(body, {reg(v)}, {});
        b.inst(body, {}, {any(v)});
        b.inst(body, {reg(c1)}, {reg(c)});
        b.inst(exit, {}, {reg(c)});
        auto a = shaped(b.f);
        a.stack_slots = 1;
        a.blocks[entry].insts[0].defs = {in_reg(rax)};
        a.blocks[entry].insts[1].before = {{in_reg(rax), in_stack(0), G}};
        a.blocks[entry].insts[1].defs = {in_reg(rbx)};
        a.blocks[header].entry = {{c.id, in_reg(rbx)}};
        a.blocks[header].insts[0].uses = {in_reg(rbx)};
        a.blocks[body].insts[0].defs = {in_reg(rax)};
        a.blocks[body].insts[1].before = {{in_reg(rax), in_stack(0), G}};
        a.blocks[body].insts[1].uses = {in_stack(0)};
        a.blocks[body].insts[2].uses = {in_reg(rbx)};
        a.blocks[body].insts[2].defs = {in_reg(rbx)};
        a.blocks[exit].insts[0].uses = {in_reg(rbx)};
        expect_accepted(b.f, a, "stale copy: a second definition spilled again");
        a.blocks[body].insts[1].before = {};
        expect_refused(b.f, a, "stale copy: a second definition read from the first one's spill");
    }
}

// Random allocations made wrong in one way each; every one must be refused.
void planted_random_test(std::uint64_t first, std::uint64_t count) {
    std::array<std::size_t, 9> made{}, refused{};
    const char* names[] = {"two uses swapped", "a scratch save dropped", "a temp on a use", "SameAsUse elsewhere",
                           "a fixed use elsewhere", "a register use in memory", "a def in a foreign frame slot",
                           "a state use in a clobbered register", "a move reading the scratch"};
    for (std::uint64_t seed = first; seed < first + count; ++seed) {
        const auto f = Generator(seed, budget_for(seed)).make();
        Allocation good;
        try {
            good = mir::allocate(f);
        } catch (const AllocationError&) {
            continue;
        }
        if (!mir::check(f, good).empty()) continue;
        Rng rng(seed * 31 + 7);
        std::int32_t max_home = -1;
        for (const auto h : f.spill_home) max_home = std::max(max_home, h);
        for (std::size_t kind = 0; kind < made.size(); ++kind) {
            // Visit instructions from a random start and plant at the first
            // one that admits this kind.
            std::vector<std::pair<std::size_t, std::size_t>> sites;
            for (std::size_t b = 0; b < f.blocks.size(); ++b) {
                for (std::size_t j = 0; j < f.blocks[b].insts.size(); ++j) sites.push_back({b, j});
            }
            if (sites.empty()) continue;
            const auto offset = rng.below(static_cast<std::uint32_t>(sites.size()));
            // A site that does not admit the kind is left unchanged.
            auto bad = good;
            for (std::size_t s = 0; s < sites.size(); ++s) {
                const auto [b, j] = sites[(s + offset) % sites.size()];
                const auto& inst = f.blocks[b].insts[j];
                auto& ia = bad.blocks[b].insts[j];
                const auto regs_of = [&](RegClass cls) {
                    std::vector<std::uint8_t> out;
                    const auto mask = cls == RegClass::Gpr ? f.allocatable_gpr : f.allocatable_xmm;
                    for (std::uint8_t r = 0; r < 16; ++r) {
                        if ((mask >> r) & 1U) out.push_back(r);
                    }
                    return out;
                };
                bool planted = false;
                switch (kind) {
                case 0:
                    for (std::size_t x = 0; x < inst.uses.size() && !planted; ++x) {
                        for (std::size_t y = x + 1; y < inst.uses.size() && !planted; ++y) {
                            if (inst.uses[x].vreg.id != inst.uses[y].vreg.id &&
                                inst.uses[x].vreg.cls == inst.uses[y].vreg.cls &&
                                !(ia.uses[x] == ia.uses[y])) {
                                std::swap(ia.uses[x], ia.uses[y]);
                                planted = true;
                            }
                        }
                    }
                    break;
                case 1:
                    for (auto* list : {&ia.before, &ia.after}) {
                        for (std::size_t m = 0; m < list->size() && !planted; ++m) {
                            const auto& mv = (*list)[m];
                            const auto scratch = mv.cls == RegClass::Gpr ? mir::scratch_gpr : mir::scratch_xmm;
                            if (is_reg(mv.to, scratch)) {
                                list->erase(list->begin() + static_cast<std::ptrdiff_t>(m));
                                planted = true;
                            }
                        }
                    }
                    break;
                case 2:
                    for (std::size_t k = 0; k < inst.uses.size() && !planted; ++k) {
                        if (inst.uses[k].vreg.cls == RegClass::Gpr && ia.uses[k].kind == LocKind::Reg &&
                            !ia.temps_gpr.empty()) {
                            ia.temps_gpr[0] = static_cast<std::uint8_t>(ia.uses[k].index);
                            planted = true;
                        }
                    }
                    break;
                case 3:
                    for (std::size_t k = 0; k < inst.defs.size() && !planted; ++k) {
                        if (inst.defs[k].c != Constraint::SameAsUse) continue;
                        for (const auto r : regs_of(inst.defs[k].vreg.cls)) {
                            if (!is_reg(ia.defs[k], r)) {
                                ia.defs[k] = in_reg(r);
                                planted = true;
                                break;
                            }
                        }
                    }
                    break;
                case 4:
                    for (std::size_t k = 0; k < inst.uses.size() && !planted; ++k) {
                        if (inst.uses[k].c != Constraint::Fixed) continue;
                        for (const auto r : regs_of(inst.uses[k].vreg.cls)) {
                            if (r != inst.uses[k].fixed) {
                                ia.uses[k] = in_reg(r);
                                planted = true;
                                break;
                            }
                        }
                    }
                    break;
                case 5:
                    for (std::size_t k = 0; k < inst.uses.size() && !planted; ++k) {
                        if (inst.uses[k].c == Constraint::Register && bad.stack_slots > 0) {
                            ia.uses[k] = in_stack(0);
                            planted = true;
                        }
                    }
                    break;
                case 6:
                    for (std::size_t k = 0; k < inst.defs.size() && !planted; ++k) {
                        if (inst.defs[k].c != Constraint::Any) continue;
                        for (std::int32_t h = 0; h <= max_home; ++h) {
                            if (h != f.spill_home[inst.defs[k].vreg.id]) {
                                ia.defs[k] = in_frame(static_cast<std::uint32_t>(h));
                                planted = true;
                                break;
                            }
                        }
                    }
                    break;
                case 7:
                    for (std::size_t k = 0; k < inst.state_uses.size() && !planted; ++k) {
                        const auto cls = inst.state_uses[k].vreg.cls;
                        const auto clobbers = cls == RegClass::Gpr ? inst.clobbers_gpr : inst.clobbers_xmm;
                        for (const auto r : regs_of(cls)) {
                            if ((clobbers >> r) & 1U) {
                                ia.state_uses[k] = in_reg(r);
                                planted = true;
                                break;
                            }
                        }
                    }
                    break;
                case 8: {
                    const auto cls = inst.uses.empty() ? RegClass::Gpr : inst.uses[0].vreg.cls;
                    const auto scratch = cls == RegClass::Gpr ? mir::scratch_gpr : mir::scratch_xmm;
                    const auto target = regs_of(cls);
                    if (!target.empty()) {
                        ia.before.insert(ia.before.begin(), Move{in_reg(scratch), in_reg(target[0]), cls});
                        planted = true;
                    }
                    break;
                }
                default:
                    break;
                }
                if (!planted) continue;
                ++made[kind];
                if (!mir::check(f, bad).empty()) {
                    ++refused[kind];
                } else if (refused[kind] + 1 == made[kind]) {
                    std::fprintf(stderr, "FAIL: planted %s accepted, seed %llu\n", names[kind],
                                 static_cast<unsigned long long>(seed));
                }
                break;
            }
        }
    }
    for (std::size_t kind = 0; kind < made.size(); ++kind) {
        expect(made[kind] > 0, std::string("planted: no site for ") + names[kind]);
        expect(refused[kind] == made[kind], std::string("planted: ") + names[kind] + " accepted " +
                                                std::to_string(made[kind] - refused[kind]) + " times");
        std::printf("planted %-36s %zu of %zu refused\n", names[kind], refused[kind], made[kind]);
    }
}

// ---- printing ----------------------------------------------------------------

std::string show(const Location& l) {
    const char* kind = l.kind == LocKind::Reg ? "r" : l.kind == LocKind::Stack ? "s" : "f";
    return kind + std::to_string(l.index);
}

std::string show(const Operand& op) {
    std::string out = (op.vreg.cls == RegClass::Gpr ? "v" : "x") + std::to_string(op.vreg.id);
    switch (op.c) {
    case Constraint::Register:
        return out;
    case Constraint::Fixed:
        return out + "@" + std::to_string(op.fixed);
    case Constraint::Any:
        return out + "?";
    case Constraint::SameAsUse:
        return out + "=u" + std::to_string(op.same_as);
    }
    return out;
}

std::string show(const std::vector<Move>& moves) {
    std::string out;
    for (const auto& m : moves) out += " " + show(m.from) + "->" + show(m.to) + (m.cls == RegClass::Gpr ? "" : "x");
    return out;
}

// Prints a function and, when there is one, its allocation.
void dump(const Function& f, const Allocation* a) {
    std::printf("allocatable gpr %04llx xmm %04llx, %u vregs\n", static_cast<unsigned long long>(f.allocatable_gpr),
                static_cast<unsigned long long>(f.allocatable_xmm), f.vreg_count);
    for (std::size_t b = 0; b < f.blocks.size(); ++b) {
        const auto& block = f.blocks[b];
        std::string preds, succs;
        for (const auto p : block.preds) preds += " " + std::to_string(p);
        for (const auto s : block.succs) succs += " " + std::to_string(s);
        std::printf("block %zu: preds%s succs%s%s\n", b, preds.c_str(), succs.c_str(), block.cold ? " cold" : "");
        if (a != nullptr) {
            std::string entry;
            for (const auto& [v, l] : a->blocks[b].entry) entry += " " + std::to_string(v) + ":" + show(l);
            std::printf("  entry%s\n", entry.c_str());
        }
        for (const auto& phi : block.phis) {
            std::string in;
            for (const auto& v : phi.inputs) in += " " + std::to_string(v.id);
            std::printf("  %u = phi%s\n", phi.def.id, in.c_str());
        }
        for (std::size_t j = 0; j < block.insts.size(); ++j) {
            const auto& inst = block.insts[j];
            std::string line;
            for (const auto& d : inst.defs) line += " " + show(d);
            line += " <-";
            for (const auto& u : inst.uses) line += " " + show(u);
            if (!inst.state_uses.empty()) line += " |";
            for (const auto& u : inst.state_uses) line += " " + show(u);
            if (inst.clobbers_gpr != 0 || inst.clobbers_xmm != 0) line += " clobbers";
            if (inst.temps_gpr != 0 || inst.temps_xmm != 0) {
                line += " temps " + std::to_string(inst.temps_gpr) + "/" + std::to_string(inst.temps_xmm);
            }
            std::printf("  %zu:%s\n", j, line.c_str());
            if (a == nullptr) continue;
            const auto& ia = a->blocks[b].insts[j];
            std::string where;
            if (!ia.before.empty()) where += " before" + show(ia.before) + ";";
            for (const auto* list : {&ia.defs, &ia.uses, &ia.state_uses}) {
                where += " [";
                for (const auto& l : *list) where += " " + show(l);
                where += " ]";
            }
            for (const auto t : ia.temps_gpr) where += " t" + std::to_string(t);
            for (const auto t : ia.temps_xmm) where += " tx" + std::to_string(t);
            if (!ia.after.empty()) where += "; after" + show(ia.after);
            std::printf("     %s\n", where.c_str());
        }
        if (a != nullptr) {
            for (std::size_t k = 0; k < block.succs.size(); ++k) {
                std::printf("  edge to %u:%s\n", block.succs[k], show(a->blocks[b].edges[k]).c_str());
            }
        }
    }
}

// ---- timing ----------------------------------------------------------------

double seconds_to_allocate(const Function& f, int runs) {
    std::vector<double> times;
    for (int i = 0; i < runs; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto a = mir::allocate(f);
        const auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double>(t1 - t0).count());
        expect(!a.blocks.empty(), "timing: an allocation");
    }
    std::sort(times.begin(), times.end());
    return times[times.size() / 2];
}

void large_test(bool timing, std::uint64_t largest_seed) {
    // A function of about 20000 instructions, allocated and checked.
    const auto big = Generator(424242, 29000).make();
    Allocation a;
    try {
        a = mir::allocate(big);
    } catch (const AllocationError& e) {
        expect(false, std::string("large: rejected: ") + e.what());
        return;
    }
    const auto errors = mir::check(big, a);
    expect(errors.empty(), "large: " + first_error(errors));
    if (!timing) return;
    const auto t = seconds_to_allocate(big, 5);
    std::printf("timing: %zu instructions, %zu blocks, %u vregs: %.2f ms (median of 5)\n", instruction_count(big),
                big.blocks.size(), big.vreg_count, t * 1000.0);
    const auto largest = Generator(largest_seed, budget_for(largest_seed)).make();
    const auto tl = seconds_to_allocate(largest, 5);
    std::printf("timing: largest random function (seed %llu): %zu instructions, %zu blocks: %.3f ms (median of 5)\n",
                static_cast<unsigned long long>(largest_seed), instruction_count(largest), largest.blocks.size(),
                tl * 1000.0);
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t seeds = 20000, first = 0;
    bool timing = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seeds" && i + 1 < argc) {
            seeds = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--first" && i + 1 < argc) {
            first = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--timing") {
            timing = true;
        } else if (arg == "--dump" && i + 1 < argc) {
            const auto seed = std::strtoull(argv[++i], nullptr, 10);
            const auto f = Generator(seed, budget_for(seed)).make();
            try {
                const auto a = mir::allocate(f);
                dump(f, &a);
                for (const auto& e : mir::check(f, a)) std::printf("check: %s\n", e.c_str());
            } catch (const AllocationError& e) {
                dump(f, nullptr);
                std::printf("rejected: %s\n", e.what());
            }
            return 0;
        } else {
            std::fprintf(stderr, "usage: %s [--seeds N] [--first S] [--timing] [--dump S]\n", argv[0]);
            return 2;
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    pressure_test();
    live_through_loop_test();
    cycle_test(RegClass::Gpr, 2);
    cycle_test(RegClass::Xmm, 2);
    cycle_test(RegClass::Gpr, 3);
    cycle_test(RegClass::Xmm, 3);
    fixed_test();
    call_test();
    same_as_use_test();
    state_use_test();
    temps_test();
    mixed_test();
    rejection_test();
    sequencer_test();
    planted_test();
    stale_copy_test();
    planted_random_test(first, std::min<std::uint64_t>(seeds, 2000));

    const auto stats = random_test(first, seeds, true);
    expect(stats.rejected == 0, "random: " + std::to_string(stats.rejected) + " functions rejected");
    expect(stats.invalid == 0, "random: " + std::to_string(stats.invalid) + " allocations refused by the checker");
    expect(stats.nondeterministic == 0,
           "random: " + std::to_string(stats.nondeterministic) + " allocations differ when repeated");
    std::printf("random: %zu functions (%zu instructions, largest %zu at seed %llu): %zu rejected, "
                "%zu refused by the checker, %zu nondeterministic\n",
                stats.functions, stats.instructions, stats.largest, static_cast<unsigned long long>(stats.largest_seed),
                stats.rejected, stats.invalid, stats.nondeterministic);
    std::printf("random: %.2f s generating, %.2f s in the first allocations, %.2f s checking\n", stats.generate,
                stats.allocate, stats.check);
    large_test(timing, stats.largest_seed);
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("regalloc tests: %zu checks, %zu failures, %.1f s\n", checks, failures,
                std::chrono::duration<double>(t1 - t0).count());
    return failures == 0 ? 0 : 1;
}

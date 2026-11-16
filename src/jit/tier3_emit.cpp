// The emission of tier 3 (tier3_native.hpp): machine IR, allocated, to
// x86-64 code.
//
// - Layout: the machine blocks in the lowering's order (entries first, loops
//   together, loop heads aligned to 16 bytes), then the cold code: the
//   deoptimization stubs, the slow paths of helper statuses, the shared
//   endings, and the trampoline. Jumps are relaxed by the encoder
//   (rel8 where the displacement fits, else rel32).
// - A native activation's stack frame: the argument array of helper calls at
//   [rsp], the spill slots after it, then the saved `program` of the
//   context; its size keeps rsp 16-byte aligned at every call.
// - Integer operations follow lift::evaluate (src/lift/evaluate.hpp)
//   expression by expression, on the operand values the IR's types allow
//   (an Int(w) value is below 2^w): the processor's flags where they are
//   exactly the failure condition (32 and 64 bits), else the sign extension
//   and range tests of evaluate. Float operations of binary32 and binary64 go
//   through XMM registers as the region code generator's do (compiler.cpp),
//   with the canonical NaN; everything else (16-bit float formats, a value
//   hash of a float, a truncation outside the 64-bit range) calls
//   lift::evaluate through the evaluate helper, saving the registers the
//   call would destroy, so it is the interpreter's code by construction.
#include "jit/tier3_native.hpp"

#include "jit/mir_check.hpp"
#include "jit/regalloc.hpp"
#include "jit/tier3_lower.hpp"
#include "jit/x64.hpp"
#include "lift/evaluate.hpp"
#include "nvm/heap.hpp"
#include "nvm/tier3.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace neura::jit::native {
namespace {

namespace t3 = nvm::tier3;
namespace off = nvm::tier3::offsets;
namespace frame_off = nvm::tier3::frame_offsets;
using x64::Alu;
using x64::Assembler;
using x64::Bit;
using x64::Cond;
using x64::Gpr;
using x64::Imm32;
using x64::Imm8;
using x64::Mem;
using x64::Prec;
using x64::Rm;
using x64::Shift;
using x64::Size;
using x64::Xmm;
using x64::XmmRm;
using x64::S16;
using x64::S32;
using x64::S64;
using x64::S8;
using lift::Opcode;

struct EmitError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr Gpr context = x64::r13;
constexpr Gpr counter = x64::r12;
constexpr Gpr frame = x64::r15;
constexpr Gpr scratch = x64::r11;

constexpr std::uint64_t canonical_nan32 = 0x7FC00000ULL;
constexpr std::uint64_t canonical_nan64 = 0x7FF8000000000000ULL;

std::int32_t displacement(std::uint64_t value) {
    if (value > 0x7FFFFFFFULL) throw EmitError("a displacement out of range");
    return static_cast<std::int32_t>(value);
}

Imm32 imm32(std::uint64_t value) { return Imm32{static_cast<std::int32_t>(static_cast<std::uint32_t>(value))}; }

unsigned float_width(lift::FloatType type) {
    switch (type) {
    case lift::FloatType::Binary16:
    case lift::FloatType::BFloat16: return 16;
    case lift::FloatType::Binary32: return 32;
    default: return 64;
    }
}

// The trampoline: void(Context*, const void* entry). It saves the
// registers C++ expects kept, takes the context, C and the frame, enters the
// code at `entry`, and stores the outcome.
x64::Label trampoline(Assembler& a) {
    const auto label = a.label();
    a.align(16);
    a.bind(label);
    for (const auto r : {x64::rbx, x64::rbp, x64::r12, x64::r13, x64::r14, x64::r15}) a.push(r);
    a.alu(Alu::sub, S64, x64::rsp, Imm32{8});
    a.mov(S64, context, x64::rdi);
    a.mov(S64, counter, Mem::at(context, off::C));
    a.mov(S64, frame, Mem::at(context, off::slots));
    a.call(Rm(x64::rsi));
    a.mov(S32, Mem::at(context, off::outcome), x64::rcx);
    a.alu(Alu::add, S64, x64::rsp, Imm32{8});
    for (const auto r : {x64::r15, x64::r14, x64::r13, x64::r12, x64::rbp, x64::rbx}) a.pop(r);
    a.ret();
    return label;
}

// The code of one unit of a function (entry_units).
class Emitter {
public:
    Emitter(const t3::Function& f, const Lowered& lowered, const mir::Allocation& allocation, Assembler& assembler)
        : f_(f), l_(lowered), alloc_(allocation), a_(assembler) {}

    // Emits the unit's code; returns the label of each of its entries, in
    // Lowered::entry_blocks order.
    std::vector<x64::Label> run() {
        const auto& blocks = l_.function.blocks;
        const auto argument_bytes = std::uint64_t{8} * l_.argument_words;
        spill_base_ = static_cast<std::uint32_t>(argument_bytes);
        saved_program_ = spill_base_ + 8 * alloc_.stack_slots;
        std::uint64_t need = std::uint64_t{saved_program_} + 8;
        if (need % 16 != 8) need += 8;
        if (need + 8 > max_frame_bytes) throw EmitError("a stack frame over 2 KiB");
        frame_bytes_ = static_cast<std::uint32_t>(need);

        for (std::size_t b = 0; b < blocks.size(); ++b) block_labels_.push_back(a_.label());
        ret_outcome_ = a_.label();
        exception_exit_ = a_.label();
        trapped_exit_ = a_.label();
        left_exit_ = a_.label();
        std::vector<bool> entry(blocks.size(), false);
        for (const auto b : l_.entry_blocks) entry[b] = true;
        const auto pending_in = pending_counts(entry);

        for (std::uint32_t b = 0; b < blocks.size(); ++b) {
            const auto& block = blocks[b];
            const auto& allocated = alloc_.blocks[b];
            if (l_.loop_head[b]) a_.align(16);
            a_.bind(block_labels_[b]);
            if (entry[b]) prologue();
            pending_ = pending_in[b];
            // The moves of an edge from a block with several successors.
            if (block.preds.size() == 1 && blocks[block.preds[0]].succs.size() > 1) {
                const auto p = block.preds[0];
                const auto& succs = blocks[p].succs;
                const auto k = static_cast<std::size_t>(std::find(succs.begin(), succs.end(), b) - succs.begin());
                for (const auto& move : alloc_.blocks[p].edges.at(k)) emit_move(move);
            }
            for (std::size_t j = 0; j < block.insts.size(); ++j) {
                block_ = b;
                const auto& ia = allocated.insts[j];
                for (const auto& move : ia.before) emit_move(move);
                emit(block.insts[j], ia);
                for (const auto& move : ia.after) emit_move(move);
            }
            if (block.succs.size() == 1) {
                if (blocks[block.succs[0]].preds.size() > 1) materialize();
                for (const auto& move : allocated.edges.at(0)) emit_move(move);
                if (block.succs[0] != b + 1) a_.jmp(block_labels_[block.succs[0]]);
            }
        }

        // Cold code.
        for (std::size_t i = 0; i < cold_.size(); ++i) cold_[i]();
        for (const auto& [key, label] : tails_) {
            a_.bind(label);
            a_.mov(S64, x64::rdi, context);
            a_.mov(S32, x64::rsi, imm32(key.first));
            a_.mov(S32, x64::rdx, imm32(key.second));
            a_.call(Mem::at(context, off::deoptimize));
            a_.mov(S64, frame, Mem::at(context, off::slots));
            a_.mov(S32, x64::rcx, imm32(static_cast<std::uint32_t>(t3::Outcome::Deopt)));
            a_.jmp(ret_outcome_);
        }
        const auto ending = [&](Label label, t3::Outcome outcome) {
            a_.bind(label);
            a_.mov(S32, x64::rcx, imm32(static_cast<std::uint32_t>(outcome)));
            a_.jmp(ret_outcome_);
        };
        ending(exception_exit_, t3::Outcome::Exception);
        ending(trapped_exit_, t3::Outcome::Trapped);
        ending(left_exit_, t3::Outcome::Leave);
        a_.bind(ret_outcome_);
        a_.alu(Alu::add, S64, x64::rsp, Imm32{static_cast<std::int32_t>(frame_bytes_)});
        a_.ret();
        // Constant data the code reads (the tables of type tests).
        for (std::size_t i = 0; i < data_.size(); ++i) data_[i]();

        std::vector<x64::Label> entries;
        for (const auto b : l_.entry_blocks) entries.push_back(block_labels_[b]);
        return entries;
    }

    std::uint32_t frame_bytes() const { return frame_bytes_; }

private:
    using Label = x64::Label;

    const t3::Function& f_;
    const Lowered& l_;
    const mir::Allocation& alloc_;
    Assembler& a_;
    std::vector<Label> block_labels_;
    std::uint32_t spill_base_ = 0;
    std::uint32_t saved_program_ = 0;
    std::uint32_t frame_bytes_ = 0;
    std::uint32_t block_ = 0;
    // Block steps counted but not yet added to r12 (design 4.1: one add per
    // straight-line run): C is r12 + pending_.
    std::uint64_t pending_ = 0;
    Label ret_outcome_, exception_exit_, trapped_exit_, left_exit_;
    std::vector<std::function<void()>> cold_;
    // The target of an unchecked operation's failure branch (cold: ud2).
    Label unreachable_;
    bool have_unreachable_ = false;

    // A failure the passes proved impossible: an unchecked operation
    // reaches it only if a proof is wrong, and then stops the process.
    Label unreachable() {
        if (!have_unreachable_) {
            unreachable_ = a_.label();
            have_unreachable_ = true;
            const auto label = unreachable_;
            cold_.push_back([this, label] {
                a_.bind(label);
                a_.ud2();
            });
        }
        return unreachable_;
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, Label> tails_;
    std::map<std::vector<std::uint64_t>, Label> stubs_;

    // ---- locations ----------------------------------------------------------

    static bool in_register(const mir::Location& l) { return l.kind == mir::LocKind::Reg; }
    static Gpr gpr(const mir::Location& l) {
        if (!in_register(l)) throw EmitError("a register operand in memory");
        return Gpr{static_cast<std::uint8_t>(l.index)};
    }
    static Xmm xmm(const mir::Location& l) {
        if (!in_register(l)) throw EmitError("an XMM operand in memory");
        return Xmm{static_cast<std::uint8_t>(l.index)};
    }
    Mem memory(const mir::Location& l) const {
        switch (l.kind) {
        case mir::LocKind::Stack: return Mem::at(x64::rsp, displacement(std::uint64_t{spill_base_} + 8ULL * l.index));
        case mir::LocKind::FrameSlot: return Mem::at(frame, displacement(8ULL * l.index));
        default: throw EmitError("a memory operand in a register");
        }
    }
    Mem slot(std::uint32_t r) const { return Mem::at(frame, displacement(8ULL * r)); }
    // A general register or memory location as an r/m operand.
    Rm rm(const mir::Location& l) const {
        if (in_register(l)) return Rm(gpr(l));
        return Rm(memory(l));
    }
    void load(Gpr to, const mir::Location& l) {
        if (in_register(l)) {
            if (gpr(l) != to) a_.mov(S64, to, gpr(l));
        } else {
            a_.mov(S64, to, memory(l));
        }
    }

    void emit_move(const mir::Move& move) {
        const bool from_reg = in_register(move.from);
        const bool to_reg = in_register(move.to);
        if (move.cls == mir::RegClass::Gpr) {
            if (from_reg && to_reg) {
                if (move.from.index != move.to.index) a_.mov(S64, gpr(move.to), gpr(move.from));
            } else if (from_reg) {
                a_.mov(S64, memory(move.to), gpr(move.from));
            } else if (to_reg) {
                a_.mov(S64, gpr(move.to), memory(move.from));
            } else {
                throw EmitError("a move between two memory locations");
            }
            return;
        }
        if (from_reg && to_reg) {
            if (move.from.index != move.to.index) a_.movs(Prec::dbl, xmm(move.to), xmm(move.from));
        } else if (from_reg) {
            a_.movs(Prec::dbl, memory(move.to), xmm(move.from));
        } else if (to_reg) {
            a_.movs(Prec::dbl, xmm(move.to), memory(move.from));
        } else {
            throw EmitError("a move between two memory locations");
        }
    }

    // ---- the counter ----------------------------------------------------------

    // Whether an instruction needs C in r12 itself: a helper that reads it
    // from the context, a native call (the callee counts on from r12), a
    // return (its helper is passed r12), and an ending.
    static bool reads_counter(Kind kind) {
        return kind == Kind::Helper || kind == Kind::Call || kind == Kind::Return || kind == Kind::Leave;
    }

    // The pending count at each block's start: 0 at an entry and at a block
    // with several predecessors (each adds its own before the jump), else the
    // count its predecessor ends with.
    std::vector<std::uint64_t> pending_counts(const std::vector<bool>& entry) const {
        const auto& blocks = l_.function.blocks;
        std::vector<std::uint64_t> in(blocks.size(), 0), out(blocks.size(), 0);
        for (bool changed = true; changed;) {
            changed = false;
            for (std::size_t b = 0; b < blocks.size(); ++b) {
                const auto& block = blocks[b];
                std::uint64_t start = 0;
                if (!entry[b] && block.preds.size() == 1) start = out[block.preds[0]];
                auto count = start;
                for (const auto& inst : block.insts) {
                    const auto& step = l_.steps[inst.opcode];
                    if (step.kind == Kind::CounterAdd) count += step.imm;
                    else if (reads_counter(step.kind)) count = 0;
                }
                if (block.succs.size() == 1 && blocks[block.succs[0]].preds.size() > 1) count = 0;
                if (start != in[b] || count != out[b]) {
                    in[b] = start;
                    out[b] = count;
                    changed = true;
                }
            }
        }
        return in;
    }

    void materialize() {
        if (pending_ == 0) return;
        if (simm32(pending_)) {
            a_.alu(Alu::add, S64, counter, imm32(pending_));
        } else {
            a_.mov_constant(scratch, pending_);
            a_.alu(Alu::add, S64, counter, scratch);
        }
        pending_ = 0;
    }

    // ---- small sequences ----------------------------------------------------

    void prologue() {
        a_.alu(Alu::sub, S64, x64::rsp, Imm32{static_cast<std::int32_t>(frame_bytes_)});
        a_.mov(S64, scratch, Mem::at(context, off::program));
        a_.mov(S64, Mem::at(x64::rsp, static_cast<std::int32_t>(saved_program_)), scratch);
    }

    // r &= mask(w).
    void mask(Gpr r, unsigned w) {
        if (w >= 64) return;
        if (w == 32) {
            a_.mov(S32, r, r);
        } else if (w == 16) {
            a_.movzx(S32, r, S16, r);
        } else if (w == 8) {
            a_.movzx(S32, r, S8, r);
        } else if (w < 32) {
            a_.alu(Alu::and_, S32, r, imm32(lift::mask_of(w)));
        } else {
            a_.shift(Shift::shl, S64, r, Imm8{static_cast<std::uint8_t>(64 - w)});
            a_.shift(Shift::shr, S64, r, Imm8{static_cast<std::uint8_t>(64 - w)});
        }
    }

    // r = sign_extend(r, w) for r below 2^w.
    void sign_extend_fitting(Gpr r, unsigned w) {
        if (w >= 64) return;
        if (w == 32) {
            a_.movsxd(r, r);
        } else if (w == 16) {
            a_.movsx(S64, r, S16, r);
        } else if (w == 8) {
            a_.movsx(S64, r, S8, r);
        } else {
            a_.shift(Shift::shl, S64, r, Imm8{static_cast<std::uint8_t>(64 - w)});
            a_.shift(Shift::sar, S64, r, Imm8{static_cast<std::uint8_t>(64 - w)});
        }
    }

    // r = (r ^ s) - s with s = 2^(w - 1): lift::sign_extend of any value.
    void sign_extend_any(Gpr r, unsigned w) {
        if (w >= 64) return;
        const auto s = std::uint64_t{1} << (w - 1);
        if (simm32(s)) {
            a_.alu(Alu::xor_, S64, r, imm32(s));
            a_.alu(Alu::sub, S64, r, imm32(s));
        } else {
            a_.mov_constant(scratch, s);
            a_.alu(Alu::xor_, S64, r, scratch);
            a_.alu(Alu::sub, S64, r, scratch);
        }
    }

    void sign_extend(Gpr r, unsigned w, bool fits) {
        if (fits) sign_extend_fitting(r, w);
        else sign_extend_any(r, w);
    }

    // cmp r, value (64 bits), through r11 when it is no 32-bit immediate.
    void compare_constant(Gpr r, std::uint64_t value) {
        if (simm32(value)) {
            a_.alu(Alu::cmp, S64, r, imm32(value));
        } else {
            a_.mov_constant(scratch, value);
            a_.alu(Alu::cmp, S64, r, scratch);
        }
    }

    // Branches to `fail` unless lo <= r <= hi (signed).
    void range(Gpr r, std::int64_t lo, std::int64_t hi, Label fail) {
        compare_constant(r, static_cast<std::uint64_t>(hi));
        a_.jcc(Cond::g, fail);
        compare_constant(r, static_cast<std::uint64_t>(lo));
        a_.jcc(Cond::l, fail);
    }

    void branch(Cond cc, std::uint32_t taken, std::uint32_t other) {
        if (other == block_ + 1) {
            a_.jcc(cc, block_labels_[taken]);
        } else if (taken == block_ + 1) {
            a_.jcc(x64::invert(cc), block_labels_[other]);
        } else {
            a_.jcc(cc, block_labels_[taken]);
            a_.jmp(block_labels_[other]);
        }
    }

    // ---- stubs ----------------------------------------------------------------

    // The deoptimization stub of a check: it stores every value the frame
    // state names, from where the allocation keeps it at this instruction,
    // into the context's value array, then calls the deoptimization helper.
    // Checks with the same state, reason and locations share a stub.
    Label stub(const mir::Inst& inst, const mir::InstAllocation& ia) {
        const auto& step = l_.steps[inst.opcode];
        if (step.state == t3::none) throw EmitError("a check without a frame state");
        std::vector<std::pair<mir::Location, std::uint32_t>> values;
        for (const auto& [use, value] : step.state_values) values.emplace_back(ia.state_uses.at(use), value);
        std::vector<std::uint64_t> key{step.state, step.reason, pending_};
        for (const auto& [location, value] : values) {
            key.push_back(value);
            key.push_back((static_cast<std::uint64_t>(location.kind) << 32) | location.index);
        }
        const auto constants = step.state_constants;
        const auto pending = pending_;
        if (const auto found = stubs_.find(key); found != stubs_.end()) return found->second;
        const auto label = a_.label();
        stubs_.emplace(std::move(key), label);
        const auto tail = tail_for(step.state, step.reason);
        const auto blocks = f_.states[step.state].blocks;
        cold_.push_back([this, label, tail, blocks, values, constants, pending] {
            a_.bind(label);
            a_.mov(S64, scratch, Mem::at(context, off::values));
            const auto at = [&](std::uint32_t value) { return Mem::at(scratch, displacement(8ULL * value)); };
            for (const auto& [location, value] : values) {
                if (in_register(location)) a_.mov(S64, at(value), gpr(location));
            }
            // C: r12 and the steps not yet added to it.
            if (pending == 0) {
                a_.mov(S64, at(blocks), counter);
            } else {
                a_.lea(S64, x64::rax, Mem::at(counter, displacement(pending)));
                a_.mov(S64, at(blocks), x64::rax);
            }
            for (const auto& [location, value] : values) {
                if (in_register(location)) continue;
                a_.mov(S64, x64::rax, memory(location));
                a_.mov(S64, at(value), x64::rax);
            }
            for (const auto& [value, bits] : constants) {
                if (simm32(bits)) {
                    a_.mov(S64, at(value), imm32(bits));
                } else {
                    a_.mov_constant(x64::rax, bits);
                    a_.mov(S64, at(value), x64::rax);
                }
            }
            a_.jmp(tail);
        });
        return label;
    }

    Label tail_for(std::uint32_t state, std::uint32_t reason) {
        const auto key = std::make_pair(state, reason);
        const auto found = tails_.find(key);
        if (found != tails_.end()) return found->second;
        const auto label = a_.label();
        tails_.emplace(key, label);
        return label;
    }

    // ---- helper calls ---------------------------------------------------------

    void call_helper(std::int32_t offset) {
        a_.call(Mem::at(context, offset));
        // A push or pop may have moved the frame arenas.
        a_.mov(S64, frame, Mem::at(context, off::slots));
    }

    void store_arguments(const mir::InstAllocation& ia, std::size_t first, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto& location = ia.uses[first + i];
            const auto to = Mem::at(x64::rsp, static_cast<std::int32_t>(8 * i));
            if (in_register(location)) {
                a_.mov(S64, to, gpr(location));
            } else {
                a_.mov(S64, scratch, memory(location));
                a_.mov(S64, to, scratch);
            }
        }
    }

    // Branches to the slow path unless the status is Ok; the slow path goes
    // to `fault` for Fault (when given) and to the ending of any other status.
    void check_status(Label fault, bool has_fault) {
        const auto slow = a_.label();
        a_.alu(Alu::cmp, S32, Mem::at(context, off::status), Imm32{0});
        a_.jcc(Cond::ne, slow);
        cold_.push_back([this, slow, fault, has_fault] {
            a_.bind(slow);
            const auto status = Mem::at(context, off::status);
            if (has_fault) {
                a_.alu(Alu::cmp, S32, status, imm32(static_cast<std::uint32_t>(t3::NativeStatus::Fault)));
                a_.jcc(Cond::e, fault);
            }
            a_.alu(Alu::cmp, S32, status, imm32(static_cast<std::uint32_t>(t3::NativeStatus::Trapped)));
            a_.jcc(Cond::e, trapped_exit_);
            a_.alu(Alu::cmp, S32, status, imm32(static_cast<std::uint32_t>(t3::NativeStatus::Left)));
            a_.jcc(Cond::e, left_exit_);
            a_.jmp(exception_exit_);
        });
    }

    // lift::evaluate of IR instruction `inst` through the evaluate helper,
    // saving every allocatable register a call destroys, so the instruction
    // destroys none: result = evaluate(a, b), or `fail` when its check
    // fails and the instruction is checked.
    void call_evaluate(std::uint32_t inst, const Gpr* a, const Gpr* b, Gpr result, const Label* fail) {
        static constexpr Gpr saved[] = {x64::rax, x64::rcx, x64::rdx, x64::rsi, x64::rdi, x64::r8, x64::r9, x64::r10};
        for (const auto r : saved) a_.push(r);
        if (b) a_.mov(S64, scratch, *b);
        if (a) a_.mov(S64, x64::rdx, *a);
        if (b) a_.mov(S64, x64::rcx, scratch);
        a_.mov(S32, x64::rsi, imm32(inst));
        a_.mov(S64, x64::rdi, context);
        call_helper(off::evaluate);
        a_.mov(S64, scratch, x64::rax);
        for (auto i = std::size(saved); i > 0; --i) a_.pop(saved[i - 1]);
        if (fail) {
            a_.alu(Alu::cmp, S32, Mem::at(context, off::status), Imm32{0});
            a_.jcc(Cond::ne, *fail);
        }
        a_.mov(S64, result, scratch);
    }

    // ---- instructions ---------------------------------------------------------

    void emit(const mir::Inst& inst, const mir::InstAllocation& ia) {
        const auto& step = l_.steps[inst.opcode];
        if (reads_counter(step.kind)) materialize();
        switch (step.kind) {
        case Kind::Const: a_.mov_constant(gpr(ia.defs[0]), step.imm); return;
        case Kind::RunConst: {
            const auto d = gpr(ia.defs[0]);
            a_.mov(S64, d, Mem::at(context, off::run_constants));
            a_.mov(S64, d, Mem::at(d, displacement(8ULL * step.index)));
            mask(d, step.width);
            return;
        }
        case Kind::EntryLoad: {
            const auto& to = ia.defs[0];
            if (in_register(to)) {
                a_.mov(S64, gpr(to), slot(step.index));
            } else if (!(to.kind == mir::LocKind::FrameSlot && to.index == step.index)) {
                a_.mov(S64, scratch, slot(step.index));
                a_.mov(S64, memory(to), scratch);
            }
            return;
        }
        case Kind::EntryFlag: {
            const auto d = gpr(ia.defs[0]);
            a_.mov(S64, d, Mem::at(context, off::flags));
            a_.movzx(S32, d, S8, Mem::at(d, displacement(step.index)));
            return;
        }
        case Kind::CounterAdd: pending_ += step.imm; return;
        case Kind::Lift: return lift(inst, ia, step);
        case Kind::Mask:
            if (step.a_width > step.width) mask(gpr(ia.defs[0]), step.width);
            return;
        case Kind::Gather: {
            const auto t = Gpr{ia.temps_gpr.at(0)};
            a_.alu(Alu::xor_, S32, t, t);
            for (std::size_t i = 0; i < step.bits.size(); ++i) {
                load(scratch, ia.uses[i]);
                if (step.bits[i] != 0) a_.shift(Shift::shr, S64, scratch, Imm8{static_cast<std::uint8_t>(step.bits[i])});
                a_.alu(Alu::and_, S32, scratch, Imm32{1});
                if (i != 0) a_.shift(Shift::shl, S64, scratch, Imm8{static_cast<std::uint8_t>(i)});
                a_.alu(Alu::or_, S64, t, scratch);
            }
            a_.mov(S64, gpr(ia.defs[0]), t);
            return;
        }
        case Kind::Budget: {
            // Deoptimizes when C + P > room (unsigned, wrapping as the
            // interpreter's sum).
            if (step.imm + pending_ <= 0x7FFFFFFFULL) {
                a_.lea(S64, scratch, Mem::at(counter, static_cast<std::int32_t>(step.imm + pending_)));
            } else {
                a_.mov_constant(scratch, step.imm + pending_);
                a_.alu(Alu::add, S64, scratch, counter);
            }
            a_.alu(Alu::cmp, S64, scratch, Mem::at(context, off::room));
            a_.jcc(Cond::a, stub(inst, ia));
            return;
        }
        case Kind::SwitchCheck: {
            const auto label = stub(inst, ia);
            if (step.imm == 0) {
                a_.jmp(label);
                return;
            }
            compare_constant(gpr(ia.uses[0]), step.imm);
            a_.jcc(Cond::ae, label);
            return;
        }
        case Kind::Guard:
            a_.test(S8, gpr(ia.uses[0]), Imm32{1});
            a_.jcc(Cond::e, stub(inst, ia));
            return;
        case Kind::StateCall: {
            const auto count = static_cast<std::uint32_t>(inst.uses.size());
            store_arguments(ia, 0, count);
            a_.mov(S64, x64::rdi, context);
            a_.mov(S32, x64::rsi, imm32(step.index));
            a_.mov(S64, x64::rdx, x64::rsp);
            a_.mov(S32, x64::rcx, imm32(count));
            call_helper(off::state);
            check_status(stub(inst, ia), true);
            return;
        }
        case Kind::CheckReference:
            // The bits are in rdx.
            a_.mov(S64, x64::rdi, context);
            a_.mov(S32, x64::rsi, imm32(step.index));
            call_helper(off::check_reference);
            check_status(stub(inst, ia), true);
            return;
        case Kind::WriteBack: return write_back(ia, step);
        case Kind::Helper: {
            const auto count = static_cast<std::uint32_t>(inst.uses.size());
            a_.mov(S64, Mem::at(context, off::C), counter);
            store_arguments(ia, 0, count);
            a_.mov(S64, x64::rdi, context);
            a_.mov(S32, x64::rsi, imm32(step.index));
            a_.mov(S64, x64::rdx, x64::rsp);
            a_.mov(S32, x64::rcx, imm32(count));
            call_helper(off::helper);
            check_status(Label{}, false);
            return;
        }
        case Kind::Call: return call(inst, ia, step);
        case Kind::Return: {
            if (inst.uses.empty()) a_.alu(Alu::xor_, S32, x64::rsi, x64::rsi);
            if (f_.variant == t3::Variant::Standard) inline_pop();
            a_.mov(S64, x64::rdx, counter);
            a_.mov(S64, x64::rdi, context);
            call_helper(off::ret);
            check_status(stub(inst, ia), true);
            // The value comes back in rax; C counts the return block.
            a_.mov(S64, counter, Mem::at(context, off::C));
            a_.mov(S32, x64::rcx, imm32(static_cast<std::uint32_t>(t3::Outcome::Return)));
            a_.jmp(ret_outcome_);
            return;
        }
        case Kind::Leave:
            a_.mov(S64, Mem::at(context, off::C), counter);
            a_.mov(S64, x64::rdi, context);
            a_.mov(S32, x64::rsi, imm32(step.imm));
            call_helper(off::leave);
            a_.jmp(left_exit_);
            return;
        case Kind::Deopt: a_.jmp(stub(inst, ia)); return;
        case Kind::Branch: {
            no_moves_after(ia);
            const auto& succs = l_.function.blocks[block_].succs;
            a_.test(S8, gpr(ia.uses[0]), Imm32{1});
            branch(Cond::ne, succs[0], succs[1]);
            return;
        }
        case Kind::CompareBranch: {
            no_moves_after(ia);
            const auto& in = f_.insts[step.inst].lifted;
            const auto shape = lift_shape(in, step);
            const auto cc = compare(in, step, shape, ia);
            const auto& succs = l_.function.blocks[block_].succs;
            branch(cc, succs[0], succs[1]);
            return;
        }
        case Kind::Switch: {
            no_moves_after(ia);
            const auto& succs = l_.function.blocks[block_].succs;
            const auto index = gpr(ia.uses[0]);
            for (std::size_t i = 0; i + 1 < succs.size(); ++i) {
                a_.alu(Alu::cmp, S64, index, imm32(i));
                a_.jcc(Cond::e, block_labels_[succs[i]]);
            }
            a_.jmp(block_labels_[succs.back()]);
            return;
        }
        case Kind::Dispatch: {
            no_moves_after(ia);
            const auto& succs = l_.function.blocks[block_].succs;
            const auto& table = f_.dispatch_tables[step.imm];
            const auto tag = gpr(ia.uses[0]);
            for (const auto& [value, exit] : table.exits) {
                compare_constant(tag, value);
                a_.jcc(Cond::e, block_labels_[succs[exit]]);
            }
            a_.jmp(block_labels_[succs[table.fallback]]);
            return;
        }
        default: return heap(inst, ia, step);
        }
        throw EmitError("an unknown machine instruction");
    }

    static void no_moves_after(const mir::InstAllocation& ia) {
        if (!ia.after.empty()) throw EmitError("moves after a branch");
    }

    // The physical frame from the state: each slot whose entry is not
    // Unchanged (a value already in its own frame slot needs no store), each
    // initialized flag, and the block.
    void write_back(const mir::InstAllocation& ia, const Step& step) { write_back_from(ia, step, 0, Gpr{ia.temps_gpr.at(0)}); }

    // The write-back of `step`'s state from the uses at `first` on, with the
    // temporary register `t`.
    void write_back_from(const mir::InstAllocation& ia, const Step& step, std::size_t first, Gpr t) {
        const auto& state = f_.states[step.state];
        const auto constant = [&](std::uint64_t v, std::uint64_t& bits) {
            for (const auto& [value, known] : step.state_constants) {
                if (value == v) {
                    bits = known;
                    return true;
                }
            }
            return false;
        };
        const auto store_bits = [&](std::uint32_t r, std::uint64_t bits) {
            if (simm32(bits)) {
                a_.mov(S64, slot(r), imm32(bits));
            } else {
                a_.mov_constant(t, bits);
                a_.mov(S64, slot(r), t);
            }
        };
        std::size_t u = first;
        for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
            const auto& entry = state.slots[r];
            std::uint64_t bits = 0;
            if (entry.kind == t3::StateSlot::Kind::Value && constant(entry.value, bits)) {
                store_bits(r, bits);
            } else if (entry.kind == t3::StateSlot::Kind::Value) {
                const auto& location = ia.uses[u++];
                if (location.kind == mir::LocKind::FrameSlot && location.index == r) continue;
                if (in_register(location)) {
                    a_.mov(S64, slot(r), gpr(location));
                } else {
                    a_.mov(S64, t, memory(location));
                    a_.mov(S64, slot(r), t);
                }
            } else if (entry.kind == t3::StateSlot::Kind::Const) {
                store_bits(r, entry.value);
            }
        }
        bool flags = false;
        const auto flags_base = [&] {
            if (!flags) a_.mov(S64, scratch, Mem::at(context, off::flags));
            flags = true;
        };
        for (std::uint32_t r = 0; r < state.flags.size(); ++r) {
            const auto& entry = state.flags[r];
            std::uint64_t bits = 0;
            if (entry.kind == t3::StateFlag::Kind::One ||
                (entry.kind == t3::StateFlag::Kind::Value && constant(entry.value, bits))) {
                if (entry.kind == t3::StateFlag::Kind::One) bits = 1;
                flags_base();
                a_.mov(S8, Mem::at(scratch, displacement(r)), Imm32{static_cast<std::int32_t>(bits & 1U)});
            } else if (entry.kind == t3::StateFlag::Kind::Value) {
                load(t, ia.uses[u++]);
                a_.alu(Alu::and_, S32, t, Imm32{1});
                flags_base();
                a_.mov(S8, Mem::at(scratch, displacement(r)), t);
            }
        }
        a_.mov(S64, scratch, Mem::at(context, off::block));
        a_.mov(S32, Mem::at(scratch), imm32(state.block));
    }

    // A call not inlined: the push through the call helper, then the
    // callee's native code, which continues C in r12.
    // The largest frame native code pushes itself; a larger one is pushed
    // by the call helper.
    static constexpr std::uint32_t max_inline_frame = 64;

    // The callee's frame pushed in place, exactly as FrameStack::push does:
    // the header (block 0, the return slot and block, the base, not a
    // boundary, called from this program), every slot zero and
    // uninitialized, the arguments (on the stack) in the first slots with
    // their flags set, and the stack's sizes. Only where the stack has room
    // for it without growing and the frame limit lets it be pushed (else
    // `slow`); the innermost frame in the context becomes the callee's.
    void inline_push(const Step& step, std::uint32_t count, std::uint32_t slots, Label slow) {
        const auto& exit = *f_.exits[step.index];
        const auto record = x64::rax, size = x64::rcx, base = x64::rdx, end = x64::rsi, header = x64::rdi,
                   t = x64::r8, values = x64::r9, flags = x64::r10;
        a_.mov(S64, record, Mem::at(context, off::frames));
        a_.mov(S64, size, Mem::at(record, frame_off::size));
        a_.alu(Alu::cmp, S64, size, Mem::at(record, frame_off::capacity));
        a_.jcc(Cond::ae, slow);
        a_.alu(Alu::cmp, S64, size, Mem::at(context, off::max_frames));
        a_.jcc(Cond::ae, slow);
        a_.mov(S64, base, Mem::at(record, frame_off::slot_size));
        a_.lea(S64, end, Mem::at(base, static_cast<std::int32_t>(slots)));
        a_.alu(Alu::cmp, S64, end, Mem::at(record, frame_off::slot_capacity));
        a_.jcc(Cond::a, slow);
        a_.lea(S64, header, Mem::at(size, size, 4));
        a_.shift(Shift::shl, S64, header, Imm8{3});
        a_.alu(Alu::add, S64, header, Mem::at(record, frame_off::headers));
        a_.mov(S32, Mem::at(header, frame_off::function), imm32(step.imm));
        a_.mov(S32, Mem::at(header, frame_off::block), Imm32{0});
        a_.mov(S32, Mem::at(header, frame_off::return_slot), imm32(exit.result_slot));
        a_.mov(S32, Mem::at(header, frame_off::return_block), imm32(exit.resume));
        a_.mov(S64, Mem::at(header, frame_off::base), base);
        a_.mov(S8, Mem::at(header, frame_off::boundary), Imm32{0});
        a_.mov_constant(t, reinterpret_cast<std::uint64_t>(f_.programs.front()));
        a_.mov(S64, Mem::at(header, frame_off::called_from), t);
        a_.mov(S64, values, Mem::at(record, frame_off::slots));
        a_.lea(S64, values, Mem::at(values, base, 8));
        a_.mov(S64, flags, Mem::at(record, frame_off::initialized));
        a_.alu(Alu::add, S64, flags, base);
        for (std::uint32_t i = 0; i < slots; ++i) {
            const auto at = static_cast<std::int32_t>(8 * i);
            if (i < count) {
                a_.mov(S64, t, Mem::at(x64::rsp, at));
                a_.mov(S64, Mem::at(values, at), t);
            } else {
                a_.mov(S64, Mem::at(values, at), Imm32{0});
            }
            a_.mov(S8, Mem::at(flags, static_cast<std::int32_t>(i)), Imm32{i < count ? 1 : 0});
        }
        a_.alu(Alu::add, S64, size, Imm32{1});
        a_.mov(S64, Mem::at(record, frame_off::size), size);
        a_.mov(S64, Mem::at(record, frame_off::slot_size), end);
        a_.mov(S64, Mem::at(context, off::slots), values);
        a_.mov(S64, Mem::at(context, off::flags), flags);
        a_.lea(S64, t, Mem::at(header, frame_off::block));
        a_.mov(S64, Mem::at(context, off::block), t);
        a_.alu(Alu::add, S64, Mem::at(context, off::inline_calls), Imm32{1});
    }

    // A return to a native caller with the innermost frame popped in place,
    // exactly as commit_exit's Return does: when the frame is not the last
    // nor a callback's (else the return helper, which deoptimizes), the
    // stack's sizes go back to the caller's, the caller's result slot gets
    // the value (in rsi) and its flag, and its block is the return block.
    // The return block's step is C's; the value comes back in rax. An
    // activation the dispatcher entered returns through the helper, which
    // ends it as a Leave.
    void inline_pop() {
        const auto slow = a_.label();
        const auto record = x64::rax, size = x64::rcx, t = x64::rdx, header = x64::rdi, caller = x64::r8,
                   at = x64::r9, arena = x64::r10;
        a_.alu(Alu::cmp, S32, Mem::at(context, off::native_depth), Imm32{1});
        a_.jcc(Cond::be, slow);
        a_.mov(S64, record, Mem::at(context, off::frames));
        a_.mov(S64, size, Mem::at(record, frame_off::size));
        a_.alu(Alu::cmp, S64, size, Imm32{2});
        a_.jcc(Cond::b, slow);
        a_.lea(S64, header, Mem::at(size, size, 4, -1 * 5));
        a_.shift(Shift::shl, S64, header, Imm8{3});
        a_.alu(Alu::add, S64, header, Mem::at(record, frame_off::headers));
        a_.alu(Alu::cmp, S8, Mem::at(header, frame_off::boundary), Imm32{0});
        a_.jcc(Cond::ne, slow);
        a_.mov(S64, t, Mem::at(header, frame_off::base));
        a_.mov(S64, Mem::at(record, frame_off::slot_size), t);
        a_.alu(Alu::sub, S64, size, Imm32{1});
        a_.mov(S64, Mem::at(record, frame_off::size), size);
        a_.lea(S64, caller, Mem::at(header, -frame_off::header_bytes));
        const auto no_result = a_.label();
        a_.mov(S32, t, Mem::at(header, frame_off::return_slot));
        a_.alu(Alu::cmp, S32, t, imm32(nvr::absent));
        a_.jcc(Cond::e, no_result);
        a_.mov(S64, at, Mem::at(caller, frame_off::base));
        a_.alu(Alu::add, S64, at, t);
        a_.mov(S64, arena, Mem::at(record, frame_off::slots));
        a_.mov(S64, Mem::at(arena, at, 8), x64::rsi);
        a_.mov(S64, arena, Mem::at(record, frame_off::initialized));
        a_.mov(S8, Mem::at(arena, at, 1), Imm32{1});
        a_.bind(no_result);
        a_.mov(S32, t, Mem::at(header, frame_off::return_block));
        a_.mov(S32, Mem::at(caller, frame_off::block), t);
        a_.mov(S32, Mem::at(context, off::next_block), t);
        a_.mov(S64, at, Mem::at(caller, frame_off::base));
        a_.mov(S64, arena, Mem::at(record, frame_off::slots));
        a_.lea(S64, arena, Mem::at(arena, at, 8));
        a_.mov(S64, Mem::at(context, off::slots), arena);
        a_.mov(S64, frame, arena);
        a_.mov(S64, arena, Mem::at(record, frame_off::initialized));
        a_.alu(Alu::add, S64, arena, at);
        a_.mov(S64, Mem::at(context, off::flags), arena);
        a_.lea(S64, arena, Mem::at(caller, frame_off::block));
        a_.mov(S64, Mem::at(context, off::block), arena);
        a_.alu(Alu::add, S64, Mem::at(context, off::inline_returns), Imm32{1});
        a_.alu(Alu::add, S64, counter, Imm32{1});
        a_.mov(S64, x64::rax, x64::rsi);
        a_.mov(S32, x64::rcx, imm32(static_cast<std::uint32_t>(t3::Outcome::Return)));
        a_.jmp(ret_outcome_);
        a_.bind(slow);
    }

    void call(const mir::Inst& inst, const mir::InstAllocation& ia, const Step& step) {
        const auto count = static_cast<std::uint32_t>(inst.uses.size());
        a_.mov(S64, Mem::at(context, off::C), counter);
        store_arguments(ia, 0, count);
        const auto frame_size = f_.call_frames.find(static_cast<std::uint32_t>(step.imm));
        const bool in_place = f_.variant == t3::Variant::Standard && frame_size != f_.call_frames.end() &&
                              frame_size->second <= max_inline_frame && count <= frame_size->second;
        const auto pushed = a_.label();
        if (in_place) {
            const auto slow = a_.label();
            inline_push(step, count, frame_size->second, slow);
            a_.mov(S64, x64::rdi, context);
            a_.mov(S32, x64::rsi, imm32(step.imm));
            call_helper(off::enter);
            a_.jmp(pushed);
            a_.bind(slow);
        }
        a_.mov(S64, x64::rdi, context);
        a_.mov(S32, x64::rsi, imm32(step.index));
        a_.mov(S32, x64::rdx, imm32(step.imm));
        a_.mov(S64, x64::rcx, x64::rsp);
        a_.mov(S32, x64::r8, imm32(count));
        call_helper(off::call);
        if (in_place) a_.bind(pushed);
        check_status(Label{}, false);
        a_.call(Rm(x64::rax));
        // The callee's activation is over: the context's program is ours
        // again, and the frame arenas may have moved.
        a_.mov(S64, scratch, Mem::at(x64::rsp, static_cast<std::int32_t>(saved_program_)));
        a_.mov(S64, Mem::at(context, off::program), scratch);
        a_.alu(Alu::sub, S32, Mem::at(context, off::native_depth), Imm32{1});
        a_.mov(S64, frame, Mem::at(context, off::slots));
        a_.test(S32, x64::rcx, x64::rcx);
        a_.jcc(Cond::ne, ret_outcome_);
    }

    // ---- heap access ------------------------------------------------------------
    //
    // What the IR interpreter does for each heap operation
    // (src/nvm/tier3_interp.cpp) on the published layout (src/nvm/heap.hpp),
    // instruction for instruction; a failing check jumps to its stub.

    using H = nvm::Heap;

    // Constant data emitted after the code.
    std::vector<std::function<void()>> data_;

    // Jumps to `fail` unless entry `e` passes type test `index`: its kind
    // byte, then (unless the kind is enough) its type word against each type
    // of the set or, for a larger set, a bit of a table after the code. `t`
    // is destroyed.
    void type_test(Gpr e, std::uint32_t index, Label fail, Gpr t) {
        const auto& test = f_.type_tests.at(index);
        a_.movzx(S32, t, S8, Mem::at(e, static_cast<std::int32_t>(H::kind_offset())));
        a_.alu(Alu::cmp, S32, t, imm32(test.kind));
        a_.jcc(Cond::ne, fail);
        if (test.any_type) return;
        if (test.types.empty()) {
            a_.jmp(fail);
            return;
        }
        a_.mov(S32, t, Mem::at(e, static_cast<std::int32_t>(H::type_offset())));
        if (test.types.size() <= 4) {
            const auto passed = a_.label();
            for (std::size_t i = 0; i + 1 < test.types.size(); ++i) {
                a_.alu(Alu::cmp, S32, t, imm32(test.types[i]));
                a_.jcc(Cond::e, passed);
            }
            a_.alu(Alu::cmp, S32, t, imm32(test.types.back()));
            a_.jcc(Cond::ne, fail);
            a_.bind(passed);
            return;
        }
        const auto table = a_.label();
        const auto largest = test.types.back();
        std::vector<std::uint64_t> words(largest / 64 + 1, 0);
        for (const auto type : test.types) words[type / 64] |= std::uint64_t{1} << (type % 64);
        data_.push_back([this, table, words] {
            a_.align(8);
            a_.bind(table);
            for (const auto w : words) a_.data64(w);
        });
        a_.alu(Alu::cmp, S32, t, imm32(largest));
        a_.jcc(Cond::a, fail);
        a_.bit(Bit::bt, S64, Rm(Mem::rip_relative(table)), t);
        a_.jcc(Cond::ae, fail);
    }

    // The address of a field: after the entry's initialized words, `words`
    // of them when known, else as many as its count needs (in `scratch`).
    Mem field(Gpr e, std::uint64_t index, std::uint32_t words) {
        const auto base = H::initialized_offset() + 8 * index;
        if (words != t3::none) return Mem::at(e, displacement(base + 8ULL * words));
        a_.mov(S64, scratch, Mem::at(e, static_cast<std::int32_t>(H::count_offset())));
        a_.alu(Alu::add, S64, scratch, Imm32{63});
        a_.shift(Shift::shr, S64, scratch, Imm8{6});
        return Mem::at(e, scratch, 8, displacement(base));
    }

    // Sets bit `bit` of the word at `word`.
    void set_bit(const Mem& word, unsigned bit) {
        if (bit < 31) a_.alu(Alu::or_, S64, word, imm32(std::uint64_t{1} << bit));
        else a_.bit(Bit::bts, S64, Rm(word), Imm8{static_cast<std::uint8_t>(bit)});
    }

    static Size width_size(std::uint64_t bytes) {
        switch (bytes) {
        case 1: return S8;
        case 2: return S16;
        case 4: return S32;
        default: return S64;
        }
    }

    // Loads the element at `p` + `i` * `bytes`, zero-extended.
    void load_element(Gpr d, Gpr p, Gpr i, std::uint64_t bytes) {
        const auto at = Mem::at(p, i, static_cast<unsigned>(bytes), 0);
        if (bytes == 1 || bytes == 2) a_.movzx(S32, d, width_size(bytes), at);
        else a_.mov(width_size(bytes), d, at);
    }

    // Calls `offset`'s helper (context first) keeping every allocatable
    // caller-saved register; the helper's result is in `scratch` after it.
    void call_saving(std::int32_t offset, const std::function<void()>& arguments) {
        static constexpr Gpr saved[] = {x64::rax, x64::rcx, x64::rdx, x64::rsi, x64::rdi, x64::r8, x64::r9, x64::r10};
        for (const auto r : saved) a_.push(r);
        arguments();
        a_.mov(S64, x64::rdi, context);
        call_helper(offset);
        a_.mov(S64, scratch, x64::rax);
        for (auto i = std::size(saved); i > 0; --i) a_.pop(saved[i - 1]);
    }

    // NEURA_TIER3_VERIFY=1: the heap's consistency after an inline
    // allocation or append.
    void check_heap() {
        if (!f_.check_heap) return;
        call_saving(off::check_heap, [] {});
    }

    // The slow path of an allocation or append: the frame written back, the
    // block's step in the context, then the helper of exits[index] on the
    // argument uses, every allocatable register kept; its result into `d`
    // (when it has one), and on to `done`. A trap or an exception ends the
    // activation.
    void slow_path(const mir::InstAllocation& ia, const Step& step, std::size_t arguments, Label slow, Label done,
                   const Gpr* d, Gpr t) {
        const auto pending = pending_;
        cold_.push_back([this, ia, step, arguments, slow, done, d_reg = d ? *d : Gpr{0}, has_d = d != nullptr, t, pending] {
            a_.bind(slow);
            if (step.inlined) return inlined_slow_path(ia, step, arguments, t, pending);
            write_back_from(ia, step, arguments + step.arguments, t);
            if (pending == 0) {
                a_.mov(S64, Mem::at(context, off::C), counter);
            } else {
                a_.lea(S64, scratch, Mem::at(counter, displacement(pending)));
                a_.mov(S64, Mem::at(context, off::C), scratch);
            }
            store_arguments(ia, arguments, step.arguments);
            call_saving(off::helper, [&] {
                a_.mov(S32, x64::rsi, imm32(step.index));
                a_.lea(S64, x64::rdx, Mem::at(x64::rsp, 64));
                a_.mov(S32, x64::rcx, imm32(step.arguments));
            });
            const auto failed = a_.label();
            a_.alu(Alu::cmp, S32, Mem::at(context, off::status), Imm32{0});
            a_.jcc(Cond::ne, failed);
            if (has_d) a_.mov(S64, d_reg, scratch);
            a_.jmp(done);
            a_.bind(failed);
            a_.alu(Alu::cmp, S32, Mem::at(context, off::status), imm32(static_cast<std::uint32_t>(t3::NativeStatus::Trapped)));
            a_.jcc(Cond::e, trapped_exit_);
            a_.jmp(exception_exit_);
        });
    }

    // The slow path inside inlined code: the values of every level of the
    // state into the context's value array (as a stub stores them), C in the
    // context, then the helper that materializes the frames, runs the
    // reference operation and leaves at its resume block; the activation
    // ends.
    void inlined_slow_path(const mir::InstAllocation& ia, const Step& step, std::size_t arguments, Gpr t,
                           std::uint64_t pending) {
        a_.mov(S64, t, Mem::at(context, off::values));
        const auto at = [&](std::uint32_t value) { return Mem::at(t, displacement(8ULL * value)); };
        for (const auto& [use, value] : step.state_values) {
            const auto& location = ia.state_uses.at(use);
            if (in_register(location)) {
                a_.mov(S64, at(value), gpr(location));
            } else {
                a_.mov(S64, scratch, memory(location));
                a_.mov(S64, at(value), scratch);
            }
        }
        for (const auto& [value, bits] : step.state_constants) {
            if (simm32(bits)) {
                a_.mov(S64, at(value), imm32(bits));
            } else {
                a_.mov_constant(scratch, bits);
                a_.mov(S64, at(value), scratch);
            }
        }
        if (pending == 0) {
            a_.mov(S64, Mem::at(context, off::C), counter);
        } else {
            a_.lea(S64, scratch, Mem::at(counter, displacement(pending)));
            a_.mov(S64, Mem::at(context, off::C), scratch);
        }
        store_arguments(ia, arguments, step.arguments);
        a_.mov(S64, x64::rdi, context);
        a_.mov(S32, x64::rsi, imm32(step.index));
        a_.mov(S64, x64::rdx, x64::rsp);
        a_.mov(S32, x64::rcx, imm32(step.arguments));
        a_.mov(S32, x64::r8, imm32(step.state));
        call_helper(nvm::tier3::offsets::inlined_helper);
        a_.alu(Alu::cmp, S32, Mem::at(context, off::status), Imm32{0});
        a_.jcc(Cond::e, left_exit_);
        a_.alu(Alu::cmp, S32, Mem::at(context, off::status), imm32(static_cast<std::uint32_t>(t3::NativeStatus::Trapped)));
        a_.jcc(Cond::e, trapped_exit_);
        a_.jmp(exception_exit_);
    }

    // Jumps to `slow` unless `cells` cells fit below the collection
    // threshold: live <= next and cells <= next - live. Leaves live in `t`.
    void cells_fit(std::uint64_t cells, Gpr t, Label slow) {
        a_.mov(S64, scratch, Mem::at(context, nvm::tier3::offsets::live_cells));
        a_.mov(S64, t, Mem::at(scratch));
        a_.mov(S64, scratch, Mem::at(context, nvm::tier3::offsets::next_collection));
        a_.mov(S64, scratch, Mem::at(scratch));
        a_.alu(Alu::sub, S64, scratch, t);
        a_.jcc(Cond::b, slow);
        if (simm32(cells)) {
            a_.alu(Alu::cmp, S64, scratch, imm32(cells));
        } else {
            a_.push(t);
            a_.mov_constant(t, cells);
            a_.alu(Alu::cmp, S64, scratch, t);
            a_.pop(t);
        }
        a_.jcc(Cond::b, slow);
    }

    // live += cells (live is in `t`), and the peak.
    void count_cells(std::uint64_t cells, Gpr t) {
        if (simm32(cells)) {
            a_.alu(Alu::add, S64, t, imm32(cells));
        } else {
            a_.mov_constant(scratch, cells);
            a_.alu(Alu::add, S64, t, scratch);
        }
        a_.mov(S64, scratch, Mem::at(context, nvm::tier3::offsets::live_cells));
        a_.mov(S64, Mem::at(scratch), t);
        a_.mov(S64, scratch, Mem::at(context, nvm::tier3::offsets::peak_cells));
        const auto kept = a_.label();
        a_.alu(Alu::cmp, S64, Mem::at(scratch), t);
        a_.jcc(Cond::ae, kept);
        a_.mov(S64, Mem::at(scratch), t);
        a_.bind(kept);
    }

    void heap(const mir::Inst& inst, const mir::InstAllocation& ia, const Step& step) {
        namespace o3 = nvm::tier3::offsets;
        switch (step.kind) {
        case Kind::Lookup: {
            const auto h = gpr(ia.uses[0]);
            const auto d = gpr(ia.defs[0]);
            const auto t = Gpr{ia.temps_gpr.at(0)};
            const auto fail = stub(inst, ia);
            const auto done = a_.label();
            if ((step.imm & 1U) != 0) {
                const auto handle = a_.label();
                a_.test(S64, h, h);
                a_.jcc(Cond::ne, handle);
                a_.alu(Alu::xor_, S32, d, d);
                a_.jmp(done);
                a_.bind(handle);
            }
            a_.mov(S32, t, h);
            a_.alu(Alu::and_, S32, t, imm32(H::handle_page_mask()));
            a_.mov(S64, scratch, h);
            a_.shift(Shift::shr, S64, scratch, Imm8{static_cast<std::uint8_t>(H::handle_page_bits())});
            a_.mov(S64, d, Mem::at(context, o3::handle_table));
            a_.alu(Alu::cmp, S64, scratch, Mem::at(d, static_cast<std::int32_t>(offsetof(H::HandleTable, count))));
            a_.jcc(Cond::ae, fail);
            a_.mov(S64, d, Mem::at(d, static_cast<std::int32_t>(offsetof(H::HandleTable, pages))));
            a_.mov(S64, d, Mem::at(d, scratch, 8, 0));
            a_.test(S64, d, d);
            a_.jcc(Cond::e, fail);
            a_.mov(S64, d, Mem::at(d, t, 8, static_cast<std::int32_t>(H::page_entries_offset())));
            a_.test(S64, d, d);
            a_.jcc(Cond::e, fail);
            a_.bind(done);
            return;
        }
        case Kind::CheckNotNil: {
            const auto h = gpr(ia.uses[0]);
            a_.test(S64, h, h);
            a_.jcc(Cond::e, stub(inst, ia));
            return;
        }
        case Kind::CheckKind: {
            const auto e = gpr(ia.uses[0]);
            const auto fail = stub(inst, ia);
            const auto passed = a_.label();
            if (step.checked) {
                a_.test(S64, e, e);
                a_.jcc(Cond::e, passed);
            }
            type_test(e, step.index, fail, scratch);
            a_.bind(passed);
            return;
        }
        case Kind::CheckInit: {
            const auto e = gpr(ia.uses[0]);
            const auto word = Mem::at(e, displacement(H::initialized_offset() + 8 * (step.imm / 64)));
            const auto bit = static_cast<unsigned>(step.imm % 64);
            if (bit < 31) {
                a_.test(S64, word, imm32(std::uint64_t{1} << bit));
                a_.jcc(Cond::e, stub(inst, ia));
            } else {
                a_.bit(Bit::bt, S64, Rm(word), Imm8{static_cast<std::uint8_t>(bit)});
                a_.jcc(Cond::ae, stub(inst, ia));
            }
            return;
        }
        case Kind::CheckIndex: {
            const auto i = gpr(ia.uses[0]);
            if (step.b_constant) {
                a_.alu(Alu::cmp, S64, i, imm32(step.b_value));
            } else if (in_register(ia.uses[1])) {
                a_.alu(Alu::cmp, S64, i, gpr(ia.uses[1]));
            } else {
                a_.alu(Alu::cmp, S64, i, memory(ia.uses[1]));
            }
            a_.jcc(Cond::ae, stub(inst, ia));
            return;
        }
        case Kind::CheckGlobalInit: {
            a_.mov(S64, scratch, Mem::at(context, o3::global_initialized));
            a_.movzx(S32, scratch, S8, Mem::at(scratch, displacement(step.imm)));
            a_.test(S32, scratch, scratch);
            a_.jcc(Cond::e, stub(inst, ia));
            return;
        }
        case Kind::TestKind: {
            const auto e = gpr(ia.uses[0]);
            const auto d = gpr(ia.defs[0]);
            const auto t = Gpr{ia.temps_gpr.at(0)};
            const auto done = a_.label();
            a_.alu(Alu::xor_, S32, t, t);
            a_.test(S64, e, e);
            a_.jcc(Cond::e, done);
            type_test(e, step.index, done, scratch);
            a_.mov(S32, t, Imm32{1});
            a_.bind(done);
            a_.mov(S64, d, t);
            return;
        }
        case Kind::LoadTag:
            a_.mov(S32, gpr(ia.defs[0]), Mem::at(gpr(ia.uses[0]), static_cast<std::int32_t>(H::type_offset())));
            return;
        case Kind::LoadField: {
            const auto at = field(gpr(ia.uses[0]), step.imm, step.index);
            a_.mov(S64, gpr(ia.defs[0]), at);
            return;
        }
        case Kind::LoadLength:
            a_.mov(S64, gpr(ia.defs[0]), Mem::at(gpr(ia.uses[0]), static_cast<std::int32_t>(H::length_offset())));
            return;
        case Kind::LoadData:
            a_.mov(S64, gpr(ia.defs[0]), Mem::at(gpr(ia.uses[0]), static_cast<std::int32_t>(H::data_offset())));
            return;
        case Kind::LoadStorage:
            a_.movzx(S32, gpr(ia.defs[0]), S8, Mem::at(gpr(ia.uses[0]), static_cast<std::int32_t>(H::storage_width_offset())));
            return;
        case Kind::LoadVersion:
            a_.mov(S64, gpr(ia.defs[0]), Mem::at(gpr(ia.uses[0]), static_cast<std::int32_t>(H::list_version_offset())));
            return;
        case Kind::LoadElement:
        case Kind::StoreElement: {
            const auto p = gpr(ia.uses[0]);
            const auto i = gpr(ia.uses[1]);
            const bool load = step.kind == Kind::LoadElement;
            const auto access = [&](std::uint64_t bytes) {
                if (load) load_element(gpr(ia.defs[0]), p, i, bytes);
                else a_.mov(width_size(bytes), Mem::at(p, i, static_cast<unsigned>(bytes), 0), gpr(ia.uses[3]));
            };
            if (step.imm == 8) {
                access(8);
                return;
            }
            // The storage is the declared width, or eight bytes once widened.
            const auto wide = a_.label();
            const auto done = a_.label();
            if (in_register(ia.uses[2])) a_.alu(Alu::cmp, S64, gpr(ia.uses[2]), imm32(step.imm));
            else a_.alu(Alu::cmp, S64, memory(ia.uses[2]), imm32(step.imm));
            a_.jcc(Cond::ne, wide);
            access(step.imm);
            a_.jmp(done);
            a_.bind(wide);
            access(8);
            a_.bind(done);
            return;
        }
        case Kind::LoadGlobal:
            a_.mov(S64, scratch, Mem::at(context, o3::globals));
            a_.mov(S64, gpr(ia.defs[0]), Mem::at(scratch, displacement(8 * step.imm)));
            return;
        case Kind::StoreGlobal:
            a_.mov(S64, scratch, Mem::at(context, o3::globals));
            a_.mov(S64, Mem::at(scratch, displacement(8 * step.imm)), gpr(ia.uses[0]));
            a_.mov(S64, scratch, Mem::at(context, o3::global_initialized));
            a_.mov(S8, Mem::at(scratch, displacement(step.imm)), Imm32{1});
            return;
        case Kind::StoreField: {
            const auto e = gpr(ia.uses[0]);
            a_.mov(S64, field(e, step.imm, step.index), gpr(ia.uses[1]));
            set_bit(Mem::at(e, displacement(H::initialized_offset() + 8 * (step.imm / 64))), static_cast<unsigned>(step.imm % 64));
            return;
        }
        case Kind::Select: {
            const auto index = gpr(ia.uses[0]);
            const auto d = gpr(ia.defs[0]);
            const auto done = a_.label();
            for (std::uint64_t k = 0; k + 1 < step.imm; ++k) {
                const auto next = a_.label();
                a_.alu(Alu::cmp, S64, index, imm32(k));
                a_.jcc(Cond::ne, next);
                load(d, ia.uses[1 + k]);
                a_.jmp(done);
                a_.bind(next);
            }
            load(d, ia.uses[step.imm]);
            a_.bind(done);
            return;
        }
        case Kind::Widen:
            a_.mov(S64, x64::rdi, context);
            call_helper(o3::set_element);
            check_status(Label{}, false);
            return;
        case Kind::AllocObject: return allocate(ia, step);
        case Kind::Append: return append(ia, step);
        default: break;
        }
        throw EmitError("an unknown machine instruction");
    }

    // AllocObject (design 6.7): inline when the pool is in use and the
    // reference would allocate without a collection, a limit, a new window,
    // a new page or a new chunk; every condition is decided before the first
    // write. What it writes is what Blocks::allocate, the object's
    // initialization, store_new() and the executor's count write.
    void allocate(const mir::InstAllocation& ia, const Step& step) {
        namespace o3 = nvm::tier3::offsets;
        const auto d = gpr(ia.defs[0]);
        const Gpr words{ia.temps_gpr.at(0)}, h{ia.temps_gpr.at(1)}, t{ia.temps_gpr.at(2)}, page{ia.temps_gpr.at(3)};
        const auto fields = step.imm;
        const auto layout = std::uint64_t{f_.exits.at(step.index)->immediate};
        const auto slow = a_.label();
        const auto done = a_.label();
        const auto bytes = H::object_block_bytes(static_cast<std::size_t>(fields));
        slow_path(ia, step, 0, slow, done, &d, words);
        if (bytes > H::allocator_largest()) {
            a_.jmp(slow);
            a_.bind(done);
            return;
        }
        const auto c = H::allocator_class_of(bytes);
        const auto size = H::allocator_class_size(c);
        const auto free_at = Mem::at(words, static_cast<std::int32_t>(offsetof(H::NativeAllocator, free) + 8 * c));
        const auto next_at = Mem::at(words, static_cast<std::int32_t>(offsetof(H::NativeAllocator, next) + 8 * c));
        const auto end_at = Mem::at(words, static_cast<std::int32_t>(offsetof(H::NativeAllocator, end) + 8 * c));
        const auto last_at = Mem::at(words, static_cast<std::int32_t>(offsetof(H::NativeAllocator, last)));
        a_.alu(Alu::cmp, S64, Mem::at(context, o3::fast_allocation), Imm32{0});
        a_.jcc(Cond::e, slow);
        a_.mov(S64, words, Mem::at(context, o3::allocator));
        a_.mov(S64, h, last_at);
        a_.alu(Alu::cmp, S64, h, Mem::at(context, o3::max_handles));
        a_.jcc(Cond::ae, slow);
        cells_fit(1 + fields, t, slow);
        // The page of the handle exists.
        a_.alu(Alu::add, S64, h, Imm32{1});
        a_.mov(S64, page, h);
        a_.shift(Shift::shr, S64, page, Imm8{static_cast<std::uint8_t>(H::handle_page_bits())});
        a_.mov(S64, scratch, Mem::at(context, o3::handle_table));
        a_.alu(Alu::cmp, S64, page, Mem::at(scratch, static_cast<std::int32_t>(offsetof(H::HandleTable, count))));
        a_.jcc(Cond::ae, slow);
        a_.mov(S64, scratch, Mem::at(scratch, static_cast<std::int32_t>(offsetof(H::HandleTable, pages))));
        a_.mov(S64, page, Mem::at(scratch, page, 8, 0));
        a_.test(S64, page, page);
        a_.jcc(Cond::e, slow);
        // A free block of the class, or room in its chunk.
        const auto pop = a_.label();
        const auto have = a_.label();
        a_.mov(S64, scratch, free_at);
        a_.test(S64, scratch, scratch);
        a_.jcc(Cond::ne, pop);
        a_.mov(S64, scratch, end_at);
        a_.alu(Alu::sub, S64, scratch, next_at);
        a_.alu(Alu::cmp, S64, scratch, imm32(size));
        a_.jcc(Cond::b, slow);
        a_.mov(S64, scratch, next_at);
        a_.alu(Alu::add, S64, next_at, imm32(size));
        a_.jmp(have);
        a_.bind(pop);
        a_.mov(S64, t, Mem::at(scratch));
        a_.mov(S64, free_at, t);
        a_.bind(have);
        // The object: header, identity 0, no field initialized, every field 0.
        a_.mov_constant(t, std::uint64_t{0x08080000} | (layout << 32));
        a_.mov(S64, Mem::at(scratch, 0), t);
        if (simm32(fields)) {
            a_.mov(S64, Mem::at(scratch, static_cast<std::int32_t>(H::count_offset())), imm32(fields));
        } else {
            a_.mov_constant(t, fields);
            a_.mov(S64, Mem::at(scratch, static_cast<std::int32_t>(H::count_offset())), t);
        }
        a_.mov(S64, Mem::at(scratch, static_cast<std::int32_t>(H::identity_offset())), Imm32{0});
        const auto zeroed = (fields + 63) / 64 + fields;
        for (std::uint64_t k = 0; k < zeroed; ++k) {
            a_.mov(S64, Mem::at(scratch, displacement(H::initialized_offset() + 8 * k)), Imm32{0});
        }
        // Its handle: the page's entry, its live count, the last handle.
        a_.mov(S32, t, h);
        a_.alu(Alu::and_, S32, t, imm32(H::handle_page_mask()));
        a_.mov(S64, Mem::at(page, t, 8, static_cast<std::int32_t>(H::page_entries_offset())), scratch);
        a_.alu(Alu::add, S64, Mem::at(page, static_cast<std::int32_t>(H::page_live_offset())), Imm32{1});
        a_.mov(S64, last_at, h);
        // The cells.
        a_.mov(S64, scratch, Mem::at(context, o3::live_cells));
        a_.mov(S64, t, Mem::at(scratch));
        count_cells(1 + fields, t);
        a_.alu(Alu::add, S64, Mem::at(context, o3::fast_allocations), Imm32{1});
        check_heap();
        a_.mov(S64, d, h);
        a_.bind(done);
    }

    // Append (design 6.8): inline when the pool is in use, the cell fits
    // below the collection threshold, the value fits the storage and the
    // list has room; then what append() and the count write.
    void append(const mir::InstAllocation& ia, const Step& step) {
        namespace o3 = nvm::tier3::offsets;
        const auto e = gpr(ia.uses[0]);
        const auto v = gpr(ia.uses[1]);
        const Gpr live{ia.temps_gpr.at(0)}, storage{ia.temps_gpr.at(1)}, count{ia.temps_gpr.at(2)};
        const auto declared = f_.insts[step.inst].imm;
        const auto slow = a_.label();
        const auto done = a_.label();
        slow_path(ia, step, 2, slow, done, nullptr, live);
        a_.alu(Alu::cmp, S64, Mem::at(context, o3::fast_allocation), Imm32{0});
        a_.jcc(Cond::e, slow);
        cells_fit(1, live, slow);
        a_.movzx(S32, storage, S8, Mem::at(e, static_cast<std::int32_t>(H::storage_width_offset())));
        if (step.checked) {
            // The value fits the storage: no bit above it.
            const auto fits = a_.label();
            a_.alu(Alu::cmp, S32, storage, Imm32{8});
            a_.jcc(Cond::e, fits);
            for (const std::uint64_t w : {1, 2}) {
                const auto other = a_.label();
                a_.alu(Alu::cmp, S32, storage, imm32(w));
                a_.jcc(Cond::ne, other);
                a_.alu(Alu::cmp, S64, v, imm32((std::uint64_t{1} << (8 * w)) - 1));
                a_.jcc(Cond::a, slow);
                a_.jmp(fits);
                a_.bind(other);
            }
            a_.mov(S64, scratch, v);
            a_.shift(Shift::shr, S64, scratch, Imm8{32});
            a_.jcc(Cond::ne, slow);
            a_.bind(fits);
        }
        a_.mov(S64, count, Mem::at(e, static_cast<std::int32_t>(H::count_offset())));
        a_.alu(Alu::cmp, S64, count, Mem::at(e, static_cast<std::int32_t>(H::list_capacity_offset())));
        a_.jcc(Cond::ae, slow);
        a_.mov(S64, scratch, Mem::at(e, static_cast<std::int32_t>(H::data_offset())));
        const auto store = [&](std::uint64_t bytes) {
            a_.mov(width_size(bytes), Mem::at(scratch, count, static_cast<unsigned>(bytes), 0), v);
        };
        if (declared == 8) {
            store(8);
        } else {
            const auto wide = a_.label();
            const auto stored = a_.label();
            a_.alu(Alu::cmp, S32, storage, imm32(declared));
            a_.jcc(Cond::ne, wide);
            store(declared);
            a_.jmp(stored);
            a_.bind(wide);
            store(8);
            a_.bind(stored);
        }
        a_.alu(Alu::add, S64, count, Imm32{1});
        a_.mov(S64, Mem::at(e, static_cast<std::int32_t>(H::count_offset())), count);
        a_.alu(Alu::add, S64, Mem::at(e, static_cast<std::int32_t>(H::list_version_offset())), Imm32{1});
        count_cells(1, live);
        a_.alu(Alu::add, S64, Mem::at(context, o3::fast_appends), Imm32{1});
        check_heap();
        a_.bind(done);
    }

    // ---- lifted operations ------------------------------------------------------

    struct Operands {
        Gpr a{0}, b{0}, d{0};
        bool has_a = false, has_b = false, has_d = false;
        std::vector<Gpr> temps;
        std::vector<Xmm> xmm_temps;
    };

    Operands operands(const LiftShape& shape, const mir::InstAllocation& ia) const {
        using U = LiftShape::Use;
        Operands o;
        std::size_t u = 0;
        if (shape.a != U::None && shape.a != U::Immediate) {
            o.a = gpr(ia.uses.at(u++));
            o.has_a = true;
        }
        if (shape.b != U::None && shape.b != U::Immediate) {
            o.b = gpr(ia.uses.at(u++));
            o.has_b = true;
        }
        if (!ia.defs.empty()) {
            o.d = gpr(ia.defs[0]);
            o.has_d = true;
        }
        for (const auto t : ia.temps_gpr) o.temps.push_back(Gpr{t});
        for (const auto t : ia.temps_xmm) o.xmm_temps.push_back(Xmm{t});
        return o;
    }

    // `op` d, b-or-immediate at `size`.
    void alu_b(Alu op, Size size, Gpr d, const Operands& o, const Step& step) {
        if (o.has_b) a_.alu(op, size, d, o.b);
        else a_.alu(op, size, d, imm32(step.b_value));
    }

    // r11 = b << k (b fits its width, so no bit is lost).
    void shifted_b(const Operands& o, const Step& step, unsigned k) {
        if (o.has_b) {
            a_.mov(S64, scratch, o.b);
            a_.shift(Shift::shl, S64, scratch, Imm8{static_cast<std::uint8_t>(k)});
        } else {
            a_.mov_constant(scratch, step.b_value << k);
        }
    }

    void lift(const mir::Inst& inst, const mir::InstAllocation& ia, const Step& step) {
        const auto& ir = f_.insts[step.inst];
        const auto& in = ir.lifted;
        const auto shape = lift_shape(in, step);
        const auto o = operands(shape, ia);
        const bool checked = (ir.flags & t3::Checked) != 0;
        Label fail_label;
        bool have_fail = false;
        const auto fail = [&]() -> Label {
            if (!checked) return unreachable();
            if (!have_fail) {
                fail_label = stub(inst, ia);
                have_fail = true;
            }
            return fail_label;
        };
        if (shape.evaluate) {
            Label label;
            if (checked) label = fail();
            call_evaluate(step.inst, o.has_a ? &o.a : nullptr, o.has_b ? &o.b : nullptr, o.d, checked ? &label : nullptr);
            return;
        }
        if (lift::float_opcode(in.op)) return float_op(in, step, o, fail);
        const unsigned w = in.width;
        const bool signed_op = in.is_signed;
        const bool fits_a = step.a_width <= w;
        const bool fits_b = step.b_width <= w;
        const bool fits = fits_a && (lift_arity(in) < 2 || fits_b);
        const auto d = o.d;
        if (!checked) {
            // lift::evaluate's result where it reports no failure, which the
            // passes proved; the other unchecked operations take their
            // checked code below, whose failure branches go to unreachable().
            switch (in.op) {
            case Opcode::Multiply:
                if (o.has_b) a_.imul(S64, d, o.b);
                else a_.imul(S64, d, d, imm32(step.b_value));
                mask(d, w);
                return;
            case Opcode::Negate:
                a_.neg(S64, d);
                mask(d, w);
                return;
            case Opcode::Index: return;
            case Opcode::ConvertInteger:
                if (signed_op) sign_extend(d, w, fits_a);
                mask(d, in.to_width);
                return;
            default: break;
            }
        }
        switch (in.op) {
        case Opcode::Constant: a_.mov_constant(d, in.immediate); return;
        case Opcode::Add:
        case Opcode::Subtract: {
            const bool add = in.op == Opcode::Add;
            const auto op = add ? Alu::add : Alu::sub;
            if (w == 64 || (w == 32 && fits)) {
                alu_b(op, w == 64 ? S64 : S32, d, o, step);
                a_.jcc(signed_op ? Cond::o : Cond::b, fail());
                return;
            }
            if (!signed_op) {
                alu_b(op, S64, d, o, step);
                if (!add) {
                    // a < b, as 64-bit values.
                    a_.jcc(Cond::b, fail());
                    mask(d, w);
                } else if (fits) {
                    // a + b < 2^(w + 1): it fails when bit w is set.
                    a_.bit(Bit::bt, S64, d, Imm8{static_cast<std::uint8_t>(w)});
                    a_.jcc(Cond::b, fail());
                } else {
                    compare_constant(d, lift::mask_of(w));
                    a_.jcc(Cond::a, fail());
                }
                return;
            }
            if (fits) {
                // In the top w bits, the processor's overflow is the range
                // test of the width.
                const unsigned k = 64 - w;
                a_.shift(Shift::shl, S64, d, Imm8{static_cast<std::uint8_t>(k)});
                shifted_b(o, step, k);
                a_.alu(op, S64, d, scratch);
                a_.jcc(Cond::o, fail());
                a_.shift(Shift::shr, S64, d, Imm8{static_cast<std::uint8_t>(k)});
                return;
            }
            // evaluate's expressions on any operand values.
            const auto t0 = o.temps.at(0);
            const auto t1 = o.temps.at(1);
            a_.mov(S64, t0, o.a);
            sign_extend_any(t0, w);
            a_.mov(S64, t1, o.b);
            sign_extend_any(t1, w);
            a_.alu(op, S64, t0, t1);
            if (add) a_.jcc(Cond::o, fail());
            a_.mov(S64, t1, o.a);
            a_.alu(op, S64, t1, o.b);
            mask(t1, w);
            range(t0, lift::signed_min(w), lift::signed_max(w), fail());
            a_.mov(S64, d, t1);
            return;
        }
        case Opcode::AddWrapping:
        case Opcode::SubtractWrapping:
            alu_b(in.op == Opcode::AddWrapping ? Alu::add : Alu::sub, w <= 32 ? S32 : S64, d, o, step);
            if (w != 32) mask(d, w);
            return;
        case Opcode::Negate:
            if (!signed_op) {
                a_.test(S64, o.a, o.a);
                a_.jcc(Cond::ne, fail());
                a_.alu(Alu::xor_, S32, d, d);
                return;
            }
            if (w == 64 || (w == 32 && fits_a)) {
                a_.neg(w == 64 ? S64 : S32, d);
                a_.jcc(Cond::o, fail());
                return;
            }
            compare_constant(d, std::uint64_t{1} << (w - 1));
            a_.jcc(Cond::e, fail());
            a_.neg(S64, d);
            mask(d, w);
            return;
        case Opcode::Multiply:
            if (!signed_op && w == 64) {
                // rdx:rax = rax * b; it fails when rdx is not zero.
                a_.mul(S64, o.b);
                a_.jcc(Cond::o, fail());
                return;
            }
            if (!signed_op) {
                // The wrapping 64-bit product, which fails above the mask.
                if (o.has_b) a_.imul(S64, d, o.b);
                else a_.imul(S64, d, d, imm32(step.b_value));
                a_.mov(S64, scratch, d);
                a_.shift(Shift::shr, S64, scratch, Imm8{static_cast<std::uint8_t>(w)});
                a_.jcc(Cond::ne, fail());
                return;
            }
            if (w == 64 || (w == 32 && fits)) {
                const auto size = w == 64 ? S64 : S32;
                if (o.has_b) a_.imul(size, d, o.b);
                else a_.imul(size, d, d, imm32(step.b_value));
                a_.jcc(Cond::o, fail());
                return;
            }
            if (fits) {
                // (x << k) * y overflows exactly when x * y is outside the
                // width's range; its top w bits are the result.
                const unsigned k = 64 - w;
                a_.shift(Shift::shl, S64, d, Imm8{static_cast<std::uint8_t>(k)});
                if (o.has_b) {
                    a_.mov(S64, scratch, o.b);
                    sign_extend_fitting(scratch, w);
                } else {
                    a_.mov_constant(scratch, static_cast<std::uint64_t>(lift::sign_extend(step.b_value, w)));
                }
                a_.imul(S64, d, scratch);
                a_.jcc(Cond::o, fail());
                a_.shift(Shift::shr, S64, d, Imm8{static_cast<std::uint8_t>(k)});
                return;
            }
            {
                const auto t0 = o.temps.at(0);
                const auto t1 = o.temps.at(1);
                a_.mov(S64, t0, o.a);
                sign_extend_any(t0, w);
                a_.mov(S64, t1, o.b);
                sign_extend_any(t1, w);
                a_.imul(S64, t0, t1);
                a_.jcc(Cond::o, fail());
                a_.mov(S64, t1, o.a);
                a_.imul(S64, t1, o.b);
                mask(t1, w);
                range(t0, lift::signed_min(w), lift::signed_max(w), fail());
                a_.mov(S64, d, t1);
            }
            return;
        case Opcode::Divide:
        case Opcode::Remainder:
            if (!o.has_b) return constant_division(in, step, o, fail);
            return division(in, step, fail);
        case Opcode::Equal:
        case Opcode::NotEqual:
        case Opcode::Less:
        case Opcode::LessEqual:
        case Opcode::Greater:
        case Opcode::GreaterEqual: {
            const bool zero_first = d != o.a && !(o.has_b && d == o.b);
            if (zero_first) a_.alu(Alu::xor_, S32, d, d);
            const auto cc = compare(in, step, shape, ia);
            a_.setcc(cc, d);
            if (!zero_first) a_.movzx(S32, d, S8, d);
            return;
        }
        case Opcode::And:
        case Opcode::Or:
            a_.alu(in.op == Opcode::And ? Alu::and_ : Alu::or_, S64, d, o.b);
            if (step.a_width > 1 || step.b_width > 1) a_.alu(Alu::and_, S32, d, Imm32{1});
            return;
        case Opcode::Not:
            a_.alu(Alu::xor_, S64, d, Imm32{1});
            if (step.a_width > 1) a_.alu(Alu::and_, S32, d, Imm32{1});
            return;
        case Opcode::BitAnd:
            if (!o.has_b && !simm32(step.b_value)) a_.alu(Alu::and_, S32, d, imm32(step.b_value));
            else alu_b(Alu::and_, S64, d, o, step);
            return;
        case Opcode::BitOr: alu_b(Alu::or_, S64, d, o, step); return;
        case Opcode::BitXor: alu_b(Alu::xor_, S64, d, o, step); return;
        case Opcode::BitNot:
            if (w == 32) {
                a_.not_(S32, d);
            } else {
                a_.not_(S64, d);
                mask(d, w);
            }
            return;
        case Opcode::ShiftLeft:
        case Opcode::ShiftRight: return shift(in, step, o, fail);
        case Opcode::Index:
            if (signed_op) {
                a_.bit(Bit::bt, S64, d, Imm8{static_cast<std::uint8_t>(w - 1)});
                a_.jcc(Cond::b, fail());
            }
            return;
        case Opcode::ConvertInteger: {
            const unsigned to = in.to_width;
            if (!signed_op) {
                const auto limit = in.to_signed ? static_cast<std::uint64_t>(lift::signed_max(to)) : lift::mask_of(to);
                if (limit != ~std::uint64_t{0}) {
                    compare_constant(d, limit);
                    a_.jcc(Cond::a, fail());
                }
                return;
            }
            sign_extend(d, w, fits_a);
            if (in.to_signed) {
                if (to < 64) range(d, lift::signed_min(to), lift::signed_max(to), fail());
            } else {
                a_.test(S64, d, d);
                a_.jcc(Cond::s, fail());
                if (to < 64) {
                    compare_constant(d, lift::mask_of(to));
                    a_.jcc(Cond::a, fail());
                }
            }
            mask(d, to);
            return;
        }
        case Opcode::ValueHash:
            if (w == 1) {
                a_.test(S8, d, Imm32{1});
                a_.mov(S32, d, Imm32{1237});
                a_.mov(S32, scratch, Imm32{1231});
                a_.cmov(Cond::ne, S32, d, scratch);
            } else if (w >= 64) {
                a_.mov(S64, scratch, d);
                a_.shift(Shift::shr, S64, scratch, Imm8{32});
                a_.alu(Alu::xor_, S64, d, scratch);
                a_.mov(S32, d, d);
            } else if (signed_op) {
                sign_extend(d, w, fits_a);
                a_.mov(S32, d, d);
            } else {
                mask(d, w);
            }
            return;
        default: break;
        }
        throw EmitError("an integer operation the code generator does not compile");
    }

    // Sets the flags for a compare and returns the condition that holds when
    // it is true.
    Cond compare(const lift::Instruction& in, const Step& step, const LiftShape&, const mir::InstAllocation& ia) {
        const auto shape = lift_shape(in, step);
        const auto o = operands(shape, ia);
        const unsigned w = in.width;
        const bool signed_op = in.is_signed && in.op != Opcode::Equal && in.op != Opcode::NotEqual;
        const bool fits = step.a_width <= w && step.b_width <= w;
        Cond cc = Cond::e;
        switch (in.op) {
        case Opcode::Equal: cc = Cond::e; break;
        case Opcode::NotEqual: cc = Cond::ne; break;
        case Opcode::Less: cc = signed_op ? Cond::l : Cond::b; break;
        case Opcode::LessEqual: cc = signed_op ? Cond::le : Cond::be; break;
        case Opcode::Greater: cc = signed_op ? Cond::g : Cond::a; break;
        case Opcode::GreaterEqual: cc = signed_op ? Cond::ge : Cond::ae; break;
        default: throw EmitError("a compare of another operation");
        }
        if (!signed_op || w >= 64) {
            if (o.has_b) a_.alu(Alu::cmp, S64, o.a, o.b);
            else a_.alu(Alu::cmp, S64, o.a, imm32(step.b_value));
        } else if ((w == 32 || w == 16 || w == 8) && fits) {
            const auto size = w == 32 ? S32 : w == 16 ? S16 : S8;
            if (o.has_b) a_.alu(Alu::cmp, size, o.a, o.b);
            else a_.alu(Alu::cmp, size, o.a, imm32(step.b_value));
        } else if (fits) {
            const unsigned k = 64 - w;
            const auto t = o.temps.at(0);
            a_.mov(S64, t, o.a);
            a_.shift(Shift::shl, S64, t, Imm8{static_cast<std::uint8_t>(k)});
            shifted_b(o, step, k);
            a_.alu(Alu::cmp, S64, t, scratch);
        } else {
            const auto t0 = o.temps.at(0);
            const auto t1 = o.temps.at(1);
            a_.mov(S64, t0, o.a);
            sign_extend_any(t0, w);
            a_.mov(S64, t1, o.b);
            sign_extend_any(t1, w);
            a_.alu(Alu::cmp, S64, t0, t1);
        }
        return cc;
    }

    // Division and remainder as lift::evaluate computes them: a zero
    // divisor fails; unsigned, the whole values; signed, the sign-extended
    // values, with a divisor of -1 as negation (MIN / -1 fails, a remainder
    // by -1 is 0), masked. a is in rax, b in rcx.
    template <typename Fail>
    void division(const lift::Instruction& in, const Step& step, const Fail& fail) {
        const bool remainder = in.op == Opcode::Remainder;
        const unsigned w = in.width;
        const auto rax = x64::rax;
        const auto rcx = x64::rcx;
        const auto rdx = x64::rdx;
        a_.test(S64, rcx, rcx);
        a_.jcc(Cond::e, fail());
        if (!in.is_signed) {
            a_.alu(Alu::xor_, S32, rdx, rdx);
            a_.div(S64, rcx);
            return;
        }
        sign_extend(rax, w, step.a_width <= w);
        sign_extend(rcx, w, step.b_width <= w);
        const auto minus_one = a_.label();
        const auto done = a_.label();
        a_.alu(Alu::cmp, S64, rcx, Imm32{-1});
        a_.jcc(Cond::e, minus_one);
        a_.cqo();
        a_.idiv(S64, rcx);
        mask(remainder ? rdx : rax, w);
        a_.jmp(done);
        a_.bind(minus_one);
        if (remainder) {
            a_.alu(Alu::xor_, S32, rdx, rdx);
        } else {
            compare_constant(rax, static_cast<std::uint64_t>(lift::signed_min(w)));
            a_.jcc(Cond::e, fail());
            a_.neg(S64, rax);
            mask(rax, w);
        }
        a_.bind(done);
    }

    // Division and remainder by a constant (constant_divisor), as division()
    // computes them: by zero, a failure; unsigned by 2^k, a shift and a mask
    // of the whole value; signed, of x = sign_extend(a): by 1, x masked (and
    // a remainder of 0); by -1, the negation (MIN fails); by 2^k, the
    // quotient truncated toward zero, (x + ((x >> 63) >>> (64 - k))) >> k,
    // and the remainder x minus the quotient times 2^k, masked.
    template <typename Fail>
    void constant_division(const lift::Instruction& in, const Step& step, const Operands& o, const Fail& fail) {
        const bool remainder = in.op == Opcode::Remainder;
        const unsigned w = in.width;
        const auto d = o.d;
        const auto v = step.b_value;
        if (v == 0) {
            a_.jmp(fail());
            return;
        }
        const auto log2 = [](std::uint64_t power) {
            unsigned k = 0;
            while ((std::uint64_t{1} << k) != power) ++k;
            return k;
        };
        if (!in.is_signed) {
            const auto k = log2(v);
            if (remainder) {
                if (k == 0) a_.alu(Alu::xor_, S32, d, d);
                else if (k < 32) a_.alu(Alu::and_, S32, d, imm32(v - 1));
                else if (k < 64) mask(d, k);
            } else if (k != 0) {
                a_.shift(Shift::shr, S64, d, Imm8{static_cast<std::uint8_t>(k)});
            }
            return;
        }
        sign_extend(d, w, step.a_width <= w);
        const auto y = lift::sign_extend(v, w);
        if (y == 1) {
            if (remainder) a_.alu(Alu::xor_, S32, d, d);
            else mask(d, w);
            return;
        }
        if (y == -1) {
            if (remainder) {
                a_.alu(Alu::xor_, S32, d, d);
                return;
            }
            compare_constant(d, static_cast<std::uint64_t>(lift::signed_min(w)));
            a_.jcc(Cond::e, fail());
            a_.neg(S64, d);
            mask(d, w);
            return;
        }
        const auto k = log2(static_cast<std::uint64_t>(y));
        // r11 = 2^k - 1 when x is negative, else 0
        a_.mov(S64, scratch, d);
        a_.shift(Shift::sar, S64, scratch, Imm8{63});
        a_.shift(Shift::shr, S64, scratch, Imm8{static_cast<std::uint8_t>(64 - k)});
        if (remainder) {
            // x - ((x + r11) with its low k bits clear)
            a_.alu(Alu::add, S64, scratch, d);
            a_.shift(Shift::sar, S64, scratch, Imm8{static_cast<std::uint8_t>(k)});
            a_.shift(Shift::shl, S64, scratch, Imm8{static_cast<std::uint8_t>(k)});
            a_.alu(Alu::sub, S64, d, scratch);
        } else {
            a_.alu(Alu::add, S64, d, scratch);
            a_.shift(Shift::sar, S64, d, Imm8{static_cast<std::uint8_t>(k)});
        }
        mask(d, w);
    }

    template <typename Fail>
    void shift(const lift::Instruction& in, const Step& step, const Operands& o, const Fail& fail) {
        const unsigned w = in.width;
        const bool left = in.op == Opcode::ShiftLeft;
        const bool fits = step.a_width <= w;
        if (!o.has_b) {
            const auto c = step.b_value;
            if (c >= w) {
                // The count is never below the width: it always fails.
                a_.jmp(fail());
                return;
            }
            const auto d = o.d;
            const auto count = Imm8{static_cast<std::uint8_t>(c)};
            if (left) {
                if (c != 0) a_.shift(Shift::shl, S64, d, count);
                mask(d, w);
            } else if (!in.is_signed) {
                if (c != 0) a_.shift(Shift::shr, S64, d, count);
                if (!fits) mask(d, w);
            } else if (fits && (w == 32 || w == 64)) {
                if (c != 0) a_.shift(Shift::sar, w == 32 ? S32 : S64, d, count);
                else if (w == 32) a_.mov(S32, d, d);
            } else {
                sign_extend(d, w, fits);
                if (c != 0) a_.shift(Shift::sar, S64, d, count);
                mask(d, w);
            }
            return;
        }
        // The count in rcx, read as unsigned.
        const auto t = o.temps.at(0);
        a_.alu(Alu::cmp, S64, x64::rcx, imm32(w));
        a_.jcc(Cond::ae, fail());
        a_.mov(S64, t, o.a);
        if (left) {
            a_.shift_cl(Shift::shl, S64, t);
            mask(t, w);
        } else if (!in.is_signed) {
            a_.shift_cl(Shift::shr, S64, t);
            if (!fits) mask(t, w);
        } else if (fits && (w == 32 || w == 64)) {
            a_.shift_cl(Shift::sar, w == 32 ? S32 : S64, t);
        } else {
            sign_extend(t, w, fits);
            a_.shift_cl(Shift::sar, S64, t);
            mask(t, w);
        }
        a_.mov(S64, o.d, t);
    }

    void to_xmm(Xmm to, Gpr from, bool dbl) {
        if (dbl) a_.movq(to, from);
        else a_.movd(to, from);
    }
    void from_xmm(Gpr to, Xmm from, bool dbl) {
        if (dbl) a_.movq(to, from);
        else a_.movd(to, from);
    }

    // to = the canonical quiet NaN when `value` is a NaN (unordered with
    // itself).
    void canonicalize(Gpr to, Xmm value, bool dbl) {
        const auto skip = a_.label();
        a_.ucomis(dbl ? Prec::dbl : Prec::single, value, value);
        a_.jcc(Cond::np, skip);
        a_.mov_constant(to, dbl ? canonical_nan64 : canonical_nan32);
        a_.bind(skip);
    }

    template <typename Fail>
    void float_op(const lift::Instruction& in, const Step& step, const Operands& o, const Fail& fail) {
        const auto d = o.d;
        switch (in.op) {
        case Opcode::FloatAdd:
        case Opcode::FloatSubtract:
        case Opcode::FloatMultiply:
        case Opcode::FloatDivide: {
            const bool dbl = in.format == lift::FloatType::Binary64;
            const auto x0 = o.xmm_temps.at(0);
            const auto x1 = o.xmm_temps.at(1);
            to_xmm(x0, o.a, dbl);
            to_xmm(x1, o.b, dbl);
            const auto op = in.op == Opcode::FloatAdd        ? x64::Fp::add
                            : in.op == Opcode::FloatSubtract ? x64::Fp::sub
                            : in.op == Opcode::FloatMultiply ? x64::Fp::mul
                                                             : x64::Fp::div;
            a_.arith(op, dbl ? Prec::dbl : Prec::single, x0, x1);
            from_xmm(d, x0, dbl);
            canonicalize(d, x0, dbl);
            return;
        }
        case Opcode::FloatNegate: {
            // a ^ the format's sign bit: the bits above it are kept, and a
            // NaN keeps its payload.
            const auto sign = std::uint64_t{1} << (float_width(in.format) - 1);
            if (simm32(sign)) {
                a_.alu(Alu::xor_, S64, d, imm32(sign));
            } else {
                a_.mov_constant(scratch, sign);
                a_.alu(Alu::xor_, S64, d, scratch);
            }
            return;
        }
        case Opcode::FloatEqual:
        case Opcode::FloatNotEqual:
        case Opcode::FloatLess:
        case Opcode::FloatLessEqual:
        case Opcode::FloatGreater:
        case Opcode::FloatGreaterEqual: {
            // IEEE comparisons: ucomis is unordered for a NaN (ZF, PF and
            // CF set), and -0 equals +0.
            const bool dbl = in.format == lift::FloatType::Binary64;
            const auto prec = dbl ? Prec::dbl : Prec::single;
            const auto x0 = o.xmm_temps.at(0);
            const auto x1 = o.xmm_temps.at(1);
            to_xmm(x0, o.a, dbl);
            to_xmm(x1, o.b, dbl);
            switch (in.op) {
            case Opcode::FloatEqual:
                a_.ucomis(prec, x0, x1);
                a_.setcc(Cond::e, d);
                a_.setcc(Cond::np, scratch);
                a_.alu(Alu::and_, S8, d, scratch);
                break;
            case Opcode::FloatNotEqual:
                a_.ucomis(prec, x0, x1);
                a_.setcc(Cond::ne, d);
                a_.setcc(Cond::p, scratch);
                a_.alu(Alu::or_, S8, d, scratch);
                break;
            case Opcode::FloatLess:
                a_.ucomis(prec, x1, x0);
                a_.setcc(Cond::a, d);
                break;
            case Opcode::FloatLessEqual:
                a_.ucomis(prec, x1, x0);
                a_.setcc(Cond::ae, d);
                break;
            case Opcode::FloatGreater:
                a_.ucomis(prec, x0, x1);
                a_.setcc(Cond::a, d);
                break;
            default:
                a_.ucomis(prec, x0, x1);
                a_.setcc(Cond::ae, d);
                break;
            }
            a_.movzx(S32, d, S8, d);
            return;
        }
        case Opcode::FloatConvert: {
            // binary32 to binary64 exactly, binary64 to binary32 rounded to
            // nearest even; a NaN becomes the target's canonical NaN.
            const bool to_dbl = in.to_format == lift::FloatType::Binary64;
            const auto x0 = o.xmm_temps.at(0);
            to_xmm(x0, o.a, !to_dbl);
            if (to_dbl) a_.cvtss2sd(x0, x0);
            else a_.cvtsd2ss(x0, x0);
            from_xmm(d, x0, to_dbl);
            canonicalize(d, x0, to_dbl);
            return;
        }
        case Opcode::FloatFromInteger: {
            // Signed: the sign-extended value. Unsigned: as a signed 64-bit
            // value below 2^63 (whatever its width: the reference does not
            // mask it); else (v >> 1) | (v & 1), converted, then doubled.
            const bool dbl = in.to_format == lift::FloatType::Binary64;
            const auto prec = dbl ? Prec::dbl : Prec::single;
            const auto x0 = o.xmm_temps.at(0);
            const auto t = o.temps.at(0);
            a_.mov(S64, t, o.a);
            if (in.is_signed) {
                sign_extend(t, in.width, step.a_width <= in.width);
                a_.cvtsi2s(prec, x0, S64, t);
            } else {
                const auto large = a_.label();
                const auto done = a_.label();
                a_.bit(Bit::bt, S64, t, Imm8{63});
                a_.jcc(Cond::b, large);
                a_.cvtsi2s(prec, x0, S64, t);
                a_.jmp(done);
                a_.bind(large);
                a_.mov(S64, scratch, t);
                a_.alu(Alu::and_, S32, scratch, Imm32{1});
                a_.shift(Shift::shr, S64, t, Imm8{1});
                a_.alu(Alu::or_, S64, t, scratch);
                a_.cvtsi2s(prec, x0, S64, t);
                a_.arith(x64::Fp::add, prec, x0, x0);
                a_.bind(done);
            }
            from_xmm(d, x0, dbl);
            return;
        }
        case Opcode::FloatToInteger: {
            // The truncation t; where it is not INT64_MIN it is exact, and the
            // bounds are those of the target. INT64_MIN (a NaN, a value
            // outside the signed 64-bit range, or -2^63) is lift::evaluate's.
            const bool dbl = in.format == lift::FloatType::Binary64;
            const auto x0 = o.xmm_temps.at(0);
            const auto t = o.temps.at(0);
            const unsigned to = in.to_width;
            to_xmm(x0, o.a, dbl);
            a_.cvtts2si(dbl ? Prec::dbl : Prec::single, S64, t, x0);
            const auto slow = a_.label();
            const auto done = a_.label();
            // t - 1 overflows exactly when t is INT64_MIN.
            a_.alu(Alu::cmp, S64, t, Imm32{1});
            a_.jcc(Cond::o, slow);
            if (in.to_signed) {
                if (to < 64) range(t, lift::signed_min(to), lift::signed_max(to), fail());
                mask(t, to);
            } else {
                a_.test(S64, t, t);
                a_.jcc(Cond::s, fail());
                if (to < 64) {
                    compare_constant(t, lift::mask_of(to));
                    a_.jcc(Cond::a, fail());
                }
            }
            a_.mov(S64, d, t);
            a_.bind(done);
            const auto label = fail();
            const auto a = o.a;
            const auto inst = step.inst;
            cold_.push_back([this, slow, done, label, a, d, inst] {
                a_.bind(slow);
                call_evaluate(inst, &a, nullptr, d, &label);
                a_.jmp(done);
            });
            return;
        }
        default: break;
        }
        throw EmitError("a float operation the code generator does not compile");
    }
};

} // namespace

bool supported() {
#if defined(__x86_64__) && defined(__linux__)
    return true;
#else
    return false;
#endif
}

namespace {

// The code of `units` of `function` in one piece, with the trampoline when
// `with_trampoline`; the entries of other units are `lazy`.
Generated generate_units(const nvm::tier3::Function& function, const std::vector<std::vector<std::uint32_t>>& units,
                         bool with_trampoline, bool verify) {
    Generated result;
    if (!supported()) {
        result.failure = "no tier 3 code generator for this platform";
        return result;
    }
    try {
        Assembler assembler;
        std::vector<x64::Label> entries(function.entries.size());
        std::vector<bool> here(function.entries.size(), false);
        std::uint32_t frame_bytes = 0;
        for (const auto& unit : units) {
            const auto lowered = lower(function, unit);
            mir::Allocation allocation;
            try {
                allocation = mir::allocate(lowered.function);
            } catch (const mir::AllocationError& error) {
                result.failure = std::string("the register allocator: ") + error.what();
                return result;
            }
            if (verify) {
                const auto problems = mir::check(lowered.function, allocation);
                if (!problems.empty()) {
                    std::fprintf(stderr, "tier 3: the register allocation of function %u is invalid: %s\n",
                                 function.function, problems.front().c_str());
                    for (std::size_t i = 1; i < problems.size(); ++i) {
                        std::fprintf(stderr, "  %s\n", problems[i].c_str());
                    }
                    std::abort();
                }
            }
            Emitter emitter(function, lowered, allocation, assembler);
            const auto labels = emitter.run();
            for (std::size_t k = 0; k < unit.size(); ++k) {
                entries[unit[k]] = labels[k];
                here[unit[k]] = true;
            }
            frame_bytes = std::max(frame_bytes, emitter.frame_bytes());
        }
        x64::Label stub;
        if (with_trampoline) stub = trampoline(assembler);
        auto& code = result.code;
        code.bytes = assembler.finish();
        if (code.bytes.size() > max_code_bytes) throw EmitError("code size over 1 MiB");
        for (std::size_t e = 0; e < entries.size(); ++e) {
            code.entries.push_back(here[e] ? static_cast<std::uint32_t>(assembler.offset(entries[e])) : lazy);
        }
        if (with_trampoline) code.trampoline = static_cast<std::uint32_t>(assembler.offset(stub));
        code.frame_bytes = frame_bytes;
    } catch (const LoweringError& error) {
        result.failure = error.what();
    } catch (const EmitError& error) {
        result.failure = error.what();
    } catch (const std::logic_error& error) {
        result.failure = std::string("the encoder: ") + error.what();
    }
    if (!result.failure.empty()) result.code = {};
    return result;
}

} // namespace

Generated generate(const nvm::tier3::Function& function, bool verify) {
    try {
        const auto units = entry_units(function);
        return generate_units(function, {units.front()}, true, verify);
    } catch (const LoweringError& error) {
        Generated result;
        result.failure = error.what();
        return result;
    }
}

Generated generate_entry(const nvm::tier3::Function& function, std::uint32_t entry, bool verify) {
    return generate_units(function, {{entry}}, false, verify);
}

} // namespace neura::jit::native

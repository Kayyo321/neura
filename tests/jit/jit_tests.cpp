// The native code generator of tier 2 (docs/runtime/NVM.md, "Native
// compilation"; src/jit). The `jit` suite (backend-jit) checks:
//
// - the encoders' bytes against an assembler's (tests/jit/encodings.inc,
//   from scripts/jit-encodings.py), for x86-64 and AArch64;
// - every integer instruction compiled inline, and those that call
//   lift::evaluate, against lift::evaluate on edge values of every width,
//   in and out of machine registers;
// - random region programs (loops, branches, switches, budgets, polls,
//   gathers, stores) against a transcription of the region interpreter,
//   down to every register and initialized flag;
// - whole runs with native code against tier 0 and the region interpreter:
//   each deoptimization kind, step limits and task quanta inside regions,
//   collections at region ends, and the fallback when code cannot be
//   mapped.
//
// Where native code is not enabled by default, the generator is checked by
// its encodings and by generating code for the same programs; NEURA_JIT=on
// runs the rest there too.
#include "backend/fixtures.hpp"
#include "backend/graph_test_support.hpp"

#include "jit/aarch64.hpp"
#include "jit/jit.hpp"
#include "jit/x86_64.hpp"
#include "lift/evaluate.hpp"
#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvr/format.hpp"

#include <functional>
#include <map>
#include <random>

namespace neura::testing {
namespace {

struct Encoding {
    const char* arch;
    const char* text;
    std::vector<std::uint8_t> bytes;
};

const std::vector<Encoding>& encodings() {
    static const std::vector<Encoding> table = {
#include "jit/encodings.inc"
    };
    return table;
}

std::string hex(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    char text[4];
    for (const auto b : bytes) {
        std::snprintf(text, sizeof text, "%02x ", b);
        out += text;
    }
    return out;
}

using X86 = jit::x86_64::Emitter;
using A64 = jit::aarch64::Emitter;

std::map<std::string, std::function<void(X86&)>> x86_cases() {
    using namespace jit::x86_64;
    std::map<std::string, std::function<void(X86&)>> c;
    c["mov rax, rcx"] = [](X86& e) { e.mov(rax, rcx); };
    c["mov r13, rdi"] = [](X86& e) { e.mov(r13, rdi); };
    c["mov rbx, r11"] = [](X86& e) { e.mov(rbx, r11); };
    c["mov rdi, r13"] = [](X86& e) { e.mov(rdi, r13); };
    c["mov qword ptr [r15 + 8], rax"] = [](X86& e) { e.store(r15, 8, rax); };
    c["mov rax, qword ptr [r15 + 8]"] = [](X86& e) { e.load(rax, r15, 8); };
    c["mov rax, qword ptr [r15]"] = [](X86& e) { e.load(rax, r15, 0); };
    c["mov r10, qword ptr [r15 + 4096]"] = [](X86& e) { e.load(r10, r15, 4096); };
    c["mov rcx, qword ptr [r13 + 16]"] = [](X86& e) { e.load(rcx, r13, 16); };
    c["mov rdx, qword ptr [r12 + 8]"] = [](X86& e) { e.load(rdx, r12, 8); };
    c["mov rax, qword ptr [rax]"] = [](X86& e) { e.load(rax, rax, 0); };
    c["mov r15, qword ptr [r13]"] = [](X86& e) { e.load(r15, r13, 0); };
    c["mov qword ptr [r15 + 800], rbp"] = [](X86& e) { e.store(r15, 800, rbp); };
    c["mov qword ptr [r13 + 56], r12"] = [](X86& e) { e.store(r13, 56, r12); };
    c["mov qword ptr [r15 + 16], rsi"] = [](X86& e) { e.store(r15, 16, rsi); };
    c["movzx eax, byte ptr [r13 + 72]"] = [](X86& e) { e.load_byte(rax, r13, 72); };
    c["movzx ecx, byte ptr [r13 + 72]"] = [](X86& e) { e.load_byte(rcx, r13, 72); };
    c["mov byte ptr [r14 + 3], 1"] = [](X86& e) { e.store_byte(r14, 3, 1); };
    c["mov byte ptr [r14 + 300], 1"] = [](X86& e) { e.store_byte(r14, 300, 1); };
    c["mov byte ptr [r14], 1"] = [](X86& e) { e.store_byte(r14, 0, 1); };
    c["mov dword ptr [r13 + 64], 7"] = [](X86& e) { e.store32(r13, 64, 7); };
    c["mov dword ptr [r13 + 68], 305419896"] = [](X86& e) { e.store32(r13, 68, 305419896); };
    c["mov eax, 0"] = [](X86& e) { e.mov_immediate(rax, 0); };
    c["mov r12d, 0"] = [](X86& e) { e.mov_immediate(r12, 0); };
    c["mov esi, 17"] = [](X86& e) { e.mov_immediate(rsi, 17); };
    c["mov r11d, 4294967295"] = [](X86& e) { e.mov_immediate(r11, 0xFFFFFFFFULL); };
    c["mov rax, -1"] = [](X86& e) { e.mov_immediate(rax, ~0ULL); };
    c["mov r11, -2147483648"] = [](X86& e) { e.mov_immediate(r11, 0xFFFFFFFF80000000ULL); };
    c["movabs r11, 0x8000000000000000"] = [](X86& e) { e.mov_immediate(r11, 0x8000000000000000ULL); };
    c["movabs rax, 0x123456789abcdef0"] = [](X86& e) { e.mov_immediate(rax, 0x123456789abcdef0ULL); };
    c["add rax, rcx"] = [](X86& e) { e.alu(AluOp::add, rax, rcx); };
    c["sub rdx, rcx"] = [](X86& e) { e.alu(AluOp::sub, rdx, rcx); };
    c["and rax, r11"] = [](X86& e) { e.alu(AluOp::and_, rax, r11); };
    c["or rax, rcx"] = [](X86& e) { e.alu(AluOp::or_, rax, rcx); };
    c["xor rcx, r11"] = [](X86& e) { e.alu(AluOp::xor_, rcx, r11); };
    c["cmp rax, rdx"] = [](X86& e) { e.alu(AluOp::cmp, rax, rdx); };
    c["cmp rcx, r11"] = [](X86& e) { e.alu(AluOp::cmp, rcx, r11); };
    c["add r12, 1"] = [](X86& e) { e.add_immediate(r12, 1); };
    c["add r12, 1000"] = [](X86& e) { e.add_immediate(r12, 1000); };
    c["add rsp, -8"] = [](X86& e) { e.add_immediate(rsp, -8); };
    c["add rsp, 8"] = [](X86& e) { e.add_immediate(rsp, 8); };
    c["imul rax, rcx"] = [](X86& e) { e.imul(rax, rcx); };
    c["imul rdx, rcx"] = [](X86& e) { e.imul(rdx, rcx); };
    c["mul rcx"] = [](X86& e) { e.mul(rcx); };
    c["shl rax, cl"] = [](X86& e) { e.shift_cl(ShiftOp::shl, rax); };
    c["shr rax, cl"] = [](X86& e) { e.shift_cl(ShiftOp::shr, rax); };
    c["sar rdx, cl"] = [](X86& e) { e.shift_cl(ShiftOp::sar, rdx); };
    c["shl rcx, 5"] = [](X86& e) { e.shift_immediate(ShiftOp::shl, rcx, 5); };
    c["shr rcx, 1"] = [](X86& e) { e.shift_immediate(ShiftOp::shr, rcx, 1); };
    c["sar rax, 63"] = [](X86& e) { e.shift_immediate(ShiftOp::sar, rax, 63); };
    c["neg rax"] = [](X86& e) { e.neg(rax); };
    c["not rdx"] = [](X86& e) { e.not_(rdx); };
    c["sete al"] = [](X86& e) { e.setcc(CC::e, rax); };
    c["setb cl"] = [](X86& e) { e.setcc(CC::b, rcx); };
    c["setg dl"] = [](X86& e) { e.setcc(CC::g, rdx); };
    c["setne r11b"] = [](X86& e) { e.setcc(CC::ne, r11); };
    c["setl sil"] = [](X86& e) { e.setcc(CC::l, rsi); };
    c["movzx eax, al"] = [](X86& e) { e.movzx_byte(rax, rax); };
    c["movzx edx, dl"] = [](X86& e) { e.movzx_byte(rdx, rdx); };
    c["movzx r11d, r11b"] = [](X86& e) { e.movzx_byte(r11, r11); };
    c["movzx esi, sil"] = [](X86& e) { e.movzx_byte(rsi, rsi); };
    c["bt rax, 0"] = [](X86& e) { e.bt(rax, 0); };
    c["bt rax, 63"] = [](X86& e) { e.bt(rax, 63); };
    c["test rax, rax"] = [](X86& e) { e.test(rax, rax); };
    c["test rcx, rcx"] = [](X86& e) { e.test(rcx, rcx); };
    const auto jump = [](CC cc) {
        return [cc](X86& e) { e.patch(e.jcc(cc), 4096); };
    };
    c[".att_syntax; je .+4096"] = jump(CC::e);
    c[".att_syntax; jo .+4096"] = jump(CC::o);
    c[".att_syntax; jb .+4096"] = jump(CC::b);
    c[".att_syntax; jae .+4096"] = jump(CC::ae);
    c[".att_syntax; jg .+4096"] = jump(CC::g);
    c[".att_syntax; jmp .+4096"] = [](X86& e) { e.patch(e.jmp(), 4096); };
    c["call qword ptr [r13 + 80]"] = [](X86& e) { e.call(r13, 80); };
    c["call qword ptr [r13 + 104]"] = [](X86& e) { e.call(r13, 104); };
    c["push rbx"] = [](X86& e) { e.push(rbx); };
    c["push r15"] = [](X86& e) { e.push(r15); };
    c["pop r12"] = [](X86& e) { e.pop(r12); };
    c["pop rbp"] = [](X86& e) { e.pop(rbp); };
    c["ret"] = [](X86& e) { e.ret(); };
    return c;
}

std::map<std::string, std::function<void(A64&)>> aarch64_cases() {
    using namespace jit::aarch64;
    std::map<std::string, std::function<void(A64&)>> c;
    c["mov x9, x23"] = [](A64& e) { e.mov(9, 23); };
    c["mov x21, x0"] = [](A64& e) { e.mov(21, 0); };
    c["mov x0, x21"] = [](A64& e) { e.mov(0, 21); };
    c["ldr x9, [x19, #8]"] = [](A64& e) { e.ldr(9, 19, 8); };
    c["ldr x9, [x19, #32760]"] = [](A64& e) { e.ldr(9, 19, 32760); };
    c["ldr x19, [x21]"] = [](A64& e) { e.ldr(19, 21, 0); };
    c["str x23, [x19, #24]"] = [](A64& e) { e.str(23, 19, 24); };
    c["str x22, [x21, #56]"] = [](A64& e) { e.str(22, 21, 56); };
    c["ldr x9, [x19, x17, lsl #3]"] = [](A64& e) { e.ldr_indexed(9, 19, 17); };
    c["str x10, [x19, x17, lsl #3]"] = [](A64& e) { e.str_indexed(10, 19, 17); };
    c["ldrb w10, [x21, #72]"] = [](A64& e) { e.ldrb(10, 21, 72); };
    c["strb w16, [x20, #4095]"] = [](A64& e) { e.strb(16, 20, 4095); };
    c["strb w16, [x20, x17]"] = [](A64& e) { e.strb_indexed(16, 20, 17); };
    c["str w16, [x21, #68]"] = [](A64& e) { e.str_w(16, 21, 68); };
    c["movz x9, #0"] = [](A64& e) { e.movz(9, 0, 0); };
    c["movz x9, #4660, lsl #16"] = [](A64& e) { e.movz(9, 4660, 16); };
    c["movk x9, #65535, lsl #48"] = [](A64& e) { e.movk(9, 65535, 48); };
    c["movn x12, #0"] = [](A64& e) { e.mov_immediate(12, ~0ULL); };
    c["movz x12, #0x8000, lsl #48"] = [](A64& e) { e.mov_immediate(12, 0x8000000000000000ULL); };
    c["movz x12, #0xdef0; movk x12, #0x9abc, lsl #16; movk x12, #0x5678, lsl #32; movk x12, #0x1234, lsl #48"] =
        [](A64& e) { e.mov_immediate(12, 0x123456789abcdef0ULL); };
    c["movn x12, #0xedcb; movk x12, #0, lsl #16"] = [](A64& e) { e.mov_immediate(12, 0xFFFFFFFF00001234ULL); };
    c["movn x12, #0x8000, lsl #48"] = [](A64& e) { e.mov_immediate(12, 0x7FFFFFFFFFFFFFFFULL); };
    c["movz x1, #7"] = [](A64& e) { e.mov_immediate(1, 7); };
    c["add x9, x9, x10"] = [](A64& e) { e.add(9, 9, 10); };
    c["adds x9, x9, x10"] = [](A64& e) { e.adds(9, 9, 10); };
    c["sub x11, x11, x10"] = [](A64& e) { e.sub(11, 11, 10); };
    c["subs x9, x9, x10"] = [](A64& e) { e.subs(9, 9, 10); };
    c["and x9, x9, x12"] = [](A64& e) { e.and_(9, 9, 12); };
    c["orr x9, x9, x10"] = [](A64& e) { e.orr(9, 9, 10); };
    c["eor x9, x9, x12"] = [](A64& e) { e.eor(9, 9, 12); };
    c["mul x9, x9, x10"] = [](A64& e) { e.mul(9, 9, 10); };
    c["umulh x16, x9, x10"] = [](A64& e) { e.umulh(16, 9, 10); };
    c["smulh x16, x9, x10"] = [](A64& e) { e.smulh(16, 9, 10); };
    c["lslv x9, x9, x10"] = [](A64& e) { e.lslv(9, 9, 10); };
    c["lsrv x9, x9, x10"] = [](A64& e) { e.lsrv(9, 9, 10); };
    c["asrv x11, x11, x10"] = [](A64& e) { e.asrv(11, 11, 10); };
    c["lsl x10, x10, #5"] = [](A64& e) { e.lsl(10, 10, 5); };
    c["lsl x10, x10, #63"] = [](A64& e) { e.lsl(10, 10, 63); };
    c["lsr x10, x10, #1"] = [](A64& e) { e.lsr(10, 10, 1); };
    c["asr x9, x9, #63"] = [](A64& e) { e.asr(9, 9, 63); };
    c["neg x9, x9"] = [](A64& e) { e.neg(9, 9); };
    c["mvn x9, x9"] = [](A64& e) { e.mvn(9, 9); };
    c["cmp x9, x10"] = [](A64& e) { e.cmp(9, 10); };
    c["cmp x16, x9, asr #63"] = [](A64& e) { e.cmp_asr(16, 9, 63); };
    c["tst x9, #0x1"] = [](A64& e) { e.tst_bit(9, 0); };
    c["tst x9, #0x100"] = [](A64& e) { e.tst_bit(9, 8); };
    c["tst x9, #0x8000000000000000"] = [](A64& e) { e.tst_bit(9, 63); };
    c["cset x9, eq"] = [](A64& e) { e.cset(9, eq); };
    c["cset x9, lo"] = [](A64& e) { e.cset(9, lo); };
    c["cset x9, gt"] = [](A64& e) { e.cset(9, gt); };
    c["cset x9, ls"] = [](A64& e) { e.cset(9, ls); };
    c["add x29, sp, #0"] = [](A64& e) { e.add_immediate(29, sp, 0); };
    c["add x22, x22, #1"] = [](A64& e) { e.add_immediate(22, 22, 1); };
    c["add x22, x22, #4095"] = [](A64& e) { e.add_immediate(22, 22, 4095); };
    c["b .+64"] = [](A64& e) { e.patch(e.b(), 16); };
    const auto conditional = [](Cond cond) {
        return [cond](A64& e) { e.patch(e.b_cond(cond), 16); };
    };
    c["b.eq .+64"] = conditional(eq);
    c["b.vs .+64"] = conditional(vs);
    c["b.hs .+64"] = conditional(hs);
    c["b.ne .+64"] = conditional(ne);
    c["cbz x9, .+64"] = [](A64& e) { e.patch(e.cbz(9), 16); };
    c["cbnz x16, .+64"] = [](A64& e) { e.patch(e.cbnz(16), 16); };
    c["mov x9, x9; b .-4"] = [](A64& e) {
        e.mov(9, 9);
        e.patch(e.b(), 0);
    };
    c["mov x9, x9; b.ne .-4"] = [](A64& e) {
        e.mov(9, 9);
        e.patch(e.b_cond(ne), 0);
    };
    c["mov x9, x9; cbnz x9, .-4"] = [](A64& e) {
        e.mov(9, 9);
        e.patch(e.cbnz(9), 0);
    };
    c["blr x16"] = [](A64& e) { e.blr(16); };
    c["ret"] = [](A64& e) { e.ret(); };
    c["stp x29, x30, [sp, #-96]!"] = [](A64& e) { e.stp_pre(29, 30, sp, -96); };
    c["ldp x29, x30, [sp], #96"] = [](A64& e) { e.ldp_post(29, 30, sp, 96); };
    c["stp x19, x20, [sp, #16]"] = [](A64& e) { e.stp(19, 20, sp, 16); };
    c["ldp x27, x28, [sp, #80]"] = [](A64& e) { e.ldp(27, 28, sp, 80); };
    return c;
}

// --- Region programs ------------------------------------------------------

using nvm::tier2::Code;
using nvm::tier2::Op;
using nvm::tier2::Region;

// Generated code is run where `auto` enables it (x86-64 Linux), and
// anywhere else the environment asks for it with NEURA_JIT=on, as the
// native-code workflow does (.github/workflows/jit.yml).
bool native_runs_here() {
    if (!jit::host_has_backend()) return false;
    if (jit::environment_mode() == jit::Mode::On) return jit::availability(jit::Mode::On).enabled;
    return jit::availability(jit::Mode::Auto).enabled;
}

struct Machine {
    std::uint64_t steps = 0;
    std::uint64_t max_steps = ~0ULL;
    std::uint64_t allowance = ~0ULL;
    bool scheduler = false;
    std::uint64_t polls = 0;
    std::uint64_t quantum = 1024;
};

struct Ending {
    jit::Outcome outcome = jit::Outcome::Leave;
    std::uint32_t block = 0;
    std::uint64_t done = 0;
    bool operator==(const Ending&) const = default;
};

std::ostream& operator<<(std::ostream& out, const Ending& e) {
    return out << "outcome " << static_cast<int>(e.outcome) << " block " << e.block << " done " << e.done;
}

// The region interpreter of src/nvm/tier2_steps.inc, transcribed for the
// ops that need no executor (no CheckRef or State), as the oracle of
// generated code on synthetic programs.
Ending interpret(const Region& region, std::vector<std::uint64_t>& regs, std::vector<std::uint8_t>& init,
                 Machine& machine) {
    std::uint32_t pc = 0;
    std::uint64_t done = 0;
    for (;;) {
        const auto& op = region.code[pc++];
        switch (op.code) {
        case Code::Budget: {
            const auto& checkpoint = region.checkpoints[op.a];
            const auto used = machine.steps + done;
            const bool steps_fit = used <= machine.max_steps && machine.max_steps - used >= checkpoint.steps;
            const bool allowed = machine.allowance - done >= checkpoint.steps;
            const bool polls_fit = !machine.scheduler || checkpoint.polls == 0 ||
                                   machine.polls + checkpoint.polls < machine.quantum;
            if (!steps_fit || !allowed || !polls_fit) return {jit::Outcome::Budget, op.block, done};
            break;
        }
        case Code::Lift: {
            bool failed = false;
            const auto value = lift::evaluate(region.instructions[op.index], regs[op.a], regs[op.b], failed);
            if (failed) return {jit::Outcome::Checks, op.block, done};
            regs[op.dst] = value;
            if (op.flags & nvm::tier2::defines) init[op.dst] = 1;
            break;
        }
        case Code::Mask: regs[op.dst] = regs[op.a] & lift::mask_of(op.width); break;
        case Code::Move: regs[op.dst] = regs[op.a]; break;
        case Code::Gather: {
            std::uint64_t value = 0;
            const auto* bits = region.gathers.data() + op.index;
            for (unsigned i = 0; i < op.width; ++i) value |= ((regs[bits[i].reg] >> bits[i].bit) & 1U) << i;
            regs[op.dst] = value;
            break;
        }
        case Code::Store:
            regs[op.dst] = regs[op.a];
            init[op.dst] = 1;
            break;
        case Code::Branch: pc = (regs[op.a] & 1U) != 0 ? op.target : op.other; break;
        case Code::Switch: {
            const auto index = regs[op.a];
            if (index >= op.width) return {jit::Outcome::Selectors, op.block, done};
            pc = region.operands[op.index + index];
            break;
        }
        case Code::Jump:
            done += op.flags & nvm::tier2::completes;
            pc = op.target;
            break;
        case Code::Poll:
            if (machine.scheduler) ++machine.polls;
            break;
        case Code::Complete: ++done; break;
        case Code::Leave: return {jit::Outcome::Leave, op.target, done + (op.flags & nvm::tier2::completes)};
        case Code::Deopt: return {jit::Outcome::Exits, op.block, done};
        default: fail("an op the oracle does not run");
        }
    }
}

std::uint64_t test_poll(jit::Context* context, std::uint32_t, std::uint64_t, std::uint64_t) {
    ++*context->polls;
    context->status = 0;
    return 0;
}

Ending run_native(const jit::Program& program, const Region& region, std::vector<std::uint64_t>& regs,
                  std::vector<std::uint8_t>& init, Machine& machine) {
    jit::Context context;
    context.registers = regs.data();
    context.initialized = init.data();
    context.steps = &machine.steps;
    context.max_steps = machine.max_steps;
    context.allowance = machine.allowance;
    context.polls = machine.scheduler ? &machine.polls : nullptr;
    context.quantum = machine.quantum;
    context.lift = jit::evaluate_helper;
    context.poll = test_poll;
    context.region = &region;
    program.run(context);
    return {static_cast<jit::Outcome>(context.outcome), context.next_block, context.done};
}

std::unique_ptr<jit::Program> compile_or_fail(const Region& region, const std::string& label) {
    auto compiled = jit::compile(region);
    check(compiled.program != nullptr, label + ": not compiled: " + compiled.failure);
    // The AArch64 generator must accept the same program.
    const auto other = jit::generate(region, jit::Arch::AArch64);
    check(other.failure.empty() && !other.code.empty(), label + ": no AArch64 code: " + other.failure);
    return std::move(compiled.program);
}

// `lift r2 = in(r0, r1) block 7; leave to 9`, or, with `pinned`, the same
// through moves that make every register used twice, so the generator keeps
// them in machine registers.
Region instruction_region(const lift::Instruction& in, bool pinned) {
    Region region;
    region.blocks = {7};
    region.instructions = {in};
    Op lift;
    lift.code = Code::Lift;
    lift.block = 7;
    lift.index = 0;
    Op leave;
    leave.code = Code::Leave;
    leave.target = 9;
    leave.flags = nvm::tier2::completes;
    leave.block = 7;
    if (!pinned) {
        region.register_count = 3;
        lift.dst = 2;
        lift.a = 0;
        lift.b = 1;
        region.code = {lift, leave};
        return region;
    }
    region.register_count = 6;
    Op move;
    move.code = Code::Move;
    move.dst = 3;
    move.a = 0;
    region.code.push_back(move);
    move.dst = 4;
    move.a = 1;
    region.code.push_back(move);
    lift.dst = 5;
    lift.a = 3;
    lift.b = 4;
    region.code.push_back(lift);
    move.dst = 2;
    move.a = 5;
    region.code.push_back(move);
    region.code.push_back(leave);
    return region;
}

std::vector<std::uint64_t> edge_values(unsigned width) {
    const auto m = lift::mask_of(width);
    const auto sign = std::uint64_t{1} << (width - 1);
    std::vector<std::uint64_t> values{0, 1, 2, 3, m, m - 1, m >> 1, sign, sign - 1, sign + 1, ~0ULL,
                                      std::uint64_t{1} << 63, 0x8000000000000001ULL, 0x0123456789ABCDEFULL};
    if (width < 64) {
        // Values with bits above the width: the generated code must treat
        // them as lift::evaluate does.
        values.push_back(m + 1);
        values.push_back(m + 2);
        values.push_back(m << 1);
    }
    if (width < 63) values.push_back(width + 1);
    values.push_back(width);
    values.push_back(width - 1);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

// Runs of check_instruction, and how many of them failed a check.
std::uint64_t instruction_runs = 0;
std::uint64_t instruction_failures = 0;

void check_instruction(const lift::Instruction& in, const std::string& label) {
    for (const bool pinned : {false, true}) {
        const auto region = instruction_region(in, pinned);
        const auto program = compile_or_fail(region, label);
        if (!program) return;
        auto values = edge_values(std::clamp<unsigned>(in.width, 1, 64));
        if (in.op == lift::Opcode::Divide || in.op == lift::Opcode::Remainder) {
            // lift::evaluate divides sign-extended values, so it needs
            // operands that fit their width (as every region's do): with
            // bits above it, MIN / -1 could reach the hardware.
            std::vector<std::uint64_t> fitting;
            for (const auto v : values) {
                if ((v & lift::mask_of(in.width)) == v) fitting.push_back(v);
            }
            values = fitting;
        }
        for (const auto a : values) {
            for (const auto b : values) {
                std::vector<std::uint64_t> regs(region.register_count, 0x5EED5EED5EED5EEDULL);
                std::vector<std::uint8_t> init(region.register_count, 0);
                regs[0] = a;
                regs[1] = b;
                Machine machine;
                const auto ending = run_native(*program, region, regs, init, machine);
                bool failed = false;
                const auto expected = lift::evaluate(in, a, b, failed);
                ++instruction_runs;
                instruction_failures += failed;
                const auto where = label + (pinned ? " pinned" : "") + " on " + std::to_string(a) + ", " +
                                   std::to_string(b);
                if (failed) {
                    check_equal(ending, (Ending{jit::Outcome::Checks, 7, 0}), where + " fails");
                    check_equal(regs[2], 0x5EED5EED5EED5EEDULL, where + " leaves its destination");
                } else {
                    check_equal(ending, (Ending{jit::Outcome::Leave, 9, 1}), where + " succeeds");
                    check_equal(regs[2], expected, where + " result");
                }
            }
        }
    }
}

std::string opcode_name(lift::Opcode op) { return "op" + std::to_string(static_cast<int>(op)); }

} // namespace

NEURA_TEST(jit, encoders_match_the_assembler) {
    const auto x86 = x86_cases();
    const auto a64 = aarch64_cases();
    std::size_t x86_seen = 0;
    std::size_t a64_seen = 0;
    for (const auto& encoding : encodings()) {
        std::vector<std::uint8_t> bytes;
        const std::string arch = encoding.arch;
        if (arch == "x86_64") {
            const auto it = x86.find(encoding.text);
            check(it != x86.end(), std::string("no x86-64 emitter case for ") + encoding.text);
            X86 e;
            it->second(e);
            bytes = e.code;
            ++x86_seen;
        } else {
            const auto it = a64.find(encoding.text);
            check(it != a64.end(), std::string("no AArch64 emitter case for ") + encoding.text);
            A64 e;
            it->second(e);
            bytes = e.bytes();
            ++a64_seen;
        }
        check(bytes == encoding.bytes, arch + " " + encoding.text + ": emitted " + hex(bytes) + ", the assembler " +
                                           hex(encoding.bytes));
    }
    check_equal(x86_seen, x86.size(), "x86-64 cases in the table");
    check_equal(a64_seen, a64.size(), "AArch64 cases in the table");
}

NEURA_TEST(jit, every_instruction_matches_lift_evaluate) {
    if (!native_runs_here()) return;
    using lift::Opcode;
    const unsigned widths[] = {1, 2, 7, 8, 16, 31, 32, 33, 63, 64};
    const Opcode binary[] = {Opcode::Add,        Opcode::Subtract,  Opcode::Multiply,  Opcode::Divide,
                             Opcode::Remainder,  Opcode::Negate,    Opcode::AddWrapping, Opcode::Equal,
                             Opcode::NotEqual,   Opcode::Less,      Opcode::LessEqual, Opcode::Greater,
                             Opcode::GreaterEqual, Opcode::And,     Opcode::Or,        Opcode::Not,
                             Opcode::BitAnd,     Opcode::BitOr,     Opcode::BitXor,    Opcode::BitNot,
                             Opcode::ShiftLeft,  Opcode::ShiftRight, Opcode::Index,    Opcode::Constant};
    for (const auto op : binary) {
        for (const auto width : widths) {
            for (const bool is_signed : {false, true}) {
                lift::Instruction in;
                in.op = op;
                in.width = static_cast<std::uint8_t>(width);
                in.is_signed = is_signed;
                in.immediate = 0xA5A5A5A5A5A5A5A5ULL & lift::mask_of(width);
                check_instruction(in, opcode_name(op) + " width " + std::to_string(width) +
                                          (is_signed ? " signed" : " unsigned"));
            }
        }
    }
    for (const auto width : widths) {
        for (const auto to : {1U, 8U, 16U, 31U, 32U, 63U, 64U}) {
            for (const bool is_signed : {false, true}) {
                for (const bool to_signed : {false, true}) {
                    lift::Instruction in;
                    in.op = Opcode::ConvertInteger;
                    in.width = static_cast<std::uint8_t>(width);
                    in.is_signed = is_signed;
                    in.to_width = static_cast<std::uint8_t>(to);
                    in.to_signed = to_signed;
                    check_instruction(in, "convert " + std::to_string(width) + (is_signed ? "s" : "u") + " to " +
                                              std::to_string(to) + (to_signed ? "s" : "u"));
                }
            }
        }
    }
    // Float operations call lift::evaluate through the helper.
    if (lift::float_lowering_supported()) {
        const Opcode floats[] = {Opcode::FloatAdd,  Opcode::FloatSubtract, Opcode::FloatMultiply,
                                 Opcode::FloatDivide, Opcode::FloatLess,  Opcode::FloatEqual,
                                 Opcode::FloatNegate, Opcode::FloatToInteger};
        for (const auto op : floats) {
            for (const auto format : {lift::FloatType::Binary32, lift::FloatType::Binary64}) {
                lift::Instruction in;
                in.op = op;
                in.format = format;
                in.width = format == lift::FloatType::Binary32 ? 32 : 64;
                in.to_width = 32;
                in.to_signed = true;
                check_instruction(in, opcode_name(op) + " float " + std::to_string(in.width));
            }
        }
    }
    check(instruction_runs > 200000 && instruction_failures > 10000 && instruction_failures < instruction_runs,
          "the edge values exercise both outcomes: " + std::to_string(instruction_runs) + " runs, " +
              std::to_string(instruction_failures) + " failing");
}

namespace {

lift::Instruction random_instruction(std::mt19937_64& rng) {
    using lift::Opcode;
    static const Opcode ops[] = {Opcode::Add,       Opcode::Subtract,   Opcode::Multiply,    Opcode::Negate,     Opcode::AddWrapping, Opcode::Equal,
                                 Opcode::Less,      Opcode::GreaterEqual, Opcode::And,       Opcode::Not,
                                 Opcode::BitAnd,    Opcode::BitXor,     Opcode::BitNot,      Opcode::ShiftLeft,
                                 Opcode::ShiftRight, Opcode::Index,     Opcode::ConvertInteger, Opcode::FloatAdd};
    static const unsigned widths[] = {1, 8, 16, 32, 63, 64};
    lift::Instruction in;
    in.op = ops[rng() % std::size(ops)];
    in.width = static_cast<std::uint8_t>(widths[rng() % std::size(widths)]);
    in.is_signed = rng() % 2;
    in.to_width = static_cast<std::uint8_t>(widths[rng() % std::size(widths)]);
    in.to_signed = rng() % 2;
    if (in.op == Opcode::FloatAdd) {
        in.format = rng() % 2 ? lift::FloatType::Binary32 : lift::FloatType::Binary64;
        in.width = in.format == lift::FloatType::Binary32 ? 32 : 64;
    }
    return in;
}

std::uint64_t random_value(std::mt19937_64& rng) {
    static const std::uint64_t edges[] = {0, 1, 2, 3, 0x7F, 0x80, 0xFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF,
                                          0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL, ~0ULL};
    return rng() % 3 == 0 ? rng() : edges[rng() % std::size(edges)];
}

// A random region: blocks that each begin at a checkpoint, run random
// straight-line ops, complete, and end in a branch, a switch, a jump, a
// leave, or a deoptimization. Every block completes, so budgets end loops.
Region random_region(std::mt19937_64& rng) {
    Region region;
    const std::uint32_t slots = 6;
    region.register_count = 12 + static_cast<std::uint32_t>(rng() % 20);
    for (std::uint32_t s = 0; s < slots; ++s) region.slots.push_back(s);
    const auto reg = [&] { return static_cast<std::uint32_t>(rng() % region.register_count); };
    const auto blocks = 1 + static_cast<std::uint32_t>(rng() % 6);
    std::vector<std::uint32_t> starts;
    // Ops whose targets name blocks until the starts are known.
    std::vector<std::size_t> fixups;
    std::vector<std::size_t> switch_fixups;
    for (std::uint32_t b = 0; b < blocks; ++b) {
        starts.push_back(static_cast<std::uint32_t>(region.code.size()));
        const auto image_block = 100 + b;
        region.blocks.push_back(image_block);
        // Two steps at least: a block completes once, and a jump may complete
        // it again, as a checkpoint of a real region counts.
        region.checkpoints.push_back({image_block, 2 + rng() % 3, rng() % 3});
        Op budget;
        budget.code = Code::Budget;
        budget.a = b;
        budget.block = image_block;
        region.code.push_back(budget);
        const auto count = rng() % 9;
        for (std::uint64_t i = 0; i < count; ++i) {
            Op op;
            op.block = image_block;
            switch (rng() % 10) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                op.code = Code::Lift;
                op.index = static_cast<std::uint32_t>(region.instructions.size());
                region.instructions.push_back(random_instruction(rng));
                op.a = reg();
                op.b = reg();
                op.dst = reg();
                if (op.dst < slots && rng() % 2) op.flags = nvm::tier2::defines;
                break;
            case 5:
                op.code = Code::Mask;
                op.a = reg();
                op.dst = reg();
                op.width = static_cast<std::uint8_t>(1 + rng() % 64);
                break;
            case 6:
                op.code = Code::Move;
                op.a = reg();
                op.dst = reg();
                break;
            case 7: {
                op.code = Code::Gather;
                op.dst = reg();
                op.width = static_cast<std::uint8_t>(1 + rng() % 9);
                op.index = static_cast<std::uint32_t>(region.gathers.size());
                for (unsigned k = 0; k < op.width; ++k) {
                    region.gathers.push_back({reg(), static_cast<std::uint32_t>(rng() % 64)});
                }
                break;
            }
            case 8:
                op.code = Code::Store;
                op.a = reg();
                op.dst = static_cast<std::uint32_t>(rng() % slots);
                break;
            default:
                op.code = Code::Poll;
                break;
            }
            region.code.push_back(op);
        }
        Op complete;
        complete.code = Code::Complete;
        complete.block = image_block;
        region.code.push_back(complete);
        Op end;
        end.block = image_block;
        switch (rng() % 6) {
        case 0:
        case 1:
            end.code = Code::Branch;
            end.a = reg();
            end.target = static_cast<std::uint32_t>(rng() % blocks);
            end.other = static_cast<std::uint32_t>(rng() % blocks);
            fixups.push_back(region.code.size());
            break;
        case 2: {
            Op select;
            select.code = Code::Mask;
            select.dst = reg();
            select.a = reg();
            select.width = 2;
            region.code.push_back(select);
            end.code = Code::Switch;
            end.a = select.dst;
            end.width = static_cast<std::uint8_t>(1 + rng() % 3);
            end.index = static_cast<std::uint32_t>(region.operands.size());
            for (unsigned k = 0; k < end.width; ++k) {
                region.operands.push_back(static_cast<std::uint32_t>(rng() % blocks));
            }
            switch_fixups.push_back(region.code.size());
            break;
        }
        case 3:
            end.code = Code::Jump;
            end.target = static_cast<std::uint32_t>(rng() % blocks);
            end.flags = rng() % 2 ? nvm::tier2::completes : 0;
            fixups.push_back(region.code.size());
            break;
        case 4:
            end.code = Code::Leave;
            end.target = 200 + static_cast<std::uint32_t>(rng() % 50);
            end.flags = rng() % 2 ? nvm::tier2::completes : 0;
            break;
        default:
            end.code = Code::Deopt;
            break;
        }
        region.code.push_back(end);
    }
    for (const auto at : fixups) {
        auto& op = region.code[at];
        op.target = starts[op.target];
        if (op.code == Code::Branch) op.other = starts[op.other];
    }
    for (const auto at : switch_fixups) {
        const auto& op = region.code[at];
        for (unsigned k = 0; k < op.width; ++k) {
            auto& target = region.operands[op.index + k];
            target = starts[target];
        }
    }
    return region;
}

} // namespace

NEURA_TEST(jit, random_region_programs_match_the_region_interpreter) {
    std::mt19937_64 rng(20261111);
    const bool execute = native_runs_here();
    std::map<jit::Outcome, std::uint64_t> outcomes;
    for (int program = 0; program < 1500; ++program) {
        const auto region = random_region(rng);
        const auto label = "random program " + std::to_string(program);
        const auto other = jit::generate(region, jit::Arch::AArch64);
        check(other.failure.empty(), label + ": no AArch64 code: " + other.failure);
        if (!execute) continue;
        const auto compiled = jit::compile(region);
        check(compiled.program != nullptr, label + ": not compiled: " + compiled.failure);
        for (int run = 0; run < 6; ++run) {
            std::vector<std::uint64_t> regs(region.register_count);
            for (auto& value : regs) value = random_value(rng);
            std::vector<std::uint8_t> init(region.register_count);
            for (auto& flag : init) flag = rng() % 2;
            Machine machine;
            machine.steps = rng() % 50;
            machine.max_steps = rng() % 4 == 0 ? ~0ULL : rng() % 200;
            machine.allowance = 1 + rng() % 200;
            machine.scheduler = rng() % 2;
            machine.quantum = 1 + rng() % 40;
            machine.polls = rng() % machine.quantum;
            auto native_regs = regs;
            auto native_init = init;
            auto native_machine = machine;
            const auto expected = interpret(region, regs, init, machine);
            const auto actual = run_native(*compiled.program, region, native_regs, native_init, native_machine);
            const auto where = label + " run " + std::to_string(run);
            check_equal(actual, expected, where + " ending");
            check(native_regs == regs, where + ": registers differ");
            check(native_init == init, where + ": initialized flags differ");
            check_equal(native_machine.polls, machine.polls, where + " polls");
            ++outcomes[expected.outcome];
        }
    }
    if (execute) {
        for (const auto outcome : {jit::Outcome::Leave, jit::Outcome::Budget, jit::Outcome::Checks,
                                   jit::Outcome::Selectors, jit::Outcome::Exits}) {
            check(outcomes[outcome] > 0, "random programs end with outcome " +
                                             std::to_string(static_cast<int>(outcome)));
        }
    }
}

// --- Whole runs ---------------------------------------------------------------

namespace {

nvm::TierOptions eager() {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    return options;
}

nvm::RunLimits limits(std::uint8_t tier, jit::Mode mode, nvm::TierCache* cache) {
    nvm::RunLimits result;
    result.max_steps = 20'000'000;
    result.tier = tier;
    result.jit = mode;
    result.tier_cache = cache;
    result.profile = true;
    return result;
}

void check_same(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
    for (std::size_t i = 0; i < std::min(actual.requests.size(), expected.requests.size()); ++i) {
        check_same_request(actual.requests[i], expected.requests[i], label);
    }
}

// Whether this process runs regions at all, and as native code.
bool regions_run() { return nvm::tier_ceiling() >= 2; }
bool native_enabled() { return regions_run() && jit::availability(jit::combine(jit::Mode::On, jit::environment_mode())).enabled; }

// Runs `image` at tier 0, at tier 2 on the interpreter, and at tier 2 with
// native code (each with eager regions in its own cache), under `adjust`,
// and checks the three agree. Returns the native cache's statistics.
template <typename Adjust>
void compare_modes(const nvr::Image& image, nvm::TierCache& interpreted, nvm::TierCache& native,
                   const std::string& label, const std::vector<std::string>& arguments, Adjust adjust) {
    auto reference_limits = limits(0, jit::Mode::Off, nullptr);
    adjust(reference_limits);
    const auto reference = run_captured(image, reference_limits, std::nullopt, arguments);
    auto off = limits(2, jit::Mode::Off, &interpreted);
    adjust(off);
    check_same(run_captured(image, off, std::nullopt, arguments), reference, label + " (interpreter)");
    if (!jit::availability(jit::Mode::On).enabled) return;
    auto on = limits(2, jit::Mode::On, &native);
    adjust(on);
    check_same(run_captured(image, on, std::nullopt, arguments), reference, label + " (native)");
}

void compare_modes(const nvr::Image& image, nvm::TierCache& interpreted, nvm::TierCache& native,
                   const std::string& label, const std::vector<std::string>& arguments = {}) {
    compare_modes(image, interpreted, native, label, arguments, [](nvm::RunLimits&) {});
}

fs::path tier_program(const Environment& environment, const std::string& name) {
    return backend_dir(environment) / "tier" / (name + ".nra");
}

// A loop whose wired block reads an object reference from the lanes of a
// uint64 element: nil for every element but `bad_at`, which holds a
// handle that names nothing. The commit checks such a reference (the
// region's CheckRef), so the region deoptimizes there and tier 1 traps.
//
//   block 0: values = [0, ..., 0, 4242 at bad_at, 0, ...]   (allocation)
//   block 1: next = [1, 2, ..., n - 1, 0]                     (allocation)
//   block 2: flags = [false, ..., true]                       (allocation)
//   block 3: i = 0
//   block 4: v = values[i]                                    (inline)
//   block 5: r = the object in v's lanes                      (wired, CheckRef)
//   block 6: i = next[i]                                      (inline)
//   block 7: c = flags[i]                                     (inline)
//   block 8: selector c: 0 continue at 4, 1 return 0
nvr::Image reference_walk(std::uint32_t length, std::uint32_t bad_at) {
    nvr::Image image;
    // 0 void, 1 bool, 2 int32, 3 text, 4 text[], 5 uint64, 6 uint64[],
    // 7 an object, 8 bool[]
    image.types = {{nvr::TypeKind::Void, 0, 0},     {nvr::TypeKind::Bool, 0, 0},  {nvr::TypeKind::Signed, 32, 0},
                   {nvr::TypeKind::Text, 0, 0},     {nvr::TypeKind::Array, 0, 3}, {nvr::TypeKind::Unsigned, 64, 0},
                   {nvr::TypeKind::Array, 0, 5},    {nvr::TypeKind::Object, 0, 0}, {nvr::TypeKind::Array, 0, 1}};
    image.layouts.push_back({});
    const auto u64 = [&](std::uint64_t value) {
        nvr::Constant constant{5, {}};
        for (int i = 0; i < 8; ++i) constant.payload.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        image.constants.push_back(constant);
        return nvr::Value{nvr::ValueKind::Constant, 5, static_cast<std::uint32_t>(image.constants.size() - 1)};
    };
    const auto boolean = [&](bool value) {
        image.constants.push_back({1, {static_cast<std::uint8_t>(value ? 1 : 0)}});
        return nvr::Value{nvr::ValueKind::Constant, 1, static_cast<std::uint32_t>(image.constants.size() - 1)};
    };
    const auto int32 = [&](std::uint32_t value) {
        nvr::Constant constant{2, {}};
        for (int i = 0; i < 4; ++i) constant.payload.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        image.constants.push_back(constant);
        return nvr::Value{nvr::ValueKind::Constant, 2, static_cast<std::uint32_t>(image.constants.size() - 1)};
    };
    // Graph 0: no lanes. Graph 1: one bool lane. Graph 2: 64 lanes copied.
    image.graphs.push_back({0, {}, {}});
    image.graphs.push_back({1, {}, {0}});
    nvr::Graph copy{64, {}, {}};
    for (std::uint32_t i = 0; i < 64; ++i) copy.outputs.push_back(i);
    image.graphs.push_back(copy);

    nvr::Function function;
    function.parameter_count = 1;
    function.result_type = 2;
    // Slots: 0 arguments, 1 values, 2 next, 3 flags, 4 i, 5 v, 6 r, 7 c
    function.frame_types = {4, 6, 6, 8, 5, 5, 7, 1};
    const auto slot = [](std::uint32_t index, std::uint32_t type) {
        return nvr::Value{nvr::ValueKind::Slot, type, index};
    };
    const auto allocate = [&](std::uint32_t type, std::vector<nvr::Value> elements, std::uint32_t result,
                              std::uint32_t resume) {
        nvr::Block block;
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::State;
        exit.operation = nvr::StateOp::AllocateCollection;
        exit.immediate = type;
        exit.arguments = std::move(elements);
        exit.result_slot = result;
        exit.resume = resume;
        block.exits.push_back(exit);
        return block;
    };
    std::vector<nvr::Value> values;
    std::vector<nvr::Value> next;
    std::vector<nvr::Value> flags;
    for (std::uint32_t i = 0; i < length; ++i) {
        values.push_back(u64(i == bad_at ? 4242 : 0));
        next.push_back(u64((i + 1) % length));
        flags.push_back(boolean(i + 1 == length));
    }
    function.blocks.push_back(allocate(6, values, 1, 1));
    function.blocks.push_back(allocate(6, next, 2, 2));
    function.blocks.push_back(allocate(8, flags, 3, 3));
    {
        nvr::Block block;
        nvr::Exit exit;
        exit.assignments = {{4, u64(0)}};
        exit.resume = 4;
        block.exits.push_back(exit);
        function.blocks.push_back(block);
    }
    const auto read = [&](nvr::Value array, std::uint32_t result, std::uint32_t resume) {
        nvr::Block block;
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::State;
        exit.operation = nvr::StateOp::ReadElement;
        exit.arguments = {array, slot(4, 5)};
        exit.result_slot = result;
        exit.resume = resume;
        block.exits.push_back(exit);
        return block;
    };
    function.blocks.push_back(read(slot(1, 6), 5, 5));
    {
        nvr::Block block;
        block.graph = 2;
        block.inputs = {slot(5, 5)};
        nvr::Exit exit;
        exit.assignments = {{6, nvr::Value{nvr::ValueKind::Lanes, 7, 0}}};
        exit.resume = 6;
        block.exits.push_back(exit);
        function.blocks.push_back(block);
    }
    function.blocks.push_back(read(slot(2, 6), 4, 7));
    function.blocks.push_back(read(slot(3, 8), 7, 8));
    {
        nvr::Block block;
        block.graph = 1;
        block.inputs = {slot(7, 1)};
        block.selector_start = 0;
        nvr::Exit again;
        again.resume = 4;
        nvr::Exit done;
        done.kind = nvr::ExitKind::Return;
        done.arguments = {int32(0)};
        block.exits = {again, done};
        function.blocks.push_back(block);
    }
    image.functions.push_back(function);
    image.entry = 0;
    image.minor_version = std::max<std::uint16_t>(2, nvr::required_minor_version(image));
    return nvr::load(nvr::encode(image));
}

} // namespace

NEURA_TEST(jit, native_regions_deoptimize_at_every_check_like_the_interpreter) {
    // Overflow (at both ends of int32), bounds, nil field reads,
    // modification during iteration, division by zero, a class cast, a
    // shift count, and a narrowing conversion, failing on each iteration
    // of a loop that runs in regions; and the loop without failure.
    const auto image = build_program({tier_program(environment, "region-guards")});
    nvm::TierCache interpreted(image, eager());
    nvm::TierCache native(image, eager());
    for (int mode = 0; mode <= 8; ++mode) {
        for (int k = 0; k < 12; ++k) {
            compare_modes(image, interpreted, native, "check " + std::to_string(mode) + " at " + std::to_string(k),
                          {std::to_string(mode), std::to_string(k)});
        }
    }
    if (native_enabled()) {
        const auto statistics = native.statistics();
        check(statistics.jit_compiled > 0, "regions were compiled");
        check(statistics.region_counts.native > 0 && statistics.region_counts.native == statistics.region_counts.entries,
              "every region entry ran native code");
        check(statistics.region_counts.checks > 0, "native code deoptimized at failed checks");
        check(statistics.region_counts.state_traps > 0, "native code deoptimized at trapping state operations");
        check(interpreted.statistics().region_counts.native == 0, "--jit=off ran no native code");
    }
}

NEURA_TEST(jit, removed_and_kept_overflow_checks_at_the_ends_of_their_types) {
    const auto image = build_program({tier_program(environment, "region-counters")});
    nvm::TierCache interpreted(image, eager());
    nvm::TierCache native(image, eager());
    for (const int distance : {0, 1, 2, 3, 4, 5, 7, 10, 20, 40}) {
        for (const int trap : {0, 1}) {
            compare_modes(image, interpreted, native,
                          "counters " + std::to_string(distance) + " below the top, trap " + std::to_string(trap),
                          {std::to_string(distance), std::to_string(trap)});
        }
    }
    if (native_enabled()) {
        const auto statistics = native.statistics();
        check(statistics.region_counts.native > 0, "the counters ran natively");
        check(statistics.region_counts.checks > 0, "an overflow check failed in native code");
    }
}

NEURA_TEST(jit, a_failing_reference_check_deoptimizes_native_code) {
    // The walk reads elements 0 to 10 of 12.
    for (const std::uint32_t bad_at : {0U, 1U, 2U, 5U, 10U}) {
        const auto image = reference_walk(12, bad_at);
        nvm::TierCache interpreted(image, eager());
        nvm::TierCache native(image, eager());
        compare_modes(image, interpreted, native, "an invalid reference at " + std::to_string(bad_at));
        const auto run = run_captured(image, limits(2, jit::Mode::Auto, &native)).result;
        check(!run.completed, "the walk traps at the invalid reference: " + describe_trap(run, image));
        check_equal(run.trap.block, std::uint32_t{5}, "the trap is at the wired block");
        if (native_enabled() && bad_at > 1) {
            check(native.statistics().region_counts.references > 0, "native code deoptimized at the reference check");
        }
    }
    // Without an invalid reference the walk finishes.
    const auto image = reference_walk(12, 1000);
    nvm::TierCache interpreted(image, eager());
    nvm::TierCache native(image, eager());
    compare_modes(image, interpreted, native, "a walk of nil references");
}

NEURA_TEST(jit, step_limits_and_task_quanta_land_on_the_same_block) {
    const auto image = build_program({tier_program(environment, "regions")});
    nvm::TierCache interpreted(image, eager());
    nvm::TierCache native(image, eager());
    for (std::uint64_t steps = 1; steps <= 1200; ++steps) {
        compare_modes(image, interpreted, native, "regions with " + std::to_string(steps) + " steps", {},
                      [&](nvm::RunLimits& l) { l.max_steps = steps; });
    }
    for (const std::uint32_t frames : {1U, 2U, 3U, 4U}) {
        compare_modes(image, interpreted, native, "regions with " + std::to_string(frames) + " frames", {},
                      [&](nvm::RunLimits& l) { l.max_frames = frames; });
    }
    if (native_enabled()) {
        const auto statistics = native.statistics();
        check(statistics.region_counts.native > 0, "the loops ran natively");
        check(statistics.region_counts.budget > 0, "step limits inside native regions went to tier 1");
    }
    for (const auto* name : {"fan-out", "worker-group", "select-rotation", "channel-order"}) {
        const auto path = environment.source_dir / "tests" / "concurrency" / "programs" / (std::string(name) + ".nra");
        const auto program = build_program({path});
        nvm::TierCache program_interpreted(program, eager());
        nvm::TierCache program_native(program, eager());
        for (const std::uint64_t quantum : {1ULL, 2ULL, 3ULL, 5ULL, 16ULL, 1024ULL}) {
            compare_modes(program, program_interpreted, program_native,
                          std::string(name) + " with quantum " + std::to_string(quantum), {},
                          [&](nvm::RunLimits& l) { l.task_quantum = quantum; });
        }
    }
}

NEURA_TEST(jit, references_are_written_back_before_native_code_ends_at_an_allocation) {
    const auto image = build_program({tier_program(environment, "region-gc")});
    nvm::TierCache interpreted(image, eager());
    nvm::TierCache native(image, eager());
    const auto peak = run_captured(image, limits(0, jit::Mode::Off, nullptr)).result.peak_heap_cells;
    for (std::uint64_t cells = 64; cells <= peak + 64; cells += std::max<std::uint64_t>(1, peak / 61)) {
        compare_modes(image, interpreted, native, "region-gc with " + std::to_string(cells) + " heap cells", {},
                      [&](nvm::RunLimits& l) { l.max_heap_cells = cells; });
    }
    if (native_enabled()) check(native.statistics().region_counts.native > 0, "the loops ran natively");
}

NEURA_TEST(jit, regions_fall_back_to_the_interpreter_when_code_cannot_be_mapped) {
    if (!regions_run()) return;
    const auto image = build_program({tier_program(environment, "regions")});
    const auto reference = run_captured(image, limits(0, jit::Mode::Off, nullptr));
    jit::set_load_failure_for_testing(true);
    nvm::TierCache cache(image, eager());
    const auto run = run_captured(image, limits(2, jit::Mode::Auto, &cache));
    jit::set_load_failure_for_testing(false);
    check_same(run, reference, "regions without executable memory");
    const auto statistics = cache.statistics();
    check_equal(statistics.region_counts.native, std::uint64_t{0}, "native entries without executable memory");
    check(statistics.region_counts.entries > 0, "the regions ran on the interpreter");
    if (native_enabled()) {
        check(!statistics.jit_failures.empty() && statistics.jit_compiled == 0, "the failures are counted");
        check(nvm::describe(statistics).find("tier: jit fallback") != std::string::npos,
              "--tier-stats names the fallback");
    }
    // Below tier 2 and with --jit=off nothing is compiled.
    nvm::TierCache tier1(image, eager());
    check_same(run_captured(image, limits(1, jit::Mode::On, &tier1)), reference, "tier 1 with --jit=on");
    check(tier1.statistics().jit_mode.empty() && tier1.statistics().jit_compiled == 0, "tier 1 compiles nothing");
    nvm::TierCache off(image, eager());
    check_same(run_captured(image, limits(2, jit::Mode::Off, &off)), reference, "--jit=off");
    check_equal(off.statistics().jit_compiled, std::uint64_t{0}, "--jit=off compiles nothing");
}

NEURA_TEST(jit, modes_parse_and_combine) {
    jit::Mode mode = jit::Mode::Auto;
    check(jit::parse_mode("off", mode) && mode == jit::Mode::Off, "off");
    check(jit::parse_mode("on", mode) && mode == jit::Mode::On, "on");
    check(jit::parse_mode("auto", mode) && mode == jit::Mode::Auto, "auto");
    check(!jit::parse_mode("yes", mode), "an unknown mode");
    check(jit::combine(jit::Mode::On, jit::Mode::Off) == jit::Mode::Off, "the environment's off wins");
    check(jit::combine(jit::Mode::Off, jit::Mode::On) == jit::Mode::Off, "a run's off wins");
    check(jit::combine(jit::Mode::Auto, jit::Mode::On) == jit::Mode::On, "the environment's on");
    check(jit::combine(jit::Mode::Auto, jit::Mode::Auto) == jit::Mode::Auto, "auto");
    check(!jit::availability(jit::Mode::Off).enabled, "off is never enabled");
#if defined(__x86_64__) && defined(__linux__)
    check(jit::availability(jit::Mode::Auto).enabled, "auto is enabled on x86-64 Linux");
#elif defined(__aarch64__) || defined(__APPLE__)
    check(!jit::availability(jit::Mode::Auto).enabled, "auto is off where the code is not verified by execution");
#endif
}

} // namespace neura::testing

// The native code generator of tier 2 (docs/runtime/NVM.md, "Native
// compilation"; src/jit). The `jit` suite (backend-jit) checks:
//
// - the encoders' bytes against an assembler's (tests/jit/encodings.inc,
//   from scripts/jit-encodings.py), for x86-64 and AArch64;
// - every integer instruction compiled inline, and those that call
//   lift::evaluate, against lift::evaluate on edge values of every width,
//   in and out of machine registers;
// - every float instruction compiled inline against lift::evaluate, bit
//   for bit, on edge patterns and pseudo-random operands;
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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <sstream>

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
    c["div rcx"] = [](X86& e) { e.div(rcx); };
    c["idiv rcx"] = [](X86& e) { e.idiv(rcx); };
    c["cqo"] = [](X86& e) { e.cqo(); };
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
    // The float operations.
    c["movq xmm0, rax"] = [](X86& e) { e.movq_to_xmm(xmm0, rax); };
    c["movq xmm1, rcx"] = [](X86& e) { e.movq_to_xmm(xmm1, rcx); };
    c["movq xmm1, r11"] = [](X86& e) { e.movq_to_xmm(xmm1, r11); };
    c["movd xmm0, eax"] = [](X86& e) { e.movd_to_xmm(xmm0, rax); };
    c["movd xmm1, r11d"] = [](X86& e) { e.movd_to_xmm(xmm1, r11); };
    c["movq rax, xmm0"] = [](X86& e) { e.movq_from_xmm(rax, xmm0); };
    c["movq rdx, xmm1"] = [](X86& e) { e.movq_from_xmm(rdx, xmm1); };
    c["movq r11, xmm0"] = [](X86& e) { e.movq_from_xmm(r11, xmm0); };
    c["movd eax, xmm0"] = [](X86& e) { e.movd_from_xmm(rax, xmm0); };
    c["movd ecx, xmm0"] = [](X86& e) { e.movd_from_xmm(rcx, xmm0); };
    c["movd r11d, xmm1"] = [](X86& e) { e.movd_from_xmm(r11, xmm1); };
    c["addsd xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::add, false, xmm0, xmm1); };
    c["subsd xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::sub, false, xmm0, xmm1); };
    c["mulsd xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::mul, false, xmm0, xmm1); };
    c["divsd xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::div, false, xmm0, xmm1); };
    c["addss xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::add, true, xmm0, xmm1); };
    c["subss xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::sub, true, xmm0, xmm1); };
    c["mulss xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::mul, true, xmm0, xmm1); };
    c["divss xmm0, xmm1"] = [](X86& e) { e.scalar(SseOp::div, true, xmm0, xmm1); };
    c["cmpeqsd xmm0, xmm1"] = [](X86& e) { e.compare_scalar(false, xmm0, xmm1, 0); };
    c["cmpneqsd xmm0, xmm1"] = [](X86& e) { e.compare_scalar(false, xmm0, xmm1, 4); };
    c["cmpltsd xmm0, xmm1"] = [](X86& e) { e.compare_scalar(false, xmm0, xmm1, 1); };
    c["cmplesd xmm0, xmm1"] = [](X86& e) { e.compare_scalar(false, xmm0, xmm1, 2); };
    c["cmpeqss xmm0, xmm1"] = [](X86& e) { e.compare_scalar(true, xmm0, xmm1, 0); };
    c["cmpneqss xmm0, xmm1"] = [](X86& e) { e.compare_scalar(true, xmm0, xmm1, 4); };
    c["cmpltss xmm0, xmm1"] = [](X86& e) { e.compare_scalar(true, xmm0, xmm1, 1); };
    c["cmpless xmm0, xmm1"] = [](X86& e) { e.compare_scalar(true, xmm0, xmm1, 2); };
    c["ucomisd xmm0, xmm0"] = [](X86& e) { e.ucomis(false, xmm0, xmm0); };
    c["ucomiss xmm0, xmm0"] = [](X86& e) { e.ucomis(true, xmm0, xmm0); };
    c["ucomisd xmm0, xmm1"] = [](X86& e) { e.ucomis(false, xmm0, xmm1); };
    c["cvtsi2sd xmm0, rax"] = [](X86& e) { e.convert_from_integer(false, xmm0, rax); };
    c["cvtsi2ss xmm0, rcx"] = [](X86& e) { e.convert_from_integer(true, xmm0, rcx); };
    c["cvtsi2sd xmm0, r11"] = [](X86& e) { e.convert_from_integer(false, xmm0, r11); };
    c["cvttsd2si rax, xmm0"] = [](X86& e) { e.truncate_to_integer(false, rax, xmm0); };
    c["cvttss2si rdx, xmm0"] = [](X86& e) { e.truncate_to_integer(true, rdx, xmm0); };
    c["cvttsd2si r11, xmm0"] = [](X86& e) { e.truncate_to_integer(false, r11, xmm0); };
    c["cvtss2sd xmm0, xmm0"] = [](X86& e) { e.convert_scalar(false, xmm0, xmm0); };
    c["cvtsd2ss xmm0, xmm0"] = [](X86& e) { e.convert_scalar(true, xmm0, xmm0); };
    c[".att_syntax; jnp .+4096"] = [](X86& e) { e.patch(e.jcc(CC::np), 4096); };
    c[".att_syntax; jp .+4096"] = [](X86& e) { e.patch(e.jcc(CC::p), 4096); };
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
    c["udiv x9, x9, x10"] = [](A64& e) { e.udiv(9, 9, 10); };
    c["sdiv x11, x9, x10"] = [](A64& e) { e.sdiv(11, 9, 10); };
    c["msub x9, x11, x10, x9"] = [](A64& e) { e.msub(9, 11, 10, 9); };
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
    // The float operations.
    c["fmov d0, x9"] = [](A64& e) { e.fmov_to_fp(false, 0, 9); };
    c["fmov s0, w9"] = [](A64& e) { e.fmov_to_fp(true, 0, 9); };
    c["fmov d1, x10"] = [](A64& e) { e.fmov_to_fp(false, 1, 10); };
    c["fmov s1, w12"] = [](A64& e) { e.fmov_to_fp(true, 1, 12); };
    c["fmov x9, d0"] = [](A64& e) { e.fmov_from_fp(false, 9, 0); };
    c["fmov w9, s0"] = [](A64& e) { e.fmov_from_fp(true, 9, 0); };
    c["fmov x11, d0"] = [](A64& e) { e.fmov_from_fp(false, 11, 0); };
    c["fadd d0, d0, d1"] = [](A64& e) { e.fadd(false, 0, 0, 1); };
    c["fsub d0, d0, d1"] = [](A64& e) { e.fsub(false, 0, 0, 1); };
    c["fmul d0, d0, d1"] = [](A64& e) { e.fmul(false, 0, 0, 1); };
    c["fdiv d0, d0, d1"] = [](A64& e) { e.fdiv(false, 0, 0, 1); };
    c["fadd s0, s0, s1"] = [](A64& e) { e.fadd(true, 0, 0, 1); };
    c["fsub s0, s0, s1"] = [](A64& e) { e.fsub(true, 0, 0, 1); };
    c["fmul s0, s0, s1"] = [](A64& e) { e.fmul(true, 0, 0, 1); };
    c["fdiv s0, s0, s1"] = [](A64& e) { e.fdiv(true, 0, 0, 1); };
    c["fcmp d0, d1"] = [](A64& e) { e.fcmp(false, 0, 1); };
    c["fcmp s0, s1"] = [](A64& e) { e.fcmp(true, 0, 1); };
    c["fcmp d0, d0"] = [](A64& e) { e.fcmp(false, 0, 0); };
    c["fcmp s0, s0"] = [](A64& e) { e.fcmp(true, 0, 0); };
    c["fcvt d0, s0"] = [](A64& e) { e.fcvt(false, 0, 0); };
    c["fcvt s0, d0"] = [](A64& e) { e.fcvt(true, 0, 0); };
    c["scvtf d0, x9"] = [](A64& e) { e.scvtf(false, 0, 9); };
    c["scvtf s0, x10"] = [](A64& e) { e.scvtf(true, 0, 10); };
    c["fcvtzs x9, d0"] = [](A64& e) { e.fcvtzs(false, 9, 0); };
    c["fcvtzs x11, s0"] = [](A64& e) { e.fcvtzs(true, 11, 0); };
    c["csel x9, x16, x9, vs"] = [](A64& e) { e.csel(9, 16, 9, vs); };
    c["csel x10, x16, x10, eq"] = [](A64& e) { e.csel(10, 16, 10, eq); };
    c["cset x9, mi"] = [](A64& e) { e.cset(9, mi); };
    c["cset x9, ge"] = [](A64& e) { e.cset(9, ge); };
    c["cset x9, ne"] = [](A64& e) { e.cset(9, ne); };
    c["mvn x17, x16"] = [](A64& e) { e.mvn(17, 16); };
    c["cmp x9, x17"] = [](A64& e) { e.cmp(9, 17); };
    c["movz x16, #0x7ff8, lsl #48"] = [](A64& e) { e.mov_immediate(16, 0x7FF8000000000000ULL); };
    c["movz x16, #0x7fc0, lsl #16"] = [](A64& e) { e.mov_immediate(16, 0x7FC00000ULL); };
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
                  std::vector<std::uint8_t>& init, Machine& machine, jit::Helper lift = jit::evaluate_helper) {
    jit::Context context;
    context.registers = regs.data();
    context.initialized = init.data();
    context.steps = &machine.steps;
    context.max_steps = machine.max_steps;
    context.allowance = machine.allowance;
    context.polls = machine.scheduler ? &machine.polls : nullptr;
    context.quantum = machine.quantum;
    context.lift = lift;
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
        const auto values = edge_values(std::clamp<unsigned>(in.width, 1, 64));
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
                             Opcode::ShiftLeft,  Opcode::ShiftRight, Opcode::Index,    Opcode::Constant,
                             Opcode::SubtractWrapping, Opcode::ValueHash};
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
    // Float operations on the integer edge values (float_operations_match_
    // lift_evaluate_bit_for_bit checks them on float values).
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

// --- Float operations ---------------------------------------------------------

namespace {

using lift::FloatType;

bool hardware_format(FloatType format) { return format == FloatType::Binary32 || format == FloatType::Binary64; }

std::string format_name(FloatType format) {
    switch (format) {
    case FloatType::Binary16: return "binary16";
    case FloatType::BFloat16: return "bfloat16";
    case FloatType::Binary32: return "binary32";
    case FloatType::Binary64: return "binary64";
    case FloatType::None: break;
    }
    return "none";
}

std::string hex64(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

double float_value(std::uint64_t bits, FloatType format) {
    if (format == FloatType::Binary64) {
        double value = 0;
        std::memcpy(&value, &bits, 8);
        return value;
    }
    const auto word = static_cast<std::uint32_t>(bits);
    float value = 0;
    std::memcpy(&value, &word, 4);
    return value;
}

std::uint64_t double_bits(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, 8);
    return bits;
}

std::uint64_t single_bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// Whether the generated code hands an operand to lift::evaluate: every
// instruction of a 16-bit format, and a truncation to an integer that is
// NaN, outside [-2^63, 2^63), or -2^63 itself (which the hardware's
// conversion reports as it reports the others). Everything else of
// binary32 and binary64 is inline.
bool calls_helper(const lift::Instruction& in, std::uint64_t a) {
    switch (in.op) {
    case lift::Opcode::FloatFromInteger: return !hardware_format(in.to_format);
    case lift::Opcode::FloatToInteger: {
        if (!hardware_format(in.format)) return true;
        const double t = std::trunc(float_value(a, in.format));
        return std::isnan(t) || t <= -9223372036854775808.0 || t >= 9223372036854775808.0;
    }
    default: return !hardware_format(in.format);
    }
}

// Whether generated code calls the lift helper anywhere: call qword ptr
// [r13 + 80] on x86-64, blr x16 on AArch64 (the only helper a one-
// instruction region calls).
bool code_calls_helper(const std::vector<std::uint8_t>& code, jit::Arch arch) {
    if (arch == jit::Arch::X86_64) {
        const std::uint8_t call[] = {0x41, 0xFF, 0x55, 0x50};
        return std::search(code.begin(), code.end(), std::begin(call), std::end(call)) != code.end();
    }
    for (std::size_t i = 0; i + 4 <= code.size(); i += 4) {
        const auto word = static_cast<std::uint32_t>(code[i]) | (static_cast<std::uint32_t>(code[i + 1]) << 8) |
                          (static_cast<std::uint32_t>(code[i + 2]) << 16) |
                          (static_cast<std::uint32_t>(code[i + 3]) << 24);
        if (word == 0xD63F0200U) return true;
    }
    return false;
}

std::uint64_t lift_helper_calls = 0;

std::uint64_t counting_helper(jit::Context* context, std::uint32_t index, std::uint64_t a, std::uint64_t b) {
    ++lift_helper_calls;
    return jit::evaluate_helper(context, index, a, b);
}

// Runs and failed checks of FloatRun.
std::uint64_t float_runs = 0;
std::uint64_t float_failures = 0;
std::uint64_t float_helper_runs = 0;

// One float instruction compiled for the host, in and out of machine
// registers, run on operand pairs against lift::evaluate: the same 64 bits
// in the destination, the same failures with the destination untouched,
// and the helper called exactly where calls_helper says. The code for both
// processors calls a helper only where the instruction may need one.
class FloatRun {
public:
    FloatRun(const lift::Instruction& in, std::string label) : in_(in), label_(std::move(label)) {
        for (const bool pinned : {false, true}) {
            regions_[pinned] = instruction_region(in, pinned);
            for (const auto arch : {jit::Arch::X86_64, jit::Arch::AArch64}) {
                const auto generated = jit::generate(regions_[pinned], arch);
                check(generated.failure.empty() && !generated.code.empty(),
                      label_ + ": no code: " + generated.failure);
                const bool may_call = !hardware_format(in.op == lift::Opcode::FloatFromInteger ? in.to_format
                                                                                               : in.format) ||
                                      in.op == lift::Opcode::FloatToInteger;
                check_equal(code_calls_helper(generated.code, arch), may_call,
                            label_ + (arch == jit::Arch::X86_64 ? " x86-64" : " AArch64") + " calls the helper");
            }
        }
        if (!native_runs_here()) return;
        for (const bool pinned : {false, true}) programs_[pinned] = compile_or_fail(regions_[pinned], label_);
    }

    FloatRun(const FloatRun&) = delete;
    FloatRun& operator=(const FloatRun&) = delete;

    void run(std::uint64_t a, std::uint64_t b, bool pinned) {
        constexpr std::uint64_t untouched = 0x5EED5EED5EED5EEDULL;
        const auto& region = regions_[pinned];
        regs_.assign(region.register_count, untouched);
        init_.assign(region.register_count, 0);
        regs_[0] = a;
        regs_[1] = b;
        Machine machine;
        const auto calls = lift_helper_calls;
        const auto ending = run_native(*programs_[pinned], region, regs_, init_, machine, counting_helper);
        const bool called = lift_helper_calls != calls;
        bool failed = false;
        const auto expected = lift::evaluate(in_, a, b, failed);
        const auto want_ending = failed ? Ending{jit::Outcome::Checks, 7, 0} : Ending{jit::Outcome::Leave, 9, 1};
        const auto want_value = failed ? untouched : expected;
        const bool want_call = calls_helper(in_, a);
        ++float_runs;
        float_failures += failed;
        float_helper_runs += called;
        if (ending == want_ending && regs_[2] == want_value && called == want_call) return;
        std::ostringstream message;
        message << label_ << (pinned ? " pinned" : "") << " on " << hex64(a) << ", " << hex64(b) << ": expected "
                << (failed ? "failure" : hex64(expected)) << (want_call ? " through the helper" : " inline")
                << ", got " << ending << " with " << hex64(regs_[2]) << (called ? " through the helper" : " inline");
        fail(message.str());
    }

    void run(std::uint64_t a, std::uint64_t b) {
        run(a, b, false);
        run(a, b, true);
    }

private:
    lift::Instruction in_;
    std::string label_;
    Region regions_[2];
    std::unique_ptr<jit::Program> programs_[2];
    std::vector<std::uint64_t> regs_;
    std::vector<std::uint8_t> init_;
};

// The bit patterns of a format's special values: zeros, infinities, quiet
// and signaling NaNs of both signs with payloads, the smallest and largest
// subnormals and normals, and one and its neighbours.
std::vector<std::uint64_t> special_patterns(FloatType format) {
    if (format == FloatType::Binary64) {
        return {0,
                0x8000000000000000ULL,
                0x7FF0000000000000ULL,
                0xFFF0000000000000ULL,
                0x7FF8000000000000ULL,
                0xFFF8000000000000ULL,
                0x7FF8000000000001ULL,
                0x7FFC000000000000ULL,
                0x7FFFFFFFFFFFFFFFULL,
                0xFFFFFFFFFFFFFFFFULL,
                0x7FF0000000000001ULL,
                0x7FF4000000000000ULL,
                0x7FF7FFFFFFFFFFFFULL,
                0xFFF0000000000001ULL,
                0xFFF4000000000123ULL,
                0x0000000000000001ULL,
                0x8000000000000001ULL,
                0x000FFFFFFFFFFFFFULL,
                0x800FFFFFFFFFFFFFULL,
                0x0010000000000000ULL,
                0x8010000000000000ULL,
                0x7FEFFFFFFFFFFFFFULL,
                0xFFEFFFFFFFFFFFFFULL,
                0x3FF0000000000000ULL,
                0x3FF0000000000001ULL,
                0x3FEFFFFFFFFFFFFFULL,
                0xBFF0000000000000ULL,
                0xBFF0000000000001ULL};
    }
    return {0,           0x80000000,  0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FC00001,
            0x7FE00000,  0x7FFFFFFF,  0xFFFFFFFF, 0x7F800001, 0x7FA00000, 0x7FBFFFFF, 0xFF800001,
            0xFFA00123,  0x00000001,  0x80000001, 0x007FFFFF, 0x807FFFFF, 0x00800000, 0x80800000,
            0x7F7FFFFF,  0xFF7FFFFF,  0x3F800000, 0x3F800001, 0x3F7FFFFF, 0xBF800000, 0xBF800001};
}

// Edge patterns of a binary32 or binary64 format: the special patterns;
// values whose sums and products round to even, overflow, or underflow;
// and, at each power of two 2^k for k in `powers`, the values just inside
// and outside the integer ranges it bounds (±2^k, ±(2^k ± 1), ±(2^k ± 0.5),
// and the neighbours of ±2^k). A binary32 operand also comes with bits
// above its 32: the code must ignore them as the reference does.
std::vector<std::uint64_t> float_edges(FloatType format, const std::vector<int>& powers) {
    const bool single = format == FloatType::Binary32;
    std::vector<double> values{0.5,    0.25,    0.75,    1.5,    2.5,     3.0,     0.1,     1.0 / 3.0, 0.9999999,
                               1e-300, 1e300,   1e-40,   1e38,   3.4e38,  1e-45,   0.375,   -0.5,      -1.5,
                               -2.5,   -0.9999999, -0.1, -1e-40, -1e300};
    // Rounding ties against 1.0 and their neighbours.
    for (const int k : {-53, -52, -24, -23}) {
        values.push_back(std::ldexp(1.0, k));
        values.push_back(std::ldexp(1.5, k));
        values.push_back(std::ldexp(3.0, k));
    }
    // Overflow and underflow.
    for (const int k : {127, 128, 1023, -126, -127, -149, -150, -1022, -1023, -1074}) {
        values.push_back(std::ldexp(1.0, k));
        values.push_back(std::ldexp(1.5, k));
    }
    for (const int k : powers) {
        for (const double sign : {1.0, -1.0}) {
            const double p = sign * std::ldexp(1.0, k);
            for (const double offset : {0.0, 1.0, -1.0, 0.5, -0.5}) values.push_back(p + offset);
            values.push_back(std::nextafter(p, 0.0));
            values.push_back(std::nextafter(p, sign * HUGE_VAL));
            if (single) {
                const auto f = static_cast<float>(p);
                values.push_back(std::nextafterf(f, 0.0F));
                values.push_back(std::nextafterf(f, static_cast<float>(sign * HUGE_VAL)));
            }
        }
    }
    auto patterns = special_patterns(format);
    for (const double v : values) patterns.push_back(single ? single_bits(static_cast<float>(v)) : double_bits(v));
    if (single) {
        for (const auto p : special_patterns(format)) {
            patterns.push_back(p | 0xFFFFFFFF00000000ULL);
            patterns.push_back(p | 0xA5A5A5A500000000ULL);
        }
        for (const auto v : {1.0F, -2.5F, 3.0e9F, -3.0e9F}) patterns.push_back(single_bits(v) | 0x1234567800000000ULL);
    }
    std::sort(patterns.begin(), patterns.end());
    patterns.erase(std::unique(patterns.begin(), patterns.end()), patterns.end());
    return patterns;
}

// A random float of `format` with an exponent within `spread` of `center`,
// a random sign and fraction; one in sixteen has the zero exponent and one
// in sixteen the all-ones one (zeros, subnormals, infinities, and NaNs),
// and one in four a fraction with its low half clear, so that sums and
// products are often exact or ties. One binary32 value in eight has random
// bits above its 32.
std::uint64_t biased_float(std::mt19937_64& rng, FloatType format, int center, int spread) {
    const bool single = format == FloatType::Binary32;
    const unsigned fraction_bits = single ? 23 : 52;
    const int top = single ? 255 : 2047;
    int exponent = 0;
    switch (rng() % 16) {
    case 0: exponent = 0; break;
    case 1: exponent = top; break;
    default:
        exponent = std::clamp(center + static_cast<int>(rng() % (2 * static_cast<unsigned>(spread) + 1)) - spread, 0,
                              top);
        break;
    }
    auto fraction = rng() & lift::mask_of(fraction_bits);
    if (rng() % 4 == 0) fraction &= ~lift::mask_of(fraction_bits / 2);
    const auto sign = rng() % 2;
    auto bits = (sign << (single ? 31 : 63)) | (static_cast<std::uint64_t>(exponent) << fraction_bits) | fraction;
    if (single && rng() % 8 == 0) bits |= rng() << 32;
    return bits;
}

int exponent_bias(FloatType format) { return format == FloatType::Binary32 ? 127 : 1023; }

// Integer operands of a conversion to float: the edge values of the width,
// every power of two with its neighbours and negations, and the values
// where binary32 and binary64 rounding ties or carries into the next
// power, below and above 2^63 (where the unsigned conversion halves its
// operand).
std::vector<std::uint64_t> integer_edges(unsigned width) {
    auto values = edge_values(width);
    for (unsigned k = 0; k <= 64; ++k) {
        const std::uint64_t p = k < 64 ? std::uint64_t{1} << k : 0;
        for (const std::uint64_t v : {p, p - 1, p + 1}) {
            values.push_back(v);
            values.push_back(0 - v);
        }
    }
    for (const std::uint64_t v :
         {0x20000000000001ULL, 0x20000000000003ULL, 0x1000001ULL, 0x1000003ULL, 0x8000000000000400ULL,
          0x8000000000000401ULL, 0x8000000000000BFFULL, 0x8000000000000C00ULL, 0xFFFFFFFFFFFFFBFFULL,
          0xFFFFFFFFFFFFFC00ULL, 0xFFFFFFFFFFFFFE00ULL, 0x8000008000000000ULL, 0x8000008000000001ULL,
          0x8000018000000000ULL, 0xFFFFFF7FFFFFFFFFULL, 0xFFFFFF8000000000ULL, 0x7FFFFFFFFFFFFDFFULL,
          0x7FFFFFFFFFFFFE00ULL, 0x7FFFFFBFFFFFFFFFULL, 0x7FFFFFC000000000ULL, 0x0123456789ABCDEFULL}) {
        values.push_back(v);
        values.push_back(0 - v);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

std::vector<int> all_powers() {
    std::vector<int> powers;
    for (int k = 0; k <= 65; ++k) powers.push_back(k);
    return powers;
}

lift::Instruction float_instruction(lift::Opcode op, FloatType format) {
    lift::Instruction in;
    in.op = op;
    in.format = format;
    in.width = static_cast<std::uint8_t>(format == FloatType::Binary32 ? 32 : format == FloatType::Binary64 ? 64 : 16);
    return in;
}

constexpr int random_pairs = 100000;

} // namespace

// Each float instruction compiled inline (binary32 and binary64 operations,
// conversions between them, and conversions to and from integers of every
// width) against lift::evaluate, bit for bit, on edge patterns and on
// pseudo-random pairs from a fixed seed: raw 64-bit patterns and floats
// with nearby exponents. Where native code does not run, the code for both
// processors is still generated and checked not to call the helper.
NEURA_TEST(jit, float_operations_match_lift_evaluate_bit_for_bit) {
    if (!lift::float_lowering_supported()) return;
    using lift::Opcode;
    const bool execute = native_runs_here();
    std::mt19937_64 rng(20261112);
    const FloatType formats[] = {FloatType::Binary32, FloatType::Binary64};
    const std::vector<int> pair_powers{0, 1, 8, 16, 23, 24, 31, 32, 52, 53, 63, 64};

    // Binary operations: every pair of edge patterns, then random pairs.
    const Opcode binary[] = {Opcode::FloatAdd,      Opcode::FloatSubtract,  Opcode::FloatMultiply,
                             Opcode::FloatDivide,   Opcode::FloatEqual,     Opcode::FloatNotEqual,
                             Opcode::FloatLess,     Opcode::FloatLessEqual, Opcode::FloatGreater,
                             Opcode::FloatGreaterEqual};
    for (const auto op : binary) {
        for (const auto format : formats) {
            FloatRun run(float_instruction(op, format), opcode_name(op) + " " + format_name(format));
            if (!execute) continue;
            const auto edges = float_edges(format, pair_powers);
            for (const auto a : edges) {
                for (const auto b : edges) run.run(a, b);
            }
            const int top = format == FloatType::Binary32 ? 255 : 2047;
            for (int i = 0; i < random_pairs; ++i) {
                run.run(rng(), rng(), i % 2 != 0);
                const int center = static_cast<int>(rng() % static_cast<unsigned>(top + 1));
                const int spread = rng() % 4 == 0 ? 30 : 3;
                run.run(biased_float(rng, format, center, spread), biased_float(rng, format, center, spread), i % 2 == 0);
            }
        }
    }

    // Negation and conversion between the formats.
    for (const auto format : formats) {
        const auto edges = float_edges(format, all_powers());
        const auto other = format == FloatType::Binary32 ? FloatType::Binary64 : FloatType::Binary32;
        auto convert = float_instruction(Opcode::FloatConvert, format);
        convert.to_format = other;
        for (const auto& in : {float_instruction(Opcode::FloatNegate, format), convert}) {
            FloatRun run(in, opcode_name(in.op) + " " + format_name(format) + " to " + format_name(in.to_format));
            if (!execute) continue;
            for (const auto a : edges) run.run(a, 0);
            for (int i = 0; i < random_pairs; ++i) {
                run.run(rng(), rng(), i % 2 != 0);
                const int center = static_cast<int>(rng() % static_cast<unsigned>(exponent_bias(format) * 2 + 2));
                run.run(biased_float(rng, format, center, 3), rng(), i % 2 == 0);
            }
        }
    }

    // Truncation to integers of every width and signedness.
    for (const auto format : formats) {
        const auto edges = float_edges(format, all_powers());
        for (const bool to_signed : {false, true}) {
            std::vector<std::unique_ptr<FloatRun>> runs(65);
            for (unsigned to = 1; to <= 64; ++to) {
                auto in = float_instruction(Opcode::FloatToInteger, format);
                in.to_width = static_cast<std::uint8_t>(to);
                in.to_signed = to_signed;
                runs[to] = std::make_unique<FloatRun>(in, "to integer " + format_name(format) + " to " +
                                                              std::to_string(to) + (to_signed ? "s" : "u"));
                if (!execute) continue;
                for (const auto a : edges) runs[to]->run(a, 0);
            }
            if (!execute) continue;
            for (int i = 0; i < random_pairs; ++i) {
                const auto to = 1 + static_cast<unsigned>(rng() % 64);
                runs[to]->run(rng(), rng(), i % 2 != 0);
                const auto to2 = 1 + static_cast<unsigned>(rng() % 64);
                const int spread = rng() % 4 == 0 ? 80 : 3;
                runs[to2]->run(biased_float(rng, format, exponent_bias(format) + static_cast<int>(to2), spread), rng(),
                               i % 2 == 0);
            }
        }
    }

    // Conversion from integers of every width and signedness.
    for (const auto format : formats) {
        for (const bool is_signed : {false, true}) {
            std::vector<std::unique_ptr<FloatRun>> runs(65);
            for (unsigned width = 1; width <= 64; ++width) {
                lift::Instruction in;
                in.op = Opcode::FloatFromInteger;
                in.width = static_cast<std::uint8_t>(width);
                in.is_signed = is_signed;
                in.to_format = format;
                runs[width] = std::make_unique<FloatRun>(in, "from integer " + std::to_string(width) +
                                                                 (is_signed ? "s" : "u") + " to " +
                                                                 format_name(format));
                if (!execute) continue;
                for (const auto a : integer_edges(width)) runs[width]->run(a, 0);
            }
            if (!execute) continue;
            for (int i = 0; i < random_pairs; ++i) {
                const auto width = 1 + static_cast<unsigned>(rng() % 64);
                runs[width]->run(rng(), rng(), i % 2 != 0);
                const auto width2 = 1 + static_cast<unsigned>(rng() % 64);
                auto value = rng() >> (rng() % 64);
                if (rng() % 2) value = 0 - value;
                runs[width2]->run(value, rng(), i % 2 == 0);
            }
        }
    }

    // The 16-bit formats stay on the helper, and agree with it.
    for (const auto format : {FloatType::Binary16, FloatType::BFloat16}) {
        auto to_integer = float_instruction(Opcode::FloatToInteger, format);
        to_integer.to_width = 32;
        to_integer.to_signed = true;
        auto convert = float_instruction(Opcode::FloatConvert, format);
        convert.to_format = FloatType::Binary64;
        for (const auto& in : {float_instruction(Opcode::FloatNegate, format),
                               float_instruction(Opcode::FloatLess, format), to_integer, convert}) {
            FloatRun run(in, opcode_name(in.op) + " " + format_name(format));
            if (!execute) continue;
            for (std::uint64_t a = 0; a < 0x10000; a += 7) run.run(a, 0x3C00);
        }
    }

    if (execute) {
        check(float_runs > 8000000 && float_failures > 500000 && float_helper_runs > 100000,
              "the float operands exercise every outcome: " + std::to_string(float_runs) + " runs, " +
                  std::to_string(float_failures) + " failing, " + std::to_string(float_helper_runs) +
                  " through the helper");
    }
}

namespace {

lift::Instruction random_instruction(std::mt19937_64& rng) {
    using lift::Opcode;
    static const Opcode ops[] = {
        Opcode::Add,    Opcode::Subtract,  Opcode::Multiply,     Opcode::Divide,     Opcode::Remainder,
        Opcode::Negate, Opcode::AddWrapping, Opcode::Equal,      Opcode::Less,       Opcode::GreaterEqual,
        Opcode::And,    Opcode::Not,       Opcode::BitAnd,       Opcode::BitXor,     Opcode::BitNot,
        Opcode::ShiftLeft, Opcode::ShiftRight, Opcode::Index,    Opcode::ConvertInteger, Opcode::FloatAdd};
    static const unsigned widths[] = {1, 8, 16, 32, 63, 64};
    lift::Instruction in;
    in.op = ops[rng() % std::size(ops)];
    in.width = static_cast<std::uint8_t>(widths[rng() % std::size(widths)]);
    in.is_signed = rng() % 2;
    in.to_width = static_cast<std::uint8_t>(widths[rng() % std::size(widths)]);
    in.to_signed = rng() % 2;
    // Random registers have bits above narrow widths, which can make a signed
    // division the hardware's INT64_MIN / -1 in lift::evaluate itself (no
    // region's operands do); at 64 bits that case is a failed check.
    if ((in.op == Opcode::Divide || in.op == Opcode::Remainder) && in.is_signed) in.width = 64;
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

// Whether this process runs regions at all, and as native code. Whole runs
// execute generated code under the same rule as the region programs above
// (native_runs_here): where `auto` enables it, and elsewhere only when the
// environment asks with NEURA_JIT=on, so that a build on a platform whose
// code is not yet verified by execution does not depend on it.
bool regions_run() { return nvm::tier_ceiling() >= 2; }
bool native_enabled() {
    return regions_run() && native_runs_here() &&
           jit::availability(jit::combine(jit::Mode::On, jit::environment_mode())).enabled;
}

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
    if (!native_enabled()) return;
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
        const auto& counts = statistics.region_counts;
        check(counts.native > 0 && counts.native == counts.entries, "every region entry ran native code");
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

NEURA_TEST(jit, an_exception_in_a_helper_leaves_native_code_as_it_leaves_the_interpreter) {
    // A task dump is served at a poll, inside a region too. Its writer
    // asks for the next dump each time and throws at the nth: the
    // exception must leave the run at the same poll in every mode, which in
    // native code means through the poll helper's status and a rethrow.
    if (!regions_run()) return;
    // Programs whose tasks loop, so that regions poll.
    for (const auto* name : {"worker-group", "select-rotation", "collect-parked"}) {
        const auto image = build_program(
            {environment.source_dir / "tests" / "concurrency" / "programs" / (std::string(name) + ".nra")});
        nvm::TierCache interpreted(image, eager());
        nvm::TierCache native(image, eager());
        for (const int nth : {1, 2, 3, 5, 10, 40, 200}) {
            const auto run = [&](std::uint8_t tier, jit::Mode mode, nvm::TierCache* cache) {
                nvm::TaskDumpRequest request;
                request.requested = true;
                int writes = 0;
                request.write = [&](const std::string&) {
                    if (++writes == nth) throw std::runtime_error("dump writer failed at " + std::to_string(writes));
                    request.requested = true;
                };
                auto l = limits(tier, mode, cache);
                l.task_dump = &request;
                std::string what = "no exception";
                try {
                    run_captured(image, l);
                } catch (const std::runtime_error& error) {
                    what = error.what();
                }
                return std::make_pair(what, writes);
            };
            const auto reference = run(0, jit::Mode::Off, nullptr);
            const auto label = std::string(name) + ": a dump writer failing at write " + std::to_string(nth);
            const auto off = run(2, jit::Mode::Off, &interpreted);
            check_equal(off.first, reference.first, label + " (interpreter)");
            check_equal(off.second, reference.second, label + " writes (interpreter)");
            if (!native_enabled()) continue;
            const auto on = run(2, jit::Mode::On, &native);
            check_equal(on.first, reference.first, label + " (native)");
            check_equal(on.second, reference.second, label + " writes (native)");
        }
        if (native_enabled()) check(native.statistics().region_counts.native > 0, "the tasks ran natively");
    }
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

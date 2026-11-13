// The x86-64 encoder of tier 3 (src/jit/x64.{hpp,cpp}). The test checks:
//
// - every instruction form, over every register, operand size and
//   addressing mode, against an assembler's bytes (x64-encodings.inc, from
//   scripts/x64-encodings.py);
// - labels: forward and backward jumps at the edge of rel8, a chain whose
//   widening cascades, alignment, RIP-relative constants, and random
//   programs whose jumps are decoded from the output and checked against
//   their targets and for minimality;
// - misuse that must throw;
// - on x86-64 Linux, small functions assembled, loaded through
//   jit::Program, and run: a loop, a call to a C++ function under the System V
//   ABI, floating-point operations, wide multiplication and division, bit
//   operations, byte registers, and every addressing mode through lea.
//
// It is a program of its own (no framework): it prints the failures and exits
// with their number.
#include "jit/jit.hpp"
#include "jit/x64.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__x86_64__) && defined(__linux__)
#include <emmintrin.h>
#define NEURA_X64_EXECUTION 1
#endif

namespace {

using namespace neura::jit::x64;
namespace jit = neura::jit;

using Bytes = std::vector<std::uint8_t>;

std::size_t checks = 0;
std::size_t failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (ok) return;
    if (++failures <= 40) std::fprintf(stderr, "FAIL: %s\n", what.c_str());
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

struct Encoding {
    const char* kind;
    const char* text;
    void (*emit)(Assembler&);
    const char* bytes;  // as a string literal of hex escapes
    std::size_t length;
};

const std::vector<Encoding>& encodings() {
    static const std::vector<Encoding> table = {
#include "jit/x64-encodings.inc"
    };
    return table;
}

void table_tests(bool print_counts) {
    std::map<std::string, std::size_t> counts;
    for (const auto& e : encodings()) {
        ++counts[e.kind];
        Assembler a;
        std::vector<std::uint8_t> got;
        try {
            e.emit(a);
            got = a.finish();
        } catch (const std::exception& ex) {
            check(false, std::string(e.text) + " throws " + ex.what());
            continue;
        }
        const Bytes want(e.bytes, e.bytes + e.length);
        check(got == want, std::string(e.text) + ": got " + hex(got) + ", the assembler gives " + hex(want));
    }
    if (print_counts) {
        for (const auto& [kind, count] : counts) std::printf("  %s: %zu\n", kind.c_str(), count);
    }
    std::printf("table: %zu cases\n", encodings().size());
}

// ---- labels and relaxation ----

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

Bytes fill(std::size_t n) {
    return Bytes(n, 0x90);
}

void fill_in(Assembler& a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) a.data8(0x90);
}

void edge_tests() {
    // Forward jmp: rel8 up to 127 bytes, rel32 from 128.
    for (const std::size_t n : {std::size_t{0}, std::size_t{127}}) {
        Assembler a;
        const Label l = a.label();
        a.jmp(l);
        fill_in(a, n);
        a.bind(l);
        check(a.finish() == concat({{0xEB, static_cast<std::uint8_t>(n)}, fill(n)}), "forward jmp of " + std::to_string(n));
    }
    {
        Assembler a;
        const Label l = a.label();
        a.jmp(l);
        fill_in(a, 128);
        a.bind(l);
        check(a.finish() == concat({{0xE9, 0x80, 0, 0, 0}, fill(128)}), "forward jmp of 128 is rel32");
    }
    // Backward jmp: the displacement counts the jump itself, so the fill of
    // 126 gives -128 and 127 gives -129.
    {
        Assembler a;
        const Label l = a.label();
        a.bind(l);
        fill_in(a, 126);
        a.jmp(l);
        check(a.finish() == concat({fill(126), {0xEB, 0x80}}), "backward jmp at -128");
    }
    {
        Assembler a;
        const Label l = a.label();
        a.bind(l);
        fill_in(a, 127);
        a.jmp(l);
        check(a.finish() == concat({fill(127), {0xE9, 0x7C, 0xFF, 0xFF, 0xFF}}), "backward jmp at -129 is rel32");
    }
    // Conditional jumps: 2 bytes short, 6 long (the displacement counts it).
    {
        Assembler a;
        const Label l = a.label();
        a.bind(l);
        fill_in(a, 126);
        a.jcc(Cond::ne, l);
        check(a.finish() == concat({fill(126), {0x75, 0x80}}), "backward jne at -128");
    }
    {
        Assembler a;
        const Label l = a.label();
        a.bind(l);
        fill_in(a, 127);
        a.jcc(Cond::l, l);
        check(a.finish() == concat({fill(127), {0x0F, 0x8C, 0x7B, 0xFF, 0xFF, 0xFF}}), "backward jl at -129 is rel32");
    }
    {
        Assembler a;
        const Label l = a.label();
        a.jcc(Cond::e, l);
        fill_in(a, 127);
        a.bind(l);
        check(a.finish() == concat({{0x74, 0x7F}, fill(127)}), "forward je at +127");
    }
    {
        Assembler a;
        const Label l = a.label();
        a.jcc(Cond::e, l);
        fill_in(a, 128);
        a.bind(l);
        check(a.finish() == concat({{0x0F, 0x84, 0x80, 0, 0, 0}, fill(128)}), "forward je at +128 is rel32");
    }
    // A call is always rel32.
    {
        Assembler a;
        const Label l = a.label();
        a.call(l);
        a.bind(l);
        check(a.finish() == Bytes({0xE8, 0, 0, 0, 0}), "call to the next instruction");
    }
    // Label offsets.
    {
        Assembler a;
        const Label first = a.label(), second = a.label();
        a.bind(first);
        a.jmp(second);
        fill_in(a, 3);
        a.bind(second);
        a.ret();
        const auto code = a.finish();
        check(a.offset(first) == 0 && a.offset(second) == 5 && code.size() == 6, "offsets of labels");
    }
}

// A chain whose widening cascades: j_i is within one byte of the edge once
// the jumps after it widen, one pass at a time.
void cascade_tests() {
    for (const int count : {2, 3, 6, 9, 20}) {
        Assembler a;
        std::vector<Label> labels;
        for (int i = 0; i < count; ++i) labels.push_back(a.label());
        for (int i = 0; i < count; ++i) a.jmp(labels[static_cast<std::size_t>(i)]);
        int previous = 0;
        for (int i = 0; i < count; ++i) {
            const int target = 128 - 5 * (count - 1 - i);
            if (target < previous) break;
            fill_in(a, static_cast<std::size_t>(target - previous));
            a.bind(labels[static_cast<std::size_t>(i)]);
            previous = target;
        }
        const auto code = a.finish();
        // Every jump widens: the last at once, the others one after
        // another. (A chain too long for the arithmetic above is cut short;
        // its labels are bound at the end.)
        bool all_wide = true;
        std::size_t pos = 0;
        for (int i = 0; i < count && pos < code.size(); ++i) {
            if (code[pos] != 0xE9) all_wide = false;
            pos += code[pos] == 0xE9 ? 5 : 2;
        }
        if (count <= 20 && 128 - 5 * (count - 1) >= 0) check(all_wide, "the chain of " + std::to_string(count) + " jumps widens in a cascade");
    }
}

// The length of the nop at `pos`, or 0.
std::size_t nop_length(const Bytes& code, std::size_t pos) {
    std::size_t p = pos;
    if (code[p] == 0x90) return 1;
    if (code[p] == 0x66) ++p;
    if (p < code.size() && code[p] == 0x90 && p > pos) return 2;
    if (p + 2 < code.size() && code[p] == 0x0F && code[p + 1] == 0x1F) {
        const auto modrm = code[p + 2];
        std::size_t length = p - pos + 3;
        const unsigned mod = modrm >> 6;
        if ((modrm & 7U) == 4) ++length;  // SIB
        length += mod == 1 ? 1 : mod == 2 ? 4 : 0;
        return length;
    }
    return 0;
}

struct DecodedJump {
    std::size_t start;
    std::size_t length;
    std::int64_t target;
};

// The jumps of code made of jumps and nops.
bool decode(const Bytes& code, std::vector<DecodedJump>& jumps) {
    std::size_t pos = 0;
    while (pos < code.size()) {
        if (const auto n = nop_length(code, pos)) {
            pos += n;
        } else if (code[pos] == 0xEB || (code[pos] >= 0x70 && code[pos] < 0x80)) {
            const auto d = static_cast<std::int8_t>(code[pos + 1]);
            jumps.push_back({pos, 2, static_cast<std::int64_t>(pos) + 2 + d});
            pos += 2;
        } else if (code[pos] == 0xE9) {
            std::int32_t d;
            std::memcpy(&d, &code[pos + 1], 4);
            jumps.push_back({pos, 5, static_cast<std::int64_t>(pos) + 5 + d});
            pos += 5;
        } else if (code[pos] == 0x0F && code[pos + 1] >= 0x80 && code[pos + 1] < 0x90) {
            std::int32_t d;
            std::memcpy(&d, &code[pos + 2], 4);
            jumps.push_back({pos, 6, static_cast<std::int64_t>(pos) + 6 + d});
            pos += 6;
        } else {
            return false;
        }
    }
    return true;
}

// Random programs of jumps, fills and (optionally) alignment: every jump
// reaches its label, and without alignment no long jump could be short.
void random_program_tests() {
    std::mt19937 rng(20251113);
    for (int round = 0; round < 600; ++round) {
        const bool aligned = round % 3 == 2;
        Assembler a;
        const int label_count = 1 + static_cast<int>(rng() % 40);
        std::vector<Label> labels;
        for (int i = 0; i < label_count; ++i) labels.push_back(a.label());
        std::vector<int> targets;  // the label of each jump, in order
        std::vector<int> bound_order;
        int next_bind = 0;
        const int steps = 5 + static_cast<int>(rng() % 120);
        std::vector<Label> aligned_labels;
        for (int s = 0; s < steps; ++s) {
            switch (rng() % 6) {
            case 0:
            case 1: {
                const int target = static_cast<int>(rng() % static_cast<unsigned>(label_count));
                if (rng() % 3 == 0) a.jmp(labels[static_cast<std::size_t>(target)]);
                else a.jcc(static_cast<Cond>(rng() % 16), labels[static_cast<std::size_t>(target)]);
                targets.push_back(target);
                break;
            }
            case 2:
                fill_in(a, rng() % 4 == 0 ? 60 + rng() % 90 : rng() % 12);
                break;
            case 3:
                if (aligned) {
                    a.align(16);
                    if (next_bind < label_count) {
                        aligned_labels.push_back(labels[static_cast<std::size_t>(next_bind)]);
                        a.bind(labels[static_cast<std::size_t>(next_bind++)]);
                    }
                } else {
                    fill_in(a, rng() % 30);
                }
                break;
            default:
                if (next_bind < label_count) a.bind(labels[static_cast<std::size_t>(next_bind++)]);
                break;
            }
        }
        while (next_bind < label_count) a.bind(labels[static_cast<std::size_t>(next_bind++)]);
        const auto code = a.finish();
        std::vector<DecodedJump> jumps;
        if (!decode(code, jumps)) {
            check(false, "random program " + std::to_string(round) + " decodes");
            continue;
        }
        check(jumps.size() == targets.size(), "random program " + std::to_string(round) + " keeps its jumps");
        if (jumps.size() != targets.size()) continue;
        for (std::size_t i = 0; i < jumps.size(); ++i) {
            const auto want = static_cast<std::int64_t>(a.offset(labels[static_cast<std::size_t>(targets[i])]));
            check(jumps[i].target == want, "random program " + std::to_string(round) + " jump " + std::to_string(i) + " reaches its label");
            if (!aligned && jumps[i].length > 2) {
                // The displacement the jump would have as a short one.
                const auto d = jumps[i].target - static_cast<std::int64_t>(jumps[i].start + jumps[i].length);
                const auto shortened = jumps[i].target > static_cast<std::int64_t>(jumps[i].start) ? d : d + static_cast<std::int64_t>(jumps[i].length) - 2;
                check(shortened < -128 || shortened > 127, "random program " + std::to_string(round) + " jump " + std::to_string(i) + " is no wider than needed");
            }
        }
        for (const auto l : aligned_labels) check(a.offset(l) % 16 == 0, "an aligned label is aligned");
        // The same calls give the same bytes: build it again.
    }
}

void align_tests() {
    // Padding is multi-byte nops of at most 9 bytes up to the boundary.
    for (std::size_t lead = 0; lead < 40; ++lead) {
        Assembler a;
        fill_in(a, lead);
        a.align(16);
        const Label l = a.label();
        a.bind(l);
        a.ret();
        const auto code = a.finish();
        const auto pad = (16 - lead % 16) % 16;
        check(a.offset(l) == lead + pad && code.size() == lead + pad + 1, "alignment of " + std::to_string(lead));
        std::size_t pos = lead;
        bool ok = true;
        while (pos < lead + pad) {
            const auto n = nop_length(code, pos);
            if (n == 0 || n > 9) {
                ok = false;
                break;
            }
            pos += n;
        }
        check(ok && pos == lead + pad, "padding of " + std::to_string(lead) + " is whole nops");
    }
    // Alignment between a jump and its target: padding that grows or shrinks
    // as the jump widens; the result stays consistent.
    for (std::size_t gap = 100; gap < 140; ++gap) {
        Assembler a;
        const Label l = a.label();
        a.jmp(l);
        fill_in(a, gap);
        a.align(16);
        a.bind(l);
        a.ret();
        const auto code = a.finish();
        std::vector<DecodedJump> jumps;
        check(decode(Bytes(code.begin(), code.end() - 1), jumps) && jumps.size() == 1 &&
                  jumps[0].target == static_cast<std::int64_t>(a.offset(l)) && a.offset(l) % 16 == 0,
              "a jump over alignment, gap " + std::to_string(gap));
    }
    // Calling with a loop head aligned.
    {
        Assembler a;
        a.nop(3);
        a.align(16);
        const Label head = a.label();
        a.bind(head);
        a.alu(Alu::sub, S64, rax, Imm32{1});
        a.jcc(Cond::ne, head);
        a.ret();
        const auto code = a.finish();
        check(a.offset(head) == 16 && code.size() == 16 + 4 + 2 + 1, "a loop head aligned to 16");
    }
}

void rip_tests() {
    // A constant after the code, reached by RIP-relative operands ahead and
    // behind, with an immediate after the displacement.
    Assembler a;
    const Label behind = a.label();
    a.bind(behind);
    a.data64(0x1122334455667788ULL);
    const Label ahead = a.label();
    a.mov(S64, rax, Mem::rip_relative(behind));
    a.alu(Alu::add, S32, Mem::rip_relative(ahead, 4), Imm32{1000});
    a.ret();
    a.bind(ahead);
    a.data64(0);
    const auto code = a.finish();
    // mov rax, [rip + disp]: the displacement is relative to the end of the
    // instruction (8 + 7 bytes in), to offset 0.
    check(code[8] == 0x48 && code[9] == 0x8B && code[10] == 0x05, "rip mov opcode");
    std::int32_t d1, d2;
    std::memcpy(&d1, &code[11], 4);
    check(d1 == -15, "rip displacement behind: " + std::to_string(d1));
    // add dword ptr [rip + d], 1000: 81 05 d32 i32 (the displacement ends 4
    // bytes before the instruction does).
    check(code[15] == 0x81 && code[16] == 0x05, "rip add opcode");
    std::memcpy(&d2, &code[17], 4);
    const auto end_of_add = 15 + 2 + 4 + 4;
    check(d2 == static_cast<std::int32_t>(a.offset(ahead)) + 4 - end_of_add, "rip displacement ahead with an immediate");
}

void misuse_tests() {
    auto throws = [](const std::string& what, auto&& body) {
        bool threw = false;
        try {
            body();
        } catch (const std::logic_error&) {
            threw = true;
        }
        check(threw, what + " throws");
    };
    throws("an unbound label", [] {
        Assembler a;
        const Label l = a.label();
        a.jmp(l);
        a.finish();
    });
    throws("a reference to an unbound label", [] {
        Assembler a;
        const Label l = a.label();
        a.call(l);
        a.finish();
    });
    throws("a label bound twice", [] {
        Assembler a;
        const Label l = a.label();
        a.bind(l);
        a.bind(l);
    });
    throws("rsp as an index", [] { Mem::at(rax, rsp, 2, 0); });
    throws("rsp as an index without a base", [] { Mem::absolute(rsp, 2, 0); });
    throws("a scale of 3", [] { Mem::at(rax, rcx, 3, 0); });
    throws("movzx to the same size", [] {
        Assembler a;
        a.movzx(S16, rax, S16, rcx);
    });
    throws("an 8-bit imul", [] {
        Assembler a;
        a.imul(S8, rax, rcx);
    });
    throws("an 8-bit immediate out of range", [] {
        Assembler a;
        a.mov(S8, rax, Imm32{256});
    });
    throws("an alignment that is not a power of two", [] {
        Assembler a;
        a.align(12);
    });
    throws("finish twice", [] {
        Assembler a;
        a.ret();
        a.finish();
        a.finish();
    });
    // The same calls give the same bytes.
    auto build = [] {
        Assembler a;
        const Label top = a.label(), end = a.label();
        a.bind(top);
        a.alu(Alu::add, S64, rax, Imm32{1});
        a.jcc(Cond::o, end);
        fill_in(a, 200);
        a.jmp(top);
        a.bind(end);
        a.ret();
        return a.finish();
    };
    check(build() == build(), "the same calls give the same bytes");
}

#ifdef NEURA_X64_EXECUTION

// ---- execution ----

__extension__ typedef unsigned __int128 uint128;

using Code = Bytes;

std::unique_ptr<jit::Program> load(const Code& code, const char* what) {
    std::string failure;
    auto program = jit::Program::load(code, failure);
    if (!program) check(false, std::string(what) + " cannot be loaded: " + failure);
    return program;
}

// The offsets of jit::Context fields that the functions read and write.
constexpr std::int32_t kMaxSteps = 24, kAllowance = 32, kQuantum = 48, kDone = 56, kNext = 64, kOutcome = 68;
constexpr std::int32_t kLift = 80;

Mem ctx(std::int32_t offset) {
    return Mem::at(rdi, offset);
}

std::uint64_t double_bits(double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, 8);
    return bits;
}

float bits_float(std::uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
}

double bits_double(std::uint64_t bits) {
    double v;
    std::memcpy(&v, &bits, 8);
    return v;
}

void sum_loop_test() {
    // done = n + (n-1) + ... + 1, with n in max_steps.
    Assembler a;
    const Label top = a.label(), end = a.label();
    a.mov(S64, rax, ctx(kMaxSteps));
    a.alu(Alu::xor_, S32, rcx, rcx);
    a.align(16);
    a.bind(top);
    a.test(S64, rax, rax);
    a.jcc(Cond::e, end);
    a.alu(Alu::add, S64, rcx, rax);
    a.alu(Alu::sub, S64, rax, Imm32{1});
    a.jmp(top);
    a.bind(end);
    a.mov(S64, ctx(kDone), rcx);
    a.ret();
    const auto program = load(a.finish(), "the sum loop");
    if (!program) return;
    for (const std::uint64_t n : {0ULL, 1ULL, 10ULL, 1000ULL, 123456ULL}) {
        jit::Context context;
        context.max_steps = n;
        program->run(context);
        check(context.done == n * (n + 1) / 2, "sum of 1.." + std::to_string(n));
    }
}

std::uint64_t called_helper(jit::Context* context, std::uint32_t immediate, std::uint64_t a, std::uint64_t b)
    __attribute__((noinline));

std::uint64_t called_helper(jit::Context* context, std::uint32_t immediate, std::uint64_t a, std::uint64_t b) {
    // At a call the stack is 16-byte aligned, so with its return address
    // pushed and rbp saved, the frame pointer is again a multiple of 16.
    const bool aligned = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0)) % 16 == 0;
    context->status = aligned ? 1 : 0;
    return a * b + immediate;
}

void helper_call_test() {
    // A function that keeps a value on its stack across a call: push rbx
    // aligns the stack, sub rsp, 32 keeps it aligned.
    Assembler a;
    a.push(rbx);
    a.alu(Alu::sub, S64, rsp, Imm32{32});
    a.mov(S64, rbx, rdi);
    a.mov(S64, Mem::at(rsp, 8), rbx);
    a.mov(S32, rsi, Imm32{7});
    a.mov(S64, rdx, Mem::at(rbx, kMaxSteps));
    a.mov(S64, rcx, Mem::at(rbx, kAllowance));
    a.call(Mem::at(rbx, kLift));
    a.mov(S64, rbx, Mem::at(rsp, 8));
    a.mov(S64, Mem::at(rbx, kDone), rax);
    a.alu(Alu::add, S64, rsp, Imm32{32});
    a.pop(rbx);
    a.ret();
    const auto program = load(a.finish(), "the helper call");
    if (!program) return;
    jit::Context context;
    context.max_steps = 123456789;
    context.allowance = 1000;
    context.lift = &called_helper;
    program->run(context);
    check(context.done == 123456789ULL * 1000 + 7, "the helper's result comes back");
    check(context.status == 1, "the stack is 16-byte aligned at the call");
}

void float_test() {
    // From the doubles a (max_steps) and b (allowance):
    //   done = (a * b + sqrt(a)) / 2 - b
    //   max_steps = -a (xorpd), allowance = |a| (andnpd), registers = |b| (andpd)
    //   quantum = (int64) a, outcome = bits of (float)((float)a + 1.5f)
    //   next_block = (a > b) | (unordered << 1) | ((a == a) << 2)
    Assembler a;
    const Label constants = a.label(), end = a.label();
    a.movs(Prec::dbl, xmm0, ctx(kMaxSteps));
    a.movs(Prec::dbl, xmm1, ctx(kAllowance));
    a.movs(Prec::dbl, xmm2, xmm0);
    a.arith(Fp::mul, Prec::dbl, xmm2, xmm1);
    a.arith(Fp::sqrt, Prec::dbl, xmm3, xmm0);
    a.arith(Fp::add, Prec::dbl, xmm2, xmm3);
    a.arith(Fp::div, Prec::dbl, xmm2, Mem::rip_relative(constants, 32));
    a.arith(Fp::sub, Prec::dbl, xmm2, xmm1);
    a.movs(Prec::dbl, ctx(kDone), xmm2);
    a.movs(Prec::dbl, xmm4, xmm0);
    a.xorpd(xmm4, Mem::rip_relative(constants, 0));
    a.movs(Prec::dbl, ctx(kMaxSteps), xmm4);
    a.movs(Prec::dbl, xmm5, Mem::rip_relative(constants, 0));
    a.andnpd(xmm5, xmm0);
    a.movs(Prec::dbl, ctx(kAllowance), xmm5);
    a.movs(Prec::dbl, xmm6, xmm1);
    a.andpd(xmm6, Mem::rip_relative(constants, 16));
    a.movs(Prec::dbl, ctx(0), xmm6);
    a.cvtts2si(Prec::dbl, S64, rax, xmm0);
    a.mov(S64, ctx(kQuantum), rax);
    a.cvtts2si(Prec::dbl, S32, rax, xmm0);
    a.cvtsi2s(Prec::dbl, xmm11, S32, rax);
    a.movs(Prec::dbl, Mem::at(rdi, 40), xmm11);
    a.mov(S64, rax, ctx(kQuantum));
    a.cvtsi2s(Prec::dbl, xmm12, S64, rax);
    a.movs(Prec::dbl, Mem::at(rdi, 16), xmm12);
    a.mov(S64, rax, ctx(kQuantum));
    a.cvtsi2s(Prec::single, xmm13, S64, rax);
    a.cvtss2sd(xmm13, xmm13);
    a.movs(Prec::dbl, Mem::at(rdi, 112), xmm13);
    a.cvtsd2ss(xmm7, xmm0);
    a.mov(S32, rax, Imm32{0x3FC00000});  // 1.5f
    a.movd(xmm9, rax);
    a.arith(Fp::add, Prec::single, xmm7, xmm9);
    a.cvtss2sd(xmm10, xmm7);
    a.cvtsd2ss(xmm10, xmm10);
    a.movd(rax, xmm10);
    a.mov(S32, ctx(kOutcome), rax);
    a.mov(S64, rax, ctx(kAllowance));
    a.movq(xmm14, rax);
    a.movq(rax, xmm14);
    a.movs(Prec::single, xmm15, Mem::at(rdi, kAllowance));
    a.movs(Prec::single, Mem::at(rdi, 8), xmm15);
    a.alu(Alu::xor_, S32, rax, rax);
    a.alu(Alu::xor_, S32, rcx, rcx);
    a.alu(Alu::xor_, S32, rdx, rdx);
    a.ucomis(Prec::dbl, xmm0, xmm1);
    a.setcc(Cond::a, rax);
    a.setcc(Cond::p, rcx);
    a.shift(Shift::shl, S32, rcx, Imm8{1});
    a.alu(Alu::or_, S32, rax, rcx);
    a.ucomis(Prec::dbl, xmm0, xmm0);
    a.setcc(Cond::np, rdx);
    a.shift(Shift::shl, S32, rdx, Imm8{2});
    a.alu(Alu::or_, S32, rax, rdx);
    a.mov(S32, ctx(kNext), rax);
    a.jmp(end);
    a.align(16);
    a.bind(constants);
    a.data64(0x8000000000000000ULL);
    a.data64(0x8000000000000000ULL);
    a.data64(0x7FFFFFFFFFFFFFFFULL);
    a.data64(0x7FFFFFFFFFFFFFFFULL);
    a.data64(double_bits(2.0));
    a.bind(end);
    a.ret();
    // The labels: `constants` is at an aligned offset, `end` after it.
    const auto program = load(a.finish(), "the float function");
    if (!program) return;
    const double nan = std::nan("");
    const double cases[][2] = {{3.0, 4.0}, {-2.5, 1e10}, {0.0, -0.0}, {nan, 1.0}, {1.0, nan}, {1e300, 1e300},
                               {2.0, 2.0}, {1e-310, 7.0}, {-1.0, -3.0}, {123456.789, 0.5}};
    for (const auto& c : cases) {
        const double x = c[0], y = c[1];
        jit::Context context;
        context.max_steps = double_bits(x);
        context.allowance = double_bits(y);
        program->run(context);
        const auto label = "float case " + std::to_string(x) + ", " + std::to_string(y);
        const double expected = (x * y + std::sqrt(x)) / 2.0 - y;
        // NaN results compare by class, not payload: x86 produces the default NaN.
        const double got = bits_double(context.done);
        check((std::isnan(expected) && std::isnan(got)) || double_bits(expected) == context.done, label + ": the arithmetic");
        check(context.max_steps == (double_bits(x) ^ 0x8000000000000000ULL), label + ": xorpd");
        check(context.allowance == (double_bits(x) & 0x7FFFFFFFFFFFFFFFULL), label + ": andnpd");
        check(reinterpret_cast<std::uint64_t>(context.registers) == (double_bits(y) & 0x7FFFFFFFFFFFFFFFULL), label + ": andpd");
        check(static_cast<std::int64_t>(context.quantum) == _mm_cvttsd_si64(_mm_set_sd(x)), label + ": cvttsd2si");
        const auto as_int = static_cast<double>(_mm_cvttsd_si32(_mm_set_sd(x)));
        check(double_bits(as_int) == reinterpret_cast<std::uintptr_t>(context.polls), label + ": cvttsd2si and cvtsi2sd, 32 bits");
        check(double_bits(static_cast<double>(_mm_cvttsd_si64(_mm_set_sd(x)))) == reinterpret_cast<std::uintptr_t>(context.steps),
              label + ": cvtsi2sd, 64 bits");
        check(double_bits(static_cast<double>(static_cast<float>(_mm_cvttsd_si64(_mm_set_sd(x))))) == reinterpret_cast<std::uintptr_t>(context.region),
              label + ": cvtsi2ss, 64 bits");
        const float single_load = bits_float(static_cast<std::uint32_t>(double_bits(x) & 0x7FFFFFFFFFFFFFFFULL));
        std::uint32_t stored;
        std::memcpy(&stored, &context.initialized, 4);
        check(std::memcmp(&stored, &single_load, 4) == 0, label + ": movss load and store (of |x|'s low half)");
        const float single = static_cast<float>(x) + 1.5f;
        std::uint32_t single_bits;
        std::memcpy(&single_bits, &single, 4);
        const bool single_nan = std::isnan(single);
        check(context.outcome == single_bits || (single_nan && (context.outcome & 0x7F800000U) == 0x7F800000U),
              label + ": the single-precision path");
        const std::uint32_t flags = (x > y ? 1U : 0U) | ((std::isnan(x) || std::isnan(y)) ? 2U : 0U) | ((x == x) ? 4U : 0U);
        check(context.next_block == flags, label + ": ucomisd and setcc");
    }
}

std::uint64_t remainder_of(const jit::Context& context) {
    std::uint64_t value;
    std::memcpy(&value, &context.next_block, 8);
    return value;
}

void integer_test() {
    // rdx:rax = max_steps * allowance (mul), the halves to done and quantum;
    // then unsigned (hi:lo) / d with hi = allowance's high word... : the
    // quotient and remainder of idiv of the signed product by quantum.
    {
        Assembler a;
        a.mov(S64, rax, ctx(kMaxSteps));
        a.mov(S64, rcx, ctx(kAllowance));
        a.mul(S64, rcx);
        a.mov(S64, ctx(kDone), rax);
        a.mov(S64, ctx(kQuantum), rdx);
        a.ret();
        const auto program = load(a.finish(), "the mul function");
        if (program) {
            for (const auto& c : std::vector<std::pair<std::uint64_t, std::uint64_t>>{{3, 5}, {~0ULL, ~0ULL}, {1ULL << 63, 2}, {0x123456789ABCDEFULL, 0xFEDCBA987654321ULL}}) {
                jit::Context context;
                context.max_steps = c.first;
                context.allowance = c.second;
                program->run(context);
                const auto product = static_cast<uint128>(c.first) * c.second;
                check(context.done == static_cast<std::uint64_t>(product) && context.quantum == static_cast<std::uint64_t>(product >> 64), "mul");
            }
        }
    }
    {
        // done = quotient, next (64 bits at next_block) = remainder of
        // the unsigned rdx:rax by rcx; the signed divide of rax by rcx goes
        // to max_steps and allowance.
        Assembler a;
        a.mov(S64, rax, ctx(kMaxSteps));
        a.mov(S64, rdx, ctx(kAllowance));
        a.mov(S64, rcx, ctx(kQuantum));
        a.div(S64, rcx);
        a.mov(S64, ctx(kDone), rax);
        a.mov(S64, ctx(kNext), rdx);
        a.mov(S64, rax, ctx(kMaxSteps));
        a.cqo();
        a.idiv(S64, rcx);
        a.mov(S64, ctx(kMaxSteps), rax);
        a.mov(S64, ctx(kAllowance), rdx);
        a.mov(S32, rax, ctx(kQuantum));
        a.cdq();
        a.mov(S32, rcx, Imm32{-7});
        a.idiv(S32, rcx);
        a.mov(S64, ctx(kQuantum), rax);  // eax zero-extended after the 32-bit idiv
        a.ret();
        const auto program = load(a.finish(), "the div function");
        if (program) {
            struct Case {
                std::uint64_t lo, hi, d;
            };
            for (const Case c : {Case{100, 0, 7}, {~0ULL, 5, 1ULL << 62}, {1ULL << 63, 0, 3}, {12345678901234567ULL, 1, ~0ULL}}) {
                jit::Context context;
                context.max_steps = c.lo;
                context.allowance = c.hi;
                context.quantum = c.d;
                program->run(context);
                const auto n = (static_cast<uint128>(c.hi) << 64) | c.lo;
                check(context.done == static_cast<std::uint64_t>(n / c.d) && remainder_of(context) == static_cast<std::uint64_t>(n % c.d),
                      "div of " + std::to_string(c.lo));
                const auto sn = static_cast<std::int64_t>(c.lo);
                const auto sd = static_cast<std::int64_t>(c.d);
                if (sd != 0 && !(sn == INT64_MIN && sd == -1)) {
                    check(static_cast<std::int64_t>(context.max_steps) == sn / sd && static_cast<std::int64_t>(context.allowance) == sn % sd,
                          "idiv of " + std::to_string(sn));
                }
                const auto low = static_cast<std::int32_t>(c.d);
                check(static_cast<std::uint32_t>(context.quantum) == static_cast<std::uint32_t>(low / -7), "32-bit idiv");
            }
        }
    }
}

void bit_test() {
    // x = max_steps, n = allowance & 63. done = x with bit n set (bts),
    // quantum = with it reset (btr), next_block = the old bit (bt, setb) |
    // 2 * (x < 0 signed, through cmovl); max_steps = x >> n (sar), allowance = x
    // rotated left by n (rol), polls = x with bit 63 set and bit 0 reset.
    Assembler a;
    a.mov(S64, rax, ctx(kMaxSteps));
    a.mov(S64, rcx, ctx(kAllowance));
    a.mov(S64, rdx, rax);
    a.bit(Bit::bts, S64, rdx, rcx);
    a.mov(S64, ctx(kDone), rdx);
    a.mov(S64, rdx, rax);
    a.bit(Bit::btr, S64, rdx, rcx);
    a.mov(S64, ctx(kQuantum), rdx);
    a.alu(Alu::xor_, S32, rsi, rsi);
    a.bit(Bit::bt, S64, rax, rcx);
    a.setcc(Cond::b, rsi);
    a.mov(S32, rdx, Imm32{2});
    a.mov(S32, r8, Imm32{0});
    a.test(S64, rax, rax);
    a.cmov(Cond::l, S32, r8, rdx);
    a.alu(Alu::or_, S32, rsi, r8);
    a.mov(S32, ctx(kNext), rsi);
    a.mov(S64, rdx, rax);
    a.shift_cl(Shift::sar, S64, rdx);
    a.mov(S64, ctx(kMaxSteps), rdx);
    a.mov(S64, rdx, rax);
    a.shift_cl(Shift::rol, S64, rdx);
    a.mov(S64, ctx(kAllowance), rdx);
    a.mov(S64, rdx, rax);
    a.bit(Bit::bts, S64, rdx, Imm8{63});
    a.bit(Bit::btr, S64, rdx, Imm8{0});
    a.mov(S64, Mem::at(rdi, 40), rdx);
    a.ret();
    const auto program = load(a.finish(), "the bit function");
    if (!program) return;
    for (const std::uint64_t x : {0ULL, 1ULL, 0x8000000000000000ULL, 0xDEADBEEFCAFEF00DULL, ~0ULL}) {
        for (const unsigned n : {0U, 1U, 31U, 32U, 63U}) {
            jit::Context context;
            context.max_steps = x;
            context.allowance = n;
            program->run(context);
            const auto label = "bits of " + std::to_string(x) + " at " + std::to_string(n);
            check(context.done == (x | (1ULL << n)), label + ": bts");
            check(context.quantum == (x & ~(1ULL << n)), label + ": btr");
            check(reinterpret_cast<std::uintptr_t>(context.polls) == ((x | (1ULL << 63)) & ~1ULL), label + ": bts and btr with immediates");
            check(context.next_block == (((x >> n) & 1U) | (static_cast<std::int64_t>(x) < 0 ? 2U : 0U)), label + ": bt, setb and cmovl");
            check(static_cast<std::int64_t>(context.max_steps) == (static_cast<std::int64_t>(x) >> n), label + ": sar");
            check(context.allowance == ((x << n) | (n == 0 ? 0 : x >> (64 - n))), label + ": rol");
        }
    }
}

void register_test() {
    // Byte, word and double-word views of every register but rsp: each holds a
    // distinct value; movzx and mov read it back through its own name (never
    // ah..bh). done = the zero-extended low byte, quantum = the low word,
    // outcome = the low dword (written through a 32-bit mov).
    for (unsigned r = 0; r < 16; ++r) {
        if (r == 4 || r == 11) continue;  // rsp is the stack; r11 carries the context
        const Gpr reg{static_cast<std::uint8_t>(r)};
        const Gpr tmp = r == 10 ? r9 : r10;  // a scratch register that is not the one under test
        Assembler a;
        for (const Gpr saved : {rbx, rbp, r12, r13, r14, r15}) a.push(saved);
        a.mov(S64, r11, rdi);
        for (unsigned k = 0; k < 16; ++k) {
            if (k == 4 || k == 11) continue;
            a.mov_constant(Gpr{static_cast<std::uint8_t>(k)}, 0x1122334455667788ULL + 0x0101010101010101ULL * k * 0x11);
        }
        a.movzx(S32, tmp, S8, reg);
        a.mov(S64, Mem::at(r11, kDone), tmp);
        a.movzx(S64, tmp, S16, reg);
        a.mov(S64, Mem::at(r11, kQuantum), tmp);
        a.mov(S32, tmp, reg);
        a.mov(S32, Mem::at(r11, kOutcome), tmp);
        a.movsx(S64, tmp, S8, reg);
        a.mov(S64, Mem::at(r11, kMaxSteps), tmp);
        a.movsxd(tmp, reg);
        a.mov(S64, Mem::at(r11, kAllowance), tmp);
        for (const Gpr saved : {r15, r14, r13, r12, rbp, rbx}) a.pop(saved);
        a.ret();
        const auto program = load(a.finish(), "the register function");
        if (!program) return;
        jit::Context context;
        program->run(context);
        const std::uint64_t value = 0x1122334455667788ULL + 0x0101010101010101ULL * r * 0x11;
        const auto label = "register " + std::to_string(r);
        check(context.done == (value & 0xFF), label + ": the low byte (no ah..bh)");
        check(context.quantum == (value & 0xFFFF), label + ": the low word");
        check(context.outcome == static_cast<std::uint32_t>(value), label + ": the low dword");
        check(static_cast<std::int64_t>(context.max_steps) == static_cast<std::int8_t>(value & 0xFF), label + ": movsx of the low byte");
        check(static_cast<std::int64_t>(context.allowance) == static_cast<std::int32_t>(value), label + ": movsxd");
    }
}

void addressing_test() {
    // lea of every base, index and scale combination with the registers set
    // to known values: the CPU decodes the ModRM, SIB and displacement bytes,
    // and the address must be base + index * scale + disp.
    struct Combo {
        int base, index;
        unsigned scale;
        std::int32_t disp;
    };
    std::vector<Combo> combos;
    const std::int32_t disps[] = {0, 8, -8, 127, -128, 128, -129, 0x12345678, -0x12345678};
    unsigned k = 0;
    for (int base = -1; base < 16; ++base) {
        for (int index = -1; index < 16; ++index) {
            if (index == 4) continue;
            if (base < 0 && index < 0) continue;
            const unsigned scale = 1U << (k % 4);
            combos.push_back({base, index, scale, disps[(k * 7) % 9]});
            if (base == 4 || base == 5 || base == 12 || base == 13) combos.push_back({base, index, scale, 0});
            ++k;
        }
    }
    const auto n = combos.size();
    constexpr std::uint64_t kBase = 0x100000000ULL;
    auto value_of = [&](unsigned r) { return kBase * (r + 1) + 0x1234567 * (r + 3); };
    Assembler a;
    for (const Gpr saved : {rbx, rbp, r12, r13, r14, r15}) a.push(saved);
    a.push(rdi);                                                    // the context
    a.alu(Alu::sub, S64, rsp, Imm32{static_cast<std::int32_t>(8 * (n + 1))});
    a.mov(S64, Mem::at(rsp, static_cast<std::int32_t>(8 * n)), rsp);  // the stack pointer the combos see
    for (std::size_t i = 0; i < n; ++i) {
        for (unsigned r = 0; r < 16; ++r) {
            if (r != 4) a.mov_constant(Gpr{static_cast<std::uint8_t>(r)}, value_of(r));
        }
        const auto& c = combos[i];
        Mem m;
        if (c.base >= 0 && c.index >= 0) m = Mem::at(Gpr{static_cast<std::uint8_t>(c.base)}, Gpr{static_cast<std::uint8_t>(c.index)}, c.scale, c.disp);
        else if (c.base >= 0) m = Mem::at(Gpr{static_cast<std::uint8_t>(c.base)}, c.disp);
        else m = Mem::absolute(Gpr{static_cast<std::uint8_t>(c.index)}, c.scale, c.disp);
        a.lea(S64, rax, m);
        a.mov(S64, Mem::at(rsp, static_cast<std::int32_t>(8 * i)), rax);
    }
    a.mov(S64, rdi, Mem::at(rsp, static_cast<std::int32_t>(8 * (n + 1))));  // the context
    a.mov(S64, rax, Mem::at(rsp, static_cast<std::int32_t>(8 * n)));
    a.mov(S64, Mem::at(rdi, kDone), rax);
    // The addresses go to a buffer the context points to.
    a.mov(S64, rdx, Mem::at(rdi, 0));
    for (std::size_t i = 0; i < n; ++i) {
        a.mov(S64, rax, Mem::at(rsp, static_cast<std::int32_t>(8 * i)));
        a.mov(S64, Mem::at(rdx, static_cast<std::int32_t>(8 * i)), rax);
    }
    a.alu(Alu::add, S64, rsp, Imm32{static_cast<std::int32_t>(8 * (n + 1))});
    a.pop(rdi);
    for (const Gpr saved : {r15, r14, r13, r12, rbp, rbx}) a.pop(saved);
    a.ret();
    const auto program = load(a.finish(), "the addressing function");
    if (!program) return;
    std::vector<std::uint64_t> out(n);
    jit::Context context;
    context.registers = out.data();
    program->run(context);
    const auto stack = context.done;
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& c = combos[i];
        const auto base = c.base < 0 ? 0 : c.base == 4 ? stack : value_of(static_cast<unsigned>(c.base));
        const auto index = c.index < 0 ? 0 : value_of(static_cast<unsigned>(c.index)) * c.scale;
        const std::uint64_t want = base + index + static_cast<std::uint64_t>(static_cast<std::int64_t>(c.disp));
        if (out[i] != want) {
            ++wrong;
            check(false, "lea combination base " + std::to_string(c.base) + " index " + std::to_string(c.index) + " scale " +
                             std::to_string(c.scale) + " disp " + std::to_string(c.disp));
        }
    }
    check(wrong == 0, "every lea combination (" + std::to_string(n) + ")");
}

void long_jump_test() {
    // A loop whose body is too long for rel8: the back jump is rel32 and the
    // forward exit is too.
    Assembler a;
    const Label top = a.label(), exit = a.label();
    a.mov(S64, rcx, ctx(kMaxSteps));
    a.alu(Alu::xor_, S32, rax, rax);
    a.align(16);
    a.bind(top);
    a.test(S64, rcx, rcx);
    a.jcc(Cond::e, exit);
    a.alu(Alu::add, S64, rax, Imm32{3});
    a.nop(300);
    a.alu(Alu::sub, S64, rcx, Imm32{1});
    a.jmp(top);
    a.bind(exit);
    a.mov(S64, ctx(kDone), rax);
    a.ret();
    const auto program = load(a.finish(), "the long loop");
    if (!program) return;
    for (const std::uint64_t n : {0ULL, 1ULL, 2ULL, 100ULL}) {
        jit::Context context;
        context.max_steps = n;
        program->run(context);
        check(context.done == 3 * n, "the long loop of " + std::to_string(n));
    }
}

void execution_tests() {
    sum_loop_test();
    helper_call_test();
    float_test();
    integer_test();
    bit_test();
    register_test();
    addressing_test();
    long_jump_test();
}

#endif

} // namespace

int main(int argc, char** argv) {
    const bool print_counts = argc > 1 && std::string(argv[1]) == "--counts";
    table_tests(print_counts);
    edge_tests();
    cascade_tests();
    align_tests();
    rip_tests();
    random_program_tests();
    misuse_tests();
#ifdef NEURA_X64_EXECUTION
    execution_tests();
#else
    std::printf("execution tests skipped: not x86-64 Linux\n");
#endif
    std::printf("x64 tests: %zu checks, %zu failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

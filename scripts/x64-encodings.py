#!/usr/bin/env python3
"""Regenerate tests/jit/x64-encodings.inc: the bytes an assembler produces for
each instruction form of the tier 3 encoder (src/jit/x64.{hpp,cpp}), together
with the encoder call that must produce them.

    scripts/x64-encodings.py [--clang clang] [--check]
    scripts/x64-encodings.py --cross-check [--as as] [--samples 500] [--seed 1]

The cases are generated systematically: every instruction form, over every
register (all 16, in every operand size, including spl/bpl/sil/dil and the
r12/r13/rsp/rbp special cases of the ModRM and SIB bytes) and over a set of
memory operands that covers each base with no, 8-bit and 32-bit and negative
displacements, each scale with every index (r12 and r13 among them), operands
with no base, and RIP-relative operands. Each case is assembled with
`clang --target=x86_64-linux-gnu -c` (Intel syntax; one section per case, all
in one run) and the bytes of its section are read from the object file.
tests/jit/x64_tests.cpp runs the encoder call of each case and compares the
bytes, so the test needs no assembler.

--check fails when the committed table is out of date. --cross-check
assembles a seeded random sample of the cases with GNU as
(`as -msyntax=intel -mnaked-reg`) and fails where its bytes differ from
clang's, which catches case texts the two assemblers read differently.
"""

import argparse
import os
import random
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tests" / "jit" / "x64-encodings.inc"

GPR64 = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
         "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
GPR32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"] + [f"r{i}d" for i in range(8, 16)]
GPR16 = ["ax", "cx", "dx", "bx", "sp", "bp", "si", "di"] + [f"r{i}w" for i in range(8, 16)]
GPR8 = ["al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"] + [f"r{i}b" for i in range(8, 16)]
NAMES = {64: GPR64, 32: GPR32, 16: GPR16, 8: GPR8}
PTR = {8: "byte", 16: "word", 32: "dword", 64: "qword"}
SIZES = [8, 16, 32, 64]
CONDS = ["o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g"]
ALU = ["add", "or", "adc", "and", "sub", "xor", "cmp"]
ALU_CPP = {"add": "add", "or": "or_", "adc": "adc", "and": "and_", "sub": "sub", "xor": "xor_", "cmp": "cmp"}


def reg(n, size=64):
    return NAMES[size][n]


# ---- memory operands ----

class Mem:
    """[base + index * scale + disp], [index * scale + disp], [disp], or
    [rip + label + addend]."""

    def __init__(self, base=None, index=None, scale=1, disp=0, rip=False):
        self.base, self.index, self.scale, self.disp, self.rip = base, index, scale, disp, rip

    @staticmethod
    def _signed(value):
        return f" + {value}" if value >= 0 else f" - {-value}"

    def text(self):
        if self.rip:
            return "[rip + .L0" + (self._signed(self.disp) if self.disp else "") + "]"
        parts = []
        if self.base is not None:
            parts.append(GPR64[self.base])
        if self.index is not None:
            parts.append(f"{GPR64[self.index]}*{self.scale}")
        body = " + ".join(parts)
        if self.base is None and self.index is None:
            return f"[{self.disp}]"
        if self.disp or self.base is None:
            body += self._signed(self.disp)
        return f"[{body}]"

    def cpp(self):
        if self.rip:
            return f"Mem::rip_relative(l, {self.disp})" if self.disp else "Mem::rip_relative(l)"
        if self.base is None and self.index is None:
            return f"Mem::absolute({self.disp})"
        if self.base is None:
            return f"Mem::absolute({GPR64[self.index]}, {self.scale}, {self.disp})"
        if self.index is None:
            return f"Mem::at({GPR64[self.base]}, {self.disp})" if self.disp else f"Mem::at({GPR64[self.base]})"
        return f"Mem::at({GPR64[self.base]}, {GPR64[self.index]}, {self.scale}, {self.disp})"


RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI = range(8)
R8, R9, R10, R11, R12, R13, R14, R15 = range(8, 16)

DISPS = [0, 8, -8, 127, -128, 128, -129, 0x12345678, -0x12345678]
INDEXES = [i for i in range(16) if i != RSP]
SCALES = [1, 2, 4, 8]


def mems_full():
    """Every base with disp 0, 8-bit, 32-bit and negative displacements; every
    index with every scale; the special bases with the special indexes; no
    base; RIP-relative."""
    out = []
    for base in range(16):
        for disp in DISPS:
            out.append(Mem(base=base, disp=disp))
    for i, index in enumerate(INDEXES):
        for s, scale in enumerate(SCALES):
            out.append(Mem(base=(i * 4 + s * 3 + i // 3) % 16, index=index, scale=scale, disp=DISPS[(i + s) % len(DISPS)]))
    for base in (RSP, RBP, R12, R13):
        for index in (RAX, R12, R13, R8):
            for scale in SCALES:
                for disp in (0, 8, 0x1000):
                    out.append(Mem(base=base, index=index, scale=scale, disp=disp))
    for i, index in enumerate(INDEXES):
        for s, scale in enumerate(SCALES):
            out.append(Mem(index=index, scale=scale, disp=DISPS[(i * 3 + s) % len(DISPS)]))
    for disp in (0, 8, -8, 4096, 0x7FFFFFFF, -0x80000000):
        out.append(Mem(disp=disp))
    out.append(Mem(rip=True))
    out.append(Mem(rip=True, disp=4))
    out.append(Mem(rip=True, disp=-8))
    return out


def mems_small():
    return [
        Mem(base=RAX), Mem(base=RBP), Mem(base=R13), Mem(base=RSP), Mem(base=R12),
        Mem(base=RCX, disp=8), Mem(base=R12, disp=8), Mem(base=RBP, disp=-8), Mem(base=R13, disp=-8),
        Mem(base=R9, disp=127), Mem(base=RBX, disp=-128), Mem(base=RDX, disp=128), Mem(base=RSP, disp=-129),
        Mem(base=RDI, disp=0x12345678), Mem(base=R10, disp=-0x12345678), Mem(base=R14, disp=4096),
        Mem(base=RAX, index=RCX), Mem(base=RBX, index=RSI, scale=2, disp=8), Mem(base=RSP, index=RAX, scale=4, disp=0x100),
        Mem(base=RBP, index=R12, scale=8), Mem(base=R13, index=R13, scale=4, disp=8), Mem(base=R12, index=R8, scale=2, disp=-8),
        Mem(base=R15, index=R12, disp=128), Mem(base=R8, index=R13, scale=8, disp=0x12345678),
        Mem(disp=4096), Mem(disp=-8), Mem(index=RAX, scale=4, disp=8), Mem(index=R12, scale=8, disp=4096),
        Mem(index=R13, scale=2, disp=-8), Mem(rip=True),
    ]


def mems_tiny():
    return [
        Mem(base=RAX), Mem(base=RBP), Mem(base=R13), Mem(base=RSP), Mem(base=R12),
        Mem(base=RCX, disp=8), Mem(base=RBX, disp=-128), Mem(base=RDX, disp=128),
        Mem(base=RSI, index=RDI, scale=4), Mem(base=R12, index=R13, scale=8, disp=8),
        Mem(disp=4096), Mem(rip=True),
    ]


# ---- cases ----

class Case:
    def __init__(self, kind, text, cpp, cross=True):
        self.kind, self.text, self.cpp, self.cross = kind, text, cpp, cross


CASES = []


def add(kind, text, cpp, mem=None, cross=True):
    """One case. `text` and `cpp` hold the memory operand's `{m}`; a RIP-relative
    one gets a label and a constant after the instruction."""
    if mem is not None and mem.rip:
        text = text.replace("{m}", mem.text()) + "; .L0:; .quad 0x1122334455667788"
        cpp = ("Label l = a.label(); " + cpp.replace("{m}", mem.cpp()) +
               " a.bind(l); a.data64(0x1122334455667788ULL);")
    elif mem is not None:
        text = text.replace("{m}", mem.text())
        cpp = cpp.replace("{m}", mem.cpp())
    CASES.append(Case(kind, text, cpp, cross))


def pairs():
    """Register pairs: every register as the first and as the second operand,
    and the special ones (rsp, rbp, r12, r13) against each other."""
    out = [(i, (i * 3 + 1) % 16) for i in range(16)]
    out += [(RSP, RBP), (RBP, RSP), (R12, R13), (R13, R12), (RSP, R12), (R13, RBP), (R8, R15), (R15, R8)]
    return out


def with_mems(mems, count=None):
    """(mem, register number) pairs; the register rotates through all 16."""
    return [(m, (k * 7 + 3) % 16) for k, m in enumerate(mems)]


IMM32 = [0, 1, -1, 127, -128, 128, -129, 1000, 0x7FFFFFFF, -0x80000000, 0x12345678]
IMM16 = [0, 1, -1, 127, -128, 128, 255, 1000, 32767, -32768, 65535]
IMM8 = [0, 1, 127, -128, 128, 255, 85]


def imms(size):
    return {8: IMM8, 16: IMM16, 32: IMM32, 64: IMM32}[size]


def imm_for(size, k):
    values = imms(size)
    return values[k % len(values)]


S = {8: "S8", 16: "S16", 32: "S32", 64: "S64"}


def gen_mov():
    for size in SIZES:
        for d, s in pairs():
            add("mov", f"mov {reg(d, size)}, {reg(s, size)}", f"a.mov({S[size]}, {GPR64[d]}, {GPR64[s]});")
    for size in SIZES:
        mems = mems_full() if size in (64, 32) else mems_small()
        for m, r in with_mems(mems):
            add("mov", f"mov {reg(r, size)}, {PTR[size]} ptr {{m}}", f"a.mov({S[size]}, {GPR64[r]}, {{m}});", m)
        for m, r in with_mems(mems_full() if size == 32 else mems_small()):
            add("mov", f"mov {PTR[size]} ptr {{m}}, {reg(r, size)}", f"a.mov({S[size]}, {{m}}, {GPR64[r]});", m)
    for size in SIZES:
        for r in range(16):
            v = imm_for(size, r)
            add("mov", f"mov {reg(r, size)}, {v}", f"a.mov({S[size]}, {GPR64[r]}, Imm32{{{v}}});")
        for v in imms(size):
            add("mov", f"mov {reg(RAX, size)}, {v}", f"a.mov({S[size]}, rax, Imm32{{{v}}});")
        for k, m in enumerate(mems_small()):
            v = imm_for(size, k)
            add("mov", f"mov {PTR[size]} ptr {{m}}, {v}", f"a.mov({S[size]}, {{m}}, Imm32{{{v}}});", m)
    for r in range(16):
        for v in (0x123456789ABCDEF0, 0x8000000000000000, 0xFFFFFFFF00000000):
            add("movabs", f"movabs {GPR64[r]}, {v:#x}", f"a.mov({GPR64[r]}, Imm64{{static_cast<std::int64_t>(UINT64_C({v:#x}))}});")
    values = (0, 1, 0xFFFFFFFF, 0x80000000, 0x100000000, 0xFFFFFFFF80000000, 0xFFFFFFFFFFFFFFFF,
              0x7FFFFFFFFFFFFFFF, 0xFFFFFFFF7FFFFFFF, 0x123456789)
    for r in range(16):
        for k in range(3):
            v = values[(r + 3 * k) % len(values)]
            if v <= 0xFFFFFFFF:
                text = f"mov {GPR32[r]}, {v}"
            elif v >= 0xFFFFFFFF80000000:
                text = f"mov {GPR64[r]}, {v - 2**64}"
            else:
                text = f"movabs {GPR64[r]}, {v:#x}"
            add("mov_constant", text, f"a.mov_constant({GPR64[r]}, UINT64_C({v:#x}));")


def gen_lea():
    for size in (16, 32, 64):
        for m, r in with_mems(mems_full() if size == 64 else mems_small()):
            add("lea", f"lea {reg(r, size)}, {{m}}", f"a.lea({S[size]}, {GPR64[r]}, {{m}});", m)


def gen_alu():
    for op in ALU:
        for size in SIZES:
            for d, s in pairs():
                add(op, f"{op} {reg(d, size)}, {reg(s, size)}", f"a.alu(Alu::{ALU_CPP[op]}, {S[size]}, {GPR64[d]}, {GPR64[s]});")
            mems = mems_full() if (op == "add" and size == 64) else mems_small()
            for m, r in with_mems(mems):
                add(op, f"{op} {reg(r, size)}, {PTR[size]} ptr {{m}}",
                    f"a.alu(Alu::{ALU_CPP[op]}, {S[size]}, {GPR64[r]}, {{m}});", m)
            for m, r in with_mems(mems_small()):
                add(op, f"{op} {PTR[size]} ptr {{m}}, {reg(r, size)}",
                    f"a.alu(Alu::{ALU_CPP[op]}, {S[size]}, {{m}}, {GPR64[r]});", m)
            for r in range(16):
                v = imm_for(size, r)
                add(op, f"{op} {reg(r, size)}, {v}", f"a.alu(Alu::{ALU_CPP[op]}, {S[size]}, {GPR64[r]}, Imm32{{{v}}});")
            for v in imms(size):
                add(op, f"{op} {reg(RAX, size)}, {v}", f"a.alu(Alu::{ALU_CPP[op]}, {S[size]}, rax, Imm32{{{v}}});")
            for k, m in enumerate(mems_small()):
                v = imm_for(size, k)
                add(op, f"{op} {PTR[size]} ptr {{m}}, {v}",
                    f"a.alu(Alu::{ALU_CPP[op]}, {S[size]}, {{m}}, Imm32{{{v}}});", m)


def gen_test():
    for size in SIZES:
        for d, s in pairs():
            add("test", f"test {reg(d, size)}, {reg(s, size)}", f"a.test({S[size]}, {GPR64[d]}, {GPR64[s]});")
        for m, r in with_mems(mems_small()):
            add("test", f"test {PTR[size]} ptr {{m}}, {reg(r, size)}", f"a.test({S[size]}, {{m}}, {GPR64[r]});", m)
        for r in range(16):
            v = imm_for(size, r)
            add("test", f"test {reg(r, size)}, {v}", f"a.test({S[size]}, {GPR64[r]}, Imm32{{{v}}});")
        for v in imms(size):
            add("test", f"test {reg(RAX, size)}, {v}", f"a.test({S[size]}, rax, Imm32{{{v}}});")
        for k, m in enumerate(mems_small()):
            v = imm_for(size, k)
            add("test", f"test {PTR[size]} ptr {{m}}, {v}", f"a.test({S[size]}, {{m}}, Imm32{{{v}}});", m)


def gen_imul():
    for size in (16, 32, 64):
        for d, s in pairs():
            add("imul", f"imul {reg(d, size)}, {reg(s, size)}", f"a.imul({S[size]}, {GPR64[d]}, {GPR64[s]});")
        for m, r in with_mems(mems_small()):
            add("imul", f"imul {reg(r, size)}, {PTR[size]} ptr {{m}}", f"a.imul({S[size]}, {GPR64[r]}, {{m}});", m)
        values = [1, -1, 5, 127, -128, 128, -129, 1000, 32767, -32768]
        if size != 16:
            values += [0x7FFFFFFF, -0x80000000, 0x12345678]
        for d, s in pairs():
            v = values[(d + s) % len(values)]
            add("imul", f"imul {reg(d, size)}, {reg(s, size)}, {v}", f"a.imul({S[size]}, {GPR64[d]}, {GPR64[s]}, Imm32{{{v}}});")
        for k, (m, r) in enumerate(with_mems(mems_small())):
            v = values[k % len(values)]
            add("imul", f"imul {reg(r, size)}, {PTR[size]} ptr {{m}}, {v}",
                f"a.imul({S[size]}, {GPR64[r]}, {{m}}, Imm32{{{v}}});", m)


def gen_unary():
    for op, fn in (("neg", "neg"), ("not", "not_"), ("mul", "mul"), ("div", "div"), ("idiv", "idiv")):
        for size in SIZES:
            for r in range(16):
                add(op, f"{op} {reg(r, size)}", f"a.{fn}({S[size]}, {GPR64[r]});")
            for m in mems_tiny():
                add(op, f"{op} {PTR[size]} ptr {{m}}", f"a.{fn}({S[size]}, {{m}});", m)
    add("cdq", "cdq", "a.cdq();")
    add("cqo", "cqo", "a.cqo();")


def gen_shift():
    counts = [1, 2, 3, 7, 8, 15, 31, 63, 0, 255]
    for op in ("rol", "ror", "shl", "shr", "sar"):
        for size in SIZES:
            for r in range(16):
                c = counts[(r + size) % len(counts)]
                add(op, f"{op} {reg(r, size)}, {c}", f"a.shift(Shift::{op}, {S[size]}, {GPR64[r]}, Imm8{{{c}}});")
                add(op, f"{op} {reg(r, size)}, cl", f"a.shift_cl(Shift::{op}, {S[size]}, {GPR64[r]});")
            for k, m in enumerate(mems_tiny()):
                c = counts[k % len(counts)]
                add(op, f"{op} {PTR[size]} ptr {{m}}, {c}", f"a.shift(Shift::{op}, {S[size]}, {{m}}, Imm8{{{c}}});", m)
                add(op, f"{op} {PTR[size]} ptr {{m}}, cl", f"a.shift_cl(Shift::{op}, {S[size]}, {{m}});", m)


def gen_bit():
    for op in ("bt", "bts", "btr"):
        for size in (16, 32, 64):
            for d, s in pairs():
                add(op, f"{op} {reg(d, size)}, {reg(s, size)}", f"a.bit(Bit::{op}, {S[size]}, {GPR64[d]}, {GPR64[s]});")
            for m, r in with_mems(mems_tiny()):
                add(op, f"{op} {PTR[size]} ptr {{m}}, {reg(r, size)}", f"a.bit(Bit::{op}, {S[size]}, {{m}}, {GPR64[r]});", m)
            for r in range(16):
                i = (r * 5 + size) % size
                add(op, f"{op} {reg(r, size)}, {i}", f"a.bit(Bit::{op}, {S[size]}, {GPR64[r]}, Imm8{{{i}}});")
            for k, m in enumerate(mems_tiny()):
                i = (k * 5) % size
                add(op, f"{op} {PTR[size]} ptr {{m}}, {i}", f"a.bit(Bit::{op}, {S[size]}, {{m}}, Imm8{{{i}}});", m)


def gen_cc():
    for c, cc in enumerate(CONDS):
        for r in range(16):
            add("setcc", f"set{cc} {GPR8[r]}", f"a.setcc(Cond::{cc}, {GPR64[r]});")
        for k, m in enumerate(mems_tiny()):
            add("setcc", f"set{cc} byte ptr {{m}}", f"a.setcc(Cond::{cc}, {{m}});", m)
        for size in (16, 32, 64):
            for k, (d, s) in enumerate(pairs()):
                if (k + c) % 4 == 0:
                    add("cmov", f"cmov{cc} {reg(d, size)}, {reg(s, size)}", f"a.cmov(Cond::{cc}, {S[size]}, {GPR64[d]}, {GPR64[s]});")
            for k, m in enumerate(mems_tiny()):
                if (k + c) % 3 == 0:
                    r = (k * 5 + c) % 16
                    add("cmov", f"cmov{cc} {reg(r, size)}, {PTR[size]} ptr {{m}}", f"a.cmov(Cond::{cc}, {S[size]}, {GPR64[r]}, {{m}});", m)


def gen_extend():
    forms = [(16, 8), (32, 8), (64, 8), (32, 16), (64, 16)]
    for mnemonic in ("movzx", "movsx"):
        for to_size, from_size in forms:
            for d, s in pairs():
                add(mnemonic, f"{mnemonic} {reg(d, to_size)}, {reg(s, from_size)}",
                    f"a.{mnemonic}({S[to_size]}, {GPR64[d]}, {S[from_size]}, {GPR64[s]});")
            for m, r in with_mems(mems_small()):
                add(mnemonic, f"{mnemonic} {reg(r, to_size)}, {PTR[from_size]} ptr {{m}}",
                    f"a.{mnemonic}({S[to_size]}, {GPR64[r]}, {S[from_size]}, {{m}});", m)
    for d, s in pairs():
        add("movsxd", f"movsxd {GPR64[d]}, {GPR32[s]}", f"a.movsxd({GPR64[d]}, {GPR64[s]});")
    for m, r in with_mems(mems_small()):
        add("movsxd", f"movsxd {GPR64[r]}, dword ptr {{m}}", f"a.movsxd({GPR64[r]}, {{m}});", m)


def gen_flow():
    for r in range(16):
        add("push", f"push {GPR64[r]}", f"a.push({GPR64[r]});")
        add("pop", f"pop {GPR64[r]}", f"a.pop({GPR64[r]});")
        add("call", f"call {GPR64[r]}", f"a.call({GPR64[r]});")
        add("jmp", f"jmp {GPR64[r]}", f"a.jmp({GPR64[r]});")
    for m in mems_small():
        add("call", f"call qword ptr {{m}}", "a.call({m});", m)
        add("jmp", f"jmp qword ptr {{m}}", "a.jmp({m});", m)
    add("ret", "ret", "a.ret();")
    add("ud2", "ud2", "a.ud2();")
    for n in range(1, 10):
        add("nop", f".nops {n}", f"a.nop({n});", cross=False)


def gen_labels():
    # Forward and backward jumps at the edges of rel8: the displacement is
    # the number of fill bytes (forward) or the fill and the jump (backward).
    def fill(n):
        return f".fill {n}, 1, 0x90", f"for (int i = 0; i < {n}; ++i) a.data8(0x90);"

    for target in ["jmp"] + [f"j{cc}" for cc in CONDS]:
        def call(label):
            return f"a.jmp({label});" if target == "jmp" else f"a.jcc(Cond::{target[1:]}, {label});"
        distances = [0, 1, 127, 128, 129, 1000] if target in ("jmp", "je", "jne", "jg", "jo", "jb") else [0, 127, 128]
        for n in distances:
            ft, fc = fill(n)
            add("jump", f"{target} .L0; {ft}; .L0:", f"Label l = a.label(); {call('l')} {fc} a.bind(l);")
        for n in (0, 1, 100, 126, 127, 128, 1000) if target in ("jmp", "je", "jne", "jg", "jo", "jb") else (0, 126, 127):
            ft, fc = fill(n)
            add("jump", f".L0:; {ft}; {target} .L0", f"Label l = a.label(); a.bind(l); {fc} {call('l')}")
    for n in (0, 1, 1000):
        ft, fc = fill(n)
        add("call", f"call .L0; {ft}; .L0:", f"Label l = a.label(); a.call(l); {fc} a.bind(l);")
        add("call", f".L0:; {ft}; call .L0", f"Label l = a.label(); a.bind(l); {fc} a.call(l);")
    # A chain whose widening cascades: each jump that grows pushes the next
    # one over the edge.
    for count in (2, 3, 6, 9):
        texts, calls = [], ["Label l[%d]; for (auto& x : l) x = a.label();" % count]
        for i in range(count):
            texts.append(f"jmp .L{i}")
            calls.append(f"a.jmp(l[{i}]);")
        previous = 0
        for i in range(count):
            gap = (128 - 5 * (count - 1 - i)) - previous
            texts.append(f".fill {gap}, 1, 0x90")
            texts.append(f".L{i}:")
            calls.append(f"for (int i = 0; i < {gap}; ++i) a.data8(0x90);")
            calls.append(f"a.bind(l[{i}]);")
            previous = 128 - 5 * (count - 1 - i)
        add("jump", "; ".join(texts), " ".join(calls))
    # RIP-relative constants after the code, behind and ahead.
    add("rip", ".L0:; .quad 5; lea rax, [rip + .L0]", "Label l = a.label(); a.bind(l); a.data64(5); a.lea(S64, rax, Mem::rip_relative(l));")
    add("rip", "mov rax, qword ptr [rip + .L0 + 8]; ret; .L0:; .quad 5; .quad 6",
        "Label l = a.label(); a.mov(S64, rax, Mem::rip_relative(l, 8)); a.ret(); a.bind(l); a.data64(5); a.data64(6);")
    add("rip", "add dword ptr [rip + .L0], 1000; .L0:; .long 7",
        "Label l = a.label(); a.alu(Alu::add, S32, Mem::rip_relative(l), Imm32{1000}); a.bind(l); a.data32(7);")
    add("rip", "movsd xmm1, qword ptr [rip + .L0]; ret; .L0:; .quad 0x3ff0000000000000",
        "Label l = a.label(); a.movs(Prec::dbl, xmm1, Mem::rip_relative(l)); a.ret(); a.bind(l); a.data64(0x3ff0000000000000ULL);")


def gen_sse():
    xpairs = [(i, (i * 3 + 1) % 16) for i in range(16)] + [(0, 1), (8, 15), (15, 8), (7, 8), (8, 7)]
    gpairs = pairs()
    for mn, prec in (("movq", None),):
        for x, g in gpairs:
            add("movq", f"movq xmm{x}, {GPR64[g]}", f"a.movq(xmm{x}, {GPR64[g]});")
            add("movq", f"movq {GPR64[g]}, xmm{x}", f"a.movq({GPR64[g]}, xmm{x});")
            add("movd", f"movd xmm{x}, {GPR32[g]}", f"a.movd(xmm{x}, {GPR64[g]});")
            add("movd", f"movd {GPR32[g]}, xmm{x}", f"a.movd({GPR64[g]}, xmm{x});")
    for suffix, prec, ptr in (("sd", "dbl", "qword"), ("ss", "single", "dword")):
        for x, y in xpairs:
            add("mov" + suffix, f"mov{suffix} xmm{x}, xmm{y}", f"a.movs(Prec::{prec}, xmm{x}, xmm{y});")
        for m, r in with_mems(mems_small()):
            x = r
            add("mov" + suffix, f"mov{suffix} xmm{x}, {ptr} ptr {{m}}", f"a.movs(Prec::{prec}, xmm{x}, {{m}});", m)
            add("mov" + suffix, f"mov{suffix} {ptr} ptr {{m}}, xmm{x}", f"a.movs(Prec::{prec}, {{m}}, xmm{x});", m)
        for op in ("add", "sub", "mul", "div", "sqrt"):
            for x, y in xpairs:
                add(f"{op}{suffix}", f"{op}{suffix} xmm{x}, xmm{y}", f"a.arith(Fp::{op}, Prec::{prec}, xmm{x}, xmm{y});")
            for m, r in with_mems(mems_tiny()):
                add(f"{op}{suffix}", f"{op}{suffix} xmm{r}, {ptr} ptr {{m}}", f"a.arith(Fp::{op}, Prec::{prec}, xmm{r}, {{m}});", m)
        for x, y in xpairs:
            add("ucomi" + suffix, f"ucomi{suffix} xmm{x}, xmm{y}", f"a.ucomis(Prec::{prec}, xmm{x}, xmm{y});")
        for m, r in with_mems(mems_tiny()):
            add("ucomi" + suffix, f"ucomi{suffix} xmm{r}, {ptr} ptr {{m}}", f"a.ucomis(Prec::{prec}, xmm{r}, {{m}});", m)
        for size in (32, 64):
            for x, g in gpairs:
                add("cvtsi2" + suffix, f"cvtsi2{suffix} xmm{x}, {reg(g, size)}", f"a.cvtsi2s(Prec::{prec}, xmm{x}, {S[size]}, {GPR64[g]});")
                add("cvtt" + suffix + "2si", f"cvtt{suffix}2si {reg(g, size)}, xmm{x}", f"a.cvtts2si(Prec::{prec}, {S[size]}, {GPR64[g]}, xmm{x});")
            for m, r in with_mems(mems_tiny()):
                add("cvtsi2" + suffix, f"cvtsi2{suffix} xmm{r}, {PTR[size]} ptr {{m}}", f"a.cvtsi2s(Prec::{prec}, xmm{r}, {S[size]}, {{m}});", m)
                add("cvtt" + suffix + "2si", f"cvtt{suffix}2si {reg(r, size)}, {ptr} ptr {{m}}", f"a.cvtts2si(Prec::{prec}, {S[size]}, {GPR64[r]}, {{m}});", m)
    for mn, fn in (("andpd", "andpd"), ("andnpd", "andnpd"), ("xorpd", "xorpd")):
        for x, y in xpairs:
            add(mn, f"{mn} xmm{x}, xmm{y}", f"a.{fn}(xmm{x}, xmm{y});")
        for m, r in with_mems(mems_tiny()):
            add(mn, f"{mn} xmm{r}, xmmword ptr {{m}}", f"a.{fn}(xmm{r}, {{m}});", m)
    for mn, fn, ptr in (("cvtss2sd", "cvtss2sd", "dword"), ("cvtsd2ss", "cvtsd2ss", "qword")):
        for x, y in xpairs:
            add(mn, f"{mn} xmm{x}, xmm{y}", f"a.{fn}(xmm{x}, xmm{y});")
        for m, r in with_mems(mems_tiny()):
            add(mn, f"{mn} xmm{r}, {ptr} ptr {{m}}", f"a.{fn}(xmm{r}, {{m}});", m)


def build_cases():
    CASES.clear()
    gen_mov()
    gen_lea()
    gen_alu()
    gen_test()
    gen_imul()
    gen_unary()
    gen_shift()
    gen_bit()
    gen_cc()
    gen_extend()
    gen_flow()
    gen_labels()
    gen_sse()
    seen = set()
    unique = []
    for case in CASES:
        if case.text in seen:
            continue
        seen.add(case.text)
        unique.append(case)
    return unique


# ---- assembling ----

def sections(path):
    """The bytes of every section of a little-endian ELF64 object, by name."""
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise RuntimeError(f"{path}: not an ELF64 object")
    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    headers = []
    for i in range(shnum):
        base = shoff + i * shentsize
        name, kind, _, _, offset, size = struct.unpack_from("<IIQQQQ", data, base)
        headers.append((name, kind, offset, size))
    names_offset = headers[shstrndx][2]
    out = {}
    for name, kind, offset, size in headers:
        end = data.index(b"\0", names_offset + name)
        out[data[names_offset + name:end].decode()] = data[offset:offset + size] if kind != 8 else b""
    return out


def source_for(index, case):
    """Assembler source of one case, in a section of its own; local labels are
    made unique per case."""
    lines = [line.strip() for line in case.text.split(";")]
    body = "\n".join(re.sub(r"\.L(\d+)", rf".L{index}_\1", line) for line in lines if line)
    return f'.section .t{index},"ax",@progbits\n{body}\n'


def assemble(clang, cases, prefix):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "cases.s"
        source.write_text(prefix + "".join(source_for(i, c) for i, c in enumerate(cases)))
        done = subprocess.run(clang(source, Path(directory) / "cases.o"), capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"the cases do not assemble:\n{done.stderr[:4000]}")
        found = sections(Path(directory) / "cases.o")
    return [found[f".t{i}"] for i in range(len(cases))]


def clang_bytes(clang, cases):
    return assemble(lambda s, o: [clang, "--target=x86_64-linux-gnu", "-c", str(s), "-o", str(o)],
                    cases, ".intel_syntax noprefix\n")


def gnu_bytes(gas, cases):
    out = []
    # GNU as stops at the first error, so assemble each case on its own.
    with tempfile.TemporaryDirectory() as directory:
        for i, case in enumerate(cases):
            source = Path(directory) / "case.s"
            source.write_text(source_for(0, case))
            done = subprocess.run([gas, "-msyntax=intel", "-mnaked-reg", str(source), "-o", str(Path(directory) / "case.o")],
                                  capture_output=True, text=True)
            if done.returncode != 0:
                raise RuntimeError(f"GNU as rejects '{case.text}':\n{done.stderr}")
            out.append(sections(Path(directory) / "case.o")[".t0"])
    return out


def render(clang):
    cases = build_cases()
    results = clang_bytes(clang, cases)
    rows = []
    for case, code in zip(cases, results):
        escaped = "".join(f"\\x{b:02x}" for b in code)
        text = case.text.replace("\\", "\\\\").replace('"', '\\"')
        rows.append(f'    {{"{case.kind}", "{text}", [](Assembler& a) {{ {case.cpp} }}, "{escaped}", {len(code)}}},')
    header = ("// Generated by scripts/x64-encodings.py: the bytes of each case as clang's\n"
              "// assembler encodes it, and the encoder calls that must produce them. Do not\n"
              "// edit; regenerate.\n")
    return header + "\n".join(rows) + "\n", cases


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--as", dest="gas", default="as")
    parser.add_argument("--check", action="store_true", help="fail when the committed table differs")
    parser.add_argument("--cross-check", action="store_true", help="compare a random sample with GNU as")
    parser.add_argument("--samples", type=int, default=500)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    table, cases = render(args.clang)
    if args.cross_check:
        rng = random.Random(args.seed)
        chosen = rng.sample([c for c in cases if c.cross], args.samples)
        clang_result = dict(zip((c.text for c in cases), clang_bytes(args.clang, cases)))
        gnu = gnu_bytes(args.gas, chosen)
        bad = [(c, g) for c, g in zip(chosen, gnu) if g != clang_result[c.text]]
        for case, g in bad:
            print(f"differs: {case.text}\n  clang {clang_result[case.text].hex(' ')}\n  gnu   {g.hex(' ')}", file=sys.stderr)
        print(f"cross-check: {len(chosen)} cases, {len(bad)} differ")
        return 1 if bad else 0
    if args.check:
        if TABLE.read_text() != table:
            print(f"{TABLE.relative_to(ROOT)} is out of date: run scripts/x64-encodings.py", file=sys.stderr)
            return 1
        return 0
    TABLE.parent.mkdir(parents=True, exist_ok=True)
    TABLE.write_text(table)
    counts = {}
    for case in cases:
        counts[case.kind] = counts.get(case.kind, 0) + 1
    print(f"wrote {TABLE.relative_to(ROOT)}: {len(cases)} cases")
    for kind in sorted(counts):
        print(f"  {kind}: {counts[kind]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Regenerate tests/jit/encodings.inc: the bytes an assembler produces for
each instruction the native code generator's encoders (src/jit/x86_64.cpp,
src/jit/aarch64.cpp) can emit.

    scripts/jit-encodings.py [--clang clang] [--check]

Each case is assembled on its own with `clang --target=<triple> -c`, which
needs no linker or sysroot, and the bytes of its .text section are read from
the object file. tests/jit/jit_tests.cpp emits the same instructions with the
encoders and compares them with this table, so the test needs no assembler.
The case text is also the key the test looks its emitter call up by: change
both together. --check fails when the committed table is out of date.
"""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tests" / "jit" / "encodings.inc"

# Intel syntax. Branch targets are relative to the start of the case.
X86_64 = [
    "mov rax, rcx",
    "mov r13, rdi",
    "mov rbx, r11",
    "mov rdi, r13",
    "mov qword ptr [r15 + 8], rax",
    "mov rax, qword ptr [r15 + 8]",
    "mov rax, qword ptr [r15]",
    "mov r10, qword ptr [r15 + 4096]",
    "mov rcx, qword ptr [r13 + 16]",
    "mov rdx, qword ptr [r12 + 8]",
    "mov rax, qword ptr [rax]",
    "mov r15, qword ptr [r13]",
    "mov qword ptr [r15 + 800], rbp",
    "mov qword ptr [r13 + 56], r12",
    "mov qword ptr [r15 + 16], rsi",
    "movzx eax, byte ptr [r13 + 72]",
    "movzx ecx, byte ptr [r13 + 72]",
    "mov byte ptr [r14 + 3], 1",
    "mov byte ptr [r14 + 300], 1",
    "mov byte ptr [r14], 1",
    "mov dword ptr [r13 + 64], 7",
    "mov dword ptr [r13 + 68], 305419896",
    "mov eax, 0",
    "mov r12d, 0",
    "mov esi, 17",
    "mov r11d, 4294967295",
    "mov rax, -1",
    "mov r11, -2147483648",
    "movabs r11, 0x8000000000000000",
    "movabs rax, 0x123456789abcdef0",
    "add rax, rcx",
    "sub rdx, rcx",
    "and rax, r11",
    "or rax, rcx",
    "xor rcx, r11",
    "cmp rax, rdx",
    "cmp rcx, r11",
    "add r12, 1",
    "add r12, 1000",
    "add rsp, -8",
    "add rsp, 8",
    "imul rax, rcx",
    "imul rdx, rcx",
    "mul rcx",
    "div rcx",
    "idiv rcx",
    "cqo",
    "shl rax, cl",
    "shr rax, cl",
    "sar rdx, cl",
    "shl rcx, 5",
    "shr rcx, 1",
    "sar rax, 63",
    "neg rax",
    "not rdx",
    "sete al",
    "setb cl",
    "setg dl",
    "setne r11b",
    "setl sil",
    "movzx eax, al",
    "movzx edx, dl",
    "movzx r11d, r11b",
    "movzx esi, sil",
    "bt rax, 0",
    "bt rax, 63",
    "test rax, rax",
    "test rcx, rcx",
    ".att_syntax; je .+4096",
    ".att_syntax; jo .+4096",
    ".att_syntax; jb .+4096",
    ".att_syntax; jae .+4096",
    ".att_syntax; jg .+4096",
    ".att_syntax; jmp .+4096",
    "call qword ptr [r13 + 80]",
    "call qword ptr [r13 + 104]",
    "push rbx",
    "push r15",
    "pop r12",
    "pop rbp",
    "ret",
    # The float operations (SSE2).
    "movq xmm0, rax",
    "movq xmm1, rcx",
    "movq xmm1, r11",
    "movd xmm0, eax",
    "movd xmm1, r11d",
    "movq rax, xmm0",
    "movq rdx, xmm1",
    "movq r11, xmm0",
    "movd eax, xmm0",
    "movd ecx, xmm0",
    "movd r11d, xmm1",
    "addsd xmm0, xmm1",
    "subsd xmm0, xmm1",
    "mulsd xmm0, xmm1",
    "divsd xmm0, xmm1",
    "addss xmm0, xmm1",
    "subss xmm0, xmm1",
    "mulss xmm0, xmm1",
    "divss xmm0, xmm1",
    "cmpeqsd xmm0, xmm1",
    "cmpneqsd xmm0, xmm1",
    "cmpltsd xmm0, xmm1",
    "cmplesd xmm0, xmm1",
    "cmpeqss xmm0, xmm1",
    "cmpneqss xmm0, xmm1",
    "cmpltss xmm0, xmm1",
    "cmpless xmm0, xmm1",
    "ucomisd xmm0, xmm0",
    "ucomiss xmm0, xmm0",
    "ucomisd xmm0, xmm1",
    "cvtsi2sd xmm0, rax",
    "cvtsi2ss xmm0, rcx",
    "cvtsi2sd xmm0, r11",
    "cvttsd2si rax, xmm0",
    "cvttss2si rdx, xmm0",
    "cvttsd2si r11, xmm0",
    "cvtss2sd xmm0, xmm0",
    "cvtsd2ss xmm0, xmm0",
    ".att_syntax; jnp .+4096",
    ".att_syntax; jp .+4096",
]

# Cases with several instructions separate them with ';'.
AARCH64 = [
    "mov x9, x23",
    "mov x21, x0",
    "mov x0, x21",
    "ldr x9, [x19, #8]",
    "ldr x9, [x19, #32760]",
    "ldr x19, [x21]",
    "str x23, [x19, #24]",
    "str x22, [x21, #56]",
    "ldr x9, [x19, x17, lsl #3]",
    "str x10, [x19, x17, lsl #3]",
    "ldrb w10, [x21, #72]",
    "strb w16, [x20, #4095]",
    "strb w16, [x20, x17]",
    "str w16, [x21, #68]",
    "movz x9, #0",
    "movz x9, #4660, lsl #16",
    "movk x9, #65535, lsl #48",
    "movn x12, #0",
    "movz x12, #0x8000, lsl #48",
    "movz x12, #0xdef0; movk x12, #0x9abc, lsl #16; movk x12, #0x5678, lsl #32; movk x12, #0x1234, lsl #48",
    "movn x12, #0xedcb; movk x12, #0, lsl #16",
    "movn x12, #0x8000, lsl #48",
    "movz x1, #7",
    "add x9, x9, x10",
    "adds x9, x9, x10",
    "sub x11, x11, x10",
    "subs x9, x9, x10",
    "and x9, x9, x12",
    "orr x9, x9, x10",
    "eor x9, x9, x12",
    "mul x9, x9, x10",
    "umulh x16, x9, x10",
    "smulh x16, x9, x10",
    "udiv x9, x9, x10",
    "sdiv x11, x9, x10",
    "msub x9, x11, x10, x9",
    "lslv x9, x9, x10",
    "lsrv x9, x9, x10",
    "asrv x11, x11, x10",
    "lsl x10, x10, #5",
    "lsl x10, x10, #63",
    "lsr x10, x10, #1",
    "asr x9, x9, #63",
    "neg x9, x9",
    "mvn x9, x9",
    "cmp x9, x10",
    "cmp x16, x9, asr #63",
    "tst x9, #0x1",
    "tst x9, #0x100",
    "tst x9, #0x8000000000000000",
    "cset x9, eq",
    "cset x9, lo",
    "cset x9, gt",
    "cset x9, ls",
    "add x29, sp, #0",
    "add x22, x22, #1",
    "add x22, x22, #4095",
    "b .+64",
    "b.eq .+64",
    "b.vs .+64",
    "b.hs .+64",
    "b.ne .+64",
    "cbz x9, .+64",
    "cbnz x16, .+64",
    "mov x9, x9; b .-4",
    "mov x9, x9; b.ne .-4",
    "mov x9, x9; cbnz x9, .-4",
    "blr x16",
    "ret",
    "stp x29, x30, [sp, #-96]!",
    "ldp x29, x30, [sp], #96",
    "stp x19, x20, [sp, #16]",
    "ldp x27, x28, [sp, #80]",
    # The float operations (scalar floating point).
    "fmov d0, x9",
    "fmov s0, w9",
    "fmov d1, x10",
    "fmov s1, w12",
    "fmov x9, d0",
    "fmov w9, s0",
    "fmov x11, d0",
    "fadd d0, d0, d1",
    "fsub d0, d0, d1",
    "fmul d0, d0, d1",
    "fdiv d0, d0, d1",
    "fadd s0, s0, s1",
    "fsub s0, s0, s1",
    "fmul s0, s0, s1",
    "fdiv s0, s0, s1",
    "fcmp d0, d1",
    "fcmp s0, s1",
    "fcmp d0, d0",
    "fcmp s0, s0",
    "fcvt d0, s0",
    "fcvt s0, d0",
    "scvtf d0, x9",
    "scvtf s0, x10",
    "fcvtzs x9, d0",
    "fcvtzs x11, s0",
    "csel x9, x16, x9, vs",
    "csel x10, x16, x10, eq",
    "cset x9, mi",
    "cset x9, ge",
    "cset x9, ne",
    "mvn x17, x16",
    "cmp x9, x17",
    "movz x16, #0x7ff8, lsl #48",
    "movz x16, #0x7fc0, lsl #16",
]

TRIPLES = {"x86_64": "x86_64-linux-gnu", "aarch64": "aarch64-linux-gnu"}


def text_section(path):
    """The bytes of the .text section of a little-endian ELF64 object."""
    data = Path(path).read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise RuntimeError(f"{path}: not an ELF64 object")
    shoff = struct.unpack_from("<Q", data, 0x28)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    sections = []
    for i in range(shnum):
        base = shoff + i * shentsize
        name, _, _, _, offset, size = struct.unpack_from("<IIQQQQ", data, base)
        sections.append((name, offset, size))
    _, names_offset, _ = sections[shstrndx]
    for name, offset, size in sections:
        end = data.index(b"\0", names_offset + name)
        if data[names_offset + name:end] == b".text":
            return data[offset:offset + size]
    raise RuntimeError(f"{path}: no .text section")


def assemble(clang, arch, case, directory):
    lines = [line.strip() for line in case.split(";")]
    prefix = ".intel_syntax noprefix\n" if arch == "x86_64" else ""
    stem = Path(directory) / f"{arch}-{abs(hash(case))}"
    source = stem.with_suffix(".s")
    source.write_text(prefix + ".text\n" + "\n".join(lines) + "\n")
    done = subprocess.run([clang, f"--target={TRIPLES[arch]}", "-c", str(source), "-o", str(stem.with_suffix(".o"))],
                          capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"{arch} case '{case}' does not assemble:\n{done.stderr}")
    return text_section(stem.with_suffix(".o"))


def render(clang):
    rows = []
    with tempfile.TemporaryDirectory() as directory, ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        jobs = [(arch, case) for arch, cases in (("x86_64", X86_64), ("aarch64", AARCH64)) for case in cases]
        results = list(pool.map(lambda job: assemble(clang, job[0], job[1], directory), jobs))
    for (arch, case), code in zip(jobs, results):
        hex_bytes = ", ".join(f"0x{b:02x}" for b in code)
        rows.append(f'    {{"{arch}", "{case}", {{{hex_bytes}}}}},')
    return ("// Generated by scripts/jit-encodings.py: the bytes of each case as clang's\n"
            "// assembler encodes it. Do not edit; regenerate.\n" + "\n".join(rows) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--check", action="store_true", help="fail when the committed table differs")
    args = parser.parse_args()
    table = render(args.clang)
    if args.check:
        if TABLE.read_text() != table:
            print(f"{TABLE.relative_to(ROOT)} is out of date: run scripts/jit-encodings.py", file=sys.stderr)
            return 1
        return 0
    TABLE.parent.mkdir(parents=True, exist_ok=True)
    TABLE.write_text(table)
    print(f"wrote {TABLE.relative_to(ROOT)}: {len(X86_64)} x86-64 and {len(AARCH64)} AArch64 cases")
    return 0


if __name__ == "__main__":
    sys.exit(main())

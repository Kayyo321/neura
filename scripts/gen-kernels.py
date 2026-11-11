#!/usr/bin/env python3
"""Generate the kernel table and reference from the annotated kernel headers.

The kernels are C functions in `src/kernels/`. Each header declares its
kernels in the small dialect that `src/kernels/neura_kernel.h` describes:

    NEURA_KERNEL("neura.area.name", 1, "cost formula")
    NEURA_KERNEL_BUFFER("param", "size formula")      (zero or more)
    result_type neura_k_area_name(type name, ...);

From every header in `src/kernels/` (sorted by name) this script writes:

  * `src/kernels/kernel_table.inc`: for C++, through `kernel_table.hpp`, a
    thunk per kernel with the generic signature `neura_kernel_thunk` and a
    `KernelInfo` table holding each kernel's name, ABI version, parameter
    and result kinds, and its step charge and buffer sizes as formula text
    and as postfix programs.
  * `docs/runtime/Kernels.md`: the ABI rules, the binding rules, and the
    kernel reference.
  * `stdlib/std/internal/kernels/<group>.nra`: for each header, the trusted
    internal module `Std.Internal.Kernels.<Group>` with one bodyless
    `kernel("neura.area.name", version)` function per kernel that has a
    binding (`emit_neura_declarations`; the C++ twin of its rules is
    `src/kernels/kernel_binding.hpp`).

By default the script writes the files. `--check` writes nothing and exits 1
when a generated file is missing or differs from what would be written.
Malformed annotations are errors in either mode. `--root` names another
checkout.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

KERNEL_DIR = Path("src/kernels")
TABLE = KERNEL_DIR / "kernel_table.inc"
REFERENCE = Path("docs/runtime/Kernels.md")
ABI_HEADER = "neura_kernel.h"

SCALARS = {
    "neura_i8": "I8", "neura_i16": "I16", "neura_i32": "I32", "neura_i64": "I64",
    "neura_u8": "U8", "neura_u16": "U16", "neura_u32": "U32", "neura_u64": "U64",
    "neura_f32": "F32", "neura_f64": "F64", "neura_bool": "Bool",
}
ELEMENTS = ["i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"]
VALUE_TYPES = {"neura_text": "Text", "neura_bytes": "Bytes"}
VALUE_TYPES.update({f"neura_view_{e}": "View" + e.upper() for e in ELEMENTS})
VALUE_TYPES.update({f"neura_mut_{e}": "Mut" + e.upper() for e in ELEMENTS})
VALUE_TYPES.update(SCALARS)
POINTER_TYPES = {"neura_out_text": "OutText", "neura_out_bytes": "OutBytes", "neura_state": "State",
                 "neura_work": "Work"}
POINTER_TYPES.update({t: "Result" + k for t, k in SCALARS.items()})
RESULT_TYPES = dict(SCALARS)
RESULT_TYPES["neura_status"] = "Status"
# Parameters that stand for a length in formulas, and those a buffer formula may size.
LENGTH_KINDS = {"Text", "Bytes", "OutText", "OutBytes", "State"} | {
    "View" + e.upper() for e in ELEMENTS} | {"Mut" + e.upper() for e in ELEMENTS}
BUFFER_KINDS = {"OutText", "OutBytes", "State"} | {"Mut" + e.upper() for e in ELEMENTS}
REQUIRED_BUFFER_KINDS = {"OutText", "OutBytes", "State"}

# How the reference describes each kind.
KIND_TEXT = {
    "Status": "status", "Text": "text view", "Bytes": "bytes view", "OutText": "text output",
    "OutBytes": "bytes output", "State": "state", "Work": "work budget", "Bool": "bool",
}
for e in ELEMENTS:
    KIND_TEXT[e.upper()] = e
    KIND_TEXT["View" + e.upper()] = f"{e} view"
    KIND_TEXT["Mut" + e.upper()] = f"writable {e} view"
    KIND_TEXT["Result" + e.upper()] = f"{e} result"
KIND_TEXT["ResultBool"] = "bool result"

ANNOTATION = re.compile(r'^NEURA_KERNEL\("([^"]*)",\s*([0-9]+),\s*"([^"]*)"\)\s*$')
BUFFER = re.compile(r'^NEURA_KERNEL_BUFFER\("([^"]*)",\s*"([^"]*)"\)\s*$')
PROTOTYPE = re.compile(r'^(\w+)\s+(neura_k_\w+)\s*\((.*)\)\s*;$', re.S)
PARAMETER = re.compile(r'^(\w+)\s*(\*?)\s*(\w+)$')
NAME = re.compile(r'^neura(\.[a-z][a-z0-9_]*)+$')
TOKEN = re.compile(r'\s*(?:(\d+)|([A-Za-z_]\w*)|(.))')
MAX_DEPTH = 16


class KernelError(Exception):
    pass


@dataclass
class Formula:
    text: str
    steps: list[tuple[str, int]]


@dataclass
class Param:
    name: str
    ctype: str
    pointer: bool
    kind: str


@dataclass
class Kernel:
    header: str
    line: int
    name: str
    version: int
    function: str
    result_ctype: str
    result: str
    params: list[Param]
    cost: Formula
    buffers: list[tuple[int, Formula]] = field(default_factory=list)
    doc: str = ""


def compile_formula(text: str, params: list[Param], where: str) -> Formula:
    """Parses a formula into postfix steps: ("Const", n), ("Arg", i), or an operator with 0."""
    tokens: list[tuple[str, str]] = []
    position = 0
    while position < len(text):
        match = TOKEN.match(text, position)
        if not match or match.end() == position:
            break
        position = match.end()
        number, word, symbol = match.groups()
        if number is not None:
            tokens.append(("number", number))
        elif word is not None:
            tokens.append(("word", word))
        elif symbol is not None and symbol.strip():
            if symbol not in "+-*/(),":
                raise KernelError(f"{where}: unexpected '{symbol}' in formula \"{text}\"")
            tokens.append(("symbol", symbol))
    names = {p.name: i for i, p in enumerate(params)}
    steps: list[tuple[str, int]] = []
    index = 0

    def peek() -> tuple[str, str] | None:
        return tokens[index] if index < len(tokens) else None

    def take(expected: str | None = None) -> tuple[str, str]:
        nonlocal index
        token = peek()
        if token is None or (expected is not None and token[1] != expected):
            raise KernelError(f"{where}: formula \"{text}\" expected {expected or 'more'}")
        index += 1
        return token

    def primary() -> None:
        kind, value = take()
        if kind == "number":
            steps.append(("Const", int(value)))
        elif kind == "word" and value in ("log2", "min", "max"):
            take("(")
            expression()
            if value != "log2":
                take(",")
                expression()
            take(")")
            steps.append((value.capitalize(), 0))
        elif kind == "word":
            if value not in names:
                raise KernelError(f"{where}: formula \"{text}\" names no parameter '{value}'")
            param = params[names[value]]
            if param.kind not in LENGTH_KINDS and param.kind not in SCALARS.values():
                raise KernelError(f"{where}: formula \"{text}\" cannot use parameter '{value}'")
            steps.append(("Arg", names[value]))
        elif value == "(":
            expression()
            take(")")
        else:
            raise KernelError(f"{where}: formula \"{text}\" has '{value}' out of place")

    def term() -> None:
        primary()
        while peek() is not None and peek()[1] in ("*", "/"):
            op = take()[1]
            primary()
            steps.append(("Mul" if op == "*" else "Div", 0))

    def expression() -> None:
        term()
        while peek() is not None and peek()[1] in ("+", "-"):
            op = take()[1]
            term()
            steps.append(("Add" if op == "+" else "Sub", 0))

    if not tokens:
        raise KernelError(f"{where}: empty formula")
    expression()
    if index != len(tokens):
        raise KernelError(f"{where}: formula \"{text}\" has trailing '{tokens[index][1]}'")
    depth = 0
    for op, _ in steps:
        depth += 1 if op in ("Const", "Arg") else (0 if op == "Log2" else -1)
        if depth > MAX_DEPTH:
            raise KernelError(f"{where}: formula \"{text}\" is deeper than {MAX_DEPTH}")
    return Formula(text, steps)


def parse_params(text: str, where: str) -> list[Param]:
    text = " ".join(text.split())
    if text in ("", "void"):
        return []
    params = []
    for part in text.split(","):
        match = PARAMETER.match(part.strip())
        if not match:
            raise KernelError(f"{where}: cannot read parameter '{part.strip()}'")
        ctype, star, name = match.groups()
        table = POINTER_TYPES if star else VALUE_TYPES
        if ctype not in table:
            raise KernelError(f"{where}: parameter '{name}' has type {ctype}{star}, outside the kernel dialect")
        params.append(Param(name, ctype, bool(star), table[ctype]))
    if len({p.name for p in params}) != len(params):
        raise KernelError(f"{where}: repeated parameter name")
    return params


def leading_comment(lines: list[str], index: int) -> str:
    """The /* ... */ comment that ends on the line before `index`, as one paragraph."""
    end = index - 1
    if end < 0 or not lines[end].rstrip().endswith("*/"):
        return ""
    start = end
    while start >= 0 and "/*" not in lines[start]:
        start -= 1
    if start < 0:
        return ""
    parts = []
    for line in lines[start:end + 1]:
        line = line.strip()
        if line.startswith("/*"):
            line = line[2:]
        elif line.startswith("*") and not line.startswith("*/"):
            line = line[1:]
        if line.endswith("*/"):
            line = line[:-2]
        parts.append(line.strip())
    return " ".join(" ".join(parts).split())


def parse_header(path: Path, relative: str) -> list[Kernel]:
    lines = path.read_text().split("\n")
    kernels = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.startswith("NEURA_KERNEL"):
            i += 1
            continue
        where = f"{relative}:{i + 1}"
        match = ANNOTATION.match(line)
        if not match:
            raise KernelError(f"{where}: malformed annotation: {line.strip()}")
        name, version, cost = match.group(1), int(match.group(2)), match.group(3)
        doc = leading_comment(lines, i)
        start = i
        i += 1
        buffers_text = []
        while i < len(lines) and lines[i].startswith("NEURA_KERNEL_BUFFER"):
            buffer = BUFFER.match(lines[i])
            if not buffer:
                raise KernelError(f"{relative}:{i + 1}: malformed buffer annotation")
            buffers_text.append((buffer.group(1), buffer.group(2), f"{relative}:{i + 1}"))
            i += 1
        prototype_lines = []
        while i < len(lines):
            prototype_lines.append(lines[i])
            if ";" in lines[i]:
                break
            i += 1
        i += 1
        prototype = " ".join(l.strip() for l in prototype_lines)
        proto = PROTOTYPE.match(prototype)
        if not proto:
            raise KernelError(f"{where}: the annotation is not followed by a kernel prototype")
        result_ctype, function, param_text = proto.groups()
        if not NAME.match(name):
            raise KernelError(f"{where}: kernel name '{name}' is not neura. and lowercase words")
        expected = "neura_k_" + name[len("neura."):].replace(".", "_")
        if function != expected:
            raise KernelError(f"{where}: kernel {name} must be the function {expected}, not {function}")
        if version < 1:
            raise KernelError(f"{where}: ABI version must be positive")
        if result_ctype not in RESULT_TYPES:
            raise KernelError(f"{where}: result type {result_ctype} is outside the kernel dialect")
        params = parse_params(param_text, where)
        kernel = Kernel(relative, start + 1, name, version, function, result_ctype, RESULT_TYPES[result_ctype],
                        params, compile_formula(cost, params, where), doc=doc)
        names = {p.name: index for index, p in enumerate(params)}
        for param_name, size, buffer_where in buffers_text:
            if param_name not in names:
                raise KernelError(f"{buffer_where}: no parameter '{param_name}'")
            if params[names[param_name]].kind not in BUFFER_KINDS:
                raise KernelError(f"{buffer_where}: '{param_name}' is not an output, state, or writable view")
            kernel.buffers.append((names[param_name], compile_formula(size, params, buffer_where)))
        sized = {index for index, _ in kernel.buffers}
        for index, param in enumerate(params):
            if param.kind in REQUIRED_BUFFER_KINDS and index not in sized:
                raise KernelError(f"{where}: '{param.name}' needs a NEURA_KERNEL_BUFFER size")
        kernels.append(kernel)
    return kernels


def collect(root: Path) -> list[Kernel]:
    kernels = []
    for path in sorted((root / KERNEL_DIR).glob("*.h")):
        if path.name == ABI_HEADER:
            continue
        kernels.extend(parse_header(path, path.relative_to(root).as_posix()))
    seen: dict[str, Kernel] = {}
    for kernel in kernels:
        if kernel.name in seen:
            raise KernelError(f"{kernel.header}:{kernel.line}: kernel {kernel.name} is declared twice")
        seen[kernel.name] = kernel
    return kernels


def headers(root: Path) -> list[str]:
    return [p.name for p in sorted((root / KERNEL_DIR).glob("*.h"))]


def identifier(kernel: Kernel) -> str:
    return kernel.name[len("neura."):].replace(".", "_")


def formula_cpp(formula: Formula, symbol: str) -> list[str]:
    steps = ", ".join(f"{{Op::{op}, {value}u}}" for op, value in formula.steps)
    return [f"inline constexpr Step {symbol}[] = {{{steps}}};"]


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def generate_table(root: Path, kernels: list[Kernel]) -> str:
    out = [
        "// Generated by scripts/gen-kernels.py from the headers in src/kernels; do not edit.",
        "// Included by kernel_table.hpp, which defines the types used here.",
        "#pragma once",
        "",
    ]
    out += [f'#include "{name}"' for name in headers(root) if name != ABI_HEADER]
    out += ["", "namespace neura::kernels::generated {", ""]
    for kernel in kernels:
        ident = identifier(kernel)
        args = []
        for i, param in enumerate(kernel.params):
            ctype = param.ctype + ("*" if param.pointer else "")
            args.append(f"*static_cast<{ctype} const*>(args[{i}])")
        call = f"{kernel.function}({', '.join(args)})"
        unused = "" if kernel.params else "/*args*/"
        out.append(f"inline void thunk_{ident}(void* const* {unused or 'args'}, void* result) {{")
        out.append(f"    *static_cast<{kernel.result_ctype}*>(result) = {call};")
        out.append("}")
        out += formula_cpp(kernel.cost, f"cost_{ident}")
        if kernel.params:
            kinds = ", ".join(f"Kind::{p.kind}" for p in kernel.params)
            names = ", ".join(c_string(p.name) for p in kernel.params)
            out.append(f"inline constexpr Kind params_{ident}[] = {{{kinds}}};")
            out.append(f"inline constexpr const char* param_names_{ident}[] = {{{names}}};")
        for n, (index, formula) in enumerate(kernel.buffers):
            out += formula_cpp(formula, f"buffer_{ident}_{n}")
        if kernel.buffers:
            entries = ", ".join(
                f"{{{index}u, {{{c_string(f.text)}, buffer_{ident}_{n}, {len(f.steps)}u}}}}"
                for n, (index, f) in enumerate(kernel.buffers))
            out.append(f"inline constexpr BufferSize buffers_{ident}[] = {{{entries}}};")
        out.append("")
    out.append("}  // namespace neura::kernels::generated")
    out.append("")
    out.append("namespace neura::kernels {")
    out.append("")
    out.append("inline constexpr KernelInfo kKernels[] = {")
    for kernel in kernels:
        ident = identifier(kernel)
        g = "generated::"
        params = f"{g}params_{ident}, {g}param_names_{ident}" if kernel.params else "nullptr, nullptr"
        buffers = f"{g}buffers_{ident}, {len(kernel.buffers)}u" if kernel.buffers else "nullptr, 0u"
        out.append(f"    {{{c_string(kernel.name)}, {kernel.version}u, "
                   f"{{{c_string(kernel.cost.text)}, {g}cost_{ident}, {len(kernel.cost.steps)}u}},")
        out.append(f"     {params}, {len(kernel.params)}u, Kind::{kernel.result}, {buffers}, "
                   f"{g}thunk_{ident}}},")
    out.append("};")
    out.append("")
    out.append(f"inline constexpr std::size_t kKernelCount = {len(kernels)};")
    out.append("")
    out.append("}  // namespace neura::kernels")
    out.append("")
    return "\n".join(out)


NEURA_SCALARS = {"I8": "int8", "I16": "int16", "I32": "int32", "I64": "int64", "U8": "uint8", "U16": "uint16",
                 "U32": "uint32", "U64": "uint64", "F32": "float32", "F64": "float64", "Bool": "bool"}
DECLARATIONS = Path("stdlib/std/internal/kernels")
LINE_WIDTH = 112


@dataclass
class Binding:
    """How a kernel binds to a Neura function; src/kernels/kernel_binding.hpp is the C++ twin."""
    problem: str | None
    arguments: list[Param]
    result: str


def element(kind: str) -> str:
    if kind in ("Bytes", "State"):
        return "U8"
    for prefix in ("View", "Mut", "Result"):
        if kind.startswith(prefix):
            return kind[len(prefix):]
    return kind


def neura_type(kind: str) -> str:
    if kind in NEURA_SCALARS:
        return NEURA_SCALARS[kind]
    if kind in ("Text", "OutText"):
        return "string"
    if kind == "OutBytes":
        return "uint8[list]"
    return NEURA_SCALARS[element(kind)] + "[list]"


def plan_binding(kernel: Kernel) -> Binding:
    problem = None
    arguments: list[Param] = []
    value = None
    outcome = False
    buffered = {index for index, _ in kernel.buffers}
    for index, param in enumerate(kernel.params):
        if param.kind == "Work":
            problem = "it takes a work budget, which the runtime does not supply yet"
        elif param.kind == "ResultI64" and param.name == "error_offset":
            outcome = True
        elif param.kind in ("OutText", "OutBytes") or param.kind.startswith("Result"):
            if value is not None:
                problem = "it returns more than one value"
            value = param.kind if param.kind in ("OutText", "OutBytes") else element(param.kind)
        elif param.kind.startswith("Mut") and index in buffered:
            pass
        else:
            arguments.append(param)
    if kernel.result != "Status":
        if value is not None:
            problem = "it returns more than one value"
        value = kernel.result
    if outcome:
        result = "[uint32, int64]" if value is None else f"[uint32, int64, {neura_type(value)}]"
    else:
        result = "void" if value is None else neura_type(value)
    return Binding(problem, arguments, result)


def module_name(header: str) -> tuple[str, str]:
    """The file stem and module name of a header's declarations: neura_integer_text.h is
    integer-text.nra and the module IntegerText."""
    stem = Path(header).stem[len("neura_"):]
    return stem.replace("_", "-"), "".join(word.capitalize() for word in stem.split("_"))


def wrap_doc(text: str, indent: str) -> list[str]:
    lines: list[str] = []
    words = text.split()
    current = indent + "/// @brief"
    for word in words:
        if len(current) + 1 + len(word) > LINE_WIDTH:
            lines.append(current)
            current = indent + "///"
        current += " " + word
    lines.append(current)
    return lines


def emit_neura_declarations(kernels: list[Kernel]) -> dict[Path, str]:
    """The Neura side of each kernel: for each header, the trusted internal module
    `Std.Internal.Kernels.<Group>` with one `kernel("<name>", <version>)` declaration per
    kernel that has a binding (docs/runtime/Kernels.md, "Binding a kernel")."""
    by_header: dict[str, list[Kernel]] = {}
    for kernel in kernels:
        by_header.setdefault(kernel.header, []).append(kernel)
    files: dict[Path, str] = {}
    for header, group in by_header.items():
        stem, module = module_name(header)
        out = [
            f"// Generated by scripts/gen-kernels.py from {header}; do not edit.",
            "",
        ]
        out += wrap_doc(f"The kernels of {Path(header).name} (docs/runtime/Kernels.md). Only the standard library "
                        "may import this module; each function runs the built-in C kernel it names, which "
                        "keeps the results, errors, and traps of the Neura code it replaces.", "")
        out.append(f"export let {module} = module (")
        names: set[str] = set()
        first = True
        for kernel in group:
            binding = plan_binding(kernel)
            words = kernel.name[len("neura."):].split(".")
            name = "_".join(words[1:]) if len(words) > 1 else words[0]
            if name in names:
                raise KernelError(f"{kernel.header}:{kernel.line}: two kernels of {header} declare '{name}'")
            names.add(name)
            if not first:
                out.append("")
            first = False
            if binding.problem:
                out.append(f"    // {kernel.name} version {kernel.version} has no binding: {binding.problem}.")
                continue
            out += wrap_doc(kernel.doc or kernel.name, "    ")
            out.append(f"    /// Step charge: `{kernel.cost.text}`.")
            params = ", ".join(f"{p.name}: {neura_type(p.kind)}" for p in binding.arguments)
            result = "" if binding.result == "void" else f" -> {binding.result}"
            head = f"    export let {name} = func({params}){result}"
            binding_text = f"kernel(\"{kernel.name}\", {kernel.version});"
            if len(head) + 1 + len(binding_text) <= LINE_WIDTH:
                out.append(f"{head} {binding_text}")
            else:
                out += [head, f"        {binding_text}"]
        out.append(");")
        out.append("")
        files[DECLARATIONS / f"{stem}.nra"] = "\n".join(out)
    return files


ABI_RULES = """\
<!-- Generated by scripts/gen-kernels.py from the headers in src/kernels; do not edit. -->
# Kernels

A kernel is a deterministic, pure C function that the Neura runtime can call in place of standard library code
written in Neura, which runs as a neuron circuit and is slow for coarse operations such as hashing a buffer or
sorting an array. The kernels live in [`src/kernels/`](../../src/kernels/) and build into the static library
`neura_kernels`. Each one replaces the body of a function of the standard library and keeps its results,
errors, and traps exactly; the Neura implementation stays as its test oracle.

**Status.** Every kernel below can be bound, and the generated modules `Std.Internal.Kernels.*` declare them.
The standard library calls them from `Std.Digest.Sha256` and `Std.Digest.Crc32`; every function of
`Std.Math.Math`; `Std.Collections.Sorting.sort_ascending`; `Std.Text.Strings` (searches, comparisons, trims,
`split`, `split_limit`, `lines`, replacements, ASCII case mappings, `scalar_count`, `hash`, and the error path of
`from_bytes`) and the `String` companion (comparisons, ASCII case comparison, `trim`, and the members built on
`Strings`); `StringBuilder.hash_code`; the integer companions' `to_string_radix` and `parse_radix`; and
`Std.Encoding.Utf8.validate`, `Base64`, and `Hex` ([Standard library, P2a](../lang/Stdlib.md#p2a-kernel-bindings-in-source)).
Every other module runs its Neura code. The scalar hash kernels `neura.hash.combine`, `spread`, `i64`, `u64`, and
`f64` are not called by the library, because a kernel call costs more steps than the few operations of Neura code
they would replace; they are kept for completeness and as oracles. An image that calls a kernel is NVR 1.6
([NVR](NVR.md#kernels-16)), and the NVM runs the call as a state operation
([NVM](NVM.md#kernel-calls)).

## ABI rules

The authoritative text is the comment at the top of
[`src/kernels/neura_kernel.h`](../../src/kernels/neura_kernel.h). In short:

- **Pure.** A kernel reads only its arguments and writes only the buffers, state, and result pointers it is
  given. It has no global mutable state, never allocates, never calls back into the runtime, and never reads the
  clock, the environment, the locale, or a file.
- **Self-contained.** No kernel calls a libm function or a locale-dependent libc function.
  `scripts/check-kernels.sh` fails if the archive has an undefined libm symbol. Floating point uses C's
  `+ - * /` and comparisons on IEEE 754 binary64 with round-to-nearest-even, compiled with
  `-ffp-contract=off` and never with `-ffast-math` or `-march=native`; the build requires `FLT_EVAL_METHOD`
  0. Every NaN that a float kernel returns is the canonical NaN `0x7FF8000000000000`.
- **Deterministic.** The same arguments give the same results and statuses on every platform. State kept
  between calls is a byte array with a fixed little-endian layout.
- **Charged in advance.** Each kernel declares a step charge as a formula over its arguments. The runtime
  evaluates it and charges it before the call, so a step limit fires at the same point in every execution
  mode. The charge counts the most work the arguments allow, and a kernel's worst-case work is within a
  constant factor of it ([Step charges](#running-a-kernel)).
- **Work budget.** A kernel whose work depends on the data beyond its argument sizes takes a `neura_work`:
  it may spend at most `budget` units, adds what it spends to `used`, and returns
  `NEURA_TRAP_WORK_EXHAUSTED` instead of exceeding the budget. The runtime charges `used` after the call. No
  current kernel needs one.

### Values

| C type | Kind | Meaning |
| --- | --- | --- |
| `neura_i8` ... `neura_u64`, `neura_f32`, `neura_f64` | scalar | A Neura integer or float of the same width |
| `neura_bool` | bool | A Neura `bool`: 0 is false; kernels return 0 or 1 |
| `neura_text` | text view | A Neura `string`, valid UTF-8, read-only |
| `neura_bytes` | bytes view | A `uint8[list]` or `uint8[]`, any bytes, read-only |
| `neura_view_T` | T view | A read-only array of T |
| `neura_mut_T` | writable T view | An array of T the kernel changes in place |
| `neura_out_text*` | text output | A buffer for new text, which becomes a `string` |
| `neura_out_bytes*` | bytes output | A buffer for a new `uint8[list]` |
| `neura_state*` | state | Bytes the caller keeps between calls, in the layout the kernel family documents |
| `neura_work*` | work budget | The budget of a data-dependent kernel |
| `neura_T*` for a scalar T | T result | Where the kernel stores an additional result |

An output buffer belongs to the caller. A kernel first works out the exact result length; when it exceeds the
capacity, the kernel stores the length and returns `NEURA_NEED_CAPACITY`, and the caller grows the buffer and
calls again without a second charge. Each output has a declared capacity formula that is always enough, so a
runtime that allocates it never sees that status.

### Formulas

A step charge or buffer size is a formula of decimal integers, parameter names, `+ - * /`, parentheses, and
`log2(x)` (the floor, with `log2(0) = 0`), `min(a, b)`, and `max(a, b)`. A view, output, or state parameter
stands for its length in elements; a scalar parameter for its value, a negative value counting as 0. The
arithmetic is unsigned 64-bit and saturating, and `x / 0` is 0. The generated table holds each formula as
text and as a postfix program for `neura::kernels::evaluate`.

### Statuses

A kernel that can fail returns a `neura_status`. `NEURA_ERR_*` statuses reach the Neura wrapper as the outcome
tuple of its binding (below), which the wrapper turns into an `Err` of `Std.Core.Result` with the offset the
kernel stores through its `error_offset` parameter; `NEURA_TRAP_*` statuses become a trap.
`neura_kernel_status_message` gives the exact message and `neura_kernel_status_error_kind` the `Error` variant.

## Binding a kernel

A bodyless function of a trusted standard library module binds a kernel by ending in
`kernel("<name>", <ABI version>)`, as a host binding ends in `host(...)`:

<!-- example: fragment: a member of a trusted standard library module; user code cannot bind a kernel -->
```neura
export let sha256 = func(bytes: uint8[list]) -> uint8[list] kernel("neura.digest.sha256", 1);
```

Only library source may bind a kernel (`E0780` elsewhere). Lowering looks the kernel up in the compiler's table
(`E0782` for an unknown name or ABI version), checks that the function's signature is the one the kernel's
parameter and result kinds give (`E0781`, which spells out the expected signature), and records the
`kernel.binding` attribute ([NeuraIR 1.10](../NirFormat.md)). A call compiles to a kernel state operation, not
to a host request. `scripts/gen-kernels.py` writes one such declaration per kernel into
`stdlib/std/internal/kernels/<header>.nra`, the internal module `Std.Internal.Kernels.<Header>`, which only
library code may import; [`src/kernels/kernel_binding.hpp`](../../src/kernels/kernel_binding.hpp) holds the same
rules for the compiler, the loader, and the runtime.

Each C parameter has one role:

| C parameter | Role | Neura side |
| --- | --- | --- |
| Integer, float, or `neura_bool` scalar | Argument | The Neura scalar of the same width and signedness: `int8` ... `uint64`, `float32`, `float64`, `bool` |
| `neura_text` | Argument | `string`, viewed in place |
| `neura_bytes` | Argument | `uint8[list]` (an array `uint8[]` also binds) |
| `neura_view_T` | Argument | `T[list]` or `T[]` |
| `neura_mut_T` without a buffer size | Argument | `T[list]` or `T[]`, written in place |
| `neura_mut_T` with a buffer size | Scratch | Nothing: the runtime supplies the scratch space |
| `neura_state*` | Argument | `uint8[list]` (or `uint8[]`) of exactly the declared size, written in place |
| `neura_out_text*` | Result | `string` |
| `neura_out_bytes*` | Result | `uint8[list]` |
| `neura_T*` named `error_offset` | Error offset | The second element of the outcome tuple |
| Any other `neura_T*` | Result | The scalar `T` |
| `neura_work*` | None | The kernel has no binding yet: the runtime does not supply a work budget |

The function's result is the kernel's value: its scalar C result, or its one result parameter, or `void` when
it has neither. A kernel with more than one value has no binding. A kernel with an `error_offset` returns the
**outcome tuple** `[uint32, int64, T]`, or `[uint32, int64]` without a value: the status, 0 for success or the
`NEURA_ERR_*` code, then the error offset (0 on success), then the value, which is zero or empty after an error.
The standard library wrapper turns the outcome into a `Result`, with the message of
`neura_kernel_status_message`. A `NEURA_TRAP_*` status is the program trap (reason 3) with the message of
`neura_kernel_status_message`, the same trap and text as the Neura code the kernel replaces.

With these rules every kernel of ABI generation 1 has a binding. Two shapes need care, and both are expressed
with storage the heap model already has. A writable view and a state are ordinary mutable arrays or lists that
the kernel writes in place, as a run of element writes would; the runtime copies them in and writes them back
only when the kernel succeeds, so a trapping kernel leaves them unchanged and two arguments never alias in C. A
state's contents are the module's private field (for example `Sha256`'s 112 bytes), and a state another module
corrupts traps with `invalid state: kernel state` instead of misbehaving.

## Running a kernel

A kernel exit runs in the NVM (see [NVM](NVM.md#kernel-calls) for the full contract):

1. Resolve the arguments. Text, an NVR byte string, and an `int64` or `uint64` array or list are viewed in place;
   any other array or list is packed into a buffer the state reuses.
2. Evaluate the step charge on the arguments' lengths and values and add it to `steps`, with the step limit
   check of a block step, before the kernel runs. Under `--profile` the steps count for the calling function.
3. Size each output from its buffer formula, run the kernel, copy writable arguments back, and allocate the
   result through the allocator that `ConstructText` and collection allocation use, with the same heap-cell
   accounting and collection schedule.

A kernel call is not a host request: no trace records it, `--replay` needs nothing for it, `--deny-native` and
the capability flags do not affect it, and it works in tasks, parallel items, fleets, and the static launcher.

**Step charges.** A charge is one step for the call plus one step per unit of work that takes no longer than an
average NVM block step on the reference evaluator, so a step limit never lets a kernel do more work than the
same number of block steps could. For the digests the unit is 64 bytes read: on the measuring host one SHA-256
compression took about 0.15 µs and CRC-32 over 64 bytes about 0.1 µs (plus about 0.06 µs to pack a list's
bytes), while an average block step of the replaced SHA-256 code took about 0.75 µs.

The work counted is the most the call can do with the arguments it is given, never what it happens to do, which
the runtime cannot know before the call:

- **Bounded by the arguments.** A charge counts the bytes or elements the call can read or write, plus the
  writable views the runtime copies: `s - from` for a search that starts at an offset, the window
  `min(s - from, limit + needle)` for `neura.text.index_of`, `min(s / target, limit)` copies of the replacement
  for `neura.text.replace`, and `n log n` for a sort. A call is never charged for bytes it cannot touch, and the
  charge is within a small constant factor of the work the call can do (`tests/kernels/kernel_tests.cpp`,
  suite `table`, checks representative charges against the elements touched).
- **Linear work.** A kernel's worst case must be linear in what it is charged for. The text searches use the
  two-way algorithm, linear in the text read plus the needle, so a pathological needle cannot make a search do
  quadratic work for a linear charge.
- **No repeated charges.** A library loop never calls a kernel on the same text over and over. `Strings.split`,
  `split_limit`, and `lines` find all separators with `neura.text.split`, one call to count and one to write the
  offsets; `Strings.index_of` and `index_of_from` call `neura.text.index_of` on windows that start at 128 bytes
  and double, so the charges of one search add up to about twice the distance it covers, and a loop that calls
  `index_of_from` after each occurrence is charged in proportion to the text.

**Versions.** A kernel's name and ABI version identify its signature and behaviour; a change to either takes a
new version or a new name once the kernel has been released. A step charge or buffer formula is part of the
runtime's table, not of the image, so a runtime that changes a charge charges every image by the new formula.
No release has shipped a kernel yet, so the kernels changed before release keep ABI version 1.

"""


def doc_formula(text: str) -> str:
    return "`" + text + "`"


def generate_reference(root: Path, kernels: list[Kernel]) -> str:
    out = [ABI_RULES.rstrip("\n"), "", "## Kernels", ""]
    out.append("Each table gives a header's kernels: the name and ABI version, the parameters in order (the C function")
    out.append("is `neura_k_` and the name after `neura.` with `.` turned into `_`), the result, the step charge, and the")
    out.append("buffer sizes. The notes under each table come from the comments in the header.")
    out.append("")
    by_header: dict[str, list[Kernel]] = {}
    for kernel in kernels:
        by_header.setdefault(kernel.header, []).append(kernel)
    for header, group in by_header.items():
        out.append(f"### [`{Path(header).name}`](../../{header})")
        out.append("")
        out.append("| Kernel | Version | Parameters | Result | Step charge | Buffers |")
        out.append("| --- | --- | --- | --- | --- | --- |")
        for kernel in group:
            params = ", ".join(f"`{p.name}` ({KIND_TEXT[p.kind]})" for p in kernel.params) or "none"
            buffers = ", ".join(f"`{kernel.params[i].name}`: {doc_formula(f.text)}" for i, f in kernel.buffers)
            out.append(f"| `{kernel.name}` | {kernel.version} | {params} | {KIND_TEXT[kernel.result]} | "
                       f"{doc_formula(kernel.cost.text)} | {buffers or 'none'} |")
        out.append("")
        for kernel in group:
            if kernel.doc:
                out.append(f"- `{kernel.name}`: {kernel.doc}")
        out.append("")
    text = "\n".join(out)
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    return text.rstrip("\n") + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if a generated file is stale")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    options = parser.parse_args()
    root = options.root.resolve()
    try:
        kernels = collect(root)
    except KernelError as error:
        print(f"gen-kernels: {error}", file=sys.stderr)
        return 2
    outputs = {TABLE: generate_table(root, kernels), REFERENCE: generate_reference(root, kernels)}
    try:
        outputs.update(emit_neura_declarations(kernels))
    except KernelError as error:
        print(f"gen-kernels: {error}", file=sys.stderr)
        return 2
    stale = []
    for relative, text in outputs.items():
        path = root / relative
        current = path.read_text() if path.exists() else None
        if current == text:
            continue
        if options.check:
            stale.append(relative.as_posix())
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
            print(f"wrote {relative.as_posix()}")
    if stale:
        for name in stale:
            print(f"gen-kernels: {name} is stale; run python3 scripts/gen-kernels.py", file=sys.stderr)
        return 1
    if options.check:
        print(f"gen-kernels: {len(kernels)} kernels, generated files up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Exercise typed numeric literals through the compiler, formatter, and runtime."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


NUMBERS = (
    ("signed8", "int8", "-128s8", "-128"),
    ("signed16", "int16", "32'767s16", "32767"),
    ("signed32", "int32", "64'000s32", "64000"),
    ("signed64", "int64", "-9'223'372'036'854'775'808s64", "-9223372036854775808"),
    ("unsigned8", "uint8", "255u8", "255"),
    ("unsigned16", "uint16", "65'535u16", "65535"),
    ("unsigned32", "uint32", "4'294'967'295u32", "4294967295"),
    ("unsigned64", "uint64", "18'446'744'073'709'551'615u64", "18446744073709551615"),
    ("half", "float16", "6.5'5e+0'4f16", "6.55e+04"),
    ("bfloat", "bfloat16", "1'024.0bf16", "1024.0"),
    ("single", "float32", "1f32", "1.0"),
    ("double", "float64", "13.8'85f64", "13.885"),
)

# Each tuple gives the exact source slice receiving the primary diagnostic.
# A leading single quote still opens a character literal, including when the
# failed spelling was intended as a number.
REJECTIONS = (
    ("let value = '120;", "E0261", "'", 12),
    ("let value = -'120;", "E0261", "'", 13),
    ("let value = 120';", "E0106", "'", 15),
    ("let value = 1''20;", "E0106", "'", 13),
    ("let value = 1'.2;", "E0106", "'", 13),
    ("let value = 1.'2;", "E0106", "'", 14),
    ("let value = 1.2';", "E0106", "'", 15),
    ("let value = 1.2'e3;", "E0106", "'", 15),
    ("let value = 1'E3;", "E0106", "'", 13),
    ("let value = 1e'3;", "E0106", "'", 14),
    ("let value = 1E'3;", "E0106", "'", 14),
    ("let value = 1e+'3;", "E0106", "'", 15),
    ("let value = 1e-'3;", "E0106", "'", 15),
    ("let value = 1e'-3;", "E0106", "'", 14),
    ("let value = 1e3';", "E0106", "'", 15),
    ("let value = 1'u8;", "E0106", "'", 13),
    ("let value = 1u8'0;", "E0106", "'", 15),
    ("let value = 1u'8;", "E0106", "'", 14),
    ("let value = 1.5u8;", "E0107", "u8", 15),
    ("let value = 1e3s32;", "E0107", "s32", 15),
    ("let value = 1u80;", "E0105", "u80", 13),
    ("let value = 1int;", "E0105", "int", 13),
    ("let value = 1byte;", "E0105", "byte", 13),
    ("let value = 1float;", "E0105", "float", 13),
    ("let value = 300u8;", "E0424", "300u8", 12),
    ("let value = -1u32;", "E0424", "-1u32", 12),
    ("let value = 128s8;", "E0424", "128s8", 12),
    ("let value = -129s8;", "E0424", "-129s8", 12),
    ("let value = 18446744073709551616u64;", "E0424", "18446744073709551616u64", 12),
    ("let value = 65520f16;", "E0424", "65520f16", 12),
    ("let value = 1e-50f32;", "E0424", "1e-50f32", 12),
    ("let value: uint64 = 1s32;", "E0403", "1s32", 20),
    ("let value: float64 = 1f32;", "E0403", "1f32", 21),
    ("let value = 1u8 + 1u16;", "E0403", "1u16", 18),
)


def run(binary, root, *args, source=None, success=True):
    result = subprocess.run((binary, *args), cwd=root, input=source, capture_output=True, timeout=60)
    assert (result.returncode == 0) == success, (args, result.returncode, result.stderr.decode())
    return result


def check_ir(binary, root, fixture):
    typed, annotated = [], []
    for name, typename, suffixed, bare in NUMBERS:
        # Match the literal's source location so the printed provenance
        # can also be compared byte for byte without postprocessing.
        typed.append(f"let {name}{' ' * (len(typename) + 2)} = {suffixed};\n")
        annotated.append(f"let {name}: {typename} = {bare};\n")
    for flags in ((), ("--no-optimize",)):
        fixture.write_text("".join(typed), encoding="utf-8")
        typed_ir = run(binary, root, "emit-ir", *flags, fixture.name).stdout
        fixture.write_text("".join(annotated), encoding="utf-8")
        annotated_ir = run(binary, root, "emit-ir", *flags, fixture.name).stdout
        assert typed_ir == annotated_ir, (flags, typed_ir.decode(), annotated_ir.decode())


def check_rejections(binary, root, fixture):
    for source, code, primary, offset in REJECTIONS:
        assert source[offset:offset + len(primary)] == primary, (source, offset, primary)
        fixture.write_text(source + "\n", encoding="utf-8")
        result = run(binary, root, "check", "--error-format=short", fixture.name, success=False)
        first = result.stderr.decode().splitlines()[0]
        location = rf"{re.escape(fixture.name)}:1:{offset + 1}: error\[{code}\]: "
        assert re.search(location, first), (source, first, location)
        detailed = run(binary, root, "check", "--error-format=json", fixture.name, success=False)
        diagnostic = json.loads(detailed.stderr.decode().splitlines()[0])
        span = next(span for span in diagnostic["spans"] if span["primary"])
        assert (span["offset"], span["length"]) == (offset, len(primary)), (source, diagnostic)


def check_contexts(binary, root, fixture):
    source = """let identity = func<T>(value: T) -> T => value;

let select_width = func(value: uint16) -> uint16 => value
    or func(value: int32) -> int32 => value;

/// Return the input or the default limit.
export let limit = func(value: uint16 = 1'024u16) -> uint16 => value;

let integer = 10'000;
let default_integer: int32 = integer;
let contextual_integer: uint16 = 1'024;
let floating = 1'2.3'4e+0'1;
let default_float: float64 = floating;
let contextual_float: float16 = 1.0'5;
let unary: uint32 = -0u32;
let sum: uint16 = 1 + 2u16;
let picked: uint16 = select_width(1u16);
let inferred = identity(1u16);
let inferred_width: uint16 = inferred;
let choose = func(ready: bool) -> uint16 {
    let choice = ready ? 1 : 2u16;
    return choice;
};
let unchanged_character: byte = '1';
let range = 1'0 .. 1'2;
let interpolation = $"{1'024u16}";
let page = html() {
    <p>{1'024u16}</p>
};
"""
    fixture.write_text(source, encoding="utf-8")
    run(binary, root, "check", fixture.name)
    formatted = run(binary, root, "fmt", "--stdin", "--stdin-filepath", fixture.name, source=source.encode()).stdout
    assert b"1'024u16" in formatted and b"{1'024u16}</p>" in formatted, formatted
    fixture.write_bytes(formatted)
    run(binary, root, "fmt", "--check", fixture.name)
    run(binary, root, "check", fixture.name)
    docs = run(binary, root, "docs", fixture.name).stdout
    assert b"value: uint16 = 1'024u16" in docs, docs
    doxygen = run(binary, root, "docs", "--doxygen", fixture.name).stdout
    assert b"value = 1024" in doxygen, doxygen


def check_rounding(binary, root, fixture):
    fixture.write_text("""let console = import:Std.Io.Console;
let program = import:Std.Program;

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let tie = 1.000'488'281'25f16;
        let above = 1.000'488'281'250'000'000'000'01f16;
        let negative_zero = -0f32;
        console.print_line(tie.to_bits().to_string());
        console.print_line(above.to_bits().to_string());
        console.print_line(negative_zero.to_bits().to_string());
        return program.ExitCode.Ok;
    };
);
""", encoding="utf-8")
    image = "numeric.nvr"
    run(binary, root, "build", fixture.name, "-o", image)
    first = run(binary, root, "run", image).stdout
    assert first == b"15360\n15361\n2147483648\n", first
    assert run(binary, root, "run", image).stdout == first


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="neura-numeric-literals-", dir=Path.cwd()) as directory:
        root = Path(directory)
        fixture = root / "numeric.nra"
        check_ir(binary, root, fixture)
        check_rejections(binary, root, fixture)
        check_contexts(binary, root, fixture)
        check_rounding(binary, root, fixture)
    print(f"Checked {len(NUMBERS)} suffixes, {len(REJECTIONS)} diagnostic spans, byte-identical IR, and runtime rounding.")


if __name__ == "__main__":
    main()

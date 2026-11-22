#!/usr/bin/env python3
"""A Pointer import alone leaves the native argument codec unchanged."""

from pathlib import Path
import subprocess
import sys
import tempfile


compiler = Path(sys.argv[1]).resolve()
imports = """let program = import:Std.Program;
let descriptor = import:Std.DynamicForeign.FunctionDescriptor;
let layouts = import:Std.DynamicForeign.ValueLayout;
let segments = import:Std.DynamicForeign.MemorySegment;
let methods = import:Std.DynamicForeign.MethodHandle;
"""

direct_program = imports + """
let App = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let handle: methods.MethodHandle = new(segments.of_address(1), descriptor.of_void(layouts.Address), -1);
        handle.invoke(segments.of_address(2));
        return program.ExitCode.Ok;
    };
);
"""

generic_class_program = imports + """
let forward = func<T>(handle: methods.MethodHandle, value: T) {
    handle.invoke(value);
};

let App = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let handle: methods.MethodHandle = new(segments.of_address(1), descriptor.of_void(layouts.Address), -1);
        let erased: class = segments.of_address(2);
        forward<class>(handle, erased);
        return program.ExitCode.Ok;
    };
);
"""


def checked(*arguments):
    result = subprocess.run([compiler, *arguments], capture_output=True, encoding="utf-8", timeout=60)
    assert result.returncode == 0, result.stderr


def build(source, image):
    checked("build", "--no-source-map", source, "-o", image)
    return image.read_bytes()


def check_unused_import(case, program):
    with tempfile.TemporaryDirectory(prefix="neura-pointer-unused-") as temporary:
        directory = Path(temporary)
        source = directory / "ffi.nra"
        source.write_text(program, encoding="utf-8")
        plain = build(source, directory / "plain.nvr")
        source.write_text("let.promote _ = import:Std.Core.Pointer;\n" + program, encoding="utf-8")
        imported = build(source, directory / "imported.nvr")
        assert plain == imported, (
            f"{case}: an unused Pointer import changed the image ({len(plain)} vs {len(imported)} bytes)"
        )

        nir = directory / "ffi.nir"
        checked("emit-ir", source, "-o", nir)
        restored = build(nir, directory / "restored.nvr")
        assert plain == restored, (
            f"{case}: the unused import changed the NeuraIR build ({len(plain)} vs {len(restored)} bytes)"
        )
        print(f"{case}: unused Pointer import preserves all {len(plain)} image bytes from source and NeuraIR")


check_unused_import("direct", direct_program)
check_unused_import("generic class", generic_class_program)

#!/usr/bin/env python3
"""Exercise managed pointer lifetimes, bounds, and isolation through the SDK."""

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv.pop(1)).resolve()
NATIVE = sys.argv[1:2] == ["--native"]
if NATIVE:
    sys.argv.pop(1)

MODES = [
    ("tier 0", "off", ["--tier=0", "--jit=off"]),
    ("tier 1", "off", ["--tier=1", "--jit=off"]),
    ("tier 2 interpreter", "off", ["--tier=2", "--jit=off"]),
    ("tier 3 interpreter", "interp", ["--tier=2", "--jit=off"]),
]
if NATIVE:
    MODES += [
        ("tier 2 native", "off", ["--tier=2", "--jit=on"]),
        ("tier 3 native", "native", ["--tier=2", "--jit=on"]),
    ]

POINTER = """let cell = import:Std.Core.Cell;
let.promote _ = import:Std.Core.Pointer;
"""
ENTRY = """let program = import:Std.Program;
let console = import:Std.Io.Console;
"""
FOREIGN = """let descriptor = import:Std.DynamicForeign.FunctionDescriptor;
let layouts = import:Std.DynamicForeign.ValueLayout;
let segments = import:Std.DynamicForeign.MemorySegment;
let methods = import:Std.DynamicForeign.MethodHandle;

let native = func -> methods.MethodHandle {
    return new(segments.of_address(1), descriptor.of_void(layouts.Address), -1);
};
"""


def program(body, declarations=""):
    return POINTER + ENTRY + declarations + """
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
""" + body + """
        return program.ExitCode.Ok;
    };
);
"""


class PointerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="neura-pointer-")
        self.addCleanup(temporary.cleanup)
        self.path = pathlib.Path(temporary.name)
        self.source = self.path / "main.nra"
        self.image = self.path / "main.nvr"

    def command(self, *arguments, env=None):
        return subprocess.run([str(NEURA), *map(str, arguments)], cwd=self.path, env=env,
                              capture_output=True, text=True, encoding="utf-8", timeout=120)

    def build(self, source, *flags, warnings=()):
        self.source.write_text(source, encoding="utf-8")
        built = self.command("build", "--error-format=json", *flags, self.source, "-o", self.image)
        self.assertEqual(built.returncode, 0, built.stderr)
        observed = []
        for line in built.stderr.splitlines():
            if not line.startswith("{"):
                continue
            diagnostic = json.loads(line)
            if diagnostic["code"] in ("W0006", "W0007"):
                span = next(span for span in diagnostic["spans"] if span["primary"])
                offset = span["offset"]
                observed.append((diagnostic["code"], source[offset:offset + span["length"]]))
        self.assertEqual(sorted(observed), sorted(warnings), built.stderr)

    def run_modes(self, status, output="", error=None, args=(), limits=()):
        results = {}
        for name, tier3, tier_flags in MODES:
            with self.subTest(mode=name, args=args):
                env = dict(os.environ, NEURA_TIER3=tier3)
                if tier3 == "native":
                    env["NEURA_TIER3_VERIFY"] = "1"
                result = self.command("run", "--runtime=off", "--gpu=off", "--deny-native", "--stats",
                                      *tier_flags, *limits, self.image, "--", *args, env=env)
                self.assertEqual(result.returncode, status, result.stderr)
                self.assertEqual(result.stdout, output)
                if error:
                    self.assertIn(error, result.stderr)
                results[name] = result
        return results

    def assert_counts_agree(self, results):
        pattern = re.compile(r"stats: steps (\d+), peak heap cells (\d+)", re.S)
        stats = [pattern.search(result.stderr) for result in results.values()]
        self.assertTrue(all(stats), [result.stderr for result in results.values()])
        self.assertEqual(len({item.groups() for item in stats}), 1)

    def test_retained_cell_and_primitive_reference_after_destroy(self):
        source = program('''
        let storage = cell.of<int64>(7);
        let alias = storage;
        let ptr: Pointer<int64> = new(storage);
        let borrowed: &int64 = storage.value;
        borrowed = 12;
        console.println("{} {}", ptr.read(), storage.value);
        let other = ptr.clone();
        let readonly = ptr.as_const().clone();
        destroy storage;
        other.write(42);
        console.println("{} {} {}", ptr.read(), other.read(), readonly.read());
        if (args.length != 0) {
            if (args[0] == "borrow") {
                console.println("{}", borrowed);
            } else {
                console.println("{}", alias.value);
            }
        }
''')
        for flags in ((), ("--no-optimize",)):
            self.build(source, *flags)
            results = self.run_modes(0, "12 12\n42 42 42\n")
            self.assert_counts_agree(results)

            self.run_modes(139, "12 12\n42 42 42\n", "invalid reference handle", ("destroyed-cell",))
            self.run_modes(139, "12 12\n42 42 42\n", "invalid reference handle", ("borrow",))

    def test_retained_fixed_array_and_live_list(self):
        declarations = '''
let make = func -> ListPointer<int64> {
    let original: int64[] = [10, 20, 30, 40];
    let whole: ListPointer<int64> = new(original, 0, 4);
    let middle = whole.sub(start: 1, end: 3);
    middle.set(1, 31);
    console.println("{} {}", original[2], whole.value_at(2));
    return middle;
};
'''
        source = program('''
        let middle = make();
        let smallest = middle.sub(1, 2);
        console.println("{} {} {}", middle.len(), smallest.value_at(0), middle.is_empty());
        let list: int64[list] = new([1, 2, 3]);
        let view: ListPointer<int64> = new(list, 0, 3);
        let sub = view.sub(1, 3);
        list[1] = 22;
        sub.set(1, 33);
        console.println("{} {}", sub.value_at(0), list[2]);
        if (args.length != 0) {
            list.append(4);
            list.remove_at(3);
            console.println("{}", sub.len());
        }
''', declarations)
        expected = "31 31\n2 31 false\n22 33\n"
        for flags in ((), ("--no-optimize",)):
            self.build(source, *flags)
            self.assert_counts_agree(self.run_modes(0, expected))
            self.run_modes(139, expected, "index out of bounds: stale ListPointer view", ("change",))

    def test_bounds_and_nil(self):
        self.build(program('''
        let original: int[] = [1, 2, 3];
        let view: ListPointer<int> = new(original, 0, 3);
        if (args[0] == "negative") {
            view.value_at(-1);
        } else if (args[0] == "range") {
            let rejected: ListPointer<int> = new(original, 2, 1);
        } else if (args[0] == "end") {
            view.sub(0, 4);
        } else if (args[0] == "max") {
            view.set(2147483647, 10);
        } else if (args[0] == "nil-cell") {
            let absent: cell.Cell<int> = nil;
            let invalid: Pointer<int> = new(absent);
        } else {
            let absent: Pointer<int> = nil;
            absent.read();
        }
'''), warnings=(("W0007", "absent"), ("W0006", "absent")))
        for case in ("negative", "range", "end", "max"):
            self.run_modes(139, "", "source bounds failure (trap reason 2)", (case,))
        self.run_modes(139, "", "nil class reference", ("nil",))
        self.run_modes(139, "", "nil class reference", ("nil-cell",))

    def test_erasure_reflection_and_native_packing(self):
        declarations = "let types = import:Std.Reflect.Type.TypeDescriptor;\n" + FOREIGN
        source = program('''
        let ptr: Pointer<int64> = new(cell.of<int64>(19));
        let erased: class = ptr;
        let held: any = ptr;
        let values: any[list] = new([held]);
        console.println("{} {}", typeof(values[0]).name, types.of_value(erased).fields.length);
        if (args.length != 0) {
            if (args[0] == "class") {
                native().invoke(erased);
            } else if (args[0] == "destroy") {
                destroy erased;
            } else {
                native().invoke(held);
            }
        }
''', declarations)
        self.build(source)
        self.run_modes(0, "Pointer<int64> 0\n")
        for erased in ("class", "any"):
            self.run_modes(134, "Pointer<int64> 0\n", "argument does not fit", (erased,))
        self.run_modes(134, "Pointer<int64> 0\n", "a managed pointer cannot be destroyed", ("destroy",))

    def test_const_pointer_can_cross_an_ordinary_task(self):
        declarations = '''
let consume = func(view: ConstPointer<int64> = nil) {
    if (view) {
        console.println("{}", view.read());
    } else {
        console.println("nil");
    }
};
'''
        paths = (
            ("ordinary", '''
        let ptr: Pointer<int64> = new(cell.of<int64>(7));
        let readonly = ptr.as_const();
''', "7\n"),
            ("boxed", '''
        let storage = cell.of<int64>(7);
        let borrowed: &int64 = storage.value;
        borrowed = 8;
        let ptr: Pointer<int64> = new(storage);
        let readonly = ptr.as_const();
        destroy storage;
''', "8\n"),
            ("nil-only", "        let readonly: ConstPointer<int64> = nil;\n", "nil\n"),
        )
        for name, initialization, expected in paths:
            with self.subTest(storage=name):
                self.build(program(initialization + '''
        scope {
            spawn consume(readonly);
        }
''', declarations))
                self.run_modes(0, expected)

    def test_const_pointer_with_frozen_list_stays_in_its_task(self):
        declarations = '''
let frozen = import:Std.Collections.Frozen;
let consume = func(view: ConstPointer<frozen.FrozenList<int>>) {
    console.println("{}", view.read().get(0));
};
'''
        initialization = '''
        let items = frozen.of<int>(23);
        let storage = cell.of<frozen.FrozenList<int>>(items);
        let pointer: Pointer<frozen.FrozenList<int>> = new(storage);
        let readonly = pointer.as_const();
'''
        self.build(program(initialization + "        consume(readonly);\n", declarations))
        self.run_modes(0, "23\n")

        source = program(initialization + "        scope { spawn consume(readonly); }\n", declarations)
        self.source.write_text(source, encoding="utf-8")
        result = self.command("check", "--error-format=json", self.source)
        self.assertEqual(result.returncode, 1, result.stderr)
        diagnostic = json.loads(result.stderr.splitlines()[0])
        self.assertEqual(diagnostic["code"], "E0602", result.stderr)
        self.assertEqual(diagnostic["spans"][0]["line"],
                         next(i for i, line in enumerate(source.splitlines(), 1) if "spawn consume" in line))
        self.assertTrue(any("pointee type" in note and "FrozenList<int32>" in note
                            for note in diagnostic["notes"]), result.stderr)

    def test_artifact_roundtrip(self):
        declarations = '''
let read = func(view: ConstPointer<int>, items: ListPointer<int>) {
    console.println("{} {}", view.read(), items.value_at(1));
};
'''
        self.build(program('''
        let storage = cell.of<int>(3);
        let borrow: &int = storage.value;
        borrow = 8;
        let pointer: Pointer<int> = new(storage);
        let readonly = pointer.as_const();
        destroy storage;
        let values: int[] = [13, 21];
        let items: ListPointer<int> = new(values, 0, 2);
        scope {
            spawn observe(readonly);
        }
        read(readonly, items);
''', declarations + '''
let observe = func(view: ConstPointer<int>) {
    console.println("{}", view.read());
};
'''))
        ir = self.path / "main.nir"
        ir_image = self.path / "compiled.nvr"
        assembly = self.path / "main.nvra"
        assembled = self.path / "assembled.nvr"
        for arguments in (("emit-ir", self.source, "-o", ir),
                          ("build", ir, "-o", ir_image),
                          ("disassemble", self.image, "-o", assembly),
                          ("assemble", assembly, "-o", assembled)):
            result = self.command(*arguments)
            self.assertEqual(result.returncode, 0, result.stderr)
        expected = self.image.read_bytes()
        self.assertTrue(ir_image.read_bytes() == expected, "building from NeuraIR changed the image")
        self.assertTrue(assembled.read_bytes() == expected, "assembling the disassembly changed the image")
        self.image = assembled
        self.assert_counts_agree(self.run_modes(0, "8\n8 21\n"))

    def test_pointer_artifact_with_law_and_nil_metadata(self):
        self.build(program('''
        let storage = cell.of<int>(3);
        let view: Pointer<int> = new(storage);
        if (view) {
            console.println("{}", view.read());
        }
''', '''
let Proofs = module (
    export let Reflexive = law (
        for value: bool;
        claim: { value == value : bool };
    );
        | proof: {==};
);
'''))
        ir = self.path / "mixed.nir"
        result = self.command("emit-ir", self.source, "-o", ir)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(ir.read_bytes()[:8], b"NEIR\x01\x00\x11\x00")
        listing = self.command("emit-ir", "--no-optimize", ir)
        self.assertEqual(listing.returncode, 0, listing.stderr)
        self.assertIn("non_nil_uses {", listing.stdout)
        verdict = self.command("prove", ir)
        self.assertEqual(verdict.returncode, 0, verdict.stderr)
        self.assertIn("PROVEN   Proofs.Reflexive", verdict.stdout)
        artifact_image = self.path / "mixed.nvr"
        built = self.command("build", ir, "-o", artifact_image)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertEqual(artifact_image.read_bytes(), self.image.read_bytes())

    def test_static_rejections(self):
        cases = [
            ("E0451", "unknown allocation site", '''
let retain = func(incoming: int[]) -> ListPointer<int> {
    return new(incoming, 0, incoming.length);
};
''', "incoming, 0"),
            ("E0580", "deeply immutable", '''
let Mutable = class (export let field: int = 0;);
let reject = func {
    let ptr: Pointer<Mutable> = new(cell.of<Mutable>(new Mutable()));
    ptr.as_const();
};
''', "ptr.as_const"),
            ("E0514", "managed pointer cannot be destroyed", '''
let reject = func(ptr: Pointer<int>) {
    destroy ptr;
};
''', "ptr;"),
            ("E0403", "not a native value", FOREIGN + '''
let reject = func(ptr: Pointer<int>) {
    native().invoke(ptr);
};
''', "ptr);"),
            ("E0403", "not a native value", FOREIGN + '''
let invoke_generic = func<T>(value: T) {
    native().invoke(value);
};
let reject = func(ptr: Pointer<int>) {
    invoke_generic<Pointer<int>>(ptr);
};
''', "invoke(value)"),
            ("E0403", "not a native value", FOREIGN + '''
let reject = func(ptr: Pointer<int>) {
    segments.of_address(ptr);
};
''', "of_address(ptr)"),
            ("E0415", "cannot change a managed pointer", '''
let reject = func(ptr: ConstPointer<int>) {
    let bad = ptr as Pointer<int>;
};
''', "ptr as Pointer"),
            ("E0507", "constructor is not exported", '''
let reject = func {
    let bad: ConstPointer<int> = new(cell.of<int>(1));
};
''', "new(cell.of<int>"),
            ("E0602", "not transferable", '''
let read = func(ptr: Pointer<int>) { ptr.read(); };
let reject = func(ptr: Pointer<int>) {
    scope { spawn read(ptr); }
};
''', "spawn read"),
            ("E0602", "not transferable", '''
let read = func(ptr: ListPointer<int>) { ptr.len(); };
let reject = func(ptr: ListPointer<int>) {
    scope { spawn read(ptr); }
};
''', "spawn read"),
            ("E0305", "unknown or inaccessible member", '''
let reject = func(ptr: ConstPointer<int>) {
    ptr.write(1);
};
''', "ptr.write"),
        ]
        for code, message, body, use in cases:
            with self.subTest(code=code, use=use):
                text = POINTER + body
                self.source.write_text(text, encoding="utf-8")
                result = self.command("check", "--error-format=json", self.source)
                self.assertEqual(result.returncode, 1)
                diagnostic = json.loads(result.stderr.splitlines()[0])
                self.assertEqual(diagnostic["code"], code, result.stderr)
                self.assertIn(message, diagnostic["message"])
                line = next(i for i, value in enumerate(text.splitlines(), 1) if use in value)
                self.assertEqual(diagnostic["spans"][0]["line"], line, result.stderr)


if __name__ == "__main__":
    unittest.main()

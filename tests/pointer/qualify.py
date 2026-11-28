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
        declarations = "let types = import:Std.Reflect.Type.TypeDescriptor;\n" + FOREIGN + """
let invoke_erased = func<T>(value: T) {
    native().invoke(value);
};
"""
        source = program('''
        let ptr: Pointer<int64> = new(cell.of<int64>(19));
        let erased: class = ptr;
        let held: any = ptr;
        let values: any[list] = new([held]);
        console.println("{} {}", typeof(values[0]).name, types.of_value(erased).fields.length);
        if (args.length != 0) {
            if (args[0] == "class") {
                native().invoke(erased);
            } else if (args[0] == "generic-class") {
                invoke_erased<class>(erased);
            } else if (args[0] == "destroy") {
                destroy erased;
            } else {
                invoke_erased<any>(held);
            }
        }
''', declarations)
        self.build(source)
        self.run_modes(0, "Pointer<int64> 0\n")
        for erased in ("class", "generic-class", "any"):
            self.run_modes(134, "Pointer<int64> 0\n", "argument does not fit", (erased,))
        self.run_modes(134, "Pointer<int64> 0\n", "a managed pointer cannot be destroyed", ("destroy",))

    def test_c2_native_variadic_rejects_static_any(self):
        source = FOREIGN + """
let reject = func {
    let held: any = 5 as int64;
    native().invoke(held);
};
"""
        self.source.write_text(source, encoding="utf-8")
        checked = self.command("check", "--error-format=json", self.source)
        self.assertEqual(checked.returncode, 1, checked.stderr)
        diagnostic = json.loads(checked.stderr.splitlines()[0])
        self.assertEqual(diagnostic["code"], "E0493", checked.stderr)
        self.assertIn("operation is not defined for any", diagnostic["message"])
        primary = next(span for span in diagnostic["spans"] if span["primary"])
        self.assertEqual(source[primary["offset"]:primary["offset"] + primary["length"]], "held")

    def test_c4_generic_native_argument_uses_existing_nir_version(self):
        source = program("""
        invoke_generic<int64>(5);
""", FOREIGN + """
let invoke_generic = func<T>(value: T) {
    native().invoke(value);
};
""").removeprefix(POINTER)
        self.build(source)

        module = self.path / "native-generic.nir"
        emitted = self.command("emit-ir", self.source, "-o", module)
        self.assertEqual(emitted.returncode, 0, emitted.stderr)
        artifact = module.read_bytes()
        self.assertEqual(artifact[:6], b"NEIR\x01\x00")
        self.assertLess(int.from_bytes(artifact[6:8], "little"), 17)
        self.assertIn(b"$native_argument", artifact)

        rebuilt = self.path / "native-generic.nvr"
        built = self.command("build", module, "-o", rebuilt)
        self.assertEqual(built.returncode, 0, built.stderr)
        self.assertTrue(rebuilt.read_bytes() == self.image.read_bytes())

    def reject(self, text, code, message, use, notes=()):
        """Checks one rejection: its code, message, primary line, and notes."""
        self.source.write_text(text, encoding="utf-8")
        result = self.command("check", "--error-format=json", self.source)
        self.assertEqual(result.returncode, 1, result.stderr)
        diagnostic = next(json.loads(line) for line in result.stderr.splitlines()
                          if line.startswith("{") and json.loads(line)["severity"] == "error")
        self.assertEqual(diagnostic["code"], code, result.stderr)
        self.assertIn(message, diagnostic["message"])
        primary = next(span for span in diagnostic["spans"] if span["primary"])
        self.assertEqual(primary["line"], next(i for i, line in enumerate(text.splitlines(), 1) if use in line),
                         result.stderr)
        for note in notes:
            self.assertTrue(any(note in item for item in diagnostic["notes"]), result.stderr)
        self.assertFalse(any("monitor" in item for item in diagnostic["notes"]), result.stderr)
        return diagnostic

    KINDS_HEADER = POINTER + """let parallel = import:Std.Concurrent.Parallel;
.Immutable
let Fixed = class (
    export last n: int = 0;
);
let Mutable = class (
    export let n: int = 0;
);
"""
    # One shareable and one task-local instantiation of each declared kind.
    SHAREABLE = ("SharedPointer<Fixed>", "WeakPointer<Fixed>", "SoftPointer<Fixed>", "AtomicPointer<Fixed>",
                 "LockedPointer<Mutable>")
    TASK_LOCAL = ("UniquePointer<Mutable>", "SharedPointer<Mutable>", "WeakPointer<Mutable>", "SoftPointer<Mutable>")

    def accept(self, text):
        self.source.write_text(text, encoding="utf-8")
        result = self.command("check", "--error-format=json", self.source)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_declared_kinds_accept_their_targets(self):
        self.accept(self.KINDS_HEADER + """
let use = func(a: UniquePointer<Mutable>, b: SharedPointer<int[]>, c: WeakPointer<Mutable>, d: SoftPointer<int[list]>,
               e: AtomicPointer<Fixed>, f: LockedPointer<int>, g: LockedPointer<Mutable>) {};
""")
        for name, code, message, declaration in (
            ("unique value", "E0418", "UniquePointer needs a class or interface type", "UniquePointer<string>"),
            ("shared value", "E0418", "SharedPointer needs a managed object type", "SharedPointer<int>"),
            ("weak value", "E0418", "WeakPointer needs a managed object type", "WeakPointer<bool>"),
            ("soft tuple", "E0418", "SoftPointer needs a managed object type", "SoftPointer<[int, int]>"),
            ("atomic interface", "E0418", "AtomicPointer needs an #Immutable class type", "AtomicPointer<Shape>"),
            ("atomic mutable", "E0580", "an AtomicPointer can hold only a deeply immutable value",
             "AtomicPointer<Mutable>"),
        ):
            with self.subTest(target=name):
                self.reject(self.KINDS_HEADER + """
let Shape = interface (
    let area = func -> int;
);
let use = func(pointer: """ + declaration + """) {};
""", code, message, "let use = func(pointer:")

    def test_declared_kinds_follow_the_transfer_rules(self):
        for kind in self.SHAREABLE:
            with self.subTest(spawn=kind):
                self.accept(self.KINDS_HEADER + f"""
let read = func(pointer: {kind}) {{}};
let use = func(pointer: {kind}) {{
    scope {{ spawn read(pointer); }}
}};
""")
        for kind in self.TASK_LOCAL:
            with self.subTest(spawn=kind):
                diagnostic = self.reject(self.KINDS_HEADER + f"""
let read = func(pointer: {kind}) {{}};
let use = func(pointer: {kind}) {{
    scope {{ spawn read(pointer); }}
}};
""", "E0602", "given to a spawned task", "spawn read")
                self.assertTrue(diagnostic["notes"], diagnostic)
        for kind in self.SHAREABLE + self.TASK_LOCAL:
            with self.subTest(item=kind):
                self.reject(self.KINDS_HEADER + f"""
let read = func(pointer: {kind}) -> int => 1;
let use = func(pointers: {kind}[]) {{
    let _ = parallel.map<{kind}, int>(pointers, read, parallel.limits(1000, 1000));
}};
""", "E0602", "given to a parallel item", "parallel.map")
            with self.subTest(worker_global=kind):
                self.reject(self.KINDS_HEADER + f"""
const shared: {kind} = nil;
let read = func(n: int) -> int {{
    if (shared) {{
        return n;
    }}
    return 0;
}};
let use = func {{
    let _ = parallel.map<int, int>([1], read, parallel.limits(1000, 1000));
}};
""", "E0601", "uses the binding 'shared'", "parallel.map")
        # A const global read by a spawned call follows the task rule.
        self.accept(self.KINDS_HEADER + """
const shared: SharedPointer<Fixed> = nil;
let read = func() -> bool => shared == nil;
let use = func {
    scope { spawn read(); }
};
""")
        self.reject(self.KINDS_HEADER + """
const shared: UniquePointer<Mutable> = nil;
let read = func() -> bool => shared == nil;
let use = func {
    scope { spawn read(); }
};
""", "E0601", "uses the binding 'shared'", "spawn read")

    def test_declared_kinds_keep_the_pointer_boundaries(self):
        kinds = self.SHAREABLE + ("UniquePointer<Mutable>",)
        for kind in kinds:
            # SharedPointer and WeakPointer have constructors since M2.
            if not kind.startswith(("SharedPointer", "WeakPointer")):
                with self.subTest(constructor=kind):
                    self.reject(self.KINDS_HEADER + f"""
let use = func {{
    let pointer: {kind} = new();
}};
""", "E0507", "constructor is not exported", "= new()")
            with self.subTest(destroy=kind):
                self.reject(self.KINDS_HEADER + f"""
let use = func(pointer: {kind}) {{
    destroy pointer;
}};
""", "E0514", "managed pointer cannot be destroyed", "destroy pointer")
            with self.subTest(native=kind):
                self.reject(self.KINDS_HEADER + FOREIGN + f"""
let use = func(pointer: {kind}) {{
    native().invoke(pointer);
}};
""", "E0403", "not a native value", "invoke(pointer)")
        self.reject(self.KINDS_HEADER + """
let use = func(pointer: SharedPointer<Fixed>) {
    let weak = pointer as WeakPointer<Fixed>;
};
""", "E0415", "cannot change a managed pointer", "as WeakPointer")

    SHARED_DECLARATIONS = """let optional = import:Std.Core.Optional;
let.promote _ = import:Std.Core.Result;
let.promote _ = import:Std.Core.Unit;

let Node = class (
    export let value: int64 = 0;
    export let parent: WeakPointer<Node> = nil;
    export let init = func(initial: int64) {
        value = initial;
    };
);

let alive = func(weak: WeakPointer<Node>) -> string {
    match (weak.upgrade()) {
        optional.Optional.Some(strong) => {
            last value = strong.read().value;
            strong.release();
            return "alive " + value.to_string();
        },
        optional.Optional.None => {},
    }
    return "gone";
};

let scoped = func(owner: SharedPointer<Node>) -> Result<Unit> {
    using (copy = owner.clone()) {
        console.println("scoped {}", copy.read().value);
    }
    return Result<Unit>.Ok(Unit.Value);
};
"""

    def test_shared_and_weak_pointers(self):
        self.build(program("""
        let root: SharedPointer<Node> = new(new Node(1));
        let alias = root.clone();
        console.println("{} {} {}", root.read().value, alias.read().value, root.read() == alias.read());
        let weak = root.downgrade();
        root.with_mut(func(node: Node) {
            node.value = 5;
        });
        let doubled = alias.with_read<int64>(func(node: Node) -> int64 => node.value * 2);
        console.println("{} {} {}", doubled, alive(weak), weak.is_alive());
        scoped(alias);
        root.release();
        console.println("{}", alive(weak));
        alias.close();
        alias.close();
        console.println("{} {}", alive(weak), weak.is_alive());
        let data: int64[] = [10, 20, 30];
        let numbers: SharedPointer<int64[]> = new(data);
        let items: SharedPointer<int64[list]> = new(new([4, 5]));
        console.println("{} {}", numbers.read()[1], items.read().length);
        if (args.length != 0) {
            if (args[0] == "twice") {
                alias.release();
            } else if (args[0] == "released") {
                alias.read();
            } else if (args[0] == "borrowed") {
                let other: SharedPointer<Node> = new(new Node(2));
                other.with_mut(func(node: Node) {
                    other.with_read<int64>(func(inner: Node) -> int64 => inner.value);
                });
            } else {
                let other: SharedPointer<Node> = new(new Node(3));
                let target = other.read();
                destroy target;
            }
        }
""", self.SHARED_DECLARATIONS))
        expected = "1 1 true\n10 alive 5 true\nscoped 5\nalive 5\ngone false\n20 2\n"
        self.assert_counts_agree(self.run_modes(0, expected))
        self.run_modes(134, expected, "invalid state: SharedPointer released twice", ("twice",))
        self.run_modes(134, expected, "invalid state: released SharedPointer", ("released",))
        self.run_modes(134, expected, "invalid state: SharedPointer is borrowed by with_mut", ("borrowed",))
        self.run_modes(139, expected, "pointer-owned storage is released by collection", ("destroy",))

    def test_shared_pointers_cross_tasks_with_immutable_targets(self):
        self.build(program("""
        let root: SharedPointer<Config> = new(new Config(7));
        let weak = root.downgrade();
        scope {
            last a = spawn use(root.clone(), 10);
            last b = spawn use(root.clone(), 10);
            last c = spawn probe(weak);
            console.println("{} {}", a.join() + b.join(), c.join());
        }
        console.println("{}", weak.is_alive());
        root.release();
        console.println("{}", weak.is_alive());
""", """let optional = import:Std.Core.Optional;

.Immutable
let Config = class (
    export last level: int;
    export let init = func(value: int) {
        level = value;
    };
);

let use = func(owner: SharedPointer<Config>, rounds: int) -> int {
    let total = 0;
    for (let i: int = 0; i < rounds; i++) {
        last copy = owner.clone();
        total += copy.read().level;
        copy.release();
    }
    owner.release();
    return total;
};

let probe = func(weak: WeakPointer<Config>) -> bool {
    match (weak.upgrade()) {
        optional.Optional.Some(strong) => {
            last seen = strong.read().level == 7;
            strong.release();
            return seen;
        },
        optional.Optional.None => {},
    }
    return false;
};
"""))
        for quantum in ("1", "1024"):
            self.assert_counts_agree(self.run_modes(0, "140 true\ntrue\nfalse\n", limits=("--task-quantum", quantum)))

    def test_shared_pointer_images_and_artifacts(self):
        self.build(program("""
        let owner: SharedPointer<Node> = new(new Node(4));
        console.println("{}", owner.read().value);
""", self.SHARED_DECLARATIONS))
        image = self.image.read_bytes()
        self.assertEqual(image[:4], b"NEUR")
        self.assertEqual(int.from_bytes(image[6:8], "little"), 12)
        ir = self.path / "shared.nir"
        rebuilt = self.path / "rebuilt.nvr"
        assembly = self.path / "shared.nvra"
        assembled = self.path / "assembled.nvr"
        for arguments in (("emit-ir", self.source, "-o", ir), ("build", ir, "-o", rebuilt),
                          ("disassemble", self.image, "-o", assembly), ("assemble", assembly, "-o", assembled)):
            result = self.command(*arguments)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(ir.read_bytes()[:8], b"NEIR\x01\x00\x12\x00")
        self.assertTrue(rebuilt.read_bytes() == image, "building from NeuraIR changed the image")
        self.assertTrue(assembled.read_bytes() == image, "assembling the disassembly changed the image")
        self.assertIn("retain-object-storage", assembly.read_text(encoding="utf-8"))
        older = bytearray(image)
        older[6] = 11
        (self.path / "older.nvr").write_bytes(bytes(older))
        result = self.command("run", "--runtime=off", "--gpu=off", self.path / "older.nvr")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("the image uses NVR 1.12 features but declares 1.11", result.stderr)

    def test_shared_pointer_targets_must_be_fresh(self):
        declarations = POINTER + """
let Node = class (
    export let value: int64 = 0;
);
let Registered = class (
    export let init = func {
        keep(instance);
    };
);
let kept: Registered = nil;
let keep = func(item: Registered) {
    kept = item;
};
"""
        for body in (
            "let take = func -> SharedPointer<Node> => new(new Node());",
            "let take = func -> SharedPointer<Node> {\n    let node = new Node();\n    return new(node);\n};",
            "let take = func -> SharedPointer<int[]> {\n    let data: int[] = [1, 2];\n    return new(data);\n};",
            "let take = func -> SharedPointer<int[list]> => new(new([1, 2]));",
        ):
            with self.subTest(accepted=body):
                self.accept(declarations + body + "\n")
        for name, body, use, note in (
            ("parameter", "let take = func(node: Node) -> SharedPointer<Node> => new(node);", "new(node)", None),
            ("used twice", "let take = func -> SharedPointer<Node> {\n    let node = new Node();\n    node.value = 2;\n"
             "    return new(node);\n};", "return new(node)", "'node' is used again elsewhere"),
            ("captured", "let take = func -> SharedPointer<Node> {\n    let node = new Node();\n"
             "    let read = func() -> int64 => node.value;\n    return new(node);\n};", "return new(node)",
             "'node' is used again elsewhere"),
            ("constructor keeps it", "let take = func -> SharedPointer<Registered> => new(new Registered());",
             "new(new Registered())", "the constructor of 'Registered' keeps its receiver"),
        ):
            with self.subTest(rejected=name):
                diagnostic = self.reject(declarations + body + "\n", "E0465",
                                         "a SharedPointer takes ownership of a value that nothing else refers to", use)
                if note:
                    self.assertTrue(any(note in item for item in diagnostic["notes"]), diagnostic)

    def test_shared_pointer_specification_examples(self):
        source = (pathlib.Path(__file__).with_name("spec-shared.nra")).read_text(encoding="utf-8")
        self.build(source)
        self.run_modes(0, "1 1\n3\n1\n10\n")

    def test_const_pointer_stays_in_its_task(self):
        # A Pointer to the same storage can still change it, so a read view is
        # not shared with another task, whatever its value type.
        view_note = "a ConstPointer reads storage that a Pointer in its task can still change"
        declarations = POINTER + ENTRY + '''
let channels = import:Std.Concurrent.Channel;
let parallel = import:Std.Concurrent.Parallel;
let consume = func(view: ConstPointer<int64>) -> int64 => view.read();
'''
        cases = (
            ("spawn", "given to a spawned task", '''
let reject = func(view: ConstPointer<int64>) {
    scope { spawn consume(view); }
};
''', "spawn consume"),
            ("nil", "given to a spawned task", '''
let reject = func {
    let view: ConstPointer<int64> = nil;
    scope { spawn consume(view); }
};
''', "spawn consume"),
            ("channel", "a channel's element type", '''
let reject = func {
    let [out, incoming, closer] = channels.open<ConstPointer<int64>>(1);
};
''', "channels.open"),
            ("item", "given to a parallel item", '''
let reject = func(views: ConstPointer<int64>[]) {
    let _ = parallel.map<ConstPointer<int64>, int64>(views, consume, parallel.limits(1000, 1000));
};
''', "parallel.map"),
        )
        for name, message, body, use in cases:
            with self.subTest(path=name):
                diagnostic = self.reject(declarations + body, "E0602", message, use, (view_note,))
                self.assertTrue(any(span.get("label") == "'ConstPointer<int64>' is marked #NotThreadSafe here"
                                    for span in diagnostic["spans"]), diagnostic)
        # A read view in a const global is not task-safe for a spawned call.
        self.reject(declarations + '''
let writable: Pointer<int64> = new(cell.of<int64>(3));
const view: ConstPointer<int64> = writable.as_const();
let read_view = func() -> int64 => view.read();
let reject = func {
    scope { spawn read_view(); }
};
''', "E0601", "not task-safe", "spawn read_view")

    def test_const_pointer_cannot_be_sent(self):
        view_note = "a ConstPointer reads storage that a Pointer in its task can still change"
        declarations = POINTER + "let channels = import:Std.Concurrent.Channel;\n"
        # A sender that comes from elsewhere: the value sent is checked at the
        # send, as a plain send and as a select arm.
        self.reject(declarations + '''
let reject = func(view: ConstPointer<int64>, out: channels.Sender<ConstPointer<int64>>) {
    out.send(view);
};
''', "E0602", "sent on a channel", "out.send(view)", (view_note,))
        self.reject(declarations + '''
let reject = func(view: ConstPointer<int64>, out: channels.Sender<ConstPointer<int64>>) {
    select {
        out.send(view) => {},
    }
};
''', "E0602", "sent on a channel", "out.send(view) =>", (view_note,))

    def test_mutable_pointer_and_view_move_through_join(self):
        source = pathlib.Path(__file__).with_name("join-move.nra").read_text(encoding="utf-8")
        self.build(source)
        self.run_modes(0, "6 2\n9\n")

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
        self.assertTrue(any("a ConstPointer reads storage" in note for note in diagnostic["notes"]), result.stderr)

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
        observe(readonly);
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
        # The Pointer module's own SharedPointer retains object storage
        # (NeuraIR 1.18), so a program that imports it is written as 1.18.
        self.assertEqual(ir.read_bytes()[:8], b"NEIR\x01\x00\x12\x00")
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

    def test_reference_and_string_targets(self):
        declarations = '''
let Box = class (
    export let count: int = 0;
);
'''
        self.build(program('''
        let first = new Box();
        let boxes: Pointer<Box> = new(cell.of<Box>(first));
        boxes.read().count = 5;
        let alias = boxes.clone();
        console.println("{} {}", first.count, alias.read() == first);
        alias.write(new Box());
        console.println("{} {}", boxes.read() == first, boxes.read().count);
        let words: Pointer<string> = new(cell.of<string>("tea"));
        let copy = words.read();
        words.write(copy + " and cake");
        console.println("{} | {} | {}", copy, words.read(), words.as_const().read());
''', declarations))
        self.assert_counts_agree(self.run_modes(0, "5 true\nfalse 0\ntea | tea and cake | tea and cake\n"))

    def test_empty_views_and_stale_access(self):
        self.build(program('''
        let list: int64[list] = new([1, 2, 3]);
        let empty: ListPointer<int64> = new(list, 3, 3);
        let whole: ListPointer<int64> = new(list, 0, 3);
        let none = whole.sub(1, 1);
        console.println("{} {} {} {}", empty.len(), empty.is_empty(), none.is_empty(), whole.is_empty());
        if (args.length != 0) {
            if (args[0] == "empty-read") {
                empty.value_at(0);
            } else {
                list.append(4);
                if (args[0] == "stale-read") {
                    whole.value_at(0);
                } else if (args[0] == "stale-set") {
                    whole.set(0, 9);
                } else {
                    whole.sub(0, 1);
                }
            }
        }
'''))
        expected = "0 true true false\n"
        self.assert_counts_agree(self.run_modes(0, expected))
        self.run_modes(139, expected, "source bounds failure (trap reason 2)", ("empty-read",))
        for case in ("stale-read", "stale-set", "stale-sub"):
            self.run_modes(139, expected, "index out of bounds: stale ListPointer view", (case,))

    def test_repeated_runs_are_identical(self):
        self.build(pathlib.Path(__file__).with_name("join-move.nra").read_text(encoding="utf-8"))
        runs = [self.run_modes(0, "6 2\n9\n") for _ in range(3)]
        for name in runs[0]:
            self.assertEqual(len({(run[name].stdout, run[name].stderr) for run in runs}), 1, name)

    def test_handle_consumption_ignores_collection(self):
        # Each retain of a Cell takes its own storage handle, so a collection
        # that sweeps an earlier pointer's storage cannot change the handle a
        # later allocation gets (the trap detail names it).
        self.build(program('''
        let storage = cell.of<int64>(1);
        let first = touch(storage);
        let total = churn(10);
        let second = touch(storage);
        let victim = cell.of<int64>(5);
        destroy victim;
        if (first + second + total < 0) {
            return program.ExitCode.Failure;
        }
        let unreachable = victim.value;
''', '''
let touch = func(storage: cell.Cell<int64>) -> int64 {
    let view: Pointer<int64> = new(storage);
    return view.read();
};

let churn = func(rounds: int) -> int64 {
    let total: int64 = 0;
    for (let i: int = 0; i < rounds; i++) {
        let junk = cell.of<int64>(i as int64);
        total += junk.value;
    }
    return total;
};
'''))
        details = set()
        for limit in ("40", "60", "100000"):
            for result in self.run_modes(139, "", "invalid reference handle", limits=("--max-heap-cells", limit)).values():
                details.add(re.search(r"handle \d+", result.stderr).group(0))
        self.assertEqual(len(details), 1, details)

    def test_const_pointer_check_names_the_instantiation(self):
        text = POINTER + '''
let Mutable = class (export let field: int = 0;);
let expose = func<T>(ptr: Pointer<T>) -> ConstPointer<T> => ptr.as_const();
let reject = func {
    let ptr: Pointer<Mutable> = new(cell.of<Mutable>(new Mutable()));
    expose<Mutable>(ptr);
};
'''
        self.source.write_text(text, encoding="utf-8")
        result = self.command("check", "--error-format=json", self.source)
        self.assertEqual(result.returncode, 1, result.stderr)
        diagnostic = json.loads(result.stderr.splitlines()[0])
        self.assertEqual(diagnostic["code"], "E0580", result.stderr)
        lines = text.splitlines()
        primary = next(span for span in diagnostic["spans"] if span["primary"])
        self.assertEqual(primary["line"], next(i for i, line in enumerate(lines, 1) if "ptr.as_const" in line))
        self.assertTrue(any(span.get("label") == "instantiated here" and
                            span["line"] == next(i for i, line in enumerate(lines, 1) if "expose<Mutable>" in line)
                            for span in diagnostic["spans"]), result.stderr)

    def test_pointer_images_declare_nvr_1_11(self):
        # The writer selects 1.11 for retain-cell-storage itself and for the
        # stale-view trap (writer.cpp, required_minor_version), with no task
        # and no TRNS cell layout in the image.
        for name, body in (
            ("pointer", "        let ptr: Pointer<int64> = new(cell.of<int64>(1));\n"
                        "        console.println(\"{}\", ptr.as_const().read());\n"),
            ("list view", "        let list: int64[list] = new([1, 2]);\n"
                          "        let view: ListPointer<int64> = new(list, 0, 2);\n"
                          "        console.println(\"{}\", view.value_at(1));\n"),
        ):
            with self.subTest(image=name):
                self.build(program(body))
                header = self.image.read_bytes()[:8]
                self.assertEqual(header[:4], b"NEUR")
                self.assertEqual(int.from_bytes(header[4:6], "little"), 1)
                self.assertEqual(int.from_bytes(header[6:8], "little"), 11)
        # The same program without a pointer needs an earlier minor version.
        self.build(program("        console.println(\"{}\", 1);\n"))
        self.assertLess(int.from_bytes(self.image.read_bytes()[6:8], "little"), 11)

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
let invoke_generic = func<T: class>(value: T) {
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
            ("E0602", "not transferable", '''
let apply = func(step: func() -> int) -> int => step();
let reject = func(ptr: Pointer<int>) {
    let read = new func() -> int => ptr.read();
    scope { spawn apply(read); }
};
''', "spawn apply"),
            ("E0601", "not task-safe", '''
let shared: Pointer<int> = new(cell.of<int>(1));
let read = func() -> int => shared.read();
let reject = func {
    scope { spawn read(); }
};
''', "spawn read"),
            ("E0602", "not transferable", '''
let parallel = import:Std.Concurrent.Parallel;
let read = func(view: ConstPointer<int>) -> int => view.read();
let reject = func(views: ConstPointer<int>[]) {
    let _ = parallel.map<ConstPointer<int>, int>(views, read, parallel.limits(1000, 1000));
};
''', "parallel.map"),
            ("E0580", "deeply immutable", '''
let Mutable = class (export let field: int = 0;);
let reject = func {
    let ptr: Pointer<Mutable> = new(cell.of<Mutable>(new Mutable()));
    let make = ptr.as_const;
    make();
};
''', "ptr.as_const"),
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

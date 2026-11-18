#!/usr/bin/env python3
"""Derived JSON codecs across parsing, linking, expansion and executable images."""
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv.pop(1)).resolve()
FIXTURES = pathlib.Path(__file__).resolve().parent / "codec"
BASE = """let json = import:Std.Encoding.Json;
let.promote _ = import:Std.Core.Optional;
let User = record (id: int, name: string);
let Team = record (nick: string);
"""


class Codecs(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = pathlib.Path(tmp.name)

    def command(self, *args, failure=False):
        result = subprocess.run([NEURA, *args], cwd=self.root, text=True, capture_output=True, timeout=120)
        if failure:
            self.assertNotEqual(result.returncode, 0, args)
        else:
            self.assertEqual(result.returncode, 0, f"{args}\n{result.stderr}")
        return result

    def test_shapes_and_mappings(self):
        path = self.root / "form.nra"
        path.write_text(BASE + "let derive = 1; let after = derive + 1;\n"
                        "let A = derive json.Codec<User>; let B = derive json.Codec<User> {};\n")
        self.command("check", path)
        # (source, code, the text the primary span begins at, message).
        cases = [
            ("let J = derive json.Codec<User, Team>;", "E0417", "json.Codec<",
             "Json.Codec expects one type argument"),
            ("let J = derive json.Codec;", "E0202", ";", "expected '<' and the type to derive"),
            ("let J = derive json.JsonValue<User>;", "E0579", "json.JsonValue",
             "'json.JsonValue' is not a generator; this release derives 'Std.Encoding.Json.Codec<T>'"),
            ("let App = module (export let Codec = interface<T> ();); let J = derive App.Codec<User>;", "E0579",
             "App.Codec<", "'App.Codec' is not a generator"),
            ("let use = func() { let J = derive json.Codec<User>; };", "E0579", "derive",
             "a derivation is a member of a file or module"),
            ("let f = func(x: int) => x; let value = f(derive json.Codec<User>);", "E0202", "derive",
             "a derivation is a declaration, not an expression"),
            ('let J = derive json.Codec<User> { field User.name: "a" };', "E0202", "}",
             "expected ';' after the mapping"),
            ('let J = derive json.Codec<User> { field User.name: "a" + "b"; };', "E0202", "+",
             "expected ';' after the mapping"),
            ('let J = derive json.Codec<User> { wrong User.name: "a"; };', "E0202", "wrong",
             "expected 'field' or 'variant' in a derivation mapping"),
            ('let J = derive json.Codec<User> { field User.name: name; };', "E0202", "name;",
             "expected a wire name as one string literal"),
            ('let J = derive json.Codec<User> { field User.nick: "n"; };', "E0318", "User.nick",
             "'User.nick' names no member reachable from the codec root"),
            ('let J = derive json.Codec<User> { field Team.nick: "n"; };', "E0318", "Team.nick",
             "'Team.nick' names no member reachable from the codec root"),
            ('let J = derive json.Codec<User> { variant User.name: "a"; };', "E0318", "User.name",
             "'User.name' names no member reachable from the codec root"),
            ('let J = derive json.Codec<User> { field User.name: ""; };', "E0319", '""',
             "a wire name must not be empty"),
            ('let J = derive json.Codec<User> { field User.name: "id"; };', "E0319", '"id"',
             "duplicate wire name 'id'"),
            ('let J = derive json.Codec<User> { field User.name: "a"; field User.name: "b"; };', "E0319", '"b"',
             "this member already has a wire mapping"),
            ("let Node = record (next: Node); let J = derive json.Codec<Node>;", "E0590", "Node>",
             "'Json.Codec' cannot encode Node.next: Node reaches itself"),
            ("let A = record (b: B); let B = record (a: A); let J = derive json.Codec<A>;", "E0590", "A>",
             "'Json.Codec' cannot encode A.b.a: A reaches itself"),
            ("let J = derive json.Codec<[int, int]>;", "E0590", "[int",
             "a JSON codec root must be a record or an enum"),
            ("let J = derive json.Codec<Optional<User>>;", "E0590", "Optional<User>",
             "'Json.Codec' cannot encode Optional<User>: Optional is supported only as the type of a record field"),
            ("let J = derive json.Codec<int>;", "E0590", "int>", "a JSON codec root must be a record or an enum; got int32"),
            ("let Plain = class (); let J = derive json.Codec<Plain>;", "E0590", "Plain>",
             "a JSON codec root must be a record or an enum; got Plain"),
            ("let Bad = record (o: Optional<Optional<int>>); let J = derive json.Codec<Bad>;", "E0590", "Bad>",
             "'Json.Codec' cannot encode Bad.o: Optional is supported only as the type of a record field"),
            ("let Bad = record (t: [int, Optional<int>]); let J = derive json.Codec<Bad>;", "E0590", "Bad>",
             "'Json.Codec' cannot encode Bad.t.1: Optional is supported only as the type of a record field"),
            ("let Bad = enum (Case(Optional<int>)); let J = derive json.Codec<Bad>;", "E0590", "Bad>",
             "'Json.Codec' cannot encode Bad.Case.0: Optional is supported only as the type of a record field"),
            ("let W = record (x: int[]); let J = derive json.Codec<W>;", "E0590", "W>",
             "'Json.Codec' cannot encode W.x: int32[] is outside the supported JSON shapes"),
        ]
        for source, code, anchor, message in cases:
            with self.subTest(source=source):
                path.write_text(BASE + source)
                stderr = self.command("check", path, failure=True).stderr
                self.assertIn(f"error[{code}]: {message}", stderr)
                self.assertIn(f"form.nra:5:{source.rindex(anchor) + 1}\n", stderr)

    @staticmethod
    def sections(image):
        # The NVR directory (docs/runtime/NVR.md, File header): a u32 section
        # count at byte 12, then one 24-byte record per section from byte 24.
        data = image.read_bytes()
        count = struct.unpack_from("<I", data, 12)[0]
        return {data[24 + 24 * i: 28 + 24 * i].decode() for i in range(count)}

    def test_wire_and_artifacts(self):
        for name in ("record", "wire", "bounds"):
            with self.subTest(name=name):
                source = FIXTURES / (name + ".nra")
                expected = (FIXTURES / (name + ".expected")).read_text().splitlines()
                self.command("emit-ir", source, "-o", "program.nir")
                for strip in ([], ["--no-source-map", "--no-lift-hints"]):
                    self.command("build", *strip, source, "-o", "source.nvr")
                    self.command("build", *strip, "program.nir", "-o", "import.nvr")
                    self.assertEqual((self.root / "source.nvr").read_bytes(), (self.root / "import.nvr").read_bytes())
                    # Case 135: the stripped image keeps no NAME or SMAP, and
                    # the codec's output and errors are unchanged.
                    names = {"NAME", "SMAP"} & self.sections(self.root / "import.nvr")
                    self.assertEqual(names, set() if strip else {"NAME", "SMAP"})
                    result = self.command("run", "--tier=0", "import.nvr").stdout.splitlines()
                    if name == "wire":
                        self.assertEqual(result[-2], result[-1])
                        self.assertTrue(result[-1].startswith("parse "))
                        result = result[:-2]
                    # Error text begins with the path. The remaining human explanation can change.
                    for i, line in enumerate(expected):
                        if line.startswith("json.codec.") and i < len(result):
                            self.assertRegex(result[i], re.escape(line) + r"(?:$|[:\s])")
                            result[i] = line
                    self.assertEqual(expected, result)

    def test_expansion_matches_compiled_work(self):
        source = (FIXTURES / "record.nra").read_text()
        path = self.root / "expand.nra"
        path.write_text(source)
        display = self.command("check", "--show-derived=UserJson", path).stdout
        self.assertIn("let UserJson = module", display)
        self.assertNotIn("let TeamJson", display)
        directive = 'let UserJson = derive json.Codec<User> { field User.name: "display_name"; };'
        measurements = []
        for program in (source, source.replace(directive, display)):
            path.write_text(program)
            self.command("build", path, "-o", "expand.nvr")
            run = self.command("run", "--stats", "--tier=0", "expand.nvr")
            counts = re.search(r"steps (\d+), peak heap cells (\d+), host requests (\d+)", run.stderr)
            self.assertIsNotNone(counts, run.stderr)
            measurements.append((run.stdout, counts.groups()))
        self.assertEqual(*measurements)

    def test_generated_names_are_hygienic(self):
        path = self.root / "hygiene.nra"
        path.write_text("""let json = import:Std.Encoding.Json;
let CodecResultModule = record (CodecJson: string, CodecType0: int);
let Name = enum (CodecJson, With { CodecSupport: CodecResultModule });
let Json = derive json.Codec<Name> { field Name.With.CodecSupport: "space.key"; };
""")
        expanded = self.command("check", "--show-derived=Json", path).stdout
        source = path.read_text()
        path.write_text(source[:source.index("let Json = derive")] + expanded)
        self.command("check", path)

    def test_expansion_imports_a_transitive_schema(self):
        inner = self.root / 'detail.nra'
        inner.write_text('export let Detail = module (export let Key = record (value: int););')
        model = self.root / 'model.nra'
        model.write_text('let details = import:Detail; export let Model = module ('
                         'export let Entry = record (key: details.Key););')
        app = self.root / 'consumer.nra'
        self.command('emit-ir', model, inner, '-o', 'model.nir')
        for route, files in (('Model', [model, inner]), ('"model.nir".Model', [])):
            prefix = f'let model = import:{route};\nlet json = import:Std.Encoding.Json;\n'
            app.write_text(prefix + 'let Codec = derive json.Codec<model.Entry>;')
            expansion = self.command('check', '--show-derived=Codec', app, *files).stdout
            app.write_text(prefix + expansion)
            self.command('check', app, *files)

    def test_import_exported_derived_module(self):
        lib = self.root / "model.nra"
        lib.write_text("""let json = import:Std.Encoding.Json;
export let Model = module (
    export let Key = record (id: int);
    export let KeyJson = derive json.Codec<Key> { field Key.id: "key"; };
);
""")
        self.command("emit-ir", lib, "-o", "model.nir")
        app = self.root / "consumer.nra"
        program = """let m = import:{module};
let console = import:Std.Io.Console;
let program = import:Std.Program;
let.promote _ = import:Std.Core.Result;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        match (m.KeyJson.encode(new m.Key(8))) {{
            Result.Ok(text) => {{ console.print_line(text); }},
            Result.Err(_) => {{ return program.ExitCode.Failure; }},
        }}
        return program.ExitCode.Ok;
    }};
);
"""
        for module, sources in (("Model", [lib]), ('"model.nir".Model', [])):
            app.write_text(program.format(module=module))
            self.command("build", "--no-source-map", "--no-lift-hints", app, *sources, "-o", "image.nvr")
            self.assertEqual(self.command("run", "image.nvr").stdout, '{"key":8}\n')

    def test_derive_from_imported_schema(self):
        lib = self.root / "model.nra"
        lib.write_text("export let Model = module (export let Flag = enum (Named { value: int }););")
        app = self.root / "consumer.nra"
        source = """let m = import:{module};
let j = import:Std.Encoding.Json;
let.promote _ = import:Std.Core.Result;
let console = import:Std.Io.Console;
let program = import:Std.Program;
let C = derive j.Codec<m.Flag> {{ field m.Flag.Named.value: "v"; }};
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        match (C.encode(m.Flag.Named {{ value: 8 }})) {{
            Result.Ok(text) => {{ console.print_line(text); }},
            Result.Err(_) => {{ return program.ExitCode.Failure; }},
        }}
        return program.ExitCode.Ok;
    }};
);
"""
        self.command("emit-ir", lib, "-o", "model.nir")
        for module, sources in (('Model', [lib]), ('"model.nir".Model', [])):
            app.write_text(source.format(module=module))
            self.command("build", app, *sources, "-o", "consumer.nvr")
            self.assertEqual(self.command("run", "consumer.nvr").stdout, '{"variant":"Named","payload":{"v":8}}\n')


if __name__ == "__main__":
    unittest.main()

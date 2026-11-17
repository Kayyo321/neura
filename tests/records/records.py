#!/usr/bin/env python3
"""Records, copies, properties and derivations across the NeuraIR boundary
(docs/design/Records030.md).

A record compiled to a `.nir` module is imported by a program that constructs,
copies and compares it, with the same output as the program built from source;
the record's attributes survive the round trip.

Usage: records.py <neura>
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv[1]).resolve()

LIBRARY = '''
export let Shapes = module (
    export let Point = record (x: int, y: int);
    export let Segment = record (start: Point, end: Point, label: string);
);
'''

APP = '''
let console = import:Std.Io.Console;
let program = import:Std.Program;
let shapes = import:{source};

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let a = new shapes.Point(1, 2);
        let segment = new shapes.Segment(a, new shapes.Point(3, 4), "s");
        let same = new shapes.Segment(new shapes.Point(1, 2), new shapes.Point(3, 4), "s");
        console.print_line(segment.to_string());
        console.print_line(segment.hash_code().to_string());
        console.print_line((segment === same).to_string() + " " + (segment == same).to_string());
        let moved = new with(segment) {{ label: "t", start: new shapes.Point(0, 0) }};
        console.print_line(moved.to_string() + " " + (moved.end == segment.end).to_string());
        return program.ExitCode.Ok;
    }};
);
'''

EXPECTED = ('Shapes.Segment(start: Shapes.Point(x: 1, y: 2), end: Shapes.Point(x: 3, y: 4), label: s)\n'
            '{hash}\ntrue false\n'
            'Shapes.Segment(start: Shapes.Point(x: 0, y: 0), end: Shapes.Point(x: 3, y: 4), label: t) true\n')


def run(*arguments, cwd=None):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=120, cwd=cwd,
                          env={'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin'})


def segment_hash():
    def combine(values):
        result = 1
        for value in values:
            result = (31 * result + value) % 2 ** 32
        return result
    text = 0
    for byte in b's':
        text = (31 * text + byte) % 2 ** 32
    return combine([combine([1, 2]), combine([3, 4]), text])


class RecordBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.directory = pathlib.Path(self.scratch.name)
        (self.directory / 'shapes.nra').write_text(LIBRARY)
        result = run('emit-ir', 'shapes.nra', '-o', 'shapes.nir', cwd=self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)

    def tearDown(self):
        self.scratch.cleanup()

    def build_and_run(self, *sources):
        result = run('build', *sources, '-o', 'app.nvr', cwd=self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = run('run', 'app.nvr', cwd=self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_a_record_from_a_nir_module_behaves_as_from_source(self):
        expected = EXPECTED.format(hash=segment_hash())
        (self.directory / 'app.nra').write_text(APP.format(source='"shapes.nir".Shapes'))
        self.assertEqual(self.build_and_run('app.nra'), expected)
        (self.directory / 'app.nra').write_text(APP.format(source='Shapes'))
        self.assertEqual(self.build_and_run('app.nra', 'shapes.nra'), expected)

    def test_named_payload_schema_round_trip_and_rejections(self):
        library = self.directory / 'palette.nra'
        library.write_text("""let Palette = module (
            export let Color = enum (None, Rgb { red: int, blue: int });
        );""")
        module = self.directory / 'palette.nir'
        result = run('emit-ir', library, '-o', module, cwd=self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        data = module.read_bytes()
        self.assertEqual(data[6:8], bytes([13, 0]))
        marker = data.rindex(b'PNAM')
        head, table = data[:marker], data[marker:]
        entry = table[8:]
        self.assertEqual(struct.unpack_from('<I', table, 4)[0], 1)
        legacy = self.directory / 'legacy.nir'
        legacy.write_bytes(head)
        self.assertEqual(run('emit-ir', legacy, cwd=self.directory).returncode, 0)

        app = self.directory / 'app.nra'
        app_text = """let palette = import:{source};
let console = import:Std.Io.Console;
let program = import:Std.Program;
let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let color = palette.Color.Rgb {{ blue: 4, red: 3 }};
        let red = match (color) {{ palette.Color.None => 0, palette.Color.Rgb {{ blue: _, red: value }} => value, }};
        console.print_line(color.to_string());
        return red == 3 ? program.ExitCode.Ok : program.ExitCode.Failure;
    }};
);"""
        app.write_text(app_text.format(source='"palette.nir".Palette'))
        expected = 'Rgb{red: 3, blue: 4}\n'
        self.assertEqual(self.build_and_run(app), expected)
        app.write_text(app_text.format(source='Palette'))
        self.assertEqual(self.build_and_run(app, library), expected)
        app.write_text(app_text.format(source='"legacy.nir".Palette'))
        unavailable = run('check', app, cwd=self.directory)
        self.assertEqual(unavailable.returncode, 1)
        self.assertIn('rebuild its .nir', unavailable.stderr)

        u32 = lambda x: struct.pack('<I', x)
        bad = self.directory / 'bad.nir'
        cases = [
            (head[:6] + bytes([12, 0]) + head[8:] + table, 'require NeuraIR format 1.13'),
            (head + table[:-1], 'truncated'),
            (head + b'PNAM' + u32(0), 'invalid named variant count'),
            (head + b'PNAM' + u32(0xffffffff), 'truncated'),
            (head + b'PNAM' + u32(2) + entry * 2, 'invalid or repeated named variant'),
            (head + table[:8] + u32(0) + entry[4:], 'invalid or repeated named variant'),
            (head + table[:12] + u32(1) + entry[8:], 'one name per variant payload'),
            (head + table.replace(b'blue', b'nil!'), 'invalid or repeated named payload field'),
            (head + table.replace(b'blue', b'else'), 'invalid or repeated named payload field'),
            (head + table[:-8] + u32(3) + b'red', 'invalid or repeated named payload field'),
            (head + table + b'x', 'unread bytes'),
        ]
        for raw, message in cases:
            with self.subTest(message=message):
                bad.write_bytes(raw)
                result = run('emit-ir', bad, cwd=self.directory)
                self.assertEqual(result.returncode, 1)
                self.assertIn(message, result.stderr)

    def test_exported_wrapper_retains_its_trusted_generic_payload(self):
        library = self.directory / 'envelope.nra'
        library.write_text("""let errors = import:Std.Core.Error;
export let Envelope = module (
    export let Box = enum<T> (Item(T));
    export let sample = func() -> Box<errors.Error> =>
        Box<errors.Error>.Item(errors.Error.Io { kind: errors.IoErrorKind.NotFound, path: "p", message: "m" });
);""")
        module = self.directory / 'envelope.nir'
        result = run('emit-ir', library, '-o', module, cwd=self.directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(module.read_bytes()[6:8], bytes([13, 0]))
        app = self.directory / 'wrapper-app.nra'
        app.write_text("""let wrap = import:"envelope.nir".Envelope;
let console = import:Std.Io.Console;
let program = import:Std.Program;
let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(wrap.sample().to_string());
        return program.ExitCode.Ok;
    };
);""")
        self.assertEqual(self.build_and_run(app), 'Item(Io{kind: NotFound, message: m, path: p})\n')

    def test_the_attributes_survive_the_round_trip(self):
        listing = run('emit-ir', 'shapes.nir', cwd=self.directory)
        self.assertEqual(listing.returncode, 0, listing.stderr)
        point = next(line for line in listing.stdout.splitlines() if 'class "Point"' in line)
        self.assertIn('@class.record(', point)
        self.assertIn('"Final"', point)
        self.assertIn('"Immutable"', point)
        generated = [line for line in listing.stdout.splitlines() if '@generated(["record"' in line]
        # init, equals, hash_code and to_string of each of the two records.
        self.assertEqual(len(generated), 8)


PROPERTIES = '''
export let Library = module (
    export let Named = interface (
        let name: string;
            | get;
    );
    export let Plain = class implements Named (
        export let name: string = "plain";
    );
    export let Unrelated = class (
        export let name: string = "unrelated";
    );
);
'''

PROPERTIES_APP = '''
let console = import:Std.Io.Console;
let program = import:Std.Program;
let library = import:{source};

let Local = class implements library.Named (
    export let name: string = "local";
);

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let items: library.Named[] = [new library.Plain(), new Local()];
        for (let item in items) {{
            console.print_line(item.name);
        }}
        console.print_line(new library.Unrelated().name);
        return program.ExitCode.Ok;
    }};
);
'''


class PropertyBoundaryTests(unittest.TestCase):
    def test_requirements_and_adapters_across_the_boundary(self):
        with tempfile.TemporaryDirectory() as scratch:
            directory = pathlib.Path(scratch)
            (directory / 'library.nra').write_text(PROPERTIES)
            result = run('emit-ir', 'library.nra', '-o', 'library.nir', cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            listing = run('emit-ir', 'library.nir', cwd=directory).stdout.splitlines()
            requirement = [line for line in listing if 'function "$get_name"' in line and 'property.requirement' in line]
            self.assertEqual(len(requirement), 1, listing)
            self.assertIn('@property.requirement(["name", 0u])', requirement[0])
            # An adapter only in the class that implements the interface.
            adapters = [line for line in listing if '@generated(["property.adapter"' in line]
            self.assertEqual(len(adapters), 1)
            self.assertIn('function "$get_name"', adapters[0])
            self.assertIn('@accessor.of(', adapters[0])
            self.assertEqual(sum('function "$get_name"' in line for line in listing), 2)
            outputs = []
            for source, files in (('"library.nir".Library', []), ('Library', ['library.nra'])):
                (directory / 'app.nra').write_text(PROPERTIES_APP.format(source=source))
                result = run('build', 'app.nra', *files, '-o', 'app.nvr', cwd=directory)
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append(run('run', 'app.nvr', cwd=directory).stdout)
                # The linked program keeps the attributes.
                result = run('emit-ir', 'app.nra', *files, '-o', 'linked.nir', cwd=directory)
                self.assertEqual(result.returncode, 0, result.stderr)
                linked = run('emit-ir', 'linked.nir', cwd=directory).stdout
                self.assertEqual(linked.count('@generated(["property.adapter"'), 2)
                self.assertEqual(linked.count('@property.requirement('), 1)
            self.assertEqual(outputs[0], 'plain\nlocal\nunrelated\n')
            self.assertEqual(outputs[0], outputs[1])


VIRTUAL = '''
export let Shapes = module (
    export let Shape = class (
        export let label: string = "shape";
            | get.virtual: label;
    );
    export let Circle = class : Shape (
        export let.override label: string;
            | get.override: "circle of " + label;
    );
);
'''

VIRTUAL_APP = '''
let console = import:Std.Io.Console;
let program = import:Std.Program;
let shapes = import:{source};

let Ring = class : shapes.Circle (
    export let.override label: string;
        | get.override: "ring, " + label;
);

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let all: shapes.Shape[] = [new shapes.Shape(), new shapes.Circle(), new Ring()];
        for (let shape in all) {{
            console.print_line(shape.label);
        }}
        return program.ExitCode.Ok;
    }};
);
'''


class VirtualAccessorTests(unittest.TestCase):
    def test_overrides_add_no_field_and_cross_the_boundary(self):
        with tempfile.TemporaryDirectory() as scratch:
            directory = pathlib.Path(scratch)
            (directory / 'shapes.nra').write_text(VIRTUAL)
            result = run('emit-ir', 'shapes.nra', '-o', 'shapes.nir', cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            listing = run('emit-ir', 'shapes.nir', cwd=directory).stdout.splitlines()
            classes = {line.split('"')[1]: line.split()[0] for line in listing if ' class "' in line}
            fields = [line for line in listing if ' field "' in line]
            # Edge case 89: one field, in Shape; Circle declares none.
            self.assertEqual(len(fields), 1, fields)
            self.assertIn('owner=' + classes['Shape'], fields[0])
            slot = next(line for line in listing if 'function "$get_label"' in line and 'owner=' + classes['Shape'] in line)
            self.assertIn('@accessor.virtual(true)', slot)
            override = next(line for line in listing
                            if 'function "$get_label"' in line and 'owner=' + classes['Circle'] in line)
            self.assertIn('overrides=' + slot.split()[0], override)
            self.assertIn('@accessor.of(', override)
            # Each slot and override has its twin, which only its class declares.
            self.assertTrue(any('function "$get_label$1"' in line for line in listing))
            self.assertTrue(any('function "$get_label$2"' in line for line in listing))
            outputs = []
            for source, files in (('"shapes.nir".Shapes', []), ('Shapes', ['shapes.nra'])):
                (directory / 'app.nra').write_text(VIRTUAL_APP.format(source=source))
                result = run('build', 'app.nra', *files, '-o', 'app.nvr', cwd=directory)
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append(run('run', 'app.nvr', cwd=directory).stdout)
            self.assertEqual(outputs[0], 'shape\ncircle of shape\nring, circle of shape\n')
            self.assertEqual(outputs[0], outputs[1])


STATIC = '''
export let Numbers = module (
    export let Additive = interface<T> (
        static let zero = func() -> T;
        static let add = func(left: T, right: T) -> T;
    );
    export let sum = func<T: Additive<T>>(items: T[]) -> T {
        let total = T.zero();
        for (let item in items) {
            total = T.add(total, item);
        }
        return total;
    };
);
'''

STATIC_APP = '''
let console = import:Std.Io.Console;
let program = import:Std.Program;
let numbers = import:{source};

let Money = class implements numbers.Additive<Money> (
    export last cents: int64;
    export let init = func(cents: int64) => set;
    export static let zero = func() -> Money => new Money(0);
    export static let add = func(left: Money, right: Money) -> Money => new Money(left.cents + right.cents);
);

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let items: Money[] = [new Money(1), new Money(2), new Money(39)];
        console.print_line(numbers.sum<Money>(items).cents.to_string());
        return program.ExitCode.Ok;
    }};
);
'''


class StaticRequirementTests(unittest.TestCase):
    def test_a_generic_function_from_a_nir_module_specializes_for_an_importing_class(self):
        # Edge case 110 with a class of the importing program: the same image
        # from source and through .nir, and no conformance call is left after
        # specialization.
        with tempfile.TemporaryDirectory() as scratch:
            directory = pathlib.Path(scratch)
            (directory / 'numbers.nra').write_text(STATIC)
            result = run('emit-ir', 'numbers.nra', '-o', 'numbers.nir', cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((directory / 'numbers.nir').read_bytes()[4:8], bytes([1, 0, 13, 0]))
            images = []
            for source, files in (('"numbers.nir".Numbers', []), ('Numbers', ['numbers.nra'])):
                (directory / 'app.nra').write_text(STATIC_APP.format(source=source))
                result = run('build', '--no-source-map', 'app.nra', *files, '-o', 'app.nvr', cwd=directory)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(run('run', 'app.nvr', cwd=directory).stdout, '42\n')
                images.append((directory / 'app.nvr').read_bytes())
                listing = run('emit-ir', 'app.nra', *files, cwd=directory)
                self.assertEqual(listing.returncode, 0, listing.stderr)
                self.assertNotIn('conformance_call', listing.stdout)
                self.assertIn('@static.implements(', listing.stdout)
            self.assertEqual(images[0], images[1])
            # The unspecialized program keeps the operation.
            listing = run('emit-ir', '--no-optimize', 'numbers.nir', cwd=directory)
            self.assertIn('conformance_call', listing.stdout)

    def test_a_nir_library_bounded_by_arithmetic_specializes_for_int32(self):
        # Edge case 110 with a primitive: the library is bounded by
        # Std.Core.Arithmetic, the importing program imports neither it nor a
        # companion, and lowering loads both for the bound; source and .nir
        # build the same image.
        library = '''
let.promote _ = import:Std.Core.Arithmetic;
export let Totals = module (
    export let sum = func<T: Additive<T>>(items: T[]) -> T {
        let total = T.zero();
        for (let item in items) {
            total = T.add(total, item);
        }
        return total;
    };
);
'''
        app = '''
let console = import:Std.Io.Console;
let program = import:Std.Program;
let totals = import:{source};

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let items: int32[] = [1, 2, 39];
        console.print_line(totals.sum<int32>(items).to_string());
        return program.ExitCode.Ok;
    }};
);
'''
        with tempfile.TemporaryDirectory() as scratch:
            directory = pathlib.Path(scratch)
            (directory / 'totals.nra').write_text(library)
            self.assertEqual(run('emit-ir', 'totals.nra', '-o', 'totals.nir', cwd=directory).returncode, 0)
            images = []
            for source, files in (('"totals.nir".Totals', []), ('Totals', ['totals.nra'])):
                (directory / 'app.nra').write_text(app.format(source=source))
                result = run('build', '--no-source-map', 'app.nra', *files, '-o', 'app.nvr', cwd=directory)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(run('run', 'app.nvr', cwd=directory).stdout, '42\n')
                images.append((directory / 'app.nvr').read_bytes())
            self.assertEqual(images[0], images[1])

    def test_a_reader_rejects_a_newer_minor_version(self):
        # The operation is written under its minor version, which an earlier
        # reader rejects; this reader rejects the next one in the same way.
        with tempfile.TemporaryDirectory() as scratch:
            directory = pathlib.Path(scratch)
            (directory / 'numbers.nra').write_text(STATIC)
            self.assertEqual(run('emit-ir', 'numbers.nra', '-o', 'numbers.nir', cwd=directory).returncode, 0)
            data = bytearray((directory / 'numbers.nir').read_bytes())
            data[6] = 14
            (directory / 'newer.nir').write_bytes(bytes(data))
            result = run('emit-ir', 'newer.nir', cwd=directory)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('NeuraIR format 1.14 is not supported; this reader accepts 1.0 through 1.13', result.stderr)

    def test_images_from_source_and_through_nir_are_the_same(self):
        directory = pathlib.Path(__file__).resolve().parent / 'equivalence'
        with tempfile.TemporaryDirectory() as scratch:
            for name in ('values', 'copies', 'properties', 'virtual', 'static'):
                with self.subTest(program=name):
                    source = directory / (name + '.nra')
                    module = pathlib.Path(scratch) / (name + '.nir')
                    self.assertEqual(run('emit-ir', str(source), '-o', str(module)).returncode, 0)
                    first = pathlib.Path(scratch) / (name + '.source.nvr')
                    second = pathlib.Path(scratch) / (name + '.nir.nvr')
                    self.assertEqual(run('build', str(source), '-o', str(first)).returncode, 0)
                    self.assertEqual(run('build', str(module), '-o', str(second)).returncode, 0)
                    self.assertEqual(first.read_bytes(), second.read_bytes())
                    # A repeated build reproduces the image.
                    self.assertEqual(run('build', str(source), '-o', str(second)).returncode, 0)
                    self.assertEqual(first.read_bytes(), second.read_bytes())


if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()

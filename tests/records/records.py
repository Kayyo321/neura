#!/usr/bin/env python3
"""Records, copies, properties and derivations across the NeuraIR boundary
(docs/design/Records030.md).

A record compiled to a `.nir` module is imported by a program that constructs,
copies and compares it, with the same output as the program built from source;
the record's attributes survive the round trip.

Usage: records.py <neura>
"""
import pathlib
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


if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()

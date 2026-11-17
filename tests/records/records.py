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
        return program.ExitCode.Ok;
    }};
);
'''

EXPECTED = ('Shapes.Segment(start: Shapes.Point(x: 1, y: 2), end: Shapes.Point(x: 3, y: 4), label: s)\n'
            '{hash}\ntrue false\n')


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


if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()

#!/usr/bin/env python3
"""A range literal as a for-each source is counted, not built
(docs/lang/CoreLanguage.md, Loops): a loop over three million values runs
under a 64-cell stack limit and takes fewer steps than the same range built
into a variable first, which needs a region of three million cells and
fails under the same limit. Its image repeats into one region fewer.

Usage: range-loop-stack.py <neura>
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv[1]).resolve()

PROGRAM = """let console = import:Std.Io.Console;
let program = import:Std.Program;

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let total = 0;
        SOURCE
        for (let value in RANGE) {
            total = (total + value) % 1000003;
        }
        console.print_line(total.to_string());
        return program.ExitCode.Ok;
    };
);
"""


# NEURA_TEST_TIMEOUT_SCALE comes from CMake (docs/ContinuousIntegration.md).
TIMEOUT = 120 * int(os.environ.get('NEURA_TEST_TIMEOUT_SCALE', '1'))


def run(*arguments):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=TIMEOUT,
                          env={'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin'})


def steps(stats):
    line = next(line for line in stats.splitlines() if line.startswith('stats: '))
    return int(line.split('steps ')[1].split(',')[0])


class RangeLoopStackTests(unittest.TestCase):
    def build(self, scratch, name, source, range_text):
        path = pathlib.Path(scratch) / (name + '.nra')
        path.write_text(PROGRAM.replace('SOURCE', source).replace('RANGE', range_text))
        image = pathlib.Path(scratch) / (name + '.nvr')
        result = run('build', str(path), '-o', str(image))
        self.assertEqual(result.returncode, 0, result.stderr)
        return image

    def test_a_counted_range_loop_allocates_nothing(self):
        with tempfile.TemporaryDirectory() as scratch:
            counted = self.build(scratch, 'counted', '', '0 .. 3000000')
            built = self.build(scratch, 'built', 'let items = 0 .. 3000000;', 'items')
            for tier in ([], ['--tier=0']):
                with self.subTest(tier=tier):
                    result = run('run', *tier, '--stats', '--max-stack-cells', '64', str(counted))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, '45\n')
                    # No region at all, or one far below the range's length.
                    if 'peak stack cells' in result.stderr:
                        peak = int(result.stderr.split('peak stack cells ')[1].split(',')[0])
                        self.assertLessEqual(peak, 64)
                    reference = run('run', *tier, '--stats', str(built))
                    self.assertEqual(reference.returncode, 0, reference.stderr)
                    self.assertEqual(reference.stdout, '45\n')
                    self.assertIn('peak stack cells 3000001', reference.stderr)
                    self.assertLess(steps(result.stderr), steps(reference.stderr))
                    limited = run('run', *tier, '--max-stack-cells', '64', str(built))
                    self.assertNotEqual(limited.returncode, 0)
                    self.assertIn('stack limit exceeded', limited.stderr)

    def test_the_image_builds_no_range(self):
        with tempfile.TemporaryDirectory() as scratch:
            counts = []
            for name, source, range_text in (('counted', '', '0 ..= 2999999 : 1'),
                                             ('built', 'let items = 0 ..= 2999999 : 1;', 'items')):
                image = self.build(scratch, name, source, range_text)
                listing = pathlib.Path(scratch) / (name + '.nvra')
                result = run('disassemble', str(image), '-o', str(listing))
                self.assertEqual(result.returncode, 0, result.stderr)
                counts.append(listing.read_text().count('region-repeat'))
            self.assertEqual(counts[0] + 1, counts[1])

if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()

#!/usr/bin/env python3
"""Runs the matching programs (docs/design/Matching030.md).

Each `<name>.nra` in the directory, with its standard output in
`<name>.expected`, is built and run as tests/parameters/equivalence.py runs
a program with no written-out form: from source, unoptimized, from a `.nir`
module, and without names and a source map, each at the default tier and
at tier 0.

Usage: programs.py <neura> <directory>
"""
import importlib.util
import platform
import pathlib
import re
import sys
import tempfile
import unittest

HARNESS = pathlib.Path(__file__).resolve().parent.parent / 'parameters' / 'equivalence.py'
ROOT = pathlib.Path(__file__).resolve().parents[2]


class Step9Tests(unittest.TestCase):
    def test_fuzzer_any_section_can_be_disabled(self):
        spec = importlib.util.spec_from_file_location('program_fuzz', ROOT / 'scripts/program-fuzz.py')
        fuzz = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(fuzz)
        # Quick CTest seeds include a mixed task and hook case, as well as any.
        for seed in (1, 2, 5):
            self.assertIn('any', fuzz.build_program(seed)[2].features)
            self.assertNotIn('any', fuzz.build_program(seed, any_values=False)[2].features)
        self.assertIn('class-hooks', fuzz.build_program(2)[2].features)
        self.assertIn('tasks', fuzz.build_program(2)[2].features)

    def test_wide_call_has_280_words(self):
        source = ROOT / 'tests/matching/programs/any-wide.nra'
        with tempfile.TemporaryDirectory() as scratch:
            image = pathlib.Path(scratch) / 'wide.nvr'
            listing = pathlib.Path(scratch) / 'wide.nvra'
            for flags in ([], ['--no-optimize']):
                result = equivalence.run('build', *flags, str(source), '-o', str(image))
                self.assertEqual(result.returncode, 0, result.stderr)
                result = equivalence.run('disassemble', str(image), '-o', str(listing))
                self.assertEqual(result.returncode, 0, result.stderr)
                text = listing.read_text()
                targets = set(re.findall(r'^function (\d+) parameters 280\b', text, re.MULTILINE))
                calls = re.findall(r'exit call target (\d+) arguments \[([^\]]*)\]', text)
                self.assertTrue(any(target in targets and len(words.split()) == 280 for target, words in calls))

    def test_hot_arithmetic_nan_at_native_tier(self):
        source = ROOT / 'tests/matching/programs/any-hot-nan.nra'
        with tempfile.TemporaryDirectory() as scratch:
            image = pathlib.Path(scratch) / 'hot.nvr'
            result = equivalence.run('build', str(source), '-o', str(image))
            self.assertEqual(result.returncode, 0, result.stderr)
            options = ['--tier-stats']
            if sys.platform == 'linux' and platform.machine() in ('x86_64', 'amd64'):
                options.append('--jit=on')
            result = equivalence.run('run', *options, str(image), env={
                'NEURA_TIER_EAGER': '1', 'NEURA_TIER3_VERIFY': '1',
            })
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, 'nan 1000 ok\n')
            if sys.platform == 'linux' and platform.machine() in ('x86_64', 'amd64'):
                native = re.search(r'tier: native programs (\d+).*?entered (\d+).*?steps in native code (\d+)',
                                   result.stderr)
                self.assertIsNotNone(native, result.stderr)
                self.assertTrue(all(int(count) > 0 for count in native.groups()), result.stderr)
                # The loop runs 1,000 times; require sustained native execution,
                # not merely a native startup block.
                self.assertGreaterEqual(int(native.group(3)), 1000, result.stderr)

if __name__ == '__main__':
    spec = importlib.util.spec_from_file_location('equivalence', HARNESS)
    equivalence = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(equivalence)
    suite = unittest.TestSuite([equivalence.EquivalenceTests('test_programs_without_a_written_out_form_run'),
                                unittest.defaultTestLoader.loadTestsFromTestCase(Step9Tests)])
    sys.exit(0 if unittest.TextTestRunner().run(suite).wasSuccessful() else 1)

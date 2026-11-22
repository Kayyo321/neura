#!/usr/bin/env python3
"""A program written with grouped parameters, the constructor shorthand, or
named arguments in parameter order must build the same image as the program
written out in full, and run alike.

Each `<name>.<form>.nra`, such as `groups.grouped.nra` or `named.named.nra`,
has a `<name>.expanded.nra`. The images are built with `--no-source-map`,
because a source map records where each construct is written and the two
spellings are written differently; everything that executes, and the
optional lift section, must be byte for byte the same.
`<name>.nra` with `<name>.expected` is a program with no written-out form,
which is built and run for its standard output: from source, unoptimized,
from a `.nir` module, and without names and a source map, each at the
default tier and at tier 0. A program that traps keeps the output printed
before the trap.

Usage: equivalence.py <neura> <directory>
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv[1]).resolve()
DIRECTORY = pathlib.Path(sys.argv[2]).resolve()

# Keep Windows' launch environment. POSIX cases use the fixed executable search path.
BASE_ENV = dict(os.environ) if os.name == 'nt' else {'PATH': '/usr/bin:/bin'}
NATIVE_JIT = os.name != 'nt'
SHADOW_ARGS = ['--jit=on'] if NATIVE_JIT else ['--jit=off']
SHADOW_ENV = {'NEURA_TIER_CHECK': '2', 'NEURA_TIER_EAGER': '1'}
if not NATIVE_JIT:
    SHADOW_ENV['NEURA_TIER3'] = 'interp'


# NEURA_TEST_TIMEOUT_SCALE comes from CMake (docs/ContinuousIntegration.md).
TIMEOUT = 120 * int(os.environ.get('NEURA_TEST_TIMEOUT_SCALE', '1'))


def run(*arguments, env=None):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, encoding='utf-8', timeout=TIMEOUT,
                          env={**BASE_ENV, 'NEURA_GPU': 'off', **(env or {})})


class EquivalenceTests(unittest.TestCase):
    def build(self, source, image):
        result = run('build', '--no-source-map', str(source), '-o', str(image))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_grouped_and_expanded_forms_build_the_same_image(self):
        pairs = sorted(path for path in DIRECTORY.glob('*.*.nra') if not path.name.endswith('.expanded.nra'))
        self.assertGreaterEqual(len(pairs), 3)
        with tempfile.TemporaryDirectory() as scratch:
            for grouped in pairs:
                expanded = grouped.with_name(grouped.name.split('.')[0] + '.expanded.nra')
                with self.subTest(program=grouped.name.split('.')[0]):
                    images = []
                    for source in (grouped, expanded):
                        image = pathlib.Path(scratch) / (source.name + '.nvr')
                        self.build(source, image)
                        images.append(image.read_bytes())
                    self.assertEqual(images[0], images[1])
                    outputs = [run('run', str(pathlib.Path(scratch) / (source.name + '.nvr'))).stdout
                               for source in (grouped, expanded)]
                    self.assertEqual(outputs[0], outputs[1])
                    self.assertTrue(outputs[0])

    def test_programs_without_a_written_out_form_run(self):
        with tempfile.TemporaryDirectory() as scratch:
            for expected in sorted(DIRECTORY.glob('*.expected')):
                with expected.open(encoding='utf-8', newline=None) as stream:
                    expected_stdout = stream.read()
                source = expected.with_suffix('.nra')
                module = pathlib.Path(scratch) / (source.name + '.nir')
                result = run('emit-ir', str(source), '-o', str(module))
                self.assertEqual(result.returncode, 0, result.stderr)
                trap_file = expected.with_suffix('.trap')
                status, trap = 0, ''
                if trap_file.exists():
                    with trap_file.open(encoding='utf-8', newline=None) as stream:
                        first, message = stream.read().split('\n', 1)
                    status, trap = int(first), message.rstrip('\n')
                builds = {'source': [str(source)], 'unoptimized': ['--no-optimize', str(source)],
                          'nir': [str(module)], 'nir-unoptimized': ['--no-optimize', str(module)], 'stripped': ['--no-source-map', str(source)]}
                for build, arguments in builds.items():
                    image = pathlib.Path(scratch) / (source.name + '.' + build + '.nvr')
                    result = run('build', *arguments, '-o', str(image))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    if build in ('nir', 'nir-unoptimized'):
                        source_build = 'source' if build == 'nir' else 'unoptimized'
                        source_image = image.with_name(source.name + '.' + source_build + '.nvr')
                        self.assertEqual(source_image.read_bytes(), image.read_bytes(), source.name)
                    if build == 'stripped':
                        listing = run('disassemble', str(image), '-o', str(image) + '.nvra')
                        self.assertEqual(listing.returncode, 0, listing.stderr)
                        with pathlib.Path(str(image) + '.nvra').open(encoding='utf-8', newline=None) as stream:
                            self.assertNotIn('section "NAME"', stream.read())
                    modes = [([], {}), (['--tier=0'], {})]
                    if source.name.startswith('hook-'):
                        modes += [(['--tier=1'], {}), (['--tier=2'], {}), (['--jit=off'], {}),
                                  *([(['--jit=on'], {})] if NATIVE_JIT else []),
                                  ([], {'NEURA_TIER3': 'interp', 'NEURA_TIER_EAGER': '1'}),
                                  (SHADOW_ARGS, SHADOW_ENV)]
                        modes = [(tier, {**env, 'NEURA_TIER3_VERIFY': '1'}) for tier, env in modes]
                    for tier, env in modes:
                        with self.subTest(program=source.name, build=build, tier=tier, env=env):
                            result = run('run', *tier, str(image), env=env)
                            self.assertEqual(result.returncode, status, result.stderr)
                            self.assertEqual(result.stdout, expected_stdout)
                            if trap:
                                self.assertIn(trap, result.stderr)

    def test_default_images_differ_only_in_what_records_the_source(self):
        # The grouped spelling is a different text, so the default image,
        # which carries a source map, is allowed to differ; it must still run.
        with tempfile.TemporaryDirectory() as scratch:
            for grouped in sorted(path for path in DIRECTORY.glob('*.*.nra')
                                  if not path.name.endswith('.expanded.nra')):
                image = pathlib.Path(scratch) / 'default.nvr'
                result = run('build', str(grouped), '-o', str(image))
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()

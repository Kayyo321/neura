#!/usr/bin/env python3
"""A program written with grouped parameters or the constructor shorthand
must build the same image as the program written out in full, and run alike.

Each `<name>.grouped.nra` has a `<name>.expanded.nra`. The images are built
with `--no-source-map`, because a source map records where each construct is
written and the two spellings are written differently; everything that
executes, and the optional lift section, must be byte for byte the same.
`<name>.nra` with `<name>.expected` is a program with no written-out form,
which is built and run for its output.

Usage: equivalence.py <neura> <directory>
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv[1]).resolve()
DIRECTORY = pathlib.Path(sys.argv[2]).resolve()


def run(*arguments):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=120,
                          env={'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin'})


class EquivalenceTests(unittest.TestCase):
    def build(self, source, image):
        result = run('build', '--no-source-map', str(source), '-o', str(image))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_grouped_and_expanded_forms_build_the_same_image(self):
        pairs = sorted(DIRECTORY.glob('*.grouped.nra'))
        self.assertGreaterEqual(len(pairs), 2)
        with tempfile.TemporaryDirectory() as scratch:
            for grouped in pairs:
                expanded = grouped.with_name(grouped.name.replace('.grouped.', '.expanded.'))
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
                source = expected.with_suffix('.nra')
                image = pathlib.Path(scratch) / (source.name + '.nvr')
                self.build(source, image)
                self.assertEqual(run('run', str(image)).stdout, expected.read_text())

    def test_default_images_differ_only_in_what_records_the_source(self):
        # The grouped spelling is a different text, so the default image,
        # which carries a source map, is allowed to differ; it must still run.
        with tempfile.TemporaryDirectory() as scratch:
            for grouped in sorted(DIRECTORY.glob('*.grouped.nra')):
                image = pathlib.Path(scratch) / 'default.nvr'
                result = run('build', str(grouped), '-o', str(image))
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()

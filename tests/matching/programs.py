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
import pathlib
import sys
import unittest

HARNESS = pathlib.Path(__file__).resolve().parent.parent / 'parameters' / 'equivalence.py'

if __name__ == '__main__':
    spec = importlib.util.spec_from_file_location('equivalence', HARNESS)
    equivalence = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(equivalence)
    suite = unittest.TestSuite([equivalence.EquivalenceTests('test_programs_without_a_written_out_form_run')])
    sys.exit(0 if unittest.TextTestRunner().run(suite).wasSuccessful() else 1)

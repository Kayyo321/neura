#!/usr/bin/env python3
"""NeuraIR versions of the matching milestone (docs/design/Matching030.md).

A program that uses no feature of the milestone writes the version it wrote
before. A program whose postfix `?` returns from inside a scoped value is
written as NeuraIR 1.12 (nir_any_minor), and the same file marked 1.10 is
rejected by the reader.

Usage: nir_versions.py <neura> <directory of programs>
"""
import pathlib
import subprocess
import sys
import tempfile

NEURA = pathlib.Path(sys.argv[1]).resolve()
PROGRAMS = pathlib.Path(sys.argv[2]).resolve()
ANY_MINOR = 12


def run(*arguments):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=120,
                          env={'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin'})


def emit(source, target):
    result = run('emit-ir', str(source), '-o', str(target))
    if result.returncode != 0:
        sys.exit(f'{source.name}: emit-ir failed\n{result.stderr}')
    return target.read_bytes()


def main():
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        for name, minimum, maximum in (('guards', 0, ANY_MINOR - 1), ('match-is', 0, ANY_MINOR - 1),
                                       ('optional-access', 0, ANY_MINOR - 1),
                                       ('propagation', ANY_MINOR, ANY_MINOR)):
            data = emit(PROGRAMS / (name + '.nra'), scratch / (name + '.nir'))
            minor = data[6] | (data[7] << 8)
            if not minimum <= minor <= maximum:
                failures.append(f'{name}: NeuraIR 1.{minor}, expected 1.{minimum} to 1.{maximum}')
        data = bytearray((scratch / 'propagation.nir').read_bytes())
        data[6], data[7] = 10, 0
        (scratch / 'older.nir').write_bytes(data)
        result = run('build', str(scratch / 'older.nir'), '-o', str(scratch / 'older.nvr'))
        expected = f'a return inside a scoped value requires NeuraIR format 1.{ANY_MINOR}'
        if result.returncode == 0 or expected not in result.stderr:
            failures.append(f'a 1.10 file with a scoped return: expected "{expected}", got:\n{result.stderr}')
    for failure in failures:
        print(failure)
    print(f'{len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())

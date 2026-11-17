#!/usr/bin/env python3
"""NeuraIR versions of the matching milestone (docs/design/Matching030.md).

A program that uses no feature of the milestone writes the version it wrote
before. A program that uses `any` passes the verifier before and after
optimization and executes as value words. A program whose postfix `?` returns from
inside a scoped value is written as NeuraIR 1.12 (nir_any_minor), and the same file marked 1.10 is
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
        # The any fixture includes variadics, generic results, a field and an overload.
        fixture = PROGRAMS.parent.parent / 'backend' / 'nir-versions' / 'any.nra'
        nir = scratch / 'any.nir'
        emit(fixture, nir)
        for flags in ([], ['--no-optimize']):
            images = []
            for source in (fixture, nir):
                image = scratch / (source.suffix[1:] + '.nvr')
                result = run('build', *flags, str(source), '-o', str(image))
                if result.returncode:
                    failures.append(f'any fixture {flags}: {result.stderr}')
                    continue
                data = image.read_bytes()
                if int.from_bytes(data[6:8], 'little') != 7:
                    failures.append(f'any fixture {flags}: hook-free image uses a different minor from NVR 1.7')
                images.append(data)
                for mode in ([], ['--tier=0']):
                    executed = run('run', *mode, str(image))
                    if executed.returncode or executed.stdout:
                        failures.append(f'any fixture {flags} {mode}: {executed.stdout} {executed.stderr}')
            if len(images) != 2 or images[0] != images[1]:
                failures.append(f'any fixture {flags}: source and NIR images differ')
        # The optional nil policy belongs to the root reference word. The
        # same inline limit therefore allows 16 or 12 carriers, respectively.
        for hooked, width, words in ((False, 16, 64), (False, 17, 68), (True, 12, 60), (True, 13, 65)):
            source = scratch / 'width.nra'
            clause = 'let Item = class (| set: value;);' if hooked else ''
            source.write_text('let program = import:Std.Program; ' + clause +
                              ' let take = func(x: [' + ', '.join(['any'] * width) + ']) {};'
                              ' let App = module:program.Entry (export let.override main = func(args: string[])'
                              ' -> program.ExitCode => program.ExitCode.Ok;);')
            result = run('build', str(source), '-o', str(scratch / 'width.nvr'))
            if words <= 64:
                if result.returncode:
                    failures.append(f'{words} words failed: {result.stderr}')
            elif result.returncode != 1 or 'E0449' not in result.stderr or f'{words} value words' not in result.stderr:
                failures.append(f'{words} words did not report the representation limit: {result.stderr}')
    for failure in failures:
        print(failure)
    print(f'{len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())

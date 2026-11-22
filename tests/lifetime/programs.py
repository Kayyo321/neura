#!/usr/bin/env python3
"""Runs the programs of the lifetime milestone (docs/design/Lifetime030.md)
in every build and execution mode.

Each `<name>.nra` in the directory, with its standard output in
`<name>.expected`, is built four ways: from source, unoptimized, from a
`.nir` module, and without names and a source map. Each image runs at tier
0, tier 1 and tier 2 (native code off and on), and with blocks lifted and
regions formed at their first execution (NEURA_TIER_EAGER=1) with tier 3
off, interpreted and native, with programs formed without tier 3
(NEURA_TIER_PROGRAMS=1), with every run checked against a shadow run at
tier 1 (NEURA_TIER_CHECK=2), with native tier 3 checked by its verifier
(NEURA_TIER3_VERIFY=1), and with native code off (NEURA_JIT=off).

Every image round-trips through disassemble and assemble byte for byte,
and decompiles with and without provenance. A program with a generator
shows its state layout as inferred generator state when it decompiles with
names, and not without them or from the build without names. Every run of one image must
print the expected output and give the same
exit status, trap text, steps and peak heap cells, and the builds from
source, from `.nir` and without names give the same steps and peak heap
cells as each other. A program that traps
gives its status in `<name>.status`; its output is what it printed before
the trap. A program with `<name>.deopt` must form native tier 3 programs
and deoptimize out of one in each mode that file lists, one per line, so
that the deoptimization tests do not pass vacuously (the unoptimized build
is exempt: its calls are not inlined). A program with `<name>.deopt-in`
must deoptimize, in those modes, with the innermost frame in a function
whose provenance name ends with the text in that file, such as `.$resume`.

A program may list modes in `<name>.skip`, one per line after a line that
says why; the reason is printed.

With NEURA_TEST_SANITIZERS=1 (Tier 2's sanitizer job) the images are not
decompiled, except for a program with `<name>.deopt-in`, whose check needs
the provenance names: seven of the programs take about 20 s to decompile in a
Release build, which with eight decompiles each does not fit the job's six
hours when sanitized. The decompiler runs under the sanitizers in the
decompiler-* entries, and this check runs in full everywhere else.

Then each program's `.nir` is checked: a program that uses `defer`,
`using` or a generator is written as NeuraIR 1.14 (nir_lifetime_minor),
and the same file marked as an older minor is rejected by the reader. A
program with `<name>.plain` uses none of them, and is written as an older
minor.

Usage: programs.py <neura> <directory>
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

NEURA = pathlib.Path(sys.argv[1]).resolve()
DIRECTORY = pathlib.Path(sys.argv[2]).resolve()
LIFETIME_MINOR = 14
# NeuraIR 1.15 (nir_expressions_minor) holds concat text, the string chains
# that the standard library uses for its messages since the 0.3.0
# modernization (Std.Core.Error.message and others): a program that links one
# is written as 1.15 whatever else it uses.
EXPRESSIONS_MINOR = 15
GENERATOR_STATE = 'inferred from provenance names, not from the circuit: generator state; resume index'
YIELD = re.compile(r'^\s*yield\b', re.MULTILINE)

BASE = {'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin'}
if os.name == 'nt':
    BASE.update({name: os.environ[name] for name in ('PATH', 'SystemRoot') if name in os.environ})
TIMEOUT = 300 * int(os.environ.get('NEURA_TEST_TIMEOUT_SCALE', '1'))
SANITIZERS = os.environ.get('NEURA_TEST_SANITIZERS') == '1'
# (name, environment, run options)
MODES = [
    ('tier0', {}, ['--tier=0']),
    ('tier1', {}, ['--tier=1']),
    ('tier2-jit-off', {'NEURA_TIER3': 'off'}, ['--tier=2', '--jit=off']),
    ('tier2-jit-on', {'NEURA_TIER3': 'off'}, ['--tier=2', '--jit=on']),
    ('eager-tier3-off', {'NEURA_TIER_EAGER': '1', 'NEURA_TIER3': 'off'}, ['--jit=on']),
    ('eager-tier3-interp', {'NEURA_TIER_EAGER': '1', 'NEURA_TIER3': 'interp'}, ['--jit=on']),
    ('eager-tier3-native', {'NEURA_TIER_EAGER': '1', 'NEURA_TIER3': 'native'}, ['--jit=on']),
    ('eager-programs', {'NEURA_TIER_EAGER': '1', 'NEURA_TIER_PROGRAMS': '1'}, ['--jit=on']),
    ('eager-check2', {'NEURA_TIER_EAGER': '1', 'NEURA_TIER_CHECK': '2'}, ['--jit=on']),
    ('eager-verify', {'NEURA_TIER_EAGER': '1', 'NEURA_TIER3': 'native', 'NEURA_TIER3_VERIFY': '1'}, ['--jit=on']),
    ('eager-jit-env-off', {'NEURA_TIER_EAGER': '1', 'NEURA_JIT': 'off'}, []),
]
STATS = re.compile(r'^stats: steps (\d+), peak heap cells (\d+)', re.M)
NATIVE = re.compile(r'^tier: native programs (\d+) ', re.M)
NATIVE_BY_DEFAULT = sys.platform.startswith('linux') and os.uname().machine in ('x86_64', 'amd64')
DEOPTIMIZED = re.compile(r'^tier: native deoptimized (\d+) times', re.M)
DEOPTIMIZED_AT = re.compile(r'^tier: native deoptimized \d+ times at function (\d+) block', re.M)
FUNCTION = re.compile(r'^function f(\d+)\(.*// provenance: (.+)$', re.M)
TRAP = re.compile(r'^.*trap.*$', re.M)


def run(arguments, environment=None):
    env = dict(BASE)
    env.update(environment or {})
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=TIMEOUT, env=env, encoding='utf-8')


def main():
    failures = []
    if os.name == 'nt':
        print('NOT RUN: lifetime execution modes that require --jit=on; Windows has no JIT in 0.3.0')
    if SANITIZERS:
        print('NOT RUN: decompiling programs without a .deopt-in check; under the sanitizers it does not fit '
              'the job (the decompiler-* entries run it there)')
    programs = sorted(DIRECTORY.glob('*.expected'))
    if not programs:
        sys.exit('no programs')
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        for expected_path in programs:
            source = expected_path.with_suffix('.nra')
            expected = expected_path.read_text()
            status_path = expected_path.with_suffix('.status')
            status = int(status_path.read_text()) if status_path.exists() else 0
            deopt_path = expected_path.with_suffix('.deopt')
            deopt_modes = set(deopt_path.read_text().split()) if deopt_path.exists() else set()
            # A mode that leaves NEURA_TIER3 unset has native tier 3 only where
            # it is the default, x86-64 Linux (docs/runtime/NVM.md); elsewhere
            # it forms no programs, so only the modes that name tier 3 expect one.
            if not NATIVE_BY_DEFAULT:
                deopt_modes = {mode for mode in deopt_modes if 'NEURA_TIER3' in dict(
                    (name, environment) for name, environment, _ in MODES).get(mode, {})}
            within_path = expected_path.with_suffix('.deopt-in')
            within = within_path.read_text().strip() if within_path.exists() else None
            skip_path = expected_path.with_suffix('.skip')
            skipped = set()
            if skip_path.exists():
                reason, *names = skip_path.read_text().splitlines()
                skipped = {name.strip() for name in names if name.strip()}
                print(f'{source.name}: skips {", ".join(sorted(skipped))}: {reason}')
            generator = YIELD.search(source.read_text()) is not None
            module = scratch / (source.name + '.nir')
            result = run(['emit-ir', str(source), '-o', str(module)])
            if result.returncode != 0:
                failures.append(f'{source.name}: emit-ir failed\n{result.stderr}')
                continue
            builds = {'source': [str(source)], 'unoptimized': ['--no-optimize', str(source)],
                      'nir': [str(module)], 'stripped': ['--no-source-map', str(source)]}
            counts = {}
            for build, arguments in builds.items():
                image = scratch / (source.name + '.' + build + '.nvr')
                result = run(['build', *arguments, '-o', str(image)])
                if result.returncode != 0:
                    failures.append(f'{source.name} {build}: build failed\n{result.stderr}')
                    continue
                listing = scratch / (image.name + '.nvra')
                again = scratch / (image.name + '.again.nvr')
                for command in (['disassemble', str(image), '-o', str(listing)],
                                ['assemble', str(listing), '-o', str(again)]):
                    result = run(command)
                    if result.returncode != 0:
                        failures.append(f'{source.name} {build}: {command[0]} failed\n{result.stderr}')
                if again.exists() and again.read_bytes() != image.read_bytes():
                    failures.append(f'{source.name} {build}: assemble did not give back the image')
                functions = {}
                for flags in ([], ['--no-provenance']) if within or not SANITIZERS else ():
                    result = run(['decompile', *flags, str(image)])
                    if not flags:
                        functions = {int(number): name for number, name in FUNCTION.findall(result.stdout)}
                    if result.returncode != 0 or not result.stdout:
                        failures.append(f'{source.name} {build}: decompile {flags} failed\n{result.stderr}')
                    # The generator state is inferred from names: shown with
                    # them, absent without them.
                    labelled = GENERATOR_STATE in result.stdout
                    if labelled != (generator and not flags and build != 'stripped'):
                        failures.append(f'{source.name} {build}: decompile {flags} '
                                        f'{"shows" if labelled else "does not show"} the generator state')
                observed = {}
                for mode, environment, options in MODES:
                    if mode in skipped or (os.name == 'nt' and '--jit=on' in options):
                        continue
                    result = run(['run', '--stats', '--tier-stats', *options, str(image)], environment)
                    where = f'{source.name} {build} {mode}'
                    if result.stdout != expected:
                        failures.append(f'{where}: output differs\n--- expected\n{expected}--- got\n{result.stdout}'
                                        f'--- stderr\n{result.stderr}')
                    if result.returncode != status:
                        failures.append(f'{where}: exit status {result.returncode}, expected {status}\n{result.stderr}')
                    if 'is not applied' in result.stderr:
                        failures.append(f'{where}: the shadow oracle was not applied\n{result.stderr}')
                    stats = STATS.search(result.stderr)
                    traps = sorted(set(TRAP.findall(result.stderr)) - {line for line in TRAP.findall(result.stderr)
                                                                        if line.startswith('tier:')})
                    observed[mode] = (stats.groups() if stats else None, traps)
                    if mode in deopt_modes and build != 'unoptimized':
                        native = sum(int(n) for n in NATIVE.findall(result.stderr))
                        deoptimized = sum(int(n) for n in DEOPTIMIZED.findall(result.stderr))
                        if native == 0 or deoptimized == 0:
                            failures.append(f'{where}: expected native programs and a deoptimization, found '
                                            f'{native} programs and {deoptimized} deoptimizations\n{result.stderr}')
                        # The innermost frame after the deoptimization, by
                        # its provenance name; the build without names has
                        # none to compare.
                        places = [functions.get(int(number), '?') for number in DEOPTIMIZED_AT.findall(result.stderr)]
                        if within and build != 'stripped' and not any(place.endswith(within) for place in places):
                            failures.append(f'{where}: expected a deoptimization in a function named *{within}, '
                                            f'found {places}\n{result.stderr}')
                reference = observed.get('tier0')
                counts[build] = reference[0] if reference else None
                for mode, value in observed.items():
                    if value != reference:
                        failures.append(f'{source.name} {build}: {mode} gives steps, peak cells and traps '
                                        f'{value}, tier 0 gives {reference}')
            if len({counts.get(build) for build in ('source', 'nir', 'stripped')}) != 1:
                failures.append(f'{source.name}: steps and peak cells differ between builds: {counts}')
            data = bytearray(module.read_bytes())
            minor = data[6] | (data[7] << 8)
            chained = 'concat_text' in run(['emit-ir', str(module)]).stdout
            if expected_path.with_suffix('.plain').exists():
                if minor >= LIFETIME_MINOR and not (chained and minor == EXPRESSIONS_MINOR):
                    failures.append(f'{source.name}: NeuraIR 1.{minor}, expected a minor below 1.{LIFETIME_MINOR}')
                continue
            expected_minor = EXPRESSIONS_MINOR if chained else LIFETIME_MINOR
            if minor != expected_minor:
                failures.append(f'{source.name}: NeuraIR 1.{minor}, expected 1.{expected_minor}')
            data[6], data[7] = LIFETIME_MINOR - 2, 0
            older = scratch / (source.name + '.older.nir')
            older.write_bytes(data)
            result = run(['build', str(older), '-o', str(scratch / 'older.nvr')])
            if result.returncode == 0:
                failures.append(f'{source.name}: the file marked NeuraIR 1.{LIFETIME_MINOR - 2} was accepted')
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f'{len(programs)} programs, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())

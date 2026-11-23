#!/usr/bin/env python3
"""Exercise Tier 3's checked-loop rebuild after repeated preflight failures."""

import pathlib
import re
import subprocess
import sys
import tempfile


ENVIRONMENT = {'PATH': '/usr/bin:/bin', 'NEURA_GPU': 'off'}
NATIVE = re.compile(
    r'^tier: native programs .*?, guards (?P<guards>\d+), .*?, '
    r'state traps (?P<state_traps>\d+), .*?; steps in native code (?P<steps>\d+)$', re.M)
BACKOFFS = re.compile(r'^tier: native loop preflight back-offs (\d+)$', re.M)
REBUILDS = re.compile(r'^tier 3 program (\d+) of function (\d+) \(loop preflight back-off\):$', re.M)
FAILURE_LIMIT = 3


def run(neura, arguments, environment=ENVIRONMENT):
    return subprocess.run([neura, *arguments], env=environment, capture_output=True, text=True, timeout=60)


def observe(result):
    # The stats line includes logical steps, heap and stack. A trap line
    # includes the original reason and attribution. Tier statistics differ.
    stats = [line for line in result.stderr.splitlines() if line.startswith('stats: steps ')]
    traps = [line for line in result.stderr.splitlines() if ': trap: ' in line]
    return result.returncode, result.stdout, stats, traps


def check(neura, image, argument, expected_output, expected_trap, deopt_kind):
    where = f'{image.stem} {argument}'
    arguments = ['--stats', '--tier-stats', image, '--', argument]
    reference = run(neura, ['run', '--tier=0', *arguments])
    reference_result = observe(reference)
    status, output, stats, traps = reference_result
    trapping = expected_trap is not None
    if (output != expected_output or len(stats) != 1 or bool(status) != trapping or
            len(traps) != int(trapping) or (trapping and expected_trap not in traps[0])):
        raise ValueError(f'{where}: unexpected Tier 0 result {status}\n{output}{reference.stderr}')

    result = run(neura, ['run', *arguments], dict(ENVIRONMENT, NEURA_TIER3='native'))
    if observe(result) != reference_result:
        raise ValueError(f'{where}: native result differs from Tier 0\n'
                         f'--- Tier 0\n{output}{reference.stderr}--- native\n{result.stdout}{result.stderr}')

    native = NATIVE.search(result.stderr)
    backoffs = BACKOFFS.search(result.stderr)
    upper_bound = FAILURE_LIMIT + int(trapping)
    if (native is None or int(native['steps']) == 0 or backoffs is None or int(backoffs[1]) != 1 or
            'tier: native programs on the IR interpreter:' in result.stderr or
            not FAILURE_LIMIT <= int(native[deopt_kind]) <= upper_bound):
        raise ValueError(f'{where}: expected one back-off, {FAILURE_LIMIT}–{upper_bound} {deopt_kind} '
                         f'deoptimizations and native steps\n{result.stderr}')


def check_recursive(backend, source_dir):
    selector = ['tier3_backoff', source_dir, 'recursive_backoff_reuses_refreshed_programs']
    result = run(backend, selector, dict(ENVIRONMENT, NEURA_TIER_DUMP='1'))
    if result.returncode != 0:
        raise ValueError(f'recursive preflight: backend test failed\n{result.stdout}{result.stderr[-4000:]}')
    rebuilds = REBUILDS.findall(result.stderr)
    if len({function for _, function in rebuilds}) != 1 or len(set(rebuilds)) < 2 or len(set(rebuilds)) != len(rebuilds):
        raise ValueError(f'recursive preflight: expected one build per region for the same function, got {rebuilds}')


def main():
    neura = pathlib.Path(sys.argv[1]).resolve()
    sources = pathlib.Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory() as directory:
        bounds, cast = (pathlib.Path(directory) / f'preheader-{kind}-backoff.nvr' for kind in ('bounds', 'cast'))
        for image in (bounds, cast):
            result = run(neura, ['build', sources / image.with_suffix('.nra').name, '-o', image])
            if result.returncode != 0:
                raise ValueError(f'{image.stem}: build failed\n{result.stderr}')
        check(neura, bounds, 'ordinary', '90300000\n', None, 'guards')
        check(neura, cast, 'ordinary', '100000\n', None, 'state_traps')
        check(neura, cast, 'cast', '100000\n', 'invalid class cast', 'state_traps')
    check_recursive(pathlib.Path(sys.argv[2]).resolve(), pathlib.Path(sys.argv[3]).resolve())


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        sys.exit(str(error))

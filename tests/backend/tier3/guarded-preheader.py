#!/usr/bin/env python3
"""Compare guarded heap preheaders with the original NVR continuation."""

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


CASES = {
    'write': (('write', '0', '4'), 'write 4028000000000000 3ff8000000000000 4012000000000000\n'),
    'zero': (('zero', '0', '0'), 'zero 0\n'),
    'zero-negative-bound': (('zero', '2', '-1'), 'zero 0\n'),
    'nil-body': (('zero', '0', '1'), 'nil class reference'),
    'late': (('late', '0', '4'), 'index out of bounds'),
    'high': (('high', '0', '2147483647'), 'index out of bounds'),
    'negative': (('negative', '-1', '4'), 'source bounds failure (trap reason 2)'),
    'mutate': (('mutate', '0', '4'), 'mutate 4052400000000000\n'),
    'shrink': (('shrink', '0', '4'), 'index out of bounds'),
    'grow': (('grow', '0', '4'), 'grow 4004000000000000 5\n'),
    'affine': (('affine', '0', '4'), 'affine 4030000000000000\n'),
    'affine-overflow': (('affine-overflow', '0', '4'), 'source arithmetic failure (trap reason 1)'),
    'osr': (('osr', '0', '256'), 'osr 4080000000000000\n'),
}

ENVIRONMENT = {key: value for key, value in os.environ.items()
               if not key.startswith('NEURA_') and key not in {'LD_LIBRARY_PATH', 'LD_PRELOAD'}}
ENVIRONMENT.update(LC_ALL='C.UTF-8', NEURA_GPU='off')
TIMEOUT = 120 * int(os.environ.get('NEURA_TEST_TIMEOUT_SCALE', '1'))
NATIVE_ENVIRONMENT = {'NEURA_TIER3': 'native'}
EAGER = dict(NATIVE_ENVIRONMENT, NEURA_TIER3_AFTER='1', NEURA_TIER3_VERIFY='1')
NATIVE_FLAGS = ['--tier=2', '--jit=on']
STATS_FLAGS = [*NATIVE_FLAGS, '--tier-stats']
REFERENCE = {'reference': (['--tier=0', '--jit=off'], {})}
MODES = {
    'ordinary': (NATIVE_FLAGS, NATIVE_ENVIRONMENT),
    'eager': (STATS_FLAGS, EAGER),
    'shadow': (STATS_FLAGS, dict(EAGER, NEURA_TIER_CHECK='2', NEURA_TIER_CHECK_HEAP='1')),
    'no-passes': (STATS_FLAGS, dict(NATIVE_ENVIRONMENT, NEURA_TIER3_AFTER='1', NEURA_TIER3_PASSES='0')),
}
DELAYED = {'delayed': (STATS_FLAGS, dict(EAGER, NEURA_TIER3_AFTER='7', NEURA_TIER_DUMP='1'))}
NATIVE_STEPS = re.compile(r'^tier: native programs .*; steps in native code [1-9]\d*$', re.M)
HEAP_GUARDS = re.compile(r'^pass guard\.heap_bounds [1-9]\d*$', re.M)


def run(neura, arguments, environment):
    return subprocess.run([neura, *arguments], env={**ENVIRONMENT, **environment},
                          capture_output=True, text=True, encoding='utf-8', timeout=TIMEOUT)


def observe(result):
    diagnostics = [line for line in result.stderr.splitlines()
                   if line.startswith(('stats:', 'gpu:')) or ': trap: ' in line]
    return result.returncode, result.stdout, diagnostics


def check(neura, image, case, arguments, expected):
    run_options = ['run', '--runtime=off', '--parallel-workers', '1', '--gpu=off', '--stats']
    modes = {**REFERENCE, **MODES, **(DELAYED if case == 'osr' else {})}
    reference = None
    for mode, (flags, environment) in modes.items():
        result = run(neura, [*run_options, *flags, image, '--', *arguments], environment)
        observation = observe(result)
        where = f'{case} {mode}'
        if mode == 'reference':
            status, output, diagnostics = observation
            if not any(line.startswith('stats:') for line in diagnostics):
                raise ValueError(f'{where}: no execution statistics\n{result.stderr}')
            if expected.endswith('\n'):
                matches = status == 0 and output == expected
            else:
                matches = status > 0 and any(': trap: ' in line and expected in line for line in diagnostics)
            if not matches:
                raise ValueError(f'{where}: expected {expected!r}, got {observation}')
            reference = observation
        elif observation != reference:
            raise ValueError(f'{where}: expected Tier 0 result {reference}, got {observation}\n{result.stderr}')

        if 'NEURA_TIER_CHECK=2 is not applied:' in result.stderr:
            raise ValueError(f'{where}: shadow comparison did not run\n{result.stderr}')
        if '--tier-stats' in flags and not NATIVE_STEPS.search(result.stderr):
            raise ValueError(f'{where}: no native steps\n{result.stderr}')
        if mode == 'delayed' and not HEAP_GUARDS.search(result.stderr):
            raise ValueError(f'{where}: no guarded heap bounds in the delayed-entry graph')
    return len(modes)


def main():
    neura = Path(sys.argv[1]).resolve()
    source = Path(__file__).with_suffix('.nra').resolve()
    with tempfile.TemporaryDirectory() as directory:
        image = Path(directory) / 'guarded-preheader.nvr'
        built = run(neura, ['build', source, '-o', image], {})
        if built.returncode != 0:
            raise ValueError(f'build failed\n{built.stderr}')
        count = sum(check(neura, image, name, arguments, expected) for name, (arguments, expected) in CASES.items())
    print(f'{count} guarded-preheader executions across {len(CASES)} cases matched Tier 0')


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        sys.exit(str(error))

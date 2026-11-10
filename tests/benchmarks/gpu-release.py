#!/usr/bin/env python3
"""Measure the GPU release gate for run and test on Linux."""

import argparse
import dataclasses
import hashlib
import json
import os
import re
import shlex
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


@dataclasses.dataclass
class Workload:
    group: str
    name: str
    commands: list[list[str]]
    cwd: Path
    expected_stdout: bytes | None = None


def run_command(command, cwd):
    started = time.perf_counter_ns()
    process = subprocess.run(command, cwd=cwd, env=ENVIRONMENT, capture_output=True)
    elapsed = time.perf_counter_ns() - started
    if process.returncode:
        raise RuntimeError(f'{shlex.join(command)} exited {process.returncode}\n{process.stdout!r}\n{process.stderr!r}')
    return elapsed, process.stdout, process.stderr


def benchmark_workloads(repo, executable, destination):
    images = destination / 'images'
    images.mkdir(exist_ok=True)
    for source in sorted((repo / 'tests/benchmarks').glob('*.nra')):
        image = images / f'{source.stem}.nvr'
        command = [str(executable), 'build', str(source), '-o', str(image)]
        print(f'build: {shlex.join(command)}', flush=True)
        run_command(command, destination)
        sizes = re.search(r'^// sizes: (\d+) (\d+)$', source.read_text(), re.MULTILINE)
        if not sizes:
            raise RuntimeError(f'benchmark has no sizes header: {source}')
        for size in sizes.groups():
            yield Workload('benchmark', f'{source.stem}/{size}', [['run', '--stats', str(image), '--', size]],
                           destination, b'ok\n')


def stdlib_workloads(repo, executable, destination):
    for name in ('array-deque', 'core', 'integers'):
        area = repo / 'tests/stdlib' / name
        cwd = destination / name
        cwd.mkdir(exist_ok=True)
        commands = []
        for source in sorted(area.glob('*.nra')):
            option_line = re.search(r'^// test-options: (.+)$', source.read_text(), re.MULTILINE)
            options = []
            if option_line:
                values = option_line[1].replace('@AREA_DIR@', str(area))
                values = values.replace('@NATIVE_DIR@', str(executable.parent / 'native-tests'))
                options = shlex.split(values)
            support = ['-I', str(area / 'support')] if (area / 'support').is_dir() else []
            commands.append(['test', '--stats', *options, *support, str(source)])
        if not commands:
            raise RuntimeError(f'no test sources in {area}')
        yield Workload('stdlib', name, commands, cwd)


def command_for(executable, args, mode):
    flags = ['--no-gpu'] if mode == 'off' else []
    return [str(executable), args[0], *flags, *args[1:]]


def measure_one(workload, executable, mode):
    wall_ns = 0
    gpu = []
    comparable = []
    for args in workload.commands:
        elapsed, stdout, stderr = run_command(command_for(executable, args, mode), workload.cwd)
        wall_ns += elapsed
        if workload.expected_stdout is not None and stdout != workload.expected_stdout:
            raise RuntimeError(f'{workload.name}: unexpected standard output {stdout!r}')
        if workload.group == 'stdlib':
            summary = re.search(rb'^(\d+) test\(s\), (\d+) passed, 0 failed$', stdout, re.MULTILINE)
            if not summary or summary[1] == b'0' or summary[1] != summary[2]:
                raise RuntimeError(f'{workload.name}: expected passing test summary\n{stdout!r}')
        lines = stderr.splitlines(keepends=True)
        gpu_lines = [line for line in lines if line.startswith(b'gpu: ')]
        if len(gpu_lines) != 1 or lines[-1] != gpu_lines[0]:
            raise RuntimeError(f'{workload.name}/{mode}: expected exactly one final GPU line\n{stderr!r}')
        line = gpu_lines[0].rstrip(b'\r\n').decode()
        if mode == 'off' and line != 'gpu: off':
            raise RuntimeError(f'{workload.name}: unexpected CPU GPU line {line!r}')
        if mode == 'auto' and not line.startswith('gpu: auto, '):
            raise RuntimeError(f'{workload.name}: expected default auto mode, got {line!r}')
        gpu.append(line)
        comparable.append((stdout, b''.join(lines[:-1])))
    return wall_ns, gpu, comparable


def measure(workload, executable, count):
    print(f'[{workload.group}] {workload.name} (cwd {workload.cwd})', flush=True)
    for args in workload.commands:
        for mode in ('off', 'auto'):
            print(f'  {shlex.join(command_for(executable, args, mode))}', flush=True)
    samples = {'off': [], 'auto': []}
    gpu = {'off': [], 'auto': []}
    expected = None
    for index in range(-1, count):
        order = ('off', 'auto') if index % 2 == 0 or index < 0 else ('auto', 'off')
        for mode in order:
            wall_ns, lines, comparable = measure_one(workload, executable, mode)
            if expected is not None and expected != comparable:
                raise RuntimeError(f'{workload.name}: {mode} changed standard output or non-GPU diagnostics')
            expected = comparable
            if index >= 0:
                samples[mode].append(wall_ns)
                gpu[mode].append(lines)
    medians = {mode: statistics.median(values) for mode, values in samples.items()}
    ratio = medians['auto'] / medians['off']
    bound = 1.05 if workload.group == 'benchmark' else 1.10
    auto_lines = sorted({line for batch in gpu['auto'] for line in batch})
    no_device = all(line.startswith('gpu: auto, not used (') for line in auto_lines)
    passed = ratio <= bound and (workload.group != 'benchmark' or no_device)
    print(f'  median off {medians["off"] / 1e9:.6f}s, auto {medians["auto"] / 1e9:.6f}s, '
          f'ratio {ratio:.6f}, bound {bound:.2f} ({"MET" if passed else "NOT MET"})', flush=True)
    for line in auto_lines:
        print(f'  {line}', flush=True)
    return {
        'group': workload.group, 'name': workload.name, 'cwd': str(workload.cwd),
        'commands': {mode: [command_for(executable, args, mode) for args in workload.commands] for mode in samples},
        'samples_ns': samples, 'median_ns': medians, 'ratio': ratio, 'bound': bound, 'gpu': gpu, 'met': passed,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2],
                        help='Neura checkout (defaults to this script’s checkout)')
    parser.add_argument('--neura', type=Path, help='GPU build of neura (defaults to build-gpu/neura in the checkout)')
    parser.add_argument('--output', type=Path, help='directory for temporary images and sample data')
    parser.add_argument('--group', choices=('all', 'benchmark', 'stdlib'), default='all')
    parser.add_argument('--samples', type=int, default=9)
    args = parser.parse_args()
    if args.samples < 1 or args.samples % 2 == 0:
        parser.error('--samples must be a positive odd number')
    repo = args.repo.resolve()
    executable = (args.neura or repo / 'build-gpu/neura').resolve()
    destination = (args.output or Path(tempfile.mkdtemp(prefix='neura-gpu-release-'))).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    results_path = destination / f'{args.group}-results.json'
    print(f'host {os.uname().sysname} {os.uname().machine}; CPUs {sorted(os.sched_getaffinity(0))}; '
          f'neura SHA-256 {hashlib.sha256(executable.read_bytes()).hexdigest()}', flush=True)
    results = []
    for group, workloads in [('benchmark', benchmark_workloads), ('stdlib', stdlib_workloads)]:
        if args.group not in ('all', group):
            continue
        for workload in workloads(repo, executable, destination):
            results.append(measure(workload, executable, args.samples))
            results_path.write_text(json.dumps(results, indent=2) + '\n')
    failures = sum(not entry['met'] for entry in results)
    print(f'{len(results) - failures}/{len(results)} workloads met their gate; samples in {results_path}', flush=True)
    return int(failures != 0)


ENVIRONMENT = {key: value for key, value in os.environ.items() if key != 'NEURA_GPU'}
if __name__ == '__main__':
    raise SystemExit(main())

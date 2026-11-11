#!/usr/bin/env python3
"""Measure the concurrency workloads of docs/lang/Concurrency.md section 16, phase 4.

Reports throughput, tail latency, peak memory, and cancellation latency for the
programs under tests/benchmarks/concurrency, each beside its baseline, with the
machine they ran on. This is a measurement, not a test: it checks only that
every configuration of a workload prints the same answer.
"""

import argparse
import json
import os
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

STEPS = '100000000000'


def peak_resident_kib(pid):
    """The high-water mark of the process's resident memory, from /proc, or None."""
    try:
        with open(f'/proc/{pid}/status', encoding='ascii') as status:
            for line in status:
                if line.startswith('VmHWM:'):
                    return int(line.split()[1])
    except (OSError, ValueError):
        pass
    return None


def measure(command, cwd, runs):
    """Median wall seconds and peak resident KiB over `runs`, and the output.

    The peak is the last VmHWM reading of the child, sampled every 5 ms while
    it runs. The mark belongs to the program's own address space, so it does
    not include this script's memory, which the resource usage of a forked
    child does. A rise in the final 5 ms can be missed.
    """
    times, peaks, output = [], [], None
    for _ in range(runs):
        started = time.perf_counter()
        process = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        peak = None
        while process.poll() is None:
            reading = peak_resident_kib(process.pid)
            if reading is not None:
                peak = reading
            time.sleep(0.005)
        times.append(time.perf_counter() - started)
        out, err = process.communicate()
        if process.returncode:
            raise RuntimeError(f'{command} exited {process.returncode}\n{err.decode(errors="replace")}')
        if output is not None and out != output:
            raise RuntimeError(f'{command} printed {out!r}, earlier {output!r}')
        if peak is None:
            raise RuntimeError(f'{command} ended before its memory could be read')
        peaks.append(peak)
        output = out
    return statistics.median(times), statistics.median(peaks), output


def events(path):
    with open(path, encoding='utf-8') as lines:
        return [json.loads(line) for line in lines if line.strip()]


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def request_latencies(trace):
    """Spawn-to-finish wall time of each request task, in milliseconds.

    The request tasks are the largest group of tasks spawned at one place.
    """
    spawned, finished, sites = {}, {}, {}
    for event in trace:
        if event['event'] == 'spawn':
            spawned[event['child']] = event['wall_ns']
            sites.setdefault(event.get('at', ''), []).append(event['child'])
        elif event['event'] == 'finish':
            finished[event['task']] = event['wall_ns']
    requests = max(sites.values(), key=len)
    return [(finished[task] - spawned[task]) / 1e6 for task in requests]


def cancellation_latency(trace):
    """Milliseconds from the cancellation to the last task's finish."""
    cancelled = next(event['wall_ns'] for event in trace if event['event'] == 'cancel')
    last = max(event['wall_ns'] for event in trace if event['event'] == 'finish' and event['task'] != 0)
    return (last - cancelled) / 1e6


def machine():
    model = 'unknown'
    try:
        with open('/proc/cpuinfo', encoding='utf-8') as info:
            for line in info:
                if line.startswith('model name'):
                    model = line.split(':', 1)[1].strip()
                    break
    except OSError:
        pass
    return {'cpu': model, 'logical_cpus': os.cpu_count(), 'system': f'{platform.system()} {platform.release()}',
            'architecture': platform.machine()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--neura', required=True, type=Path, help='the neura executable to measure')
    parser.add_argument('--runs', type=int, default=5, help='runs per configuration; the median is reported')
    parser.add_argument('--json', type=Path, help='also write the results here')
    arguments = parser.parse_args()
    neura = arguments.neura.resolve()
    sources = Path(__file__).resolve().parent / 'concurrency'
    results = {'machine': machine(), 'runs': arguments.runs, 'workloads': {}}

    with tempfile.TemporaryDirectory() as scratch:
        scratch = Path(scratch)
        images = {}
        for source in sorted(sources.glob('*.nra')):
            images[source.stem] = scratch / f'{source.stem}.nvr'
            subprocess.run([str(neura), 'build', str(source), '-o', str(images[source.stem])], check=True)

        def timed(name, options, program_arguments=()):
            command = [str(neura), 'run', '--max-steps', STEPS, *options, str(images[name])]
            if program_arguments:
                command += ['--', *program_arguments]
            return measure(command, scratch, arguments.runs)

        def traced(name, program_arguments=()):
            trace = scratch / f'{name}.events'
            command = [str(neura), 'run', '--max-steps', STEPS, '--trace-events', str(trace), '--trace-wall-time',
                       str(images[name])]
            if program_arguments:
                command += ['--', *program_arguments]
            samples = []
            for _ in range(arguments.runs):
                subprocess.run(command, cwd=scratch, check=True, stdout=subprocess.DEVNULL)
                samples.append(events(trace))
            return samples

        # Throughput and peak memory of a parallel map: 64 items.
        rows = {}
        seconds, peak, answer = timed('parallel-primes', [], ['sequential'])
        rows['sequential loop (baseline)'] = {'seconds': seconds, 'items_per_second': 64 / seconds, 'peak_kib': peak}
        workers = [count for count in (1, 2, 4, 8, 16) if count <= (os.cpu_count() or 1)]
        for count in workers:
            seconds, peak, output = timed('parallel-primes', ['--parallel-workers', str(count)])
            if output != answer:
                raise RuntimeError(f'parallel-primes: {count} workers printed {output!r}, the loop {answer!r}')
            rows[f'{count} worker(s)'] = {'seconds': seconds, 'items_per_second': 64 / seconds, 'peak_kib': peak}
        results['workloads']['parallel-primes'] = rows

        # Throughput and peak memory of a channel pipeline: 200,000 messages.
        rows = {}
        seconds, peak, answer = timed('channel-pipeline', [], ['sequential'])
        rows['sequential loop (baseline)'] = {'seconds': seconds, 'messages_per_second': 200000 / seconds,
                                              'peak_kib': peak}
        seconds, peak, output = timed('channel-pipeline', [])
        if output != answer:
            raise RuntimeError(f'channel-pipeline: tasks printed {output!r}, the loop {answer!r}')
        rows['producer, 4 workers, collector'] = {'seconds': seconds, 'messages_per_second': 200000 / seconds,
                                                  'peak_kib': peak}
        results['workloads']['channel-pipeline'] = rows

        # Tail latency of 2,000 concurrent requests. Tracing is on, so these
        # times include the cost of writing each event.
        latencies = [latency for trace in traced('request-latency') for latency in request_latencies(trace)]
        seconds, peak, _ = timed('request-latency', [])
        results['workloads']['request-latency'] = {
            '2,000 requests at once': {
                'p50_ms': percentile(latencies, 0.50), 'p99_ms': percentile(latencies, 0.99),
                'p999_ms': percentile(latencies, 0.999), 'max_ms': max(latencies),
                'requests_per_second': 2000 / seconds, 'peak_kib': peak,
            }
        }

        # Cancellation latency of a scope of 1,000 tasks.
        rows = {}
        for label, program_arguments in (('1,000 parked tasks', ()), ('1,000 running tasks', ('running',))):
            samples = [cancellation_latency(trace) for trace in traced('cancellation-latency', program_arguments)]
            _, peak, _ = timed('cancellation-latency', [], program_arguments)
            rows[label] = {'median_ms': statistics.median(samples), 'max_ms': max(samples), 'peak_kib': peak}
        results['workloads']['cancellation-latency'] = rows

    info = results['machine']
    print(f"Machine: {info['cpu']}, {info['logical_cpus']} logical CPUs, {info['system']} {info['architecture']}; "
          f"median of {arguments.runs} runs")
    for workload, rows in results['workloads'].items():
        columns = list(next(iter(rows.values())))
        print(f'\n{workload}\n')
        print('| Configuration | ' + ' | '.join(columns) + ' |')
        print('| --- |' + ' ---: |' * len(columns))
        for label, row in rows.items():
            print(f'| {label} | ' + ' | '.join(f'{row[column]:,.2f}' if isinstance(row[column], float)
                                             else f'{row[column]:,}' for column in columns) + ' |')
    if arguments.json:
        arguments.json.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

# Census: why steps run outside compiled regions, and what a step costs

Commit b9ef537 (`ccr-e433a631-4hzo4n-m2`), Release build (`cmake -G Ninja
-DCMAKE_BUILD_TYPE=Release`, `--parallel 2`), x86-64 Linux, 4 cores shared
with another agent's builds and tests. Native code is on (`--jit=auto`
enables it on x86-64 Linux). Every time below is **CPU time (user +
system) of the measured process**, read with `wait4` (`cpurun.py`), unless
it is a figure of the `tier: time:` line, which is wall time split by the
runtime. Nothing in the repository was changed; the instrumentation lives in
`census-instrumentation.patch` and was reverted after building.

## How it was measured

Binaries, both built from b9ef537 in the worktree's `build/`:

- `neura-base`: the commit as it is. All `--tier-stats`, clean, and
  callgrind runs.
- `neura-census`: the commit plus `census-instrumentation.patch` (applied by
  `instrument.py`, never committed). With `NEURA_CENSUS=FILE` it counts every
  step committed outside a region by (function, block, exit taken, *cause*,
  tier-1 form), where the cause is what the last `run_regions` call found at
  that block: no region there (`no-region`; after a region left to it,
  `no-region-after-leave`), or a region that deoptimized there (budget, exit,
  check, selector, reference, state trap) or refused entry. It also records,
  per block, the region formation result and its rejection reason (a field
  added to `TierStatistics`), whether the block has a LIFT hint, and why it
  is not lifted. With `NEURA_CENSUS_TIME=1` it adds `rdtsc` timers around
  `commit_exit` (by exit kind and state operation), `push_frame`, `allocate`,
  `collect`, `run_regions`, `run_region`, the native code, and each inline
  state helper call of native code. The timers add 15 to 30 ns per timed
  call; they are used only for differences between kinds of the same call
  (a commit of kind K minus a plain continue commit), never as absolute
  costs. A census run is about three times slower than a plain one; the
  counts do not depend on it.

Runs (scripts in this directory; each skips work already done, so a
container restart costs a re-run of one program):

- `run_full.sh stats|census|clean`: every program of
  `tests/bench-wall/neura` at its **full** size (the middle number of its
  `// sizes:` line), one at a time: `neura run --stats --tier-stats
  --max-steps 1000000000000 --max-heap-cells 4294967296 IMG -- SIZE` (raw
  output in `raw/P.full.tierstats.{out,err,cpu}`), the census binary with
  both variables (`runs/P.full.census.*`), and three clean runs without
  `--tier-stats` (`runs/P.full.clean.cpu`; the median is reported).
- `run_long.sh stats`: every program at its **long** size with
  `--tier-stats` (`raw/P.long.tierstats.*`); `run_long.sh clean` for
  `binary-trees`, `nbody`, `sha256`, `text-split` (`runs/P.long.clean.cpu`).
- `run_mid.sh`: the census at 10 times the full size for `nbody` (250000)
  and 10 times for `text-split` (150000), whose full sizes are dominated by
  warm-up (nbody) or are short (text-split); and callgrind runs.
- Callgrind (`valgrind --tool=callgrind --smc-check=all-non-file`, base
  binary, with `--tier-stats` so the run reports its own step counts):
  `sha256` 50000, `binary-trees` 13, `text-split` 15000, `nbody` 250000
  (`run_mid.sh`), and the other thirteen programs at full size
  (`run_cg2.sh`). `perf` is not installed on this machine; callgrind's
  instruction counts are exact and deterministic, and are converted to time
  with the program's own ns per instruction: the fastest of three clean CPU
  times at the callgrind size divided by the run's total instructions
  (`cgmeasure.py` → `cg/measured.txt`). Generated native code has no symbol,
  so callgrind attributes it to `???`; that count is "native code".

Classification of a step outside a region (`analyse.py`, `tables.py`): by
the exit the block took — call (direct; through a dispatch block; a closure,
when the callee is a `$closureN.$invoke`), return, object allocation,
collection allocation, text building, `AppendElement` (and the other growth
and freeing operations), a kernel by name, a host request, a scheduler
operation — or, for a continue exit or an inline state operation (which a
region could run), the reason the block is outside: the block is not lifted
(with its reason), the region is not formed yet (`counting`, or formed later
in the run: warm-up), the region formation's rejection ("no exit stays in
the region", with the dominant exit of the successor block that keeps it
out, or "the entry is not lifted or cannot continue in a region"), or the
deoptimization that sent it there.

Measured: everything in the tables. Derived by reading, and said where used:
the reasons a block's successor keeps it out of a region (from
`form_region`, tier2.cpp:1153-1201), the collection cost's growth in
`text-split` (from `HeapTable::sweep`, machine.cpp:675-689), the instruction
counts of the inline forms in estimate (b), the long-size collection cost
per collection (from the full size's count scaled by the steps), and every
estimate in A4 (a model over measured quantities, shown term by term).

## A1. `--tier-stats` as it is

Full size. "CPU (clean)" is the median of three runs without
`--tier-stats`; the other columns are from one `--tier-stats` run (the
`tier: time:` figures are wall-clock seconds as the runtime splits them).
Steps are `stats: steps`; the tier percentages are of block steps (kernel-
charged steps are not blocks: nbody and text-split charge 25,026 and 384,573
kernel steps at this size). The `left N times at function F block B` lines
are in `raw/P.full.tierstats.err`.

| Program | Steps | CPU s (clean) | CPU s (--tier-stats) | tier 2 | tier 1 | tier 0 | regions entered (deoptimized) | load | proving | tier 0 | tier 1 | regions | collection | other |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | 209,899,904 | 14.889 | 16.829 | 32.2% | 67.8% | 0.0% | 30,056,179 (0) | 0.017 | 0.005 | 0.000 | 10.204 | 3.669 | 3.030 | 0.041 |
| closures | 12,268,525 | 0.548 | 0.656 | 67.4% | 32.6% | 0.0% | 1,946,306 (0) | 0.031 | 0.066 | 0.001 | 0.309 | 0.248 | 0.001 | 0.001 |
| collatz | 43,516,363 | 0.146 | 0.141 | 100.0% | 0.0% | 0.0% | 1 (0) | 0.017 | 0.017 | 0.000 | 0.001 | 0.102 | 0.000 | 0.001 |
| dispatch | 9,854,101 | 0.535 | 0.626 | 62.7% | 37.3% | 0.0% | 1,194,003 (0) | 0.021 | 0.049 | 0.001 | 0.337 | 0.214 | 0.000 | 0.001 |
| fannkuch | 64,070,452 | 0.729 | 0.711 | 100.0% | 0.0% | 0.0% | 1 (0) | 0.017 | 0.006 | 0.000 | 0.002 | 0.690 | 0.000 | 0.000 |
| hash-map | 52,186,859 | 2.027 | 2.432 | 75.3% | 24.7% | 0.0% | 7,415,688 (0) | 0.027 | 0.029 | 0.001 | 1.079 | 1.313 | 0.008 | 0.005 |
| mandelbrot | 2,592,335 | 0.252 | 0.207 | 97.5% | 2.5% | 0.0% | 1 (0) | 0.032 | 0.150 | 0.004 | 0.012 | 0.003 | 0.000 | 0.001 |
| matrix-multiply | 6,052,076 | 0.125 | 0.122 | 97.6% | 2.4% | 0.0% | 23,389 (0) | 0.022 | 0.047 | 0.001 | 0.015 | 0.032 | 0.000 | 0.001 |
| nbody | 2,517,196 | 0.276 | 0.287 | 94.9% | 5.1% | 0.0% | 26,767 (0) | 0.056 | 0.148 | 0.014 | 0.028 | 0.036 | 0.000 | 0.001 |
| object-churn | 57,220,437 | 2.455 | 3.007 | 62.7% | 37.3% | 0.0% | 11,222,199 (0) | 0.031 | 0.037 | 0.001 | 1.521 | 1.371 | 0.045 | 0.001 |
| parse-results | 44,722,868 | 1.258 | 1.567 | 88.8% | 11.2% | 0.0% | 4,033,598 (0) | 0.024 | 0.067 | 0.001 | 0.588 | 0.808 | 0.081 | 0.001 |
| sha256 | 31,343,525 | 1.881 | 2.029 | 20.6% | 79.4% | 0.0% | 1,933,426 (0) | 0.019 | 0.025 | 0.001 | 1.739 | 0.247 | 0.000 | 0.001 |
| sieve | 41,695,702 | 0.562 | 0.863 | 92.7% | 7.3% | 0.0% | 2,999,167 (0) | 0.019 | 0.013 | 0.000 | 0.323 | 0.505 | 0.000 | 0.002 |
| sort | 73,316,801 | 2.286 | 2.999 | 76.2% | 23.8% | 0.0% | 8,885,083 (0) | 0.025 | 0.026 | 0.001 | 1.299 | 1.646 | 0.000 | 0.001 |
| spectral-norm | 1,653,456 | 0.188 | 0.243 | 79.2% | 20.8% | 0.0% | 324,741 (0) | 0.044 | 0.126 | 0.004 | 0.031 | 0.030 | 0.000 | 0.001 |
| string-building | 63,578,697 | 1.488 | 1.927 | 85.0% | 15.0% | 0.0% | 6,279,925 (0) | 0.018 | 0.004 | 0.000 | 0.880 | 1.022 | 0.009 | 0.003 |
| text-split | 22,398,636 | 0.884 | 1.220 | 75.6% | 24.4% | 0.0% | 3,310,802 (0) | 0.024 | 0.068 | 0.001 | 0.573 | 0.512 | 0.042 | 0.001 |

Per-step cost from the time line (ns): regions = regions time / steps in regions; tier 1 = tier 1 time / (lifted + wired + empty + declined steps)

| Program | ns per region step | region steps per entry | ns per tier-1 step |
| --- | ---: | ---: | ---: |
| binary-trees | 54.2 | 2.3 | 71.7 |
| closures | 30.0 | 4.2 | 77.3 |
| collatz | 2.3 | 43511327.0 | 119.4 |
| dispatch | 34.6 | 5.2 | 91.7 |
| fannkuch | 10.8 | 64059129.0 | 150.2 |
| hash-map | 33.4 | 5.3 | 83.8 |
| mandelbrot | 1.2 | 2528137.0 | 188.5 |
| matrix-multiply | 5.5 | 252.6 | 99.9 |
| nbody | 15.1 | 88.4 | 219.7 |
| object-churn | 38.2 | 3.2 | 71.2 |
| parse-results | 20.3 | 9.9 | 117.9 |
| sha256 | 38.3 | 3.3 | 69.9 |
| sieve | 13.1 | 12.9 | 106.7 |
| sort | 29.5 | 6.3 | 74.5 |
| spectral-norm | 22.8 | 4.0 | 90.1 |
| string-building | 18.9 | 8.6 | 91.9 |
| text-split | 30.8 | 5.0 | 106.5 |

Long size, one `--tier-stats` run each (clean runs for four programs in A3):

| Program | Steps | CPU s (clean) | CPU s (--tier-stats) | tier 2 | tier 1 | tier 0 | regions entered (deoptimized) | load | proving | tier 0 | tier 1 | regions | collection | other |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | 1,914,011,930 | 169.146 | 153.675 | 32.2% | 67.8% | 0.0% | 274,024,867 (0) | 0.017 | 0.005 | 0.001 | 92.972 | 33.410 | 28.236 | 0.264 |
| closures | 6,132,004,525 | — | 308.394 | 67.7% | 32.3% | 0.0% | 976,792,107 (0) | 0.030 | 0.071 | 0.001 | 171.732 | 138.165 | 1.280 | 0.001 |
| collatz | 4,934,339,417 | — | 12.335 | 100.0% | 0.0% | 0.0% | 1 (0) | 0.019 | 0.018 | 0.000 | 0.001 | 12.373 | 0.000 | 0.001 |
| dispatch | 2,460,462,296 | — | 139.921 | 63.4% | 36.6% | 0.0% | 299,994,003 (0) | 0.022 | 0.048 | 0.001 | 86.477 | 54.725 | 0.000 | 0.001 |
| fannkuch | 9,417,572,451 | — | 114.990 | 100.0% | 0.0% | 0.0% | 1 (0) | 0.024 | 0.010 | 0.000 | 0.003 | 115.633 | 0.000 | 0.001 |
| hash-map | 673,452,337 | — | 30.510 | 76.7% | 23.3% | 0.0% | 100,044,003 (0) | 0.020 | 0.021 | 0.001 | 13.030 | 17.507 | 0.029 | 0.101 |
| mandelbrot | 14,583,983,433 | — | 21.779 | 100.0% | 0.0% | 0.0% | 1,290 (0) | 0.032 | 0.155 | 0.004 | 0.026 | 21.654 | 0.000 | 0.001 |
| matrix-multiply | 10,680,679,456 | — | 80.980 | 100.0% | 0.0% | 0.0% | 3,630,099 (0) | 0.037 | 0.050 | 0.001 | 0.380 | 81.169 | 0.000 | 0.003 |
| nbody | 35,210,002,196 | 807.706 | 736.502 | 98.8% | 1.2% | 0.0% | 419,991,265 (0) | 0.053 | 0.120 | 0.013 | 181.256 | 561.999 | 0.000 | 0.001 |
| object-churn | 5,872,678,432 | — | 366.087 | 62.8% | 37.2% | 0.0% | 1,153,477,197 (0) | 0.024 | 0.040 | 0.001 | 184.925 | 168.762 | 16.006 | 0.001 |
| parse-results | 6,249,474,408 | — | 323.718 | 89.1% | 10.9% | 0.0% | 564,690,098 (0) | 0.026 | 0.068 | 0.001 | 91.467 | 129.617 | 105.737 | 0.002 |
| sha256 | 18,801,572,041 | 1410.848 | 1325.177 | 20.7% | 79.3% | 0.0% | 1,167,175,706 (0) | 0.028 | 0.038 | 0.001 | 1168.903 | 168.004 | 0.000 | 0.127 |
| sieve | 2,038,449,781 | — | 45.562 | 93.1% | 6.9% | 0.0% | 139,999,167 (0) | 0.018 | 0.014 | 0.001 | 13.353 | 32.443 | 0.000 | 0.059 |
| sort | 5,789,817,239 | — | 234.270 | 75.9% | 24.1% | 0.0% | 712,969,755 (0) | 0.019 | 0.022 | 0.001 | 103.418 | 133.030 | 0.000 | 0.014 |
| spectral-norm | 8,100,990,976 | — | 342.594 | 80.0% | 20.0% | 0.0% | 1,619,995,542 (0) | 0.035 | 0.086 | 0.004 | 168.970 | 176.222 | 0.000 | 0.002 |
| string-building | 4,477,778,702 | — | 147.247 | 85.7% | 14.3% | 0.0% | 438,879,926 (0) | 0.025 | 0.005 | 0.003 | 66.504 | 81.097 | 0.763 | 0.186 |
| text-split | 2,985,816,673 | 334.473 | 329.846 | 75.9% | 24.1% | 0.0% | 443,536,792 (0) | 0.029 | 0.081 | 0.001 | 73.169 | 66.977 | 192.139 | 0.001 |

Per-step cost from the time line (ns): regions = regions time / steps in regions; tier 1 = tier 1 time / (lifted + wired + empty + declined steps)

| Program | ns per region step | region steps per entry | ns per tier-1 step |
| --- | ---: | ---: | ---: |
| binary-trees | 54.2 | 2.3 | 71.7 |
| closures | 33.3 | 4.2 | 86.6 |
| collatz | 2.5 | 4934334361.0 | 118.9 |
| dispatch | 35.1 | 5.2 | 96.1 |
| fannkuch | 12.3 | 9417562236.0 | 264.5 |
| hash-map | 33.9 | 5.2 | 82.9 |
| mandelbrot | 1.5 | 11305307.0 | 192.9 |
| matrix-multiply | 7.6 | 2941.2 | 101.2 |
| nbody | 16.3 | 82.0 | 431.4 |
| object-churn | 45.8 | 3.2 | 84.6 |
| parse-results | 23.3 | 9.9 | 134.9 |
| sha256 | 43.1 | 3.3 | 78.4 |
| sieve | 17.1 | 13.6 | 95.4 |
| sort | 30.3 | 6.2 | 74.2 |
| spectral-norm | 27.2 | 4.0 | 104.3 |
| string-building | 21.1 | 8.7 | 104.1 |
| text-split | 30.1 | 5.0 | 103.5 |

Observations:

- At long size, collatz, fannkuch, mandelbrot and matrix-multiply run 100%
  of their steps in regions, at 1.5 to 12 ns per region step; nbody 98.8%,
  sieve 93%, parse-results 89%, string-building 86%, spectral-norm 80%,
  hash-map 77%, sort and text-split 76%, closures 68%, dispatch 63%,
  object-churn 63%, binary-trees 32%, sha256 21%. No region ever
  deoptimized in any of these runs (the parenthesized counts): regions end
  only by leaving, at a call, a return, or a state operation they cannot run.
- Every program with calls in its inner loop has 2 to 10 region steps per
  region entry, and pays an entry every few blocks; its region steps cost 21
  to 54 ns each because of that (and of the inline state helpers, A3).
- A step outside a region costs 70 to 135 ns at tier 1 (nbody's 431 ns at
  long size is mostly its `sqrt` kernel call; fannkuch's and mandelbrot's
  are their warm-up steps at tier 0).
- Collection is a large share at long size for text-split (192 s),
  parse-results (106 s), binary-trees (28 s) and object-churn (16 s).
- `--tier-stats` costs 0 to 35% (clean vs. stats CPU at full size): two clock
  readings per region entry.

## A2. Why each step outside a region is outside

Overview (share of the steps outside regions; "next to call/return/helper"
is a block whose own exit a region could run, a continue or an inline state
operation, but whose region was rejected because its successor ends a
region: a call, a return, an allocation or another helper, so the region
would have no internal edge; "warm-up" is a block whose region was not
formed yet when it ran):

| Program | size | steps outside regions | call | return | alloc | append | text | kernel | next to call/return/helper | warm-up | not lifted | other |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | full | 142,252,344 (67.8%) | 31.6% | 31.6% | 10.5% | 0.0% | 0.0% | 0.0% | 26.2% | 0.0% | 0.0% | 0.0% |
| closures | full | 4,000,643 (32.6%) | 30.0% | 30.0% | 0.1% | 19.0% | 0.0% | 0.0% | 20.0% | 0.9% | 0.0% | 0.0% |
| collatz | full | 5,027 (0.0%) | 1.4% | 1.4% | 0.4% | 0.5% | 0.1% | 0.1% |  | 92.2% | 3.9% | 0.0% |
| dispatch | full | 3,671,312 (37.3%) | 32.7% | 32.7% | 0.0% | 0.0% | 0.0% | 0.0% | 32.7% | 1.8% | 0.0% | 0.0% |
| fannkuch | full | 11,317 (0.0%) | 0.5% | 0.5% | 0.2% | 0.4% | 0.0% | 0.0% |  | 95.9% | 2.4% | 0.0% |
| hash-map | full | 12,866,235 (24.7%) | 29.5% | 29.5% | 1.2% | 24.6% | 0.0% | 0.0% | 10.9% | 4.3% | 0.0% | 0.0% |
| mandelbrot | full | 64,191 (2.5%) | 0.1% | 0.1% | 0.0% | 0.0% | 0.0% | 0.0% |  | 99.4% | 0.4% | 0.0% |
| matrix-multiply | full | 145,117 (2.4%) | 0.0% | 0.0% | 0.0% | 16.8% | 0.0% | 0.0% | 0.7% | 82.3% | 0.1% | 0.0% |
| nbody | full | 126,987 (5.1%) | 2.0% | 2.0% | 0.0% | 0.0% | 0.0% | 19.7% | 2.0% | 73.7% | 0.6% | 0.0% |
| nbody | 250000 | 3,113,494 (1.3%) | 8.0% | 8.0% | 0.0% | 0.0% | 0.0% | 80.3% | 0.3% | 3.4% | 0.0% | 0.0% |
| object-churn | full | 21,363,506 (37.3%) | 35.5% | 35.5% | 1.6% | 10.5% | 0.0% | 0.0% | 16.7% | 0.2% | 0.0% | 0.0% |
| parse-results | full | 4,987,723 (11.2%) | 30.7% | 30.7% | 27.7% | 0.4% | 5.9% | 0.0% | 2.3% | 2.3% | 0.0% | 0.0% |
| sha256 | full | 24,880,024 (79.4%) | 37.0% | 37.0% | 0.0% | 2.0% | 0.0% | 0.0% | 23.9% | 0.2% | 0.0% | 0.0% |
| sieve | full | 3,031,251 (7.3%) | 0.0% | 0.0% | 0.0% | 99.0% | 0.0% | 0.0% | 0.0% | 1.0% | 0.0% | 0.0% |
| sort | full | 17,442,537 (23.8%) | 48.1% | 48.1% | 0.0% | 2.9% | 0.0% | 0.0% | 0.1% | 1.0% | 0.0% | 0.0% |
| spectral-norm | full | 344,116 (20.8%) | 47.6% | 47.6% | 0.0% | 0.1% | 0.0% | 0.0% |  | 4.6% | 0.1% | 0.0% |
| string-building | full | 9,567,637 (15.0%) | 16.7% | 16.7% | 0.0% | 49.0% | 4.2% | 0.0% | 12.6% | 0.7% | 0.0% | 0.0% |
| text-split | full | 5,375,705 (24.4%) | 18.4% | 18.4% | 2.2% | 38.1% | 6.7% | 5.0% | 9.8% | 1.3% | 0.0% | 0.0% |
| text-split | 150000 | 53,109,876 (24.1%) | 18.6% | 18.6% | 2.3% | 38.6% | 6.8% | 5.1% | 9.9% | 0.1% | 0.0% | 0.0% |

What the overview says, per kind:

- **Calls and returns** are the largest class wherever the inner loop calls
  a function: 63% of the steps outside regions in binary-trees, 74% in
  sha256, 96% in sort and spectral-norm, 60-71% in closures, dispatch,
  hash-map, object-churn, parse-results. Each one also cuts the region before
  it, which is where most "next to call/return/helper" steps come from.
- **Allocation, growth and text**: `AppendElement` is 99% of sieve's, 49% of
  string-building's, 38% of text-split's, 25% of hash-map's steps outside
  regions; object allocation 28% of parse-results' and 11% of
  binary-trees'.
- **Kernels**: nbody's `neura.math.sqrt` is 80% of its steps outside regions
  in steady state (250000); text-split calls three text kernels.
- **Blocks not lifted** are below 0.1% everywhere; no hint was rejected and
  no block of these programs stays on tier 0 except below the threshold.
- **Warm-up** dominates the full sizes of collatz, fannkuch, mandelbrot,
  matrix-multiply and nbody, where under 3% of steps are outside regions;
  at long size those programs run 98.8-100% in regions.

Per program, the blocks that account for at least 95% of the steps outside
regions, with the reason (the complete per-block tables of all seventeen
programs at full size, including the warm-up-only ones, are in
`census-blocks.md`):

### binary-trees (size 16)

Steps 209,899,833 (block steps; kernel-charged steps are not blocks); in regions 67,647,489 (32.2%); outside 142,252,344 (67.8%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 44,958,282 | 31.6% |
| call (direct) | 44,957,945 | 31.6% |
| object allocation | 14,986,017 | 10.5% |
| continue: no exit stays in the region (successor: return) | 7,536,640 | 5.3% |
| continue: no exit stays in the region (successor: call) | 7,450,264 | 5.2% |
| inline WriteField: no exit stays in the region (successor: call) | 7,449,262 | 5.2% |
| inline WriteField: no exit stays in the region (successor: return) | 7,449,262 | 5.2% |
| inline ReadField: no exit stays in the region (successor: call) | 7,449,258 | 5.2% |
| (other categories) | 15,414 | 0.0% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f4 BinaryTrees.bottom_up | 0 | binary-trees.nra:20:20 | 14,985,902 | 10.5% | object allocation |
| f8 BinaryTrees.TreeNode.init | 2 | binary-trees.nra:16:9 | 14,985,902 | 21.1% | return |
| f4 BinaryTrees.bottom_up | 4 | binary-trees.nra:25:9 | 14,985,902 | 31.6% | return |
| f4 BinaryTrees.bottom_up | 1 | binary-trees.nra:20:20 | 14,985,902 | 42.1% | call (direct) → BinaryTrees.TreeNode.init |
| f4 BinaryTrees.bottom_up | 2 | binary-trees.nra:21:9 | 7,536,640 | 47.4% | continue: no exit stays in the region (successor: return) |
| f5 BinaryTrees.check | 2 | binary-trees.nra:30:13 | 7,536,640 | 52.7% | return |
| f4 BinaryTrees.bottom_up | 3 | binary-trees.nra:22:35 | 7,449,262 | 58.0% | call (direct) → BinaryTrees.bottom_up |
| f5 BinaryTrees.check | 4 | binary-trees.nra:32:20 | 7,449,262 | 63.2% | call (direct) → BinaryTrees.check |
| f5 BinaryTrees.check | 6 | binary-trees.nra:32:39 | 7,449,262 | 68.4% | call (direct) → BinaryTrees.check |
| f4 BinaryTrees.bottom_up | 5 | binary-trees.nra:22:13 | 7,449,262 | 73.7% | inline WriteField: no exit stays in the region (successor: call) |
| f5 BinaryTrees.check | 7 | binary-trees.nra:32:16 | 7,449,262 | 78.9% | return |
| f4 BinaryTrees.bottom_up | 6 | binary-trees.nra:23:36 | 7,449,262 | 84.2% | call (direct) → BinaryTrees.bottom_up |
| f4 BinaryTrees.bottom_up | 7 | binary-trees.nra:23:13 | 7,449,262 | 89.4% | inline WriteField: no exit stays in the region (successor: return) |
| f4 BinaryTrees.bottom_up | 2 | binary-trees.nra:21:9 | 7,449,258 | 94.6% | continue: no exit stays in the region (successor: call) |
| f5 BinaryTrees.check | 5 | binary-trees.nra:32:16 | 7,449,258 | 99.9% | inline ReadField: no exit stays in the region (successor: call) |
### sha256 (size 500000)

Steps 31,343,491 (block steps; kernel-charged steps are not blocks); in regions 6,463,467 (20.6%); outside 24,880,024 (79.4%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 9,196,300 | 37.0% |
| call (direct) | 9,196,078 | 37.0% |
| inline ReadGlobal: no exit stays in the region (successor: return) | 4,687,796 | 18.8% |
| inline ReadElement: no exit stays in the region (successor: call) | 1,250,068 | 5.0% |
| state op AppendElement | 500,361 | 2.0% |
| (other categories) | 49,421 | 0.2% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f11 Sha256.add | 1 | sha256.nra:29:55 | 4,687,800 | 18.8% | return |
| f11 Sha256.add | 0 | sha256.nra:29:90 | 4,687,796 | 37.7% | inline ReadGlobal: no exit stays in the region (successor: return) |
| f10 Sha256.rotate | 0 | sha256.nra:26:63 | 4,500,288 | 55.8% | return |
| f5 Sha256.compress | 38 | sha256.nra:56:30 | 500,032 | 57.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 45 | sha256.nra:57:22 | 500,032 | 59.8% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 41 | sha256.nra:56:42 | 500,032 | 61.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 50 | sha256.nra:67:17 | 500,032 | 63.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 33 | sha256.nra:54:22 | 500,032 | 65.8% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 49 | sha256.nra:63:17 | 500,032 | 67.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 48 | sha256.nra:59:22 | 500,032 | 69.8% | call (direct) → Sha256.add |
| f5 Sha256.compress | 46 | sha256.nra:57:37 | 500,032 | 71.8% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 47 | sha256.nra:57:53 | 500,032 | 73.9% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 44 | sha256.nra:56:22 | 500,032 | 75.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 42 | sha256.nra:56:26 | 500,032 | 77.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 36 | sha256.nra:54:37 | 500,032 | 79.9% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 37 | sha256.nra:54:53 | 500,032 | 81.9% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 43 | sha256.nra:56:75 | 500,028 | 83.9% | inline ReadElement: no exit stays in the region (successor: call) |
| f1 Sha256.main | 6 | sha256.nra:83:28 | 500,000 | 85.9% | state op AppendElement |
| f5 Sha256.compress | 22 | sha256.nra:43:27 | 375,024 | 87.4% | call (direct) → Sha256.add |
| f5 Sha256.compress | 19 | sha256.nra:43:31 | 375,024 | 88.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 16 | sha256.nra:42:22 | 375,024 | 90.4% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 21 | sha256.nra:43:58 | 375,024 | 91.9% | call (direct) → Sha256.add |
| f5 Sha256.compress | 14 | sha256.nra:41:22 | 375,024 | 93.5% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 15 | sha256.nra:41:39 | 375,024 | 95.0% | call (direct) → Sha256.rotate |
| f5 Sha256.compress | 17 | sha256.nra:42:39 | 375,024 | 96.5% | call (direct) → Sha256.rotate |
### nbody (size 250000)

Steps 249,002,170 (block steps; kernel-charged steps are not blocks); in regions 245,888,676 (98.7%); outside 3,113,494 (1.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| kernel neura.math.sqrt | 2,500,020 | 80.3% |
| return | 250,075 | 8.0% |
| call (through a dispatch block) | 250,000 | 8.0% |
| continue: region not formed yet (counting) | 33,349 | 1.1% |
| inline ReadField: region not formed yet (counting) | 25,501 | 0.8% |
| inline ReadElement: region not formed yet (counting) | 18,111 | 0.6% |
| continue: region not formed yet (formed later) | 17,996 | 0.6% |
| inline ReadTag: no exit stays in the region (successor: call) | 8,003 | 0.3% |
| (other categories) | 10,439 | 0.3% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f25 NBody.Planets.advance | 30 | nbody.nra:60:36 | 2,500,000 | 80.3% | kernel neura.math.sqrt |
| f25 NBody.Planets.advance | 91 | nbody.nra:70:13 | 250,000 | 88.3% | return |
| f1 NBody.main | 12 | nbody.nra:98:13 | 250,000 | 96.4% | call (through a dispatch block) → NBody.Planets.advance |
### text-split (size 150000)

Steps 220,100,516 (block steps; kernel-charged steps are not blocks); in regions 166,990,640 (75.9%); outside 53,109,876 (24.1%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| state op AppendElement | 20,501,625 | 38.6% |
| return | 9,901,065 | 18.6% |
| call (through a dispatch block) | 5,100,993 | 9.6% |
| call (direct) | 4,800,070 | 9.0% |
| inline ReadElement: no exit stays in the region (successor: AppendElement) | 3,584,937 | 6.8% |
| text building (TextSlice) | 2,400,004 | 4.5% |
| kernel neura.text.space_suffix_start | 1,200,000 | 2.3% |
| kernel neura.text.space_prefix_length | 1,200,000 | 2.3% |
| collection allocation (AllocateCollection) | 900,004 | 1.7% |
| inline ReadTag: no exit stays in the region (successor: call) | 900,000 | 1.7% |
| text building (ScalarText) | 900,000 | 1.7% |
| object allocation | 300,015 | 0.6% |
| inline WriteField: no exit stays in the region (successor: return) | 300,002 | 0.6% |
| text building (TextFromUtf8) | 300,001 | 0.6% |
| kernel neura.text.split | 300,000 | 0.6% |
| inline ReadField: no exit stays in the region (successor: TextFromUtf8) | 150,001 | 0.3% |
| inline ReadLength: no exit stays in the region (successor: AllocateCollection) | 150,000 | 0.3% |
| (other categories) | 221,159 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f34 StringBuilder.StringBuilder.append | 7 | std/core/string-builder.nra:35:17 | 9,726,326 | 18.3% | state op AppendElement |
| f34 StringBuilder.StringBuilder.append | 9 | std/core/string-builder.nra:37:13 | 4,051,000 | 25.9% | return |
| f13 Strings.append_bytes | 14 | std/text/strings.nra:517:13 | 2,545,367 | 30.7% | state op AppendElement |
| f13 Strings.append_bytes | 13 | std/text/strings.nra:516:9 | 2,250,000 | 35.0% | return |
| f1 TextSplit.main | 21 | text-split.nra:35:21 | 1,200,993 | 37.2% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f6 Strings.trim | 4 | std/text/strings.nra:241:16 | 1,200,000 | 39.5% | text building (TextSlice) |
| f1 TextSplit.main | 44 | text-split.nra:53:17 | 1,200,000 | 41.8% | state op AppendElement |
| f1 TextSplit.main | 40 | text-split.nra:49:32 | 1,200,000 | 44.0% | call (direct) → Strings.trim |
| f6 Strings.trim | 0 | std/text/strings.nra:236:20 | 1,200,000 | 46.3% | kernel neura.text.space_suffix_start |
| f6 Strings.trim | 1 | std/text/strings.nra:236:20 | 1,200,000 | 48.5% | kernel neura.text.space_prefix_length |
| f7 Strings.join | 16 | std/text/strings.nra:206:42 | 1,200,000 | 50.8% | call (direct) → Strings.append_bytes |
| f6 Strings.trim | 5 | std/text/strings.nra:241:9 | 1,200,000 | 53.0% | return |
| f13 Strings.append_bytes | 7 | std/text/strings.nra:512:13 | 1,194,983 | 55.3% | state op AppendElement |
| f13 Strings.append_bytes | 9 | std/text/strings.nra:513:13 | 1,194,983 | 57.5% | state op AppendElement |
| f13 Strings.append_bytes | 5 | std/text/strings.nra:511:13 | 1,194,983 | 59.8% | state op AppendElement |
| f13 Strings.append_bytes | 3 | std/text/strings.nra:510:13 | 1,194,983 | 62.0% | state op AppendElement |
| f13 Strings.append_bytes | 8 | std/text/strings.nra:513:36 | 1,194,979 | 64.3% | inline ReadElement: no exit stays in the region (successor: AppendElement) |
| f13 Strings.append_bytes | 6 | std/text/strings.nra:512:36 | 1,194,979 | 66.5% | inline ReadElement: no exit stays in the region (successor: AppendElement) |
| f13 Strings.append_bytes | 4 | std/text/strings.nra:511:36 | 1,194,979 | 68.8% | inline ReadElement: no exit stays in the region (successor: AppendElement) |
| f21 Strings.separators | 8 | std/text/strings.nra:464:13 | 1,050,000 | 70.8% | state op AppendElement |
| f12 Strings.split_pieces | 12 | std/text/strings.nra:476:9 | 1,050,000 | 72.8% | text building (TextSlice) |
| f7 Strings.join | 14 | std/text/strings.nra:203:51 | 1,050,000 | 74.7% | call (direct) → Strings.append_bytes |
| f12 Strings.split_pieces | 13 | std/text/strings.nra:477:13 | 1,050,000 | 76.7% | state op AppendElement |
| f1 TextSplit.main | 16 | text-split.nra:32:21 | 1,050,000 | 78.7% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 TextSplit.main | 28 | text-split.nra:40:21 | 900,000 | 80.4% | call (through a dispatch block) → StringBuilder.StringBuilder.append_int |
| f35 StringBuilder.StringBuilder.append_int | 3 | std/core/string-builder.nra:86:68 | 900,000 | 82.1% | return |
| f35 StringBuilder.StringBuilder.append_int | 2 | std/core/string-builder.nra:86:68 | 900,000 | 83.8% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f35 StringBuilder.StringBuilder.append_int | 1 | std/core/string-builder.nra:86:68 | 900,000 | 85.5% | inline ReadTag: no exit stays in the region (successor: call) |
| f35 StringBuilder.StringBuilder.append_int | 0 | std/core/string-builder.nra:86:75 | 900,000 | 87.2% | text building (ScalarText) |
| f1 TextSplit.main | 30 | text-split.nra:43:21 | 600,000 | 88.3% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 TextSplit.main | 27 | text-split.nra:38:21 | 300,000 | 88.9% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f20 Cell.Cell<StringBuilder>.init | 0 | std/core/cell.nra:14:13 | 150,001 | 89.1% | inline WriteField: no exit stays in the region (successor: return) |
| f4 StringBuilder.create | 1 | std/core/string-builder.nra:180:39 | 150,001 | 89.4% | object allocation |
| f11 StringBuilder.StringBuilder.init | 3 | std/core/string-builder.nra:28:13 | 150,001 | 89.7% | return |
| f10 Cell.of<StringBuilder> | 2 | std/core/cell.nra:19:53 | 150,001 | 90.0% | return |
| f10 Cell.of<StringBuilder> | 1 | std/core/cell.nra:19:53 | 150,001 | 90.3% | call (direct) → Cell.Cell<StringBuilder>.init |
| f36 StringBuilder.StringBuilder.to_string | 0 | std/core/string-builder.nra:159:50 | 150,001 | 90.5% | inline ReadField: no exit stays in the region (successor: TextFromUtf8) |
| f4 StringBuilder.create | 2 | std/core/string-builder.nra:180:39 | 150,001 | 90.8% | call (direct) → StringBuilder.StringBuilder.init |
| f36 StringBuilder.StringBuilder.to_string | 6 | std/core/string-builder.nra:163:13 | 150,001 | 91.1% | return |
| f4 StringBuilder.create | 4 | std/core/string-builder.nra:182:9 | 150,001 | 91.4% | return |
| f36 StringBuilder.StringBuilder.to_string | 1 | std/core/string-builder.nra:159:33 | 150,001 | 91.7% | text building (TextFromUtf8) |
| f10 Cell.of<StringBuilder> | 0 | std/core/cell.nra:19:53 | 150,001 | 92.0% | object allocation |
| f11 StringBuilder.StringBuilder.init | 0 | std/core/string-builder.nra:22:34 | 150,001 | 92.2% | collection allocation (AllocateCollection) |
| f20 Cell.Cell<StringBuilder>.init | 1 | std/core/cell.nra:14:13 | 150,001 | 92.5% | return |
| f4 StringBuilder.create | 0 | std/core/string-builder.nra:179:22 | 150,001 | 92.8% | call (direct) → Cell.of<StringBuilder> |
| f4 StringBuilder.create | 3 | std/core/string-builder.nra:181:9 | 150,001 | 93.1% | inline WriteField: no exit stays in the region (successor: return) |
| f1 TextSplit.main | 38 | text-split.nra:55:27 | 150,000 | 93.4% | call (direct) → Strings.join |
| f1 TextSplit.main | 33 | text-split.nra:47:41 | 150,000 | 93.7% | collection allocation (AllocateCollection) |
| f21 Strings.separators | 0 | std/text/strings.nra:457:71 | 150,000 | 93.9% | collection allocation (AllocateCollection) |
| f12 Strings.split_pieces | 2 | std/text/strings.nra:476:52 | 150,000 | 94.2% | call (direct) → Strings.separators |
| f12 Strings.split_pieces | 1 | std/text/strings.nra:473:18 | 150,000 | 94.5% | collection allocation (AllocateCollection) |
| f21 Strings.separators | 1 | std/text/strings.nra:457:22 | 150,000 | 94.8% | kernel neura.text.split |
| f12 Strings.split_pieces | 0 | std/text/strings.nra:473:18 | 150,000 | 95.1% | inline ReadLength: no exit stays in the region (successor: AllocateCollection) |
### closures (size 400)

Steps 12,268,521 (block steps; kernel-charged steps are not blocks); in regions 8,267,878 (67.4%); outside 4,000,643 (32.6%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 1,202,040 | 30.0% |
| call (closure, through a dispatch block) | 1,197,200 | 29.9% |
| inline ReadField: no exit stays in the region (successor: return) | 799,996 | 20.0% |
| state op AppendElement | 758,218 | 19.0% |
| continue: region not formed yet (counting) | 10,637 | 0.3% |
| continue: region not formed yet (formed later) | 5,994 | 0.1% |
| call (direct) | 4,838 | 0.1% |
| inline ReadTag: region not formed yet (counting) | 4,006 | 0.1% |
| (other categories) | 17,714 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f35 $closure785.$invoke | 1 | closures.nra:61:78 | 400,000 | 10.0% | return |
| f36 $closure792.$invoke | 1 | closures.nra:62:78 | 400,000 | 20.0% | return |
| f7 Closures.filter_values | 8 | closures.nra:29:17 | 400,000 | 30.0% | call (closure, through a dispatch block) → $closure792.$invoke |
| f5 Closures.map_values | 8 | closures.nra:21:27 | 400,000 | 40.0% | call (closure, through a dispatch block) → $closure785.$invoke |
| f35 $closure785.$invoke | 0 | closures.nra:61:86 | 400,000 | 50.0% | inline ReadField: no exit stays in the region (successor: return) |
| f5 Closures.map_values | 9 | closures.nra:21:13 | 400,000 | 60.0% | state op AppendElement |
| f36 $closure792.$invoke | 0 | closures.nra:62:92 | 399,996 | 70.0% | inline ReadField: no exit stays in the region (successor: return) |
| f37 $closure799.$invoke | 3 | closures.nra:63:90 | 357,200 | 78.9% | return |
| f9 Closures.fold_values | 7 | closures.nra:39:21 | 357,200 | 87.8% | call (closure, through a dispatch block) → $closure799.$invoke |
| f7 Closures.filter_values | 11 | closures.nra:30:17 | 357,200 | 96.8% | state op AppendElement |
### dispatch (size 600)

Steps 9,854,095 (block steps; kernel-charged steps are not blocks); in regions 6,182,783 (62.7%); outside 3,671,312 (37.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 1,201,255 | 32.7% |
| call (through a dispatch block) | 1,200,000 | 32.7% |
| inline ReadField: no exit stays in the region (successor: return) | 599,996 | 16.3% |
| inline ReadTag: no exit stays in the region (successor: call) | 599,996 | 16.3% |
| continue: region not formed yet (counting) | 34,793 | 0.9% |
| inline ReadLength: region not formed yet (formed later) | 7,999 | 0.2% |
| inline ReadGlobal: region not formed yet (formed later) | 7,999 | 0.2% |
| inline ReadField: region not formed yet (counting) | 6,196 | 0.2% |
| (other categories) | 13,078 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 Dispatch.main | 37 | dispatch.nra:106:45 | 600,000 | 16.3% | call (through a dispatch block) → Dispatch.Circle.weight |
| f1 Dispatch.main | 39 | dispatch.nra:106:62 | 600,000 | 32.7% | call (through a dispatch block) → Dispatch.Rectangle.mix |
| f1 Dispatch.main | 38 | dispatch.nra:106:29 | 599,996 | 49.0% | inline ReadTag: no exit stays in the region (successor: call) |
| f33 Dispatch.Square.mix | 1 | dispatch.nra:30:57 | 249,000 | 55.8% | return |
| f33 Dispatch.Square.mix | 0 | dispatch.nra:30:65 | 249,000 | 62.6% | inline ReadField: no exit stays in the region (successor: return) |
| f28 Dispatch.Square.weight | 3 | dispatch.nra:28:48 | 129,600 | 66.1% | return |
| f30 Dispatch.Rectangle.weight | 3 | dispatch.nra:50:48 | 127,200 | 69.6% | return |
| f34 Dispatch.Rectangle.mix | 1 | dispatch.nra:52:57 | 127,200 | 73.1% | return |
| f34 Dispatch.Rectangle.mix | 0 | dispatch.nra:52:65 | 127,200 | 76.5% | inline ReadField: no exit stays in the region (successor: return) |
| f29 Dispatch.Cube.weight | 5 | dispatch.nra:38:57 | 119,400 | 79.8% | return |
| f36 Dispatch.Circle.mix | 1 | dispatch.nra:78:57 | 112,800 | 82.8% | return |
| f32 Dispatch.Circle.weight | 3 | dispatch.nra:76:48 | 112,800 | 85.9% | return |
| f36 Dispatch.Circle.mix | 0 | dispatch.nra:78:72 | 112,800 | 89.0% | inline ReadField: no exit stays in the region (successor: return) |
| f31 Dispatch.Triangle.weight | 3 | dispatch.nra:64:48 | 111,000 | 92.0% | return |
| f35 Dispatch.Triangle.mix | 1 | dispatch.nra:66:57 | 111,000 | 95.0% | return |
### hash-map (size 200000)

Steps 52,186,850 (block steps; kernel-charged steps are not blocks); in regions 39,320,615 (75.3%); outside 12,866,235 (24.7%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 3,800,265 | 29.5% |
| state op AppendElement | 3,159,305 | 24.6% |
| call (through a dispatch block) | 2,350,081 | 18.3% |
| call (closure, through a dispatch block) | 900,000 | 7.0% |
| inline ReadTag: no exit stays in the region (successor: call) | 801,004 | 6.2% |
| inline ReadField: no exit stays in the region (successor: AppendElement) | 600,000 | 4.7% |
| call (direct) | 550,182 | 4.3% |
| object allocation | 150,020 | 1.2% |
| inline ReadField: region not formed yet (counting) | 140,429 | 1.1% |
| continue: region not formed yet (formed later) | 125,972 | 1.0% |
| continue: region not formed yet (counting) | 87,328 | 0.7% |
| inline ReadField: region not formed yet (formed later) | 45,982 | 0.4% |
| inline ReadLength: region not formed yet (counting) | 34,988 | 0.3% |
| inline ReadElement: region not formed yet (counting) | 32,775 | 0.3% |
| inline WriteField: region not formed yet (counting) | 28,309 | 0.2% |
| (other categories) | 59,595 | 0.5% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f32 HashCore.empty_index | 5 | std/collections/internal/hash-core.nra:299:13 | 1,048,568 | 8.1% | state op AppendElement |
| f43 HashMap.spread | 0 | std/collections/hash-map.nra:554:50 | 550,000 | 12.4% | return |
| f54 $closure1243<int64>.$invoke | 0 | std/collections/hash-map.nra:545:42 | 550,000 | 16.7% | return |
| f77 HashMap.reordered<int64> | 7 | std/collections/hash-map.nra:562:13 | 524,280 | 20.8% | state op AppendElement |
| f55 HashMap.HashMap<int64, int64>.find | 23 | std/collections/hash-map.nra:430:53 | 350,000 | 23.5% | call (closure, through a dispatch block) → $closure1247<int64>.$invoke |
| f62 $closure1247<int64>.$invoke | 0 | std/collections/hash-map.nra:550:44 | 350,000 | 26.2% | return |
| f55 HashMap.HashMap<int64, int64>.find | 25 | std/collections/hash-map.nra:431:21 | 350,000 | 28.9% | return |
| f52 HashMap.HashMapIterator<int64, int64>.has_next | 19 | std/collections/hash-map.nra:507:31 | 300,001 | 31.3% | return |
| f36 HashMap.HashMap<int64, int64>.set | 3 | std/collections/hash-map.nra:89:25 | 300,000 | 33.6% | call (direct) → HashMap.spread |
| f36 HashMap.HashMap<int64, int64>.set | 2 | std/collections/hash-map.nra:89:32 | 300,000 | 35.9% | call (closure, through a dispatch block) → $closure1243<int64>.$invoke |
| f36 HashMap.HashMap<int64, int64>.set | 5 | std/collections/hash-map.nra:90:25 | 300,000 | 38.3% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.find |
| f36 HashMap.HashMap<int64, int64>.set | 4 | std/collections/hash-map.nra:90:25 | 300,000 | 40.6% | inline ReadTag: no exit stays in the region (successor: call) |
| f96 HashCore.reordered_hashes | 7 | std/collections/internal/hash-core.nra:311:13 | 262,140 | 42.6% | state op AppendElement |
| f80 HashCore.HashCore.live_entries | 9 | std/collections/internal/hash-core.nra:175:21 | 262,140 | 44.7% | state op AppendElement |
| f97 HashCore.all_live | 3 | std/collections/internal/hash-core.nra:322:13 | 262,140 | 46.7% | state op AppendElement |
| f56 HashMap.HashMap<int64, int64>.insert | 20 | std/collections/hash-map.nra:447:13 | 200,000 | 48.3% | return |
| f1 HashMapBenchmark.main | 14 | hash-map.nra:24:29 | 200,000 | 49.8% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.get_or |
| f67 HashCore.HashCore.claim | 12 | std/collections/internal/hash-core.nra:133:13 | 200,000 | 51.4% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f56 HashMap.HashMap<int64, int64>.insert | 18 | std/collections/hash-map.nra:447:13 | 200,000 | 52.9% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f56 HashMap.HashMap<int64, int64>.insert | 17 | std/collections/hash-map.nra:446:13 | 200,000 | 54.5% | state op AppendElement |
| f56 HashMap.HashMap<int64, int64>.insert | 16 | std/collections/hash-map.nra:446:13 | 200,000 | 56.0% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f67 HashCore.HashCore.claim | 18 | std/collections/internal/hash-core.nra:136:13 | 200,000 | 57.6% | return |
| f67 HashCore.HashCore.claim | 11 | std/collections/internal/hash-core.nra:132:13 | 200,000 | 59.1% | state op AppendElement |
| f63 HashCore.HashCore.is_full | 5 | std/collections/internal/hash-core.nra:86:13 | 200,000 | 60.7% | return |
| f55 HashMap.HashMap<int64, int64>.find | 12 | std/collections/hash-map.nra:424:28 | 200,000 | 62.3% | return |
| f56 HashMap.HashMap<int64, int64>.insert | 19 | std/collections/hash-map.nra:447:13 | 200,000 | 63.8% | state op AppendElement |
| f1 HashMapBenchmark.main | 9 | hash-map.nra:20:13 | 200,000 | 65.4% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.set |
| f56 HashMap.HashMap<int64, int64>.insert | 15 | std/collections/hash-map.nra:445:13 | 200,000 | 66.9% | call (through a dispatch block) → HashCore.HashCore.claim |
| f36 HashMap.HashMap<int64, int64>.set | 14 | std/collections/hash-map.nra:95:13 | 200,000 | 68.5% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.insert |
| f36 HashMap.HashMap<int64, int64>.set | 15 | std/collections/hash-map.nra:95:13 | 200,000 | 70.0% | return |
| f67 HashCore.HashCore.claim | 13 | std/collections/internal/hash-core.nra:133:13 | 200,000 | 71.6% | state op AppendElement |
| f56 HashMap.HashMap<int64, int64>.insert | 2 | std/collections/hash-map.nra:441:17 | 200,000 | 73.1% | call (through a dispatch block) → HashCore.HashCore.is_full |
| f37 HashMap.HashMap<int64, int64>.get_or | 4 | std/collections/hash-map.nra:71:25 | 200,000 | 74.7% | inline ReadTag: no exit stays in the region (successor: call) |
| f37 HashMap.HashMap<int64, int64>.get_or | 2 | std/collections/hash-map.nra:71:42 | 200,000 | 76.2% | call (closure, through a dispatch block) → $closure1243<int64>.$invoke |
| f37 HashMap.HashMap<int64, int64>.get_or | 3 | std/collections/hash-map.nra:71:35 | 200,000 | 77.8% | call (direct) → HashMap.spread |
| f37 HashMap.HashMap<int64, int64>.get_or | 5 | std/collections/hash-map.nra:71:25 | 200,000 | 79.3% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.find |
| f37 HashMap.HashMap<int64, int64>.get_or | 13 | std/collections/hash-map.nra:75:13 | 200,000 | 80.9% | return |
| f1 HashMapBenchmark.main | 29 | hash-map.nra:33:9 | 150,001 | 82.1% | call (through a dispatch block) → HashMap.HashMapIterator<int64, int64>.has_next |
| f53 HashMap.HashMapIterator<int64, int64>.next | 15 | std/collections/hash-map.nra:517:13 | 150,000 | 83.2% | return |
| f53 HashMap.HashMapIterator<int64, int64>.next | 10 | std/collections/hash-map.nra:515:33 | 150,000 | 84.4% | object allocation |
| f53 HashMap.HashMapIterator<int64, int64>.next | 1 | std/collections/hash-map.nra:512:18 | 150,000 | 85.6% | call (through a dispatch block) → HashMap.HashMapIterator<int64, int64>.has_next |
| f1 HashMapBenchmark.main | 33 | hash-map.nra:33:9 | 150,000 | 86.7% | call (through a dispatch block) → HashMap.HashMapIterator<int64, int64>.next |
| f53 HashMap.HashMapIterator<int64, int64>.next | 0 | std/collections/hash-map.nra:512:18 | 150,000 | 87.9% | inline ReadTag: no exit stays in the region (successor: call) |
| f36 HashMap.HashMap<int64, int64>.set | 13 | std/collections/hash-map.nra:93:17 | 100,000 | 88.7% | return |
| f1 HashMapBenchmark.main | 20 | hash-map.nra:27:13 | 100,000 | 89.5% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.set |
| f38 HashMap.HashMap<int64, int64>.delete | 10 | std/collections/hash-map.nra:119:13 | 50,000 | 89.8% | return |
| f1 HashMapBenchmark.main | 25 | hash-map.nra:30:13 | 50,000 | 90.2% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.delete |
| f57 HashMap.HashMap<int64, int64>.release | 10 | std/collections/hash-map.nra:453:13 | 50,000 | 90.6% | return |
| f70 HashCore.HashCore.needs_compaction | 6 | std/collections/internal/hash-core.nra:164:13 | 50,000 | 91.0% | return |
| f38 HashMap.HashMap<int64, int64>.delete | 5 | std/collections/hash-map.nra:114:25 | 50,000 | 91.4% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.find |
| f38 HashMap.HashMap<int64, int64>.delete | 9 | std/collections/hash-map.nra:118:13 | 50,000 | 91.8% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.release |
| f68 HashCore.HashCore.release | 16 | std/collections/internal/hash-core.nra:157:13 | 50,000 | 92.2% | return |
| f38 HashMap.HashMap<int64, int64>.delete | 2 | std/collections/hash-map.nra:114:42 | 50,000 | 92.6% | call (closure, through a dispatch block) → $closure1243<int64>.$invoke |
| f57 HashMap.HashMap<int64, int64>.release | 4 | std/collections/hash-map.nra:452:13 | 50,000 | 93.0% | call (through a dispatch block) → HashMap.HashMap<int64, int64>.shorten |
| f38 HashMap.HashMap<int64, int64>.delete | 3 | std/collections/hash-map.nra:114:35 | 50,000 | 93.3% | call (direct) → HashMap.spread |
| f38 HashMap.HashMap<int64, int64>.delete | 4 | std/collections/hash-map.nra:114:25 | 50,000 | 93.7% | inline ReadTag: no exit stays in the region (successor: call) |
| f57 HashMap.HashMap<int64, int64>.release | 2 | std/collections/hash-map.nra:452:21 | 50,000 | 94.1% | call (through a dispatch block) → HashCore.HashCore.release |
| f57 HashMap.HashMap<int64, int64>.release | 3 | std/collections/hash-map.nra:452:13 | 50,000 | 94.5% | inline ReadTag: no exit stays in the region (successor: call) |
| f69 HashMap.HashMap<int64, int64>.shorten | 5 | std/collections/hash-map.nra:461:13 | 50,000 | 94.9% | return |
| f57 HashMap.HashMap<int64, int64>.release | 7 | std/collections/hash-map.nra:453:17 | 50,000 | 95.3% | call (through a dispatch block) → HashCore.HashCore.needs_compaction |
### object-churn (size 1000)

Steps 57,220,430 (block steps; kernel-charged steps are not blocks); in regions 35,856,924 (62.7%); outside 21,363,506 (37.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 7,578,308 | 35.5% |
| call (through a dispatch block) | 7,232,248 | 33.9% |
| state op AppendElement | 2,238,460 | 10.5% |
| inline ReadField: no exit stays in the region (successor: AppendElement) | 2,238,432 | 10.5% |
| inline ReadTag: no exit stays in the region (successor: call) | 1,339,711 | 6.3% |
| call (direct) | 346,058 | 1.6% |
| object allocation | 344,015 | 1.6% |
| (other categories) | 46,274 | 0.2% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f27 ArrayList.ArrayList<Particle>.size | 2 | std/collections/array-list.nra:30:42 | 2,328,409 | 10.9% | return |
| f29 ArrayList.ArrayList<Particle>.add | 0 | std/collections/array-list.nra:79:13 | 2,238,432 | 21.4% | inline ReadField: no exit stays in the region (successor: AppendElement) |
| f29 ArrayList.ArrayList<Particle>.add | 1 | std/collections/array-list.nra:79:13 | 2,238,432 | 31.9% | state op AppendElement |
| f29 ArrayList.ArrayList<Particle>.add | 2 | std/collections/array-list.nra:80:13 | 2,238,432 | 42.3% | return |
| f28 ArrayList.ArrayList<Particle>.get | 2 | std/collections/array-list.nra:36:51 | 2,237,408 | 52.8% | return |
| f1 ObjectChurn.main | 46 | object-churn.nra:59:33 | 1,247,704 | 58.6% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.size |
| f1 ObjectChurn.main | 51 | object-churn.nra:60:33 | 1,246,704 | 64.5% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.get |
| f1 ObjectChurn.main | 61 | object-churn.nra:63:21 | 991,728 | 69.1% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.add |
| f1 ObjectChurn.main | 13 | object-churn.nra:47:33 | 991,704 | 73.8% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.size |
| f1 ObjectChurn.main | 18 | object-churn.nra:48:32 | 990,704 | 78.4% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.get |
| f1 ObjectChurn.main | 20 | object-churn.nra:48:17 | 990,704 | 83.0% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.add |
| f1 ObjectChurn.main | 19 | object-churn.nra:48:17 | 990,704 | 87.7% | inline ReadTag: no exit stays in the region (successor: call) |
| f30 ObjectChurn.Random.next | 3 | object-churn.nra:36:13 | 342,000 | 89.3% | return |
| f6 ObjectChurn.Particle.init | 3 | object-churn.nra:27:13 | 342,000 | 90.9% | return |
| f1 ObjectChurn.main | 28 | object-churn.nra:52:45 | 256,000 | 92.1% | call (through a dispatch block) → ObjectChurn.Random.next |
| f1 ObjectChurn.main | 29 | object-churn.nra:52:32 | 256,000 | 93.3% | object allocation |
| f1 ObjectChurn.main | 31 | object-churn.nra:52:17 | 256,000 | 94.5% | inline ReadTag: no exit stays in the region (successor: call) |
| f1 ObjectChurn.main | 32 | object-churn.nra:52:17 | 256,000 | 95.7% | call (through a dispatch block) → ArrayList.ArrayList<Particle>.add |
### parse-results (size 500)

Steps 44,722,855 (block steps; kernel-charged steps are not blocks); in regions 39,735,132 (88.8%); outside 4,987,723 (11.2%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 1,532,987 | 30.7% |
| call (direct) | 1,532,925 | 30.7% |
| object allocation | 1,381,019 | 27.7% |
| text building (TextSlice) | 290,506 | 5.8% |
| inline WriteField: no exit stays in the region (successor: return) | 109,500 | 2.2% |
| continue: region not formed yet (counting) | 39,038 | 0.8% |
| state op AppendElement | 18,621 | 0.4% |
| inline ReadField: region not formed yet (formed later) | 16,994 | 0.3% |
| continue: region not formed yet (formed later) | 12,993 | 0.3% |
| inline WriteField: region not formed yet (counting) | 11,081 | 0.2% |
| inline WriteField: region not formed yet (formed later) | 8,991 | 0.2% |
| inline ReadElement: region not formed yet (counting) | 5,426 | 0.1% |
| inline ReadLength: region not formed yet (counting) | 5,237 | 0.1% |
| (other categories) | 22,405 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f1 ParseResults.main | 51 | parse-results.nra:129:24 | 500,000 | 10.0% | call (direct) → ParseResults.separator_of |
| f1 ParseResults.main | 57 | parse-results.nra:134:56 | 440,500 | 18.9% | call (direct) → ParseResults.parse_int |
| f5 ParseResults.separator_of | 9 | parse-results.nra:28:24 | 440,500 | 27.7% | object allocation |
| f5 ParseResults.separator_of | 14 | parse-results.nra:28:17 | 440,500 | 36.5% | return |
| f1 ParseResults.main | 63 | parse-results.nra:136:40 | 290,500 | 42.3% | call (direct) → ParseResults.key_weight |
| f1 ParseResults.main | 62 | parse-results.nra:136:51 | 290,500 | 48.2% | text building (TextSlice) |
| f6 ParseResults.parse_int | 20 | parse-results.nra:65:16 | 237,000 | 52.9% | object allocation |
| f6 ParseResults.parse_int | 26 | parse-results.nra:65:9 | 237,000 | 57.7% | return |
| f1 ParseResults.main | 72 | parse-results.nra:147:43 | 150,000 | 60.7% | call (direct) → ParseResults.error_offset |
| f11 ParseResults.parse_error | 8 | parse-results.nra:35:9 | 150,000 | 63.7% | return |
| f11 ParseResults.parse_error | 0 | parse-results.nra:35:41 | 150,000 | 66.7% | object allocation |
| f11 ParseResults.parse_error | 5 | parse-results.nra:35:16 | 150,000 | 69.7% | object allocation |
| f8 ParseResults.error_offset | 4 | parse-results.nra:88:17 | 150,000 | 72.7% | return |
| f7 ParseResults.key_weight | 12 | parse-results.nra:74:13 | 62,000 | 74.0% | return |
| f7 ParseResults.key_weight | 8 | parse-results.nra:74:20 | 62,000 | 75.2% | object allocation |
| f7 ParseResults.key_weight | 6 | parse-results.nra:71:13 | 61,000 | 76.4% | return |
| f7 ParseResults.key_weight | 2 | parse-results.nra:71:20 | 61,000 | 77.6% | object allocation |
| f5 ParseResults.separator_of | 4 | parse-results.nra:31:16 | 59,500 | 78.8% | object allocation |
| f5 ParseResults.separator_of | 15 | parse-results.nra:31:16 | 59,500 | 80.0% | inline WriteField: no exit stays in the region (successor: return) |
| f5 ParseResults.separator_of | 16 | parse-results.nra:31:9 | 59,500 | 81.2% | return |
| f7 ParseResults.key_weight | 18 | parse-results.nra:77:13 | 59,000 | 82.4% | return |
| f7 ParseResults.key_weight | 14 | parse-results.nra:77:20 | 59,000 | 83.6% | object allocation |
| f7 ParseResults.key_weight | 24 | parse-results.nra:80:13 | 58,500 | 84.8% | return |
| f6 ParseResults.parse_int | 16 | parse-results.nra:57:24 | 58,500 | 85.9% | call (direct) → ParseResults.parse_error |
| f6 ParseResults.parse_int | 18 | parse-results.nra:57:17 | 58,500 | 87.1% | return |
| f7 ParseResults.key_weight | 20 | parse-results.nra:80:20 | 58,500 | 88.3% | object allocation |
| f6 ParseResults.parse_int | 19 | parse-results.nra:63:44 | 53,500 | 89.3% | object allocation |
| f6 ParseResults.parse_int | 23 | parse-results.nra:63:13 | 53,500 | 90.4% | return |
| f7 ParseResults.key_weight | 26 | parse-results.nra:82:9 | 50,000 | 91.4% | return |
| f7 ParseResults.key_weight | 21 | parse-results.nra:82:16 | 50,000 | 92.4% | object allocation |
| f7 ParseResults.key_weight | 25 | parse-results.nra:82:16 | 50,000 | 93.4% | inline WriteField: no exit stays in the region (successor: return) |
| f6 ParseResults.parse_int | 6 | parse-results.nra:48:20 | 49,500 | 94.4% | call (direct) → ParseResults.parse_error |
| f6 ParseResults.parse_int | 8 | parse-results.nra:48:13 | 49,500 | 95.4% | return |
### sieve (size 3000000)

Steps 41,695,696 (block steps; kernel-charged steps are not blocks); in regions 38,664,445 (92.7%); outside 3,031,251 (7.3%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| state op AppendElement | 3,000,026 | 99.0% |
| continue: region not formed yet (counting) | 10,578 | 0.3% |
| continue: region not formed yet (formed later) | 9,997 | 0.3% |
| (other categories) | 10,650 | 0.4% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f4 Lists.filled<bool> | 5 | std/collections/lists.nra:114:13 | 3,000,001 | 99.0% | state op AppendElement |
### sort (size 250000)

Steps 73,316,797 (block steps; kernel-charged steps are not blocks); in regions 55,874,260 (76.2%); outside 17,442,537 (23.8%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 8,382,530 | 48.1% |
| call (direct) | 4,199,105 | 24.1% |
| call (closure, through a dispatch block) | 4,183,423 | 24.0% |
| state op AppendElement | 500,014 | 2.9% |
| continue: region not formed yet (counting) | 88,755 | 0.5% |
| continue: region not formed yet (formed later) | 37,990 | 0.2% |
| (other categories) | 50,720 | 0.3% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f32 $closure779.$invoke | 0 | sort.nra:23:36 | 4,183,423 | 24.0% | call (direct) → Int32.compare |
| f32 $closure779.$invoke | 1 | sort.nra:23:36 | 4,183,423 | 48.0% | return |
| f25 Sorting.merge_pass<int32> | 28 | std/collections/sorting.nra:84:34 | 3,464,975 | 67.8% | call (closure, through a dispatch block) → $closure779.$invoke |
| f35 Int32.compare | 3 | std/core/int32.nra:69:13 | 2,125,382 | 80.0% | return |
| f35 Int32.compare | 1 | std/core/int32.nra:66:13 | 2,028,202 | 91.6% | return |
| f17 Sorting.insertion_sort<int32> | 9 | std/collections/sorting.nra:26:30 | 702,824 | 95.7% | call (closure, through a dispatch block) → $closure779.$invoke |
### spectral-norm (size 64)

Steps 1,653,450 (block steps; kernel-charged steps are not blocks); in regions 1,309,334 (79.2%); outside 344,116 (20.8%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| return | 163,931 | 47.6% |
| call (direct) | 163,929 | 47.6% |
| continue: region not formed yet (counting) | 8,116 | 2.4% |
| continue: region not formed yet (formed later) | 4,995 | 1.5% |
| inline ReadElement: region not formed yet (formed later) | 1,998 | 0.6% |
| (other categories) | 1,147 | 0.3% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f9 SpectralNorm.entry | 2 | spectral-norm.nra:15:24 | 163,840 | 47.6% | return |
| f6 SpectralNorm.multiply_transposed | 6 | spectral-norm.nra:34:29 | 81,920 | 71.4% | call (direct) → SpectralNorm.entry |
| f5 SpectralNorm.multiply | 6 | spectral-norm.nra:23:29 | 81,920 | 95.2% | call (direct) → SpectralNorm.entry |
### string-building (size 400000)

Steps 63,578,690 (block steps; kernel-charged steps are not blocks); in regions 54,011,053 (85.0%); outside 9,567,637 (15.0%).

| Category | Steps outside | Share |
| --- | ---: | ---: |
| state op AppendElement | 4,688,924 | 49.0% |
| return | 1,600,061 | 16.7% |
| call (through a dispatch block) | 1,600,000 | 16.7% |
| inline ReadTag: no exit stays in the region (successor: call) | 1,208,003 | 12.6% |
| text building (ScalarText) | 400,000 | 4.2% |
| continue: region not formed yet (counting) | 25,045 | 0.3% |
| (other categories) | 45,604 | 0.5% |

| Function | Block | Source | Steps outside | Cum. | Why outside |
| --- | ---: | --- | ---: | ---: | --- |
| f27 StringBuilder.StringBuilder.append | 7 | std/core/string-builder.nra:35:17 | 4,688,924 | 49.0% | state op AppendElement |
| f27 StringBuilder.StringBuilder.append | 9 | std/core/string-builder.nra:37:13 | 1,200,005 | 61.6% | return |
| f28 StringBuilder.StringBuilder.append_int | 0 | std/core/string-builder.nra:86:75 | 400,000 | 65.7% | text building (ScalarText) |
| f28 StringBuilder.StringBuilder.append_int | 2 | std/core/string-builder.nra:86:68 | 400,000 | 69.9% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 StringBuilding.main | 9 | string-building.nra:18:13 | 400,000 | 74.1% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 StringBuilding.main | 13 | string-building.nra:18:13 | 400,000 | 78.3% | call (through a dispatch block) → StringBuilder.StringBuilder.append |
| f1 StringBuilding.main | 11 | string-building.nra:18:13 | 400,000 | 82.5% | call (through a dispatch block) → StringBuilder.StringBuilder.append_int |
| f1 StringBuilding.main | 10 | string-building.nra:18:13 | 400,000 | 86.6% | inline ReadTag: no exit stays in the region (successor: call) |
| f1 StringBuilding.main | 12 | string-building.nra:18:13 | 400,000 | 90.8% | inline ReadTag: no exit stays in the region (successor: call) |
| f28 StringBuilder.StringBuilder.append_int | 1 | std/core/string-builder.nra:86:68 | 400,000 | 95.0% | inline ReadTag: no exit stays in the region (successor: call) |
| f28 StringBuilder.StringBuilder.append_int | 3 | std/core/string-builder.nra:86:68 | 400,000 | 99.2% | return |

collatz, fannkuch, mandelbrot, matrix-multiply (and nbody at full size):
fewer than 3% of steps outside regions, nearly all warm-up before the
region formed (tables in `census-blocks.md`); at long size 0.0%, 0.0%, 0.0%
and 2.4% of their steps are outside regions (A1).

## A3. What a step costs where

### The four programs at long size

Clean CPU (no `--tier-stats`) at long size, and the `--tier-stats` run:

| Program | CPU s, clean | CPU s, --tier-stats | lead's wall s (bench-wall, one run) | Java wall s |
| --- | ---: | ---: | ---: | ---: |
| binary-trees | 169.1 | 153.7 | 140.1 | 1.93 |
| nbody | 807.7 | 736.5 | 676.0 | 2.76 |
| sha256 | 1410.8 | 1325.2 | 1291.3 | 2.75 |
| text-split | 334.5 | 329.8 | 376.8 | 3.00 |

The clean and `--tier-stats` runs are single runs on a machine shared with another agent; they differ by up to 10% in either direction (binary-trees ran slower without `--tier-stats`).

Where the time goes. Each column is a count from the long run's
`--tier-stats` (region steps, entries, tier-1 steps; kernel calls and
inline-state helper calls per step from the census) times a unit cost in
instructions from callgrind (at the size in `cg/measured.txt`), converted at
that program's ns per instruction; collection and proving are the runtime's
own wall-clock figures. The sum is within 3 to 12% of the measured CPU time
(the `--tier-stats` run's clock readings, two per region entry, are not in
the model; nor is the noise of a shared machine, which reached 35% between
two clean runs of nbody at 250000):

| Program (long) | CPU s | native code in regions | inline-state helpers in regions | region entries | tier-1 steps (dispatch + commit) | kernels | collection | proving + load | sum | measured regions / tier 1 (time line) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| sha256 | 1325.2 | 17.7 (60.3 instr/step) | 28.9 (0.45/step x 219 instr) | 32.3 (369 instr/entry) | 1082.6 (967 instr/step) | 0.0 (14244 instr/call) | 0.0 | 0.1 | 1161.6 | 168.0 / 1168.9 |
| nbody | 736.5 | 84.6 (24.6 instr/step) | 498.0 (0.61/step x 238 instr) | 84.3 (2011 instr/entry) | 36.1 (860 instr/step) | 51.9 (1486 instr/call) | 0.0 | 0.2 | 755.1 | 562.0 / 181.3 |
| text-split | 329.8 | 6.7 (36.7 instr/step) | 17.4 (0.45/step x 210 instr) | 11.8 (324 instr/entry) | 58.0 (1000 instr/step) | 3.0 (1014 instr/call) | 192.1 | 0.1 | 289.1 | 67.0 / 73.2 |
| binary-trees | 153.7 | 2.1 (40.2 instr/step) | 10.1 (0.77/step x 248 instr) | 4.2 (179 instr/entry) | 92.1 (835 instr/step) | 0.0 (13584 instr/call) | 28.2 | 0.0 | 136.7 | 33.4 / 93.0 |

Instructions are converted at each program's ns per instruction (clean CPU / total instructions of its callgrind run, with the fastest of three clean runs): sha256 0.0751, nbody 0.0998, text-split 0.0820, binary-trees 0.0850. Kernel calls run inside tier-1 commits; they are taken out of the tier-1 column and shown on their own.

Unit costs in ns (`units.py`): callgrind instructions at the program's ns per
instruction for the first seven rows; census-timer differences (each timed
call carries 15 to 30 ns of timer overhead, which the difference with a
continue commit cancels only roughly: differences under about 15 ns,
including the negative ones, mean "about zero"; rows with few calls are
noise) for the rest:

| ns per unit | sha256 | nbody | text-split | binary-trees |
| --- | ---: | ---: | ---: | ---: |
| native code, per region step | 4.5 | 2.5 | 3.0 | 3.4 |
| region entry (run_region own) | 27.7 | 200.8 | 26.5 | 15.2 |
| inline state op in a region (helper call) | 16.5 | 23.8 | 17.2 | 21.1 |
| dispatcher lookup between blocks (run_regions) | 8.6 | 24.9 | 15.4 | 11.1 |
| tier-1 step, dispatch and commit | 64.1 | 61.0 | 66.6 | 59.8 |
|   of which prepare_step | 9.8 | 11.9 | 9.9 | 10.2 |
|   of which lift::execute (lifted steps) | 18.5 | 13.3 | 15.0 | 15.0 |
| call commit beyond a continue commit (timers) | 56.1 | 137.4 | 120.9 | 55.5 |
| return commit beyond a continue commit (timers) | -9.2 | 6.6 | -3.9 | 7.4 |
| object allocation beyond a continue commit (timers, with collection) | 301.2 | 516.8 | 388.9 | 384.9 |
| AppendElement beyond a continue commit (timers) | 28.7 | 34.9 | 73.2 | 95.1 |
| kernel call beyond a continue commit (timers) | 1806.1 | 430.0 | 280.4 | 3694.4 |
| one collection (timers), and collections at the census size | 3.8 µs x 19 | 3.7 µs x 5 | 31.0 µs x 71,069 | 33109.0 µs x 90 |

The callgrind counts behind them (`cga3.py`), instructions:

```
sha256 size 50000: total 3,074,854,197 instructions; block steps 3,142,098: in regions 617,551 (184,426 entries), tier 1 2,524,547 (lifted 1,821,627)
  generated code: 37,241,354 = 60.3 per region step
  run_region own (entry, slot loads, write-back, accounting): 68,005,042 = 369 per entry
  inline-state helpers (jit_state incl.): 60,980,455 = 98.7 per region step
  run_regions outside run_region (region lookups between blocks): 287,132,007 = 114 per tier-1 step
  tier_step incl. minus proving, collection, kernels: 2,153,143,124 = 853 per tier-1 step
    commit_exit incl.: 987,910,438 (391/step), of which state() 99,250,736, push_frame 225,182,593
    lift::execute: 448,826,624 = 246 per lifted tier-1 step
    prepare_step incl.: 328,956,151 = 130/step; block_plan excl. 218,539,422; read excl. 255,684,821
  allocate incl. 116,635; collect incl. 160,624; run_kernel incl. 37,127; proving incl. 223,021,819; TierTimes::enter incl. 8,898,768
nbody size 250000: total 55,840,841,131 instructions; block steps 249,002,170: in regions 245,888,676 (2,991,265 entries), tier 1 3,113,494 (lifted 323,717)
  generated code: 6,051,137,467 = 24.6 per region step
  run_region own (entry, slot loads, write-back, accounting): 6,015,883,679 = 2011 per entry
  inline-state helpers (jit_state incl.): 35,607,443,870 = 144.8 per region step
  run_regions outside run_region (region lookups between blocks): 774,631,019 = 249 per tier-1 step
  tier_step incl. minus proving, collection, kernels: 1,900,928,130 = 611 per tier-1 step
    commit_exit incl.: 4,625,704,380 (1486/step), of which state() 28,701,346,654, push_frame 74,861,889
    lift::execute: 43,172,986 = 133 per lifted tier-1 step
    prepare_step incl.: 369,684,292 = 119/step; block_plan excl. 592,883,939; read excl. 227,755,536
  allocate incl. 111,893; collect incl. 67,230; run_kernel incl. 3,715,345,227; proving incl. 1,113,882,097; TierTimes::enter incl. 143,627,424
text-split size 15000: total 10,461,754,269 instructions; block steps 22,014,063: in regions 16,638,358 (3,310,802 entries), tier 1 5,375,705 (lifted 1,538,733)
  generated code: 610,452,109 = 36.7 per region step
  run_region own (entry, slot loads, write-back, accounting): 1,071,188,612 = 324 per entry
  inline-state helpers (jit_state incl.): 1,587,231,769 = 95.4 per region step
  run_regions outside run_region (region lookups between blocks): 1,013,025,775 = 188 per tier-1 step
  tier_step incl. minus proving, collection, kernels: 4,363,788,855 = 812 per tier-1 step
    commit_exit incl.: 3,141,338,195 (584/step), of which state() 2,686,681,408, push_frame 244,760,501
    lift::execute: 280,876,290 = 183 per lifted tier-1 step
    prepare_step incl.: 649,501,023 = 121/step; block_plan excl. 806,704,957; read excl. 438,831,201
  allocate incl. 327,342,270; collect incl. 256,416,781; run_kernel incl. 273,812,886; proving incl. 705,030,967; TierTimes::enter incl. 159,634,656
binary-trees size 13: total 14,249,419,569 instructions; block steps 18,903,881: in regions 6,090,082 (2,705,827 entries), tier 1 12,813,799 (lifted 4,031,682)
  generated code: 244,794,319 = 40.2 per region step
  run_region own (entry, slot loads, write-back, accounting): 485,155,571 = 179 per entry
  inline-state helpers (jit_state incl.): 1,171,435,976 = 192.4 per region step
  run_regions outside run_region (region lookups between blocks): 1,678,333,700 = 131 per tier-1 step
  tier_step incl. minus proving, collection, kernels: 9,023,488,817 = 704 per tier-1 step
    commit_exit incl.: 5,860,546,310 (457/step), of which state() 3,424,778,995, push_frame 895,876,985
    lift::execute: 714,707,341 = 177 per lifted tier-1 step
    prepare_step incl.: 1,537,732,339 = 120/step; block_plan excl. 1,316,087,060; read excl. 529,852,426
  allocate incl. 1,334,179,074; collect incl. 775,621,782; run_kernel incl. 42,819; proving incl. 26,176,944; TierTimes::enter incl. 129,911,712
```

What a step costs where:

- **A native step** executes 25 (nbody) to 60 (sha256) instructions of
  generated code, about 2 to 4.5 ns, including the Budget, branch, store and
  jump overhead of its block. nbody's blocks are long float blocks, sha256's
  are short `rotate`/`add` bodies and their checks.
- **An inline state operation inside a region** (ReadField, ReadElement,
  WriteElement, ReadGlobal, ...) is a helper call of about 210 to 250
  instructions (`jit_state`: argument vector, `try`, the `state()` switch,
  `typed_entry` with `TypeSystem::derives`), about 18 to 24 ns. nbody does
  0.61 of them per region step: **498 of its 737 s at long size**; fannkuch,
  sort and the other array programs pay the same per element access.
- **A region entry** costs 180 to 370 instructions in `run_region` (slot
  loads, entry checks, write-back, accounting) plus the dispatcher's lookup,
  and 2011 for nbody's 286-block region with its many slot registers: 84 s
  at long size, because the `sqrt` kernel leaves the region ten times per
  simulation step.
- **A step at tier 1**, between regions, costs 830 to 1000 instructions,
  70 to 100 ns: 110 to 250 in `run_regions` looking for a region,
  `prepare_step` (120: the step charge, `block_plan`), `lift::execute` (130
  to 250 per lifted block: it interprets the lowered block's instructions),
  `read` of every value, and `commit_exit` (390 to 580 including its state
  operation). `block_plan` alone is 7 to 9% of all instructions of
  binary-trees and sha256: it is called several times per step.
- **Calls and returns at tier 1**: `push_frame` is 145 to 220 instructions
  (two vector resizes, the argument vector); the call's commit costs 55 ns
  (direct calls) to 140 ns (through a dispatch block, with its reference
  check) more than a continue commit, and a return about nothing more
  (census timers, differences).
- **Allocation**: an object allocation costs about 186 ns beyond a
  continue commit in binary-trees, collection excluded (385 ns with its
  share of collection; census timers; callgrind: `allocate` 410
  instructions plus the `HeapEntry`'s two vectors, two `malloc`s), and 230
  to 270 ns in the programs that allocate fewer objects; `AppendElement` 25
  to 95 ns.
- **Collection**: binary-trees at long size collects for 28.2 s (its live
  heap reaches 18.7 million cells). text-split at long size collects for
  **192 s of 330 s**, with a peak heap of 2,937 cells: 7,120 collections at
  full size cost 5.8 µs each and 71,069 at ten times that size 31 µs each
  (census timers), and at long size they average about 200 µs (derived:
  192 s over about 950,000 collections, the full size's count scaled by
  the steps).
  By reading: `HeapTable::sweep` (machine.cpp:675-689) visits every page
  slot of `pages_`, which grows with the highest handle ever assigned
  (handles are never reused; `insert`, machine.cpp:651-653), so each collection's cost
  grows with the number of allocations the run has made, not with the live
  heap. parse-results shows the same at long size: 106 s of collection
  (of 324 s) with a peak heap of 23,067 cells. Function programs do not
  change this.
- **Kernels**: `run_kernel` costs about 1,000 to 1,500 instructions per call
  for nbody's `sqrt` (1486) and text-split's text kernels (1014), before the
  kernel's own work. Priced at the program's average ns per instruction that
  is 150 ns for `sqrt` (52 s of nbody at long size, the table above); the
  census timers measure 430 ns per `sqrt` commit beyond a continue commit
  (150 s at long size), which A4 uses. The difference is unexplained; the
  kernel path's buffers (`kernel_buffers_`, the argument arrays) may run at
  a lower rate per instruction than the program's average.


## A4. Estimates

### The model

For each program and size, with N the block steps of the `--tier-stats` run
of that size, and per-step rates of every kind of operation from the census
(full size, or ten times the full size for nbody and text-split) scaled to N:

- **F**, the floor no form of compiled code changes: load + proving +
  searching + collection + host of the `tier: time:` line.
- **s**, the native cost of a step, and **h**, the cost of an inline state
  operation as today's helper call: today's measured ns per region step at
  that size (regions time / steps in regions, scaled to a clean run), split
  in proportion to callgrind's instructions per region step: generated code,
  region entries, inline-state helper calls. The same ns per instruction
  prices the inline forms of (b). This charges native code with its share of
  every overhead that region time contains (the dispatcher's lookup before
  each entry, the `--tier-stats` clock readings), so it does not flatter
  call-heavy programs. The columns "(a) alt" and "(b) alt" price the same
  instruction counts at the program's average ns per instruction instead
  (callgrind total / fastest clean CPU at the callgrind size), which is lower
  for call-heavy programs and higher for the programs whose full-size run is
  mostly proving (mandelbrot, spectral-norm, matrix-multiply); the truth is
  most likely between the two.
- **Helper operations** (census timers, the commit of that kind minus a
  continue commit; collection excluded, it is in F): calls and returns in
  (a); object and collection allocation, `AppendElement`, text building,
  kernels and the other growth and freeing operations in both (a) and (b).
- **Inline forms (b)**, derived by reading what each must do, not measured
  (no such code exists): a call **50 instructions** (argument moves, the
  frame arena bump with header and zeroed slots, the step flush, the jump),
  a return **30** (the pop, the result store, the return-slot width check),
  an inline state operation **15** (the `HeapTable` page lookup, the kind and
  type check, the `initialized` or bounds check, the access).

So, with every step in a function program:

- (a) = F + N·s + (inline state operations)·h + Σ (helper operations)·(their measured cost);
- (b) = F + N·s + (calls·50 + returns·30 + inline state operations·15)
  instructions + Σ (allocation, growth, text, kernels)·(their measured cost).

"Today" is the clean CPU time where it was measured: full size, the median of
three; long size, the smaller of one clean and one `--tier-stats` run for
the four programs measured both ways (†); for the other thirteen, the
`--tier-stats` run's CPU with everything outside F scaled by the full size's
clean/stats ratio (*). "All steps at c_r" is F plus every step at today's
measured cost per region step, region entries included, for reference.
"(b) / Java" divides (b) by the Java time of the lead's long run
(`build/bench-m1/long/bench-wall.json`, 13 November). Every term of every row
is printed below the table.

| Program | Size | Today CPU s | (a) s | (b) s | today/(a) | today/(b) | floor F s (collection) | helpers kept in (b) s | all steps at c_r s | (b) / Java (long) | (a) alt s | (b) alt s |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | full | 14.89 | 12.69 | 7.80 | 1.2 | 1.9 | 3.05 (3.03) | 2.79 | 12.83 |  | 10.96 | 6.96 |
| binary-trees | long | 153.68† | 122.14 | 74.49 | 1.3 | 2.1 | 28.26 (28.24) | 25.42 | 131.94 | 38.6 | 100.36 | 63.87 |
| closures | full | 0.55 | 0.41 | 0.18 | 1.4 | 3.0 | 0.10 (0.00) | 0.02 | 0.40 |  | 0.35 | 0.16 |
| closures | long | 248.95* | 164.55 | 46.13 | 1.5 | 5.4 | 1.38 (1.28) | 9.12 | 166.02 | 18.0 | 125.48 | 31.77 |
| collatz | full | 0.15 | 0.14 | 0.14 | 1.0 | 1.0 | 0.03 (0.00) | 0.00 | 0.14 |  | 0.14 | 0.14 |
| collatz | long | 12.34* | 12.42 | 12.42 | 1.0 | 1.0 | 0.04 (0.00) | 0.01 | 12.41 | 3.9 | 11.92 | 11.92 |
| dispatch | full | 0.54 | 0.33 | 0.13 | 1.6 | 4.0 | 0.07 (0.00) | 0.00 | 0.36 |  | 0.28 | 0.12 |
| dispatch | long | 117.01* | 65.28 | 16.10 | 1.8 | 7.3 | 0.07 (0.00) | 0.09 | 72.23 | 4.9 | 51.96 | 11.58 |
| fannkuch | full | 0.73 | 0.73 | 0.20 | 1.0 | 3.7 | 0.02 (0.00) | 0.00 | 0.73 |  | 0.72 | 0.19 |
| fannkuch | long | 114.99* | 115.69 | 28.40 | 1.0 | 4.0 | 0.03 (0.00) | 0.01 | 115.67 | 8.6 | 102.90 | 25.27 |
| hash-map | full | 2.03 | 1.41 | 0.45 | 1.4 | 4.5 | 0.06 (0.01) | 0.10 | 1.51 |  | 1.12 | 0.36 |
| hash-map | long | 25.30* | 17.71 | 5.13 | 1.4 | 4.9 | 0.07 (0.03) | 1.23 | 19.00 | 2.1 | 13.67 | 3.84 |
| mandelbrot | full | 0.25 | 0.19 | 0.19 | 1.3 | 1.3 | 0.18 (0.00) | 0.00 | 0.19 |  | 0.19 | 0.19 |
| mandelbrot | long | 21.78* | 22.27 | 18.90 | 1.0 | 1.2 | 0.19 (0.00) | 0.18 | 21.84 | 5.3 | 54.63 | 46.46 |
| matrix-multiply | full | 0.12 | 0.10 | 0.08 | 1.2 | 1.5 | 0.07 (0.00) | 0.00 | 0.10 |  | 0.12 | 0.09 |
| matrix-multiply | long | 80.98* | 83.68 | 30.69 | 1.0 | 2.6 | 0.09 (0.00) | 1.15 | 81.28 | 13.1 | 94.24 | 34.47 |
| nbody | full | 0.28 | 0.24 | 0.22 | 1.1 | 1.3 | 0.20 (0.00) | 0.01 | 0.24 |  | 0.26 | 0.22 |
| nbody | long | 736.50† | 647.67 | 249.53 | 1.1 | 3.0 | 0.17 (0.00) | 150.50 | 569.03 | 90.5 | 739.34 | 267.95 |
| object-churn | full | 2.46 | 1.60 | 0.56 | 1.5 | 4.4 | 0.11 (0.05) | 0.09 | 1.88 |  | 1.25 | 0.43 |
| object-churn | long | 299.32* | 188.23 | 69.29 | 1.6 | 4.3 | 16.07 (16.01) | 9.72 | 233.67 | 28.6 | 133.07 | 48.78 |
| parse-results | full | 1.26 | 1.16 | 0.67 | 1.1 | 1.9 | 0.17 (0.08) | 0.32 | 0.88 |  | 1.10 | 0.65 |
| parse-results | long | 275.47* | 253.92 | 179.13 | 1.1 | 1.5 | 105.83 (105.74) | 44.09 | 219.03 | 77.9 | 235.23 | 172.56 |
| sha256 | full | 1.88 | 1.08 | 0.42 | 1.7 | 4.5 | 0.04 (0.00) | 0.01 | 1.15 |  | 0.86 | 0.26 |
| sha256 | long | 1325.18† | 686.48 | 273.92 | 1.9 | 4.8 | 0.07 (0.00) | 8.62 | 810.01 | 99.7 | 490.55 | 133.03 |
| sieve | full | 0.56 | 0.40 | 0.22 | 1.4 | 2.5 | 0.03 (0.00) | 0.09 | 0.38 |  | 0.38 | 0.22 |
| sieve | long | 29.07* | 22.58 | 11.12 | 1.3 | 2.6 | 0.03 (0.00) | 4.21 | 22.25 | 5.6 | 17.11 | 9.07 |
| sort | full | 2.29 | 1.59 | 0.45 | 1.4 | 5.1 | 0.05 (0.00) | 0.01 | 1.69 |  | 1.41 | 0.36 |
| sort | long | 177.60* | 122.61 | 31.86 | 1.4 | 5.6 | 0.04 (0.00) | 1.01 | 132.88 | 9.6 | 107.36 | 24.76 |
| spectral-norm | full | 0.19 | 0.19 | 0.17 | 1.0 | 1.1 | 0.17 (0.00) | 0.00 | 0.18 |  | 0.19 | 0.18 |
| spectral-norm | long | 81.71* | 79.61 | 20.15 | 1.0 | 4.1 | 0.12 (0.00) | 0.26 | 52.60 | 5.7 | 116.22 | 49.45 |
| string-building | full | 1.49 | 1.07 | 0.48 | 1.4 | 3.1 | 0.03 (0.01) | 0.23 | 0.96 |  | 0.98 | 0.45 |
| string-building | long | 113.34* | 79.80 | 34.19 | 1.4 | 3.3 | 0.79 (0.76) | 16.22 | 73.49 | 12.3 | 67.92 | 30.25 |
| text-split | full | 0.88 | 0.85 | 0.57 | 1.0 | 1.6 | 0.13 (0.04) | 0.32 | 0.60 |  | 0.78 | 0.54 |
| text-split | long | 329.85† | 302.52 | 255.95 | 1.1 | 1.3 | 192.25 (192.14) | 43.12 | 280.49 | 85.3 | 278.76 | 246.40 |

- binary-trees full: today 14.889 s (clean). F = load 0.017 + proving 0.005 + searching 0.000 + collection 3.030 + host 0.000 = 3.052 s. N = 209,899,833 block steps, 67,647,489 in regions today, 30,056,179 region entries. Today's ns per region step c_r = 46.60 (regions 3.669 s x 0.859). s_pure = 6.00 ns (measured: 40.2 of 312.2 instructions per region step); inline state ops 74,760,286 (helper today 37.1 ns, inline 15 instr); ns/instr 0.1492. Helper ops: call 44,958,281 x 55 ns, return 44,958,282 x 7 ns, alloc_object 14,986,017 x 186 ns, alloc_collection 19 x 504 ns, append 332 x 95 ns, text 43 x 641 ns, kernel 35 x 3694 ns. (a) = 3.052 + 1.259 + 2.770 + 5.612 = 12.693 s. (b) = 3.052 + 1.259 + 0.704 (calls x 50, returns x 30, inline state x 15 instr) + 2.788 (allocation, growth, text, kernels) = 7.803 s. All steps at c_r: 12.832 s.
- binary-trees long: today 153.675 s (min of clean and --tier-stats runs). F = load 0.017 + proving 0.005 + searching 0.000 + collection 28.236 + host 0.000 = 28.258 s. N = 1,914,011,851 block steps, 616,729,951 in regions today, 274,024,867 region entries. Today's ns per region step c_r = 54.17 (regions 33.410 s x 1.000). s_pure = 6.97 ns (measured: 40.2 of 312.3 instructions per region step); inline state ops 681,715,994 (helper today 43.1 ns, inline 15 instr); ns/instr 0.1735. Helper ops: call 409,960,701 x 55 ns, return 409,960,710 x 7 ns, alloc_object 136,652,868 x 186 ns, alloc_collection 173 x 504 ns, append 3,027 x 95 ns, text 392 x 641 ns, kernel 319 x 3694 ns. (a) = 28.258 + 13.349 + 29.366 + 51.172 = 122.145 s. (b) = 28.258 + 13.349 + 7.464 (calls x 50, returns x 30, inline state x 15 instr) + 25.419 (allocation, growth, text, kernels) = 74.490 s. All steps at c_r: 131.944 s.
- closures full: today 0.548 s (clean). F = load 0.031 + proving 0.066 + searching 0.000 + collection 0.001 + host 0.000 = 0.098 s. N = 12,268,521 block steps, 8,267,878 in regions today, 1,946,306 region entries. Today's ns per region step c_r = 24.20 (regions 0.248 s x 0.806). s_pure = 3.69 ns (measured: 35.0 of 229.6 instructions per region step); inline state ops 5,508,472 (helper today 23.4 ns, inline 15 instr); ns/instr 0.1054. Helper ops: call 1,202,039 x 85 ns, return 1,202,040 x 10 ns, alloc_object 1,611 x 72 ns, alloc_collection 804 x 0 ns, append 758,218 x 24 ns, text 3 x 2116 ns, kernel 3 x 9785 ns. (a) = 0.098 + 0.045 + 0.129 + 0.133 = 0.405 s. (b) = 0.098 + 0.045 + 0.019 (calls x 50, returns x 30, inline state x 15 instr) + 0.018 (allocation, growth, text, kernels) = 0.181 s. All steps at c_r: 0.395 s.
- closures long: today 248.951 s (derived). F = load 0.030 + proving 0.071 + searching 0.000 + collection 1.280 + host 0.000 = 1.380 s. N = 6,132,004,521 block steps, 4,149,565,084 in regions today, 976,792,107 region entries. Today's ns per region step c_r = 26.85 (regions 138.165 s x 0.806). s_pure = 4.10 ns (measured: 35.0 of 229.0 instructions per region step); inline state ops 2,753,223,083 (helper today 26.1 ns, inline 15 instr); ns/instr 0.1172. Helper ops: call 600,798,465 x 85 ns, return 600,798,965 x 10 ns, alloc_object 805,204 x 72 ns, alloc_collection 401,852 x 0 ns, append 378,969,576 x 24 ns, text 1,499 x 2116 ns, kernel 1,499 x 9785 ns. (a) = 1.380 + 25.153 + 71.751 + 66.262 = 164.546 s. (b) = 1.380 + 25.153 + 10.476 (calls x 50, returns x 30, inline state x 15 instr) + 9.122 (allocation, growth, text, kernels) = 46.131 s. All steps at c_r: 166.021 s.
- collatz full: today 0.146 s (clean). F = load 0.017 + proving 0.017 + searching 0.000 + collection 0.000 + host 0.000 = 0.034 s. N = 43,516,354 block steps, 43,511,327 in regions today, 1 region entries. Today's ns per region step c_r = 2.45 (regions 0.102 s x 1.047). s_pure = 2.45 ns (measured: 18.9 of 18.9 instructions per region step); inline state ops 509 (helper today 0.0 ns, inline 15 instr); ns/instr 0.1294. Helper ops: call 71 x 382 ns, return 72 x 55 ns, alloc_object 15 x 485 ns, alloc_collection 3 x 348 ns, append 26 x 106 ns, text 5 x 2531 ns, kernel 5 x 7790 ns. (a) = 0.034 + 0.107 + 0.000 + 0.000 = 0.141 s. (b) = 0.034 + 0.107 + 0.000 (calls x 50, returns x 30, inline state x 15 instr) + 0.000 (allocation, growth, text, kernels) = 0.141 s. All steps at c_r: 0.141 s.
- collatz long: today 12.335 s (derived). F = load 0.019 + proving 0.018 + searching 0.000 + collection 0.000 + host 0.000 = 0.037 s. N = 4,934,339,408 block steps, 4,934,334,361 in regions today, 1 region entries. Today's ns per region step c_r = 2.51 (regions 12.373 s x 1.000). s_pure = 2.51 ns (measured: 18.9 of 18.9 instructions per region step); inline state ops 57,716 (helper today 0.0 ns, inline 15 instr); ns/instr 0.1325. Helper ops: call 8,051 x 382 ns, return 8,164 x 55 ns, alloc_object 1,701 x 485 ns, alloc_collection 340 x 348 ns, append 2,948 x 106 ns, text 567 x 2531 ns, kernel 567 x 7790 ns. (a) = 0.037 + 12.374 + 0.000 + 0.011 = 12.421 s. (b) = 0.037 + 12.374 + 0.000 (calls x 50, returns x 30, inline state x 15 instr) + 0.007 (allocation, growth, text, kernels) = 12.418 s. All steps at c_r: 12.410 s.
- dispatch full: today 0.535 s (clean). F = load 0.021 + proving 0.049 + searching 0.000 + collection 0.000 + host 0.000 = 0.071 s. N = 9,854,095 block steps, 6,182,783 in regions today, 1,194,003 region entries. Today's ns per region step c_r = 28.96 (regions 0.214 s x 0.836). s_pure = 4.45 ns (measured: 40.4 of 262.9 instructions per region step); inline state ops 4,922,797 (helper today 28.5 ns, inline 15 instr); ns/instr 0.1102. Helper ops: call 1,201,254 x 61 ns, return 1,201,255 x 0 ns, alloc_object 1,013 x 240 ns, alloc_collection 4 x 460 ns, append 1,024 x 57 ns, text 4 x 1826 ns, kernel 4 x 7433 ns. (a) = 0.071 + 0.044 + 0.140 + 0.073 = 0.328 s. (b) = 0.071 + 0.044 + 0.019 (calls x 50, returns x 30, inline state x 15 instr) + 0.000 (allocation, growth, text, kernels) = 0.133 s. All steps at c_r: 0.356 s.
- dispatch long: today 117.011 s (derived). F = load 0.022 + proving 0.048 + searching 0.000 + collection 0.000 + host 0.000 = 0.070 s. N = 2,460,462,290 block steps, 1,560,390,983 in regions today, 299,994,003 region entries. Today's ns per region step c_r = 29.33 (regions 54.725 s x 0.836). s_pure = 4.54 ns (measured: 40.4 of 260.9 instructions per region step); inline state ops 1,229,169,841 (helper today 29.1 ns, inline 15 instr); ns/instr 0.1124. Helper ops: call 299,940,296 x 61 ns, return 299,940,545 x 0 ns, alloc_object 252,935 x 240 ns, alloc_collection 999 x 460 ns, append 255,682 x 57 ns, text 999 x 1826 ns, kernel 999 x 7433 ns. (a) = 0.070 + 11.175 + 35.795 + 18.243 = 65.283 s. (b) = 0.070 + 11.175 + 4.769 (calls x 50, returns x 30, inline state x 15 instr) + 0.085 (allocation, growth, text, kernels) = 16.099 s. All steps at c_r: 72.226 s.
- fannkuch full: today 0.729 s (clean). F = load 0.017 + proving 0.006 + searching 0.000 + collection 0.000 + host 0.000 = 0.023 s. N = 64,070,446 block steps, 64,059,129 in regions today, 1 region entries. Today's ns per region step c_r = 11.06 (regions 0.690 s x 1.026). s_pure = 2.20 ns (measured: 32.4 of 162.8 instructions per region step); inline state ops 32,304,123 (helper today 17.6 ns, inline 15 instr); ns/instr 0.0679. Helper ops: call 58 x 411 ns, return 59 x 15 ns, alloc_object 13 x 691 ns, alloc_collection 6 x 0 ns, append 43 x 60 ns, text 4 x 2070 ns, kernel 4 x 7002 ns. (a) = 0.023 + 0.141 + 0.568 + 0.000 = 0.732 s. (b) = 0.023 + 0.141 + 0.033 (calls x 50, returns x 30, inline state x 15 instr) + 0.000 (allocation, growth, text, kernels) = 0.197 s. All steps at c_r: 0.732 s.
- fannkuch long: today 114.990 s (derived). F = load 0.024 + proving 0.010 + searching 0.000 + collection 0.000 + host 0.000 = 0.034 s. N = 9,417,572,445 block steps, 9,417,562,236 in regions today, 1 region entries. Today's ns per region step c_r = 12.28 (regions 115.633 s x 1.000). s_pure = 2.44 ns (measured: 32.4 of 162.8 instructions per region step); inline state ops 4,748,311,236 (helper today 19.5 ns, inline 15 instr); ns/instr 0.0754. Helper ops: call 8,525 x 411 ns, return 8,672 x 15 ns, alloc_object 1,911 x 691 ns, alloc_collection 882 x 0 ns, append 6,320 x 60 ns, text 588 x 2070 ns, kernel 588 x 7002 ns. (a) = 0.034 + 22.988 + 92.660 + 0.011 = 115.693 s. (b) = 0.034 + 22.988 + 5.373 (calls x 50, returns x 30, inline state x 15 instr) + 0.007 (allocation, growth, text, kernels) = 28.402 s. All steps at c_r: 115.667 s.
- hash-map full: today 2.027 s (clean). F = load 0.027 + proving 0.029 + searching 0.000 + collection 0.008 + host 0.000 = 0.065 s. N = 52,186,850 block steps, 39,320,615 in regions today, 7,415,688 region entries. Today's ns per region step c_r = 27.69 (regions 1.313 s x 0.829). s_pure = 4.05 ns (measured: 35.2 of 241.3 instructions per region step); inline state ops 26,289,734 (helper today 26.8 ns, inline 15 instr); ns/instr 0.1148. Helper ops: call 3,800,264 x 80 ns, return 3,800,265 x 9 ns, alloc_object 150,020 x 170 ns, alloc_collection 105 x 528 ns, append 3,159,305 x 22 ns, text 5 x 3307 ns, kernel 5 x 6308 ns. (a) = 0.065 + 0.211 + 0.704 + 0.435 = 1.414 s. (b) = 0.065 + 0.211 + 0.080 (calls x 50, returns x 30, inline state x 15 instr) + 0.095 (allocation, growth, text, kernels) = 0.451 s. All steps at c_r: 1.510 s.
- hash-map long: today 25.302 s (derived). F = load 0.020 + proving 0.021 + searching 0.000 + collection 0.029 + host 0.000 = 0.070 s. N = 673,452,328 block steps, 516,293,780 in regions today, 100,044,003 region entries. Today's ns per region step c_r = 28.11 (regions 17.507 s x 0.829). s_pure = 4.12 ns (measured: 35.2 of 240.4 instructions per region step); inline state ops 339,259,460 (helper today 27.3 ns, inline 15 instr); ns/instr 0.1169. Helper ops: call 49,041,025 x 80 ns, return 49,041,038 x 9 ns, alloc_object 1,935,954 x 170 ns, alloc_collection 1,355 x 528 ns, append 40,769,683 x 22 ns, text 65 x 3307 ns, kernel 65 x 6308 ns. (a) = 0.070 + 2.775 + 9.254 + 5.610 = 17.708 s. (b) = 0.070 + 2.775 + 1.054 (calls x 50, returns x 30, inline state x 15 instr) + 1.231 (allocation, growth, text, kernels) = 5.130 s. All steps at c_r: 18.999 s.
- mandelbrot full: today 0.252 s (clean). F = load 0.032 + proving 0.150 + searching 0.000 + collection 0.000 + host 0.000 = 0.181 s. N = 2,592,328 block steps, 2,528,137 in regions today, 1 region entries. Today's ns per region step c_r = 3.39 (regions 0.003 s x 2.765). s_pure = 2.78 ns (measured: 25.6 of 31.1 instructions per region step); inline state ops 169,317 (helper today 9.4 ns, inline 15 instr); ns/instr 0.1088. Helper ops: call 55 x 383 ns, return 56 x 27 ns, alloc_object 13 x 187 ns, alloc_collection 3 x 336 ns, append 25 x 47 ns, text 4 x 1596 ns, kernel 4 x 5368 ns. (a) = 0.181 + 0.007 + 0.002 + 0.000 = 0.190 s. (b) = 0.181 + 0.007 + 0.000 (calls x 50, returns x 30, inline state x 15 instr) + 0.000 (allocation, growth, text, kernels) = 0.189 s. All steps at c_r: 0.190 s.
- mandelbrot long: today 21.779 s (derived). F = load 0.032 + proving 0.155 + searching 0.000 + collection 0.000 + host 0.000 = 0.188 s. N = 14,583,983,426 block steps, 14,583,846,054 in regions today, 1,290 region entries. Today's ns per region step c_r = 1.48 (regions 21.654 s x 1.000). s_pure = 1.22 ns (measured: 25.6 of 31.0 instructions per region step); inline state ops 952,547,796 (helper today 4.1 ns, inline 15 instr); ns/instr 0.0479. Helper ops: call 309,420 x 383 ns, return 315,046 x 27 ns, alloc_object 73,136 x 187 ns, alloc_collection 16,877 x 336 ns, append 140,646 x 47 ns, text 22,503 x 1596 ns, kernel 22,503 x 5368 ns. (a) = 0.188 + 17.848 + 3.922 + 0.310 = 22.268 s. (b) = 0.188 + 17.848 + 0.685 (calls x 50, returns x 30, inline state x 15 instr) + 0.183 (allocation, growth, text, kernels) = 18.904 s. All steps at c_r: 21.841 s.
- matrix-multiply full: today 0.125 s (clean). F = load 0.022 + proving 0.047 + searching 0.000 + collection 0.000 + host 0.000 = 0.069 s. N = 6,052,071 block steps, 5,906,954 in regions today, 23,389 region entries. Today's ns per region step c_r = 5.76 (regions 0.032 s x 1.057). s_pure = 1.80 ns (measured: 28.6 of 91.2 instructions per region step); inline state ops 1,498,841 (helper today 15.7 ns, inline 15 instr); ns/instr 0.0632. Helper ops: call 42 x 549 ns, return 43 x 56 ns, alloc_object 11 x 0 ns, alloc_collection 6 x 0 ns, append 24,325 x 25 ns, text 3 x 3165 ns, kernel 3 x 7705 ns. (a) = 0.069 + 0.011 + 0.024 + 0.001 = 0.104 s. (b) = 0.069 + 0.011 + 0.001 (calls x 50, returns x 30, inline state x 15 instr) + 0.001 (allocation, growth, text, kernels) = 0.082 s. All steps at c_r: 0.104 s.
- matrix-multiply long: today 80.980 s (derived). F = load 0.037 + proving 0.050 + searching 0.000 + collection 0.000 + host 0.000 = 0.087 s. N = 10,680,679,451 block steps, 10,676,923,264 in regions today, 3,630,099 region entries. Today's ns per region step c_r = 7.60 (regions 81.169 s x 1.000). s_pure = 2.44 ns (measured: 28.6 of 89.0 instructions per region step); inline state ops 2,645,150,771 (helper today 21.3 ns, inline 15 instr); ns/instr 0.0854. Helper ops: call 74,121 x 549 ns, return 75,886 x 56 ns, alloc_object 19,413 x 0 ns, alloc_collection 10,589 x 0 ns, append 42,928,698 x 25 ns, text 5,294 x 3165 ns, kernel 5,294 x 7705 ns. (a) = 0.087 + 26.066 + 56.330 + 1.197 = 83.680 s. (b) = 0.087 + 26.066 + 3.390 (calls x 50, returns x 30, inline state x 15 instr) + 1.152 (allocation, growth, text, kernels) = 30.695 s. All steps at c_r: 81.284 s.
- nbody full: today 0.276 s (clean). F = load 0.056 + proving 0.148 + searching 0.000 + collection 0.000 + host 0.000 = 0.203 s. N = 2,492,170 block steps, 2,365,183 in regions today, 26,767 region entries. Today's ns per region step c_r = 13.07 (regions 0.036 s x 0.868). s_pure = 1.62 ns (measured: 24.6 of 198.1 instructions per region step); inline state ops 1,496,301 (helper today 15.7 ns, inline 15 instr); ns/instr 0.0660. Helper ops: call 2,503 x 137 ns, return 2,503 x 7 ns, kernel 25,022 x 430 ns. (a) = 0.203 + 0.004 + 0.024 + 0.011 = 0.242 s. (b) = 0.203 + 0.004 + 0.001 (calls x 50, returns x 30, inline state x 15 instr) + 0.011 (allocation, growth, text, kernels) = 0.220 s. All steps at c_r: 0.236 s.
- nbody long: today 736.502 s (min of clean and --tier-stats runs). F = load 0.053 + proving 0.120 + searching 0.000 + collection 0.000 + host 0.000 = 0.173 s. N = 34,860,002,170 block steps, 34,439,888,676 in regions today, 419,991,265 region entries. Today's ns per region step c_r = 16.32 (regions 561.999 s x 1.000). s_pure = 2.07 ns (measured: 24.6 of 193.9 instructions per region step); inline state ops 20,929,980,041 (helper today 20.1 ns, inline 15 instr); ns/instr 0.0842. Helper ops: call 35,010,057 x 137 ns, return 35,010,197 x 7 ns, alloc_object 1,960 x 0 ns, alloc_collection 1,400 x 0 ns, append 5,460 x 35 ns, text 560 x 6020 ns, kernel 350,000,332 x 430 ns. (a) = 0.173 + 72.200 + 419.753 + 155.546 = 647.672 s. (b) = 0.173 + 72.200 + 26.657 (calls x 50, returns x 30, inline state x 15 instr) + 150.504 (allocation, growth, text, kernels) = 249.534 s. All steps at c_r: 569.027 s.
- object-churn full: today 2.455 s (clean). F = load 0.031 + proving 0.037 + searching 0.000 + collection 0.045 + host 0.000 = 0.113 s. N = 57,220,430 block steps, 35,856,924 in regions today, 11,222,199 region entries. Today's ns per region step c_r = 30.94 (regions 1.371 s x 0.809). s_pure = 4.20 ns (measured: 39.1 of 287.9 instructions per region step); inline state ops 29,817,460 (helper today 23.8 ns, inline 15 instr); ns/instr 0.1075. Helper ops: call 7,578,307 x 58 ns, return 7,578,308 x 0 ns, alloc_object 344,015 x 93 ns, alloc_collection 2,004 x 120 ns, append 2,238,460 x 28 ns, text 4 x 2296 ns, kernel 4 x 5529 ns. (a) = 0.113 + 0.240 + 0.711 + 0.538 = 1.602 s. (b) = 0.113 + 0.240 + 0.113 (calls x 50, returns x 30, inline state x 15 instr) + 0.095 (allocation, growth, text, kernels) = 0.562 s. All steps at c_r: 1.884 s.
- object-churn long: today 299.318 s (derived). F = load 0.024 + proving 0.040 + searching 0.000 + collection 16.006 + host 0.000 = 16.070 s. N = 5,872,678,425 block steps, 3,685,760,914 in regions today, 1,153,477,197 region entries. Today's ns per region step c_r = 37.05 (regions 168.762 s x 0.809). s_pure = 5.04 ns (measured: 39.1 of 287.6 instructions per region step); inline state ops 3,060,241,841 (helper today 28.6 ns, inline 15 instr); ns/instr 0.1288. Helper ops: call 777,780,943 x 58 ns, return 777,781,046 x 0 ns, alloc_object 35,307,135 x 93 ns, alloc_collection 205,676 x 120 ns, append 229,738,849 x 28 ns, text 411 x 2296 ns, kernel 411 x 5529 ns. (a) = 16.070 + 29.576 + 87.408 + 55.177 = 188.230 s. (b) = 16.070 + 29.576 + 13.930 (calls x 50, returns x 30, inline state x 15 instr) + 9.719 (allocation, growth, text, kernels) = 69.294 s. All steps at c_r: 233.671 s.
- parse-results full: today 1.258 s (clean). F = load 0.024 + proving 0.067 + searching 0.000 + collection 0.081 + host 0.000 = 0.172 s. N = 44,722,855 block steps, 39,735,132 in regions today, 4,033,598 region entries. Today's ns per region step c_r = 15.84 (regions 0.808 s x 0.779). s_pure = 3.27 ns (measured: 34.1 of 165.1 instructions per region step); inline state ops 16,828,173 (helper today 22.1 ns, inline 15 instr); ns/instr 0.0959. Helper ops: call 1,532,986 x 83 ns, return 1,532,987 x 15 ns, alloc_object 1,381,019 x 192 ns, alloc_collection 1,888 x 26 ns, append 18,621 x 42 ns, text 293,291 x 169 ns, kernel 7 x 4491 ns. (a) = 0.172 + 0.146 + 0.372 + 0.466 = 1.156 s. (b) = 0.172 + 0.146 + 0.036 (calls x 50, returns x 30, inline state x 15 instr) + 0.316 (allocation, growth, text, kernels) = 0.670 s. All steps at c_r: 0.880 s.
- parse-results long: today 275.468 s (derived). F = load 0.026 + proving 0.068 + searching 0.000 + collection 105.737 + host 0.000 = 105.830 s. N = 6,249,474,395 block steps, 5,571,309,632 in regions today, 564,690,098 region entries. Today's ns per region step c_r = 18.11 (regions 129.617 s x 0.779). s_pure = 3.75 ns (measured: 34.1 of 164.7 instructions per region step); inline state ops 2,351,532,260 (helper today 25.3 ns, inline 15 instr); ns/instr 0.1100. Helper ops: call 214,216,126 x 83 ns, return 214,216,266 x 15 ns, alloc_object 192,980,589 x 192 ns, alloc_collection 263,825 x 26 ns, append 2,602,058 x 42 ns, text 40,983,846 x 169 ns, kernel 978 x 4491 ns. (a) = 105.830 + 23.454 + 59.575 + 65.059 = 253.918 s. (b) = 105.830 + 23.454 + 5.763 (calls x 50, returns x 30, inline state x 15 instr) + 44.088 (allocation, growth, text, kernels) = 179.134 s. All steps at c_r: 219.028 s.
- sha256 full: today 1.881 s (clean). F = load 0.019 + proving 0.025 + searching 0.000 + collection 0.000 + host 0.000 = 0.043 s. N = 31,343,491 block steps, 6,463,467 in regions today, 1,933,426 region entries. Today's ns per region step c_r = 35.42 (regions 0.247 s x 0.925). s_pure = 7.93 ns (measured: 60.3 of 269.3 instructions per region step); inline state ops 8,886,013 (helper today 28.8 ns, inline 15 instr); ns/instr 0.1315. Helper ops: call 9,196,299 x 56 ns, return 9,196,300 x 0 ns, alloc_object 61 x 0 ns, alloc_collection 34 x 0 ns, append 500,361 x 29 ns, text 38 x 121 ns, kernel 26 x 1806 ns. (a) = 0.043 + 0.249 + 0.256 + 0.529 = 1.077 s. (b) = 0.043 + 0.249 + 0.114 (calls x 50, returns x 30, inline state x 15 instr) + 0.014 (allocation, growth, text, kernels) = 0.420 s. All steps at c_r: 1.154 s.
- sha256 long: today 1325.177 s (min of clean and --tier-stats runs). F = load 0.028 + proving 0.038 + searching 0.000 + collection 0.000 + host 0.000 = 0.066 s. N = 18,801,572,007 block steps, 3,899,963,819 in regions today, 1,167,175,706 region entries. Today's ns per region step c_r = 43.08 (regions 168.004 s x 1.000). s_pure = 9.66 ns (measured: 60.3 of 268.8 instructions per region step); inline state ops 5,330,325,626 (helper today 35.1 ns, inline 15 instr); ns/instr 0.1603. Helper ops: call 5,516,452,454 x 56 ns, return 5,516,453,054 x 0 ns, alloc_object 36,591 x 0 ns, alloc_collection 20,395 x 0 ns, append 300,144,402 x 29 ns, text 22,795 x 121 ns, kernel 15,596 x 1806 ns. (a) = 0.066 + 181.690 + 187.161 + 317.566 = 686.484 s. (b) = 0.066 + 181.690 + 83.538 (calls x 50, returns x 30, inline state x 15 instr) + 8.623 (allocation, growth, text, kernels) = 273.917 s. All steps at c_r: 810.008 s.
- sieve full: today 0.562 s (clean). F = load 0.019 + proving 0.013 + searching 0.000 + collection 0.000 + host 0.000 = 0.032 s. N = 41,695,696 block steps, 38,664,445 in regions today, 2,999,167 region entries. Today's ns per region step c_r = 8.33 (regions 0.505 s x 0.638). s_pure = 2.30 ns (measured: 30.9 of 111.9 instructions per region step); inline state ops 9,609,608 (helper today 19.6 ns, inline 15 instr); ns/instr 0.0744. Helper ops: call 56 x 911 ns, return 57 x 189 ns, alloc_object 13 x 0 ns, alloc_collection 4 x 0 ns, append 3,000,026 x 29 ns, text 4 x 1790 ns, kernel 4 x 12277 ns. (a) = 0.032 + 0.096 + 0.188 + 0.086 = 0.402 s. (b) = 0.032 + 0.096 + 0.011 (calls x 50, returns x 30, inline state x 15 instr) + 0.086 (allocation, growth, text, kernels) = 0.225 s. All steps at c_r: 0.379 s.
- sieve long: today 29.069 s (derived). F = load 0.018 + proving 0.014 + searching 0.000 + collection 0.000 + host 0.000 = 0.033 s. N = 2,038,449,775 block steps, 1,898,418,504 in regions today, 139,999,167 region entries. Today's ns per region step c_r = 10.90 (regions 32.443 s x 0.638). s_pure = 3.03 ns (measured: 30.9 of 110.9 instructions per region step); inline state ops 469,801,566 (helper today 25.9 ns, inline 15 instr); ns/instr 0.0983. Helper ops: call 2,738 x 911 ns, return 2,787 x 189 ns, alloc_object 636 x 0 ns, alloc_collection 196 x 0 ns, append 146,667,472 x 29 ns, text 196 x 1790 ns, kernel 196 x 12277 ns. (a) = 0.033 + 6.185 + 12.149 + 4.211 = 22.578 s. (b) = 0.033 + 6.185 + 0.693 (calls x 50, returns x 30, inline state x 15 instr) + 4.208 (allocation, growth, text, kernels) = 11.118 s. All steps at c_r: 22.249 s.
- sort full: today 2.286 s (clean). F = load 0.025 + proving 0.026 + searching 0.000 + collection 0.000 + host 0.000 = 0.052 s. N = 73,316,797 block steps, 55,874,260 in regions today, 8,885,083 region entries. Today's ns per region step c_r = 22.33 (regions 1.646 s x 0.758). s_pure = 3.90 ns (measured: 40.9 of 234.3 instructions per region step); inline state ops 23,442,591 (helper today 22.9 ns, inline 15 instr); ns/instr 0.0953. Helper ops: call 8,382,529 x 75 ns, return 8,382,530 x 8 ns, alloc_object 12 x 0 ns, alloc_collection 5 x 0 ns, append 500,014 x 26 ns, text 3 x 1072 ns, kernel 3 x 8898 ns. (a) = 0.052 + 0.286 + 0.538 + 0.714 = 1.589 s. (b) = 0.052 + 0.286 + 0.097 (calls x 50, returns x 30, inline state x 15 instr) + 0.013 (allocation, growth, text, kernels) = 0.448 s. All steps at c_r: 1.689 s.
- sort long: today 177.602 s (derived). F = load 0.019 + proving 0.022 + searching 0.000 + collection 0.000 + host 0.000 = 0.042 s. N = 5,789,817,235 block steps, 4,395,545,157 in regions today, 712,969,755 region entries. Today's ns per region step c_r = 22.94 (regions 133.030 s x 0.758). s_pure = 3.97 ns (measured: 40.9 of 236.6 instructions per region step); inline state ops 1,851,258,142 (helper today 23.4 ns, inline 15 instr); ns/instr 0.0970. Helper ops: call 661,967,146 x 75 ns, return 661,967,225 x 8 ns, alloc_object 948 x 0 ns, alloc_collection 395 x 0 ns, append 39,486,036 x 26 ns, text 237 x 1072 ns, kernel 237 x 8898 ns. (a) = 0.042 + 22.978 + 43.235 + 56.357 = 122.611 s. (b) = 0.042 + 22.978 + 7.829 (calls x 50, returns x 30, inline state x 15 instr) + 1.012 (allocation, growth, text, kernels) = 31.860 s. All steps at c_r: 132.876 s.
- spectral-norm full: today 0.188 s (clean). F = load 0.044 + proving 0.126 + searching 0.000 + collection 0.000 + host 0.000 = 0.171 s. N = 1,653,450 block steps, 1,309,334 in regions today, 324,741 region entries. Today's ns per region step c_r = 5.44 (regions 0.030 s x 0.238). s_pure = 1.75 ns (measured: 54.1 of 168.3 instructions per region step); inline state ops 167,070 (helper today 8.1 ns, inline 15 instr); ns/instr 0.0323. Helper ops: call 163,930 x 64 ns, return 163,931 x 4 ns, alloc_object 11 x 0 ns, alloc_collection 6 x 0 ns, append 222 x 72 ns, text 3 x 3847 ns, kernel 4 x 6241 ns. (a) = 0.171 + 0.003 + 0.001 + 0.011 = 0.186 s. (b) = 0.171 + 0.003 + 0.001 (calls x 50, returns x 30, inline state x 15 instr) + 0.000 (allocation, growth, text, kernels) = 0.174 s. All steps at c_r: 0.180 s.
- spectral-norm long: today 81.708 s (derived). F = load 0.035 + proving 0.086 + searching 0.000 + collection 0.000 + host 0.000 = 0.121 s. N = 8,100,990,970 block steps, 6,480,826,872 in regions today, 1,619,995,542 region entries. Today's ns per region step c_r = 6.48 (regions 176.222 s x 0.238). s_pure = 2.08 ns (measured: 54.1 of 168.6 instructions per region step); inline state ops 818,550,643 (helper today 9.6 ns, inline 15 instr); ns/instr 0.0384. Helper ops: call 803,166,379 x 64 ns, return 803,171,279 x 4 ns, alloc_object 53,894 x 0 ns, alloc_collection 29,397 x 0 ns, append 1,087,677 x 72 ns, text 14,698 x 3847 ns, kernel 19,598 x 6241 ns. (a) = 0.121 + 16.830 + 7.871 + 54.787 = 79.609 s. (b) = 0.121 + 16.830 + 2.940 (calls x 50, returns x 30, inline state x 15 instr) + 0.257 (allocation, growth, text, kernels) = 20.149 s. All steps at c_r: 52.597 s.
- string-building full: today 1.488 s (clean). F = load 0.018 + proving 0.004 + searching 0.000 + collection 0.009 + host 0.000 = 0.031 s. N = 63,578,690 block steps, 54,011,053 in regions today, 6,279,925 region entries. Today's ns per region step c_r = 14.55 (regions 1.022 s x 0.768). s_pure = 2.67 ns (measured: 31.9 of 173.8 instructions per region step); inline state ops 28,644,934 (helper today 16.9 ns, inline 15 instr); ns/instr 0.0837. Helper ops: call 1,600,060 x 92 ns, return 1,600,061 x 5 ns, alloc_object 15 x 672 ns, alloc_collection 4 x 463 ns, append 4,688,924 x 33 ns, text 400,005 x 185 ns, kernel 4 x 10040 ns. (a) = 0.031 + 0.170 + 0.484 + 0.386 = 1.070 s. (b) = 0.031 + 0.170 + 0.047 (calls x 50, returns x 30, inline state x 15 instr) + 0.230 (allocation, growth, text, kernels) = 0.478 s. All steps at c_r: 0.956 s.
- string-building long: today 113.339 s (derived). F = load 0.025 + proving 0.005 + searching 0.000 + collection 0.763 + host 0.000 = 0.793 s. N = 4,477,778,695 block steps, 3,838,811,057 in regions today, 438,879,926 region entries. Today's ns per region step c_r = 16.23 (regions 81.097 s x 0.768). s_pure = 3.01 ns (measured: 31.9 of 172.2 instructions per region step); inline state ops 2,017,431,866 (helper today 19.0 ns, inline 15 instr); ns/instr 0.0943. Helper ops: call 112,690,503 x 92 ns, return 112,690,574 x 5 ns, alloc_object 1,056 x 672 ns, alloc_collection 282 x 463 ns, append 330,235,870 x 33 ns, text 28,171,922 x 185 ns, kernel 282 x 10040 ns. (a) = 0.793 + 13.468 + 38.385 + 27.153 = 79.800 s. (b) = 0.793 + 13.468 + 3.703 (calls x 50, returns x 30, inline state x 15 instr) + 16.223 (allocation, growth, text, kernels) = 34.188 s. All steps at c_r: 73.487 s.
- text-split full: today 0.884 s (clean). F = load 0.024 + proving 0.068 + searching 0.000 + collection 0.042 + host 0.000 = 0.134 s. N = 22,014,063 block steps, 16,638,358 in regions today, 3,310,802 region entries. Today's ns per region step c_r = 21.27 (regions 0.512 s x 0.691). s_pure = 3.96 ns (measured: 36.7 of 196.8 instructions per region step); inline state ops 8,102,307 (helper today 22.7 ns, inline 15 instr); ns/instr 0.1081. Helper ops: call 990,287 x 121 ns, return 990,287 x 0 ns, alloc_object 30,007 x 0 ns, alloc_collection 90,017 x 0 ns, append 2,050,536 x 73 ns, text 360,066 x 271 ns, kernel 270,050 x 280 ns. (a) = 0.134 + 0.087 + 0.184 + 0.443 = 0.849 s. (b) = 0.134 + 0.087 + 0.022 (calls x 50, returns x 30, inline state x 15 instr) + 0.323 (allocation, growth, text, kernels) = 0.567 s. All steps at c_r: 0.603 s.
- text-split long: today 329.846 s (min of clean and --tier-stats runs). F = load 0.029 + proving 0.081 + searching 0.000 + collection 192.139 + host 0.000 = 192.250 s. N = 2,934,543,751 block steps, 2,227,315,926 in regions today, 443,536,792 region entries. Today's ns per region step c_r = 30.07 (regions 66.977 s x 1.000). s_pure = 5.62 ns (measured: 36.7 of 196.5 instructions per region step); inline state ops 1,080,062,922 (helper today 32.1 ns, inline 15 instr); ns/instr 0.1531. Helper ops: call 132,008,348 x 121 ns, return 132,008,361 x 0 ns, alloc_object 4,000,023 x 0 ns, alloc_collection 11,999,523 x 0 ns, append 273,342,910 x 73 ns, text 47,997,944 x 271 ns, kernel 35,998,474 x 280 ns. (a) = 192.250 + 16.479 + 34.699 + 59.091 = 302.518 s. (b) = 192.250 + 16.479 + 4.096 (calls x 50, returns x 30, inline state x 15 instr) + 43.121 (allocation, growth, text, kernels) = 255.946 s. All steps at c_r: 280.493 s.

Geometric means over the seventeen programs at long size, against the same
Java times:

today: geometric mean 44.9 times Java over 17 programs
(a): geometric mean 35.7 times Java over 17 programs
(b): geometric mean 15.1 times Java over 17 programs
(a) alt: geometric mean 33.6 times Java over 17 programs
(b) alt: geometric mean 14.2 times Java over 17 programs

(Today's 44.9 reproduces the lead's measured 45 from independent runs.)

### Assumptions, stated so the estimate can be checked

1. Every step of every function runs in compiled code; none deoptimizes
   (the census found no deoptimization in any run), no stub remains, and
   formation and proving cost what they cost today.
2. The native cost of a step is today's: the generated code a region
   program produces for a block. Function programs add no op to a step
   (Calls and Returns end the step's block and are counted separately).
3. Helper costs are those of today's reference code, measured as
   differences of census timers (±15 ns each).
4. The inline instruction counts of (b) are design figures, not
   measurements; at 0.07 to 0.15 ns per instruction, a 2× error in them moves
   (b) by at most 15% for any program, and by about 30 s for sha256 at long
   size.
5. Operation rates per step do not depend on the size beyond warm-up (true
   by construction of these programs; checked for nbody and text-split at
   two sizes).
6. Times are CPU times on a shared 4-core machine; two runs of the same
   command differed by up to 35% (nbody at 250000), and single long runs by
   up to 10%.

### What function programs will and will not buy

- **Across the suite**, (a) — every step in compiled code, with calls,
  returns, inline state operations and allocation as helper calls — is 1.0
  to 1.9 times faster than today (geometric mean 45 → 36 times Java).
  Nearly all of the gain the census points at needs (b): inline calls and
  returns and, as much, inline state operations (geometric mean 15 times
  Java).
- **sha256** (today 470 times Java at long size): 79% of its steps run at
  tier 1, three quarters of them calls and returns of `rotate` and `add`
  (one- or two-block functions) and the `ReadGlobal` of `Mask` that keeps
  each return out of a region; a tier-1 step costs 967 instructions. No
  floor: no collection, 38 ms of proving. (a) 1.9 to 2.7 times faster, (b)
  4.8 to 10 times (133 to 274 s, 48 to 100 times Java): what remains is 60
  instructions of generated code per native step, which a better code
  generator (not function programs) would have to cut.
- **nbody** (245 times Java): already 98.8% in regions. Its time is the
  inline state helper calls inside them (about 500 s of 737), the region
  entries the `sqrt` kernel forces (84 s), and `sqrt` itself through
  `run_kernel` (52 s plus its commit). (a) removes only the entries: at most
  12% faster. (b) is bound by the kernel (150 s kept): 250 to 268 s, 90 to
  97 times Java. A `sqrt` that does not go through `run_kernel` is outside
  this milestone and would be worth most of the rest.
- **text-split** (126 times Java): collection is 192 s of 330 s at long
  size and grows with the run (A3); `AppendElement`, text building and the
  text kernels stay helpers (43 s). (b) is 246 to 256 s: 1.3 times faster.
- **parse-results**: collection 106 s of the long run, allocation and text
  44 s kept: (b) 1.5 times faster.
- **binary-trees** (73 times Java): collection 28 s and allocation 25 s
  (186 ns per object beyond a plain commit) stay; (b) 64 to 74 s, 2.1 to
  2.4 times faster, 33 to 39 times Java.
- **object-churn, closures, dispatch, sort, hash-map, string-building**:
  calls, returns and their neighbours are most of the outside steps, and
  inline state helpers most of the region time: (b) 3.3 to 7.3 times faster;
  `AppendElement` keeps 16 s of string-building and 9 s of closures.
- **fannkuch, matrix-multiply, spectral-norm, sieve**: already (nearly) all
  in regions, so (a) changes nothing; their array accesses are inline state
  helpers, and (b) is 2.6 to 4.1 times faster for that reason alone.
- **collatz, mandelbrot**: loops already in one region with few or no state
  operations; function programs change nothing (mandelbrot's "alt" columns
  are an artefact of its proving-dominated full-size callgrind run).

Floors that function programs do not touch, by program: collection
(text-split 192 s, parse-results 106 s, binary-trees 28 s, object-churn 16 s
at long size); allocation and growth through the reference heap
(binary-trees, parse-results, string-building, text-split, sieve, closures);
kernel calls through `run_kernel` (nbody, text-split); proving (48-75% of
the full-size time of nbody, mandelbrot, spectral-norm, matrix-multiply);
and image load (17-56 ms per run).

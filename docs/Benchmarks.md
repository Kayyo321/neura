# Wall-clock benchmarks

This page describes how Neura measures the wall-clock speed of compiled programs and how it compares them with Java. It records the method only. Results belong to a dated run on a stated machine; they are not kept here.

The deterministic cost baseline is separate. `tests/benchmarks` records NVM steps, peak heap cells, neuron count, and image bytes, and the `benchmarks` test fails when one of them grows ([CONTRIBUTING.md](../CONTRIBUTING.md)). Those figures are the same on every machine. Wall-clock time is not, so these benchmarks are a measurement tool, not a test.

## Programs

[`tests/bench-wall/`](../tests/bench-wall/) holds seventeen programs. Each is in `neura/<name>.nra` and has a Java counterpart in `java/<Name>.java`:

| Program | Work |
| --- | --- |
| `binary-trees` | Allocates and walks perfect binary trees of several depths |
| `closures` | Map, filter, and fold written in the program, through closures that capture locals, and a returned closure called in a loop |
| `collatz` | Collatz chain lengths for every start up to n |
| `dispatch` | Calls two interface methods on objects of five classes, one of which extends another and overrides a method |
| `fannkuch` | Fannkuch-redux over every permutation of n elements |
| `hash-map` | Inserts, looks up, overwrites, and deletes int64 keys, then walks the map in insertion order |
| `mandelbrot` | Escape-time Mandelbrot set over an n by n grid |
| `matrix-multiply` | Textbook triple-loop product of two n by n int64 matrices |
| `nbody` | Symplectic integration of the Jovian planets |
| `object-churn` | Fills, partly replaces, and filters generic lists of small objects, most of which die young |
| `parse-results` | Parses `key=value` records, half of them malformed, through functions that return `Optional` and `Result` values |
| `sha256` | SHA-256 written out over 32-bit words, applied to n bytes |
| `sieve` | Sieve of Eratosthenes up to n |
| `sort` | Stable sort of n pseudo-random int32 values through a comparator |
| `spectral-norm` | Power method on a truncated infinite matrix |
| `string-building` | Builds one string from n pieces, then folds its bytes |
| `text-split` | Formats numbers into lines of text, splits them, trims and compares the fields, and joins them |

The two versions of a program run the same algorithm on the same input, written statement for statement alike, and print the same line: a checksum of the result. Floating-point results are printed as their bit patterns, so equal output means bit-for-bit equal results. Checksums of integer data are kept below 2^30 with a mask, which both languages compute without overflow. Neura traps on integer overflow where Java wraps, so every counter and key is wide enough for the largest size. Inputs come from a small linear congruential generator written in both languages.

Two programs compare library code rather than one shared algorithm. `sort` calls `Std.Collections.Sorting.sort`; the Java version is a port of that sort (binary insertion sort of 16-element blocks, then bottom-up merging), not `Arrays.sort`. `hash-map` uses `Std.Collections.HashMap`, which keeps insertion order, and the Java version uses `java.util.LinkedHashMap`. Each is its language's standard insertion-ordered hash map. Five more programs use each language's natural counterpart of a construct, and the header of each Java version names it:

- `object-churn` uses `Std.Collections.ArrayList` and `java.util.ArrayList`.
- `text-split` uses `Std.Text.Strings.split`, `trim`, and `join`, and the Java version `String.split(",", -1)`, `String.trim`, and `String.join`. With a one-character separator, `String.split` searches with `indexOf` and takes substrings without a regular expression, as the Neura library searches and slices, and the limit -1 keeps empty pieces, as `Strings.split` does.
- `parse-results` returns `Std.Core.Optional` and `Std.Core.Result` values, and the Java version `OptionalInt`, `OptionalLong`, and a sealed `Result` interface with two records, matched by a `switch`.
- `closures` passes Neura lambdas and Java lambdas of `java.util.function`'s `long` interfaces. Neura's `int64[list]` holds its values unboxed, so the Java version uses a small growable `long` array instead of `ArrayList<Long>`, which would box every value.
- `dispatch` calls through a Neura `interface` and a Java `interface`.

Each Neura program's first line names three sizes, `// sizes: quick <n> full <n> long <n>`. The quick size runs each program in about a second on the reference evaluator. The full size is roughly ten times the work. The long size keeps the Java program busy for two to four seconds on the machine where the sizes were chosen, so that the start-up of the JVM no longer dominates its time; where the size is discrete, as for `fannkuch` and `binary-trees`, it is the value closest to that range.

## Measuring

[`scripts/bench-wall.py`](../scripts/bench-wall.py) runs the set:

```sh
python3 scripts/bench-wall.py --quick            # quick sizes, 5 runs each
python3 scripts/bench-wall.py --runs 9           # full sizes
python3 scripts/bench-wall.py --long --runs 3    # long sizes
python3 scripts/bench-wall.py --only nbody,sieve --size sieve=1000000
```

For each program the script:

1. Builds the image once with `neura build` and records the wall time of that build.
2. Runs `neura run --stats` with the size as the argument, five times by default (`--runs`). It passes `--max-steps 1000000000000` and `--max-heap-cells 4294967296` (`--max-steps` and `--max-heap-cells` change them), because the full and long sizes exceed the default limits of 10^8 steps and 2^26 heap cells.
3. Takes the median wall time of the whole process, which includes starting `neura` and loading the image, and the median of its peak resident memory. The memory is the `ru_maxrss` of that process alone, which the script reads with `os.wait4` when it exits.
4. Records the program's output and the steps and peak heap cells that `--stats` reports. Every run must print the same output and report the same steps and cells, or the script stops.
5. When `javac` and `java` are both on `PATH`, compiles each Java program on its own, so that the wall time of `javac` is that program's, and runs it the same number of times with the same size. The median includes starting the JVM, and its peak resident memory is measured as for Neura. The Java output must match the Neura output byte for byte, or the run is reported as failed. Without a JDK, the script runs Neura alone and says so.

The script writes `bench-wall.json` (every time, the outputs, steps, cells, peak resident memory in KiB, build times, the Neura version, the Java version, and the machine) and `bench-wall.md` (a table, with memory in MiB) to `build/bench-wall/`, or to `--out-dir`. `--quick` and `--long` select the quick and long sizes and cannot be combined; without either the script uses the full sizes. `--size NAME=N` overrides one program's size whichever is selected.

## Comparing execution tiers

`neura run` evaluates proven lifted blocks natively, runs regions of them together, as native code where that is enabled, and evaluates other blocks by reference ([execution tiers](runtime/NVM.md#execution-tiers)). `--compare-tiers` runs each program at `--tier=0`, at `--tier=1`, at `--tier=2 --jit=off` (regions on the region interpreter), and, where `--jit=on` can run on the machine, at `--tier=2 --jit=on` (regions as native code, [native compilation](runtime/NVM.md#native-compilation)), with the same binary. It checks that all of them print the same output and report the same steps and peak heap cells, and reports the times, the speedups over tier 0, the ratio of native code to the region interpreter, the share of steps tier 1 ran at `--tier=1`, and the share tier 2 ran inside regions, from `--tier-stats`. `--run-option` passes any other option to `neura run`.

```sh
python3 scripts/bench-wall.py --quick --compare-tiers --runs 3
python3 scripts/bench-wall.py --run-option=--tier=0     # every program on the reference tier
```

The speed of tiers 1 and 2 depends on how many of a program's steps run lifted, and in regions, and on the cost of proving its blocks, which the executor spends while the program runs. A block that carries a lift hint is proven once it has run four times. Most proofs take well under a millisecond; the proof of a block with a `float64` multiplication or division takes tens of milliseconds. A program with many such blocks pays for them once per run: `nbody`, `mandelbrot`, and `spectral-norm` spend between one and two tenths of a second proving, which is about half of their time at the quick and full sizes and little of it at the long sizes (`--tier-stats` prints the division of a run's time). Regions start only where tier 1 has proven the blocks, so a program whose hot blocks stay on tier 0 gains nothing from tier 2 either. Whether the tiers are correct is not measured here: `scripts/runtime-identity.py` compares them byte for byte (`capture --extended --run-option=--tier=0` and `--tier=2`, then `compare`, also with `NEURA_TIER_EAGER=1` and `NEURA_TIER_CHECK=1`), and `scripts/tier-boundary.py` compares boundary arithmetic of every integer type and of `float32` and `float64` at the three tiers, also under `NEURA_TIER_CHECK=1`.

### Native code and the region interpreter

One local measurement of `python3 scripts/bench-wall.py --quick --compare-tiers` (median of 5 runs, quick sizes), on a 4-core Intel Xeon virtual machine at 2.10 GHz running Linux x86-64, with nothing else running. *Tier 2* runs regions on the region interpreter (`--jit=off`) and *Native* runs them as native code (`--jit=on`); *Native / tier 2* below 1 means native code is faster. Times are of whole processes, including image loading and proving blocks.

| Program | Size | Steps | Tier 0 (s) | Tier 1 (s) | Tier 2 (s) | Native (s) | Speedup 1 | Speedup 2 | Speedup native | Native / tier 2 | Steps in regions |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | 13 | 18903936 | 2.641 | 1.120 | 1.167 | 1.167 | 2.36 | 2.26 | 2.26 | 1.00 | 32.2% |
| closures | 40 | 1230925 | 2.372 | 0.150 | 0.143 | 0.136 | 15.82 | 16.62 | 17.42 | 0.95 | 65.1% |
| collatz | 10000 | 3449643 | 3.315 | 0.179 | 0.084 | 0.049 | 18.55 | 39.42 | 67.69 | 0.58 | 99.9% |
| dispatch | 60 | 996476 | 2.414 | 0.133 | 0.134 | 0.152 | 18.13 | 17.97 | 15.92 | 1.13 | 56.7% |
| fannkuch | 8 | 6122472 | 2.316 | 0.359 | 0.153 | 0.100 | 6.46 | 15.18 | 23.24 | 0.65 | 99.8% |
| hash-map | 40000 | 11070220 | 3.167 | 0.580 | 0.470 | 0.404 | 5.46 | 6.74 | 7.83 | 0.86 | 71.3% |
| mandelbrot | 24 | 234883 | 2.571 | 0.188 | 0.187 | 0.188 | 13.66 | 13.72 | 13.68 | 1.00 | 92.9% |
| matrix-multiply | 40 | 556176 | 2.666 | 0.092 | 0.082 | 0.081 | 28.96 | 32.44 | 33.05 | 0.98 | 84.4% |
| nbody | 250 | 253696 | 2.282 | 0.195 | 0.191 | 0.193 | 11.73 | 11.97 | 11.82 | 1.01 | 63.7% |
| object-churn | 100 | 4352632 | 1.966 | 0.268 | 0.256 | 0.249 | 7.33 | 7.67 | 7.91 | 0.97 | 61.7% |
| parse-results | 50 | 4548198 | 2.376 | 0.343 | 0.261 | 0.223 | 6.94 | 9.12 | 10.64 | 0.86 | 86.2% |
| sha256 | 50000 | 3142132 | 2.932 | 0.241 | 0.243 | 0.236 | 12.15 | 12.07 | 12.44 | 0.97 | 19.7% |
| sieve | 300000 | 4028187 | 2.915 | 0.226 | 0.131 | 0.085 | 12.91 | 22.27 | 34.26 | 0.65 | 91.8% |
| sort | 30000 | 7512992 | 3.593 | 0.455 | 0.325 | 0.273 | 7.90 | 11.04 | 13.15 | 0.84 | 75.5% |
| spectral-norm | 20 | 165376 | 3.916 | 0.130 | 0.130 | 0.130 | 30.05 | 30.11 | 30.22 | 1.00 | 71.1% |
| string-building | 40000 | 5918687 | 3.480 | 0.293 | 0.219 | 0.165 | 11.88 | 15.87 | 21.06 | 0.75 | 83.3% |
| text-split | 1500 | 2239595 | 2.060 | 0.226 | 0.185 | 0.176 | 9.10 | 11.13 | 11.71 | 0.95 | 72.7% |

Native code pays off where most steps run in regions of integer loops: collatz (0.58 of the interpreter's time), fannkuch and sieve (0.65), string-building (0.75), and sort, hash-map, and parse-results (0.84 to 0.86). Float operations are compiled inline too, but the float programs (mandelbrot, nbody, spectral-norm) spend most of a quick-size run proving their blocks, so the two columns are equal there. Elsewhere the time goes to steps outside regions (tier 1 and tier 0), to region entries and exits, to state operations, which run the interpreter's code, and to proving blocks. binary-trees and sha256 run under a third of their steps in regions, because a region ends at every call, return, and allocation. The 1.13 of dispatch is the noise of a 0.13 s run: its regions take 18 ms as native code and 23 ms on the interpreter. These figures depend on the machine; steps and outputs do not.

## One machine's results

Full sizes on a 4-core Intel Xeon virtual machine at 2.10 GHz running Linux x86-64, with OpenJDK 21.0.12. *0.2.0* is the released version and *0.2.1* the version in development, with the same sources; both run every program in the same number of steps and print the same output. The 0.2.1 and Java columns are medians of 5 runs with nothing else running, and the 0.2.0 column medians of 3 runs while other work ran on the machine.

| Program | Size | Steps | 0.2.0 (s) | 0.2.1 (s) | Java (s) | 0.2.1 / Java |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | 16 | 209899904 | 14.484 | 15.023 | 0.252 | 59.7 |
| closures | 400 | 12268525 | 3.802 | 0.507 | 0.094 | 5.4 |
| collatz | 100000 | 43516363 | 0.180 | 0.144 | 0.087 | 1.7 |
| dispatch | 600 | 9854101 | 5.769 | 0.518 | 0.076 | 6.8 |
| fannkuch | 9 | 64070452 | 0.841 | 0.766 | 0.091 | 8.4 |
| hash-map | 200000 | 52186859 | 7.206 | 1.911 | 0.170 | 11.3 |
| mandelbrot | 80 | 2592335 | 18.203 | 0.212 | 0.055 | 3.9 |
| matrix-multiply | 90 | 6052076 | 2.462 | 0.122 | 0.051 | 2.4 |
| nbody | 2500 | 2517196 | 15.172 | 0.248 | 0.050 | 5.0 |
| object-churn | 1000 | 57220437 | 4.475 | 2.527 | 0.129 | 19.6 |
| parse-results | 500 | 44722868 | 4.511 | 1.248 | 0.218 | 5.7 |
| sha256 | 500000 | 31343525 | 1.997 | 2.046 | 0.072 | 28.2 |
| sieve | 3000000 | 41695702 | 2.320 | 0.619 | 0.069 | 8.9 |
| sort | 250000 | 73316801 | 3.128 | 2.305 | 0.112 | 20.6 |
| spectral-norm | 64 | 1653456 | 27.412 | 0.179 | 0.047 | 3.8 |
| string-building | 400000 | 63578697 | 2.426 | 1.611 | 0.119 | 13.5 |
| text-split | 15000 | 22398636 | 2.693 | 1.022 | 0.140 | 7.3 |

The float programs (`mandelbrot`, `nbody`, `spectral-norm`) and `matrix-multiply` gain the most: in 0.2.0 the proofs of their blocks failed or took longer than the run, so they ran on the reference evaluator. `binary-trees` and `sha256` are unchanged, because their time is in calls, allocation, and state operations, which leave a region. At these sizes a Java run is mostly the start of the JVM, and about half of a float program's Neura run is the proving of its blocks, so the ratios say little about steady speed; the long sizes are for that.

Long sizes on the same machine, one run of each program while other work ran on it, so the times are good to perhaps a tenth:

| Program | Size | Steps | 0.2.1 (s) | Java (s) | 0.2.1 / Java |
| --- | ---: | ---: | ---: | ---: | ---: |
| binary-trees | 19 | 1914011930 | 140.1 | 1.93 | 73 |
| closures | 200000 | 6132004525 | 217.4 | 2.56 | 85 |
| collatz | 8000000 | 4934339417 | 11.2 | 3.15 | 4 |
| dispatch | 150000 | 2460462296 | 107.0 | 3.30 | 32 |
| fannkuch | 11 | 9417572451 | 105.1 | 3.30 | 32 |
| hash-map | 2400000 | 673452337 | 22.5 | 2.49 | 9 |
| mandelbrot | 6000 | 14583983433 | 16.9 | 3.60 | 5 |
| matrix-multiply | 1100 | 10680679456 | 72.6 | 2.35 | 31 |
| nbody | 35000000 | 35210002196 | 676.0 | 2.76 | 245 |
| object-churn | 100000 | 5872678432 | 249.7 | 2.42 | 103 |
| parse-results | 70000 | 6249474408 | 262.8 | 2.30 | 114 |
| sha256 | 300000000 | 18801572041 | 1291.3 | 2.75 | 470 |
| sieve | 140000000 | 2038449781 | 34.4 | 1.98 | 17 |
| sort | 15000000 | 5789817239 | 179.9 | 3.31 | 54 |
| spectral-norm | 4500 | 8100990976 | 215.6 | 3.51 | 61 |
| string-building | 25000000 | 4477778702 | 103.9 | 2.77 | 37 |
| text-split | 2000000 | 2985816673 | 376.8 | 3.00 | 126 |

The geometric mean of the ratios is 45. Where a program's steps run in regions of integer code the ratio is single digits (`collatz`, `mandelbrot`, `hash-map`); where they leave regions for calls, allocation, and state operations it is in the tens and hundreds, and `sha256`, which reads and writes an array element in most of its steps, is the slowest.

## Reading the results

- Compare times from one machine. Run on an otherwise idle machine and state the CPU, the operating system, and the Java version with any figures you publish.
- Steps and peak heap cells do not depend on the machine. A change to the runtime that leaves them unchanged has changed only how fast steps run. A change to the compiler or the standard library can change them; compare them before comparing times.
- The ratio of Neura to Java time at the quick sizes is dominated by start-up for Java and by image loading for small Neura programs, and at the full sizes most Java programs still finish in a fraction of a second, most of it start-up. Use the long sizes for ratios.
- Both timings are of whole processes. Neither includes warm-up runs, so the Java figures include the time its JIT compiler takes to reach steady state.
- Peak resident memory is that of the whole process, including the runtime: the JVM sizes its heap from the machine's memory, so its figure depends on the machine more than Neura's does.
- Build times are of one build each, including starting `neura` or `javac`, and do not depend on the size.

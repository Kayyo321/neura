<!-- Generated from docs/Benchmarks.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Wall-clock benchmarks

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page describes how Neura measures the wall-clock speed of compiled programs and how it compares them with Java. It records the method only. Results belong to a dated run on a stated machine; they are not kept here.

The deterministic cost baseline is separate. `tests/benchmarks` records NVM steps, peak heap cells, neuron count, and image bytes, and the `benchmarks` test fails when one of them grows ([CONTRIBUTING.md](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md)). Those figures are the same on every machine. Wall-clock time is not, so these benchmarks are a measurement tool, not a test.

## Programs

[`tests/bench-wall/`](https://github.com/Kayyo321/neura/tree/main/tests/bench-wall) holds twelve programs. Each is in `neura/<name>.nra` and has a Java counterpart in `java/<Name>.java`:

| Program | Work |
| --- | --- |
| `binary-trees` | Allocates and walks perfect binary trees of several depths |
| `collatz` | Collatz chain lengths for every start up to n |
| `fannkuch` | Fannkuch-redux over every permutation of n elements |
| `hash-map` | Inserts, looks up, overwrites, and deletes int32 keys, then walks the map in insertion order |
| `mandelbrot` | Escape-time Mandelbrot set over an n by n grid |
| `matrix-multiply` | Textbook triple-loop product of two n by n int64 matrices |
| `nbody` | Symplectic integration of the Jovian planets |
| `sha256` | SHA-256 written out over 32-bit words, applied to n bytes |
| `sieve` | Sieve of Eratosthenes up to n |
| `sort` | Stable sort of n pseudo-random int32 values through a comparator |
| `spectral-norm` | Power method on a truncated infinite matrix |
| `string-building` | Builds one string from n pieces, then folds its bytes |

The two versions of a program run the same algorithm on the same input, written statement for statement alike, and print the same line: a checksum of the result. Floating-point results are printed as their bit patterns, so equal output means bit-for-bit equal results. Checksums of integer data are kept below 2^30 with a mask, which both languages compute without overflow.

Two programs compare library code rather than one shared algorithm. `sort` calls `Std.Collections.Sorting.sort`; the Java version is a port of that sort (binary insertion sort of 16-element blocks, then bottom-up merging), not `Arrays.sort`. `hash-map` uses `Std.Collections.HashMap`, which keeps insertion order, and the Java version uses `java.util.LinkedHashMap`. Each is its language's standard insertion-ordered hash map.

Each Neura program's first line names two sizes, `// sizes: quick <n> full <n>`. The quick size runs each program in about a second on the reference evaluator. The full size is roughly ten times the work.

## Measuring

[`scripts/bench-wall.py`](https://github.com/Kayyo321/neura/blob/main/scripts/bench-wall.py) runs the set:

```sh
python3 scripts/bench-wall.py --quick            # quick sizes, 5 runs each
python3 scripts/bench-wall.py --runs 9           # full sizes
python3 scripts/bench-wall.py --only nbody,sieve --size sieve=1000000
```

For each program the script:

1. Builds the image once with `neura build`.
2. Runs `neura run --stats` with the size as the argument, five times by default (`--runs`). It passes `--max-steps 1000000000000` because the full sizes exceed the default limit of 10^8 steps.
3. Takes the median wall time of the whole process, which includes starting `neura` and loading the image.
4. Records the program's output and the steps and peak heap cells that `--stats` reports. Every run must print the same output and report the same steps and cells, or the script stops.
5. When `javac` and `java` are both on `PATH`, compiles the Java programs once and runs each the same number of times with the same size. The median includes starting the JVM. The Java output must match the Neura output byte for byte, or the run is reported as failed. Without a JDK, the script runs Neura alone and says so.

The script writes `bench-wall.json` (every time, the outputs, steps, cells, the Neura version, the Java version, and the machine) and `bench-wall.md` (a table) to `build/bench-wall/`, or to `--out-dir`.

## Comparing execution tiers

`neura run` evaluates proven lifted blocks natively, runs regions of them together, as native code where that is enabled, and evaluates other blocks by reference ([execution tiers](NVM#execution-tiers)). `--compare-tiers` runs each program at `--tier=0`, at `--tier=1`, at `--tier=2 --jit=off` (regions on the region interpreter), and, where `--jit=on` can run on the machine, at `--tier=2 --jit=on` (regions as native code, [native compilation](NVM#native-compilation)), with the same binary. It checks that all of them print the same output and report the same steps and peak heap cells, and reports the times, the speedups over tier 0, the ratio of native code to the region interpreter, the share of steps tier 1 ran at `--tier=1`, and the share tier 2 ran inside regions, from `--tier-stats`. `--run-option` passes any other option to `neura run`.

```sh
python3 scripts/bench-wall.py --quick --compare-tiers --runs 3
python3 scripts/bench-wall.py --run-option=--tier=0     # every program on the reference tier
```

The speed of tiers 1 and 2 depends on how many of a program's steps run lifted, and in regions, and on the cost of proving its blocks, which the executor spends while the program runs. A binary64 multiplication or division takes a few tenths of a second to prove, so a block that contains one is lifted only once it has run long enough to repay that: at the quick sizes, the float programs end before most of their float blocks are proven and run at about the speed of tier 0, and at the full sizes they are faster. Regions start only where tier 1 has proven the blocks, so a program whose hot blocks stay on tier 0 gains nothing from tier 2 either. Whether the tiers are correct is not measured here: `scripts/runtime-identity.py` compares them byte for byte (`capture --extended --run-option=--tier=0` and `--tier=2`, then `compare`, also with `NEURA_TIER_EAGER=1` and `NEURA_TIER_CHECK=1`), and `scripts/tier-boundary.py` compares boundary arithmetic of every integer type and of `float32` and `float64` at the three tiers, also under `NEURA_TIER_CHECK=1`.

### Native code and the region interpreter

One local measurement of `python3 scripts/bench-wall.py --quick --compare-tiers` (median of 5 runs, quick sizes), on an AMD Ryzen 7 9800X3D (8 cores) running Linux x86-64, with one unrelated process busy on another core. *Tier 2* runs regions on the region interpreter (`--jit=off`) and *Native* runs them as native code (`--jit=on`); *Native / tier 2* below 1 means native code is faster. Times are of whole processes, including image loading and proving blocks.

| Program | Size | Steps | Tier 0 (s) | Tier 1 (s) | Tier 2 (s) | Native (s) | Speedup 1 | Speedup 2 | Speedup native | Native / tier 2 | Steps in regions |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| binary-trees | 13 | 18903936 | 1.192 | 0.503 | 0.516 | 0.514 | 2.37 | 2.31 | 2.32 | 1.00 | 32.2% |
| collatz | 10000 | 3449643 | 1.300 | 0.091 | 0.042 | 0.027 | 14.27 | 30.72 | 48.30 | 0.64 | 98.6% |
| fannkuch | 8 | 6122472 | 0.982 | 0.166 | 0.085 | 0.067 | 5.92 | 11.51 | 14.70 | 0.78 | 97.3% |
| hash-map | 40000 | 11070220 | 1.347 | 0.811 | 0.769 | 0.759 | 1.66 | 1.75 | 1.78 | 0.99 | 67.6% |
| mandelbrot | 24 | 234883 | 1.186 | 0.979 | 0.982 | 0.980 | 1.21 | 1.21 | 1.21 | 1.00 | 29.1% |
| matrix-multiply | 40 | 556176 | 0.948 | 0.873 | 0.872 | 0.876 | 1.09 | 1.09 | 1.08 | 1.01 | 55.5% |
| nbody | 250 | 253696 | 1.062 | 1.288 | 1.284 | 1.280 | 0.82 | 0.83 | 0.83 | 1.00 | 0.0% |
| sha256 | 50000 | 3142132 | 1.345 | 0.122 | 0.127 | 0.126 | 11.02 | 10.55 | 10.67 | 0.99 | 19.3% |
| sieve | 300000 | 4028187 | 1.189 | 0.422 | 0.385 | 0.368 | 2.82 | 3.09 | 3.23 | 0.96 | 90.0% |
| sort | 30000 | 7512992 | 1.513 | 0.516 | 0.471 | 0.449 | 2.93 | 3.21 | 3.37 | 0.95 | 72.2% |
| spectral-norm | 20 | 165376 | 1.675 | 1.983 | 1.995 | 1.976 | 0.84 | 0.84 | 0.85 | 0.99 | 10.0% |
| string-building | 40000 | 5918687 | 1.631 | 0.279 | 0.233 | 0.224 | 5.85 | 7.01 | 7.29 | 0.96 | 76.7% |

Native code pays off where most steps run in regions of integer loops: collatz (0.64 of the interpreter's time) and fannkuch (0.78), and a few percent on sieve, sort, and string-building. Elsewhere the time goes to steps outside regions (tier 1 and tier 0), to region entries and exits, to helpers (float operations and state operations run the interpreter's code), and to proving blocks, so the two are within noise. These figures depend on the machine; steps and outputs do not.

## Reading the results

- Compare times from one machine. Run on an otherwise idle machine and state the CPU, the operating system, and the Java version with any figures you publish.
- Steps and peak heap cells do not depend on the machine. A change to the runtime that leaves them unchanged has changed only how fast steps run. A change to the compiler or the standard library can change them; compare them before comparing times.
- The ratio of Neura to Java time at the quick sizes is dominated by start-up for Java and by image loading for small Neura programs. Use the full sizes for ratios.
- Both timings are of whole processes. Neither includes warm-up runs, so the Java figures include the time its JIT compiler takes to reach steady state.

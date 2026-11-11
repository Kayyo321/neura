# Wall-clock benchmarks

This page describes how Neura measures the wall-clock speed of compiled programs and how it compares them with Java. It records the method only. Results belong to a dated run on a stated machine; they are not kept here.

The deterministic cost baseline is separate. `tests/benchmarks` records NVM steps, peak heap cells, neuron count, and image bytes, and the `benchmarks` test fails when one of them grows ([CONTRIBUTING.md](../CONTRIBUTING.md)). Those figures are the same on every machine. Wall-clock time is not, so these benchmarks are a measurement tool, not a test.

## Programs

[`tests/bench-wall/`](../tests/bench-wall/) holds twelve programs. Each is in `neura/<name>.nra` and has a Java counterpart in `java/<Name>.java`:

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

[`scripts/bench-wall.py`](../scripts/bench-wall.py) runs the set:

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

## Reading the results

- Compare times from one machine. Run on an otherwise idle machine and state the CPU, the operating system, and the Java version with any figures you publish.
- Steps and peak heap cells do not depend on the machine. A change to the runtime that leaves them unchanged has changed only how fast steps run. A change to the compiler or the standard library can change them; compare them before comparing times.
- The ratio of Neura to Java time at the quick sizes is dominated by start-up for Java and by image loading for small Neura programs. Use the full sizes for ratios.
- Both timings are of whole processes. Neither includes warm-up runs, so the Java figures include the time its JIT compiler takes to reach steady state.

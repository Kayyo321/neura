# Work order 1I — fix three defects the red team found in the overflow analysis

Read `handoff/WORK_RULES.md` first; it binds you. Then, in your worktree:

```
git reset --hard ccr-e433a631-4hzo4n          # the branch head
git cherry-pick d70e9ab                        # the red team's tests (branch worktree-agent-aca908e4b8979e0c4)
```

The cherry-pick may conflict in `tests/lift/lift_tests.cpp` (the head added other tests since). Resolve by keeping both sides' tests. Do not squash the cherry-picked commit away; add your commits on top.

## The defects

After the cherry-pick, `./build/neura_lift_tests overflow "$PWD"` fails in exactly three tests. They are in `src/lift/overflow.cpp`, in code older than this release (0.2.0 has them). The overflow proof decides which blocks run as native code, so its code must be right on every graph it is given, not only on the graphs today's loader and circuit library produce.

1. **False proof on a node with two edges from one source.** `factor()` counts a node's two edges from wire P as the two members of the step-pair group {P, Q}; `grouped_sum` then reads the node as the group's range. Reproducer graph in the test. The loader rejects such a graph (`src/nvr/loader.cpp`: "edges are not sorted by unique source"), but `prove_no_overflow` only checks that each edge reads an earlier wire.
2. **Undefined behaviour (SIGFPE):** `factor()` evaluates `INT64_MIN % -1` and `INT64_MIN / -1` for a group member with coefficient -1 read with weight INT64_MIN. The graph is valid and cannot overflow.
3. **Unbounded loop that ignores the work bound:** an interval 2^63 wide makes `interval.hi - interval.lo` overflow (signed), the wire becomes a free variable, and `eliminate()` walks about 2^63 values without charging work. `introduce()` can also wrap `n * values`. The red team lists `overflow.cpp` lines 424, 622, 791, 897–899, 1164, 1188, 1238 (line numbers at commit 1431a18).

## What to do

A. **Precondition enforced.** `prove_no_overflow` returns a proof that is not `safe` (as it does today for an edge that reads a later wire) for any graph in which some node's edges are not in strictly ascending order of source. State the precondition in `src/lift/overflow.hpp`. Also make `factor()` right by itself for whatever it is given (it must not count one member twice), so that the proof does not rest on one check far away.

B. **No undefined behaviour anywhere in `src/lift/overflow.cpp`.** Go through every `+`, `-`, unary `-`, `*`, `/`, `%`, `++`, `--`, shift and narrowing conversion on a signed or 64-bit value in the file. For each, either the operands are bounded by an argument you can state in one comment line at the site (or once for a group of sites), or the operation becomes checked and a failure means "no range found" (never "safe"). Interval widths are computed without signed overflow. Report the list of sites you changed and the sites you left with the bound that covers them.

C. **Every loop is bounded by the work it charges or by a small constant.** No loop in the analysis may iterate over the values of a variable, the assignments of a cut, or the states of an elimination without either a bound of at most a few thousand iterations established before it, or charging `work_` (and the caller's budget where the surrounding code does) per iteration. A call with a work bound W returns in time proportional to W plus the graph's size.

D. **Verdicts on real graphs do not change.** Every graph of the corpus that is proven before your change is proven after it, with the same intervals where you did not have to change the arithmetic. No fix may make the analysis accept anything it did not accept before.

## Tests

- The three reproducers pass.
- Extend the fuzzer in `tests/lift/lift_tests.cpp` (namespace `overflow_fuzz`) with a generator class for exactly these edges of the input space, audited like the others (every interval contains the truth, a safe proof never from an overflowing graph, accepted lanes are bits): weights and biases drawn from {INT64_MIN, INT64_MIN + 1, -2^62, -1, 1, 2^62, INT64_MAX - 1, INT64_MAX} mixed with small ones; wires whose interval is 2^62 to 2^64 wide; nodes with duplicate or unsorted edges (the expected result is "not safe"); step pairs and selection halves read with such weights. Run this class in a forked child with an alarm like the reproducers, so a crash or a hang is a test failure with a message, and with small work bounds as well as unbounded.
- The whole `overflow` suite's default run must add at most 15 s in a Release build on an idle machine for the fuzz test (it measured 18–21 s on a loaded machine; trim iteration counts if needed, not generators).
- Say how you confirmed each new test fails without the fix (the three reproducers already do).

## Sanitizer run (required)

Build `neura_lift_tests` only, in a second build directory, with the project's sanitizer flags, and run the three suites:

```
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined" "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined"
cmake --build build-san --parallel 3 --target neura_lift_tests
./build-san/neura_lift_tests overflow "$PWD"
./build-san/neura_lift_tests proof "$PWD"
./build-san/neura_lift_tests execute "$PWD"
```

(If the configuration needs other options to build without the static launcher, see the `sanitizers` job of `.github/workflows/tier2.yml` and use what it uses.) Zero sanitizer reports. Also run the fuzzer's large mode once under the sanitizers at whatever scale finishes in about 20 minutes (`NEURA_OVERFLOW_FUZZ_SCALE`), and report the totals.

## Files

`src/lift/overflow.cpp`, `src/lift/overflow.hpp`, `tests/lift/lift_tests.cpp`. Nothing else. If a fix seems to need a change elsewhere (for example in `lift.cpp`), stop and report.

## Acceptance commands (Release build)

```
cmake --build build --parallel 3 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"
ctest --test-dir build --parallel 3 --output-on-failure -R '^(lift-proof|lift-overflow|lift-execute|lift-corpus|backend-tier|backend-jit|backend-floats|benchmarks)$'
```

Then, for D: for each program in `tests/bench-wall/neura`, build it and run it at its quick size with `NEURA_TIER_EAGER=1 ... run --tier-stats --jit=on --max-steps 1000000000000 --max-heap-cells 4294967296 IMAGE -- SIZE` with the binary of the branch head (build it once in a second directory or keep a copy before you edit) and with yours, and compare the `tier: blocks …` line (blocks, hinted, attempted, lifted, hints rejected must be equal) and the list of rejection reasons. Report the eager `proving` milliseconds before and after for nbody, mandelbrot, spectral-norm and matrix-multiply (median of 5); a slowdown of more than 5% needs an explanation.

## Report

As WORK_RULES.md says, plus the site list of B, the loop list of C with each loop's bound, and the before/after table of D.

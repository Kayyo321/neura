# Work order 1H — when a hinted block is proven: thresholds and proof allowance by measurement

Read `handoff/WORK_RULES.md` first; it binds you. Then, in your worktree: `git reset --hard ccr-e433a631-4hzo4n` (the branch head; your worktree may start from an older commit).

## Background

Stage: NVM runtime only (`src/nvm/tier1.hpp`, `src/nvm/tier1.cpp`). Read `docs/runtime/NVM.md`, section "Execution tiers", especially the paragraph that starts "The lifted blocks of an image live in one `nvm::TierCache`".

A block with a LIFT hint runs on the reference evaluator (tier 0) until it has run `TierOptions::hint_after` = 256 times; then its hint is proven (regeneration + overflow proof) and it runs lifted. The refined overflow analysis gets work `max(minimum_proof_work, runs * edges / steps_per_work)`; when that runs out, the attempt is repeated once the block has run eight times as often.

These constants were set when the proof of a float64 multiply or divide graph took 0.3–1 s. It now takes 15–30 ms (plus about 10 ms of regeneration), and most graphs are settled by the unrefined pass in well under a millisecond. Measured on this branch at default settings, full benchmark sizes:

| program | total ms | proving | tier 0 | same run with `NEURA_TIER_EAGER=1`: total | proving | tier 0 |
| --- | --- | --- | --- | --- | --- | --- |
| nbody 2500 | 856 | 120 | 499 | 530 | 301 | 2 |
| mandelbrot 80 | 555 | 166 | 333 | 194 | 143 | 1 |
| spectral-norm 64 | 722 | 173 | 312 | 505 | 298 | 5 |

So waiting for 256 runs now costs more than it saves on these programs, but `NEURA_TIER_EAGER=1` (prove at the first run, unlimited work, and regions at once) proves blocks that run once, which costs start-up time on programs that are mostly run-once code.

## Goal

Choose `hint_after`, `steps_per_work`, `minimum_proof_work`, and the retry factor (the "eight times" in `tier1.cpp`) by measurement, so that at default settings:

1. On every program of `tests/bench-wall/neura` at its full size, time on tier 0 plus proving plus searching is at most 5% of the run's total (the `tier: time:` line of `--tier-stats`), or you show which blocks keep it higher and why no setting of these constants fixes it.
2. No bench-wall program at quick or full size, `tests/backend/programs/hello-world.nra` or equivalent smallest program, and no `neura test` run over a standard-library test directory uses more than 3% more CPU time than with the current constants. Hello-world must stay under 5 ms wall.
3. Among settings that satisfy 1 and 2, the one with the lowest geometric-mean CPU time over bench-wall quick sizes plus the `neura test` runs (start-up-dominated work is where a low threshold can hurt).

You may change `search_after`, `region_after` and `region_patience` only if a measurement shows a gain of more than 3% on some program and no loss elsewhere; report the measurement either way. Do not change what `NEURA_TIER_EAGER=1` does.

If a constant threshold cannot satisfy 1 and 2 together, propose (do not implement) the smallest rule that would, for example a threshold that depends on the graph's edge count; stop and report with the numbers.

## Method

- Measure CPU time (user + system, from `os.wait4`), not wall time: other agents share this 4-core machine. Take the median of at least 5 runs per point, and report the spread. `scripts/bench-wall.py` shows how images are built and run (`neura build SRC -o IMG`, `neura run --stats --max-steps N --max-heap-cells N IMG -- ARGS`; the first line of each program names its sizes).
- For the sweep, add a temporary environment override of the constants in a scratch copy of `tier1.cpp`. Do not commit it. The committed change is the constants, their comments, and the documentation.
- Sweep at least `hint_after` in {1, 2, 4, 8, 16, 32, 64, 128, 256} crossed with at least three values of `steps_per_work` and of `minimum_proof_work`. Record, per program and point: CPU time, the `tier: time:` line, blocks attempted and lifted, and how many attempts ran out of work.
- Runs of `neura test`: pick at least four standard-library test directories under `tests/stdlib` of different sizes (see how CTest invokes them in `CMakeLists.txt`), including the largest.

## Files you may touch

`src/nvm/tier1.hpp`, `src/nvm/tier1.cpp`, `docs/runtime/NVM.md` (the sentences that state the thresholds and what they are based on; then `python3 scripts/generate-wiki.py` and `python3 scripts/check-docs.py`), and the literal `tier: blocks …` expectation for hello-world in `tests/backend/cli.cmake` **only if** the new constants change that line; say exactly what changed and why. Nothing else. In particular nothing under `src/lift`, `src/jit`, `src/backend`, and no test other than that expectation.

## Contracts

Which blocks are lifted, and when, may change. Nothing observable may: exits, traps, `steps`, `function_steps`, `peak_heap_cells`, output, traces. The thresholds must not depend on the clock.

## Acceptance commands

```
cmake --build build --parallel 3 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"
ctest --test-dir build --parallel 3 --output-on-failure -R '^(backend-tier|backend-tier-interpreter|backend-jit|backend-cli|lift-execute|benchmarks|stdlib-install)$'
python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/t0 --run-option=--tier=0
python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/jit --jit on
python3 scripts/runtime-identity.py compare build/identity/t0 build/identity/jit
python3 scripts/tier-boundary.py --neura build/neura --out build/tier-boundary --jit off,on
```

## Report

As WORK_RULES.md says, plus: the full sweep table (a file in the scratchpad directory `handoff/wo1h/` and a summary in the report); for the chosen constants, per bench-wall program at quick and full size: CPU time before and after and the `tier: time:` line after; the `neura test` and hello-world numbers before and after; which goal (1, 2, 3) is met and which is not.

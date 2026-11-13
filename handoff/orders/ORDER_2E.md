# Work order 2E — collect less often where nobody can tell

Read `handoff/WORK_RULES.md` first; it binds you, **with one exception this order makes explicit: `peak_heap_cells` may change, and `tests/benchmarks/baseline.txt` and other pinned peaks are updated as described below.** In your worktree: `git reset --hard ccr-e433a631-4hzo4n-m2` (expected commit bd0a45a). Keep a copy of the base binary (`build/neura` built from bd0a45a) before you edit. Commit work in progress before any run longer than ten minutes; write long runs' output to files under `handoff/wo2e/`.

## Why

Stage: NVM runtime, the collector's schedule (`src/nvm/machine.cpp`: `collect`, `ensure_heap_cells`, the initial `next_collection_`). Read `docs/runtime/NVM.md`, "Garbage collection" and "Heap and objects", and `docs/Project.md` on determinism.

After every collection the next one is due when the heap has grown by `max(256, live / 2)` cells. A program with a small live heap that allocates a lot therefore collects every few hundred cells. Measured at the long benchmark sizes, collection is 192 s of `text-split`'s 330 s, 106 s of `parse-results`' 275 s and 28 s of `binary-trees`' 154 s. (The sweep also costs time in proportion to every handle ever assigned; that is a separate order, not yours, and it touches `src/nvm/heap.hpp`, which you must not edit.)

The project owner has approved this change of schedule: **a minimum growth window of 1,048,576 cells instead of 256, and growth by `live` instead of `live / 2`**, on the understanding that the only thing a program or its user can observe changing is `peak_heap_cells` (reported by `--stats`, `neura test --stats`, traces of statistics, and pinned in some tests).

## The rule you must establish before changing anything

A collection is not only a matter of speed and peak. Find, by reading the code, **every** way the moment of a collection, or the amount of garbage held between collections, can reach anything other than `peak_heap_cells`, and write the list into your report with file:line. Known or suspected ones, to start you off, not to limit you:

- In an image with tasks a collection releases the queued messages of unreachable channels and disarms unreachable timers (`release_scheduler_object`, the channel work of `collect`), which can change a message-limit trap, a deadlock outcome, the scheduler's counters, or a trace.
- In a task image, frames and scheduler records are charged to the same cell budget.
- A fleet shares one cell budget between members (`fleet_heap_`, `FleetHeapCells`): garbage one member holds may be visible to another member as a heap-limit trap or as a collection.
- A parallel item's run charges cells to the run that started it (`charged_cells`, `max_cells`).
- Explicit destruction (`FreeObject`) and stale aliases: does any behaviour depend on whether a collection has run?
- Identity numbers, handle numbers, list versions: assigned in allocation order, so they must not depend on the schedule. Confirm.
- Host-visible state: does any host request, record or trace entry carry `live_cells`, a collection count, or anything derived from them?
- `--tier-stats`, `--profile`, the task dump, the debugger's snapshot.
- `NEURA_TIER_CHECK=2` compares `next_collection_` ("collection threshold") between a run and its shadow; both use the same schedule, so that stays equal. Confirm.
- The hard limit: show from the code that a heap-limit trap happens exactly when the reachable cells plus the request exceed the limit, whatever the schedule (a collection always runs before the limit check can fail).

**The new schedule applies exactly to the runs for which your list shows that nothing but `peak_heap_cells` can change. Every other run keeps the 0.2.0 schedule bit for bit.** The owner has already decided that images with tasks keep the old schedule. Whether a run is in the new class must be decided when the run starts, from the image and the run's configuration alone (never from what the program has done so far), and stated as one predicate in one place with a comment. If the class is not what "images without tasks, outside fleets" suggests, say why with evidence.

## What to change

1. The predicate and the two constants (window minimum and growth factor) as named constants with a comment that states the rule and why the old values are kept for the other class.
2. `collect`, `ensure_heap_cells` and the initial `next_collection_` use them. No other change of behaviour: the order of the limit checks, when a collection is attempted before the hard limit, the headroom clamp, all as today.
3. Pinned peaks: `tests/benchmarks/baseline.txt` through its update mode (`tests/benchmarks/run.cmake -DUPDATE=ON`; read how the `benchmarks` test uses it), every test that pins a `peak heap cells` or `peak_heap_cells` value (find them all: backend tests, CLI expectations, stdlib tests' `--stats` output, UI tests, docs examples checked by `scripts/check-doc-examples.py`, `docs/GpuExecution.md` or other pages that quote a peak). For each changed expectation, the report gives old value, new value, and why the run is in the new class. **A pinned peak of a run in the old class must not change; if one does, the predicate is wrong.**
4. Docs: `docs/runtime/NVM.md` "Garbage collection" (the schedule in one or two sentences, both classes, and that the schedule changes only `peak_heap_cells`), any page that states the old numbers; regenerate the wiki; `python3 scripts/check-docs.py`, `python3 scripts/generate-wiki.py --check`, `python3 scripts/build-site.py --check`, `python3 scripts/check-doc-examples.py --neura build/neura`.

Do not edit `CHANGELOG.md` (the lead writes the line) or `src/nvm/heap.hpp`.

## Tests

- New-class runs: a program that allocates less than the window never collects (its peak equals what it allocated); one that allocates many times the window with a small live heap collects about once per window (test through `peak_heap_cells` bounds, not through timing); the hard limit still forces a collection and a program that fits in the limit under the old schedule still fits (run a tight-limit program at its exact minimal limit under both binaries).
- Old-class runs: for task images, fleet members, and whatever else your predicate excludes, the peak, the traps, the scheduler's counters and the traces are byte for byte those of the base binary.
- **Differential limit sweep (the main evidence).** A script under `scripts/` or an extension of `scripts/runtime-identity.py` (your choice; say which and document it in the script's docstring) that runs every case of the identity corpus with the base binary and with yours at a sweep of `--max-heap-cells` values (at least: the default; 12 values spaced geometrically between the smallest limit at which the case first succeeds and its unlimited peak; that smallest limit minus one) and compares everything except the peak: exit status, output, trap text, steps, function steps where reported, host requests, and `--record` traces where the corpus records them. The peak must be equal for old-class cases and may differ for new-class cases. Zero other differences. Also run it with `NEURA_TIER_EAGER=1`.
- Each new test must fail without the change or under a deliberate fault (say which).

## Acceptance

```
cmake --build build --parallel 2 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"        # only the known passes.cpp:166 line may appear
ctest --test-dir build --parallel 2 --output-on-failure                 # all; native-c-import may fail for lack of DNS
python3 scripts/runtime-identity.py capture ... --run-option=--tier=0 ; capture --jit on ; NEURA_TIER_EAGER=1 ... ; NEURA_TIER_CHECK=2 ... ; compare each with tier 0
python3 scripts/tier-boundary.py --neura build/neura --out build/tier-boundary --jit off,on
```

plus the differential limit sweep, and CPU time (user + system, medians of 5, interleaved with the base binary) of `text-split`, `parse-results`, `binary-trees`, `object-churn`, `string-building`, `hash-map` and `sort` at full size, with the `collection` figure of the `tier: time:` line before and after, and peak resident memory (`ru_maxrss`) before and after for every `tests/bench-wall` program at full size. Memory will rise; report by how much. If any program's peak resident memory more than doubles or rises by more than 64 MiB, say so at the top of the report: the owner approved the schedule, not an unbounded memory cost, and the lead will decide.

## Files

`src/nvm/machine.cpp`; tests and expectations as found; `scripts/` for the sweep; `docs/runtime/NVM.md` and pages that quote changed numbers, with their `wiki/` counterparts. Nothing else.

## Report

As WORK_RULES.md says, plus: the list of every observable channel with file:line and your argument for each; the predicate; the table of changed expectations; the sweep's totals (cases × limits × modes, differences); the time and memory tables.

Machine etiquette: another agent is building and measuring on this 4-core machine. `--parallel 2`, long jobs one at a time, CPU times rather than wall times.

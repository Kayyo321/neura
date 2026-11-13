# Hand-off: Neura 0.2.1 performance work

You are taking over as **project lead and reviewer** of work in progress on the repository `Kayyo321/neura`. A cloud session did the work so far and stopped at a clean point. Everything it produced is on GitHub. Your job is (A) to pick up exactly where it stopped and (B) to carry the work to the goal below. Read this file to the end before you act; it is the brief. The details live in the files it names.

## 1. Goal

Neura compiles a statically typed language to deterministic neuron circuits (`.nvr` images) that a runtime, the NVM (`src/nvm`), executes. Version 0.2.0 is far slower than Java. The owner wants a **v0.2.1 release in which long-running programs run within about 2× of Java**, usable day to day, with every execution invariant of `docs/runtime/NVM.md` unchanged. The owner decided on 2026-11-13 to **hold the release until that is true** ("Hold for parity").

**Release gate:** at the `long` benchmark sizes (`scripts/bench-wall.py --long`, which sizes each program so that Java takes 2–4 s), on x86-64 Linux, medians of 5 runs on an idle machine: geometric mean of Neura / Java ≤ 2.0 and every program ≤ 3.0. Short runs are reported, not gated.

**Where it stands (measured, long sizes, one run each):** 3.6× (`collatz`) to 470× (`sha256`), geometric mean 45. Tables: `handoff/results/`. Why, and what whole-function compiled code can and cannot buy: `handoff/wo2g/census.md`.

## 2. Get the state

```sh
git fetch origin
git submodule update --init --depth 1
```

| Branch | What it is | Head |
| --- | --- | --- |
| `main` | Released line plus a second agent's editor tooling (see section 7) | `a0e0bc5` when this was written |
| `ccr-e433a631-4hzo4n` | **Milestone 1, pull request #35.** Reviewed, complete, green on Tier 1, Native code, Editor tooling and all four Tier 2 jobs (run 43390436929). Waiting for the owner's review and merge | `124546a` |
| `ccr-e433a631-4hzo4n-m2` | **Milestone 2, reviewed and landed so far**: PR 35's head, the shadow oracle (`NEURA_TIER_CHECK=2`), the heap behind one interface (`src/nvm/heap.hpp`) | `bd0a45a` |
| `ccr-e433a631-4hzo4n-m2-wip-2d` | **Unreviewed** implementer work: heap storage layout and sweep fix (order 2D). Based on `743ba38`, an ancestor of the m2 head | `54c179b` |
| `ccr-e433a631-4hzo4n-m2-wip-2g` | **Unreviewed** implementer work: whole-function programs in the interpreter, steps 0–2 (order 2G). Based on `bd0a45a`. Its last commit is marked WIP and was never built | `02a2d18` |
| `ccr-e433a631-4hzo4n-handoff` | This directory, `handoff/`, on top of the m2 head. Documents only. Never merge it; delete it when you no longer need it | — |

Work on `ccr-e433a631-4hzo4n-m2` (or a branch of your own from it). Do not merge the two WIP branches as they are: they are input to review (section 4).

Paths inside the copied documents: where an order or the rules name `handoff/...` as a place for logs and scratch output (`handoff/wo2d/`, `handoff/wo2g/`, …), use a scratch directory of your own outside the repository instead. `handoff/WORK_RULES.md` and the documents under `handoff/` are the real ones.

## 3. What is done

**Milestone 1 (PR #35).** Proofs fast enough to pay off (gadget advice, kept enumerations), lift hints for the converter's own blocks (LIFT operator codes 23 and 24), proof thresholds (a hinted block is proven after 4 runs), inline `float32`/`float64` in native code on x86-64 and AArch64, a time line in `--tier-stats`, a larger benchmark set with `long` sizes, and fixes: three defects of the overflow analysis (with an exhaustive fuzzer), use-after-free in three NeuraIR lowering paths, a signed overflow in `lift::evaluate`, the sanitizer job's time limit, the arm64 `neura-libclang` self-test (submodule moved), a code-scanning alert. Full-size results: float programs 13–29 s → about 0.2 s. Its description on GitHub is the record of what was verified. One thing is left: its description still says the second Tier 2 run was dispatched; that run is green in all four jobs, so update the Verification paragraph if the owner has not merged yet.

**Milestone 2, landed on the m2 branch.**
- `NEURA_TIER_CHECK=2`, the shadow-run oracle (`src/nvm/tier_shadow.inc`): a second execution at tier 1 compared with the run at every return of compiled code and at every event. It is the check for everything that follows, because `NEURA_TIER_CHECK=1` cannot follow calls or allocations.
- `neura::nvm::Heap` (`src/nvm/heap.hpp`): nothing outside it knows how a heap entry is stored. New CTest entry `backend-heap`; the required test counts in the workflows are 173 (Linux), 167 (macOS).

**Measured and decided on the way** (details in `handoff/PLAN.md`, sections "Evidence on the speed target", "2E withdrawn", "Direction change"):
- A dispatcher step costs about 60 ns (850–1000 instructions); a step in today's native regions 2.5–4.5 ns; a field or element access inside a region is a helper call of about 20 ns; loops that already run entirely as native code are still 3.6–4.7× Java.
- The collection schedule (collect after growth by `max(256, live/2)` cells) cannot be changed without the owner: it is observable through `peak_heap_cells`, and fleets must be indistinguishable from serial runs. The attempt (order 2E) was stopped with evidence; its patch and a draft limit-sweep script are in `handoff/wo2e/`. Instead, the sweep must stop visiting every page ever created (item 8 of order 2D).

## 4. Work in flight: finish, review, land

Both were in their acceptance runs when the container restarted. The logs of those runs are gone except where noted; the commits are intact.

### 4a. Order 2D — heap storage and sweep (`…-m2-wip-2d`)

Order: `handoff/orders/ORDER_2D.md`, plus **item 8**, added later by message (quoted in full here): *the cost of a collection must not grow with the number of handles ever assigned. `sweep`, `for_each` and `next_live` visit only pages that hold a live entry, in ascending handle order; nothing observable changes; tests after 10^3 and 10^5 allocate-and-free cycles show the same pages visited; report callgrind instruction counts of `text-split` and `parse-results` at ten times their full size and of `binary-trees` at full size, base against new, with the instructions inside `State::collect` shown separately; and audit, read only, every part of `State::collect` whose cost is proportional to something other than live data and roots.*

State: seven commits (`f310436` … `54c179b`), tree clean. Of its acceptance, the log that survived (`handoff/wo2d/acceptance-queue.log`) shows: identity captures of the new binary identical across tier 0, default, eager, both tier checks and the extended corpus; tier boundary sweep done; the callgrind run for item 8 and the sanitizer suites had started and did not finish. No report was delivered.

To do: rebase onto the m2 head (it touches `src/nvm/heap.hpp`, the kernel argument loop in `src/nvm/machine.cpp`, the heap tests, `CMakeLists.txt`, `docs/runtime/NVM.md`); run the order's acceptance in full; then review as section 6 says. Specific things to verify yourself: captures byte-identical to those of the m2 head's binary; no interface change in `heap.hpp` beyond the published layout offsets; a widened collection reads back exactly the 64 bits stored; block reuse never leaks old contents; the sanitizer suites pass both with plain allocation and with the size-class allocator forced on; collection's share of `text-split` at ten times full size falls by most of an order of magnitude.

### 4b. Order 2G — whole-function programs, steps 0–2 (`…-m2-wip-2g`)

Design (accepted after review): `handoff/wo2g/design.md`. Census: `handoff/wo2g/census.md`. The implementer's recorded deviations from the design: `handoff/wo2g/deviations.md` (**read them; they have not been reviewed**). The instruction that started the implementation is summarized in section 5 below (stage 2G).

State: commits `d999375` (step 0: the on-trap `--task-dump` was written twice, once with a stale location, when a fault happened inside a region; now written where a fault becomes the run's trap), `2d765d2` (calls and returns inside programs), `8a38bb0` (dispatch blocks), `2df25ba` (a record/replay test), and `02a2d18`, a **WIP commit that was never built**: a three-state `TierOptions::programs` and a "steps in programs" statistic. What the lost acceptance logs showed before the restart: identity identical with `NEURA_TIER_PROGRAMS=1` and without, in every mode including both tier checks and the extended corpus; tier boundary 0 differences with programs on and off; **full CTest with programs on: `backend-tier`, `backend-jit` and `backend-tier-interpreter` failed** (tests about regions that find programs instead; the WIP commit was the fix in progress); full CTest with programs off had not finished.

To do: build the WIP commit, finish or redo it, get the full suite green with `NEURA_TIER_PROGRAMS=1` and unset; complete the acceptance the instruction asked for; review step 0 by itself and land it first as a fix (it needs a `### Fixed` line in `CHANGELOG.md`); then review steps 1–2 against the design rule by rule (R1–R6, the entry rules, the orders of effects in 4.1 and 4.2) and each deviation, and land them. Programs stay behind `NEURA_TIER_PROGRAMS=1` until native code for them exists and is not slower anywhere.

## 5. The plan from here

`handoff/PLAN.md` is the whole plan with its history; its section **"Direction change"** is the current one and overrides older sections. In short:

**Milestone 2 — foundation (one pull request from the m2 branch, after PR 35 is merged; merge `main` into the branch first, never force-push a shared branch).**
1. Land 2D (4a) and 2G steps 0–2 (4b).
2. 2G steps 3–7 of the design's section 9: helpers for allocation and growth, the other helpers (text, kernels with resume checkpoints), entries at host and scheduler resumes, stubs, proving at formation, extra oracle sync points, the `programs` statistics line, documentation. Still interpreter only, still behind the switch.
3. A generated-program fuzzer: programs with recursion, allocation, kernels and traps, with step, heap, frame and quantum limits swept so that a deoptimization or trap lands on every block, comparing tier 0 with programs on, under the oracle. Add it under `scripts/` and to the extended identity run. Every later stage depends on it.
4. Exit: identity, boundary, fuzzer and oracle clean with programs on; default behaviour unchanged; no speed claim.

**Milestone 3 — an optimizing compiler for function programs (several pull requests).** Stages, in order; each ends with the long-size table and instruction counts set against the budget of stage 3.0, and with a red-team order:
- **3S, first, a spike with nothing merged:** for `collatz`, `sha256`, `nbody`, `binary-trees` and `sort`, hand-written C that does exactly what Neura's rules oblige compiled code to do (overflow traps, bounds, nil and initialized checks, handle indirection and the liveness test, step counting at the granularity the design allows, allocation with handle numbers and cell accounting, collection on the 0.2.0 schedule), built with `gcc -O2`, timed against Java at long size. If a floor is above 2×, no compiler can meet the gate for that program: take the specific rule or limit to the owner with numbers before building anything.
- **3.0** design document to the standard of `handoff/wo2g/design.md`, with a per-benchmark budget table. **3.1** SSA form, builder from programs, verifier, an IR interpreter run under the oracle and the fuzzer. **3.2** deoptimization with frame rebuilding, step accounting, linear-scan register allocation, x86-64 code generation without optimization (must already beat today's regions everywhere). **3.3** scalar passes, check elimination, range analysis, loop-invariant motion with guarded entries. **3.4** inlining (direct, then guarded dispatch and closure calls). **3.5** heap: address reuse, inline field, element and allocation fast paths, type-test elision. **3.6** floats in vector registers, `sqrt` and `abs` as instructions. **3.7** collection speed and the schedule decision. **3.8** the modes not covered at first (tasks, `--profile`): support them or state them as limits; they may be slower, never different. **3.9** AArch64, enabled only if CI passes identity by execution.

**Milestone 4 — release.** `handoff/PLAN.md`, "M4". Release candidates take the next free number at the time (`v0.2.1-rc.1` and `-rc.2` exist and belong to the other agent). Tier 2 must be green on `main` before a tag. The owner does the manual verification of `docs/ReleaseProcess.md` and approves the publish step.

Report to the owner after 3S, after 3.2, and after every stage; stop and ask if a stage misses its budget by more than a quarter.

## 6. How the work is done

You lead; implementers (sub-agents, one work order each, each in its own git worktree) write the code; you review every line and re-run every check. An implementer's report is never evidence.

- **Work orders** follow the examples in `handoff/orders/` (2C, 2D and 2G phase 1 are the best models): goal, owning stage, files allowed, contracts, tests that must fail without the change, acceptance commands, what to report. Large or risky orders run in two phases: a survey or design first, call sites only after you have reviewed it. `handoff/WORK_RULES.md` binds every implementer; give it to each one.
- **Implementers** commit on their own branch, never push, never touch `VERSION` or `CHANGELOG.md`, never weaken a test, stop and report when an order proves wrong (that happened with 2E and was right). Tell them to commit before any run longer than ten minutes and to write long runs' output to files: this work has lost runs to restarts twice.
- **Review checklist** (`handoff/PLAN.md`, "Operating model"): the diff stays in the allowed files; for each changed path, which invariant could move and the evidence it did not; nothing from an image used unproven; a failing operation either deoptimizes before any effect or traps in place after write-back and step charge; no dependence on time, addresses or iteration order; limits behave identically (sweeps); a **mutation check by you** (plant a fault, see a new test fail, restore); zero warnings outside `third_party/`; docs and generated wiki pages in the same change; every speed claim reproduced by you.
- **Landing:** `git merge --squash` of the implementer's branch onto the work branch, one commit per order, you write the `CHANGELOG.md` line, push at once.
- **Gates** (`handoff/PLAN.md`, "Gates"). Per merge: full `ctest`; `scripts/runtime-identity.py capture` at `--run-option=--tier=0`, then default, `--jit off`, `NEURA_TIER_EAGER=1`, eager with `NEURA_TIER_CHECK=1`, `NEURA_TIER_CHECK=2` plain and eager, each `compare`d with tier 0 and, for a refactoring, byte-compared (`diff -r`) with the previous binary's captures from the same images; `scripts/tier-boundary.py --jit off,on`; from milestone 2 on, all of it again with `NEURA_TIER_PROGRAMS=1`. Per milestone: the extended corpus, the sanitizer build (configuration of the `sanitizers` job in `.github/workflows/tier2.yml`, with `-DNEURA_STATIC_LAUNCHER=OFF`), `scripts/bench-wall.py` quick, full and long, the documentation checks (`scripts/test-checks.py`, `check-workflows.py`, `check-docs.py`, `generate-wiki.py --check`, `gen-kernels.py --check`, `build-site.py --check`, `update-stdlib-wiki-api.py --check`), the `--minimum` test counts in `tier1.yml`, `tier2.yml`, `release.yml` and `docs/ContinuousIntegration.md` for every CTest entry added, then the pull request with CI green and `tier2.yml` dispatched on the branch.
- **Measuring:** on a shared or noisy machine use instruction counts (`valgrind --tool=callgrind`) for bounds and interleaved CPU time (user + system from `os.wait4`) for comparisons; wall-clock benchmark tables only with nothing else running. State machine, OS and Java version with every table.
- **Pull requests** follow `.github/PULL_REQUEST_TEMPLATE.md` (Change / Stage and contract / Verification / Checklist), say exactly what ran and what was only read, and state plainly what is not met.

## 7. Rules that must not be broken

Execution invariants (also in `handoff/WORK_RULES.md`):
1. The circuit is the artifact. Native execution only for blocks proven by the structural proof and the overflow proof. LIFT hints and anything else read from an image are never trusted.
2. Identical at every tier and mode: exit selections, slot assignments, state operations and host requests in order; trap kind, reason, detail text, function, block, callers; `steps`, `function_steps`, `peak_heap_cells`, host request count; output; `--record` bytes; `--replay`.
3. No language change by this work. Checked arithmetic, bounds, uninitialized reads, use after `destroy`, canonical NaN all stay.
4. No step is ever added to any program. `tests/benchmarks/baseline.txt` does not change.
5. Timing affects speed only. No background threads. Code memory never writable and executable at once. No new dependency. The static launcher keeps building.
6. Compiler stages stay separate (`AGENTS.md`): this work is in the runtime (`src/nvm`, `src/lift`, `src/jit`, `src/circuit`), with hints only in `src/backend/converter.cpp`.

The repository's own rules: `AGENTS.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `docs/Wiki.md` (generated wiki pages), `docs/Site.md`.

The owner's standing decisions:
- A second agent builds the VS Code tooling and language features for it on its own branches and publishes its own prereleases. **Do not touch its branches, pull requests, tags or releases.** Merge `main` into your branch before each pull request and resolve conflicts in your own files only.
- One pull request per milestone. No pull request without the milestone's gate.
- Tags: try to push release candidate and final tags; if the tag rule refuses, the owner pushes them. Never move or reuse a tag. The owner approves the publish step.
- Target platform x86-64 Linux; AArch64 Linux native code enabled only if CI proves identity by execution; macOS keeps the portable interpreter.
- `Kayyo321/neura-libclang` is the owner's submodule repository; its arm64 self-test fix is merged and the pointer is moved.
- Do not put model or assistant names in code, comments or documents.

## 8. Decisions that will go to the owner (with numbers, when due)

- **Collection schedule.** If, after the sweep fix, collection still costs more than the gate allows: the way to change it without breaking the fleet guarantee is to give fleets and serial runs the same new schedule and make fleet admission count each member's collection threshold instead of its live cells. That changes `peak_heap_cells` everywhere and `Fleet::rounds()` for embedders.
- **Handle limit.** Handles are never reused and stop at 2^32; at the target speed an allocation-heavy program reaches that in minutes. Raising the default is a limits decision; reusing handles would change trap texts and use-after-`destroy` detection.
- **Anything stage 3S shows to cost more than 2× by the rules themselves.**
- **Short runs.** Each run re-proves its blocks (0.1–0.2 s for float programs); Java finishes the full sizes in 50 ms. Matching that needs proofs kept between runs, which means trusting a file. Not in the gate; the owner may ask.

## 9. Practical notes

- Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`. The built `build/neura` finds the standard library in the tree. Benchmarks against Java need OpenJDK 21 on `PATH`.
- **If your machine is not x86-64 Linux**, native code and the gate cannot be measured on it: macOS runs the portable interpreter only. Use a Linux x86-64 machine, container or CI for native-code work and for every benchmark table, and say which you used.
- `neura run` takes program arguments after `--`; benchmark images need `--max-steps 1000000000000 --max-heap-cells 4294967296`.
- Known in the cloud container only: `native-c-import` failed for lack of DNS; expect it to pass with network access. GCC 13 printed one `-Wmaybe-uninitialized` warning at `src/ir-optimization/passes.cpp:166` that CI's GCC 14 does not.
- Under the sanitizers `decompiler-behavior` takes over an hour and `backend-reference` longer; both have three-hour limits in the Tier 2 job.
- The Tier 2 workflow (`.github/workflows/tier2.yml`) is dispatched by hand on a branch; a full run takes about two and a half hours.
- A pre-existing wart found and left alone: `Heap::max_elements()` is `std::vector<std::uint64_t>().max_size()`, which differs between standard libraries, so one trap detail for an absurdly long `RepeatCollection` differs between Linux and macOS today.

## 10. First actions

1. Fetch, check out `ccr-e433a631-4hzo4n-m2`, build, run the full suite, and capture the identity corpus at tier 0 and default to confirm your machine reproduces the state (159 cases identical).
2. Read `handoff/PLAN.md` ("Direction change" first), `handoff/WORK_RULES.md`, `handoff/wo2g/design.md` and `deviations.md`, `handoff/orders/ORDER_2D.md`.
3. Finish, review and land 2D (4a), then 2G step 0, then 2G steps 1–2 (4b).
4. Check pull request #35: if merged, merge `main` into the m2 branch; if not, leave it to the owner and keep the m2 branch stacked on it.
5. Continue with section 5, and report to the owner at the points it names.

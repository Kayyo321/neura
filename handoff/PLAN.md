# Neura 0.2.1: Java-comparable speed — execution plan

## Context

Neura 0.2.0 (published 2026-11-12) runs the twelve `tests/bench-wall` programs 1.8× to 562× slower than OpenJDK 21 (measured: Release build, 4-core x86-64 Linux, full sizes). Measured causes:

- **Tier 0 dominates wall time even at <1% of steps.** Sieve: 73% of instructions in the reference evaluator for 1.2% of steps.
- **Overflow proofs are too slow to pay off.** nbody eager: 12.4 s proving 76 blocks, 0.4 s running. By default the proofs are deferred and 34% of steps stay on tier 0.
- **Compiled regions end at every call, return and allocation.** binary-trees: 68% of steps go through the per-step dispatcher (~60 ns each). Float operations in native code go through a helper call.
- **The heap is heavy**: three containers per object, 8 bytes per element, a two-level lookup and a derivation walk per access.
- Not problems: generated code (~14 instructions per step), start-up (hello 4 ms), compile time (0.2–1.0 s per program).

**Outcome wanted:** a 0.2.1 release in which Neura is roughly as fast as Java and usable day to day, with every execution invariant of `docs/runtime/NVM.md` unchanged.

**Speed target (release gate), measured on x86-64 Linux:**
- (Superseded by "Direction change" below: geometric mean ≤ 2.0 and every program ≤ 3.0 at the long sizes; the full sizes are reported, not gated.)
- Original G1: at new `long` sizes (Java median 2–4 s) every program ≤ 2.0× Java wall time, geometric mean ≤ 1.5×.
- Original G2: at the existing full sizes every program ≤ 1.5× Java.

**Decisions made by the user:**
1. The collection schedule may change (minimum window 256 → 1,048,576 cells; growth `live/2` → `live`), on the understanding that only `peak_heap_cells` changes. **Correction found in review:** in images with tasks a collection also releases unreachable channels' queued messages and disarms unreachable timers (`machine.cpp` 1282, 2806–2813), which can change a message-limit trap, a trace, or a deadlock outcome. So the new schedule applies **only to images without tasks**; images with tasks keep the 0.2.0 schedule exactly.
2. One pull request per milestone (M1, M2, M3, release) from branch `ccr-e433a631-4hzo4n`, restarted from `main` after each merge.
3. I prepare the release and try to push the `v0.2.1-rc.N` / `v0.2.1` tags; if the admin-only tag rule refuses, the user pushes them. The user approves **Publish** and publishes the draft.
4. Target platform x86-64 Linux. Every new native operation is also written for AArch64 Linux and enabled by default there only if CI passes runtime identity by execution. macOS keeps the portable interpreter.

**Update from the user (2026-11-12, during M1):** a second agent is building a VS Code extension, adds language features for it, bumps the version to 0.2.1 and publishes `v0.2.1-rc.1` as a prerelease. Rules for this plan:
- Do not touch its branches, pull requests, tag or release. Do not merge or revert its work.
- Before each of my PRs: `git fetch origin main`, rebase `ccr-e433a631-4hzo4n` onto it, resolve conflicts in my files only (CHANGELOG **Unreleased**, `--minimum` counts, docs), re-run Gate B.
- "No language change" still binds my orders; language features are the second agent's.
- M4 changes: `v0.2.1-rc.1` is the second agent's. The version bump may already be on `main`: verify each file of step 1 instead of redoing it. My candidates start at `v0.2.1-rc.2` (rc.3 if a fix is needed); the final `v0.2.1` contains the second agent's work and mine.

- Read-only look at `the second agent's tooling branch` (2026-11-12): it adds `src/tooling/*`, a `block_observer` in `RunLimits` (tier 0 only), and debugger inspection in `machine.{hpp,cpp}` (`debug_position`, `debug_snapshot`, `debug_children`, which read frames and heap entries between steps), and edits `converter.cpp`, `main.cpp`, `CMakeLists.txt`, `release.yml`, `VERSION`. Consequences for M2: merge `main` before 2C starts if that work has landed; 2C's heap interface must cover the debugger's heap reads; 2G/2H must keep the debugger's stepping on the reference path (its sessions run at tier 0); expect merge conflicts in `machine.cpp`, `main.cpp`, `converter.cpp`, `CHANGELOG.md`, `docs/lang/Tooling.md`.

## Direction change (user decision, 2026-11-13): hold 0.2.1 for parity

Asked what 0.2.1 should be after the measurements below, the user chose **"Hold for parity"**: no 0.2.1 release until long-running programs are within about 2× of Java. This section overrides the older parts of this file where they disagree (rule 7, G2, the M2 and M3 tables, the M4 timing).

**Why the old plan cannot get there (measured):** long sizes after M1 are 3.6× to 470× Java, geometric mean 45. A dispatcher step costs about 60 ns; a step in today's native regions 2.5–4.5 ns (25–60 instructions); loops that already run entirely as native code (`collatz`, `mandelbrot`) are still 3.6–4.7× Java. The census estimates whole-function code with inline calls and state operations at a mean of about 15×. The remaining gap is the code itself: one small block at a time, slots in memory, a check and a counter update per block, a handle-table walk per heap access, a call per function.

**Release gate (replaces G1 and G2):** at the long sizes, on x86-64 Linux, medians of 5 runs on an idle machine: geometric mean of Neura / Java ≤ 2.0 and every program ≤ 3.0. Short runs (the full sizes) are reported but are not a gate: a run that takes Java 50 ms cannot be matched while each run re-proves its blocks (0.1–0.2 s for float programs), and keeping proofs between runs would mean trusting a file. If the user wants short runs in the gate, that is a separate decision about proof caching.

**What is built instead of the old M2-native and M3: an optimizing compiler for function programs.** Rule 7 ("no new tier, no SSA compiler") is withdrawn; rules 1–6 stand.
- **Input:** the function programs of order 2G (every block of a function, with Call, Return, allocation and helper operations and the rules R1–R6 of `wo2g/design.md`). Only proven lifted blocks become code; an unproven block is a deoptimization point. Hints and anything read from an image stay untrusted.
- **Middle:** an SSA form over whole functions with inlined callees; values typed by width and signedness from the lifted instructions. Passes: constant folding and copy propagation, value numbering, dead code removal, range analysis, elimination of overflow, bounds, nil, initialized and type checks that an earlier check or a range covers, loop-invariant code and checks moved to guarded loop entries, inlining of direct calls and of dispatch and closure calls behind a class test taken from observed targets.
- **Leaving compiled code exactly:** every point where a check can fail has a map from each frame slot of each (possibly inlined) frame to an SSA value or constant, the block to resume at, and the steps completed; deoptimization rebuilds the canonical frames (as `push_frame` would have made them, frame limit checked) and tier 1 re-runs the block, so traps, their `callers`, `steps`, `function_steps` and `peak_heap_cells` are those of the reference. Step counts are kept per straight-line run of blocks, not per block; the step limit is checked at loop heads and calls with the remaining budget compared against the longest run to the next check.
- **Heap:** entries never move, so compiled code may keep an entry's address for a handle between points where the entry could be freed (`destroy`, a call that is not inlined). Field and element access become a load or store at the published offsets of order 2D; the handle-table lookup, the liveness test and the type test are checks like any other and are removed or hoisted when covered. Allocation gets an inline fast path that does exactly what `allocate` does (handle number, cells, limits), with the slow path through the reference code.
- **Back end:** linear-scan register allocation over the SSA form, x86-64 through the existing `MacroAssembler`; floats in vector registers, `sqrt` and `abs` as instructions where the instruction is the correctly rounded operation. AArch64 through the same assembler, enabled only if CI passes identity by execution.
- **Modes it does not cover at first** run function programs on the interpreter or today's tiers, unchanged: images with tasks (scheduler quanta, frame cells), `--profile`, the debugger and coverage (tier 0), `NEURA_TIER_CHECK=1`. Each is either added later with its own tests or stated as a limit in the release notes; none may behave differently, only slower.

**New milestones.**

| Milestone | Content | Exit |
| --- | --- | --- |
| M1 (PR 35) | Done; green on Tier 1, Native code, Editor tooling and all four Tier 2 jobs | User merges |
| M2 — foundation (PR 2, branch `ccr-e433a631-4hzo4n-m2`) | Oracle (landed); heap interface (landed); heap storage and sweep (2D, in acceptance); `--task-dump` fix; function programs complete in the interpreter behind `NEURA_TIER_PROGRAMS=1` (2G steps 1–7); the generated-program fuzzer with limit sweeps, tier 0 against programs (the old 2J, moved here because every later stage needs it) | Identity, boundary, fuzzer and oracle clean with programs on; default unchanged; no speed claim |
| M3 — optimizing compiler (several PRs) | Stages below | The release gate |
| M4 — release | As before, candidates numbered after whatever exists then | Tier 2 green on `main`, user verification, final tag |

**M3 stages, in order. Each ends with the long-size table and instruction counts set against the budget of stage 3.0, and with a red-team order.**

| Stage | Content |
| --- | --- |
| 3S — semantic floor (spike, first, nothing merged) | For five programs (`collatz`, `sha256`, `nbody`, `binary-trees`, `sort`), hand-written C that does exactly what Neura's rules oblige compiled code to do (overflow traps, bounds and nil and initialized checks, handle indirection and the liveness test, step counting at the granularity the design allows, allocation with handle numbers and cell accounting, collection on the 0.2.0 schedule), built with `gcc -O2`, timed against Java at long size. It shows what the semantics cost with a perfect compiler. **If any floor is above 2×, the gate cannot be met for that program by any compiler, and I bring the user the specific rule or limit that costs it, with numbers, before building anything.** The collection schedule is the known candidate (see below) |
| 3.0 — design (no code) | The compiler's design document, to the standard of `wo2g/design.md`: IR, construction from programs, the deoptimization maps and frame rebuilding, step and budget accounting, each pass with its soundness argument, inlining and its guards, heap access rules, register allocation, code layout; and a **budget table**: per benchmark, the instructions per iteration of its hot loop today, what each pass removes, and the resulting estimate against Java. I review it rule by rule; the implementation is held to the table |
| 3.1 | IR, builder from programs, verifier, and an IR interpreter run under the oracle and the fuzzer (correctness before any machine code) |
| 3.2 | Deoptimization with frame rebuilding, step accounting, register allocation, x86-64 code generation with no optimization. First measured numbers; must already beat today's regions on every program or the design is wrong |
| 3.3 | Scalar passes, check elimination, range analysis, loop-invariant motion with guarded entries |
| 3.4 | Inlining: direct calls, then guarded dispatch and closure calls |
| 3.5 | Heap: address reuse, inline field, element and allocation fast paths, type-test elision |
| 3.6 | Floats in vector registers, `sqrt` and `abs` |
| 3.7 | Collection: mark and sweep speed; the schedule decision below |
| 3.8 | The uncovered modes: support or state as limits |
| 3.9 | AArch64 (CI-verified) |

**Decisions that will come to the user, with numbers, when they are due (not now):**
- **Collection schedule.** The 0.2.0 schedule (collect after growth by `max(256, live/2)` cells) is observable through `peak_heap_cells` and bound to fleets (see "2E withdrawn" below). After the sweep fix it may still cost more than the gate allows on allocation-heavy programs. The way to change it without breaking the fleet guarantee is to give fleets and serial runs the same new schedule and make fleet admission count each member's collection threshold instead of its live cells; that changes `peak_heap_cells` everywhere and `Fleet::rounds()` for embedders. Needs the user's yes.
- **Handle limit.** Handles are never reused and stop at 2^32. At the target speed an allocation-heavy program reaches that in minutes. Raising the default limit is a limits decision; reusing handles would change trap texts and use-after-`destroy` detection.
- **Anything 3S shows to be above 2× by the rules themselves.**

**Honest risks.** This is the largest piece of work in the project so far: a compiler of the class of a simple optimizing JIT, built by agents under review, with container restarts. `binary-trees` competes with a generational copying collector using a non-moving mark-sweep collector behind a handle table and may stay above the per-program bound; Java vectorizes some loops (`sieve`, `matrix-multiply`) and this compiler will not. I report after 3S, after 3.2 and after every stage, and I stop and ask if a stage misses its budget by more than a quarter.

## Status on 2026-11-13 and how work resumes

The container restarted during the M1 close-out. Nothing committed was lost; three background jobs were stopped. The disk (build directories, scratchpad, agent worktrees) survived.

**Landed and pushed** (branch `ccr-e433a631-4hzo4n`, head `6334d44`): 1A, 1B, 1D, 1E with its fix order 1I, 1F, 1G, 1H, 1J, and the code-scanning fix in `scripts/build-site.py`.
- Deviation from the table below, already reported: 1D uses gadget **advice** (every range is still established by enumeration) instead of a certificate checker, and 1F added LIFT operator codes 23 and 24.
- Measured: `nbody`, `mandelbrot`, `spectral-norm` at full size 0.26–0.29 s (0.2.0: 13–29 s). Identity captures equal to tier 0 on 159 cases. Tier boundary sweep 136,816 cases, 0 differences.
- **Not met as written:** "tier 0 plus proving ≤ 5% of wall time" at full sizes. Proving is 130–180 ms of a 0.26–0.29 s run. Reported to the user with the M1 benchmark table, with options (cheaper regeneration/proof; nothing that trusts a file).

**`main` moved:** the second agent's work is merged (`e37c48b`), `VERSION` is 0.2.1, `v0.2.1-rc.1` is published. the second agent has a further pull request open (#34, its second branch) and may publish more candidates. My candidates take the next free number at the time; I never reuse or move a tag.
- Trial merge of `main` into my branch exists locally: branch `merge-main-trial`, commit `fcc8c3e`. Only `CHANGELOG.md` conflicted (both sides kept). All documentation checks pass on it. Not built or tested yet.

**Interrupted, to be resumed:**
- Order 1K (use-after-free in `lower_accessors`, `src/neura-ir`, plus audit): the agent's worktree holds an uncommitted fix (`lowering.cpp`, `lowering_accessors.inc`, `tags.cpp`, two new UI cases); `ui-accessors` passed under sanitizers there; its wider sanitizer run was cut off.
- Sanitizer sweep of the suites M1 did not need: 79 of 117 ran, only `ui-accessors` failed. 38 remain.
- Timing of `decompiler-behavior` under sanitizers: its first of two tests had not finished after about 50 minutes on a loaded machine. CI kills it at 3600 s.

**Tier 2 workflow (must be green on `main` before the final tag)** — state per job, from CI logs:

| Job | Failing | Owner / fix |
| --- | --- | --- |
| Sanitizers | `backend-jit` | Fixed (1J, landed) |
| Sanitizers | `ui-accessors` | 1K, in progress |
| Sanitizers | `decompiler-behavior` timeout | Step 4 below |
| Linux arm64 | `neura-libclang-load` (`self_test.c:171` expects signed `char`) | In the submodule repository `Kayyo321/neura-libclang`: I fix it there with access the user grants (decision below) |
| macOS x86-64, arm64 | only `stdlib-core` at `a7cfe76`; the second agent's next commit aligned that assertion | Expected green; confirm on my branch's Tier 2 run |

**Remaining M1 steps, in this order:**
1. Fast-forward `ccr-e433a631-4hzo4n` to `merge-main-trial` (a merge commit, no history rewrite), push, rebuild `build/` and `build-san/`.
2. Resume the 1K agent (same agent, same worktree): commit, rebase onto the merged head, finish its sanitizer and image-identity runs, report. I review the diff and the audit table, re-run `ui-*` and `frontend` under sanitizers, mutation-check, land, push.
3. Re-run the sanitizer sweep on the merged head: the 38 suites not yet run, the suites that changed with the merge, and the second agent's new tooling suites. A report in my code or in code older than both of us → a fix order. A report in the second agent's new code → I tell the user and do not touch it.
4. `decompiler-behavior` under sanitizers: one measured run on an otherwise idle machine. If it passes: give it a configurable CTest limit the way `backend-reference` has (`NEURA_REFERENCE_TEST_TIMEOUT` in `CMakeLists.txt`, set in the `sanitizers` job of `tier2.yml`), sized so the job stays inside its 300-minute limit. If it cannot fit: split the suite's two tests into two CTest entries (and raise the `--minimum` counts). If it fails: fix order.
5. Gate B on the merged head, then Gate C. Benchmarks (`bench-wall.py` quick, full, long, 5 runs) run **alone on the machine**: no agent and no other job while they run. Table into `docs/Benchmarks.md`; long sizes recalibrated to Java 2–4 s.
6. Open PR 1 with the repository's template; subscribe to its activity; dispatch `tier2.yml` on the branch; tell the user what is not met (the 5% criterion, the risk to G2 for short float programs) with numbers.

**Progress later on 2026-11-13:** steps 1–4 and 6 are done. Branch head `eba6cdb`; PR 1 is https://github.com/Kayyo321/neura/pull/35 (subscribed); Tier 2 is dispatched on the branch; the arm64 self-test fix is Kayyo321/neura-libclang#1 (submodule pointer moves after the user merges it). Of step 5, the local sanitizer re-run at the head and the long-size benchmarks (one run each: five runs would take many hours at the current speed) were still running when the PR was opened; the PR body says so and gets updated. M2 has started: branch `ccr-e433a631-4hzo4n-m2` (local, at the PR head), the 2B agent is fixing three review findings (`REVIEW_2B_findings.md` in the scratchpad), and 2C runs in two phases (`ORDER_2C.md`): survey plus interface header first, call sites only after my review of the interface.

**Evidence on the speed target (2026-11-13, after M1):**
- Long sizes, one run each: Neura / Java from 3.6 (`collatz`) to 470 (`sha256`), geometric mean 45.
- Census (scratchpad `wo2g/census.md`): a tier-1 step costs about 60 ns (850–1000 instructions); a native region step 2.5–4.5 ns (25–60 instructions); a state operation inside a region is a helper call of about 20 ns; collection is 192 s of `text-split`'s 330 s and 106 s of `parse-results`' 275 s, because the window is `max(256, live/2)` and the sweep visits every page ever created.
- The census's estimate: every step in compiled code with helper calls gives a mean of about 36× Java; with inline calls, returns and state operations about 15×. **M2 as planned therefore ends near 15×, not 8×, and G1 (2×) is not reachable by M2 + M3's passes on this evidence;** even pure integer loops already entirely in native regions are 3.8–5× Java. Reported to the user with a recommendation; the plan continues because every remaining order is needed on any path.

**M2 order as now run (replaces the table's order):** 2B oracle (landed) → 2C heap interface (landed) → in parallel: 2E collection schedule (`ORDER_2E.md`; new schedule only for runs where nothing but `peak_heap_cells` can change), 2D storage layout (`ORDER_2D.md`, reduced: one block per entry, elements at their width, size-class allocator; no flat handle table, no side bitmaps), 2G function programs in the interpreter behind `NEURA_TIER_PROGRAMS=1` (design in scratchpad `wo2g/design.md`, accepted; step 0 fixes a doubled `--task-dump` on a fault inside a region) → 2F sweep cost proportional to live pages (after 2D, same file) → 2H native code for programs, inline state operations first → 2I AArch64 ‖ 2J red team. A `sqrt` intrinsic (planned as 3E) moves up next to 2H: the kernel path is about 150 s of `nbody`'s long run.

**2E withdrawn (2026-11-13):** the implementer stopped the order with evidence, correctly. A fleet must be indistinguishable from running its members one after another (`docs/lang/GpuExecution.md`, test `fleet.corpus_matches_serial_runs`, which compares `peak_heap_cells` too), and fleet admission sums members' live cells including garbage, so fleets must keep the 0.2.0 schedule; then so must every serial run, or `neura test --stats` would report different peaks with and without a GPU. No predicate satisfies "only `peak_heap_cells` changes". The schedule therefore stays exactly as in 0.2.0. The cost is attacked where it is: a collection's sweep must not grow with the handles ever assigned (added to order 2D as item 8, with a read-only audit of the rest of `collect`). If collection is still a large share afterwards, the options (fleet and serial peaks may differ; or fleet admission counts cells after a collection) go to the user with numbers. The stopped patch and the draft limit-sweep script are in the scratchpad under `wo2e/`.

**Second restart (2026-11-13, about 10:25 UTC). State found, and how work resumes:**
- Nothing committed or pushed was lost. `ccr-e433a631-4hzo4n` (PR 35) is at `124546a`; `ccr-e433a631-4hzo4n-m2` is at `bd0a45a`. The cloud session's scratchpad (its documents are now under `handoff/`) and both agent worktrees survived.
- **PR 35:** the second Tier 2 run on `124546a` is **green in all four jobs** (sanitizers, Linux arm64, both macOS): the first fully green Tier 2 the repository has had. Tier 1, Native code and Editor tooling were green on the head. Remaining for M1: update the PR body with that result; the user reviews and merges; then check that code-scanning alert #1 closes.
- **2D (heap storage + sweep fix), agent worktree `agent-a820cbcb06671ed46`:** seven commits on `bd0a45a`'s parent line (`f310436` … `54c179b`), tree clean; it was in its acceptance runs (identity, callgrind, sanitizers). Resume the same agent: re-run only the acceptance jobs that had not finished (its `wo2d/queue.log` says which), then report. Then my review: the diff of `heap.hpp` in full, byte-identical captures against the base binary, instruction counts, the collection share of `text-split` at ten times full size, one mutation, sanitizers with the size-class allocator forced on.
- **2G (function programs), agent worktree `agent-a27204fb3bc701fc3`:** four commits (task-dump fix `d999375`; calls and returns `2d765d2`; dispatch blocks `8a38bb0`; a record/replay test `2df25ba`) plus uncommitted edits in `tier1.*`, `tier2.*` and tests. Resume the same agent: commit or discard the uncommitted edits knowingly, rebase onto the branch head after 2D lands (2D touches `heap.hpp` and the kernel loop only), finish the acceptance of steps 0–2 and report. Then my review of step 0 by itself (it can land first, as a fix), then of steps 1–2 against `wo2g/design.md` rule by rule.
- At most two agents at a time again; three on four cores made every acceptance run slow enough to be caught by a restart.
- Landing order: 2D → 2G step 0 → 2G steps 1–2 (behind `NEURA_TIER_PROGRAMS=1`) → 2G steps 3–7 → 2H native (inline state operations first) with the `sqrt` intrinsic → 2I ‖ 2J → Gate C → PR 2 from `ccr-e433a631-4hzo4n-m2` after PR 35 merges.

**Lessons applied from here on:**
- Every work order tells the agent to commit work in progress on its worktree branch before any run longer than ten minutes, so a restart costs a re-run, not the work.
- Long measurement jobs write their partial results to a file as they go (per-test lines), never only at the end.
- At most one agent builds while a sanitizer or benchmark job runs; benchmarks get the machine alone.

**Decisions by the user on 2026-11-13:**
- The arm64 self-test: I request access to `Kayyo321/neura-libclang` with `add_repo` (the user approves the prompt), fix `tests/self_test.c` there so it accepts the platform's `char` signedness without weakening what it checks on x86-64, open a pull request in that repository for the user to merge, then move the submodule pointer in `neura` in a commit of its own. Done during step 3–4, so that the Tier 2 run of PR 1 can show arm64 green.
- M2 while PR 1 is unmerged: a second branch, `ccr-e433a631-4hzo4n-m2`, stacked on PR 1's head. I push M2 commits only there. PR 2 is opened from it after PR 1 merges (rebased onto `main` then). These two branches are the only ones I push to.

**M2 start (after PR 1 is open):** the oracle (2B) is finished on branch `worktree-agent-a859f9eed8d50b921` (three commits on `caa9214`). It is rebased onto the merged head first, because the second agent changed `machine.{hpp,cpp}`; then my review (tests, measurements, a mutation check, the heap comparison read in full) and landing. M2 commits go to the second branch (decision above).

## Fixed rules (no work order may break these)

1. The circuit is the artifact. Native execution only for blocks proven by the structural proof and the overflow proof. LIFT hints and anything else read from an image are never trusted.
2. Identical at every tier and mode: exit selections, slot assignments, state operations and host requests in order; trap kind, reason, detail text, function, block, callers; `steps`, `function_steps`, `peak_heap_cells`, host request count; output; `--record` bytes; `--replay`.
3. No language change. Checked arithmetic, bounds, uninitialized reads, use-after-`destroy`, canonical NaN all stay.
4. No step is ever added to any program. `tests/benchmarks/baseline.txt` may change only for `peak_heap_cells` under decision 1.
5. Timing affects speed only. No background threads. Code memory never writable and executable at once. No new dependency; the static launcher keeps working.
6. Stages stay separate (AGENTS.md). M1 touches the converter (hints only) and the lift library; everything else is NVM/output-side.
7. **(Withdrawn by the user's decision of 2026-11-13; see "Direction change".)** ~~No new tier.~~ All optimization is done as passes on tier-2 function programs, keeping the existing rule "the slot map is the deoptimization map". A separate SSA compiler is not built in this plan.

## Operating model: lead and implementers

- **Lead (me):** writes each work order, owns contracts, reads every diff in full, re-runs every gate personally, merges or rejects, and writes the `CHANGELOG.md` lines at landing. An implementer's report is never evidence.
- **Implementers:** implementer agents, each in its own git worktree, one work order each, at most two at once (4 cores). They never push, open PRs, touch `VERSION` or `CHANGELOG.md`, or change an existing test expectation unless the order says so.
- **Red team:** after each soundness-critical order a fresh implementer agent gets only the code and the contract and must produce a failing input or a wrongly accepted mutation. Its cases become tests.
- **Work order contents:** goal; owning stage; files it may and may not touch; contracts to keep; acceptance commands; tests to add (each must fail without the change); docs to update; "report what ran and what was only inspected".
- **Review checklist (every order):**
  1. Diff stays inside the allowed files.
  2. For each changed path: which invariant could move, and the evidence it did not.
  3. Nothing from the image is used unproven.
  4. A failing operation is either re-run by tier 1 from block-entry state (allowed only when it fails before its first effect) or trapped in place after write-back, step flush and step charge.
  5. No dependence on time, addresses, thread order or container iteration order.
  6. Step, heap, frame, handle, quantum, message and fleet limits behave identically (limit sweeps).
  7. Mutation check: I revert the core change and confirm a new test fails.
  8. Zero compiler warnings outside `third_party/`; native launcher tests pass.
  9. Docs and generated wiki pages in the same change.
  10. Any speed claim reproduced by me.
- **Rejection loop:** findings go back to the same agent with file and line. Three failed rounds on one finding → I re-scope or redesign.
- **Landing:** an accepted worktree branch is rebased onto `ccr-e433a631-4hzo4n`, one commit per order, pushed after Gate B.

## Gates

- **Gate A (per order; implementer runs, lead re-runs):** Release build with no warnings; the CTest entries named in the order.
- **Gate B (per merge onto the branch):**
  - Full `ctest --test-dir build --parallel 4`; total time must not grow more than 10% (failures caused by this container, recorded in M0, are the only tolerated ones).
  - `scripts/runtime-identity.py capture` at `--tier=0`, then capture and `compare` for tiers 1 and 2 × `--jit off,on`, plain and with `NEURA_TIER_EAGER=1`.
  - `scripts/tier-boundary.py --jit off,on`.
  - `scripts/bench-wall.py --quick --runs 5`: no program more than 5% slower than at the previous merge unless the order predicted it; hello-world ≤ 5 ms.
- **Gate C (per milestone, before its PR):**
  - Gate B with `--extended`, plus `NEURA_TIER_CHECK=1` and, from M2, `NEURA_TIER_CHECK=2`.
  - Address + undefined-behavior sanitizer build (configuration from `tier2.yml`) running the backend, tier, jit, lift and concurrency suites and the identity corpus.
  - `bench-wall.py` quick, full and long, 5 runs; table recorded in `docs/Benchmarks.md`.
  - Documentation checks: `test-checks.py`, `check-workflows.py`, `check-docs.py`, `check-diagnostics.py`, `update-stdlib-wiki-api.py --check`, `generate-wiki.py --check`, `gen-kernels.py --check`, `build-site.py --check`.
  - `--minimum` test counts raised in `tier1.yml`, `tier2.yml`, `release.yml`, `docs/ContinuousIntegration.md` for every CTest entry added.
  - After the PR opens: Tier 1 and Native code CI green; `tier2.yml` dispatched on the branch (arm64, macOS, sanitizers) and its report read.

## M0 — baseline (lead, no PR)

1. Initialize the submodule; Release build in `build/`; full CTest with per-test times; record container-caused failures.
2. Sanitizer build in `build-san/`.
3. Identity captures and a 5-run benchmark table at the starting commit.

## M1 — no time on tier 0, floats inline (PR 1)

Expected: float programs from 13–29 s to under 1 s; worst ratio about 30×.

| Order | Content | Files | Runs with |
| --- | --- | --- | --- |
| 1A | Benchmark harness: `long` size token and `--long`; peak-RSS and build-time columns; five new program pairs (interface dispatch, closures, object-list churn, `Result`/`Optional` parsing, string split/join/format), added to the identity corpus | `scripts/bench-wall.py`, `scripts/runtime-identity.py`, `tests/bench-wall/**`, `docs/Benchmarks.md` | 1C, 1G |
| 1B | `--tier-stats` wall-time attribution (load, proving/searching, tier 0, dispatcher, compiled code, collection, host) as **one added line**; existing lines unchanged because `bench-wall.py` and `tier-boundary.py` parse them. Clock read only with the option on, only at category transitions | `src/nvm/tier1.*`, `machine.cpp`, CLI, `docs/runtime/NVM.md` | after 1A |
| 1C | Spike, nothing merged: profile the proof on `float64` divide/multiply/add and `int64` multiply graphs. Report time per phase (regeneration, unrefined pass, `range()` set/map rebuilding, the adjacent-pair retry loop at `overflow.cpp` 1029–1044, `whole_group` rescans) and what fixing those alone achieves | scratch | 1A, 1G |
| 1D | Proof speed-up to ≤ 5 ms for a `float64` divide graph. Step 1: the fixes 1C identifies inside the existing analysis (negative cache for failed pairs, no per-round container rebuilds). Step 2, unless step 1 already meets the bound: **certificate check.** The builder tags each neuron at its seven creation sites in `circuit.cpp` with the gadget that made it and records claims on **forms** (affine combinations), not single wires. A linear checker verifies that each node's stored edges equal the claimed form exactly, then accepts each claim by one local rule: interval containment; threshold pair on an integer-valued sum; enumeration bounded by assignment count; refinement of a partition of unity (for `BlockSynthesizer::close` trap sums and selector lanes). A failed claim fails the proof. Dispatch blocks keep `one_hot_bit`. `prove_graph` order: unrefined pass → certificate → existing refined analysis as fallback | `src/circuit/*`, `src/lift/overflow.*`, `proof.cpp`, `lift.cpp`, lift tests | after 1C |
| 1E | Red team on 1D: circuits accepted by the checker whose reference evaluation overflows; mutated claims must be rejected; slow test that every corpus graph the checker accepts is accepted by the unbudgeted refined analysis | lift tests | after 1D |
| 1F | Hint threshold lowered from 256 to the **measured break-even** (expected 16–64; lowering, regeneration and the unrefined pass are unbudgeted, so 1 would tax run-once blocks). A hint for every converter construction with neurons that is not lifted today (found with `NEURA_TIER_EAGER=1 --tier-stats` over the corpus): vocabulary, regeneration, lowering in `lift::evaluate`, a `lift-execute` case each | `converter.cpp` (hints only), `src/lift/*`, `tier1.*` | after 1D |
| 1G | x86-64 and AArch64 generators: scalar float add/subtract/multiply/divide/negate/compare/convert inline (SSE2 / scalar NEON, never fused) with canonical-NaN fix-up; integer divide/remainder inline with their checks | `src/jit` only | 1A, 1C |

**M1 exit:** tier 0 plus proving ≤ 5% of wall time on every bench program at default settings; hello-world ≤ 5 ms; total CTest time not higher; Gate C; PR 1.

## M2 — heap and whole-function compiled code (PR 2) — original table; the milestone is redefined in "Direction change"

Expected: worst ratio about 5–8×.

| Order | Content |
| --- | --- |
| 2B | **Shadow-run oracle `NEURA_TIER_CHECK=2`:** a second execution at tier 1 advanced to the primary's step count at every return from compiled code, host request and end; compares steps, every frame (function, block, return slot/block, slots, flags), globals, last handle, live/peak cells, collection threshold, polls/tick, queued messages; heap digest every 64th sync (`NEURA_TIER_CHECK_HEAP=1`: every sync); host completions shared. A test-only fault injection proves it detects divergence. `NEURA_TIER_CHECK=1` (undo and re-run, single frame, writes only) cannot cover the new operations: under it Call/Return/allocation/helper operations compile as deoptimizations, and the docs say `=2` is their check |
| 2C | **Heap becomes a real class** with a narrow interface, behavior-preserving: the ~125 direct `HeapEntry` field touches (state operations, kernels, host conversion, parallel export/import, tier-2 undo, the ~54 text sites) go through it. "Reference word → object" is one inline function, so a later change of reference representation stays contained. Identity byte for byte including `peak_heap_cells` |
| 2D | **New representation behind the interface.** One contiguous allocation per entry (16-byte header: handle, type, kind, flags). Objects and tuples: 8-byte fields + init bitmap. Arrays/lists/text/bytes: no per-element flags (as today); elements at their own width (1/2/4/8 bytes) with a canonical-value test on store — a value that does not fit converts that collection to 8-byte elements, because stores are unmasked today. Lists keep a separate buffer, so `AppendElement` and kernels never hold a pointer across growth or collection. Handle table: growable flat array (doubling; base reloaded after every helper), all-null pages released at sweep. Constant-time subtype test (ancestor display + interface bit set). Size-class allocator, side mark bitmaps. The size limit that selects a trap text (`cells.max_size()`, line 3457) is preserved as a constant. Same roots, same reachability, **same schedule**, so identity is byte for byte. Kernel read-only views become in-place for every element type |
| 2E | Collection schedule constants at lines 1287, 1305, 3928 **for images without tasks only** (decision 1); update pinned peaks (`fleet_tests.cpp`, `baseline.txt` via `-DUPDATE=ON`, `GpuExecution.md`); tests that task images are unchanged; NVM.md; CHANGELOG **Changed**. Own commit |
| 2G | **Function programs in the portable interpreter** (`tier2.*`, `tier2_steps.inc`, `machine.cpp`, `jit.hpp`). Unit = function. Entries: function entry, loop heads, resume blocks of calls. Per-entry `entry_reads` and `entry_canonical` (the three analyses rooted at pc 0 today become multi-entry); the return-slot register's width fact resets after a call. At formation all hinted blocks of the function are proven, so cold blocks do not stay stubs. Checkpoints at function entry, loop heads **and every resume block**; a Call's poll counts. New operations: Call, Return, AllocObject, AllocCollection/Repeat, helper calls for every other state operation and kernel. **Call**: commit assignments; frame limit → deoptimize; task images → frame-cell helper; arena growth → leave; push exactly as `push_frame`; caller's `frame.block` stays the call block; flush `done` and `function_steps` at every Call and Return. **Return**: deoptimize at callback boundaries, empty stack, a task's last frame, open scopes; all effects before the native return. **Effectful helpers** (kernels, allocation slow path, text building): write back, set `frame.block`, flush `done`, charge the step, then call; a fault is the trap in place, never a re-run. Re-run by tier 1 only for checks that fail before any effect |
| 2H | x86-64 native code for 2G: compiled-to-compiled calls (≤ 1,024 nested native activations, then leave; "left" unwinds every activation without touching state; one global `done` counter); inline state operations as check + load/store; inline allocation fast path repeating `allocate` exactly, slow path through the existing helper |
| 2I | AArch64 native code for 2G/2H (CI-verified; stays experimental unless the arm64 job passes) |
| 2J | Red team on 2G–2I: limit-sweep fuzzer (generated programs with recursion, allocation, kernels, traps; step, heap, frame, quantum limits swept so a deoptimization or trap lands on every block) comparing tier 0 with tier 2 interpreter and native; added under `scripts/` and to the extended identity run |

Order: 2B → 2C → 2D → 2E → 2G → 2H → (2I ‖ 2J).

**M2 exit:** ≥ 95% of steps in compiled code on binary-trees, hash-map, sort; dispatcher entered < once per 1,000 steps; every program ≤ 8× Java at long size; Gate C; PR 2.

## M3 — optimization passes on function programs (PR 3) — superseded by the optimizing compiler of "Direction change"

Each pass is small, has its own tests, and is checked by the same identity harness and oracle. Effects are never reordered, duplicated, removed or moved across a block boundary.

| Order | Content | Mainly helps |
| --- | --- | --- |
| 3A | Split each inline state operation into check and load/store micro-operations; **available-check elimination** (handle validity/type, nil, field initialized, bounds against the same length, global initialized), killed by writes that may alias, FreeObject, RemoveElement, calls and kernels; field/length load reuse under the same kill rules | all |
| 3B | General range analysis (today only ±1): overflow, negative-index, conversion and bounds checks on bounded induction variables. **Loop-invariant checks become entry conditions of a speculative loop-head entry** (the existing refuse-entry mechanism); if refused, the unspeculated program runs. Per-loop step and budget accounting folded to one update per iteration | sieve, matrix-multiply, nbody, fannkuch |
| 3C | **Inlining as a splice:** a direct callee's blocks (≤ 200 operations, depth ≤ 4) and dispatch-block arms holding ≥ 5% of executions are copied into the caller; the canonical frame is still pushed and popped, frame limit kept. Then, for callees with no safepoint, the push is deferred to that callee's deoptimization stubs. Disabled in images with tasks | sha256, spectral-norm, sort, hash-map |
| 3D | Register use: all allocatable registers instead of 7 pinned; per-loop pinning; vector registers for floats; spills only around helper calls | all |
| 3E | Kernel intrinsics where one hardware instruction is the correctly rounded operation (`sqrt`, `abs`; tested against the kernel on all `float32` and 10⁹ `float64` inputs; step charge kept). Type-test elision for handles read from statically typed slots, fields, elements, globals (loader `assign()` checks confirmed; nil and freed tests always stay) | nbody, spectral-norm |
| 3F | AArch64 parity for 3A–3E (CI-verified) | |
| 3G | Red team: adversarial programs per pass (aliasing stores in loops, list mutation under a speculative bounds entry, `destroy` in a loop, overflow exactly at a loop bound, recursion with inlining at the frame limit, heap limit inside an inlined callee, record/replay) plus the 2J fuzzer | |
| 3H | Only if image load exceeds 10% of wall time on a full-size program: loader decodes graphs without per-node allocations | |

**M3 exit:** G1 and G2; Gate C; PR 3.

**Stated risk now, not later:** binary-trees is bounded by never-reused handle numbers and a sweep that visits every dead object, so compiler work alone may leave it above 2×. If it or any program misses G1 after M3, I report numbers and profile and ask before continuing. First follow-up in that case: object addresses instead of handle numbers in slots and fields (handle number kept in the header, translated wherever reference bits enter or leave graph lanes).

## M4 — release 0.2.1 (PR 4a, PR 4b)

1. **PR 4a:** `VERSION` → 0.2.1; `stdlib/std/core/version.nra` (`Patch`); `tests/stdlib/core/values-tests.nra`; "describes Neura 0.2.0" in `docs/lang/Overview.md`, `prompts/neura/SKILL.md`, `prompts/neura/references/cli.md`, `docs/Install.md`; regenerate wiki and stdlib API pages; `docs/Release.md` rows for function programs, the proof checker, the oracle and the passes; refreshed `docs/Benchmarks.md`; `docs/Project.md` runtime-boundary paragraph; `docs/lang/Status.md`; CHANGELOG **Unreleased** complete, answering the 0.2.0 known-issue speed figures.
2. Trial: dispatch `release.yml` on the branch (four targets as a `-dev` version, publishes nothing) and `tier2.yml`.
3. After the user merges 4a and Tier 2 passed on `main`: push `v0.2.1-rc.1`; user approves Publish; then `v0.2.1-rc.2` (its update check 9 must run).
4. User does the manual verification of `docs/ReleaseProcess.md`.
5. **PR 4b:** rename **Unreleased** to `## [0.2.1] - <date>`; after merge push `v0.2.1`; user approves Publish and publishes the draft.

## Critical files

- `src/lift/overflow.cpp`, `proof.cpp`, `lift.cpp`, `recognize.cpp`; `src/circuit/circuit.cpp`, `block_synth.cpp`, `float_circuit.cpp`
- `src/nvm/machine.cpp` (heap 575–680, collector 1143–1367, step path 2860–3243, `state()` 3377–3600, kernels 3682–3903), `tier1.*`, `tier2.*`, `tier1_steps.inc`, `tier2_steps.inc`
- `src/jit/compiler.cpp`, `x86_64.cpp`, `aarch64.cpp`, `jit.hpp`
- `src/backend/converter.cpp` (hints only)
- `scripts/bench-wall.py`, `runtime-identity.py`, `tier-boundary.py`; `tests/benchmarks/baseline.txt`
- `docs/runtime/NVM.md`, `docs/Benchmarks.md`, `docs/Backend.md`, `CHANGELOG.md`

## Reused, not rebuilt

- `lift::evaluate` (`src/lift/evaluate.hpp`): the single definition of operation semantics.
- `Execution::State::state()`, `commit_exit`, `push_frame`, `allocate`, `ensure_heap_cells`: slow paths and trap raisers of all compiled code.
- `BlockSynthesizer` / `lift::replay` for regeneration; `GraphProofs` per-graph verdict cache; `one_hot_bit` for dispatch blocks.
- The tier-2 worklist dataflow and rewrite framework (`tier2.cpp` 144–159, 364–460) and its refuse-entry mechanism.
- `MacroAssembler` (`src/jit/assembler.hpp`) with its two back ends; `Program::load` (W^X).
- `runtime-identity.py`, `tier-boundary.py`, `tests/benchmarks/run.cmake -DUPDATE=ON`.

## Verification (end to end)

- Every gate above, run by me, with commands and results quoted in each PR description: what ran, what was only inspected, what only CI can show.
- Release evidence: benchmark table (quick/full/long, 5 runs, medians, Neura and Java, ratios, RSS); tier statistics with time attribution; identity captures equal across tiers 0–2 × jit off/on × eager × both checkers; fuzzer and sanitizer runs clean; links to the Tier 1, Tier 2, Native code and Release-trial workflow runs.

## Known risks and limits

- **Proof checker soundness** (1D): reviewer's confidence in reaching ≤ 5 ms is about 70%; regeneration time may be the floor. 1E blocks M1. If the bound is missed, the hint threshold stays higher and float programs pay a one-time proof cost.
- **G1 is not guaranteed**: binary-trees as stated; the passes of M3 are estimates until measured. M1 and M2 each land on `main` as releasable improvements.
- **Images with tasks** keep the 0.2.0 collection schedule, so allocation-heavy concurrent programs gain less.
- **Handle numbers are never reused and `max_handles` is 2³²**: a process that allocates more than 4.29 billion objects traps, as in 0.2.0. Not changed by this plan; it becomes reachable sooner as allocation gets faster. Lifting it needs the address-based follow-up and a limits decision.
- **AArch64** cannot be run here; each fix costs a CI round. It ships enabled only if proven.
- **Tag push** may be refused (admin-only rule); then the user pushes the three tags.

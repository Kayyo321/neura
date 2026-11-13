# Rules for every work order (Neura 0.2.1 performance work)

You are an implementer. A lead reviews every line you write and re-runs every check. Your report is not evidence; only reproducible commands are. A rejected order comes back to you with findings.

## Project
Neura compiles a statically typed language to deterministic neuron circuits (`.nvr` images) run by the NVM (`src/nvm`). Read `AGENTS.md`, and the sections of `docs/runtime/NVM.md` and `docs/Project.md` your order names, before editing.

## Invariants no change may break
1. The circuit is the artifact. Native execution is allowed only for blocks proven by the structural proof and the overflow proof. LIFT hints and anything else read from an image are never trusted.
2. Identical at every tier and mode: exit selections, slot assignments, state operations and host requests in order; trap kind, reason, detail text, function, block, callers; `steps`, `function_steps`, `peak_heap_cells`, host request count; stdout; `--record` bytes; `--replay`.
3. No language change. Checked arithmetic, bounds, uninitialized reads, use-after-destroy, canonical NaN all stay.
4. No step is ever added to any program. Do not touch `tests/benchmarks/baseline.txt`.
5. Timing may affect speed only. No background threads. Code memory is never writable and executable at once. No new dependency. The static launcher must keep building.
6. No dependence on addresses, container iteration order, thread order, or the clock for anything observable.

## Hard rules for you
- Work only in your worktree. Touch only the files your order allows. No drive-by edits, renames, or reformatting.
- Never push, never open a pull request, never edit `VERSION` or `CHANGELOG.md`.
- Never weaken, delete, skip or re-record an existing test or expectation unless the order says so explicitly. If an existing test fails, the code is wrong until proven otherwise; if you believe the test is wrong, stop and say so in the report.
- Every new behavior gets a test that fails without your change. Say in the report how you confirmed it fails without the change.
- Zero compiler warnings outside `third_party/` (`-Wall -Wextra -Wpedantic`). CI fails on any.
- C++20, the style of the surrounding file. Comments state what the code does or why, in present tense; no history, no references to this order.
- If docs change: `docs/X.md` pages listed in `scripts/generate-wiki.py` have a generated `wiki/X.md`; run `python3 scripts/generate-wiki.py` and `python3 scripts/check-docs.py`.
- Commit your work on your worktree branch in one or a few commits with plain descriptive messages (no AI or model names in messages). Do not amend commits of others.
- If the order turns out to be wrong or impossible as written, stop and report why with evidence. Do not improvise a different design.

## Build and test
```
git submodule update --init --depth 1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"     # must print nothing
ctest --test-dir build --parallel 3 --output-on-failure -R '<pattern>'
```
Other agents share this 4-core machine: use `--parallel 3` at most, and do not run the whole CTest suite unless the order asks (`backend-reference` alone takes ~25 minutes). The built `build/neura` finds the standard library in the worktree.

Differential checks (see the docstrings of the scripts):
```
python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/t0 --run-option=--tier=0
python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/jit --jit on
python3 scripts/runtime-identity.py compare build/identity/t0 build/identity/jit
NEURA_TIER_EAGER=1 python3 scripts/runtime-identity.py capture ... --out build/identity/jit-eager --jit on   # then compare with t0
python3 scripts/tier-boundary.py --neura build/neura --out build/tier-boundary --jit off,on
```

## Final report (your last message), in this order
1. What you changed, file by file, in two or three lines each.
2. Commands you ran and their results (pass/fail counts, timings), exactly.
3. What you only inspected and did not run.
4. Every place you were unsure the invariants hold, with file:line. Hiding a doubt is worse than failing the order.
5. The branch name and commit ids.

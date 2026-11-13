# Work order 1E — red team: break the overflow proof's advice and enumeration memo

Read `handoff/WORK_RULES.md` first; it binds you. Then, in your worktree: `git reset --hard ccr-e433a631-4hzo4n` (the branch head; your worktree may start from an older commit).

## What you are attacking

The last commit on the branch changed `src/lift/overflow.cpp`, `src/lift/lift.cpp`, `src/lift/proof.cpp` and `src/circuit/circuit.{hpp,cpp}`:

1. **Advice.** `Circuit::record_gadgets()` / `gadget_advice()` record which gadget instance built each neuron and the instance's input wires. `prove_no_overflow(graph, refine, work, resume, advice, use)` and `lanes_are_bits(..., advice)` use it through `Analysis::advice_cut()`, `Analysis::range(top, budget, rounds, start)`, `pair_range()` and `apart()`. `AdviceUse` is `none`, `first` or `decisive`. `prove_graph` in `lift.cpp` chains decisive → first → none.
2. **Canonical memo.** `Analysis::enumerate()` keeps results under a key with wires renamed by role (`memo_find`, `memo_keep`).

The claim under attack (docs/runtime/NVM.md, "Execution tiers", the overflow proof paragraphs; `src/lift/overflow.hpp`):

- **C1.** Whatever the advice (true, wrong, garbage, for another graph, self-referential, enormous) and whatever the use and the work given, if `prove_no_overflow` returns `safe`, then for every assignment of 0 or 1 to the input lanes: reference evaluation (stored order, checked signed 64-bit: bias, then each product, then each partial sum) does not overflow; every `proof.wires[w]` contains wire `w`'s value; every group's weighted member sum lies in the group's `range`.
- **C2.** If additionally `lanes_are_bits(proof, graph, lanes, …)` returns true, every listed output lane is 0 or 1 on every such assignment.
- **C3.** No call reads or writes out of bounds, whatever the advice contains.
- **C4.** A proof that ran out of work and is resumed with the same advice and use is `safe` only when C1 holds (it need not equal an unbounded run's intervals, but must be sound). `GraphProofs::verdict` called repeatedly with small `work` values, with advice, never returns an empty verdict for a graph where C1 or C2 fails.

Your job is to find a counterexample to any of C1–C4, not to confirm them. A counterexample is worth more than any number of passing cases. Think about where the argument is thinnest before writing generators. Places I consider thinnest, to start you off, not to limit you:

- `range()` with `start`: wires marked computed by the walk from `top`, cut wires that feed other cut wires, a start wire above `top`, a start that names `top`'s own wire or its readers, group members split between cut and cone, the `start_cone` limit, the first-round rule that turns a cut neuron whose sources are all marked into a computed one, constant neurons (no edges).
- The memo key: two different computations with equal keys. Group variables (the carrier is named as a free variable, other members as fixed wires of value 0), variables fixed by conditioning in `sum_range` (named as other wires with their current value), a wire that is a term and also read by a needed node, edge order, activation, the same wire read twice by one node, results kept after an overflow (`keep(std::nullopt)`), a result found again while `work_` is nearly spent.
- `advice_cut()`: recursion through instances that name each other, signals naming wires at or above the neuron, the "exactly an established group" shortcut when the group does not actually sum to that signal's weights, caps of 64.
- `prove_graph` (`lift.cpp`): the allowance accounting, `partial` carrying a proof made under one use into another, `resume && resume->safe ? *resume` taking an earlier proof's intervals.
- `apart()` and `is_settled` caching in the pair loops of `prove_no_overflow`.

## What to build

A fuzzer as new tests in `tests/lift/lift_tests.cpp` (suite `overflow`), seeded and deterministic, that in a Release build runs in at most 20 seconds in total, plus a larger run of the same generators that you execute once and report (aim for at least 10^6 graph × advice × use × work combinations; say how many ran).

Generators (all graphs small enough to enumerate every input assignment: at most 14 input lanes):

1. Circuit-library gadgets with their true recorded advice (`src/circuit/circuit.hpp`, `float_circuit.hpp`, and whatever else builds integer and float operations): float add, subtract, multiply, divide, compare, conversions at tiny formats; checked integer add, subtract, multiply, divide, remainder, shifts, comparisons, `choose` chains at widths 2–6; compositions of two operations.
2. Mutations of those: weight or bias changed by ±1 and by powers of two; an edge retargeted to another earlier wire; an activation flipped; a node duplicated; **appended reader nodes with weights near 2^62 on two or three wires**, both where those wires are in fact exclusive (a sound proof needs their correlation) and where they are not (the proof must fail). The second kind is the one a wrongly trusted cut would accept.
3. Random feed-forward ReLU graphs (4–10 inputs, 5–40 nodes, small weights, some weights near the overflow boundary).
4. Graphs made of many copies of one gadget where the copies differ in exactly one thing the memo key must distinguish (a weight, a bias, an activation, an edge order, which earlier wire is read, the value of a constant neuron they read), to provoke a false memo hit.

Advice for each graph: the true advice; the advice of a different graph; instances permuted; cuts drawn from random earlier wires of the right graph (plausible, in range); true cuts with one dependency removed; cuts naming the neuron itself or later wires; instances that name each other in a cycle; signals with duplicate wires or no wires; more than 64 instances or wires; indices far out of range.

For every (graph, advice, use, work) check C1, C2 and C4 by exhaustive evaluation of all wires over all input assignments. Use each `AdviceUse`, unlimited work and a sweep of small work values with resumption (`resume` pointing at the previous exhausted proof), and the `GraphProofs::verdict` chain.

Report how many generated cases were actually proven `safe` and how many unsound graphs (overflowing, or with a lane outside [0, 1]) the generators produced: a fuzzer whose graphs are never proven, or never unsound, tests nothing. If fewer than a few percent of cases fall in each class, fix the generators.

Then confirm the fuzzer has teeth: make three deliberate unsound edits to `src/lift/overflow.cpp` one at a time, in a scratch copy you do not commit (for example: drop the node biases from the memo key; in `range()` with `start`, mark a wire computed without pushing its sources; treat an advised cut wire's interval as [0, 1] regardless of `proof_.wires`). State for each whether a test of yours fails, and with what message. An edit the fuzzer misses means the fuzzer needs another generator.

## Files

You may edit only `tests/lift/lift_tests.cpp` (and add files under `tests/lift/` if a helper is cleaner). You may not change anything under `src/`. If you find a counterexample, do not fix it: commit the reproducer as a test that fails, say so at the top of your report, and give the smallest graph, advice, use and work that shows it, with the wire, the claimed interval and the actual value.

## Acceptance commands

```
cmake --build build --parallel 3 --target neura_lift_tests 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"
ctest --test-dir build --output-on-failure -R '^lift-overflow$'
```

Report as WORK_RULES.md says, with: counts per generator, per advice kind and per use (cases, proven safe, unsound, falsely accepted); the result of the three deliberate edits; the large run's totals and wall time; every place you think the argument is still thin and what you could not reach.

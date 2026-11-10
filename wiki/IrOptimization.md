# NeuraIR optimization

The IR optimization stage takes validated NeuraIR from the frontend and returns validated NeuraIR with the same observable behavior. It sits between lowering and neuron conversion:

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

The passes live in [`src/ir-optimization/`](https://github.com/Kayyo321/neura/tree/main/src/ir-optimization). The NeuraIR verifier that checks the stage boundaries is in [`src/neura-ir/verifier.cpp`](https://github.com/Kayyo321/neura/blob/main/src/neura-ir/verifier.cpp), so that later stages can reuse it. This page states what the stage guarantees, what each pass assumes, and what the tests establish. The general behavior contract is in [the project design](Project#neurair-and-optimization).

## Inspect IR before and after optimization

`emit-ir` optimizes by default. `--no-optimize` prints the IR exactly as lowered:

```sh
build/neura emit-ir --no-optimize tests/ir-optimization/fixtures/folding.nra > before.ir
build/neura emit-ir tests/ir-optimization/fixtures/folding.nra > after.ir
diff before.ir after.ir
```

Both modes print the same listing format. The symbol table is identical in both because no pass adds, removes, or renumbers symbols, and bodies stay in the same order. A diff therefore shows only rewritten instructions. `--no-optimize` also skips verification, so IR from a faulty lowering can still be inspected. `check` validates source and never runs the optimizer.

The listing is an inspection interface. The [NVM toolchain](NVM#toolchain-and-distribution) separately defines a versioned `.nir` file that holds NeuraIR before optimization. `neura emit-ir -o` writes one, whatever the listing's default, and `neura build` applies the optimization pipeline to source or `.nir` input. See [NeuraIR file format](NirFormat).

## Stage contract

- **Input.** The program must pass the verifier. Otherwise the stage reports `NeuraIR verification failed before optimization` at the offending node, which indicates a lowering defect rather than a source error.
- **Observable behavior.** Return values, the order and arguments of calls and external effects, writes to locals and globals, and termination are preserved. Operations that trap or whose result the language leaves open stay in place and keep their evaluation order, so every specified trap still happens at run time. The optimizer never chooses an overflow, division-by-zero, rounding, or lifecycle rule.
- **Global initializers.** A module or file global whose initializer is written as a literal is set when the image loads; any other runs at program start in source order ([Modules](Modules#initialization)). Rewrites never replace the root of a global's initializer, only expressions inside it, so folding `2 * 3` cannot change when its global is set.
- **Symbols and spans.** Symbols are unchanged. A folded literal takes the type and span of the expression it replaces. An operand that replaces a short-circuit operation keeps its own span. A branch region selected by a literal condition becomes a block with the branch's span.
- **Validation.** The verifier runs before the first pass and after every pass that changes the program. A failure names the pass and round, for example `NeuraIR verification failed after pass 'fold-constants' in round 1: ...`.
- **Determinism.** Enabled passes run in the fixed pipeline order below, whatever order they are requested in. Traversals follow program order, and no decision depends on hashing, addresses, or host integer widths. The same input and options produce the same IR.
- **Termination.** The pipeline runs in rounds until a round changes nothing. Every rewrite removes at least one IR node, so each changing round strictly shrinks the program and at most one round per original node can change anything. The driver checks that a pass reporting rewrites actually shrank the program and stops with an internal error otherwise.

## Semantics the passes rely on

The passes only evaluate operations whose results [Numbers](Numbers), [Core language](CoreLanguage), and [Types](Types) define and that do not trap.

| Operation | Defined today | Treatment |
| --- | --- | --- |
| `!`, `==`, `!=` on `bool` literals | Yes | Folded |
| `+`, `-`, `*`, unary `-` on integer literals with an in-range result, including `int` and `byte`, which are `int32` and `uint8` | Yes | Folded |
| The same operations when the result is out of range | Traps with reason 1 | Left in place |
| `/` and `%` with a nonzero divisor | Truncating division; `MIN % -1` is 0 | Folded |
| `/` and `%` with a zero divisor, `MIN / -1` | Traps | Left in place |
| `as` between integer types with the value in range | Yes | Folded |
| `as` out of range, and every conversion involving floats or enums | Traps, or not folded | Left in place |
| Comparisons of integer literals | Yes | Folded |
| Floating-point operations | Specified, but the optimizer does not implement IEEE arithmetic | Left in place; the circuits fold constant operands |
| String concatenation | Encoding and allocation failure are open | Left in place |
| Enum equality | Representation is open | Left in place |

Fixed-width arithmetic is computed in the C++ type matching the operand's signedness. Every addition, subtraction, multiplication, and division checks before it runs whether the C++ operation would overflow, so the optimizer itself never performs signed overflow.

### Pure and total expressions

Some rewrites drop an evaluation. They require the expression to be *pure and total*: evaluating it cannot write state, request an effect, fail, or diverge. The predicate is deliberately narrow:

- Boolean and integer literals.
- Loads of locals and parameters. Lowering guarantees that both are initialized when read.
- `!` of a pure and total operand.
- `==`, `!=`, `<`, `<=`, `>`, and `>=` on `bool` or integer operands that are pure and total. Comparisons cannot overflow.
- `&&` and `||` whose operands are pure and total.

Arithmetic that may overflow, calls of any kind, field and element access, allocation, collection operations, loads of globals, strings, and floats are all treated as effectful. An unused result alone never justifies removing a computation.

### Statements that never complete

Unreachable-code removal relies on statements after which control cannot reach the next statement in the same region: `return`, `break`, `continue`, a block whose region contains such a statement, a branch whose two arms both contain one, and `while (true)` with no `break` that leaves that loop. A `break` inside a nested loop leaves the nested loop, so it does not count.

## Passes

All five passes are enabled by default and run in this order.

### `fold-constants`

Replaces a unary or binary operation whose operands are all literals with the literal result, using the rules in the table above. Traversal is post-order, so nested expressions such as `(2 + 3) * 4 - 18 / 6` fold in one pass. Partial results stay: `(100 + 27) + 1` in `int8` becomes `127 + 1` and stops there, because `127 + 1` overflows. Operations with a non-literal operand are untouched.

### `simplify-short-circuit`

Rewrites `&&` and `||` when one operand is a Boolean literal. The right operand of a short-circuit operation is evaluated lazily, so each rewrite keeps exactly the evaluations of the original:

| Form | Result | Why it is safe |
| --- | --- | --- |
| `false && right`, `true \|\| right` | `false`, `true` | `right` is never evaluated |
| `true && right`, `false \|\| right` | `right` | `right` is always evaluated, and its value is the result |
| `left && true`, `left \|\| false` | `left` | `left` is evaluated once either way, and evaluating a literal has no effect |
| `left && false`, `left \|\| true` | `false`, `true` | Only when `left` is pure and total; otherwise left in place |

For example, `observe(value) && false` keeps the call because the call requests an effect, and `value / 0 == 1 && false` keeps the division because it may fail.

### `simplify-control-flow`

- `if (true)` or `if (false)`: replaces the branch with the selected region as a block. The block keeps the region's scope, so locals declared inside stay local. If the selected region is empty, the branch is removed.
- `while (false)`: removed, because the body never runs and evaluating the literal condition has no effect. `while (true)` is never removed, because removing it would change termination.
- Empty blocks: removed.

The pass applies only to literal conditions. Conditions become literals through `fold-constants` or `simplify-short-circuit`.

### `remove-unreachable`

In each statement region, removes every statement after the first statement that never completes. The removed statements cannot execute, so removing them changes no behavior, including termination. Any local declared in removed code can only be referenced in removed code, because its scope begins at its declaration. Its symbol stays in the table. Branch folding can expose more unreachable code. For example, `while (true) { if (false) { break; } }` loses its only `break`, which turns the statement after the loop into unreachable code. The loop itself stays.

### `remove-pure-discards`

Removes an expression statement whose expression is pure and total, such as `value < 3;` or `true;`. Expression statements that may fail, such as `value + 1;` or `value / 0;`, or that call a function stay.

## Tasks, polls, and scheduler operations

A program that uses [tasks](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md) contains three more operations (`task_scope`, `spawn`, `scheduler_call`) and a `poll` flag on calls and loops ([NeuraIR format](NirFormat#revision-19-tasks)). The stage contract is that the optimized program executes the same scheduler operations and the same polls in the same order as the unoptimized one, so the schedule at every quantum is the same tick for tick. No pass removes an executed call, an executed loop back edge, a `spawn`, a task scope, or a scheduler call, duplicates one, or moves one past another. The reasons, pass by pass:

| Pass | Why it preserves tasks, polls, and scheduler operations |
| --- | --- |
| `fold-constants` | It rewrites only unary and binary nodes whose operands are all literals. None of the task operations, calls, or loops is one, so the nodes it replaces carry no poll; it does descend into call arguments and replaces only the literal operands. |
| `simplify-short-circuit` | It drops an operand only when that operand is never evaluated (`false && right`, `true \|\| right`) or, for `left && false`, when `left` is pure and total, which excludes every call and scheduler call. A node it keeps is moved whole, so its poll flag moves with it. |
| `simplify-control-flow` | It removes the arm of a branch with a literal condition that cannot run, a `while (false)` (whose body and back edge never run), and empty blocks. A task scope is a statement region of its own kind and is never removed, even when empty, because its exit waits for tasks. A selected arm becomes a block and keeps its statements. `while (true)` stays. |
| `remove-unreachable` | It removes statements after one that never completes, which cannot execute. A task scope counts as never completing when its body does, and its exits still wait for its tasks, so the scope stays. |
| `remove-pure-discards` | It removes only a discarded expression that is pure and total. Calls, spawns, and scheduler calls are never pure, so none is removed. |

The traversals that find regions (`statement_regions`) and that decide whether a region completes (`breaks_out`, `always_returns`, `never_completes`) treat a task scope like a block. Polls are flags on call and loop nodes and nodes are never split, so a pass cannot separate a poll from its call. Passes keep evaluation order, which orders the scheduler operations. The verifier checks the three operations: a task scope appears in a function body and its identity local is a `uint64`, a spawn is inside a scope of its body and starts a function value of no arguments, and a scheduler call names a known operation with arguments and result of the types the operation requires.

The differential evidence is `ir-optimization-concurrency`: an independent NeuraIR evaluator with its own scheduler runs every concurrent fixture before and after optimization under several quanta and task limits and requires the same results, traps, host effects, and tick-for-tick schedule. It also checks structurally that every scheduler operation and poll the evaluator can reach is still present after optimization, and that clearing the polls changes the schedule at quantum 1, which shows the comparison can fail. `backend-concurrency` repeats the comparison from the converted image on the NVM. These are observed agreements on the fixtures, quanta, and limits they run, not a proof.

## NeuraIR verifier

The verifier reports issues in a deterministic order and is independent of the passes' own analyses. For example, its rule that a non-void function always returns mirrors the frontend's rule rather than reusing `remove-unreachable`'s analysis. It checks:

- **Symbols.** Owner, base, override, parameter, and type-parameter references resolve, and parameters belong to their function.
- **Bodies.** Each body has a valid owner of a kind that may own one, at most one body per owner, and no body for an external function.
- **Shape.** Statement operations appear only in statement regions and expression operations only in operand positions. Each operation has the expected number of operands and regions, and void expressions appear only as discarded statements.
- **Types.** Branch and loop conditions are `bool`. Unary and binary operators match their operand and result types, and binary operands have the same type. Short-circuit operations have a `bool` left operand and exactly one lazily evaluated `bool` right operand. Initializers, stores, returns, collection elements, and appended values are assignable to their targets. Call arity matches the target.
- **Literals.** Boolean literals are `true` or `false`. Integer literals are decimal and, for `int8` to `uint64`, in range.
- **State and scope.** Locals are read only after their initialization in an enclosing scope of the same body, and each binding is initialized at most once. Parameters are read only in their own function, and `const` bindings are never stored to.
- **Control.** `break` and `continue` appear inside loops, `return` appears only in function bodies and matches the return type, and a non-void function cannot finish without returning.
- **Provenance.** Every node has a source span.

## Tests and the limits of the evidence

The tests are in [`tests/ir-optimization/`](https://github.com/Kayyo321/neura/tree/main/tests/ir-optimization). Run them with `ctest --test-dir build --output-on-failure`. The CTest entries keep the kinds of evidence separate:

| Test | Kind of evidence |
| --- | --- |
| `ir-optimization-structure` | Structural. Expected IR shapes for each rewrite and for each case where a pass must decline, plus span preservation, pass order, the reported rewrite counts, idempotence, symbol preservation, and determinism of repeated optimization. |
| `ir-optimization-verifier` | The verifier accepts every lowered and optimized fixture and example, and it rejects 13 kinds of deliberately malformed IR at the right node. |
| `ir-optimization-behavior` | Behavioral. Optimized and unoptimized IR are compared by an independent reference evaluator, and the folder is checked against arithmetic oracles. |
| `ir-optimization-concurrency` | Differential and structural. Optimized and unoptimized concurrent fixtures give the same results, traps, effects, and schedule under several quanta; scheduler operations and polls survive optimization. |
| `ir-optimization-cli` | All examples, together and one at a time, are emitted twice with and twice without `--no-optimize`. Each mode must be byte-for-byte reproducible, and both must have the same symbol table. Option errors are also checked. |

The reference evaluator in `tests/ir-optimization/evaluator.cpp` is written separately from the optimizer. It uses sign-and-magnitude integers with a 128-bit product, while the optimizer uses pre-checked 64-bit arithmetic. It models `bool`, integers, floats through host IEEE arithmetic, conversions, string literals and concatenation, enums, locals, parameters, module globals, program initialization order, branches, loops with latches, direct calls, calls on module receivers including module-interface dispatch, objects with construction, fields, virtual dispatch, and destruction, and arrays and lists with versions. It records external calls as effects whose response is `void`. At a program trap it stops with a *trapped* outcome and the NVR reason code; at a fault NVM detects, such as an index past the end or a read of an uninitialized field, with a *faulted* outcome. Two runs agree when they have the same status, result, stop reason and location, effect sequence, and final global state.

The evidence covers the following:

- **Exhaustive folding over 8-bit operands.** For `int8` and `uint8`, every operand pair under `+`, `-`, `*`, `/`, and the six comparisons, and every operand under unary `-`, is folded and evaluated. The folder must fold exactly the operations whose result is defined, and must produce that result. A plain `long long` oracle checks the evaluator for `int8` to `int32`, `uint8`, and `uint16`.
- **Boundary samples for wider types.** For 16-, 32-, and 64-bit types, the same check runs over pairs of boundary values: the extremes, their neighbors, half-range values, and small values. For `uint32`, `int64`, and `uint64`, the evaluator is the only reference.
- **Differential runs.** Every function in the fixtures and the examples runs in both versions over a sampled domain: all values for 8-bit parameters when the input space has at most 70,000 points, boundary values otherwise, and both values for `bool`. Initializer bodies run in program order before the calls. Functions with parameters of other types are skipped and counted in the test output.
- **Hand-derived expectations.** Selected fixture calls run against the optimized IR with expected results and effects worked out from the source.

These checks establish observed agreement over the stated domains. Only the 8-bit folding check is exhaustive. None of them is a proof of equivalence. In particular:

- Nonterminating runs are compared up to a budget of 100,000 loop iterations and calls and a call depth of 64. Agreement at the budget is bounded evidence, not a proof that both versions diverge. The passes never add or remove an executed iteration or call, so both versions reach the budget at the same point.
- The evaluator and the compiler follow the same written rules, so a misreading of a rule that both share would not be caught; the circuit tests check arithmetic against independent C++ and hardware references.
- The evaluator makes its own assumptions where the language is silent: one storage location per module global, dispatch on module receivers by override chain, and `void` responses from external calls. Both versions run under the same assumptions, so these choices do not favor either one, but they are not language rules.

## Adding a pass

State the rewrite's preconditions in terms of the semantics table and the predicates above, and make every rewrite remove at least one node. Register the pass in `src/ir-optimization/optimizer.cpp` at its place in the pipeline. Add fixtures that show the rewrite and the cases where it must decline, extend the evaluator if the pass touches constructs it does not model, and update this page.

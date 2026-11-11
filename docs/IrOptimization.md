# NeuraIR optimization

The IR optimization stage takes validated NeuraIR from the frontend and returns validated NeuraIR with the same observable behavior. It sits between lowering and neuron conversion:

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

The passes live in [`src/ir-optimization/`](../src/ir-optimization/). The NeuraIR verifier that checks the stage boundaries is in [`src/neura-ir/verifier.cpp`](../src/neura-ir/verifier.cpp), so that later stages can reuse it. This page states what the stage guarantees, what each pass assumes, and what the tests establish. The general behavior contract is in [the project design](Project.md#neurair-and-optimization).

## Inspect IR before and after optimization

`emit-ir` optimizes by default. `--no-optimize` prints the IR exactly as lowered:

```sh
build/neura emit-ir --no-optimize tests/ir-optimization/fixtures/folding.nra > before.ir
build/neura emit-ir tests/ir-optimization/fixtures/folding.nra > after.ir
diff before.ir after.ir
```

Both modes print the same listing format. No pass removes or renumbers a symbol, and bodies stay in the same order. Only `inline-calls` adds symbols: the locals of each inlined copy, listed after the original table and named `$inline.<callee>.<name>`. Apart from those lines, the symbol table is identical in both, so a diff shows rewritten instructions and the added locals. `--no-optimize` also skips verification, so IR from a faulty lowering can still be inspected. `check` validates source and never runs the optimizer.

The listing is an inspection interface. The [NVM toolchain](runtime/NVM.md#toolchain-and-distribution) separately defines a versioned `.nir` file that holds NeuraIR before optimization. `neura emit-ir -o` writes one, whatever the listing's default, and `neura build` applies the optimization pipeline to source or `.nir` input. See [NeuraIR file format](NirFormat.md).

## Stage contract

- **Input.** The program must pass the verifier. Otherwise the stage reports `NeuraIR verification failed before optimization` at the offending node, which indicates a lowering defect rather than a source error.
- **Observable behavior.** Return values, the order and arguments of calls and external effects, writes to locals and globals, and termination are preserved. Operations that trap or whose result the language leaves open stay in place and keep their evaluation order, so every specified trap still happens at run time. The one exception reuses a value: `eliminate-common-subexpressions` may replace a repeated evaluation with the value of an identical earlier one whose operands have not changed, which cannot trap because the earlier one did not, including a repeated call of a pure function (see [pure calls](#pure-calls)). No operation is evaluated earlier than its place unless it is pure and total, and none is replaced by another operation that traps on different inputs. The optimizer never chooses an overflow, division-by-zero, rounding, or lifecycle rule.
- **Global initializers.** A module or file global whose initializer is written as a literal is set when the image loads; any other runs at program start in source order ([Modules](lang/Modules.md#initialization)). Rewrites never replace the root of a global's initializer, only expressions inside it, so folding `2 * 3` cannot change when its global is set.
- **Resources.** Optimization may reduce but never increase the resources a run consumes: its steps, its peak heap cells, and its frames. A pass therefore adds no evaluation that could take a step of its own, allocate, or call, and never makes a slot hold a heap reference longer. The only evaluation a pass may add is a pure and total one, which the converter computes in the graph of a block that runs anyway: `hoist-loop-invariants` evaluates its value once before a loop even when the loop then runs zero times. The consequence is one-sided: a program at a limit (`--max-steps`, `--max-heap-cells`, `--max-frames`) may complete only when optimized, and its unoptimized build may stop at that limit, never the reverse. Image bytes and neuron counts are compile-time costs, not run resources; inlining can raise them. `run --profile` attributes the steps of inlined code to the caller.
- **Symbols and spans.** No symbol is removed or renumbered. Inlining appends one local of the caller per parameter and local of each inlined copy, and one for the result of a callee with early returns; every other pass leaves the table unchanged. A folded literal takes the type and span of the expression it replaces. A propagated value takes the span of the read it replaces, and a reused value is a load with the span of the expression it replaces. An operand that replaces a short-circuit operation keeps its own span. A branch region selected by a literal condition becomes a block with the branch's span. Inlined nodes keep the callee's spans; the nodes that inlining adds (parameter initializations, the enclosing block or scoped value, the loop and breaks of an early-returning callee) take the call's span. Every inlined node records its call site (below).
- **Inlined call sites.** `IrProgram::inline_sites` lists every inlined call with its callee, its caller, the call's span, and the site of the call itself when the call was in code inlined earlier, and `IrNode::inline_site` names the site a node was copied from. Following the parents gives the caller chain the unoptimized program would have had, for a later stage that reports one. The sites exist only after optimization and are not part of a `.nir` file.
- **Validation.** The verifier runs before the first pass and after every pass that changes the program. A failure names the pass and round, for example `NeuraIR verification failed after pass 'fold-constants' in round 1: ...`.
- **Determinism.** Enabled passes run in the fixed pipeline order below, whatever order they are requested in. Traversals follow program order, and no decision depends on hashing, addresses, or host integer widths. The same input and options produce the same IR.
- **Termination.** The pipeline runs in rounds until a round changes nothing, then runs `inline-calls` once; if it inlined anything, the rounds resume. Every rewrite in a round removes at least one IR node, or keeps the node count and moves nodes out of a loop, so each changing round strictly decreases the pair (node count, sum over nodes of the loops around them) in lexicographic order. Inlining adds nodes but removes at least one call per rewrite and adds none, so the alternation also ends. The driver checks both measures after every pass that reports rewrites and stops with an internal error otherwise.

## Semantics the passes rely on

The passes only evaluate operations whose results [Numbers](lang/Numbers.md), [Core language](lang/CoreLanguage.md), and [Types](lang/Types.md) define and that do not trap.

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

Arithmetic that may overflow, calls of any kind, field and element access, allocation, collection operations, loads of globals, strings, and floats are all treated as effectful. An unused result alone never justifies removing a computation; the one call that may be removed is an unused call of a pure and total function, below.

### Pure calls

Two rewrites use the purity of the function a call reaches. The property is the one the frontend's [`#Pure` check](lang/Tags.md#pure) uses, computed by the same analysis ([`neura-ir/purity.hpp`](../src/neura-ir/purity.hpp)) over the whole linked program, so an untagged function that is pure benefits as much as a tagged one; the tag only makes "not pure" an error. A function is *pure* when it writes no global and no field or element of an object it did not allocate during the call, reads no reassignable global and no global of a mutable type, makes no host request (a kernel call is allowed), uses no scheduler operation or object identity, calls no function value, and calls only pure functions, a virtual call counting every override. A pure function may allocate, trap, and loop. It is *total* when it is pure and its body contains no loop and no operation that can trap (the [pure and total](#pure-and-total-expressions) operations, assignments to locals, blocks, branches, and returns), it takes part in no cycle of calls, and every call it makes is a direct call of a total function or a call on a module receiver with a fixed target that nothing overrides. Totality is claimed only where this syntactic rule proves it.

A call reaches a fixed target when it is a direct call, a call on a module receiver with a fixed target (see [effect summaries](#effect-summaries)), or a virtual call of a method with a body that no method in the program overrides.

- **Reuse.** `eliminate-common-subexpressions` treats a call as part of an expression it may reuse when its target is fixed and pure, its result is `bool`, an integer, or a float, and its arguments are themselves reusable. The summary of the call includes everything the callee reads, so the pass's own condition, that nothing between the definition and the occurrence writes what the expression reads, covers the callee's reads: the fields and elements of its arguments, and nothing else that can change. A pure function that reads only its arguments and immutable data reads nothing that can change, so nothing intervenes. The occurrence would have computed the same value or trapped the same way as the definition, which completed. A call whose summary is unknown, such as one that reaches a kernel, may write anything and so is not reused.
- **Removal.** `remove-pure-discards` removes a discarded call whose target is fixed and total and whose arguments are pure and total. It cannot fail, has no effect, and returns. The calls of a total function allocate nothing.

A call that may trap is never removed, even when its result is unused: it decides whether the run ends there. Neither rewrite applies to a call with a poll, so in a program that spawns no executed call is removed (see [tasks](#tasks-polls-and-scheduler-operations)). Both rewrites remove a call, so the run takes fewer steps and frames and may allocate less; removing a call that would have exceeded `--max-frames` or the heap limit is the one-sided change the resources contract allows. `tests/ir-optimization/fixtures/purity.nra` shows each rewrite and the cases where it must not apply: an impure call, a write to the object a pure method reads between two calls, a pure call that may trap, and a pure call that loops.

### Statements that never complete

Unreachable-code removal relies on statements after which control cannot reach the next statement in the same region: `return`, `break`, `continue`, a block whose region contains such a statement, a branch whose two arms both contain one, and `while (true)` with no `break` that leaves that loop. A `break` inside a nested loop leaves the nested loop, so it does not count.

### Effect summaries

The passes over the values of locals and inlining decide with effect summaries ([`effects.hpp`](../src/ir-optimization/effects.hpp)), computed on the tree without a control-flow graph or SSA form. The summary of a node covers it and everything below it, including lazily evaluated operands and every arm. It records, as over-approximations:

- the locals and parameters read and written, by symbol; a reference binding counts as both;
- fields and globals read and written, by symbol, or "any" after a call whose target is unknown. A module global accessed in a chain of modules is resolved by name for the concrete module, so such an access counts as touching any global;
- element reads and writes (arrays, lists, their lengths and versions, and text contents), allocation, and destruction;
- whether it may trap or fault. Only literals, loads of locals and parameters, `!`, `~`, comparisons and non-shift bitwise operations on `bool` and integers, and the structure of statements are free of traps; every call may reach the frame-depth limit;
- direct, virtual, and closure calls, host requests, and scheduler operations.

A function's summary is the union over its body, without its own locals, and over the functions it calls. Functions are summarized by strongly connected components of the call graph, callees first, each component iterated to a fixed point, so recursion is handled. A direct call uses its target's summary. A call on a module receiver has a fixed target when the receiver names the module, or is the enclosing module's receiver and no module derives from that module; it is summarized like a direct call of that target, which is the declared function or one that overrides it, found by name up the module chain as the converter finds it. Every other virtual call, every closure call, every call of a function without a body, and every host request is summarized as doing anything. Locals cannot be written through another function: closures capture by copy ([lowering](../src/neura-ir/lowering.cpp), `capture`), and reference bindings are materialized as shared storage before specialization.

## Passes

All ten passes are enabled by default and run in this order. `inline-calls` runs between rounds, as described under termination.

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

### `propagate-copies`

Replaces every read of a local with the value it was initialized from, then removes the initialization, when all of these hold:

- the local is of type `bool`, an integer, or a float, its only write is its `initialize`, nothing else names it (no reference binding), and it is not the hidden subject of a comparison chain;
- the value has the local's exact type and is a literal, or a load of a local that is never stored to and is initialized once, or of a parameter that is never stored to.

Every read sees the value of the latest execution of the initialization, and the source cannot change in between: it is never stored to, and it is initialized before the local in an enclosing scope, so it is re-initialized only when the local's scope is entered again. Copies of copies are followed to their source. A closure that captures the local copies its value, which is the propagated value. The replaced read takes the span of the read. Both the initial value and the replacement are pure and total, so the removal drops no effect and no trap.

### `eliminate-common-subexpressions`

In a statement region, a definition is an `initialize` of a local or a store to a local or parameter whose value `E` has at least two nodes. A later occurrence of an expression identical to `E` (same operations, types, symbols, and text, whatever the spans) is replaced with a load of that local when all of these hold:

- `E` is built only from literals, loads of locals, parameters, and globals, unary and binary operations and conversions on `bool`, integer, and float values, field reads, element reads, and lengths. String operations, which allocate, are not;
- `E` does not read the local itself, nothing names the local other than loads, stores, and its initialization, and `E`'s type is the local's type;
- the occurrence is in a later statement of the same region, or nested in one, so the definition has completed whenever the occurrence is evaluated;
- conservatively, everything between the definition and the occurrence, meaning the statements in between and the whole statement that contains the occurrence, writes neither the local nor anything `E` reads: no local `E` loads, no field or global it reads, no element when it reads elements, no destruction, and no call that may write any of these.

The occurrence is never evaluated earlier, and nothing is evaluated in its place but a load. `E` may trap: the definition evaluated `E` from the same operand values, and if that evaluation had trapped the run would have ended, so the identical occurrence cannot trap. A store's destination is never replaced. Overflow and bounds checks are not removed by any other reasoning, and no expression is rewritten into a different operation, such as a multiplication into a shift, that would trap on different inputs.

### `hoist-loop-invariants`

Moves an `initialize` statement from the top level of a loop's body to just before the loop when all of these hold:

- its value is pure and total (see above), so evaluating it once more, or once when the loop runs zero times, has no effect, cannot trap, and takes no step;
- it reads no local or parameter that the loop writes or binds anywhere: its condition, body, or latch;
- the local is written only by this initialization, nothing else names it, and it is not a comparison chain's subject.

Every iteration then sees the value it would have computed itself. A trapping invariant, such as `base * 2`, stays in the loop: moving it before a loop that runs zero times, or before an effect earlier in the body, would add or reorder a trap. Loop rotation, which would allow that behind a test that the body runs, is not implemented.

### `simplify-control-flow`

- `if (true)` or `if (false)`: replaces the branch with the selected region as a block. The block keeps the region's scope, so locals declared inside stay local. If the selected region is empty, the branch is removed.
- `while (false)`: removed, because the body never runs and evaluating the literal condition has no effect. `while (true)` is never removed, because removing it would change termination.
- Empty blocks: removed.

The pass applies only to literal conditions. Conditions become literals through `fold-constants` or `simplify-short-circuit`.

### `remove-unreachable`

In each statement region, removes every statement after the first statement that never completes. The removed statements cannot execute, so removing them changes no behavior, including termination. Any local declared in removed code can only be referenced in removed code, because its scope begins at its declaration. Its symbol stays in the table. Branch folding can expose more unreachable code. For example, `while (true) { if (false) { break; } }` loses its only `break`, which turns the statement after the loop into unreachable code. The loop itself stays.

### `remove-dead-stores`

Only locals and parameters of type `bool`, an integer, or a float are considered, never a local of a reference type: a slot keeps the heap entry it holds alive until it is overwritten or its frame returns ([NVM](runtime/NVM.md#garbage-collection)), so removing a store to it could raise the peak heap. Two forms are removed:

- every store to a local or parameter that is never read, and its initialization when the initial value is pure and total;
- a store to a local or parameter that a later store in the same region overwrites, when nothing in between reads it, the later stored value does not read it, and nothing in between contains a `break` or `continue` that leaves the region's loop. A `return` in between ends the local's life, so it does not prevent the removal.

When the stored value is not pure and total, the store becomes a discard of the value, so its evaluation, effects, and traps stay in place. An `initialize` with such a value stays, because a later store needs it.

### `remove-pure-discards`

Removes an expression statement whose expression is pure and total, such as `value < 3;` or `true;`, and an unused call of a pure and total function whose arguments are pure and total ([pure calls](#pure-calls)). Expression statements that may fail, such as `value + 1;` or `value / 0;`, and calls of any other function stay.

### `inline-calls`

Replaces a call with a copy of the callee's body. The call is a direct call, or a call on a module receiver whose target is fixed (see effect summaries). The callee must satisfy all of these:

- its summary allows no trap, no call of any kind, no allocation, no destruction, no field or element access, no access to a global that is resolved by name in a module chain, and no host request or scheduler operation. A global may be written, which cannot fail;
- its parameters and locals, and its result, are `bool`, integer, or float values, so no inlined slot holds a heap reference;
- it contains no object receiver, base initialization, reference binding, closure, task scope, spawn, or scheduler call; a module's receiver is replaced by a reference to the module the call runs on, which must be known;
- it is not recursive, has no host binding (so it is not a callback), and its name does not start with `$` (static initializers and generated methods);
- it has at most 40 nodes, unless its directive is `Always`, and its directive is not `Never`.

The program must not use tasks: every call of a program that spawns is a poll, so inlining any call would change the schedule. A call with a poll is never inlined. The caller must not be a callback.

The arguments initialize new locals for the parameters, once each, left to right, before the body, which is how the call evaluated them. The callee's locals become new locals of the caller, so each copy has its own. Where the copy goes depends on the callee's returns:

- **Returned call** (`return f(...)` with the same result type): a block of the parameter initializations and the body, whose returns now return from the caller with the same value. Any shape of body qualifies.
- **Single return at the end** (or none, for `void`): in an expression, a scoped value of the initializations, the statements, and the returned expression; as a discarded statement, a block that discards the returned value.
- **Early returns, none inside a loop**, with a `bool` or integer result: the body runs once inside `while (true)`, and each return stores its value to a new result local, initialized to `0` or `false`, and breaks. In an expression this is a scoped value whose result is that local. A float result, and a callee with a return inside a loop, are inlined only as a returned call.

Because the callee cannot trap, call, allocate, or touch the heap, no trap is raised inside inlined code, so no trap's function, caller chain, or `neura test` failure location changes, and the run uses one frame fewer per inlined call. Callees that may trap are left alone for that reason: a trap diagnostic reports the source location of its block's first check that can fail ([Backend](Backend.md#blocks-frames-and-exits)), and a test failure is reported at the innermost frame in a test file, so code merged into a caller's blocks would be reported at the caller's location or frame. The recorded call sites are what a source map would need to lift this. For steps, an inlined call no longer ends the caller's block with a call exit or resumes it after the return; the copy adds slot assignments and, for early returns, a loop whose header does no work, which the converter bypasses. The end-to-end comparison below checks that no compared run takes more steps. The directive is the `inline.directive` symbol attribute of [NeuraIR 1.10](NirFormat.md): `Auto` by default, `Always` to lift the size budget only, and `Never` to keep every call. A `.nir` file records it. In source, [`#Inline(always: true)`](lang/Tags.md#inline-and-noinline) sets `Always` and `#NoInline` sets `Never`; `#Inline` alone leaves `Auto`. When the pass refuses an `Always` function for any reason but the size budget, `optimize` reports warning `W0004` once per tag, before the first pass, with the reason `inline_refusals` gives in the words of the conditions above. `check` and `--no-optimize` do not report it.

## Tasks, polls, and scheduler operations

A program that uses [tasks](lang/Concurrency.md) contains three more operations (`task_scope`, `spawn`, `scheduler_call`) and a `poll` flag on calls and loops ([NeuraIR format](NirFormat.md#revision-19-tasks)). The stage contract is that the optimized program executes the same scheduler operations and the same polls in the same order as the unoptimized one, so the schedule at every quantum is the same tick for tick. No pass removes an executed call, an executed loop back edge, a `spawn`, a task scope, or a scheduler call, duplicates one, or moves one past another. The reasons, pass by pass:

| Pass | Why it preserves tasks, polls, and scheduler operations |
| --- | --- |
| `fold-constants` | It rewrites only unary and binary nodes whose operands are all literals. None of the task operations, calls, or loops is one, so the nodes it replaces carry no poll; it does descend into call arguments and replaces only the literal operands. |
| `simplify-short-circuit` | It drops an operand only when that operand is never evaluated (`false && right`, `true \|\| right`) or, for `left && false`, when `left` is pure and total, which excludes every call and scheduler call. A node it keeps is moved whole, so its poll flag moves with it. |
| `simplify-control-flow` | It removes the arm of a branch with a literal condition that cannot run, a `while (false)` (whose body and back edge never run), and empty blocks. A task scope is a statement region of its own kind and is never removed, even when empty, because its exit waits for tasks. A selected arm becomes a block and keeps its statements. `while (true)` stays. |
| `remove-unreachable` | It removes statements after one that never completes, which cannot execute. A task scope counts as never completing when its body does, and its exits still wait for its tasks, so the scope stays. |
| `remove-pure-discards` | It removes only a discarded expression that is pure and total, or a discarded call of a total function that has no poll. Every call of a program that spawns has a poll, so none is removed there; spawns and scheduler calls are never pure. |
| `propagate-copies` | It replaces loads of locals with literals or other loads and removes initializations whose value is a literal or a load. None of these is a call, a loop, or a task operation. A task scope's identity local is bound by the scope, not by an initialization, so it is never propagated. |
| `eliminate-common-subexpressions` | It replaces only expressions built from literals, loads, arithmetic, conversions, field and element reads, and calls of pure functions without a poll, with a load. Every call of a program that spawns has a poll, so it reuses no call there, and none of the others is a task operation. |
| `hoist-loop-invariants` | It moves only an initialization whose value is pure and total, which contains no call or task operation; the loop, its back edge, and its poll stay. |
| `remove-dead-stores` | It removes a store or initialization only when its value is pure and total; otherwise the value stays as a discarded expression in the same place, with any call or scheduler call in it. |
| `inline-calls` | It does nothing in a program that contains a task scope, a spawn, or a scheduler call, and never inlines a call with a poll. In a program without them, there are no polls and no schedule to keep. |

Only `inline-calls`, `eliminate-common-subexpressions`, and `remove-pure-discards` remove an executed call, and only in a program without tasks. A program that spawns may also contain the guard operations of [synchronized methods](lang/Concurrency.md#81-monitors), which are scheduler calls like any other, so `inline-calls` leaves the whole program alone and no pass moves an operation across a guard. The traversals that find regions (`statement_regions`) and that decide whether a region completes (`breaks_out`, `always_returns`, `never_completes`) treat a task scope like a block. Polls are flags on call and loop nodes and nodes are never split, so a pass cannot separate a poll from its call. Passes keep evaluation order, which orders the scheduler operations. The verifier checks the three operations: a task scope appears in a function body and its identity local is a `uint64`, a spawn is inside a scope of its body and starts a function value of no arguments, and a scheduler call names a known operation with arguments and result of the types the operation requires.

The differential evidence is `ir-optimization-concurrency`: an independent NeuraIR evaluator with its own scheduler runs the concurrent fixtures it models before and after optimization under several quanta and task limits and requires the same results, traps, host effects, and tick-for-tick schedule. It also checks structurally that every scheduler operation and poll the evaluator can reach is still present after optimization, and that clearing the polls changes the schedule at quantum 1, which shows the comparison can fail. `backend-concurrency` repeats the comparison from the converted image on the NVM. The evaluator models the scope operations only: scopes, `spawn`, `join`, and `is_finished`. The fixtures that use channels, `select`, cancellation, timers, or the `Std.Concurrent` helpers are compared by `concurrency-programs`, which builds each one optimized and unoptimized and requires the same output and status from the two images on the NVM under six quanta; that comparison has no independent model and cannot show an error both builds share. These are observed agreements on the fixtures, quanta, and limits they run, not a proof.

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

The new passes keep these invariants without any change to the verifier. Inlining gives each copy fresh locals owned by the caller, so every binding is still initialized once and read only in its owner's body after its initialization; parameter initializations come first in the copy's block or scoped value, which is their scope. A scoped value never contains a `return`: an inlined body in an expression either ends in its returned value or runs inside a loop whose `break`s replace its returns. `propagate-copies` and `remove-dead-stores` remove an initialization only when no load of the local remains and no store to it remains, since a store's destination is checked as a read.

## Tests and the limits of the evidence

The tests are in [`tests/ir-optimization/`](../tests/ir-optimization/). Run them with `ctest --test-dir build --output-on-failure`. The CTest entries keep the kinds of evidence separate:

| Test | Kind of evidence |
| --- | --- |
| `ir-optimization-structure` | Structural. Expected IR shapes for each rewrite and for each case where a pass must decline, plus span preservation, pass order, the reported rewrite counts, idempotence, symbol preservation (only inlined locals added), and determinism of repeated optimization. For inlining, also the recorded call sites and spans, the `Always` and `Never` directives, and that a program with tasks is left alone. |
| `ir-optimization-verifier` | The verifier accepts every lowered and optimized fixture and example, and it rejects 13 kinds of deliberately malformed IR at the right node. |
| `ir-optimization-behavior` | Behavioral. Optimized and unoptimized IR are compared by an independent reference evaluator, both as lowered and as a build prepares it, and with each new pass alone on its fixture. The folder is checked against arithmetic oracles, the effect order of reuse and inlining is checked against values worked out from the source, and six deliberately unsafe rewrites (a reuse after an operand write, a dropped trapping store, a hoisted trapping invariant, an argument evaluated twice, a dropped pure call that may trap, and a pure call reused after a write to what it reads) must be reported as differences, which shows the comparison can fail. |
| `ir-optimization-concurrency` | Differential and structural. Optimized and unoptimized builds of the fixtures the reference evaluator models (scopes, `spawn`, `join`, `is_finished`) give the same results, traps, effects, and schedule under several quanta; scheduler operations and polls survive optimization. |
| `ir-optimization-cli` | All examples, together and one at a time, are emitted twice with and twice without `--no-optimize`. Each mode must be byte-for-byte reproducible, and both must have the same symbol table apart from inlined locals. Option errors are also checked. Then the programs in `tests/ir-optimization/programs/`, those of `tests/backend/programs/`, and the cost benchmarks at their first size are built both ways and run on the NVM: the runs must have the same stdout, exit status, and trap diagnostic (without the image path and the function and block numbers), and the optimized run must not take more steps or peak heap cells. |

The reference evaluator in `tests/ir-optimization/evaluator.cpp` is written separately from the optimizer. It uses sign-and-magnitude integers with a 128-bit product, while the optimizer uses pre-checked 64-bit arithmetic. It models `bool`, integers, floats through host IEEE arithmetic, conversions, string literals and concatenation, enums, locals, parameters, module globals, program initialization order, branches, loops with latches, direct calls, calls on module receivers including module-interface dispatch, objects with construction, fields, virtual dispatch, and destruction, and arrays and lists with versions. It records external calls as effects whose response is `void`. At a program trap it stops with a *trapped* outcome and the NVR reason code; at a fault NVM detects, such as an index past the end or a read of an uninitialized field, with a *faulted* outcome. Two runs agree when they have the same status, result, stop reason and location, effect sequence, and final global state.

The evidence covers the following:

- **Exhaustive folding over 8-bit operands.** For `int8` and `uint8`, every operand pair under `+`, `-`, `*`, `/`, and the six comparisons, and every operand under unary `-`, is folded and evaluated. The folder must fold exactly the operations whose result is defined, and must produce that result. A plain `long long` oracle checks the evaluator for `int8` to `int32`, `uint8`, and `uint16`.
- **Boundary samples for wider types.** For 16-, 32-, and 64-bit types, the same check runs over pairs of boundary values: the extremes, their neighbors, half-range values, and small values. For `uint32`, `int64`, and `uint64`, the evaluator is the only reference.
- **Differential runs.** Every function in the fixtures and the examples runs in both versions over a sampled domain: all values for 8-bit parameters when the input space has at most 70,000 points, boundary values otherwise, and both values for `bool`. Initializer bodies run in program order before the calls. Functions with parameters of other types are skipped and counted in the test output.
- **Hand-derived expectations.** Selected fixture calls run against the optimized IR with expected results and effects worked out from the source.

These checks establish observed agreement over the stated domains. Only the 8-bit folding check is exhaustive. None of them is a proof of equivalence. In particular:

- Nonterminating runs are compared up to a budget of 100,000 loop iterations and calls and a call depth of 64. Agreement at the budget is bounded evidence, not a proof that both versions diverge. No pass adds or removes an executed iteration, and only `inline-calls` and the [pure-call](#pure-calls) rewrites remove executed calls; without them, both versions reach the budget at the same point. When a call was removed, the optimized run counts less and may get further, so a run whose unoptimized version reached a limit is compared only up to that point: the optimized run must have requested the same effects first.
- The resource comparison is observed on the runs `ir-optimization-cli` makes, and in the benchmark baselines, not proven for every program. The argument for each pass is given with it above.
- A trap diagnostic's location comes from a block-granular source map: the first check that can fail in the trapping block. The comparison of diagnostics holds on the compared runs; a pass that removes a repeated check, as `eliminate-common-subexpressions` may, could change which check is first in a later block, and so the reported location of a different check's failure, in a case the runs do not cover.
- The evaluator and the compiler follow the same written rules, so a misreading of a rule that both share would not be caught; the circuit tests check arithmetic against independent C++ and hardware references.
- The evaluator makes its own assumptions where the language is silent: one storage location per module global, dispatch on module receivers by override chain, and `void` responses from external calls. Both versions run under the same assumptions, so these choices do not favor either one, but they are not language rules.

## Adding a pass

State the rewrite's preconditions in terms of the semantics table, the predicates, and the effect summaries above, and make every rewrite of a pass that runs in rounds remove at least one node or move nodes out of a loop. A pass that adds nodes runs between rounds and must remove something the rounds never add, as inlining removes calls. Show that the rewrite never increases steps, heap cells, or frames. Register the pass in `src/ir-optimization/optimizer.cpp` at its place in the pipeline. Add fixtures that show the rewrite and the cases where it must decline, extend the evaluator if the pass touches constructs it does not model, and update this page.

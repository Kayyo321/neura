# Neura 0.3.0: cleanup, scoped resources, generators and events

This note fixes the design of the 0.3.0 milestone that adds `defer`, `using`, generators that produce the standard library's `Sequence`, and a task-local event library. All four are about what happens when a block is left or a computation is suspended, so they share one representation of a block's exit. The note builds on [storage and construction](Storage030.md), whose regions are released at the same exits and whose `new func` and `new bound` the event library needs, and on [semantic facts and representations](Representation030.md), whose retention summaries decide what may escape. It is a design note in the sense of [the 0.3.0 baseline](Release030.md). Rows move to [Core language](../lang/CoreLanguage.md), [Resources and concurrency](../lang/ResourcesConcurrency.md), [Concurrency](../lang/Concurrency.md), [the standard library specification](../lang/Stdlib.md) and [Backend](../Backend.md) in the change that implements them.

Status: *proposed* on 2026-11-17, and *implemented* on the milestone branch for the steps of the [order of work](#order-of-work) that the rows marked "implemented, proposed" belong to. The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written by autonomous agents and await the owner's confirmation; a row marked "implemented, proposed" is in the compiler and still awaits it. Later changes to a row are made here, with the reason, under [Decisions taken in the implementation](#decisions-taken-in-the-implementation).

None of this adds unwinding. A trap or `Process.exit` ends the run as it does today, and no deferred call, close or handler is promised after it. The collector never calls `close`.

A program that uses none of the four keeps its image bytes, its steps and its peak cells. The milestone changes no NVR version.

## What exists at the baseline

| Area | Baseline |
| --- | --- |
| Words | `defer`, `using`, `yield` and `iterator` are ordinary identifiers. `iterator` is the method of `Iterable<T>` and is written in more than 100 source files; `defer` names a function in one test program; `using` and `yield` name nothing. The parser reads `scope`, `select`, `spawn`, `last` and `set` by context (`at_contextual`). |
| Exits | Lowering emits `IrOp::Return`, `Break` and `Continue` with no exit work. The converter is the only place that acts on an exit: `FunctionCompiler::leave_task_scopes` emits one `ScopeExit` per open task scope, innermost first, at a `return`, `break` or `continue`, after copying a returned value to fresh slots. The NVM has region operations (NVR 1.7); the converter does not emit them yet. |
| Propagation | There is no postfix `?`. It belongs to the absence milestone, and rows below that mention it wait for it. |
| Resources | `Std.Io.Closeable` declares `close -> Result<Unit>` and `release<T>(resource, outcome)`, which combines a close error with an outcome. `File.with_open` and `TcpStream.with_connection` are the scoped helpers. |
| Sequences | `Sequence<T>` holds two function values, is consumed once (`invalid state: sequence already used`), and `Sequence.from_iterator` wraps an `Iterator<T>`. `for (x in s)` calls `iterator()`, then `has_next()` and `next()` (`Lowering::for_each_iterable`). `filter` already pulls and buffers inside `has_next`. |
| Suspended code | Only tasks suspend. Nothing in NeuraIR or NVR describes a suspended function. |
| Events | The standard library has no event or subscription type. |
| Tier 3 | Tier 3 inlines up to four levels and rebuilds every inlined frame on a deoptimization (`tier3_materialize`), from the slots and flags of its frame states. |

## `defer`

<!-- example: fragment: 0.3.0 design that the compiler does not accept yet -->
```neura
last log = audit.open_section("import");
defer log.end_section();
```

```text
defer-statement = "defer" call-expression ";"
```

| Rule | Status |
| --- | --- |
| `defer` at the start of a statement is the keyword when the next token is an identifier, `{`, `new` or `spawn`; no statement that begins so is valid today. In every other position it is an ordinary identifier, so `defer(x);`, `defer = 1;` and `let defer = ...` keep their meaning. | implemented, proposed |
| The operand is one call: of a function, a method, a static function, or a function value. A block, a `new` expression, `spawn`, an optional-chained call, and any other expression are `E0219`. | implemented, proposed |
| The call's result type is `void`. Any other result, including `Result`, is `E0471`: a deferred call's failure would be lost. A function that handles the result itself can be deferred. | implemented, proposed |
| At the `defer` statement the callee expression, the receiver and the arguments are evaluated once, in written order, and saved in hidden locals of the frame. Later assignments to the bindings they were read from change nothing. | implemented, proposed |
| The saved call runs once when its block is left by falling off its end, `return`, `break`, `continue`, or a propagating `?`. Calls deferred in one block run last in, first out. A block that is not left runs none. | implemented, proposed |
| A `defer` belongs to the innermost block that contains it: a loop body runs its deferred calls at the end of each iteration, and a branch of `if` at the end of the branch. | implemented, proposed |
| A `return` evaluates its expression first. The value is held while deferred calls run and is not evaluated again. | implemented, proposed |
| With class hooks: the reads of the callee, receiver and arguments are events at the `defer` statement; the admissions of the arguments to the callee's parameters are events when the call runs. | implemented, proposed |
| A saved array or reference stays valid until the call runs, because the block's region is released after the block's deferred calls. A saved value follows the escape rules of the binding it was read from; `defer` extends no lifetime. | implemented, proposed |
| `defer` is written inside a function body, at any block depth, including a lambda, a constructor and a destructor. At file level and in a startup block it is `E0472`. | implemented, proposed |
| A trap in a deferred call ends the run; the remaining deferred calls do not run. | implemented, proposed |

## `using`

<!-- example: fragment: 0.3.0 design that the compiler does not accept yet -->
```neura
let first_line = func(path: string) -> Result<string> {
    using (source = file.open(path)?) {
        return source.read_line();
    }
};
```

```text
using-statement = "using" "(" name "=" expression ")" block
```

| Rule | Status |
| --- | --- |
| `using` at the start of a statement is the keyword when it is followed by `(`, an identifier and `=`, or when its balanced `(...)` is directly followed by `{`, which no call is today. Otherwise it is an ordinary identifier. A keyword `using` that is not the form below is `E0220`. The form binds one resource; several resources nest. It has no type annotation and no `;` after its block. | implemented, proposed |
| The binding is immutable and visible in the block only. Its type implements `Std.Io.Closeable.Closeable`; any other type, and an acquisition written `nil`, is `E0474`. | implemented, proposed |
| `using` is written in a function or lambda whose result type is `Result<T>`, so that a close error has somewhere to go (`E0473`). A generator body is not such a function, so it holds no `using` of its own; a lambda inside it may. Outside a function body it is `E0472`. | implemented, proposed |
| The acquisition is evaluated once and bound with the binding's ordinary admission. If the admitted reference is nil the run fails with the existing nil-reference failure, and nothing is registered. If the acquisition returns early through `?`, traps, or its admission traps, nothing is registered. | implemented, proposed |
| After a successful binding, the admitted reference is saved in a hidden local, the *token*. `close` is called on the token, never on a later read of the binding, so a getter cannot redirect which object is closed. Saving the token and calling `close` on it are not hook events; the body of `close` has its ordinary events. | implemented, proposed |
| On every exit from the block, `close` is called exactly once. Nested `using` blocks close in reverse order of acquisition. | implemented, proposed |
| On a `return`: when `close` succeeds the returned value is unchanged; when it fails after an `Ok`, the function returns the close error; when it fails after an `Err`, the function returns a `Context` error whose cause is the original error and whose message is `closing the resource also failed: ` and the close error's message. This is `Closeable.release`'s rule, applied once per block, innermost first. | implemented, proposed |
| On falling off the end, `break` or `continue`: when `close` fails, the function returns `Err` of the close error, after the cleanups of the blocks around it. Execution does not continue as if the close had succeeded. | implemented, proposed |
| The resource may not escape its block (`E0475`): it is not returned, stored in a field, a global, an element or a binding of an outer block, sent on a channel, captured by a function value that escapes, or passed to a parameter whose retention summary is *retained elsewhere* or *unknown*. A value whose summary says it retains the resource, such as a reader constructed over it, is scoped in the same way. | implemented, proposed |
| A `spawn` that captures the resource must be inside a `scope` block that is itself inside the `using` block, so that the children are joined before the close; otherwise `E0475`. The existing transfer rules still apply to the capture. | implemented, proposed |
| An alias that the acquisition API kept elsewhere is not tracked. After the block it observes the resource's closed state; `using` proves no uniqueness. | implemented, proposed |
| A resource stays closed when `close` returns an error, as today. | implemented, proposed |

## Exit order

| Rule | Status |
| --- | --- |
| A `return` first evaluates its expression and admits the result, once. A fresh array it returns is moved to the caller's region then. The outcome is carried through every block that is left. | implemented, proposed |
| Each block that is left, innermost first, then does three things in order: its deferred calls and closes, last registered first; the join of its task scope, when the block is the body of `scope`; the release of its region. | implemented, proposed |
| So a `defer` inside a `scope` block can close a channel that lets the children finish before the join, and a `using` around a `scope` block closes only after the children have been joined. | implemented, proposed |
| An exit that a failed close turns into a return continues outward as a return, carrying the `Err`. | implemented, proposed |
| Cancellation is an ordinary outcome returned by an operation. It unwinds nothing by itself. | implemented, proposed |

## Generators

<!-- example: fragment: 0.3.0 design that the compiler does not accept yet -->
```neura
let squares = iterator(count: int) -> int {
    for (let index = 0; index < count; index++) {
        yield index * index;
    }
};

last small = squares(10).filter(func(square: int) -> bool { return square < 30; });
```

```text
generator = "iterator" [ type-parameters ] "(" [ parameters ] ")" "->" type block
yield     = "yield" expression ";"
```

| Rule | Status |
| --- | --- |
| `iterator` begins a generator when it is followed by an optional balanced `<...>`, a balanced `(...)`, and then `->` or `{`. The parser looks ahead over the brackets without parsing them. In every other position, including `collection.iterator()`, `let iterator = ...` and the call `iterator(x)`, it is an ordinary identifier. | implemented, proposed |
| The element type after `->` is required; `iterator(...) {` is `E0221`. | implemented, proposed |
| `iterator(P) -> T { ... }` is a function value of type `func(P) -> Sequence<T>`, with `Sequence` the class of `Std.Collections.Sequence`. It is written wherever `func` is: as a binding's initializer, a class or module member (where it may read `instance`), or an argument. The parameter list follows the grouped-parameter rules. | implemented, proposed |
| Calling it evaluates and admits the arguments like any call, allocates the generator's state, and returns a sequence. It runs none of the body. Its summary says it allocates; the body's effects belong to the later `has_next` and `next`. | implemented, proposed |
| A generator that captures outer bindings is a function value that captures: the rules of the storage milestone apply, and one that escapes is written `new iterator(...) -> T { ... }` (`E0479`). A generator that captures nothing needs no `new`. | implemented, proposed |
| In the body, `yield` at the start of a statement is the keyword, and a binding named `yield` is the existing `E0311`. The body of a nested `func` or lambda is not the generator's body. Outside a generator body `yield` is an ordinary identifier, and `yield` followed by an identifier, a literal or `new` there is `E0222`. | implemented, proposed |
| `yield e;` checks `e` against the element type and admits it once, as a result. `return;` and the end of the body finish the generator; `return e;` is `E0478`. | implemented, proposed |
| The first `has_next()` with nothing buffered runs the body to the next `yield` or to its end. Further calls while a value is buffered run nothing and consume nothing. `next()` returns the buffered value, after doing what `has_next()` does when nothing is buffered; it does not run past the yield it consumes. | implemented, proposed |
| After the body ends, `has_next()` is false forever and `next()` is the existing `no such element` trap. | implemented, proposed |
| Advancing a generator while its body is running, from inside its own body, is the trap `invalid state: generator already running`. Separate calls of the factory give independent generators, which may be nested. | implemented, proposed |
| The sequence is used once, as every `Sequence` is. Abandoning it, for example by `break` in the consuming loop, runs nothing more; the state is collected like any object. | implemented, proposed |
| A `yield` may not be reached with cleanup pending (`E0476`): it is not inside a `scope` block, and no `defer` precedes it in a block that encloses it. The diagnostic is at the `yield` with a label at the pending construct. A `using` cannot be pending, since a generator body has none. | implemented, proposed |
| Nothing owned by the frame may be in scope at a `yield` (`E0477`): an array binding, a primitive reference, a tuple or enum that contains one, or a closure environment held in a region. A generator's parameter may not have such a type. A block that uses an array and ends before the next `yield` is accepted. A list is the owned alternative. | implemented, proposed |
| With class hooks: a parameter or local of the body is a binding with the events of a function's, whether it lives in the frame or in the generator's state. The generated `has_next` and `next` add none. The library's wrappers around them are ordinary functions with ordinary events. | implemented, proposed |
| [The iteration contract](../lang/Stdlib.md#54-iteration) is narrowed in the same change: "`has_next` has no effect" holds for collection iterators. For every iterator, `has_next` consumes nothing and repeating it repeats no work. | implemented, proposed |

## Events

`Std.Events.Event` is a library module in `stdlib/std/events/event.nra`. It needs no language feature of its own.

| Declaration | Contract | Status |
| --- | --- | --- |
| `Event<T>` | The owner's side. `new Event<T>()`; `subscribe(handler: func(T) -> void) -> Subscription`; `emit(payload: T) -> void`; `view() -> EventView<T>`; `subscriber_count() -> int`. `#Final`, `#NotThreadSafe`. | implemented, proposed |
| `EventView<T>` | What an owner exports: `subscribe` only. It cannot emit and gives no access to the `Event`. `#Final`, `#NotThreadSafe`. | implemented, proposed |
| `Subscription` | The token of one registration. `unsubscribe() -> void` removes that registration and does nothing when called again; `active() -> bool`. Two subscriptions are equal only when they are the same object. `#Final`, `#NotThreadSafe`. | implemented, proposed |

| Rule | Status |
| --- | --- |
| `subscribe` retains its handler until `unsubscribe`. A handler that captures is therefore written `new func(...)`, and a bound method `new bound(receiver.method)`; a handler that borrows a frame is rejected by the storage milestone's rules. Collection is not unsubscription: an event keeps its handlers alive. | implemented, proposed |
| The same handler subscribed twice is two registrations with two tokens. | implemented, proposed |
| `emit` delivers synchronously, in registration order, to the registrations that exist when the emission starts. A registration added or removed by a handler takes effect from the next emission, so a handler removed during an emission is still called by it. A nested `emit` uses the registrations at its own start. | implemented, proposed |
| Handlers return nothing. A trap in a handler ends the run. | implemented, proposed |
| `emit` compares nothing and suppresses nothing. An owner commits its state first and then emits the old and new values it accepted, as a tuple payload; it does not read them again through a getter. | implemented, proposed |
| The three classes are not transferable, so giving one to a task is the existing `E0602`. | implemented, proposed |
| `emit` is not called under a monitor guard (`E0550`): in a `#Synchronized` method, in a lambda passed directly to `Guarded.with`, or in a function either of them reaches through calls the compiler resolves. A call through a function value is not followed, so the check is a check of the known call graph and not a proof. | implemented, proposed |
| The registrations are an immutable list that `subscribe` and `unsubscribe` replace, so `emit` copies nothing. `subscribe` and `unsubscribe` allocate; `emit` does not, beyond what handlers do. | implemented, proposed |

<!-- example: fragment: 0.3.0 design that the compiler does not accept yet -->
```neura
last subscription = account.balance_changed.subscribe(new bound(instance.on_balance));
defer subscription.unsubscribe();
```

## Lowering and artifacts

| Stage | Change | Status |
| --- | --- | --- |
| Parser | `Statement::Kind::Defer`, `Using` and `Yield` in `statement()`, with the lookahead above in helpers beside `at_scope_statement`; `Expr::Kind::Generator` in `primary()` beside the `func` branch, sharing `function_body`; `new iterator` in `new_expression`. | implemented, proposed |
| NeuraIR | Three operations, in the next free NeuraIR minor version when the first of them lands. The writer emits that version only for a module that uses one. `IrOp::Cleanup` is a statement of a block with one or two statement lists: what to run on every exit, and, when present, what to run on a `return` instead. `IrOp::Pending` is the returned value being carried, readable and assignable only in a `Cleanup`'s return list. `IrOp::Yield` is a statement with a suspension index, legal only in a function that carries the attribute `generator.resume`. | implemented, proposed |
| `defer` | The saved values are `Initialize` nodes of hidden locals `$defer<N>_<k>`; the `Cleanup`'s list is the call over loads of them. | implemented, proposed |
| `using` | `Initialize` of the binding; a trap when it is nil; `Initialize` of the token `$using<N>`; a `Cleanup` whose return list assigns `Pending` the result of `Std.Io.Closeable.close_outcome<T>(Pending, token.close())`, and whose other list returns `Err` when `token.close()` fails. `close_outcome` is a new library function that holds the combination rule; `release` is rewritten over it with the same results. | implemented, proposed |
| Generators | Lowering checks the body as a function body, including definite assignment and `last`. A new pass in lowering, `lower_generators`, then makes a hidden class `$generator<N>` that implements `Iterator<T>`: its fields are the resume index, the buffered value, three flags (buffered, finished, running), one `$local<M>` field per parameter, capture and written local whose scope contains a `yield`, and one `$temp<M>` field per hidden local whose scope does. A local whose scope contains no `yield` stays a local of the frame, which is where an array of a yield-free block lives. The body becomes the method `$resume() -> bool` with each moved local's accesses rewritten to field accesses, and `has_next` and `next` are generated over it. The generator expression becomes a function that returns `Sequence.from_iterator<T>(new $generator<N>(...))`. | implemented, proposed |
| Verifier | A `Cleanup` is a direct statement of a block. `Pending` appears only in a return list of a function that returns a value. A `Yield` is a direct statement, its indexes are 1 to *n* without gaps, no local symbol's scope contains one, and it is not inside a `TaskScope` or after a `Cleanup` in an enclosing block. | implemented, proposed |
| Converter | `FunctionCompiler` keeps one exit stack (`cleanup_stack.hpp`) in place of the storage milestone's scope list: each block's region is a `Region` entry, a `scope` body adds a `TaskScope` entry above it, and a `Cleanup` is an entry above both, pushed as they are entered. `leave_scopes(depth, edge)` emits each entry's exit work innermost first; `return`, `break`, `continue` and the end of a block call it, so a block's cleanups run, then its tasks are joined, then `release_region` releases its region. A `scope` block enters its region before `ScopeEnter`, which gives the order of the exit table. A `Return` inside a cleanup list is a return edge of the enclosing entries. A returned value moves its arrays to the caller's region, then is copied to fresh slots when a `Cleanup` is open, and otherwise spilled as the storage milestone does. | implemented, proposed |
| Converter, generators | A function with `generator.resume` starts with a comparison chain on the resume index, in ascending order, to the block after each `Yield`. A `Yield` stores its index and returns true. Nothing else is new: the state is an ordinary object. | implemented, proposed |
| NVR and tiers | No new operation, exit kind or trap reason. The traps are messages. Tier 2 and tier 3 see ordinary functions, calls and field operations. | implemented, proposed |
| Hooks | Three rows are added to the event derivation of the class-hook milestone (`docs/design/Hooks030.md` on its own branch) when both are merged: a read or store of `Pending` is no event; the `Initialize` of `$using<N>` copies the binding without a getter and `close` on the token admits no receiver; the store to a generator's buffer before a `Yield` is the result admission. In a generator's state a `$local<M>` field is a binding with the events of the local it replaces, except that copying an admitted argument into its field at the factory call is not a second admission; a `$temp<M>` field, the buffer, the index and the flags have none. The body of `$resume` keeps every event of the source body; the generated `has_next` and `next`, and their call of `$resume`, add none. | implemented, proposed |
| Decompiler | Nothing new to decode. With names, a `$generator<N>` class is shown as inferred generator state; without them it is an ordinary class. | implemented, proposed |

### Optimization and tiers

| Pass | Rule | Status |
| --- | --- | --- |
| IR effects | A `return`, `break`, `continue` and the end of a block have the effects of the cleanup lists they run. A `Cleanup` reads its hidden locals at every exit of its block, so the stores that fill them are never dead. | implemented, proposed |
| IR passes | A `Cleanup`, a `Pending` and a `Yield` are never removed, duplicated, merged or moved, and nothing is moved across one. No expression is available across a `Yield`. Code after a `Yield` is reachable. | implemented, proposed |
| IR `inline-calls` | A function that contains a `Cleanup` or a `Yield` is not inlined. | implemented, proposed |
| Tier 3 deoptimization | Holds by construction, and is tested. Saved `defer` values, tokens and the carried result are frame slots that the cleanup blocks read as ordinary operations, so every frame state between a registration and its exit lists them. A generator's state is an object whose fields are stored before `$resume` returns, so a frame state inside an inlined `$resume` holds only that call's slots. | implemented, proposed |
| Checks | `scripts/tier-boundary.py`, `NEURA_TIER_CHECK=2` and `NEURA_TIER3_VERIFY=1` run over programs that deoptimize between a `defer` and its exit, inside a deferred call inlined into its caller, inside a `close`, and inside `$resume` inlined through `has_next` into a consuming loop. Each IR rule above has a program in which the pass must leave the operation in place. | implemented, proposed: `tests/lifetime/programs/*-deopt.nra` under `programs.py` (see the decisions below), and `passes_leave_bodies_with_cleanups` for the passes. `generator-resume-deopt.nra` checks that `$resume` is the innermost frame of its deoptimization, not that tier 3 inlined it into the loop |

### Tooling

| Rule | Status |
| --- | --- |
| The formatter writes `defer` and one space before its call, `using (name = value) {` with one space before `(` (as `scope` has in its keyword list), `iterator(` with none (as `func(`), and `yield` and one space. | implemented, proposed |
| The language server colors the four words as keywords only where the rules above make them keywords, offers them in statement position, gives a generator the signature and hover of its function type, and lists a generator binding in the outline as a function. | implemented, proposed |
| A cleanup's operations map to the `defer` statement or to the closing brace of the `using` block, so a stop there names it. Step-in at `has_next` or `next` enters the generator body at the statement after the last `yield`. | implemented, proposed |
| The debugger shows a generator's state by its source names, with its flags, and never advances it. It runs no deferred call, close or handler. | implemented, proposed |

## Diagnostics

The milestone shares its assigned codes with the class-hook milestone, which uses `E0215` to `E0218`, `E0470` and `E0538` to `E0546`. `E0223`, `E0224`, `E0312` to `E0314`, `E0547` to `E0549` and `E0551` to `E0559` are assigned to neither and stay free.

| Code | Title | Span | Message and fix |
| --- | --- | --- | --- |
| `E0219` | malformed `defer` statement | the operand, or the token after `defer` | "'defer' takes one call", "a deferred block is not supported", or "expected ';' after the deferred call". Help: put the statements in a function and defer a call of it |
| `E0220` | malformed `using` statement | the first wrong token | "'using' binds one resource: using (name = value) { ... }", "a 'using' binding has no type annotation", "expected a block after 'using (...)'", or "a 'using' block has no ';' after its '}'". Help: nest a second `using` for a second resource |
| `E0221` | malformed generator or `yield` | the first wrong token | "a generator declares its element type: iterator(...) -> T { ... }", "'yield' takes one value", or "'yield' is a statement" |
| `E0222` | `yield` outside a generator | `yield` | "'yield' is only a statement in the body of an 'iterator'". A label names the enclosing `func` when there is one |
| `E0471` | deferred call with a result | the call, with a label at the callee's result type | "a deferred call returns nothing, and this one returns 'Result<Unit>'". Help: defer a function that handles the result, or use `using` for a `Closeable` |
| `E0472` | `defer` or `using` outside a function | the keyword | "'defer' needs a block of a function to leave". Help: move the statements into a function |
| `E0473` | `using` in a function that cannot return a close error | `using`, with a label at the function's result type | "'using' needs a function that returns 'Result<T>'" |
| `E0474` | `using` resource that cannot be closed | the acquisition | "'T' does not implement Closeable" or "the resource is nil" |
| `E0475` | scoped resource leaves its `using` block | the escaping use, with a label at the `using` | "'source' is closed at the end of its 'using' block and cannot be returned (stored, captured, sent, retained by 'f')" |
| `E0476` | `yield` with cleanup pending | `yield`, with a label at the `defer` or `scope` | "a generator cannot suspend while a deferred call (a task scope) is pending" |
| `E0477` | storage that cannot outlive a suspension | `yield`, with a label at the binding; or the parameter's type | "the array 'buffer' belongs to this frame and would not survive a 'yield'". Help: use a list |
| `E0478` | `return` with a value in a generator | the returned expression | "a generator's values come from 'yield'; 'return' only ends it". Fix: none; `yield` then `return;` changes behavior and is offered as marked |
| `E0479` | capturing generator that escapes without `new` | the generator expression, from `iterator` | "a generator that captures outer bindings escapes here", labeled "its state keeps the captures after the call that creates it". Fix: insert `new` (machine-applicable). The storage check reports the escape, as it does `E0453`, and does not name the capture or the escape's kind |
| `E0550` | event emitted under a monitor guard | the `emit` call, or the call that reaches it, with a label at the `#Synchronized` or the `Guarded.with` | "handlers would run while 'Account' is locked". Help: commit, leave the guard, then emit |

Each code gets its page under `docs/diagnostics/`, a line in the index, and a failing example. Existing codes are reused where they already say the right thing: `E0311` for a binding named `yield` in a generator body, the immutable-binding code for an assignment to a `using` binding, the type-mismatch code for a `yield` of the wrong type, `E0602` for an event given to a task, and `E0304` for `defer(f());`, with the help "write 'defer f();'" when no `defer` is in scope.

## Edge cases

Accepted unless marked *rejected*.

**`defer`**

1. `defer log.end_section();`, `defer release(handle, 2);`, `defer callback();` with a function value, `defer Registry.clear();`.
2. `defer` and its call on two lines, or with a comment between them: accepted; the formatter joins them and keeps the comment.
3. `defer a.b.c(d)(e);`: the deferred call is the last one; `a.b.c(d)` is evaluated at the statement.
4. `defer log.end_section()` with no `;`: rejected, `E0219`.
5. `defer log;` and `defer count + 1;`: rejected, `E0219`.
6. `defer { cleanup(); }`: rejected, `E0219`.
7. `defer new Guard();`, `defer spawn work();`: rejected, `E0219`; `new` and `spawn` after `defer` make it the keyword.
8. `defer (pick())();`: `defer` is an identifier here, so `E0304` with the help; a callee in parentheses is bound to a name first.
9. `let defer = func<T>(...) ...; defer(value);`: accepted, unchanged, as in `tests/backend/programs/generic-closures.nra`.
10. `defer;` and `defer` at the end of an unfinished line in the editor: `defer` is an identifier; `E0304` or `E0201` as today, and the rest of the block is still parsed.
11. `defer file.close();` where `close` returns `Result<Unit>`: rejected, `E0471`.
12. `defer compute();` where `compute` returns `int`: rejected, `E0471`.
13. Two defers in one block: run in reverse order.
14. A `defer` in a loop body: runs at the end of every iteration, and on `break` and `continue`.
15. A `defer` in one branch of `if`: runs at the end of that branch only.
16. `let n = 1; defer show(n); n = 2;`: shows 1.
17. `return compute();` after a `defer`: `compute` runs, then the deferred call, then the function returns the value.
18. A `defer` after a `return` that is taken: never registered, never run.
19. A `defer` inside `scope { }` that closes a channel: runs before the join.
20. A `defer` in a lambda: belongs to the lambda's block.
21. A deferred call that traps: the run ends; earlier defers do not run.
22. A `defer` whose argument is an array of the block: accepted; the region is released after the call.
23. A `defer` at file level or in `static scope`: rejected, `E0472`.
24. A `defer` of a generic function with explicit type arguments, `defer drop<int>(x);`: accepted.
25. A `defer` of a method on a hooked receiver: the receiver's getter runs at the statement, not at the exit.
26. `#Tag` above a `defer`: rejected, as a tag above any statement is today.

**`using`**

27. `using (source = file.open(path)?) { ... }` in a function returning `Result<T>`.
28. `using` with line breaks inside the parentheses, a comment before the block, an empty block.
29. `using (source = open()) { ... };`: rejected, `E0220`, with a fix that removes `;`.
30. `using (a = open(), b = open()) { }`, `using (open()) { }`, `using (source: File = open()) { }`: rejected, `E0220`; the first by the `=` rule and the others by the `{` after the parentheses.
31. `using (source = open())` with no block, as in an unfinished edit: `E0220` at the next token; `source` is still bound for completion.
32. `using(config);`, `using(a, b).run();` and `let using = 1;`: accepted, `using` is an identifier.
33. `using (x = 1) { }`: rejected, `E0474`. `using (x = nil) { }`: rejected, `E0474`.
34. `using` in a function that returns `void` or `int`: rejected, `E0473`. In a lambda that returns `Result<T>` inside such a function: accepted.
35. An acquisition that returns `Err` through `?`: the function returns it; no close.
36. An acquisition whose value is nil at run time: the nil-reference failure; no close.
37. A body that returns `Ok(v)` and a close that fails: the function returns the close error.
38. A body that returns `Err(e)` and a close that fails: a `Context` error with cause `e`.
39. Two nested `using` blocks where both closes fail after `Ok`: the inner error, then a `Context` around it from the outer close.
40. `break` out of a loop from inside a `using` whose close fails: the function returns the close error and the loop does not continue.
41. `source = other;` in the block: rejected with the existing immutable-binding code.
42. `return Ok(source);`, `instance.kept = source;`, `list.append(source);`, `channel.send(source);`: rejected, `E0475`.
43. `last reader = new Reader(source);` used in the block: accepted; `return Ok(reader);`: rejected, `E0475`.
44. `callbacks.subscribe(new func() { source.flush(); });`: rejected, `E0475`.
45. `scope { spawn work(source); }` inside the `using` block, with a transferable resource: accepted. A `spawn` capturing `source` when the `scope` is outside the `using`: rejected, `E0475`.
46. A binding of the same name in an outer block: shadowed as by any block binding.
47. A getter of the resource's class that returns another object: `close` is still called on the object that was admitted.
48. A `using` directly in a generator body: rejected, `E0473`, with the label "a generator returns its values through 'yield'". In a lambda in that body that returns `Result<T>`: accepted.
49. A trap in the body: no close.

**Generators**

50. `let evens = iterator(limit: int) -> int { ... };`, a generator with no parameters `iterator() -> string { ... }`, one that yields nothing.
51. `iterator<T>(items: T[list]) -> T { for (item in items) { yield item; } }`.
52. `iterator(a, b: int) -> int { ... }`: grouped parameters.
53. `export let each = iterator() -> Item { ... };` as a class member reading `instance`.
54. `iterator(limit: int) { ... }`: rejected, `E0221`.
55. `collection.iterator()`, `let iterator = list.iterator();`, `iterator(x)`, `iterator < limit`, and a method named `iterator`: accepted, unchanged.
56. `iterator(` in an unfinished edit, with no closing parenthesis: an ordinary call, with today's `E0203`.
57. `yield;` and `let x = yield 1;`: rejected, `E0221`.
58. `yield (a + b);` and `yield -1;` in a generator body: accepted as yields.
59. `yield value;` in a `func` nested in a generator, and in an ordinary function: rejected, `E0222`.
60. `let yield = 1;` in a generator body: rejected, `E0311`. In any other function: accepted.
61. `yield "text";` in a generator of `int`: rejected with the type-mismatch code.
62. `return;`: accepted. `return 1;`: rejected, `E0478`.
63. A generator called and never advanced: none of its body runs.
64. `has_next()` three times, then `next()`: the body runs to its first `yield` once.
65. `next()` with no `has_next()`: runs to the first `yield` and returns its value; the statement after the `yield` has not run.
66. `next()` after the end: the `no such element` trap; `has_next()` stays false.
67. `break` in the consuming loop: no later `yield` runs.
68. Two generators from one factory advanced in turn: independent.
69. A generator whose body advances another generator, and one that iterates a list with for-each across a `yield`: accepted.
70. A generator that calls `next()` on its own sequence: the `generator already running` trap.
71. The sequence consumed twice: the existing `sequence already used` trap.
72. `yield` inside `scope`, or after a `defer` in an enclosing block: rejected, `E0476`. A `defer` in an inner block that ends before the `yield`: accepted.
73. An array binding in scope at a `yield`: rejected, `E0477`. An array in an inner block that ends before it: accepted, and it stays a frame local.
74. `iterator(items: int[]) -> int`, and a `&int` parameter: rejected, `E0477`.
75. A generator that captures an outer local and is returned: rejected, `E0479`; `new iterator(...)`: accepted.
76. `squares(10).filter(...).map(...)` and `for (v in squares(10))`: accepted.
77. A `yield` in a `match` arm's block and in both branches of an `if`: accepted.
78. A generator of a hooked class: one setter per `yield`, in addition to the consumer's own events.
79. A generator given to a task: rejected with the existing `E0602`, as `Sequence` is.

**Events**

80. A handler that subscribes another handler during an emission: the new one is first called by the next emission.
81. A handler that unsubscribes itself, or a later handler, during an emission: both are still called by this emission.
82. `unsubscribe()` twice: the second does nothing.
83. A handler that emits the same event: the nested emission completes inside the outer one, with its own registrations.
84. `events.subscribe(func(v: int) { total = total + v; })` with a capture: rejected by the storage milestone's code for a retained capture; with `new func`: accepted.
85. `emit` in a `#Synchronized` method or in a `Guarded.with` lambda: rejected, `E0550`.
86. `view().emit(1)`: rejected with the existing unknown-member code.
87. An emission with no handlers: does nothing.

## Order of work

Each step is one commit on `m8-integration` with the targeted tests named. The full gate runs once, at the end.

| Step | Content | Targeted tests | Changes images |
| --- | --- | --- | --- |
| 1 | This note | doc checks | no |
| 2 | `defer`: parser, `IrOp::Cleanup`, the NeuraIR minor, verifier, the converter's cleanup stack in place of `leave_task_scopes`, the IR optimizer rows, `E0219`, `E0471`, `E0472`, formatter and language server | parser and UI fixtures for cases 1 to 26; run programs for order, loops, early return and a held result; `.nir` round trip; the existing task-scope tests unchanged | only programs that use it |
| 3 | `using`: parser, `IrOp::Pending`, `close_outcome` and `release` over it, `E0220`, `E0473`, `E0474`; the escape check `E0475` on lowering and on the linked program | fixtures for cases 27 to 49; the close-failure matrix (body Ok or Err, close Ok or Err, each exit kind, two levels); the existing `Closeable` tests | only programs that use it |
| 4 | Generators: parser, `lower_generators`, `IrOp::Yield`, the converter's resume chain, `E0221`, `E0222`, `E0476` to `E0479`, the narrowed iteration contract | fixtures for cases 50 to 79; run programs for laziness, buffering, exhaustion, early break, nesting, re-entry and peak heap cells | only programs that use it |
| 5 | `Std.Events.Event`, `E0550`, its specification section, wiki page and tests | `tests/stdlib/events`, cases 80 to 87, and a trap-order test | only programs that import it |
| 6 | Tier and debugger checks: the deoptimization programs, source maps of cleanups and resume points, debugger display of generator state | tier-boundary, checked modes, a debug-adapter test that inspects a generator without advancing it | no |
| 7 | Language pages, diagnostics pages, examples, changelog, status | doc checks, doc examples | no |
| 8 | After the milestones it depends on are merged: `?` in acquisitions and as an exit (absence), the three hook rows (class hooks), region release in the cleanup stack and `new iterator` (storage) | the trace and integration programs of the gate | only programs that use them |

## Gate and acceptance

- Every image of the identity set, the extended set and the examples is byte for byte the image built before the milestone, with the same steps and peak cells. `tests/backend/programs/generic-closures.nra`, which names a function `defer`, is among them.
- Cleanup: early return, propagation, loop exits, nesting, a held return value, a failed acquisition, the close-failure matrix, a getter that would redirect the resource, each rejected escape, cancellation as an ordinary outcome, and a trap that runs nothing.
- Order: a program in which a deferred channel close lets a task scope's join finish; one in which a resource outside a task scope closes after the join; and the inverted arrangement rejected with `E0475`.
- Generators: a generator through a `filter` and `map` pipeline with a log of body steps; repeated `has_next`; `next` without it; early break; illegal suspension over a `defer`, a task scope and an array, and a `using` rejected in a generator body; re-entry; independent instances; peak heap cells stated per generator.
- Events: retention of a `new func` handler, snapshot delivery, nested emission, idempotent `unsubscribe`, a frame-borrowing handler rejected, a trap in a handler.
- With class hooks merged: the events of a deferred call at registration and at the exit, the exact object closed by `using`, and one admission per `yield`.
- Every program above gives the same output, steps and peak cells from source, from `.nir` and from a serialized image, optimized and not, in every execution mode of [the baseline](Release030.md#execution-modes), and with optional metadata stripped.
- The deoptimization programs of the tier table pass with `NEURA_TIER_CHECK=2` and `NEURA_TIER3_VERIFY=1`.
- The debugger inspects a suspended generator and a frame with pending defers and changes neither output nor steps.
- The performance gate of the baseline holds, and a sequence pipeline fed by a generator is added to the recorded set with its steps and cells.

## Migration

No existing source changes meaning: the four words stay identifiers wherever they are identifiers today. For the migration guide:

- A hand-written close after the work, or a call of `Closeable.release`, may become `using`. `File.with_open` and `TcpStream.with_connection` stay.
- A hand-written iterator class with `has_next` and `next` may become a generator when it holds no resource and no array.
- [Resources and concurrency](../lang/ResourcesConcurrency.md) says to assume no cleanup at the end of a scope. It changes to: none unless a `defer` or `using` is written, and none after a trap.
- The iteration contract's sentence about `has_next` is narrowed as above; no collection changes behavior.
- A statement that begins with a name `defer` followed by another name, or `using (name = ...`, did not compile before and now has a meaning; nothing in the repository is written so.
- `Std.Events` is new. No module is replaced by it.

## Decisions taken without the owner

| Decision | Reason |
| --- | --- |
| All four words are read by context, with fixed lookahead | The language already reads `scope`, `spawn`, `select`, `last` and `set` so, and `iterator` names a method in more than 100 files. |
| A deferred call must return `void`, not only "not `Result`" | Any dropped result hides something; the stricter rule needs no list of types. |
| `defer` belongs to its innermost block, so registration is known from the source and needs no run-time flag | An exit then runs exactly the defers written before it in the blocks it leaves. |
| `defer` and `using` are rejected at file level and in startup blocks | Those blocks are not function bodies and have no `return`; rejection is the stricter choice and keeps this milestone independent of startup blocks. |
| `using` binds one resource and takes no type annotation | Nesting already gives the order and the combination; a second form would need its own lookahead against a call with named arguments. |
| `using` is accepted only in a function returning `Result<T>` | `close` always returns `Result<Unit>`, and a close error needs a destination. |
| A failed close on `break`, `continue` or the end of the block returns the error from the function | Continuing would treat a failed close as a success. |
| A value that retains the resource is scoped with it, and unknown retention is an escape | It keeps readers over a resource usable without trusting an unproved summary. |
| The element type of a generator is required | It makes the form recognizable by lookahead and needs no inference. |
| A generator is an explicit state object with a resume index, built in lowering; NVR gains no suspended frame | Every tier, the collector, the decompiler and the debugger already handle objects and calls, and deoptimization needs nothing new. |
| A local lives in a generator's state when its scope contains a `yield`, whether or not it is live across one | The rule is read from the source and needs no liveness analysis to be trusted; the cost is stated in peak cells. |
| The exit work of blocks is one stack in the converter, driven by NeuraIR nodes; a task scope's region is entered before the scope | One mechanism gives defers, then the join, then the region, for every kind of exit. |
| A handler removed during an emission is still called by that emission | Delivery follows the registrations at the start of the emission, with no exception to explain. |
| The guard rule for `emit` is a compile-time check of the resolved call graph | The run time has no query for "a guard is held", and an honest partial check is better than none. |
| The event module is `Std.Events.Event`, in a new area | Events are not collections, I/O or concurrency primitives. |
| NeuraIR takes "the next free minor" instead of a number | Other milestones allocate versions at the same time; the format page records the number when the step lands. |

## Decisions taken in the implementation

| Decision | Reason |
| --- | --- |
| The IR optimizer leaves a body that contains a `Cleanup`, `Pending` or `Yield` entirely as lowering wrote it, and inlines no call into such a body or of its function. | It keeps every row of [the optimization table](#optimization-and-tiers) without an analysis of exits in each pass; it is stricter than the rows require and costs only the optimization of the bodies that use the features. |
| A deferred call does not save an operand that is a literal, a module, or a primitive reference. | Evaluating such an operand has no effect and gives the same value at the statement and at the exit; a primitive reference designates a place, which the call reads when it runs. |
| Edge case 23 is checked at file level and in a `static scope` startup block, which the class-hook milestone added (`tests/ui/lifetime/defer-static-scope.nra`). | |
| The formatter joins `defer` and its call when they are on two lines, and keeps a comment between them on its own line, as it keeps a comment inside any statement. | A comment cannot be joined onto the line of the call without moving it. |
| `close_outcome` is in an internal module, `std/io/internal/close-outcome.nra` (`Std.Io.Internal.CloseOutcome`), which a `using` loads, and not in `Std.Io.Closeable`; `release` keeps its own body instead of being rewritten over it. `tests/stdlib/io/closeable-tests.nra` checks that a `using` block and `release` give the same result for each outcome and close. | A new declaration in `Std.Io.Closeable` renumbers the closure names of every program that imports it (`examples/guide/errors.nra` does, through `Std.Fs`), and a rewritten `release` adds a call to every program that uses `File.with_open`; both change images of programs that use none of the features. |
| The storage milestone's lexical scopes became the exit stack's `Region` entries when the milestones merged, and `release_region` emits a block's release after its cleanups and its scope's join; `tests/lifetime/programs/region-*.nra` check that a deferred call still reads the block's array, also in a loop and around a returned array, and the disassembly of `region-scope.nra` shows `scope-exit` before `release-region`. The type table keeps its order for a program that uses none of the features, because leaving a scope interns `void` as before. | The two mechanisms had the same order, and one stack keeps it in one place. |
| A `using` in a function whose `Result` carries frame-owned storage, such as `Result<int[]>`, is `E0473` with its own message. | A return passes its value to `close_outcome`, which may replace it with the close error; returning an array from that call is the storage milestone's `E0451` inside the library, which names nothing the program wrote. A generic function whose `Result<T>` holds such storage only once it is specialized, `read<int[]>`, is checked again after specialization (`check_using_results`, before the storage check): `E0473` at the `using`, with a label at the function. |
| A `using` whose resource is nil at run time fails by reading its class hash in the branch where it is nil, which raises the existing nil-reference failure before anything is registered and has no other effect. | NeuraIR has no operation that only checks a reference, and the failure must be the existing one. |
| The escape check (`E0475`) runs once, on the program that lowering links, and reads the token `$using<N>` to find each block. A call through a function value that receives the resource, and a virtual call with no code among its targets (by the runtime's rule, `DispatchTargets`), are treated as unknown retention. A field, element, projection or unpacked value of something that holds the resource, and the result of calling a function value that captured it, hold it too unless their type holds no reference; the walk repeats to a fixed point before it reports, so an alias made late in a loop is seen. | Unknown retention is an escape by the rows above, and a part of a value that holds the resource may be the resource. |
| A bare generator expression is a `MakeClosure` whose text is `$iterator`, and `new iterator` is `$new_func`; the storage check reports a capturing `$iterator` that escapes as `E0479` instead of `E0453`. The generated function values that forward to the state's `has_next` and `next` are owned, since the sequence keeps them. | The storage milestone's closure rule already finds the escape; the text only names the construct. |
| A generator's parameters always move into `$local` fields, which the constructor writes, and its body reads and assigns them there; a generator without a `yield` has the same fields. | A parameter can be assigned in the body, and the factory call is the one place that admits the arguments. |
| A generator written in an expression takes no type parameters (`E0221`); a named generator does. | A lambda takes none, and an expression generator is lowered as one. |
| `has_next` and `next` are generated over `$resume` without calling each other; `next` advances when nothing is buffered and then reads the buffer. | It keeps `next` from dispatching through the interface to its own class. |
| The resume chain's comparisons carry LIFT hints, as a written `==` does. Without them tier 2 could not lift the chain's block in time, so native tier 3 left at it on every resume (40,000 times for 40,000 elements in `generator-deopt.nra`); with them it leaves native code twice in that whole run, at the host calls that print. `generator-resume-deopt.nra` deoptimizes inside `$resume` instead: its last value divides by zero, so the checked division leaves native code there and the slower tier reports the trap; `programs.py` checks, by the provenance name of the innermost frame, that the deoptimization is in `$resume` (its `.deopt-in` file). | The cause was the missing hint, not the jump into the loop body. |
| A generator's call writes out the body of `Sequence.from_iterator`: it keeps the state in a hidden local and returns `new Sequence<T>` over two function values that call the state's `has_next` and `next`, instead of calling `from_iterator`. | Native tier 3 verification (`NEURA_TIER3_VERIFY=1`) rejects, as V7, a `from_iterator` of a fresh iterator that tier 3 inlines two calls deep (the base of this milestone fails the same way on `from_iterator<int>(new Counter(n))` called through a function), and the shape written out passes it, with the same results. |
| `subscribe` retains its handler, so the storage milestone's rule applies to it: a handler that captures is `new func(...)`, and a bare one is `E0453` (`tests/ui/lifetime/event-handler-bare.nra`). The event's own unsubscribe callback is `new func`. | The rows above. |
| The class-hook rows are in `class_hooks.cpp`: a `$local<M>:<name>` field is a source binding; the generator's constructor, `has_next`, `next` and the closures that forward to them, and its allocation at the factory call, have no events; the store to `$buffer` is the result admission; the `Initialize` of `$using<N>` reads the binding without a getter; a store to `Pending` admits nothing. `tests/lifetime/programs/hook-*.nra` check edge cases 25, 47 and 78. | The rows above. |
| `E0550` runs once, on the program that lowering links, when it contains the event module. It finds `emit` and `Guarded.with` by their trusted files, starts from the functions that carry `synchronized` and from the `$invoke` of each lambda written as an argument of `Guarded.with`, and follows Call, VirtualCall (by the runtime's rule) and constructor calls. | The rows above. |
| A program built from a `.nir` module skipped lowering, so the escape check (`E0475`) and the guard check (`E0550`) run again when such a module is loaded for `build` or `emit-ir`, over the whole program it holds, after the verifier accepts it. | A compiler never writes a module that fails them, but a module from another compiler, or edited after it was written, would otherwise reach the converter unchecked; `tests/lifetime/nir_checks.py` retargets one call in a valid module to show it. |
| The decompiler's generator view comes from names alone: a layout named `$generator<N>` that has the fields `$index`, `$buffer`, `$buffered`, `$finished` and `$running` is labelled "inferred from provenance names, not from the circuit", with its locals that live across a `yield`. Without names, or with other fields, it is an ordinary layout. | The circuit has no mark for a generator, and the label must not claim more than the names show; `programs.py` checks the label with names and its absence without them. |
| In a generator's `$resume` frame, the debug adapter resolves a name that is not a slot of the frame through the `$local<M>:<name>` fields of `this`, before globals, so a breakpoint condition and an evaluation read a moved local by its source name. | The local's value lives in the state object; the read is a stored-field read and runs no getter. |
| A moved source local's field is named `$local<M>:<name>`, and the debug adapter shows the name after the colon; hidden locals keep `$temp<M>`, and the buffer, index and flags keep their names. | The debugger shows a generator's state by source names without a table of its own; the `$local<M>` prefix keeps the note's naming and makes the field unwritable from source. |
| A `defer` whose operands are all literals or modules saves nothing, so it has a source stop only where its call runs. A failed-close branch of a `using` gives a second stop at the block's closing brace. | Registration that evaluates nothing emits no operation to stop at. |
| `scripts/tier-boundary.py` generates boundary arithmetic and takes no programs; `tests/lifetime/programs.py` runs the deoptimization programs under `NEURA_TIER_CHECK=2` and `NEURA_TIER3_VERIFY=1` instead, and checks that they deoptimize. | The tier-boundary script has no place for the milestone's programs. |


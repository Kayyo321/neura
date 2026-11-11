# `Std.Concurrent.Guarded`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Guarded`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/guarded.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Guarded`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Guarded** · [`export let Guarded = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/guarded.nra#L6)
  A guard around one mutable T.
- **Guarded.Guarded** · [`export let init = func(state: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/guarded.nra#L8)
  A guard that owns the state. Use create, which builds the state inside the guard.
- **Guarded.Guarded** · [`export let with = func<U>(body: func(T) -> U) -> U`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/guarded.nra#L15)
  Runs `body` with exclusive access to the state and returns its result. A second task that calls with waits until the first returns. The body's captures and its result are transferable, so no reference to the state escapes. Calling with on a guard the task already holds traps.
- **Guarded** · [`export let create = func<T>(initializer: func() -> T) -> Guarded<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/guarded.nra#L26)
  A guard around the state that `initializer` builds. The initializer's captures are transferable, so the state it builds is reachable only through the guard.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Guarded<T>` is mutable state that tasks may share, one at a time ([Concurrency, section 8](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#8-shared-state)). The guard owns the state; a task reaches it only inside `with`, and nothing that refers to it can leave.

```neura
let visit = func(tally: guards.Guarded<Tally>) {
    tally.with<int>(func(state: Tally) -> int {
        state.count += 1;
        return state.count;
    });
};

let visits = func -> int {
    let tally = guards.create<Tally>(func -> Tally => new Tally());
    scope {
        spawn visit(tally);
        spawn visit(tally);
    }
    return tally.with<int>(func(state: Tally) -> int => state.count);
};
```

- `create<T>(initializer)` runs `initializer` in the calling task and wraps the state it builds. The initializer's captures must be transferable, so the state is reachable only through the guard.
- `with<U>(body)` runs `body` with exclusive access to the state and returns its result. The body's captures and `U` must be transferable, so no reference to the state can escape. A value that is not is reported as `a value of type ... is not transferable, so it cannot be used where a transferable value is required`.
- A second task that calls `with` parks until the first returns, first come, first served. That wait is not cancellable, because `with` has no outcome for it.
- Calling `with` on a guard the task already holds is the program trap `invalid state: guard already held`, status 134.
- One `with` call is ordered before a later one on the same guard ([memory model](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#54-memory-model)).
- The constructor takes the state itself and is exported so that `create` can build the guard. Programs use `create`, which builds the state inside the guard.

### Limits, transfer, and task safety

- A `Guarded<T>` is a transferable handle and costs 8 heap cells.
- `create` and `with` are task-safe. They are scheduler operations, so the worker of a parallel map cannot use a guard.
- Start with state owned by one task and reached over a channel; a guard is for state that several tasks must update in place. Concurrent collections do not exist yet; one is built on a guard or as an owner task.
- A program that uses this module cannot use `Std.DynamicForeign` or a C import ([decision C15](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#3-decisions)).

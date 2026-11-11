# `Std.Concurrent.Task`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Task`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/task.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Task`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Task** · [`export let Task = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/task.nra#L8)
  A call running as a task, or its result once the call has returned. Tagged `#Final`.
- **Task.Task** · [`export let join = func -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/task.nra#L17)
  Waits until the task has finished and returns its result. Joining again returns the same value. A join is never cancelled.
- **Task.Task** · [`export let is_finished = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/task.nra#L20)
  Whether the task has finished. Never waits.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Task<T>` is what `spawn` returns: a call running as a task, or its result once the call has returned ([Concurrency, section 4.1](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#41-scope-spawn-and-join)). A program does not construct one.

```neura
let sum_of_squares = func(a: int, b: int) -> int {
    scope {
        let first = spawn square(a);
        let second = spawn square(b);
        return first.join() + second.join();
    }
};
```

- `spawn call` evaluates the callee and the arguments in the parent, in the ordinary order, and returns a `Task<T>`, where `T` is the call's result type. A call of a `void` function gives `Task<Unit>`.
- `join()` waits until the task has finished and returns its result. Joining again returns the same value. A join is never cancelled: it always waits for the child's actual result, so a scope that cancelled its tasks can still drain them.
- The result crosses the join whatever its type. Spawn arguments must be transferable; a result need not be, because nothing else can reach what the finished task built ([section 5.3](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#53-results-cross-a-join-by-transfer)). A `Result<T>` or a list is returned as it is.
- `is_finished()` reports whether the task has finished and never waits.
- A scope may exit without joining a task. The exit still waits for the task, and its result is discarded.
- A trap in any task ends the run at once ([Errors](Errors#tasks)).

### Scope-bound handles

A `Task<T>` must not be reachable after its scope exits ([section 4.3](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#43-scope-bound-values)). It may be held in a local declared inside the scope, including a local list for a fan-out of unknown width, and passed as an argument to a call. It may not be returned from a function, stored in a field, a global, or an enum payload, captured by a lambda, passed to `spawn`, sent on a channel, used as a type argument, or assigned to a binding declared outside the scope. The compiler reports each of these at the escaping expression, in the form `a value of type Task<int32> cannot leave its scope: it cannot be returned from a function`.

### Select

`task.join()` can be an arm of a `select` statement, which commits when the task has finished. A `join` arm is never cancelled. `select_handle()` and `join_selected(code)` are what the `select` statement calls; a program does not call them.

### Limits and task safety

- Live tasks are bounded by `--max-tasks`, 65,536 by default; exceeding it traps with status 137 at the `spawn`. A task record costs 20 heap cells until the scope that owns it closes; a task spawned as a statement, with no handle, is released when it finishes ([section 10.3](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#103-limits)).
- `join` and `is_finished` are task-safe: a spawned task may open scopes and join its own tasks. They are scheduler operations, so the worker of a parallel map cannot use them.
- A program that uses this module cannot use `Std.DynamicForeign` or a C import ([Concurrency, section 16](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#16-limits)).

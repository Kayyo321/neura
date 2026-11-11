# `Std.Concurrent.Tasks`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Tasks`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/tasks.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Tasks`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Tasks** · [`export let all = func<T>(calls: (func() -> T)[list]) -> T[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/tasks.nra#L20)
  Runs every call as a task and returns their results in the order of the calls. Waits for all of them; nothing is cancelled.
- **Tasks** · [`export let all_ok = func<T>(calls: (func() -> result.Result<T>)[list]) -> result.Result<T[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/tasks.nra#L38)
  Runs every call as a task. If all return Ok, returns their values in the order of the calls. Otherwise returns the first Err in the order the calls completed, after cancelling the calls still running and waiting for every one of them to finish. If the calling task is cancelled while it waits, the calls are cancelled the same way and the result is an Err with the code tasks.cancelled.
- **Tasks** · [`export let first_ok = func<T>(calls: (func() -> result.Result<T>)[list]) -> result.Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/tasks.nra#L83)
  Runs every call as a task and returns the first Ok in the order the calls completed, after cancelling the calls still running and waiting for every one of them to finish. If every call fails, returns the Err of the one that completed last. With no calls, or if the calling task is cancelled while it waits, the result is an Err with the code tasks.empty or tasks.cancelled.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Tasks` holds helpers that run a group of calls as tasks and combine their results ([Concurrency, section 4.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#42-errors-and-traps)). A call is a function value that takes no arguments. Each helper opens its own scope, so it has cancelled and drained every call it started before it returns.

```neura
let quote = func -> result.Result<int[list]> {
    let calls: (func() -> result.Result<int>)[list] = [
        func -> result.Result<int> => fetch_price("tea"),
        func -> result.Result<int> => fetch_price("cake")
    ];
    return tasks.all_ok<int>(calls);
};
```

| Helper | Result |
| --- | --- |
| `all<T>(calls: (func() -> T)[list]) -> T[list]` | Runs every call as a task and returns their results in the order of the calls. It waits for all of them and cancels nothing. |
| `all_ok<T>(calls: (func() -> Result<T>)[list]) -> Result<T[list]>` | If every call returns `Ok`, the values in the order of the calls. Otherwise the first `Err` in the order the calls completed, after cancelling the calls still running and waiting for every one of them. |
| `first_ok<T>(calls: (func() -> Result<T>)[list]) -> Result<T>` | The first `Ok` in the order the calls completed, after cancelling the calls still running and waiting for every one of them. If every call fails, the `Err` of the call that completed last. |

- **Lists, not arrays.** The helpers take and return lists. The language cannot build an array whose length is known only at run time, and a function with type parameters cannot be overloaded for arrays as well. A list literal writes the calls where the parameter's type is expected, and a loop builds them for a width known only at run time.
- **No calls.** `all` returns an empty list, `all_ok` returns `Ok` of an empty list, and `first_ok` returns an `Err` whose code is `tasks.empty`.
- **The caller is cancelled.** If the task that called `all_ok` or `first_ok` is cancelled while the helper waits, the helper cancels its calls and waits for them. `all_ok` then returns an `Err` whose code is `tasks.cancelled`. `first_ok` returns the `Err` of the last call that had failed, or an `Err` with the code `tasks.cancelled` when none had.
- **Cancelled calls.** A call learns that it was cancelled through [`Cancellation.requested()`](Stdlib-Concurrent-Cancellation) or through a parked operation that returns `Cancelled`. A call that never asks runs to its end, and the helper waits for it.
- **Errors.** The two codes are `Custom` errors made with `Std.Core.Error.custom`. Every other `Err` is one a call returned.

### Task safety

A call must be task-safe and its captures transferable, as for any spawn. The compiler checks the spawn inside the helper and reports a violation at the program's call of the helper, with the place where the library starts the task: `... (the library starts the task at tasks.nra:<line>:<column>)`. A call that captures an object is reported as `a value of type func() -> Result<int32> is not transferable, so it cannot be given to a spawned task: a function value of this type captures a value of type Box at ...` ([section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)).

The helpers themselves are task-safe, so a task may call them. They use scheduler operations, so the worker of a parallel map cannot. A program that uses this module cannot use `Std.DynamicForeign` or a C import ([Concurrency, section 16](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#16-limits)).

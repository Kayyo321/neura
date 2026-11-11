# `Std.Concurrent.Parallel`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Parallel`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Parallel`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Parallel** · [`export let ItemLimits = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra#L8)
  The limits each item of a map runs under, the same under any number of workers. An item that exceeds one traps as that item.
- **Parallel.ItemLimits** · [`export let max_steps: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra#L10)
  The most steps one item may execute.
- **Parallel.ItemLimits** · [`export let max_heap_cells: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra#L13)
  The most heap cells one item may hold at once, the copies of the program's constants it can read included.
- **Parallel.ItemLimits** · [`export let init = func(steps: int64, heap_cells: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra#L16)
  Limits of the given steps and heap cells, both positive.
- **Parallel** · [`export let limits = func(steps: int64, heap_cells: int64) -> ItemLimits`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra#L29)
  Limits of the given steps and heap cells for each item.
- **Parallel** · [`export let map = func<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/parallel.nra#L35)
  Returns what `worker` returns for each item, in input order. T and U are transferable. The worker is task-safe, issues no host request, uses no task, channel, or other scheduler operation, and uses no object identity; the compiler checks this. Each item runs alone under `limits`. If items trap, the run reports the trap of the lowest index.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Parallel` is multicore computation over isolated work ([Concurrency, section 12](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#12-multicore-parallelmap)). Tasks do not use more than one core; `map` does.

```neura
let squares = func(items: int[]) -> int[] =>
    parallel.map<int, int>(items, square, parallel.limits(100000, 10000));
```

- **Meaning.** `map<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]` returns what calling `worker` on each item in index order would return. More workers may change only wall time and peak process memory.
- **Isolation.** `worker` must be task-safe, issue no host request, use no scheduler operation (it cannot spawn, open a scope, or use a channel, timer, or guard), and perform no identity operation (class hash or class text). The compiler checks this and reports a violation at the program's call of `map`: `the worker of a parallel map is not task-safe: ...` or `the worker of a parallel map must compute in isolation: ...`, each with the call chain. `T` and `U` are transferable.
- **Order.** `map` takes and returns arrays, and results are in input order. If several items trap, the run reports the trap of the lowest index and names it: `(item 2 of a parallel map)`, counting from zero.
- **Limits.** Each item runs under `ItemLimits`, made with `limits(steps, heap_cells)`: the most steps one item may execute and the most heap cells it may hold at once, the copies of the program's constants it can read included. Both are positive; zero or a negative value traps with `invalid argument: steps` or `invalid argument: heap_cells`. An item that exceeds one traps as that item, with status 152 for steps and 137 for heap cells. The run's totals add the steps of items in index order, so the run's own `--max-steps` applies to the sum.
- **Execution.** Each item runs to its end in a state of its own: its own heap, with copies of the program's globals whose type is transferable, and no scheduler. With `--parallel-workers N`, from 1 to 1,024, N threads evaluate items at once; the default is one, on the scheduler's thread. The values, the reported trap, and the statistics do not depend on N.

### Task safety

`map` is task-safe, so a spawned task may call it. `neura check --task-safety` marks which task-safe functions could be the worker: a function that is not is listed as `task-safe; not for a parallel map: <reason>`. A native executable records the worker count in its policy ([Native executables](https://github.com/Kayyo321/neura/blob/main/docs/lang/Standalone.md#5-the-container)). A program that uses this module cannot use `Std.DynamicForeign` or a C import ([decision C15](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#3-decisions)).

# `Std.Concurrent.AtomicInt64`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.AtomicInt64`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.AtomicInt64`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **AtomicInt64** · [`export let AtomicInt64 = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L8)
  A shared signed 64-bit integer. Tagged `#Final`, `#ThreadSafe`.
- **AtomicInt64.AtomicInt64** · [`export let init = func(initial: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L12)
  An atomic holding the initial value.
- **AtomicInt64.AtomicInt64** · [`export let load = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L17)
  The current value.
- **AtomicInt64.AtomicInt64** · [`export let store = func(value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L20)
  Replaces the value.
- **AtomicInt64.AtomicInt64** · [`export let add = func(delta: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L26)
  Adds to the value and returns the sum. The addition is checked like any int64 addition: it traps on overflow and leaves the value unchanged.
- **AtomicInt64.AtomicInt64** · [`export let compare_exchange = func(expected: int64, desired: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L33)
  Stores `desired` if the value equals `expected`, and reports whether it did. Tagged `#NoDiscard`.
- **AtomicInt64** · [`export let create = func(initial: int64) -> AtomicInt64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/atomic-int64.nra#L44)
  An atomic holding the initial value. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`AtomicInt64` is a signed 64-bit integer that tasks may share ([Concurrency, section 8](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#8-shared-state)).

```neura
let count_one = func(hits: atomics.AtomicInt64) {
    hits.add(1);
};

let count_twice = func -> int64 {
    let hits = atomics.create(0);
    scope {
        spawn count_one(hits);
        spawn count_one(hits);
    }
    return hits.load();
};
```

- `create(initial)` returns an atomic holding the initial value.
- `load()` returns the current value, and `store(value)` replaces it.
- `add(delta)` adds to the value and returns the sum. The addition is checked like any `int64` addition: it traps on overflow and leaves the value unchanged.
- `compare_exchange(expected, desired)` stores `desired` if the value equals `expected`, and reports whether it did.
- **Ordering.** The operations are sequentially consistent: all operations on one atomic form a single order consistent with each task's program order, and each sees the latest earlier store ([memory model](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#54-memory-model)).

### Transfer and task safety

An `AtomicInt64` is a transferable handle: it can be given to a spawned task or sent on a channel. Every member is task-safe and never parks. Start with state owned by one task; an atomic is for a counter or a flag that several tasks update.

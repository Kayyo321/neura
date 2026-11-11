# `Std.Core.Cell`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Cell`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/cell.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Cell`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Cell** · [`export let Cell = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/cell.nra#L8)
  A mutable slot shared by holders of the same Cell reference. Tagged `#Final`. Since 0.1.0.
- **Cell.Cell** · [`export let value: T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/cell.nra#L10)
  The value currently stored in the shared slot.
- **Cell.Cell** · [`export let init = func(initial: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/cell.nra#L13)
  Initializes the slot with the supplied value.
- **Cell** · [`export let of = func<T>(value: T) -> Cell<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/cell.nra#L19)
  Creates a new, independent cell containing the value.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.6 Callbacks

- A callback parameter is written as a native function type. The library declares no named function shapes: a type overlay can be neither generic nor a function type, and the native spelling says everything a name would.
- **Concrete callbacks.** `Logger` takes a sink of type `(string) → void`, and any module can use function values whose types name no type parameter.
- **Generic callbacks specialize with their caller.** A member of `ArrayList<T>` can take `(T) → bool`. The G1 label in API tables identifies members that depend on this compiler facility.
- **Capture is by value.** A closure copies the locals it uses when it is created. A closure cannot count or accumulate in a captured local. It captures an object and mutates that. `Std.Core` provides `Cell<T>` ([section 6.5](Stdlib-Design#65-small-types)) for the purpose: a callback that sums captures a `Cell<int64>`.
- **Capture of a reference is shared.** A closure that captures a collection sees later changes to it, and changes it makes are visible outside. A callback passed to a collection's own member must not structurally modify that collection; the member compares the modification count after each call and traps on a change.
- Unless a member's description says otherwise, a callback may be called any number of times, in the order described.

### 6.5 Small types

| Type | Definition | Available |
| --- | --- | --- |
| `Unit` | A fieldless enum with the single variant `Value`. | Yes |
| `Cell<T>` | A class with one mutable field `value`, and the module function `of<T>(value)`. It exists so that closures, which capture by value, can share mutable state. | Yes |
| `IntRange` | An immutable class with the get-only fields `start` (inclusive), `end` (exclusive), and `step` (nonzero). Module functions `of(start, end)` and `stepped(start, end, step)`; a zero step traps. A negative step counts down while the value is greater than `end`. Members `size()`, `is_empty()`, `contains(value)`, `get(index)`, `to_list()`, and `iterator()`; it implements `Iterable<int>`, so `for (let i in range)` visits the values in order. `size` and iteration are computed in `int64` so that no intermediate overflows. | Yes |

`Pair`, `Entry`, and `Boxed` do not exist: a pair is the native tuple `[A, B]` (`partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`), and a map entry is the tuple `[K, V]`. No API on this page takes an `Object`.

Named types are kept where the name carries a contract a tuple cannot: `RegexMatch` (group lookup by name), `FileMetadata` (three fields of unrelated types that will grow), and the value classes of `Std.Time`.

### 9.12 `Sequence<T>`

A class for lazy pipelines, the counterpart of Java's streams. Needs G1; the members that return `Optional` also need G2.

A sequence is two native function values, and each operation is a closure over the previous two.

- **Layout.** `has_next: () → bool`, `next: () → T`, and a `bool` `consumed`.
- **Why closures fit.** An adapter's state is exactly what a closure captures. `map` captures the upstream pair and the mapping function and needs no state of its own. Operations that do keep state (`take`'s remaining count, `filter`'s buffered element, `enumerate`'s index) hold it in a captured `Cell`, because captured locals are copies ([section 5.6](Stdlib-Design#56-callbacks)).
- **Single use.** Every operation marks the sequence consumed; a second operation on the same sequence traps with `invalid state: sequence already used`.
- **Evaluation.** Pull-based and sequential. Nothing runs until a terminal operation, and each element passes through the whole pipeline before the next is requested.
- **Sources.** Module functions `from_list<T>(items: T[list])`, `empty<T>()`, `generate<T>(make: () → T)` (infinite), `iterate<T>(seed, step: (T) → T)` (infinite), and `range(start, end)` for `int`. Each collection's `sequence()` member closes over one of its iterator objects, so the pipeline is fail-fast.

| Intermediate | Behavior |
| --- | --- |
| `map<U>(f)`, `filter(keep)`, `flat_map<U>(f: (T) → U[list])` | |
| `take(n)`, `skip(n)`, `take_while(test)`, `skip_while(test)` | A negative `n` traps. `take` stops pulling after `n` elements. |
| `enumerate() → Sequence<[int, T]>` | Each element with its position from 0. |
| `zip<U>(other: Sequence<U>) → Sequence<[T, U]>` | Ends with the shorter input. |
| `chain(other: Sequence<T>)`, `peek(action)` | |
| `chunked(n)`, `windowed(n)` | Sequences of built-in lists, each list new. `n` below 1 traps. |
| `sorted(c)`, `reversed()` | Buffer the whole input when first pulled. `sorted` is stable. |
| `distinct()` | Keeps the first of each group of equal elements. Needs G3. |

| Terminal | Behavior |
| --- | --- |
| `to_list()`, `to_array_list()` | A built-in list; an `ArrayList<T>`. |
| `partition(test) → [ArrayList<T>, ArrayList<T>]` | Matching elements first. |
| `fold<A>(initial: A, f: (A, T) → A) → A` | Left to right. |
| `count() → int`, `for_each(action)` | `count` traps (reason 1) beyond the `int` range. |
| `any(test)`, `all(test)`, `none(test)` `→ bool` | Stop at the first deciding element. |
| `reduce(f) → Optional<T>`, `first()`, `last()`, `find(test)`, `min(c)`, `max(c)` | G2. `min` and `max` return the first of several equal extremes. |
| `to_set()`, `to_map<K, V>(key, value)`, `group_by<K>(key)` | G3, for the default key operations. A duplicate key in `to_map` traps. `group_by` returns a `HashMap<K, ArrayList<T>>` in first-seen key order. |

The module also has functions for sequences of one element type, which a member of a generic class cannot express: `join(s: Sequence<string>, separator: string) → string`, `sum_int32`, `sum_int64` (checked; overflow traps), and `sum_float64` (left-to-right addition).

### 14.1 `Std.Log`

`Logger`: its sink is a concrete function value and its text is built by concatenation.

- `LogLevel` is a fieldless enum, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`. Levels compare by ordinal.
- **The sink is a function value** of type `(string) → void`, called once per accepted record with the finished line, without a line terminator. `Console.print_line` is a sink. A closure that appends to a captured list is a sink for tests; because a captured list is shared, the test reads the list afterwards. A sink that can fail handles its own failure, typically by counting it in a captured `Cell`.
- Module functions `create(name: string, sink, level: LogLevel)` and `create_timed(name, sink, level, clock: Clock)`.
- Each record is one line: with a clock, the instant's text and a space; then the level in uppercase, a space, the name, `: `, and the message. If the clock fails, the timestamp is `-`.
- Without a clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, make: () → string)` (the function runs only when the level is enabled), `is_enabled(level)`, and the field `level`, which a program assigns to change the logger's level.
- There is no global logger registry. A logger is passed to the code that uses it.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` where a collection's own text reaches it again, directly or through other collections. Each collection keeps the flag for this in the instance, so text conversion reads no module state and is task-safe ([Concurrency](Concurrency#52-task-safe-calls)).
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the searched value as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.

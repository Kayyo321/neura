# `Std.Collections.Iterable`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.Iterable`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/iterable.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.Iterable`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Iterable** · [`export let Iterable = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/iterable.nra#L8)
  A value that provides an iterator for the for-each protocol.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.4 Iteration

The loop form depends on the source value.

| Way | Available | Notes |
| --- | --- | --- |
| For-each over the built-in list from `ArrayList.items()`, or from any collection's `to_list()` | Yes | The built-in loop compares the list's structural version before each advance and traps with reason 5 on a change. `items()` is live; `to_list()` is a copy. |
| An index loop over `size()` and `get(index)` | Yes | For lists and deques. No modification check. |
| The collection's iterator object: `iterator()` returns a class with `has_next() → bool` and `next() → T` | Yes, as one concrete iterator class per collection | `has_next` has no effect and can be repeated. `next` returns the next element and advances, and traps with `no such element` at the end. An iterator is used once. |
| For-each directly over the collection | Yes | Lowers to the iterator protocol when the source implements `Iterable<T>`. Every public collection, `Sequence`, and `IntRange` implement it; `tests/stdlib/for-each` loops over each one. |

- **Iteration does not allocate per element.** `has_next` and `next` stay two calls. A single `next` returning `Optional<T>` would read well with `match`, but it would allocate an enum value for every element.
- **Map iteration yields tuples.** A map's iterator returns `[K, V]`. The tuple is an immutable snapshot of the entry, which is an immutable snapshot of one association.
- **No removal through iterators.** Collections remove in bulk with `remove_if` and `retain_if`.
- **Fail-fast iterator objects.** A wrapper over a single built-in list uses that list's `uint64` structural version. Other mutable collections use a `uint64` modification count and increment it after each structural change. An iterator records its version when created. `has_next` and `next` compare first and trap on a difference. Replacing an element by index or replacing the value for an existing key is not structural.
- The iteration order of every collection is specified in section 9.
- `Iterator<T>` and `Iterable<T>` are the interfaces for this protocol. Concrete iterator classes and collections implement these member signatures.

### 5.7 Conformance summary

These are the intended source-module contracts. Compiler support for the operations does not imply that a collection module or adapter is already packaged.

| Type | Equality and hash | Natural order | Iterable | Interfaces (G4) |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, fieldless enums, `string`, tuples | Built in (G3a) | Companion `compare` functions | No (`Strings.scalars` gives a list) | |
| Array, list | Built in, structural (G3a) | No | Built-in for-each | |
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Module functions; payload enums have no built-in equality | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `RegexFlags`, `RegexMatch`, `Regex` | By value | No | No | |
| `StringBuilder`, `ByteBuffer` | Current byte sequence | No | No | |
| `Sequence`, `Random`, `Logger`, stream adapters, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](Stdlib-Design#111-stdio) |

### 9.3 `ArrayList<T>`

`ArrayList` is a thin class over one built-in list. Each built-in list operation is a single NVM storage operation; a library-managed backing array would spend graph evaluations on capacity checks for every append and would need runtime-length arrays, which do not exist.

- **Layout.** One built-in list `storage`. Its `version` is the modification counter.
- **Module functions.** `create<T>()`, `of<T>(items: T[list])` (copies), `filled<T>(count, value)`, and `wrap<T>(items: T[list])` (adopts the list without copying).
- **`items() → T[list]`** returns the live built-in list. A built-in for-each over it traps with reason 5 on structural modification. For-each over the wrapper calls its iterator through `Iterable<T>`.
- **Aliasing.** `wrap` and `items` share the same backing list. The wrapper preserves the identity of that list for its lifetime. Its iterators compare the backing version, so modifications through a direct alias are detected as well.

| Operation | Cost | How |
| --- | --- | --- |
| `get`, `set`, `size`, `add`, `remove_last` | One storage operation | Built-in index, length, `append`, and `remove_at` of the last index. |
| `remove_at` | One storage operation | Built-in `remove_at`. |
| `add_at` | One operation per element after the index | At the end, append the new value directly. Otherwise append the last element again, shift the suffix right by one with indexed writes, and store the new value.  |
| `truncate`, `clear` | One operation per removed element | Repeatedly remove the last index from the same backing list.  |
| `remove_if` | Linear | Check the version after each callback, compact the kept elements by indexed writes, then remove the tail from the end. The backing version advances on each actual removal. |
| `reverse`, `fill`, `slice`, `to_list` | Linear | Element reads and writes. |

No removal leaves a stale reference: the built-in list drops the slot, and the removed element is reclaimed.

### 9.10 Interfaces and read-only views

All of this needs G4. The interfaces are declared in `Std.Collections` and the concrete classes are marked as implementing them; no member changes.

```text
Iterable<T>
└── Collection<T>            size, is_empty, contains, to_list, iterator
    ├── MutableCollection<T> add, remove, clear, ...
    ├── List<T> ──────────── MutableList<T>
    ├── Set<T> ───────────── MutableSet<T>
    │   └── SortedSet<T>
    └── Queue<T> ─────────── Deque<T>

Map<K, V> ── MutableMap<K, V>
└── SortedMap<K, V>
```

- Read-only and mutable interfaces are separate. A function that only reads takes the read-only interface, and no member simulates immutability by trapping.
- Members that need only other members (`is_empty`, `first`, `contains_all`, `for_each`) become default methods written once in the interface.
- `ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are classes that copy their contents at construction and implement only the read-only interfaces. Their module functions `of` take a built-in list (for the map, a list of `[K, V]`; a duplicate key traps).
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.

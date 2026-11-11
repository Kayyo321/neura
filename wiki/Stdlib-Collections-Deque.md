# `Std.Collections.Deque`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.Deque`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/deque.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.Deque`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Deque** · [`export let Deque = interface<T>:Queue<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/deque.nra#L8)
  A queue with insertion, inspection, and removal at either end.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

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
- Members that an interface declares, such as `size()` and `is_empty()`, stay methods in every class, because an interface cannot require an [accessor](Types#accessors); `ArrayList.items()` stays a method with them. The immutable classes and `Sequence` are `#Final` and `#NoDiscard`, and every query that returns an `Optional` (`get`, `first_or_none`, `peek`, `floor`, and the like) is `#NoDiscard` ([Tags](Tags)).
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.

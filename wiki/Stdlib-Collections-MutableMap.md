# `Std.Collections.MutableMap`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Collections.MutableMap`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/mutable-map.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Collections.MutableMap`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **MutableMap** · [`export let MutableMap = interface<K, V>:map.Map<K, V>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/collections/mutable-map.nra#L9)
  An association whose entries can be inserted, replaced, or removed. Replacing the value of an existing key preserves its position and iterator validity.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

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

- **Decision kept:** read-only and mutable interfaces are separate. A function that only reads takes the read-only interface, and no member simulates immutability by trapping.
- Members that need only other members (`is_empty`, `first`, `contains_all`, `for_each`) become default methods written once in the interface.
- `ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are classes that copy their contents at construction and implement only the read-only interfaces. Their module functions `of` take a built-in list (for the map, a list of `[K, V]`; a duplicate key traps).
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.

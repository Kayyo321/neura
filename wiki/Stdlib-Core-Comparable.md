# `Std.Core.Comparable`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Comparable`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/comparable.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Comparable`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Comparable** · [`export let Comparable = interface<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/comparable.nra#L8)
  Defines the natural ordering between a receiver and another value of type T.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`. All of this works today.
- **A comparator is a native function value** of type `(T, T) → Ordering`. There is no comparator interface or wrapper class. A named function, a bound method, or a closure all serve. A comparator must define a total order; if it does not, the library stays memory safe and terminates, and the resulting order or lookup is unspecified.
- **Natural orders** are named functions in the companion modules, usable as comparators directly: `Int32.compare`, `Float64.total_compare`, `Strings.compare`, and so on.

| Type | Natural order |
| --- | --- |
| Integers | Numeric value |
| `bool` | `false` before `true` |
| Floats | Negative infinity, negative finite values, `-0.0`, `0.0`, positive finite values, positive infinity, then NaN; all NaNs equal |
| `string` | Byte-wise lexicographic; a proper prefix sorts first. This equals ordering by Unicode scalar value. |
| Fieldless enum | Ordinal, obtained by the caller with `as` |

- The module **`Comparators`** uses generic function values (G1) for `reversed<T>(c)`, `by_key<T, K>(key: (T) → K, c: (K, K) → Ordering)`, and `then<T>(first, second)`.
- **`Comparable<T>`**, an interface with `compare_to(other: T) → Ordering`, arrives with G4. Nothing in the library requires it: sorted structures always take a comparator.

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

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.

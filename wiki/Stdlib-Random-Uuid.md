# `Std.Random.Uuid`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Random.Uuid`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Random.Uuid`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Uuid** · [`export let Uuid = class implements Comparable<Uuid>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L13)
  An immutable 128-bit identifier ordered by its unsigned big-endian bytes.
- **Uuid.Uuid** · [`export let init = func(first: uint64, last: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L20)
  Constructs an identifier from its big-endian halves.
- **Uuid.Uuid** · [`export let to_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L26)
  The 16 bytes in order, as a new list.
- **Uuid.Uuid** · [`export let compare_to = func(other: Uuid) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L34)
  Lexicographic over the bytes, each unsigned.
- **Uuid.Uuid** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L45)
  Tests whether another UUID has the same two halves.
- **Uuid.Uuid** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L54)
  Starts from 17 and combines each byte in order.
- **Uuid.Uuid** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L63)
  The 36-character hyphenated form in lowercase.
- **Uuid** · [`export let random = func(source: Random) -> Uuid`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L82)
  A version 4 identifier from 16 bytes of random.fill_bytes, with the version bits set to 0100 and the variant bits to 10.
- **Uuid** · [`export let parse = func(text: string) -> Result<Uuid>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/random/uuid.nra#L93)
  The 36-character hyphenated form, hex digits in either case. Errors: Empty; InvalidCharacter at the first byte that is not a hex digit or a hyphen in its place, or at offset 36 when text follows; UnexpectedEnd at the length for shorter text.
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

### 12.2 `Std.Random`

**`Random`** implements the xoshiro256** generator using shifts, rotations, exclusive ors, and multiplication by 5 and 9. Use the `uint64` recipes in [section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics). `next_gaussian` depends on the library's `Math` module, and `from_entropy` requires a registered entropy provider.

- **State.** Four `uint64` words `s0` through `s3`.
- **Step.** The output is `rotate_left(s1 × 5, 7) × 9`, wrapping. Then, with `t` = `s1` shifted left by 17: `s2 ^= s0`; `s3 ^= s1`; `s1 ^= s2`; `s0 ^= s3`; `s2 ^= t`; `s3 = rotate_left(s3, 45)`.
- **Seeding.** The module function `seeded(seed: uint64)` fills the four words with successive outputs of SplitMix64 started at the seed: add 0x9E3779B97F4A7C15 to the state; take `z` as the new state; `z = (z ^ (z >> 30)) × 0xBF58476D1CE4E5B9`; `z = (z ^ (z >> 27)) × 0x94D049BB133111EB`; output `z ^ (z >> 31)`; all wrapping. The source writes these constants in decimal (A5). `from_entropy() → Result<Random>` requests 8 bytes from `neura.entropy.fill`, reads them as a little-endian `uint64`, and seeds with that.

| Member | Behavior |
| --- | --- |
| `next_uint64() → uint64` | One step. |
| `next_uint32() → uint32` | The high 32 bits of one step. |
| `next_bool() → bool` | The top bit of one step. |
| `next_int_below(bound: int) → int` | Uniform in 0 up to but excluding `bound`. Take the smallest all-ones mask covering `bound - 1`; draw `next_uint32` masked until the result is below `bound`. A bound below 1 traps. |
| `next_int_between(low: int, high: int) → int` | Uniform from `low` up to but excluding `high`. The span is computed in `int64`, sampled with the same mask-and-reject loop on `uint32`, and added to `low` in `int64`. `low` not below `high` traps. |
| `next_float64() → float64` | The top 53 bits of one step, converted to `float64` (exact) and multiplied by 2^-53: uniform in 0 up to but excluding 1. |
| `next_gaussian() → float64` | Marsaglia's polar method. Each accepted pair yields two values; the second is kept in the generator and returned by the next call. |
| `fill_bytes(count: int)` | A new built-in `uint8` list. Each step supplies 8 bytes, least significant first; a final partial group uses the low bytes. |
| `shuffle<T>(items: T[list])` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. Generic, with no callback. |
| `choose<T>(items: T[list]) → T` | Traps with `no such element` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets, and the library offers no cryptographic generator.

**`Uuid`** is an immutable value of 16 bytes. The module function `random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `to_string` writes the 36-character hyphenated form in lowercase. `parse(text) → Result<Uuid>` accepts either case and needs T1. It has `compare_to`, `equals`, and `hash_code` over its bytes.

### 18.5 Time, randomness, and logging

- **Duration text.** `parse` accepts an optional sign, `P`, an optional day count, and `T` with hours, minutes, and seconds (seconds with up to nine fraction digits); days are read but never written.
- **Instant** normalizes the nanoseconds of `of_epoch_seconds` by floor division and writes every `int64` second count; `parse` accepts years -999,999 through 999,999.
- **ZoneOffset** holds whole minutes; hours and minutes share a sign, and `compare_to` orders by total seconds.
- **Clocks.** `Clock.sleep` returns at once for a zero or negative duration. `FixedClock.of(instant)` makes a fixed clock whose monotonic reading is the total of its advances and sleeps; `advance` traps for a negative duration. `Stopwatch.create(clock)`; `start` and `stop` return `Result<Unit>` and trap when repeated.
- **Calendar arithmetic** splits off 400-year cycles with one 64-bit division and works within a cycle in 32-bit arithmetic, which keeps images that format dates several megabytes smaller.
- **Random.** `seeded` and `from_entropy` are the only constructors; `from_entropy` fails with `InvalidData` when the host returns other than 8 bytes. **Uuid** stores two big-endian `uint64` halves and has `to_bytes`.
- **Trap details.** `invalid date`, `date out of range`, `invalid time`, `invalid zone offset`, `day of week`, `negative duration`, `bound must be positive`, `low must be below high`, and `negative count` after `invalid argument: `; `stopwatch is running` and `stopwatch is stopped` after `invalid state: `.
- **Logger.** `is_enabled(Off)` is false.

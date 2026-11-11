# `Std.Core.Int32`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Int32`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Int32`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Int32** · [`export const Min: int32 = -2147483648`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L17)
  The smallest value representable by this integer type.
- **Int32** · [`export const Max: int32 = 2147483647`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L19)
  The largest value representable by this integer type.
- **Int32** · [`export const Bits: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L21)
  The number of bits in the integer representation.
- **Int32** · [`export let abs = func(v: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L25)
  Traps (reason 1) for Min, from the negation. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let sign = func(v: int32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L29)
  Returns minus one, zero, or one according to the sign of the value. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let min = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L41)
  Returns the smaller of the two operands. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let max = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L45)
  Returns the larger of the two operands. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let clamp = func(v: int32, low: int32, high: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L49)
  Traps with "invalid argument: low exceeds high". Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let compare = func(a: int32, b: int32) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L64)
  The natural order; usable as a comparator. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let pow = func(base: int32, exponent: int) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L79)
  Square-and-multiply with checked multiplication (an overflow traps with reason 1). The factor is squared only while exponent bits remain, so each intermediate divides the result. A negative exponent traps; pow(0, 0) is 1. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let gcd = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L101)
  Euclid's algorithm in the negative range, where every magnitude fits. gcd(0, 0) is 0. Traps (reason 1) when the result, 2^(Bits - 1), does not fit. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let checked_add = func(a: int32, b: int32) -> Optional<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L120)
  None where the operator would trap. Each test runs before the operation, so nothing overflows. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let checked_sub = func(a: int32, b: int32) -> Optional<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L129)
  Returns the difference in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let checked_mul = func(a: int32, b: int32) -> Optional<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L138)
  Returns the product in Some, or None when it falls outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let checked_div = func(a: int32, b: int32) -> Optional<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L147)
  None for a zero divisor and for Min / -1. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let checked_rem = func(a: int32, b: int32) -> Optional<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L156)
  None for a zero divisor. Min % -1 is 0, as for the operator. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let wrapping_add = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L166)
  The result modulo 2^Bits: the operation runs on the unsigned pattern and converts back (section 7.2). Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let wrapping_sub = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L171)
  Subtracts modulo two raised to the type's bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let wrapping_mul = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L176)
  Multiplies modulo two raised to the type's bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let wrapping_neg = func(a: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L181)
  Min wraps to itself. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let saturating_add = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L186)
  Clamp to Min or Max instead of overflowing. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let saturating_sub = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L198)
  Subtracts and clamps an out-of-range result to the nearest representable bound. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let saturating_mul = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L210)
  Multiplies and clamps an out-of-range result to the nearest representable bound. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let floor_div = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L226)
  The quotient rounded toward negative infinity. A zero divisor and Min / -1 trap (reason 1) in the division. The remainder is recovered by multiplication, which cannot overflow because |quotient * b| <= |a|; a multiplication circuit is several times smaller than a second divider. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let floor_mod = func(a: int32, b: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L239)
  The remainder with the divisor's sign. A zero divisor traps (reason 1). Computed from the quotient, so floor_div and floor_mod share one divider circuit. A divisor of -1 always leaves 0 (Min / -1 would trap, while Min % -1 is 0). Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let count_ones = func(v: int32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L252)
  Bit counts and rotations over the two's complement pattern. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let leading_zeros = func(v: int32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L256)
  Counts zero bits from the most significant end. Zero returns the full bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let trailing_zeros = func(v: int32) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L260)
  Counts zero bits from the least significant end. Zero returns the full bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let rotate_left = func(v: int32, n: int) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L264)
  Rotates the bits left, reducing the distance modulo the bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let rotate_right = func(v: int32, n: int) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L269)
  Rotates the bits right, reducing the distance modulo the bit width. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let to_unsigned = func(v: int32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L274)
  The same-width unsigned type with the same bit pattern (section 7.2). Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let to_signed = func(v: int32) -> int32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L285)
  The identity: every integer module has both conversions. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let hash = func(v: int32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L290)
  Section 5.2: the two's complement pattern as a uint32; the 64-bit types fold the high half into the low half. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let to_string = func(v: int32) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L294)
  Decimal, with `-` for negative values (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let to_string_radix = func(v: int32, radix: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L299)
  Lowercase digits, with `-` for a negative value. A radix outside 2 through 36 traps. The neura.integer.format_i64 kernel writes the digits. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let format = func(v: int32, radix: int, min_width: int, pad: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L305)
  to_string_radix, left-padded with copies of `pad` to `min_width` characters, sign included. A `0` pad goes after the sign. `pad` must be one scalar value. Tagged `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let parse = func(text: string) -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L311)
  Parses a base-10 integer, returning an error for invalid text or a value outside the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Int32** · [`export let parse_radix = func(text: string, radix: int) -> Result<int32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int32.nra#L322)
  An optional `+` or `-`, then one or more digits of the radix in either case. Syntax errors are reported first (Empty, UnexpectedEnd at the end, InvalidCharacter at the offending byte); a well-formed value outside the type fails with OutOfRange at offset 0. A radix outside 2 through 36 traps. The neura.integer.parse_i64 kernel parses the text as int64, and the value is then checked against the type's range. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.2 Equality, hashing, and text

The compiler supplies these results for supported value members. Companion functions such as `Int32.hash` and `Strings.equals` use the same contracts. For types with custom equality, equal values must have the same hash.

| Type | Equal when | Hash | Text |
| --- | --- | --- | --- |
| `bool` | Same value | 1231 for true, 1237 for false | `true` or `false` |
| Integers up to 32 bits | Same value | The value's 32-bit two's complement pattern as a `uint32` | Decimal, `-` for negatives, no leading zeros |
| `int64`, `uint64` | Same value | Low 32 bits exclusive-or high 32 bits | Decimal, as above |
| Floats | Same bit pattern, except that every NaN equals every NaN. `0.0` and `-0.0` are **not** equal. | The bit pattern with every NaN replaced by the canonical NaN; 16-bit patterns zero-extended, the 64-bit pattern folded like `uint64` | [Section 7.5](Stdlib-Design#75-float-text-conversion) |
| Fieldless enum | Same variant | The ordinal | The variant name |
| `string` | Same bytes | Start with 0; for each byte, 31 times the hash plus the byte, wrapping | The string itself |
| Tuple | Same element types and each pair of elements equal, in order | Start with 1; for each element, 31 times the hash plus the element's hash, wrapping | `[`, the elements' text separated by `, `, `]` |
| Array, list | Same kind, same length, and each pair of elements equal in index order | As for a tuple | As for a tuple |
| Class (default, G3b) | Identity | The identity number times 2,654,435,761, wrapping | The qualified class name, `#`, and the identity number, for example `Acme.Shapes.Circle#3` |
| Function value | Not comparable | None | None |
| Payload enum | Not provided | Not provided | Not provided |

Float equality here deliberately differs from `==`: it is an equivalence relation consistent with the hash, which hash tables need, while `==` stays IEEE 754. The multiplier 31 is chosen because multiplying by 31 is a shift and a subtraction.

The module **`Hash`** has the building blocks, all ordinary functions:

| Member | Behavior |
| --- | --- |
| `combine(seed: uint32, value: uint32) → uint32` | 31 times `seed` plus `value`, wrapping. Computed in `uint64`: the product is below 2^37, so nothing overflows before the mask. |
| `spread(hash: uint32) → uint32` | `hash` exclusive-or `hash` shifted right by 16. Hash tables apply this before masking. |
| `of_bool`, `of_int64`, `of_uint64` | The table's results for those types. |

A class's hash starts from 17 and combines each field's hash in declaration order.

**The contract for a class that overrides the G3b members:** `equals` is an equivalence relation; equal values have equal hashes; neither traps nor modifies the value; and while an object is a key in a hash collection its hash and equality do not change. The library does not detect violations.

**Self-containing collections.** A collection's text prints `(this collection)` wherever that text reaches the collection again, so a collection that contains itself, directly or through other collections, has finite text. The marker names the collection whose text was already being built: for a list `a` holding a list `b` that holds `a`, `a.to_string()` is `[[(this collection)]]`.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`.
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

### 9.6 Hash collections

Hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including `IntMap` and `StringMap`.

| Field | Type | Meaning |
| --- | --- | --- |
| `index` | `int` list of length `T` | Open-addressed table. Each slot holds an entry number, -1 (`Empty`), or -2 (`Tombstone`). `T` is a power of two, at least 8. |
| `hashes` | `uint32` list | Each entry's spread hash, in insertion order. Its length is `used`. |
| `live` | `bool` list | False for a removed entry. Same length. |
| `count` | `int` | Live entries. |
| `modifications` | `uint64` | The modification count ([section 5.4](Stdlib-Design#54-iteration)). |

`E`, the entry capacity, is `T / 2`. At most half of the index is ever non-empty, because each entry occupies one slot. One half is chosen because lookups use linear probing: at half load an unsuccessful search inspects about 2.5 slots on average and a successful one about 1.5, and every inspected entry costs a comparison circuit. The index holds only integers, so a sparse table is cheap.

The core's operations, as the owner uses them:

- **Lookup.** The owner computes the spread hash, asks for the home slot (the hash masked with `T - 1`), and walks slots in increasing order, wrapping. `Empty` ends the search: absent. `Tombstone` continues. An entry number whose cached hash equals the probe's hash is a candidate: the owner compares its stored key with the probe key, and stops on a match. The probe key is the first argument to the equality. The loop terminates because at least half the slots are `Empty`.
- **Claim.** For a new key the owner remembers the first `Tombstone` it passed. The core appends the hash and `true` to its lists, writes the new entry number into the remembered tombstone or the final `Empty` slot, and advances `count` and the modification count. The owner appends the key and value to its lists in the same step, so entry numbers agree.
- **Release.** The slot becomes `Tombstone`, the entry's `live` flag becomes false, `count` decreases, and the modification count advances. If the released entry is the last one, it and any dead entries before it are removed from the ends of all the dense lists at once, so stack-like use retains nothing.
- **Compaction.** The core returns the old entry numbers of the live entries in order. The owner builds new key and value lists in that order; the core rebuilds `hashes`, `live`, and `index` by probing from each cached hash for the first `Empty` slot. No hash or equality function is called. It runs in a helper function that returns the new lists ([section 5.5](Stdlib-Design#55-resources)).

When compaction happens:

1. A new key arrives and `used` equals `E`, or the live entries plus the tombstones in the index fill `E` (trailing releases leave tombstones behind). If `count` is greater than `E / 2`, `T` doubles first. A `T` above 2^30 traps with `invalid argument: capacity`.
2. A release leaves more dead entries than live ones and at least 8 dead entries. `T` is unchanged.

**Retention bound.** A removed key and value stay in the owner's dense lists, and so stay reachable, until the next compaction. Rule 2 bounds this: a hash collection never holds more dead entries than `max(count, 7)`. Compaction costs time proportional to `count` and happens after more than `count` releases, so removal stays amortized constant time. Nothing observable depends on the retained references, since reclamation runs no code; only heap use does.

`clear` replaces every list with a new one of the initial size. The table never shrinks on its own; `trim()` compacts at the smallest `T` whose `E` holds `count`.

**Costs.** Lookup, insertion, and removal are expected constant time with well-distributed hashes and linear in `used` in the worst case. There is no conversion of long probe sequences into trees. Code that hashes adversarial keys uses `TreeMap`.

#### The collections

| Class | Keys | Key operations | Needs |
| --- | --- | --- | --- |
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing |
| `StringMap<V>` | `string` | `Strings.hash` and `Strings.equals` | T1 |
| `HashMap<K, V>`, `HashSet<T>` created with `with_keys<K, V>(hash: (K) → uint32, same: (K, K) → bool)` | Any | The two function values, stored in the collection | G1 |
| `HashMap<K, V>`, `HashSet<T>` created with `create<K, V>()` | Any | `hash_code` and `equals` | G3 |

- `IntMap` and `StringMap` are permanent, not stopgaps. They follow the map contract with their key type fixed, call their key operations directly without a function value, and are the right choice for those key types after `HashMap` exists.
- `HashMap` stores its two key operations as fields either way. `create` fills them with the universal members, so the rest of the class is written once.
- `HashSet` owns a `HashCore` and a keys list. `HashMap` owns a `HashCore`, a keys list, and a values list.
- `with_capacity` variants of each constructor choose the smallest power-of-two `T` that is at least `max(8, 2n)`; a negative `n` or one above 2^29 traps.
- Setting a value for an existing key overwrites the value only: the entry keeps its position and the modification count does not change. A key removed and added again goes to the end of the order.
- Iteration visits entry numbers in order and skips dead entries.
- A callback passed to `get_or_put`, `merge`, or `compute` must not modify the map ([section 5.6](Stdlib-Design#56-callbacks)).

### 18.3 Numbers and text building

- **Text of numbers.** Integer and float `to_string` use the NVM's scalar text, which implements [section 7.5](Stdlib-Design#75-float-text-conversion); `StringBuilder.append_int` and the other number appends append that text.
- **Integer modules.** Every module has `to_unsigned` and `to_signed` (the same-signedness one is the identity). `format` counts the sign in the width; a width at or below the text length pads nothing. `parse` reports a syntax error before a range error and accepts leading zeros. `floor_div` and `floor_mod` share one divider circuit.
- **Trap details.** `low exceeds high`, `negative exponent`, `radix outside 2 through 36`, `pad is not one scalar value`, `negative count`, `not a Unicode scalar value`, `byte length out of range`, `not a character boundary`, `zero step`, `index out of range`, `NaN bound`, and `digits outside 0 through 100`, each after `invalid argument: `.
- **Floats.** `min`, `max`, and the rounding functions return the canonical NaN for a NaN operand; `clamp` orders `-0.0` below `0.0`. `to_fixed` and `to_exponential` print `NaN` and the infinities as `to_string` does, keep the sign of a negative value that rounds to zero, and take 0 through 100 digits. `parse` accepts `e` and `E`, rejects `+NaN` and `-NaN`, and treats digits beyond 800 significant digits as a sticky bit. Fast paths (Clinger's for `float64`, a guarded `float64` path for narrower formats) are used only where they are exact; exhaustive tests check every `float16` and `bfloat16` pattern.
- **IntRange** exposes `start()`, `end()`, and `step()` as methods. Equality and hashing use those three values in that order. The text is `start..end:step`. `size()` traps with reason 1 beyond `Int32.Max`.

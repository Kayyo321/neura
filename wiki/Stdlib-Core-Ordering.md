# `Std.Core.Ordering`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Ordering`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Ordering`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ordering** · [`export let Ordering = enum (Less, Equal, Greater)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L8)
  The three outcomes of comparing a left operand to a right operand. Since 0.1.0.
- **Ordering** · [`export let reverse = func(o: Ordering) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L11)
  Swaps Less and Greater while preserving Equal.
- **Ordering** · [`export let then = func(first: Ordering, second: Ordering) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L22)
  `first` unless it is Equal, for comparing by several keys.
- **Ordering** · [`export let is_less = func(o: Ordering) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L26)
  Tests whether the left operand precedes the right.
- **Ordering** · [`export let is_at_most = func(o: Ordering) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L29)
  Tests whether the left operand precedes or equals the right.
- **Ordering** · [`export let is_greater = func(o: Ordering) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L32)
  Tests whether the left operand follows the right.
- **Ordering** · [`export let is_at_least = func(o: Ordering) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/ordering.nra#L35)
  Tests whether the left operand follows or equals the right.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

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

### 7.1 Integer modules

`X` denotes the module's integer type. Parsing uses T1; the other operations use checked integer arithmetic and the modular recipes below.

| Member | Behavior |
| --- | --- |
| Constants `Min`, `Max`, `Bits` | The type's range and width. |
| `abs(v) → X` | Signed types only. Traps (reason 1) for `Min`, from the negation. |
| `sign(v) → int` | -1, 0, or 1. |
| `min(a, b)`, `max(a, b)`, `clamp(v, low, high)` | `clamp` traps with `invalid argument: low exceeds high`. |
| `compare(a, b) → Ordering` | The natural order; a comparator. |
| `pow(base, exponent: int) → X` | Square-and-multiply with checked multiplication. A negative exponent traps. `pow(0, 0)` is 1. |
| `gcd(a, b) → X` | Euclid's algorithm on magnitudes. `gcd(0, 0)` is 0. Traps (reason 1) when the result does not fit. |
| `checked_add`, `checked_sub`, `checked_mul`, `checked_div`, `checked_rem` `→ Optional<X>` | `None` where the operator would trap. Each tests its operands before operating: for addition, `b > Max - a` when both are positive, and the mirror image when both are negative. |
| `wrapping_add`, `wrapping_sub`, `wrapping_mul`, `wrapping_neg` | The result modulo 2^`Bits`, written with checked operators ([section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics)). |
| `saturating_add`, `saturating_sub`, `saturating_mul` | Clamp to `Min` or `Max`. |
| `floor_div(a, b)`, `floor_mod(a, b)` | Quotient rounded toward negative infinity, and the remainder with the divisor's sign. |
| `count_ones`, `leading_zeros`, `trailing_zeros` `→ int` | Bit counts over `Bits` bits, by masks and shifts. |
| `rotate_left(v, n: int)`, `rotate_right(v, n: int)` | `n` is first reduced to 0 through `Bits - 1`. A rotation by 0 returns `v`; otherwise the two shifts have counts between 1 and `Bits - 1`, so neither traps. |
| `to_unsigned(v)`, `to_signed(v)` | The same-width type with the same bit pattern ([section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics)). |
| `hash(v) → uint32` | [Section 5.2](Stdlib-Design#52-equality-hashing-and-text). |
| `to_string(v) → string` | Decimal. Built by concatenating one-character literals, most significant digit first; negative values are handled in the negative range so `Min` needs no negation. |
| `to_string_radix(v, radix: int) → string` | Lowercase digits. The kernel `neura.integer.format_i64` or `format_u64` writes them (P2a). |
| `format(v, radix: int, min_width: int, pad: string) → string` | As `to_string_radix`, then left-padded with copies of `pad` to `min_width` digits and sign. When `pad` is `0`, padding goes after the sign. |
| `parse(text) → Result<X>`, `parse_radix(text, radix: int) → Result<X>` | **T1.** Grammar below. The kernel `neura.integer.parse_i64` or `parse_u64` parses the text as a 64-bit value, which a narrower type then checks against its range (P2a). |

**Parsing grammar.** An optional `+` or `-`, then one or more digits of the radix, letters in either case. No whitespace, separators, or radix prefix. An empty input fails with kind `Empty`; a sign with no digits with `UnexpectedEnd`; any other byte with `InvalidCharacter` at its offset; a value outside the type with `OutOfRange` at offset 0. A `-` on an unsigned type is accepted only for zero. The parser accumulates in the negative range for signed types, so `Min` parses without overflow.

### 7.3 Float modules

The float companions use N1 for bit-level operations. `is_nan` can also use ordinary float comparison because a NaN differs from itself.

| Member | Behavior |
| --- | --- |
| Constants `Max`, `MinPositive`, `MinNormal`, `Epsilon`, `Infinity`, `NegativeInfinity`, `NaN` | Construct exact boundary and special encodings with `from_bits`. Use the format's canonical quiet NaN. |
| `is_nan`, `is_infinite`, `is_finite`, `is_negative` | `is_negative` reads the sign bit, so it is true for `-0.0`. |
| `abs`, `copy_sign(magnitude, sign)` | Sign-bit operations; never round. |
| `min(a, b)`, `max(a, b)` | NaN if either operand is NaN. `-0.0` is less than `0.0`. |
| `clamp(v, low, high)` | Traps when `low` exceeds `high` or either is NaN. |
| `floor`, `ceil`, `trunc`, `round`, `round_even` | `round` rounds halves away from zero. All preserve the sign of zero. |
| `to_bits(v)`, `from_bits(bits)` | N1 itself. |
| `total_compare(a, b) → Ordering` | The total order of [section 5.3](Stdlib-Design#53-ordering). |
| `equals(a, b)`, `hash(v)` | [Section 5.2](Stdlib-Design#52-equality-hashing-and-text). |
| `to_string(v)`, `to_fixed(v, digits: int)`, `to_exponential(v, digits: int)` | [Section 7.5](Stdlib-Design#75-float-text-conversion). `to_fixed` gives exactly `digits` fraction digits (0 through 100), correctly rounded, ties to even. |
| `parse(text) → Result<X>` | Also T1. |

### 7.4 `Bool`

`to_int(v) → int` gives 1 or 0; `xor(a, b)`; `compare(a, b) → Ordering`; `hash(v)`; `to_string(v)`. `parse(text) → Result<bool>` accepts exactly `true` and `false` and needs T1.

### 8.1 `Strings`

All of `Strings` needs T1. String operations are functions that take the string first. All positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

| Member | Behavior |
| --- | --- |
| `byte_length(s) → int` | Constant time. |
| `scalar_count(s) → int` | Counts bytes that are not continuation bytes. Linear. |
| `is_empty(s) → bool` | |
| `byte_at(s, offset) → uint8` | |
| `scalar_at(s, offset) → uint32` | Decodes the character starting at `offset`. |
| `is_boundary(s, offset) → bool` | True for 0, for the byte length, and for any offset whose byte is not a continuation byte. False out of range. |
| `scalars(s)` | A built-in list of `uint32`: the scalar values in order. For-each works on it directly. |
| `scalar_offsets(s)` | A built-in list of `[int, uint32]`: each starting offset with its scalar value. |
| `slice(s, start, end) → string`, `slice_from(s, start) → string` | Both ends on boundaries, `start` not above `end`. |
| `equals(a, b) → bool`, `hash(s) → uint32`, `compare(a, b) → Ordering` | [Sections 5.2](Stdlib-Design#52-equality-hashing-and-text) and [5.3](Stdlib-Design#53-ordering). |
| `starts_with(s, prefix)`, `ends_with(s, suffix)`, `contains(s, needle)` `→ bool` | The empty string is a prefix, suffix, and substring of every string. |
| `index_of(s, needle) → int`, `index_of_from(s, needle, from) → int` | Offset of the first occurrence at or after `from`, or -1. An empty needle matches at `from`. |
| `last_index_of(s, needle) → int` | Offset of the last occurrence, or -1. |
| `split(s, separator)` | A built-in list of strings: every piece between occurrences, left to right without overlap. Empty pieces are kept, so `a,,b,` gives four pieces and the empty string gives one. An empty separator traps. |
| `split_limit(s, separator, limit)` | At most `limit` pieces; the last holds the remainder. `limit` below 1 traps. |
| `lines(s)` | Splits at LF; a CR immediately before an LF is removed; a final empty piece after a trailing LF is dropped. |
| `join(parts: string[list], separator) → string` | |
| `repeat(s, count) → string` | A negative count traps. |
| `trim(s)`, `trim_start(s)`, `trim_end(s)` | Remove space, tab, LF, vertical tab, form feed, and CR. |
| `strip_prefix(s, prefix)`, `strip_suffix(s, suffix)` `→ Optional<string>` | The remainder when the affix is present. |
| `replace(s, target, replacement)`, `replace_first(...)` | Every non-overlapping occurrence, left to right. An empty target traps. |
| `to_ascii_upper(s)`, `to_ascii_lower(s)`, `equals_ignore_ascii_case(a, b)` | Change or ignore only the letters A through Z and a through z. |
| `pad_start(s, width, pad: string)`, `pad_end(...)` | Pads to `width` scalar values with copies of `pad`, which must be one scalar value. |
| `reverse(s) → string` | Reverses the order of scalar values. |
| `to_bytes(s)` | A built-in list of `uint8`, a copy. |
| `from_bytes(bytes: uint8[list]) → Result<string>` | `Encoding` error with the offset of the first invalid byte. |
| `from_bytes_lossy(bytes: uint8[list]) → string` | Each maximal invalid sequence becomes one U+FFFD. |
| `from_scalars(scalars: uint32[list]) → string` | Traps for a value that is not a Unicode scalar value. |

Functions that return several strings return a built-in list, not an `ArrayList`: the built-in for-each works on it, and `Std.Text` then needs nothing from `Std.Collections`.

**Search algorithm.** The searches, comparisons, trims, splits, `lines`, replacements, ASCII case mappings, `scalar_count`, and `hash` run on the `neura.text`, `neura.hash`, and `neura.encoding` kernels (P2a). The text searches use the two-way string-matching algorithm, so a search is linear in the length of the text it reads plus the needle's length, never their product. A kernel is charged by the work its arguments allow (P2a), so the steps of each member are linear: `index_of` and `index_of_from` search in windows that start at 128 bytes and double, so their steps grow with the distance from `from` to the occurrence rather than with the rest of the text, and a loop that calls `index_of_from` after each occurrence is linear in the text; `split`, `split_limit`, and `lines` find every separator in two kernel calls over the text (one counts, one writes the offsets) and then slice the pieces, so they are linear in the text plus the number of pieces.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator, the `neura.encoding.utf8_check` and `utf8_validate` kernels, must agree with the validation NVM performs when constructing text; its Neura original is the oracle of the `strings` and `utf8` tests.

**Unicode scope.** `Std.Text.Strings` handles UTF-8 encoding, decoding, and iteration for all Unicode scalar values. Its original ASCII case and classification methods keep their scope. The [`String` companion](StandardLibrary#string) adds a fixed whitespace set for strip and indentation, Unicode 17.0.0 case conversion and folding, and standard charset conversion from `Std.Text.Charsets`. These are ordinary Neura routines with embedded Unicode data and use no host locale or charset services.

### 9.1 Contracts

This section fixes the member sets that the concrete classes and the interfaces in [section 9.10](Stdlib-Design#910-interfaces-and-read-only-views) share.

The **Needs** column names the compiler facilities of [section 3](Stdlib-Design#3-language-facilities) that a member uses.

**List contract** (`ArrayList`, `LinkedList`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size() → int`, `is_empty() → bool` | | |
| `get(index) → T`, `set(index, value)` | `set` is not structural. | |
| `first() → T`, `last() → T` | Trap with `no such element` when empty. | |
| `add(value)`, `add_at(index, value)`, `add_all(items: T[list])` | `add` appends. `add_at` accepts an index equal to the size. | |
| `remove_at(index) → T`, `remove_first() → T`, `remove_last() → T` | The last two trap when empty. | |
| `swap(i, j)`, `reverse()`, `fill(value)`, `truncate(size)`, `clear()` | `truncate` traps for a size above the current size. | |
| `slice(start, end)` | A new collection of the same class. It copies; there are no sublist views. | |
| `to_list()` | A new built-in list in order. | |
| `iterator()` | [Section 5.4](Stdlib-Design#54-iteration). | |
| `first_or_none()`, `last_or_none()` `→ Optional<T>` | | G2 |
| `for_each(action: (T) → void)` | In order. | G1 |
| `remove_if(drop: (T) → bool) → int`, `retain_if(keep: (T) → bool) → int` | The callback runs exactly once per element, in order. Returns how many were removed. | G1 |
| `index_where(test: (T) → bool) → int` | The first matching index, or -1. | G1 |
| `sort(c: (T, T) → Ordering)`, `binary_search(value, c) → int` | [Section 9.11](Stdlib-Design#911-sorting-and-searching). | G1 |
| `sequence() → Sequence<T>` | | G1 |
| `contains(value) → bool`, `index_of(value) → int`, `last_index_of(value) → int`, `remove(value) → bool` | By `equals`. | G3 |
| `equals(other)`, `hash_code()`, `to_string()` | [Section 9.2](Stdlib-Design#92-equality-of-collections). | G3 |

**Queue and deque contracts** (`ArrayDeque`, `LinkedList`; the queue part also `PriorityQueue`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size()`, `is_empty()`, `clear()`, `to_list()`, `iterator()` | | |
| `enqueue(value)`, `dequeue() → T` | `dequeue` traps when empty. For a deque, `enqueue` is `add_last` and `dequeue` is `remove_first`. | |
| `add_first(value)`, `add_last(value)`, `remove_first() → T`, `remove_last() → T`, `first() → T`, `last() → T` | Deques only. | |
| `peek() → Optional<T>`, `try_dequeue() → Optional<T>` | | G2 |

**Map contract** (`HashMap`, `TreeMap`, `IntMap`, `StringMap`, `OrdinalMap`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size()`, `is_empty()`, `clear()` | | |
| `contains_key(key) → bool` | | |
| `get_or(key, fallback: V) → V` | | |
| `require(key) → V` | Traps with `no such element` when the key is absent. | |
| `set(key, value)` | Associates the value, replacing any previous one. | |
| `delete(key) → bool` | Removes the key; returns whether it was present. | |
| `keys()`, `values()`, `entries()` | New built-in lists of `K`, of `V`, and of `[K, V]`, in iteration order. They are copies. | |
| `iterator()` | Yields `[K, V]`. | |
| `put_all(other)` | From a map of the same class, in its iteration order. | |
| `get(key) → Optional<V>`, `put(key, value) → Optional<V>`, `remove(key) → Optional<V>` | `put` and `remove` return the previous value. | G2 |
| `for_each(action: (K, V) → void)`, `remove_if(drop: (K, V) → bool) → int` | | G1 |
| `get_or_put(key, make: () → V) → V` | Returns the existing value, or stores and returns the new one. | G1 |
| `merge(key, value, combine: (V, V) → V) → V` | Stores `value` when absent, otherwise the combination of old and new. | G1 |
| `compute(key, f: (K, Optional<V>) → Optional<V>) → Optional<V>` | Stores a `Some` result and removes the key for `None`. | G1, G2 |
| `contains_value(value) → bool`, `equals`, `hash_code`, `to_string` | | G3 |

`set` and `delete` avoid constructing an `Optional` when the caller does not need the previous value.

**Set contract** (`HashSet`, `TreeSet`, `BitSet`): `size`, `is_empty`, `clear`, `contains(value)`, `add(value) → bool` (false for a duplicate), `remove(value) → bool`, `to_list`, `iterator`, `add_all(items: T[list])`, and `is_subset_of`, `is_disjoint_from`, `union`, `intersection`, `difference` with a set of the same class (each of the last three returns a new set of that class: this set's elements first, then for `union` the other's new elements). With G1: `for_each`, `remove_if`, `retain_if`. With G3: `equals`, `hash_code`, `to_string`.

**Sorted contract** (`TreeSet`, `TreeMap`): `first`, `last` (trap when empty), `range(from, to)` (a built-in list of the elements or entries from `from` inclusive to `to` exclusive, ascending), `descending()` (a built-in list in descending order), and `remove_first`, `remove_last`. With G2: `floor`, `ceiling`, `lower`, `higher`, and for maps the `_entry` forms, each returning `Optional`.

### 9.11 Sorting and searching

The module `Sorting`. Generic sorting needs G1 for the comparator.

**`sort<T>(items: T[list], c: (T, T) → Ordering)`** is stable:

1. Sort each consecutive block of 16 elements (the last may be shorter) in place with binary insertion sort: for each element, binary-search the sorted prefix for the position after the last element not `Greater` than it, and shift the tail right by one.
2. Create a buffer: a second built-in list of the same length, filled by appending the elements themselves. They are references the list already holds, so the buffer keeps nothing extra alive, and no "empty" value is needed.
3. Merge bottom-up. For widths 16, 32, 64, and so on while the width is less than the length, merge each adjacent pair of runs from the source list into the destination, then swap the two lists' roles. If the last element of the left run is not `Greater` than the first of the right run, the pair is copied without comparisons. When merging, take from the right run only when its head is `Less` than the left run's head; this makes the sort stable.
4. If the result ended in the buffer, write it back to `items` by index. Element writes do not change the list's structural version.

It makes at most about `n log2 n` comparator calls. `ArrayList.sort` calls it on `storage`. `sort_range(items, start, end, c)` sorts part of a list.

**`sort_ascending(items)`** sorts an `int64`, `uint64`, `float64`, `int32`, or `uint32` list in ascending order, with the result `sort` gives with the type's `compare` (`Float64.total_compare` for `float64`, so `-0.0` comes before `0.0` and every NaN comes last, in its original order). The kernels `neura.sort.i64`, `u64`, and `f64` do the work (P2a) for one step per eight element comparisons; an `int32` or `uint32` list is widened into a new list for the kernel and written back by index. Like `sort`, it writes elements and leaves the list's version unchanged. The comparator `sort` stays in Neura, because a kernel cannot call back into the program.

**`binary_search<T>(items: T[list], value: T, c) → int`** requires `items` sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index, the first of any equal run. Otherwise it returns `-(insertion point) - 1`.

The module **`Lists`** has helpers for built-in lists that need no callback: `copy<T>(items)`, `concat<T>(a, b)`, `reverse<T>(items)`, `swap<T>(items, i, j)`, `fill<T>(items, value)`, `insert_at<T>(items, index, value)`, `truncate<T>(items, length)`, `clear<T>(items)`, `filled<T>(count, value)`, and `from_array<T>(items: T[])`. There is no conversion from a list to an array, because an array's length must come from a literal.

### 10.2 `BigInteger`

An immutable arbitrary-precision integer. Parsing uses T1 and conversion to `float64` uses N1.

- **Layout.** `sign` (`int`: -1, 0, or 1) and `magnitude`, a built-in `uint32` list of limbs, least significant first, with no zero limb at the top. Zero has sign 0 and an empty magnitude. The form is unique, so equality compares the fields. No method changes the list after construction.
- **Limits.** At most 2^26 limbs; a larger result traps with `invalid argument: magnitude too large`.
- **Module functions.** The constants `Zero`, `One`, `Ten`; `of_int64`, `of_uint64`; `parse(text)` and `parse_radix(text, radix)` returning `Result<BigInteger>`, with the grammar of [section 7.1](Stdlib-Design#71-integer-modules) and no range failure.

| Operation | Algorithm |
| --- | --- |
| `add`, `subtract` | Limb-wise with carry or borrow held in `uint64`; signs handled by comparing magnitudes. |
| `multiply` | Schoolbook with `uint64` partial products, which fit by the limb row of [section 7.2](Stdlib-Design#72-arithmetic-without-wrapping-intrinsics). When both operands have at least 48 limbs, Karatsuba: split at half of the larger length and recurse. |
| `divide`, `remainder` | Knuth's Algorithm D in base 2^32. The quotient truncates toward zero and the remainder takes the dividend's sign, matching the integer operators. A single-limb divisor uses the short division loop. A zero divisor traps (reason 1), from the division it performs. |
| `divide_and_remainder(other) → [BigInteger, BigInteger]` | Both results from one division, as a tuple. |
| `floor_mod(m)` | Non-negative result for a positive modulus. |
| `pow(exponent: int)` | Square and multiply. A negative exponent traps. |
| `gcd(other)` | Euclid's algorithm with `remainder`; the result is non-negative. |
| `mod_pow(exponent, modulus)` | Left-to-right binary exponentiation, reducing after each step. A negative exponent or a non-positive modulus traps. |
| `mod_inverse(modulus) → BigInteger` | Extended Euclid. Returns `nil` when no inverse exists. |
| `negate`, `abs`, `sign`, `is_zero`, `is_even` | |
| `shift_left(n)`, `shift_right(n)` | `shift_right` rounds toward negative infinity. A negative `n` traps. Limb shifts use counts from 1 through 31, so the built-in shifts never trap. |
| `bit_length() → int`, `test_bit(n) → bool` | Of the magnitude. |
| `compare_to(other) → Ordering`, `equals(other)`, `hash_code()` | Hash: start at 0, combine each limb from most to least significant, then negate (wrapping) for a negative value. |
| `to_int64() → Optional<int64>`, `to_float64() → float64` | `to_float64` rounds to nearest, ties to even, and gives an infinity beyond the range. |
| `to_string()`, `to_string_radix(radix)` | Radix 10 divides repeatedly by 10^9 and joins nine-digit groups. |

`BigInteger` has no bitwise and, or, or exclusive or.
